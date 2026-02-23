/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/toolbar/allow2_button.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/app/brave_command_ids.h"
#include "brave/browser/allow2/allow2_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "components/grit/brave_components_resources.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/controls/menu/menu_runner.h"

namespace allow2 {

namespace {

// Button size constants.
constexpr int kButtonSize = 28;
constexpr int kAvatarSize = 24;

// Default avatar colors (10 colors for variety).
constexpr size_t kNumAvatarColors = 10;
constexpr std::array<SkColor, kNumAvatarColors> kAvatarColors = {{
    SkColorSetRGB(0xE9, 0x1E, 0x63),  // Pink
    SkColorSetRGB(0x9C, 0x27, 0xB0),  // Purple
    SkColorSetRGB(0x67, 0x3A, 0xB7),  // Deep Purple
    SkColorSetRGB(0x3F, 0x51, 0xB5),  // Indigo
    SkColorSetRGB(0x21, 0x96, 0xF3),  // Blue
    SkColorSetRGB(0x00, 0x96, 0x88),  // Teal
    SkColorSetRGB(0x4C, 0xAF, 0x50),  // Green
    SkColorSetRGB(0xFF, 0x98, 0x00),  // Orange
    SkColorSetRGB(0xFF, 0x57, 0x22),  // Deep Orange
    SkColorSetRGB(0x79, 0x55, 0x48),  // Brown
}};

// Creates a circular avatar image with initials.
class AvatarImageSource : public gfx::CanvasImageSource {
 public:
  AvatarImageSource(const std::u16string& initials, SkColor bg_color, int size)
      : gfx::CanvasImageSource(gfx::Size(size, size)),
        initials_(initials),
        bg_color_(bg_color) {}

  void Draw(gfx::Canvas* canvas) override {
    // Draw circular background.
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(bg_color_);
    flags.setStyle(cc::PaintFlags::kFill_Style);

    gfx::Rect bounds(size());
    int radius = std::min(bounds.width(), bounds.height()) / 2;
    canvas->DrawCircle(bounds.CenterPoint(), radius, flags);

    // Draw initials.
    if (!initials_.empty()) {
      gfx::FontList font_list =
          gfx::FontList().DeriveWithSizeDelta(2).DeriveWithWeight(
              gfx::Font::Weight::BOLD);
      canvas->DrawStringRectWithFlags(
          initials_, font_list, SK_ColorWHITE, bounds,
          gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_SUBPIXEL_RENDERING);
    }
  }

 private:
  std::u16string initials_;
  SkColor bg_color_;
};

// Creates a "?" icon for unauthenticated state.
class QuestionMarkImageSource : public gfx::CanvasImageSource {
 public:
  explicit QuestionMarkImageSource(int size)
      : gfx::CanvasImageSource(gfx::Size(size, size)) {}

  void Draw(gfx::Canvas* canvas) override {
    // Draw circular background.
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetRGB(0x9E, 0x9E, 0x9E));  // Grey
    flags.setStyle(cc::PaintFlags::kFill_Style);

    gfx::Rect bounds(size());
    int radius = std::min(bounds.width(), bounds.height()) / 2;
    canvas->DrawCircle(bounds.CenterPoint(), radius, flags);

    // Draw "?".
    gfx::FontList font_list =
        gfx::FontList().DeriveWithSizeDelta(4).DeriveWithWeight(
            gfx::Font::Weight::BOLD);
    canvas->DrawStringRectWithFlags(
        u"?", font_list, SK_ColorWHITE, bounds,
        gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_SUBPIXEL_RENDERING);
  }
};

}  // namespace

// Simple menu model for the dropdown.
class Allow2MenuModel : public ui::SimpleMenuModel,
                        public ui::SimpleMenuModel::Delegate {
 public:
  explicit Allow2MenuModel(Allow2Button* button)
      : ui::SimpleMenuModel(this), button_(button) {
    AddItem(IDC_ALLOW2_SWITCH_USER, u"Switch User");
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == IDC_ALLOW2_SWITCH_USER) {
      button_->OnSwitchUserSelected();
    }
  }

 private:
  raw_ptr<Allow2Button> button_;
};

Allow2Button::Allow2Button(Browser* browser, Profile* profile)
    : ToolbarButton(base::BindRepeating(&Allow2Button::OnButtonPressed,
                                        base::Unretained(this))),
      browser_(browser),
      profile_(profile) {
  // Configure as menu button.
  auto menu_button_controller = std::make_unique<views::MenuButtonController>(
      this,
      base::BindRepeating(&Allow2Button::ShowMenu, base::Unretained(this)),
      std::make_unique<views::Button::DefaultButtonControllerDelegate>(this));
  menu_button_controller_ = menu_button_controller.get();
  SetButtonController(std::move(menu_button_controller));

  SetTooltipText(u"Allow2 User");
  SetAccessibleName(u"Allow2 User");

  // Register as observer.
  Allow2Service* service = GetService();
  if (service) {
    service->AddObserver(this);
    current_child_ = service->GetCurrentChild();
  }

  UpdateAppearance();
}

Allow2Button::~Allow2Button() {
  Allow2Service* service = GetService();
  if (service) {
    service->RemoveObserver(this);
  }
}

void Allow2Button::UpdateAppearance() {
  gfx::ImageSkia image;

  if (current_child_.has_value()) {
    // Authenticated - show user avatar.
    // TODO: If avatar_url is set, load and display the remote image.
    // For now, use initials with color as fallback.
    if (!current_child_->avatar_url.empty()) {
      // TODO: Implement async avatar loading from URL.
      // For now, fall through to initials.
    }

    // Use initials with color as avatar.
    std::u16string initials = GetInitials(current_child_->name);
    SkColor color = GetAvatarColor(current_child_->id);
    image = gfx::ImageSkia(
        std::make_unique<AvatarImageSource>(initials, color, kAvatarSize),
        gfx::Size(kAvatarSize, kAvatarSize));

    std::u16string name = base::UTF8ToUTF16(current_child_->name);
    SetTooltipText(u"Allow2: " + name + u" (click to switch user)");
    SetAccessibleName(u"Allow2 User: " + name);
  } else {
    // Not authenticated - show Allow2 hand logo.
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    const gfx::ImageSkia* logo = rb.GetImageSkiaNamed(IDR_ALLOW2_LOGO);
    if (logo) {
      // Scale the logo to button size.
      image = gfx::ImageSkiaOperations::CreateResizedImage(
          *logo, skia::ImageOperations::RESIZE_BEST,
          gfx::Size(kAvatarSize, kAvatarSize));
    } else {
      // Fallback to "?" if logo not found.
      image = gfx::ImageSkia(
          std::make_unique<QuestionMarkImageSource>(kAvatarSize),
          gfx::Size(kAvatarSize, kAvatarSize));
    }

    SetTooltipText(u"Allow2: Select user to start");
    SetAccessibleName(u"Allow2: Not logged in");
  }

  SetImage(views::Button::STATE_NORMAL, image);
  SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));

  // Update visibility based on pairing state.
  SetVisible(ShouldBeVisible());
}

bool Allow2Button::ShouldBeVisible() const {
  Allow2Service* service = GetService();
  if (!service) {
    return false;
  }
  return service->IsPaired() && service->IsEnabled();
}

void Allow2Button::OnThemeChanged() {
  ToolbarButton::OnThemeChanged();
  UpdateAppearance();
}

void Allow2Button::OnCurrentChildChanged(const std::optional<Child>& child) {
  current_child_ = child;
  UpdateAppearance();
}

void Allow2Button::OnPairedStateChanged(bool is_paired) {
  UpdateAppearance();
}

void Allow2Button::OnButtonPressed(const ui::Event& event) {
  ShowMenu();
}

void Allow2Button::ShowMenu() {
  auto menu_model = std::make_unique<Allow2MenuModel>(this);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      std::move(menu_model),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);

  gfx::Rect anchor_bounds = GetAnchorBoundsInScreen();
  menu_runner_->RunMenuAt(
      GetWidget(), menu_button_controller_, anchor_bounds,
      views::MenuAnchorPosition::kTopRight, ui::MENU_SOURCE_NONE);
}

void Allow2Button::OnSwitchUserSelected() {
  Allow2Service* service = GetService();
  if (service) {
    service->ClearCurrentChild();
    // The observer will be notified, which will trigger the child shield
    // to appear via the tab helper.
  }
}

Allow2Service* Allow2Button::GetService() const {
  if (!profile_) {
    return nullptr;
  }
  return Allow2ServiceFactory::GetForProfile(profile_);
}

// static
std::u16string Allow2Button::GetInitials(const std::string& name) {
  std::u16string initials;
  std::u16string name16 = base::UTF8ToUTF16(name);

  bool in_word = false;
  int word_count = 0;
  for (char16_t c : name16) {
    if (c == ' ' || c == '\t') {
      in_word = false;
    } else if (!in_word) {
      in_word = true;
      word_count++;
      if (word_count <= 2) {
        initials += std::towupper(c);
      }
    }
  }

  if (initials.empty() && !name16.empty()) {
    initials = std::u16string(1, std::towupper(name16[0]));
  }

  return initials;
}

// static
SkColor Allow2Button::GetAvatarColor(uint64_t child_id) {
  // Use child ID to pick a consistent color.
  // child_id 0 = parent, use a distinct grey-blue.
  if (child_id == 0) {
    return SkColorSetRGB(0x60, 0x7D, 0x8B);  // Blue Grey
  }
  return kAvatarColors[child_id % kNumAvatarColors];
}

BEGIN_METADATA(Allow2Button)
END_METADATA

}  // namespace allow2

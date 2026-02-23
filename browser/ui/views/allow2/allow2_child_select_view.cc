/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/allow2/allow2_child_select_view.h"

#include <array>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/grit/brave_components_resources.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/widget/widget.h"

namespace allow2 {

namespace {

// Layout constants.
constexpr int kDialogWidth = 450;
constexpr int kDialogPadding = 40;
constexpr int kElementSpacing = 16;
constexpr int kChildButtonSpacing = 12;
constexpr int kChildButtonPadding = 16;
constexpr int kChildButtonWidth = 100;
constexpr int kChildButtonHeight = 100;
constexpr int kPinDigitFieldWidth = 40;
constexpr int kPinDigitFieldHeight = 48;
constexpr int kPinDigitSpacing = 8;
constexpr int kTitleFontSize = 22;
constexpr int kChildNameFontSize = 14;
constexpr int kPinDigitFontSize = 20;
constexpr int kAvatarSize = 48;
constexpr int kLogoSize = 64;

// Colors.
constexpr SkColor kBackgroundColor = SkColorSetRGB(0xFA, 0xFA, 0xFA);
constexpr SkColor kTitleColor = SkColorSetRGB(0x33, 0x33, 0x33);
constexpr SkColor kTextColor = SkColorSetRGB(0x66, 0x66, 0x66);
constexpr SkColor kChildButtonColor = SkColorSetRGB(0xE8, 0xE8, 0xE8);
constexpr SkColor kChildButtonSelectedColor = gfx::kGoogleBlue100;
constexpr SkColor kChildButtonBorderColor = gfx::kGoogleBlue600;
constexpr SkColor kChildButtonDisabledColor = SkColorSetRGB(0xCC, 0xCC, 0xCC);
constexpr SkColor kLockoutTextColor = SkColorSetRGB(0xB0, 0x30, 0x30);
constexpr SkColor kErrorColor = SkColorSetRGB(0xE0, 0x4B, 0x4B);

// Lockout timer interval (1 second for countdown updates).
constexpr base::TimeDelta kLockoutTimerInterval = base::Seconds(1);

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

// Singleton tracking for IsShowing().
static bool g_is_showing = false;

}  // namespace

// Allow2LogoView - The Allow2 yellow hand logo.
// Uses the PNG logo from Allow2 iOS assets (100x100).
class Allow2LogoView : public views::ImageView {
  METADATA_HEADER(Allow2LogoView, views::ImageView)

 public:
  Allow2LogoView() {
    // Load the Allow2 logo from resources.
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    const gfx::ImageSkia* logo = rb.GetImageSkiaNamed(IDR_ALLOW2_LOGO);
    if (logo) {
      // Scale the 100x100 logo to desired display size.
      gfx::ImageSkia scaled = gfx::ImageSkiaOperations::CreateResizedImage(
          *logo, skia::ImageOperations::RESIZE_BEST,
          gfx::Size(kLogoSize, kLogoSize));
      SetImage(ui::ImageModel::FromImageSkia(scaled));
    }
    SetPreferredSize(gfx::Size(kLogoSize, kLogoSize));
  }
};

BEGIN_METADATA(Allow2LogoView)
END_METADATA

// RoundAvatarView - A circular view with initials and background color.
class RoundAvatarView : public views::View {
  METADATA_HEADER(RoundAvatarView, views::View)

 public:
  RoundAvatarView(const std::u16string& initials, SkColor bg_color)
      : initials_(initials), bg_color_(bg_color) {
    SetPreferredSize(gfx::Size(kAvatarSize, kAvatarSize));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // Draw circular background.
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(bg_color_);
    flags.setStyle(cc::PaintFlags::kFill_Style);

    gfx::Rect bounds = GetLocalBounds();
    int radius = std::min(bounds.width(), bounds.height()) / 2;
    canvas->DrawCircle(bounds.CenterPoint(), radius, flags);

    // Draw initials.
    if (!initials_.empty()) {
      gfx::FontList font_list =
          gfx::FontList().DeriveWithSizeDelta(6).DeriveWithWeight(
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

BEGIN_METADATA(RoundAvatarView)
END_METADATA

// ChildButton implementation.

// static
std::u16string Allow2ChildSelectView::ChildButton::GetInitials(
    const std::string& name) {
  std::u16string initials;
  std::u16string name16 = base::UTF8ToUTF16(name);

  // Get first letter of first two words.
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

  // If only one letter, just use that.
  if (initials.empty() && !name16.empty()) {
    initials = std::u16string(1, std::towupper(name16[0]));
  }

  return initials;
}

// static
SkColor Allow2ChildSelectView::ChildButton::GetAvatarColor(const Child& child) {
  // If child has an assigned color, use it.
  if (!child.color.empty()) {
    // Try to parse hex color (e.g., "#FF5733").
    if (child.color[0] == '#' && child.color.length() == 7) {
      uint32_t hex_value = 0;
      if (base::HexStringToUInt(child.color.substr(1), &hex_value)) {
        return SkColorSetRGB((hex_value >> 16) & 0xFF,
                             (hex_value >> 8) & 0xFF,
                             hex_value & 0xFF);
      }
    }
    // Try to parse as color index (0-9).
    int index = 0;
    if (base::StringToInt(child.color, &index) && index >= 0 &&
        static_cast<size_t>(index) < kNumAvatarColors) {
      return kAvatarColors.at(static_cast<size_t>(index));
    }
  }

  // Default: use child ID to pick a consistent color.
  return kAvatarColors.at(child.id % kNumAvatarColors);
}

std::unique_ptr<views::View> Allow2ChildSelectView::ChildButton::CreateAvatarView(
    const Child& child) {
  // TODO: If avatar_url is set, load and display the image.
  // For now, use initials with color.
  std::u16string initials = GetInitials(child.name);
  SkColor color = GetAvatarColor(child);
  return std::make_unique<RoundAvatarView>(initials, color);
}

Allow2ChildSelectView::ChildButton::ChildButton(
    const Child& child,
    base::RepeatingCallback<void(uint64_t)> on_click)
    : child_id_(child.id), on_click_(std::move(on_click)) {
  // Increase height to accommodate lockout label.
  SetPreferredSize(gfx::Size(kChildButtonWidth, kChildButtonHeight + 20));
  SetBackground(views::CreateRoundedRectBackground(kChildButtonColor, 8));
  SetBorder(views::CreateRoundedRectBorder(2, 8, kChildButtonColor));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kChildButtonPadding / 2, kChildButtonPadding / 2), 4));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Round avatar with initials and color.
  avatar_view_ = AddChildView(CreateAvatarView(child));

  // Child name.
  name_label_ = AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(child.name)));
  name_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kChildNameFontSize - gfx::FontList().GetFontSize()));
  name_label_->SetEnabledColor(kTextColor);
  name_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  name_label_->SetMaximumWidth(kChildButtonWidth - kChildButtonPadding);
  name_label_->SetElideBehavior(gfx::ELIDE_TAIL);

  // Lockout countdown label (initially hidden).
  lockout_label_ = AddChildView(std::make_unique<views::Label>(u""));
  lockout_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(-4));
  lockout_label_->SetEnabledColor(kLockoutTextColor);
  lockout_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  lockout_label_->SetVisible(false);

  // Make clickable.
  SetFocusBehavior(FocusBehavior::ALWAYS);
}

Allow2ChildSelectView::ChildButton::~ChildButton() = default;

void Allow2ChildSelectView::ChildButton::SetSelected(bool selected) {
  selected_ = selected;
  if (locked_out_) {
    // Don't change appearance if locked out.
    return;
  }
  if (selected) {
    SetBackground(views::CreateRoundedRectBackground(kChildButtonSelectedColor, 8));
    SetBorder(views::CreateRoundedRectBorder(2, 8, kChildButtonBorderColor));
  } else {
    SetBackground(views::CreateRoundedRectBackground(kChildButtonColor, 8));
    SetBorder(views::CreateRoundedRectBorder(2, 8, kChildButtonColor));
  }
  SchedulePaint();
}

void Allow2ChildSelectView::ChildButton::SetLockedOut(bool locked_out,
                                                       int remaining_seconds) {
  locked_out_ = locked_out;

  if (locked_out) {
    // Grey out the button and show countdown.
    SetBackground(views::CreateRoundedRectBackground(kChildButtonDisabledColor, 8));
    SetBorder(views::CreateRoundedRectBorder(2, 8, kChildButtonDisabledColor));
    SetFocusBehavior(FocusBehavior::NEVER);

    // Dim the name.
    if (name_label_) {
      name_label_->SetEnabledColor(SkColorSetA(kTextColor, 128));
    }

    // Show lockout countdown.
    if (lockout_label_) {
      lockout_label_->SetText(FormatLockoutTime(remaining_seconds));
      lockout_label_->SetVisible(true);
    }
  } else {
    // Restore normal appearance.
    SetBackground(views::CreateRoundedRectBackground(kChildButtonColor, 8));
    SetBorder(views::CreateRoundedRectBorder(2, 8, kChildButtonColor));
    SetFocusBehavior(FocusBehavior::ALWAYS);

    // Restore name color.
    if (name_label_) {
      name_label_->SetEnabledColor(kTextColor);
    }

    // Hide lockout countdown.
    if (lockout_label_) {
      lockout_label_->SetVisible(false);
    }
  }

  SchedulePaint();
}

// static
std::u16string Allow2ChildSelectView::ChildButton::FormatLockoutTime(
    int remaining_seconds) {
  if (remaining_seconds <= 0) {
    return u"";
  }

  int minutes = remaining_seconds / 60;
  int seconds = remaining_seconds % 60;

  if (minutes > 0) {
    return base::UTF8ToUTF16(std::to_string(minutes) + "m " +
                             std::to_string(seconds) + "s");
  }
  return base::UTF8ToUTF16(std::to_string(seconds) + "s");
}

bool Allow2ChildSelectView::ChildButton::OnMousePressed(
    const ui::MouseEvent& event) {
  // Ignore clicks when locked out.
  if (locked_out_) {
    return true;  // Consume the event but don't invoke callback.
  }
  if (on_click_) {
    on_click_.Run(child_id_);
  }
  return true;
}

BEGIN_METADATA(Allow2ChildSelectView, ChildButton)
END_METADATA

// Allow2ChildSelectView implementation.

// static
void Allow2ChildSelectView::Show(Browser* browser,
                                 const std::vector<Child>& children,
                                 const std::string& owner_name,
                                 base::WeakPtr<Allow2Service> service,
                                 ChildSelectedCallback child_selected_callback,
                                 GuestCallback guest_callback) {
  if (!browser || children.empty()) {
    return;
  }

  auto* view =
      new Allow2ChildSelectView(browser, children, owner_name,
                                std::move(service),
                                std::move(child_selected_callback),
                                std::move(guest_callback));

  constrained_window::CreateBrowserModalDialogViews(
      view, browser->window()->GetNativeWindow())
      ->Show();

  g_is_showing = true;
}

// static
bool Allow2ChildSelectView::IsShowing() {
  return g_is_showing;
}

Allow2ChildSelectView::Allow2ChildSelectView(
    Browser* browser,
    const std::vector<Child>& children,
    const std::string& owner_name,
    base::WeakPtr<Allow2Service> service,
    ChildSelectedCallback child_selected_callback,
    GuestCallback guest_callback)
    : browser_(browser),
      children_(children),
      owner_name_(owner_name),
      service_(std::move(service)),
      child_selected_callback_(std::move(child_selected_callback)),
      guest_callback_(std::move(guest_callback)) {
  // Configure dialog.
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetBackground(views::CreateSolidBackground(kBackgroundColor));

  // Main layout.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kDialogPadding, kDialogPadding), kElementSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Allow2 logo.
  // TODO: Replace placeholder with actual Allow2 vector icon from iOS assets.
  // See: /mnt/ai/allow2/iOS/Allow2/Backgrounds.xcassets/whitelogo.imageset/logo.pdf
  logo_view_ = AddChildView(std::make_unique<Allow2LogoView>());

  // Title: "Who's using Brave?"
  title_label_ =
      AddChildView(std::make_unique<views::Label>(u"Who's using Brave?"));
  title_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kTitleFontSize - gfx::FontList().GetFontSize()));
  title_label_->SetFontList(
      title_label_->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  title_label_->SetEnabledColor(kTitleColor);

  // Children buttons container.
  children_container_ = AddChildView(std::make_unique<views::View>());
  auto* children_layout =
      children_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kChildButtonSpacing));
  children_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  // Add child buttons.
  for (const auto& child : children_) {
    auto* button = children_container_->AddChildView(
        std::make_unique<ChildButton>(
            child,
            base::BindRepeating(&Allow2ChildSelectView::OnChildSelected,
                                base::Unretained(this))));
    child_buttons_.push_back(button);
  }

  // Add account owner option (shows owner's name instead of "Guest").
  Child owner_child;
  owner_child.id = 0;
  owner_child.name = owner_name_.empty() ? "Parent" : owner_name_;
  owner_child.color = "#607D8B";  // Blue Grey for owner/parent
  auto* owner_button = children_container_->AddChildView(
      std::make_unique<ChildButton>(
          owner_child,
          base::BindRepeating(&Allow2ChildSelectView::OnChildSelected,
                              base::Unretained(this))));
  child_buttons_.push_back(owner_button);

  // PIN entry container (initially hidden).
  pin_container_ = AddChildView(std::make_unique<views::View>());
  auto* pin_layout =
      pin_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  pin_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  pin_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // "Enter PIN:" label.
  pin_label_ =
      pin_container_->AddChildView(std::make_unique<views::Label>(u"Enter PIN:"));
  pin_label_->SetEnabledColor(kTextColor);

  // Container for the 4 individual digit fields.
  pin_digits_container_ =
      pin_container_->AddChildView(std::make_unique<views::View>());
  auto* digits_layout =
      pin_digits_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kPinDigitSpacing));
  digits_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  digits_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Create 4 individual PIN digit fields.
  for (size_t i = 0; i < kPinDigitCount; ++i) {
    auto* digit_field = CreatePinDigitField();
    digit_field->set_controller(this);
    pin_digit_fields_[i] = digit_field;
  }

  // PIN error label (initially hidden).
  pin_error_label_ =
      pin_container_->AddChildView(std::make_unique<views::Label>(u""));
  pin_error_label_->SetEnabledColor(kErrorColor);
  pin_error_label_->SetVisible(false);

  // Hide PIN container initially.
  pin_container_->SetVisible(false);

  // Confirm button (initially hidden).
  confirm_button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&Allow2ChildSelectView::OnConfirmClicked,
                          base::Unretained(this)),
      u"Confirm"));
  confirm_button_->SetStyle(ui::ButtonStyle::kProminent);
  confirm_button_->SetEnabled(false);
  confirm_button_->SetVisible(false);

  // Helpful text.
  auto* help_label = AddChildView(std::make_unique<views::Label>(
      u"Choose an account to start browsing."));
  help_label->SetEnabledColor(kTextColor);
  help_label->SetFontList(gfx::FontList().DeriveWithSizeDelta(-2));

  // Set preferred size (increased for logo and lockout labels).
  SetPreferredSize(gfx::Size(kDialogWidth, 440));

  // Initialize lockout states and start timer if needed.
  UpdateLockoutStates();
  StartLockoutTimer();
}

Allow2ChildSelectView::~Allow2ChildSelectView() {
  StopLockoutTimer();
  g_is_showing = false;
}

ui::mojom::ModalType Allow2ChildSelectView::GetModalType() const {
  return ui::mojom::ModalType::kWindow;
}

bool Allow2ChildSelectView::ShouldShowCloseButton() const {
  // No close button - must select a child.
  return false;
}

bool Allow2ChildSelectView::ShouldShowWindowTitle() const {
  return false;
}

void Allow2ChildSelectView::ContentsChanged(views::Textfield* sender,
                                            const std::u16string& new_contents) {
  if (is_submitting_) {
    return;
  }

  ClearPinError();

  int field_index = GetPinFieldIndex(sender);
  if (field_index < 0) {
    return;
  }

  // Filter to only allow digits - strip any non-digit characters.
  std::u16string digits_only;
  for (char16_t c : new_contents) {
    if (c >= '0' && c <= '9') {
      digits_only += c;
    }
  }

  // If content had non-digits removed or more than one digit, fix it.
  if (digits_only != new_contents || digits_only.length() > 1) {
    std::u16string single_digit = digits_only.empty() ? u"" : digits_only.substr(0, 1);
    sender->SetText(single_digit);
    // SetText triggers ContentsChanged again, so return to avoid double-processing.
    return;
  }

  // Handle paste: if multiple digits were entered, distribute them.
  if (digits_only.length() > 1) {
    HandlePastedPin(digits_only);
    return;
  }

  // Single digit entered - move to next field.
  if (digits_only.length() == 1) {
    FocusNextPinField(static_cast<size_t>(field_index));
  }

  // Update confirm button state.
  UpdateConfirmButtonState();

  // Auto-submit when all 4 digits are entered.
  if (IsPinComplete() && !is_submitting_) {
    // Use a small delay to allow UI to update before submission.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Allow2ChildSelectView::OnConfirmClicked,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

bool Allow2ChildSelectView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  int field_index = GetPinFieldIndex(sender);
  if (field_index < 0) {
    return false;
  }

  // Handle Enter key - submit if PIN is complete.
  if (key_event.key_code() == ui::VKEY_RETURN) {
    if (IsPinComplete()) {
      OnConfirmClicked();
      return true;
    }
    return false;
  }

  // Handle Backspace/Delete - move to previous field if current is empty.
  if (key_event.key_code() == ui::VKEY_BACK ||
      key_event.key_code() == ui::VKEY_DELETE) {
    if (sender->GetText().empty() && field_index > 0) {
      // Move to previous field and clear it.
      FocusPreviousPinField(static_cast<size_t>(field_index));
      if (pin_digit_fields_[static_cast<size_t>(field_index - 1)]) {
        pin_digit_fields_[static_cast<size_t>(field_index - 1)]->SetText(u"");
      }
      return true;
    }
    return false;
  }

  // Handle Left arrow - move to previous field.
  if (key_event.key_code() == ui::VKEY_LEFT && field_index > 0) {
    FocusPreviousPinField(static_cast<size_t>(field_index));
    return true;
  }

  // Handle Right arrow - move to next field.
  if (key_event.key_code() == ui::VKEY_RIGHT &&
      static_cast<size_t>(field_index) < kPinDigitCount - 1) {
    FocusNextPinField(static_cast<size_t>(field_index));
    return true;
  }

  return false;
}

void Allow2ChildSelectView::OnChildSelected(uint64_t child_id) {
  // Check if the user is locked out.
  if (service_) {
    int remaining_seconds = 0;
    if (service_->IsUserLockedOut(child_id, &remaining_seconds)) {
      // User is locked out - don't allow selection.
      // The button should already be disabled, but just in case.
      return;
    }
  }

  // Update selection state.
  // Both children (id > 0) and parent/owner (id = 0) require PIN entry.
  // No one gets immediate access without authentication.
  selected_child_id_ = child_id;

  // Update button visuals.
  for (const auto& button : child_buttons_) {
    button->SetSelected(button->child_id() == child_id);
  }

  // Clear any previous error.
  ClearPinError();

  // Show PIN entry - required for everyone.
  UpdatePinVisibility();
}

void Allow2ChildSelectView::OnConfirmClicked() {
  if (!selected_child_id_.has_value()) {
    return;
  }

  // Prevent re-entrancy during submission.
  if (is_submitting_) {
    return;
  }

  std::u16string pin = GetCombinedPin();
  if (!IsPinComplete()) {
    ShowPinError("Please enter your 4-digit PIN");
    return;
  }

  is_submitting_ = true;

  // Invoke callback with selected child and PIN.
  // The callback returns true if authentication succeeded.
  // Now using RepeatingCallback so it persists across retries.
  if (child_selected_callback_) {
    std::string debug_info;
    bool success = child_selected_callback_.Run(
        selected_child_id_.value(), base::UTF16ToUTF8(pin), &debug_info);

    if (success) {
      GetWidget()->Close();
      return;
    }

    // Check if this was a lockout response.
    bool is_lockout = debug_info.find("LOCKED OUT:") != std::string::npos ||
                      debug_info.find("Try again in") != std::string::npos;

    is_submitting_ = false;

    if (is_lockout) {
      // Lockout triggered - return to account selection.
      // Start the timer if not already running.
      StartLockoutTimer();
      ReturnToAccountSelection();
      return;
    }

    // Regular authentication failure - show error and allow retry.
    std::string error_msg = "Invalid PIN";
    if (!debug_info.empty()) {
      error_msg += "\n" + debug_info;
    }
    ShowPinError(error_msg);
    ClearPinFields();
    confirm_button_->SetEnabled(false);
    return;
  }

  // No callback - just close (shouldn't happen in normal use).
  GetWidget()->Close();
}

void Allow2ChildSelectView::OnGuestClicked() {
  if (guest_callback_) {
    std::move(guest_callback_).Run();
  }
  GetWidget()->Close();
}

void Allow2ChildSelectView::UpdatePinVisibility() {
  bool show_pin = selected_child_id_.has_value();
  pin_container_->SetVisible(show_pin);
  confirm_button_->SetVisible(show_pin);

  if (show_pin) {
    ClearPinFields();
    confirm_button_->SetEnabled(false);
  }

  InvalidateLayout();
}

void Allow2ChildSelectView::ShowPinError(const std::string& message) {
  if (pin_error_label_) {
    pin_error_label_->SetText(base::UTF8ToUTF16(message));
    pin_error_label_->SetMultiLine(true);
    pin_error_label_->SetMaximumWidth(kDialogWidth - 2 * kDialogPadding);
    pin_error_label_->SetVisible(true);
    // Re-layout to accommodate potentially larger error text.
    InvalidateLayout();
  }
}

void Allow2ChildSelectView::ClearPinError() {
  if (pin_error_label_) {
    pin_error_label_->SetVisible(false);
  }
}

std::u16string Allow2ChildSelectView::GetCombinedPin() const {
  std::u16string combined;
  for (size_t i = 0; i < kPinDigitCount; ++i) {
    if (pin_digit_fields_[i]) {
      combined += pin_digit_fields_[i]->GetText();
    }
  }
  return combined;
}

bool Allow2ChildSelectView::IsPinComplete() const {
  for (size_t i = 0; i < kPinDigitCount; ++i) {
    if (!pin_digit_fields_[i] || pin_digit_fields_[i]->GetText().empty()) {
      return false;
    }
  }
  return true;
}

void Allow2ChildSelectView::ClearPinFields() {
  for (size_t i = 0; i < kPinDigitCount; ++i) {
    if (pin_digit_fields_[i]) {
      pin_digit_fields_[i]->SetText(u"");
    }
  }
  // Focus the first field.
  if (pin_digit_fields_[0]) {
    pin_digit_fields_[0]->RequestFocus();
  }
}

void Allow2ChildSelectView::FocusNextPinField(size_t current_index) {
  size_t next_index = current_index + 1;
  if (next_index < kPinDigitCount && pin_digit_fields_[next_index]) {
    pin_digit_fields_[next_index]->RequestFocus();
  }
}

void Allow2ChildSelectView::FocusPreviousPinField(size_t current_index) {
  if (current_index > 0 && pin_digit_fields_[current_index - 1]) {
    pin_digit_fields_[current_index - 1]->RequestFocus();
  }
}

void Allow2ChildSelectView::HandlePastedPin(const std::u16string& pasted_text) {
  // Extract digits from pasted text.
  std::u16string digits_only;
  for (char16_t c : pasted_text) {
    if (c >= '0' && c <= '9') {
      digits_only += c;
    }
  }

  // Distribute digits across fields.
  for (size_t i = 0; i < kPinDigitCount && i < digits_only.length(); ++i) {
    if (pin_digit_fields_[i]) {
      pin_digit_fields_[i]->SetText(std::u16string(1, digits_only[i]));
    }
  }

  // Focus the next empty field, or the last field if all are filled.
  size_t focus_index = std::min(digits_only.length(), kPinDigitCount - 1);
  if (focus_index < kPinDigitCount && pin_digit_fields_[focus_index]) {
    pin_digit_fields_[focus_index]->RequestFocus();
  }

  UpdateConfirmButtonState();

  // Auto-submit if all 4 digits were pasted.
  if (IsPinComplete() && !is_submitting_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Allow2ChildSelectView::OnConfirmClicked,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

int Allow2ChildSelectView::GetPinFieldIndex(views::Textfield* field) const {
  for (size_t i = 0; i < kPinDigitCount; ++i) {
    if (pin_digit_fields_[i] == field) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void Allow2ChildSelectView::UpdateConfirmButtonState() {
  if (confirm_button_) {
    confirm_button_->SetEnabled(IsPinComplete());
  }
}

views::Textfield* Allow2ChildSelectView::CreatePinDigitField() {
  auto field = std::make_unique<views::Textfield>();
  field->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  field->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  field->SetPreferredSize(gfx::Size(kPinDigitFieldWidth, kPinDigitFieldHeight));

  // Style the field with larger font for single digit display.
  field->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kPinDigitFontSize - gfx::FontList().GetFontSize()));

  return pin_digits_container_->AddChildView(std::move(field));
}

void Allow2ChildSelectView::ReturnToAccountSelection() {
  // Clear selection and hide PIN entry.
  selected_child_id_.reset();

  // Clear all button selections.
  for (const auto& button : child_buttons_) {
    button->SetSelected(false);
  }

  // Hide PIN container and confirm button.
  pin_container_->SetVisible(false);
  confirm_button_->SetVisible(false);

  // Clear any error messages.
  ClearPinError();
  ClearPinFields();

  // Update lockout states.
  UpdateLockoutStates();

  InvalidateLayout();
}

void Allow2ChildSelectView::UpdateLockoutStates() {
  if (!service_) {
    return;
  }

  // Get all locked out users.
  std::map<uint64_t, int> locked_users = service_->GetLockedOutUsers();

  // Update each button.
  for (const auto& button : child_buttons_) {
    auto it = locked_users.find(button->child_id());
    if (it != locked_users.end() && it->second > 0) {
      button->SetLockedOut(true, it->second);
    } else {
      button->SetLockedOut(false, 0);
    }
  }
}

void Allow2ChildSelectView::OnLockoutTimerTick() {
  UpdateLockoutStates();

  // Check if any users are still locked out.
  if (service_) {
    std::map<uint64_t, int> locked_users = service_->GetLockedOutUsers();
    if (locked_users.empty()) {
      // No more locked users - stop the timer.
      StopLockoutTimer();
    }
  }
}

void Allow2ChildSelectView::StartLockoutTimer() {
  if (!lockout_timer_.IsRunning()) {
    lockout_timer_.Start(FROM_HERE, kLockoutTimerInterval,
                          base::BindRepeating(
                              &Allow2ChildSelectView::OnLockoutTimerTick,
                              weak_ptr_factory_.GetWeakPtr()));
  }
}

void Allow2ChildSelectView::StopLockoutTimer() {
  if (lockout_timer_.IsRunning()) {
    lockout_timer_.Stop();
  }
}

BEGIN_METADATA(Allow2ChildSelectView)
END_METADATA

}  // namespace allow2

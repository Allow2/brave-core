/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/allow2/allow2_child_select_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
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
constexpr int kChildButtonHeight = 80;
constexpr int kPinFieldWidth = 150;
constexpr int kTitleFontSize = 22;
constexpr int kChildNameFontSize = 14;

// Colors.
constexpr SkColor kBackgroundColor = SkColorSetRGB(0xFA, 0xFA, 0xFA);
constexpr SkColor kTitleColor = SkColorSetRGB(0x33, 0x33, 0x33);
constexpr SkColor kTextColor = SkColorSetRGB(0x66, 0x66, 0x66);
constexpr SkColor kChildButtonColor = SkColorSetRGB(0xE8, 0xE8, 0xE8);
constexpr SkColor kChildButtonSelectedColor = gfx::kGoogleBlue100;
constexpr SkColor kChildButtonBorderColor = gfx::kGoogleBlue600;
constexpr SkColor kErrorColor = SkColorSetRGB(0xE0, 0x4B, 0x4B);

// Singleton tracking for IsShowing().
static bool g_is_showing = false;

}  // namespace

// ChildButton implementation.

Allow2ChildSelectView::ChildButton::ChildButton(
    const Child& child,
    base::RepeatingCallback<void(uint64_t)> on_click)
    : child_id_(child.id), on_click_(std::move(on_click)) {
  SetPreferredSize(gfx::Size(kChildButtonWidth, kChildButtonHeight));
  SetBackground(views::CreateRoundedRectBackground(kChildButtonColor, 8));
  SetBorder(views::CreateRoundedRectBorder(2, 8, kChildButtonColor));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kChildButtonPadding, kChildButtonPadding / 2), 8));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Avatar placeholder (emoji for now).
  auto* avatar_label = AddChildView(std::make_unique<views::Label>(u"\U0001F464"));
  avatar_label->SetFontList(gfx::FontList().DeriveWithSizeDelta(20));

  // Child name.
  name_label_ = AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(child.name)));
  name_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kChildNameFontSize - gfx::FontList().GetFontSize()));
  name_label_->SetEnabledColor(kTextColor);
  name_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  name_label_->SetMaximumWidth(kChildButtonWidth - kChildButtonPadding);
  name_label_->SetElideBehavior(gfx::ELIDE_TAIL);

  // Make clickable.
  SetFocusBehavior(FocusBehavior::ALWAYS);
}

Allow2ChildSelectView::ChildButton::~ChildButton() = default;

void Allow2ChildSelectView::ChildButton::SetSelected(bool selected) {
  selected_ = selected;
  if (selected) {
    SetBackground(views::CreateRoundedRectBackground(kChildButtonSelectedColor, 8));
    SetBorder(views::CreateRoundedRectBorder(2, 8, kChildButtonBorderColor));
  } else {
    SetBackground(views::CreateRoundedRectBackground(kChildButtonColor, 8));
    SetBorder(views::CreateRoundedRectBorder(2, 8, kChildButtonColor));
  }
  SchedulePaint();
}

bool Allow2ChildSelectView::ChildButton::OnMousePressed(
    const ui::MouseEvent& event) {
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
                                 ChildSelectedCallback child_selected_callback,
                                 GuestCallback guest_callback) {
  if (!browser || children.empty()) {
    return;
  }

  auto* view =
      new Allow2ChildSelectView(browser, children,
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
    ChildSelectedCallback child_selected_callback,
    GuestCallback guest_callback)
    : browser_(browser),
      children_(children),
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

  // Add Guest option.
  Child guest_child;
  guest_child.id = 0;
  guest_child.name = "Guest";
  auto* guest_button = children_container_->AddChildView(
      std::make_unique<ChildButton>(
          guest_child,
          base::BindRepeating(&Allow2ChildSelectView::OnChildSelected,
                              base::Unretained(this))));
  child_buttons_.push_back(guest_button);

  // PIN entry container (initially hidden).
  pin_container_ = AddChildView(std::make_unique<views::View>());
  auto* pin_layout =
      pin_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  pin_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  pin_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // "Enter PIN" label.
  pin_label_ =
      pin_container_->AddChildView(std::make_unique<views::Label>(u"Enter PIN:"));
  pin_label_->SetEnabledColor(kTextColor);

  // PIN text field.
  pin_field_ =
      pin_container_->AddChildView(std::make_unique<views::Textfield>());
  pin_field_->SetPreferredSize(gfx::Size(kPinFieldWidth, 36));
  pin_field_->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
  pin_field_->SetPlaceholderText(u"\u2022 \u2022 \u2022 \u2022");
  pin_field_->set_controller(this);
  pin_field_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

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
      u"This helps track time limits and keep you safe"));
  help_label->SetEnabledColor(kTextColor);
  help_label->SetFontList(gfx::FontList().DeriveWithSizeDelta(-2));

  // Set preferred size.
  SetPreferredSize(gfx::Size(kDialogWidth, 350));
}

Allow2ChildSelectView::~Allow2ChildSelectView() {
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
  ClearPinError();
  // Enable confirm button if PIN is entered.
  confirm_button_->SetEnabled(!new_contents.empty());
}

bool Allow2ChildSelectView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& key_event) {
  if (key_event.type() == ui::EventType::kKeyPressed &&
      key_event.key_code() == ui::VKEY_RETURN) {
    OnConfirmClicked();
    return true;
  }
  return false;
}

void Allow2ChildSelectView::OnChildSelected(uint64_t child_id) {
  // If selecting Guest (id=0), handle differently.
  if (child_id == 0) {
    OnGuestClicked();
    return;
  }

  // Update selection state.
  selected_child_id_ = child_id;

  // Update button visuals.
  for (const auto& button : child_buttons_) {
    button->SetSelected(button->child_id() == child_id);
  }

  // Show PIN entry.
  UpdatePinVisibility();
}

void Allow2ChildSelectView::OnConfirmClicked() {
  if (!selected_child_id_.has_value()) {
    return;
  }

  std::u16string pin(pin_field_->GetText());
  if (pin.empty()) {
    ShowPinError("Please enter your PIN");
    return;
  }

  // Invoke callback with selected child and PIN.
  if (child_selected_callback_) {
    std::move(child_selected_callback_)
        .Run(selected_child_id_.value(), base::UTF16ToUTF8(pin));
  }

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
    pin_field_->SetText(u"");
    pin_field_->RequestFocus();
    confirm_button_->SetEnabled(false);
  }

  InvalidateLayout();
}

void Allow2ChildSelectView::ShowPinError(const std::string& message) {
  if (pin_error_label_) {
    pin_error_label_->SetText(base::UTF8ToUTF16(message));
    pin_error_label_->SetVisible(true);
  }
}

void Allow2ChildSelectView::ClearPinError() {
  if (pin_error_label_) {
    pin_error_label_->SetVisible(false);
  }
}

BEGIN_METADATA(Allow2ChildSelectView)
END_METADATA

}  // namespace allow2

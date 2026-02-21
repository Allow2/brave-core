/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/allow2/allow2_grant_entry_view.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/number_formatting.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
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
#include "ui/views/widget/widget.h"

namespace allow2 {

namespace {

// Layout constants.
constexpr int kDialogWidth = 450;
constexpr int kDialogPadding = 32;
constexpr int kElementSpacing = 16;
constexpr int kSmallSpacing = 8;
constexpr int kTokenFieldWidth = 380;
constexpr int kTitleFontSize = 20;
constexpr int kDetailFontSize = 14;

// Colors.
constexpr SkColor kBackgroundColor = SK_ColorWHITE;
constexpr SkColor kTitleColor = SkColorSetRGB(0x33, 0x33, 0x33);
constexpr SkColor kTextColor = SkColorSetRGB(0x66, 0x66, 0x66);
constexpr SkColor kSuccessColor = SkColorSetRGB(0x0D, 0x8C, 0x4F);
constexpr SkColor kSuccessBackgroundColor = SkColorSetRGB(0xE6, 0xF4, 0xEA);
constexpr SkColor kErrorColor = SkColorSetRGB(0xD9, 0x3B, 0x3B);
constexpr SkColor kDetailLabelColor = SkColorSetRGB(0x55, 0x55, 0x55);
constexpr SkColor kConfirmBackgroundColor = SkColorSetRGB(0xF0, 0xF4, 0xF8);

// Singleton tracking for IsShowing().
static Allow2GrantEntryView* g_instance = nullptr;

// Activity names (should match server-side activity IDs).
constexpr std::array<const char*, 8> kActivityNames = {{
    "General",    // 0
    "Internet",   // 1
    "Apps",       // 2
    "Gaming",     // 3
    "Social",     // 4
    "Messaging",  // 5
    "Video",      // 6
    "Audio",      // 7
}};
constexpr size_t kActivityNameCount = kActivityNames.size();

}  // namespace

// static
void Allow2GrantEntryView::Show(Browser* browser,
                                 uint64_t child_id,
                                 const std::string& device_id,
                                 const std::vector<uint8_t>& parent_public_key,
                                 GrantCallback callback) {
  if (!browser) {
    return;
  }

  // Dismiss any existing view.
  Dismiss();

  auto* view = new Allow2GrantEntryView(
      child_id, device_id, parent_public_key, std::move(callback));

  constrained_window::CreateBrowserModalDialogViews(
      view, browser->window()->GetNativeWindow())
      ->Show();

  g_instance = view;
}

// static
bool Allow2GrantEntryView::IsShowing() {
  return g_instance != nullptr;
}

// static
void Allow2GrantEntryView::Dismiss() {
  if (g_instance) {
    g_instance->GetWidget()->Close();
    g_instance = nullptr;
  }
}

Allow2GrantEntryView::Allow2GrantEntryView(
    uint64_t child_id,
    const std::string& device_id,
    const std::vector<uint8_t>& parent_public_key,
    GrantCallback callback)
    : child_id_(child_id),
      device_id_(device_id),
      parent_public_key_(parent_public_key),
      callback_(std::move(callback)) {
  InitLayout();
}

Allow2GrantEntryView::~Allow2GrantEntryView() {
  if (g_instance == this) {
    g_instance = nullptr;
  }
}

void Allow2GrantEntryView::InitLayout() {
  // Configure dialog buttons.
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  SetBackground(views::CreateSolidBackground(kBackgroundColor));

  // Main layout.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kDialogPadding, kDialogPadding), kElementSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kStart);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Create all view containers.
  CreateEntryView();
  CreateConfirmView();
  CreateSuccessView();

  // Start in entry state.
  SetState(State::kEntry);

  // Set preferred dialog size.
  SetPreferredSize(gfx::Size(kDialogWidth, 380));
}

void Allow2GrantEntryView::CreateEntryView() {
  entry_container_ = AddChildView(std::make_unique<views::View>());

  auto* layout =
      entry_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kElementSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kStart);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Title.
  auto* title = entry_container_->AddChildView(
      std::make_unique<views::Label>(u"Enter Grant Code"));
  title->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kTitleFontSize - gfx::FontList().GetFontSize()));
  title->SetFontList(title->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  title->SetEnabledColor(kTitleColor);

  // Instructions.
  instruction_label_ = entry_container_->AddChildView(
      std::make_unique<views::Label>(
          u"Enter the grant code from your parent's Allow2 app.\n"
          u"This is the text shown below the QR code."));
  instruction_label_->SetEnabledColor(kTextColor);
  instruction_label_->SetMultiLine(true);
  instruction_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  instruction_label_->SetMaximumWidth(kTokenFieldWidth);

  // Token input field.
  token_field_ = entry_container_->AddChildView(
      std::make_unique<views::Textfield>());
  token_field_->SetPreferredSize(gfx::Size(kTokenFieldWidth, 80));
  token_field_->SetPlaceholderText(u"Paste or type the grant code here...");
  token_field_->set_controller(this);
  token_field_->SetFontList(gfx::FontList().DeriveWithSizeDelta(2));

  // Error label (initially hidden).
  error_label_ = entry_container_->AddChildView(
      std::make_unique<views::Label>(u""));
  error_label_->SetEnabledColor(kErrorColor);
  error_label_->SetVisible(false);
  error_label_->SetMultiLine(true);
  error_label_->SetMaximumWidth(kTokenFieldWidth);

  // Validate button.
  validate_button_ = entry_container_->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&Allow2GrantEntryView::ValidateToken,
                              base::Unretained(this)),
          u"Validate"));
  validate_button_->SetStyle(ui::ButtonStyle::kProminent);
  validate_button_->SetEnabled(false);
}

void Allow2GrantEntryView::CreateConfirmView() {
  confirm_container_ = AddChildView(std::make_unique<views::View>());

  auto* layout =
      confirm_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kElementSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kStart);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Title.
  confirm_title_label_ = confirm_container_->AddChildView(
      std::make_unique<views::Label>(u"Confirm Grant"));
  confirm_title_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kTitleFontSize - gfx::FontList().GetFontSize()));
  confirm_title_label_->SetFontList(
      confirm_title_label_->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  confirm_title_label_->SetEnabledColor(kTitleColor);

  // Details container with background.
  auto* details_container =
      confirm_container_->AddChildView(std::make_unique<views::View>());
  details_container->SetBackground(
      views::CreateRoundedRectBackground(kConfirmBackgroundColor, 8));
  details_container->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(16, 24)));

  auto* details_layout =
      details_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kSmallSpacing));
  details_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);

  // Grant type.
  grant_type_label_ = details_container->AddChildView(
      std::make_unique<views::Label>(u"Type: Extension"));
  grant_type_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kDetailFontSize - gfx::FontList().GetFontSize()));
  grant_type_label_->SetEnabledColor(kDetailLabelColor);

  // Activity.
  activity_label_ = details_container->AddChildView(
      std::make_unique<views::Label>(u"Activity: Internet"));
  activity_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kDetailFontSize - gfx::FontList().GetFontSize()));
  activity_label_->SetEnabledColor(kDetailLabelColor);

  // Duration.
  duration_label_ = details_container->AddChildView(
      std::make_unique<views::Label>(u"Duration: 30 minutes"));
  duration_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kDetailFontSize - gfx::FontList().GetFontSize()));
  duration_label_->SetFontList(
      duration_label_->font_list().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  duration_label_->SetEnabledColor(kTitleColor);

  // Expires.
  expires_label_ = details_container->AddChildView(
      std::make_unique<views::Label>(u"Expires: Today at 11:59 PM"));
  expires_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kDetailFontSize - gfx::FontList().GetFontSize()));
  expires_label_->SetEnabledColor(kDetailLabelColor);

  // Button container.
  auto* button_container =
      confirm_container_->AddChildView(std::make_unique<views::View>());
  auto* button_layout =
      button_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kElementSpacing));
  button_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  // Back button.
  back_button_ = button_container->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&Allow2GrantEntryView::SetState,
                              base::Unretained(this), State::kEntry),
          u"Back"));
  back_button_->SetStyle(ui::ButtonStyle::kDefault);

  // Apply button.
  apply_button_ = button_container->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&Allow2GrantEntryView::ApplyGrant,
                              base::Unretained(this)),
          u"Apply Grant"));
  apply_button_->SetStyle(ui::ButtonStyle::kProminent);
}

void Allow2GrantEntryView::CreateSuccessView() {
  success_container_ = AddChildView(std::make_unique<views::View>());

  auto* layout =
      success_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kElementSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Success container with background.
  auto* success_box =
      success_container_->AddChildView(std::make_unique<views::View>());
  success_box->SetBackground(
      views::CreateRoundedRectBackground(kSuccessBackgroundColor, 8));
  success_box->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(24, 32)));

  auto* success_layout =
      success_box->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kSmallSpacing));
  success_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Success checkmark (using text for simplicity).
  auto* checkmark = success_box->AddChildView(
      std::make_unique<views::Label>(u"\u2714"));  // Unicode checkmark
  checkmark->SetFontList(gfx::FontList().DeriveWithSizeDelta(24));
  checkmark->SetEnabledColor(kSuccessColor);

  // Success label.
  success_label_ = success_box->AddChildView(
      std::make_unique<views::Label>(u"Grant Applied!"));
  success_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kTitleFontSize - gfx::FontList().GetFontSize()));
  success_label_->SetFontList(
      success_label_->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  success_label_->SetEnabledColor(kSuccessColor);

  // Success details.
  success_details_label_ = success_box->AddChildView(
      std::make_unique<views::Label>(u"30 minutes added to Internet"));
  success_details_label_->SetEnabledColor(kDetailLabelColor);
  success_details_label_->SetMultiLine(true);
  success_details_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
}

void Allow2GrantEntryView::SetState(State state) {
  state_ = state;

  // Update visibility.
  if (entry_container_) {
    entry_container_->SetVisible(state == State::kEntry ||
                                  state == State::kValidating ||
                                  state == State::kError);
  }
  if (confirm_container_) {
    confirm_container_->SetVisible(state == State::kConfirm);
  }
  if (success_container_) {
    success_container_->SetVisible(state == State::kSuccess);
  }

  UpdateButtonStates();
  InvalidateLayout();
}

void Allow2GrantEntryView::UpdateButtonStates() {
  switch (state_) {
    case State::kEntry:
    case State::kError:
      if (validate_button_) {
        std::u16string text = token_field_
            ? std::u16string(token_field_->GetText())
            : std::u16string();
        // Enable if there's some content (minimum token length ~50 chars).
        validate_button_->SetEnabled(text.length() >= 20);
      }
      break;

    case State::kValidating:
      if (validate_button_) {
        validate_button_->SetEnabled(false);
      }
      break;

    case State::kConfirm:
      if (apply_button_) {
        apply_button_->SetEnabled(true);
      }
      break;

    case State::kSuccess:
      // No buttons to manage.
      break;
  }
}

void Allow2GrantEntryView::ValidateToken() {
  ClearError();
  SetState(State::kValidating);

  std::u16string token_u16(token_field_->GetText());
  std::string token = base::UTF16ToUTF8(token_u16);

  // Trim whitespace.
  while (!token.empty() && (token.front() == ' ' || token.front() == '\n' ||
                            token.front() == '\r' || token.front() == '\t')) {
    token.erase(0, 1);
  }
  while (!token.empty() && (token.back() == ' ' || token.back() == '\n' ||
                            token.back() == '\r' || token.back() == '\t')) {
    token.pop_back();
  }

  // Validate using the QR token parser.
  auto grant = Allow2QRToken::ParseAndVerify(token, parent_public_key_);

  if (!grant) {
    ShowError("Invalid grant code. Please check and try again.");
    SetState(State::kError);
    return;
  }

  // Check if expired.
  if (grant->IsExpired()) {
    ShowError("This grant has expired. Please request a new one.");
    SetState(State::kError);
    return;
  }

  // Check device ID if specified.
  if (!grant->IsValidForDevice(device_id_)) {
    ShowError("This grant is for a different device.");
    SetState(State::kError);
    return;
  }

  // Check child ID.
  if (!grant->IsValidForChild(child_id_)) {
    ShowError("This grant is for a different user.");
    SetState(State::kError);
    return;
  }

  // Store the parsed grant and show confirmation.
  parsed_grant_ = grant;

  // Update confirmation UI.
  if (grant_type_label_) {
    grant_type_label_->SetText(u"Type: " + FormatGrantType(grant->type));
  }
  if (activity_label_) {
    activity_label_->SetText(u"Activity: " + GetActivityName(grant->activity_id));
  }
  if (duration_label_) {
    duration_label_->SetText(u"Duration: " + FormatDuration(grant->minutes));
  }
  if (expires_label_) {
    // Format expiry time.
    base::Time::Exploded exploded;
    grant->expires_at.LocalExplode(&exploded);
    std::u16string expires_text = u"Expires: Today at ";
    int hour = exploded.hour;
    bool is_pm = hour >= 12;
    if (hour > 12) hour -= 12;
    if (hour == 0) hour = 12;
    expires_text += base::FormatNumber(hour);
    expires_text += u":";
    if (exploded.minute < 10) expires_text += u"0";
    expires_text += base::FormatNumber(exploded.minute);
    expires_text += is_pm ? u" PM" : u" AM";
    expires_label_->SetText(expires_text);
  }

  SetState(State::kConfirm);
}

void Allow2GrantEntryView::ShowError(const std::string& message) {
  if (error_label_) {
    error_label_->SetText(base::UTF8ToUTF16(message));
    error_label_->SetVisible(true);
  }
  InvalidateLayout();
}

void Allow2GrantEntryView::ClearError() {
  if (error_label_) {
    error_label_->SetVisible(false);
  }
}

void Allow2GrantEntryView::ApplyGrant() {
  if (!parsed_grant_) {
    return;
  }

  // Update success message.
  if (success_details_label_) {
    std::u16string details = FormatDuration(parsed_grant_->minutes);
    details += u" added to ";
    details += GetActivityName(parsed_grant_->activity_id);
    success_details_label_->SetText(details);
  }

  SetState(State::kSuccess);

  // Invoke callback.
  if (callback_) {
    std::move(callback_).Run(true, *parsed_grant_);
  }

  // Close dialog after brief delay (let user see success).
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Allow2GrantEntryView::Dismiss),
      base::Seconds(2));
}

std::u16string Allow2GrantEntryView::FormatGrantType(QRGrantType type) const {
  switch (type) {
    case QRGrantType::kExtension:
      return u"Time Extension";
    case QRGrantType::kQuota:
      return u"Quota Increase";
    case QRGrantType::kEarlier:
      return u"Start Earlier";
    case QRGrantType::kLiftBan:
      return u"Lift Ban";
  }
  return u"Unknown";
}

std::u16string Allow2GrantEntryView::FormatDuration(uint16_t minutes) const {
  if (minutes >= 60) {
    int hours = minutes / 60;
    int remaining_mins = minutes % 60;
    if (remaining_mins == 0) {
      return base::FormatNumber(hours) +
             (hours == 1 ? u" hour" : u" hours");
    }
    return base::FormatNumber(hours) +
           (hours == 1 ? u" hour " : u" hours ") +
           base::FormatNumber(remaining_mins) + u" min";
  }
  return base::FormatNumber(minutes) +
         (minutes == 1 ? u" minute" : u" minutes");
}

std::u16string Allow2GrantEntryView::GetActivityName(uint8_t activity_id) const {
  if (activity_id < kActivityNameCount) {
    return base::UTF8ToUTF16(kActivityNames.at(activity_id));
  }
  return u"Activity " + base::FormatNumber(activity_id);
}

std::u16string Allow2GrantEntryView::GetWindowTitle() const {
  return u"Enter Grant Code";
}

views::View* Allow2GrantEntryView::GetInitiallyFocusedView() {
  return token_field_;
}

bool Allow2GrantEntryView::Accept() {
  // Handle accept based on state.
  if (state_ == State::kEntry || state_ == State::kError) {
    ValidateToken();
    return false;  // Don't close yet.
  }
  if (state_ == State::kConfirm) {
    ApplyGrant();
    return false;  // Don't close yet - will close after success.
  }
  return true;  // Close on success state.
}

bool Allow2GrantEntryView::Cancel() {
  // If we haven't applied a grant, invoke callback with failure.
  if (callback_ && state_ != State::kSuccess) {
    QRGrant empty_grant;
    std::move(callback_).Run(false, empty_grant);
  }
  return true;  // Allow close.
}

ui::mojom::ModalType Allow2GrantEntryView::GetModalType() const {
  return ui::mojom::ModalType::kWindow;
}

bool Allow2GrantEntryView::ShouldShowCloseButton() const {
  return false;
}

bool Allow2GrantEntryView::ShouldShowWindowTitle() const {
  return false;
}

void Allow2GrantEntryView::ContentsChanged(views::Textfield* sender,
                                            const std::u16string& new_contents) {
  ClearError();
  UpdateButtonStates();
}

bool Allow2GrantEntryView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& key_event) {
  if (key_event.type() == ui::EventType::kKeyPressed &&
      key_event.key_code() == ui::VKEY_RETURN) {
    if (validate_button_ && validate_button_->GetEnabled()) {
      ValidateToken();
    }
    return true;
  }
  return false;
}

BEGIN_METADATA(Allow2GrantEntryView)
END_METADATA

}  // namespace allow2

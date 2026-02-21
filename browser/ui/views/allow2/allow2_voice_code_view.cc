/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/allow2/allow2_voice_code_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/number_formatting.h"
#include "base/strings/string_number_conversions.h"
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
#include "ui/views/widget/widget.h"

namespace allow2 {

namespace {

// Layout constants.
constexpr int kDialogWidth = 420;
constexpr int kDialogPadding = 32;
constexpr int kElementSpacing = 16;
constexpr int kSmallSpacing = 8;
constexpr int kApprovalCodeFieldWidth = 180;
constexpr int kTitleFontSize = 20;
constexpr int kRequestCodeFontSize = 32;
constexpr int kActivityFontSize = 14;

// Colors.
constexpr SkColor kBackgroundColor = SK_ColorWHITE;
constexpr SkColor kTitleColor = SkColorSetRGB(0x33, 0x33, 0x33);
constexpr SkColor kTextColor = SkColorSetRGB(0x66, 0x66, 0x66);
constexpr SkColor kRequestCodeColor = gfx::kGoogleBlue700;
constexpr SkColor kRequestCodeBackgroundColor = SkColorSetRGB(0xE8, 0xF0, 0xFE);
constexpr SkColor kErrorColor = SkColorSetRGB(0xD9, 0x3B, 0x3B);
constexpr SkColor kActivityColor = SkColorSetRGB(0x55, 0x55, 0x55);

// Singleton tracking for IsShowing().
static bool g_is_showing = false;

}  // namespace

// static
void Allow2VoiceCodeView::Show(Browser* browser,
                                const std::string& request_code,
                                uint8_t activity_id,
                                const std::string& activity_name,
                                uint16_t minutes_requested,
                                ApprovalCallback callback) {
  if (!browser) {
    return;
  }

  auto* view = new Allow2VoiceCodeView(
      request_code, activity_id, activity_name, minutes_requested,
      std::move(callback));

  constrained_window::CreateBrowserModalDialogViews(
      view, browser->window()->GetNativeWindow())
      ->Show();

  g_is_showing = true;
}

// static
bool Allow2VoiceCodeView::IsShowing() {
  return g_is_showing;
}

Allow2VoiceCodeView::Allow2VoiceCodeView(const std::string& request_code,
                                          uint8_t activity_id,
                                          const std::string& activity_name,
                                          uint16_t minutes_requested,
                                          ApprovalCallback callback)
    : request_code_(request_code),
      activity_id_(activity_id),
      activity_name_(activity_name),
      minutes_requested_(minutes_requested),
      callback_(std::move(callback)) {
  InitLayout();
}

Allow2VoiceCodeView::~Allow2VoiceCodeView() {
  lockout_timer_.Stop();
  g_is_showing = false;
}

void Allow2VoiceCodeView::InitLayout() {
  // Configure dialog buttons.
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  SetBackground(views::CreateSolidBackground(kBackgroundColor));

  // Main layout.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kDialogPadding, kDialogPadding), kElementSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Title: "Request More Time"
  title_label_ =
      AddChildView(std::make_unique<views::Label>(u"Request More Time"));
  title_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kTitleFontSize - gfx::FontList().GetFontSize()));
  title_label_->SetFontList(
      title_label_->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  title_label_->SetEnabledColor(kTitleColor);

  // Instruction: "Tell your parent this code:"
  instruction_label_ = AddChildView(
      std::make_unique<views::Label>(u"Tell your parent this code:"));
  instruction_label_->SetEnabledColor(kTextColor);

  // Request code display container (with background).
  auto* code_container = AddChildView(std::make_unique<views::View>());
  code_container->SetBackground(
      views::CreateRoundedRectBackground(kRequestCodeBackgroundColor, 8));
  code_container->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(16, 24)));

  auto* code_layout =
      code_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  code_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  // Request code label (large, prominent).
  request_code_label_ = code_container->AddChildView(
      std::make_unique<views::Label>(FormatCodeForDisplay(request_code_)));
  request_code_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kRequestCodeFontSize - gfx::FontList().GetFontSize()));
  request_code_label_->SetFontList(
      request_code_label_->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  request_code_label_->SetEnabledColor(kRequestCodeColor);

  // Activity and minutes requested display.
  std::u16string activity_text = base::UTF8ToUTF16(activity_name_);
  activity_text += u" \u2022 ";  // Bullet separator.
  activity_text += base::FormatNumber(minutes_requested_);
  activity_text += u" minutes requested";

  activity_label_ =
      AddChildView(std::make_unique<views::Label>(activity_text));
  activity_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kActivityFontSize - gfx::FontList().GetFontSize()));
  activity_label_->SetEnabledColor(kActivityColor);

  // Spacer.
  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetPreferredSize(gfx::Size(1, kSmallSpacing));

  // Approval code entry instruction.
  approval_instruction_label_ = AddChildView(
      std::make_unique<views::Label>(u"Enter approval code from parent:"));
  approval_instruction_label_->SetEnabledColor(kTextColor);

  // Approval code text field.
  approval_code_field_ =
      AddChildView(std::make_unique<views::Textfield>());
  approval_code_field_->SetPreferredSize(
      gfx::Size(kApprovalCodeFieldWidth, 40));
  approval_code_field_->SetTextInputType(ui::TEXT_INPUT_TYPE_NUMBER);
  approval_code_field_->SetPlaceholderText(u"_ _ _ _ _ _");
  approval_code_field_->set_controller(this);
  approval_code_field_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  approval_code_field_->SetFontList(gfx::FontList().DeriveWithSizeDelta(4));

  // Error label (initially hidden).
  error_label_ = AddChildView(std::make_unique<views::Label>(u""));
  error_label_->SetEnabledColor(kErrorColor);
  error_label_->SetVisible(false);
  error_label_->SetMultiLine(true);
  error_label_->SetMaximumWidth(kDialogWidth - 2 * kDialogPadding);

  // Submit button.
  submit_button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&Allow2VoiceCodeView::SubmitApprovalCode,
                          base::Unretained(this)),
      u"Submit"));
  submit_button_->SetStyle(ui::ButtonStyle::kProminent);
  submit_button_->SetEnabled(false);

  // Set preferred dialog size.
  SetPreferredSize(gfx::Size(kDialogWidth, 400));
}

std::u16string Allow2VoiceCodeView::GetWindowTitle() const {
  return u"Request More Time";
}

views::View* Allow2VoiceCodeView::GetInitiallyFocusedView() {
  return approval_code_field_;
}

bool Allow2VoiceCodeView::Accept() {
  // This is called when the OK button is pressed.
  // We handle submission via our own button, so this shouldn't be called.
  SubmitApprovalCode();
  return false;  // Don't close yet - wait for validation.
}

ui::mojom::ModalType Allow2VoiceCodeView::GetModalType() const {
  return ui::mojom::ModalType::kWindow;
}

bool Allow2VoiceCodeView::ShouldShowCloseButton() const {
  return false;
}

bool Allow2VoiceCodeView::ShouldShowWindowTitle() const {
  return false;
}

void Allow2VoiceCodeView::ContentsChanged(views::Textfield* sender,
                                           const std::u16string& new_contents) {
  ClearError();
  UpdateOKButtonState();

  // Auto-submit when 6 digits are entered.
  if (new_contents.length() == 6 && IsValidApprovalCodeFormat(new_contents)) {
    SubmitApprovalCode();
  }
}

bool Allow2VoiceCodeView::HandleKeyEvent(views::Textfield* sender,
                                          const ui::KeyEvent& key_event) {
  if (key_event.type() == ui::EventType::kKeyPressed &&
      key_event.key_code() == ui::VKEY_RETURN) {
    if (submit_button_->GetEnabled()) {
      SubmitApprovalCode();
    }
    return true;
  }
  return false;
}

void Allow2VoiceCodeView::UpdateOKButtonState() {
  bool enabled = false;

  // Check if we're in lockout.
  if (lockout_until_ > base::Time::Now()) {
    enabled = false;
  } else {
    std::u16string code(approval_code_field_->GetText());
    enabled = IsValidApprovalCodeFormat(code);
  }

  if (submit_button_) {
    submit_button_->SetEnabled(enabled);
  }
}

void Allow2VoiceCodeView::ShowError(const std::string& message) {
  if (error_label_) {
    error_label_->SetText(base::UTF8ToUTF16(message));
    error_label_->SetVisible(true);
  }
  InvalidateLayout();
}

void Allow2VoiceCodeView::ClearError() {
  if (error_label_) {
    error_label_->SetVisible(false);
  }
}

void Allow2VoiceCodeView::HandleLockout(int seconds_remaining) {
  lockout_until_ = base::Time::Now() + base::Seconds(seconds_remaining);

  // Disable input.
  if (approval_code_field_) {
    approval_code_field_->SetEnabled(false);
  }
  UpdateOKButtonState();

  // Update display immediately.
  UpdateLockoutDisplay();

  // Start countdown timer.
  lockout_timer_.Start(FROM_HERE, base::Seconds(1),
                       base::BindRepeating(&Allow2VoiceCodeView::UpdateLockoutDisplay,
                                           weak_ptr_factory_.GetWeakPtr()));
}

void Allow2VoiceCodeView::OnLockoutTimerExpired() {
  lockout_timer_.Stop();
  lockout_until_ = base::Time();

  // Re-enable input.
  if (approval_code_field_) {
    approval_code_field_->SetEnabled(true);
    approval_code_field_->SetText(u"");
    approval_code_field_->RequestFocus();
  }

  ClearError();
  UpdateOKButtonState();
}

void Allow2VoiceCodeView::UpdateLockoutDisplay() {
  base::TimeDelta remaining = lockout_until_ - base::Time::Now();

  if (remaining <= base::TimeDelta()) {
    OnLockoutTimerExpired();
    return;
  }

  int seconds = static_cast<int>(remaining.InSeconds());
  std::string message = "Too many failed attempts. Try again in " +
                        base::NumberToString(seconds) + " seconds.";
  ShowError(message);
}

std::u16string Allow2VoiceCodeView::FormatCodeForDisplay(
    const std::string& code) {
  std::u16string formatted;

  for (size_t i = 0; i < code.length(); ++i) {
    if (i > 0 && i % 2 == 0) {
      formatted += u" ";
    }
    formatted += base::UTF8ToUTF16(std::string(1, code[i]));
  }

  return formatted;
}

bool Allow2VoiceCodeView::IsValidApprovalCodeFormat(
    const std::u16string& code) const {
  // Must be exactly 6 digits.
  if (code.length() != 6) {
    return false;
  }

  for (char16_t c : code) {
    if (c < '0' || c > '9') {
      return false;
    }
  }

  return true;
}

void Allow2VoiceCodeView::SubmitApprovalCode() {
  // Check lockout.
  if (lockout_until_ > base::Time::Now()) {
    return;
  }

  std::u16string approval_code(approval_code_field_->GetText());

  // Validate format.
  if (!IsValidApprovalCodeFormat(approval_code)) {
    ShowError("Please enter a 6-digit approval code");
    return;
  }

  // TODO: Wire to Allow2VoiceCode service for actual validation.
  // For now, simulate validation failure for demonstration.
  // In the real implementation, this will call:
  //   Allow2Service::ValidateVoiceApprovalCode(request_code_, approval_code, callback)

  // Simulate validation (this would be replaced with actual API call).
  // For demonstration, accept codes that are the reverse of the request code.
  std::string approval_str = base::UTF16ToUTF8(approval_code);

  // Demo validation: Accept if approval code equals request code reversed,
  // or if it matches a known test pattern.
  std::string reversed_request = request_code_;
  std::reverse(reversed_request.begin(), reversed_request.end());

  bool valid = (approval_str == reversed_request);

  if (valid) {
    // Success - invoke callback and close.
    if (callback_) {
      std::move(callback_).Run(true, minutes_requested_);
    }
    GetWidget()->Close();
  } else {
    // Failed attempt.
    failed_attempts_++;

    if (failed_attempts_ >= kMaxFailedAttempts) {
      // Apply exponential backoff lockout.
      int lockout_multiplier = 1 << (failed_attempts_ - kMaxFailedAttempts);
      int lockout_seconds = kBaseLockoutSeconds * lockout_multiplier;

      // Cap at 5 minutes.
      lockout_seconds = std::min(lockout_seconds, 300);

      HandleLockout(lockout_seconds);
    } else {
      int remaining_attempts = kMaxFailedAttempts - failed_attempts_;
      std::string message = "Invalid approval code. " +
                            base::NumberToString(remaining_attempts) +
                            " attempt" +
                            (remaining_attempts == 1 ? "" : "s") +
                            " remaining.";
      ShowError(message);
      approval_code_field_->SelectAll(true);
    }
  }
}

BEGIN_METADATA(Allow2VoiceCodeView)
END_METADATA

}  // namespace allow2

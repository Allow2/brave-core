/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_VOICE_CODE_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_VOICE_CODE_VIEW_H_

#include <cstdint>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;

namespace views {
class Label;
class MdTextButton;
class Textfield;
}  // namespace views

namespace allow2 {

// Voice code entry dialog for offline authorization.
// This dialog enables time extension requests when the child device is offline
// or when push authentication isn't available.
//
// FLOW:
// 1. Dialog displays a 6-digit "request code" prominently for the child to
//    read aloud to their parent (over phone call, in person, etc.)
// 2. Parent enters the request code in their Allow2 app
// 3. Parent approves the request and receives an "approval code"
// 4. Parent reads the 6-digit approval code back to the child
// 5. Child enters the approval code in this dialog
// 6. Dialog validates the code and grants the time extension if valid
//
// SECURITY:
// - Request codes are cryptographically generated and short-lived (5 minutes)
// - Approval codes are HMAC-signed and tied to specific request codes
// - Lockout after 3 failed attempts (30 seconds exponential backoff)
// - Server-side rate limiting prevents brute force attacks
class Allow2VoiceCodeView : public views::DialogDelegateView,
                            public views::TextfieldController {
  METADATA_HEADER(Allow2VoiceCodeView, views::DialogDelegateView)

 public:
  // Callback invoked when voice code validation completes.
  // |success| indicates if the approval code was valid.
  // |minutes_granted| is the number of minutes granted (0 if failed).
  using ApprovalCallback = base::OnceCallback<void(bool success,
                                                    uint16_t minutes_granted)>;

  // Shows the voice code dialog for the given browser.
  // |request_code| is the 6-digit code to display for the parent.
  // |activity_id| is the activity type being requested (for display).
  // |activity_name| is the human-readable activity name.
  // |minutes_requested| is the number of minutes being requested.
  // |callback| is invoked when the dialog completes (success or failure).
  static void Show(Browser* browser,
                   const std::string& request_code,
                   uint8_t activity_id,
                   const std::string& activity_name,
                   uint16_t minutes_requested,
                   ApprovalCallback callback);

  // Returns true if a voice code view is currently showing.
  static bool IsShowing();

  ~Allow2VoiceCodeView() override;

  Allow2VoiceCodeView(const Allow2VoiceCodeView&) = delete;
  Allow2VoiceCodeView& operator=(const Allow2VoiceCodeView&) = delete;

  // views::DialogDelegateView overrides:
  std::u16string GetWindowTitle() const override;
  views::View* GetInitiallyFocusedView() override;
  bool Accept() override;
  ui::mojom::ModalType GetModalType() const override;
  bool ShouldShowCloseButton() const override;
  bool ShouldShowWindowTitle() const override;

  // views::TextfieldController overrides:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  Allow2VoiceCodeView(const std::string& request_code,
                      uint8_t activity_id,
                      const std::string& activity_name,
                      uint16_t minutes_requested,
                      ApprovalCallback callback);

  // Initialize the dialog layout.
  void InitLayout();

  // Update OK button enabled state based on approval code field content.
  void UpdateOKButtonState();

  // Show an error message below the approval code field.
  void ShowError(const std::string& message);

  // Clear any displayed error message.
  void ClearError();

  // Handle lockout state after failed attempts.
  // |seconds_remaining| is the lockout duration remaining.
  void HandleLockout(int seconds_remaining);

  // Called when lockout timer expires.
  void OnLockoutTimerExpired();

  // Update the lockout countdown display.
  void UpdateLockoutDisplay();

  // Format a code string with spaces between digit pairs (e.g., "03 15 42").
  std::u16string FormatCodeForDisplay(const std::string& code);

  // Validate the entered approval code format (6 digits).
  bool IsValidApprovalCodeFormat(const std::u16string& code) const;

  // Submit the approval code for validation.
  void SubmitApprovalCode();

  // The request code to display to the child.
  std::string request_code_;

  // Activity information for display.
  // TODO(allow2): Wire to Allow2VoiceCode service for validation
  [[maybe_unused]] uint8_t activity_id_;
  std::string activity_name_;
  uint16_t minutes_requested_;

  // Callback to invoke on completion.
  ApprovalCallback callback_;

  // UI elements.
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> instruction_label_ = nullptr;
  raw_ptr<views::Label> request_code_label_ = nullptr;
  raw_ptr<views::Label> activity_label_ = nullptr;
  raw_ptr<views::Label> approval_instruction_label_ = nullptr;
  raw_ptr<views::Textfield> approval_code_field_ = nullptr;
  raw_ptr<views::Label> error_label_ = nullptr;
  raw_ptr<views::MdTextButton> submit_button_ = nullptr;

  // Failed attempt tracking for lockout.
  int failed_attempts_ = 0;
  base::Time lockout_until_;
  base::RepeatingTimer lockout_timer_;

  // Maximum failed attempts before lockout.
  static constexpr int kMaxFailedAttempts = 3;

  // Base lockout duration in seconds (doubles with each lockout).
  static constexpr int kBaseLockoutSeconds = 30;

  base::WeakPtrFactory<Allow2VoiceCodeView> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_VOICE_CODE_VIEW_H_

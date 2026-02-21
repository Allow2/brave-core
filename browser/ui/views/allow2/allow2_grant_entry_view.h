/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_GRANT_ENTRY_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_GRANT_ENTRY_VIEW_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/allow2/browser/allow2_qr_token.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;

namespace views {
class Label;
class MdTextButton;
class Textfield;
class View;
}  // namespace views

namespace allow2 {

// Grant entry dialog for offline authorization via QR token codes.
// This dialog enables children to enter grant tokens that parents generate
// in the Allow2 app (typically displayed as QR codes that the child reads).
//
// USE CASE:
// - Parent generates a QR code grant on their phone
// - Parent shows QR code to child OR reads the token string aloud
// - Child enters the token string in this dialog
// - Token is cryptographically verified and grant is applied
//
// Token format: header.payload.signature (base64url encoded, JWT-like)
// The token can be entered directly or pasted from clipboard.
//
// SECURITY:
// - Tokens are Ed25519 signed (cryptographically verified)
// - Tokens include nonce for replay protection
// - Device-specific tokens are validated against current device
// - Expiry times are strictly enforced
// - Public keys must be from trusted Allow2 API
class Allow2GrantEntryView : public views::DialogDelegateView,
                              public views::TextfieldController {
  METADATA_HEADER(Allow2GrantEntryView, views::DialogDelegateView)

 public:
  // Callback invoked when grant is applied.
  // |success| indicates if the grant was valid and applied.
  // |grant| contains the grant details if successful.
  using GrantCallback = base::OnceCallback<void(bool success,
                                                  const QRGrant& grant)>;

  // Shows the grant entry dialog for the given browser.
  // |child_id| is the current child's ID (for validation).
  // |device_id| is the current device's ID (for device-specific grants).
  // |parent_public_key| is the parent's Ed25519 public key for verification.
  // |callback| is invoked when a grant is successfully applied.
  static void Show(Browser* browser,
                   uint64_t child_id,
                   const std::string& device_id,
                   const std::vector<uint8_t>& parent_public_key,
                   GrantCallback callback);

  // Returns true if a grant entry view is currently showing.
  static bool IsShowing();

  // Dismiss any currently showing grant entry view.
  static void Dismiss();

  ~Allow2GrantEntryView() override;

  Allow2GrantEntryView(const Allow2GrantEntryView&) = delete;
  Allow2GrantEntryView& operator=(const Allow2GrantEntryView&) = delete;

  // views::DialogDelegateView overrides:
  std::u16string GetWindowTitle() const override;
  views::View* GetInitiallyFocusedView() override;
  bool Accept() override;
  bool Cancel() override;
  ui::mojom::ModalType GetModalType() const override;
  bool ShouldShowCloseButton() const override;
  bool ShouldShowWindowTitle() const override;

  // views::TextfieldController overrides:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  // Display states for the dialog.
  enum class State {
    kEntry,        // Initial state: showing token entry
    kValidating,   // Validating the token
    kConfirm,      // Showing grant details for confirmation
    kError,        // Showing an error message
    kSuccess,      // Grant applied successfully
  };

  Allow2GrantEntryView(uint64_t child_id,
                        const std::string& device_id,
                        const std::vector<uint8_t>& parent_public_key,
                        GrantCallback callback);

  // Initialize the dialog layout.
  void InitLayout();

  // Create the entry mode view (text input).
  void CreateEntryView();

  // Create the confirmation view (shows grant details).
  void CreateConfirmView();

  // Create the success view.
  void CreateSuccessView();

  // Switch between display states.
  void SetState(State state);

  // Update button states based on current state.
  void UpdateButtonStates();

  // Validate the entered token.
  void ValidateToken();

  // Show an error message.
  void ShowError(const std::string& message);

  // Clear any displayed error.
  void ClearError();

  // Apply the validated grant.
  void ApplyGrant();

  // Format a grant type as human-readable text.
  std::u16string FormatGrantType(QRGrantType type) const;

  // Format minutes as human-readable duration.
  std::u16string FormatDuration(uint16_t minutes) const;

  // Get activity name from activity ID.
  std::u16string GetActivityName(uint8_t activity_id) const;

  // Child ID for validation.
  uint64_t child_id_;

  // Device ID for device-specific grants.
  std::string device_id_;

  // Parent's public key for signature verification.
  std::vector<uint8_t> parent_public_key_;

  // Callback to invoke on successful grant.
  GrantCallback callback_;

  // Current display state.
  State state_ = State::kEntry;

  // Parsed grant (populated after successful validation).
  std::optional<QRGrant> parsed_grant_;

  // UI containers for different states.
  raw_ptr<views::View> entry_container_ = nullptr;
  raw_ptr<views::View> confirm_container_ = nullptr;
  raw_ptr<views::View> success_container_ = nullptr;

  // Entry mode UI elements.
  raw_ptr<views::Label> instruction_label_ = nullptr;
  raw_ptr<views::Textfield> token_field_ = nullptr;
  raw_ptr<views::Label> error_label_ = nullptr;
  raw_ptr<views::MdTextButton> validate_button_ = nullptr;

  // Confirmation mode UI elements.
  raw_ptr<views::Label> confirm_title_label_ = nullptr;
  raw_ptr<views::Label> grant_type_label_ = nullptr;
  raw_ptr<views::Label> activity_label_ = nullptr;
  raw_ptr<views::Label> duration_label_ = nullptr;
  raw_ptr<views::Label> expires_label_ = nullptr;
  raw_ptr<views::MdTextButton> apply_button_ = nullptr;
  raw_ptr<views::MdTextButton> back_button_ = nullptr;

  // Success mode UI elements.
  raw_ptr<views::Label> success_label_ = nullptr;
  raw_ptr<views::Label> success_details_label_ = nullptr;

  base::WeakPtrFactory<Allow2GrantEntryView> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_GRANT_ENTRY_VIEW_H_

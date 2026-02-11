/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_PAIRING_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_PAIRING_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;

namespace gfx {
class ImageSkia;
}  // namespace gfx

namespace views {
class ImageView;
class Label;
class ProgressBar;
}  // namespace views

namespace allow2 {

// Pairing dialog for connecting device to parent's Allow2 account.
// Displays:
// - QR code for parent to scan with Allow2 app
// - 6-digit PIN code as fallback for manual entry
// - Progress indicator while waiting for parent approval
//
// SECURITY: Device NEVER handles parent credentials. Authentication
// happens on the parent's device using passkeys/biometrics.
class Allow2PairingView : public views::DialogDelegateView {
  METADATA_HEADER(Allow2PairingView, views::DialogDelegateView)

 public:
  using PairingCompleteCallback = base::OnceCallback<void(bool success)>;
  using PairingCancelledCallback = base::OnceClosure;

  // Shows the pairing dialog for the given browser.
  // |qr_code_url| is the URL to encode as a QR code.
  // |pin_code| is the 6-digit PIN for manual entry.
  // |session_id| is the pairing session identifier.
  // |poll_callback| is called periodically to check pairing status.
  static void Show(Browser* browser,
                   const std::string& qr_code_url,
                   const std::string& pin_code,
                   const std::string& session_id,
                   base::RepeatingCallback<void(const std::string&)> poll_callback,
                   PairingCompleteCallback complete_callback,
                   PairingCancelledCallback cancelled_callback);

  // Returns true if a pairing view is currently showing.
  static bool IsShowing();

  // Call when pairing is completed (either success or failure).
  void OnPairingComplete(bool success, const std::string& error);

  // Call when parent has scanned the QR code (waiting for approval).
  void OnQRCodeScanned();

  Allow2PairingView(const Allow2PairingView&) = delete;
  Allow2PairingView& operator=(const Allow2PairingView&) = delete;

 private:
  Allow2PairingView(
      Browser* browser,
      const std::string& qr_code_url,
      const std::string& pin_code,
      const std::string& session_id,
      base::RepeatingCallback<void(const std::string&)> poll_callback,
      PairingCompleteCallback complete_callback,
      PairingCancelledCallback cancelled_callback);
  ~Allow2PairingView() override;

  // views::DialogDelegateView overrides:
  ui::mojom::ModalType GetModalType() const override;
  bool ShouldShowCloseButton() const override;
  bool ShouldShowWindowTitle() const override;
  void OnDialogCancelled();

  // Generate QR code image from URL.
  gfx::ImageSkia GenerateQRCode(const std::string& url, int size);

  // Start polling for pairing status.
  void StartPolling();

  // Stop polling.
  void StopPolling();

  // Timer callback for polling.
  void OnPollTimer();

  raw_ptr<Browser> browser_ = nullptr;
  std::string session_id_;

  raw_ptr<views::ImageView> qr_code_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> instruction_label_ = nullptr;
  raw_ptr<views::Label> or_label_ = nullptr;
  raw_ptr<views::Label> pin_label_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::ProgressBar> progress_bar_ = nullptr;

  base::RepeatingCallback<void(const std::string&)> poll_callback_;
  PairingCompleteCallback complete_callback_;
  PairingCancelledCallback cancelled_callback_;

  base::RepeatingTimer poll_timer_;

  base::WeakPtrFactory<Allow2PairingView> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_PAIRING_VIEW_H_

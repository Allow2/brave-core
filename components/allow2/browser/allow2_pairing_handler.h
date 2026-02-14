/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_PAIRING_HANDLER_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_PAIRING_HANDLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/timer/timer.h"

namespace allow2 {

class Allow2ApiClient;
class Allow2CredentialManager;
struct Child;
struct InitPairingResponse;
struct PairingStatusResponse;

// Pairing mode types.
enum class PairingMode {
  kNone,
  kQRCode,
  kPINCode,
};

// Pairing session state.
enum class PairingState {
  kIdle,           // No active session
  kInitializing,   // Initializing session with API
  kWaiting,        // Waiting for parent to scan/enter code
  kScanned,        // Parent scanned QR (for QR mode only)
  kAuthenticating, // Parent is authenticating
  kCompleted,      // Pairing successful
  kExpired,        // Session expired
  kDeclined,       // Parent declined pairing
  kFailed,         // Error occurred
};

// Observer interface for pairing events.
class Allow2PairingHandlerObserver : public base::CheckedObserver {
 public:
  // Called when pairing state changes.
  virtual void OnPairingStateChanged(PairingState state) {}

  // Called when QR code URL is available.
  virtual void OnQRCodeReady(const std::string& qr_code_url) {}

  // Called when PIN code is available.
  virtual void OnPINCodeReady(const std::string& pin_code) {}

  // Called when pairing completes successfully.
  virtual void OnPairingCompleted(const std::vector<Child>& children) {}

  // Called when pairing fails.
  virtual void OnPairingFailed(const std::string& error) {}

  // Called when session expires.
  virtual void OnPairingExpired() {}

  // Called when parent has scanned QR code (still authenticating).
  virtual void OnQRCodeScanned() {}

 protected:
  ~Allow2PairingHandlerObserver() override = default;
};

// Handles the device pairing flow with the Allow2 API.
//
// SECURITY PRINCIPLE: Device NEVER handles parent credentials.
// Authentication happens entirely on the parent's device using
// passkey/biometrics. The child's device only:
// - Displays a QR code or PIN
// - Polls for completion
// - Receives credentials after parent approves
//
// Flow:
// 1. Device calls InitQRPairing or InitPINPairing
// 2. API returns session ID and QR URL/PIN code
// 3. Device displays QR/PIN to user
// 4. Device polls CheckPairingStatus every 2 seconds
// 5. Parent scans QR or enters PIN in their Allow2 app
// 6. Parent authenticates with Face ID/fingerprint
// 7. Poll returns completed=true with credentials
// 8. Device stores credentials and notifies observers
class Allow2PairingHandler {
 public:
  Allow2PairingHandler(Allow2ApiClient* api_client,
                       Allow2CredentialManager* credential_manager);
  ~Allow2PairingHandler();

  Allow2PairingHandler(const Allow2PairingHandler&) = delete;
  Allow2PairingHandler& operator=(const Allow2PairingHandler&) = delete;

  // Observer management.
  void AddObserver(Allow2PairingHandlerObserver* observer);
  void RemoveObserver(Allow2PairingHandlerObserver* observer);

  // ============================================================================
  // Pairing Flow
  // ============================================================================

  // Start QR code pairing flow.
  // |device_name| - Human-readable name for this device.
  void StartQRPairing(const std::string& device_name);

  // Start PIN code pairing flow.
  // |device_name| - Human-readable name for this device.
  void StartPINPairing(const std::string& device_name);

  // Cancel the current pairing session.
  void CancelPairing();

  // Retry pairing (after failure or expiry).
  void RetryPairing();

  // ============================================================================
  // State Queries
  // ============================================================================

  // Get current pairing state.
  PairingState GetState() const;

  // Get current pairing mode.
  PairingMode GetMode() const;

  // Check if pairing is currently in progress.
  bool IsPairingInProgress() const;

  // Get the QR code as base64-encoded PNG data URL (only valid in QR mode).
  std::string GetQRCodeUrl() const;

  // Get the web pairing URL (the URL encoded in the QR code).
  std::string GetWebPairingUrl() const;

  // Get the PIN code (only valid in PIN mode after initialization).
  std::string GetPINCode() const;

  // Get the session ID.
  std::string GetSessionId() const;

  // Get time remaining before session expires.
  base::TimeDelta GetTimeRemaining() const;

  // Get the last error message.
  std::string GetLastError() const;

 private:
  // Handle initialization response.
  void OnInitComplete(const InitPairingResponse& response);

  // Handle status check response.
  void OnStatusCheckComplete(const PairingStatusResponse& response);

  // Handle successful pairing.
  void OnPairingSuccess(const PairingStatusResponse& response);

  // Handle pairing failure.
  void OnPairingFailure(const std::string& error);

  // Start polling for status.
  void StartPolling();

  // Stop polling.
  void StopPolling();

  // Poll timer callback.
  void OnPollTimer();

  // Session expiry timer callback.
  void OnExpiryTimer();

  // Update state and notify observers.
  void SetState(PairingState state);

  // Notify observers of error.
  void NotifyError(const std::string& error);

  // Clean up session state.
  void ResetSession();

  raw_ptr<Allow2ApiClient> api_client_;
  raw_ptr<Allow2CredentialManager> credential_manager_;

  // Current session state.
  PairingState state_ = PairingState::kIdle;
  PairingMode mode_ = PairingMode::kNone;

  // Session data.
  std::string session_id_;
  std::string qr_code_url_;       // Base64-encoded PNG image
  std::string web_pairing_url_;   // URL encoded in QR code
  std::string pin_code_;
  std::string device_name_;
  std::string last_error_;

  // Session timing.
  base::Time session_start_time_;
  int expires_in_seconds_ = 0;

  // Polling timer (every 2 seconds).
  base::RepeatingTimer poll_timer_;
  static constexpr base::TimeDelta kPollInterval = base::Seconds(2);

  // Expiry timer.
  base::OneShotTimer expiry_timer_;

  // Observers.
  base::ObserverList<Allow2PairingHandlerObserver> observers_;

  base::WeakPtrFactory<Allow2PairingHandler> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_PAIRING_HANDLER_H_

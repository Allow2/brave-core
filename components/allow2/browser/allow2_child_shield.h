/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_SHIELD_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_SHIELD_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace network {
class NetworkConnectionTracker;
}  // namespace network

namespace allow2 {

class Allow2ApiClient;

struct ChildInfo;
class Allow2ChildManager;

// Shield state for UI display.
enum class ShieldState {
  kHidden,                    // Shield not visible
  kSelectingChild,            // User is selecting which child they are
  kEnteringPIN,               // User has selected child, entering PIN
  kValidating,                // Validating PIN with server
  kError,                     // Showing error (wrong PIN, locked out)
  kSuccess,                   // PIN accepted, about to dismiss
  kWaitingForConfirmation,    // Waiting for child to confirm via push/app
  kConfirmationTimeout,       // Push auth timed out, showing fallback options
  kConfirmationDenied,        // Child denied the auth request
};

// Observer interface for child shield events.
class Allow2ChildShieldObserver : public base::CheckedObserver {
 public:
  // Called when shield state changes.
  virtual void OnShieldStateChanged(ShieldState state) {}

  // Called when a child is selected (before PIN entry).
  virtual void OnChildSelected(uint64_t child_id) {}

  // Called when PIN validation succeeds.
  virtual void OnPINAccepted(uint64_t child_id) {}

  // Called when PIN validation fails.
  virtual void OnPINRejected(const std::string& error) {}

  // Called when shield is dismissed.
  virtual void OnShieldDismissed() {}

  // Called when lockout starts.
  virtual void OnLockoutStarted(int lockout_seconds) {}

  // Called when push auth request is sent and we're waiting for confirmation.
  virtual void OnWaitingForConfirmation(uint64_t child_id,
                                        const std::string& child_name) {}

  // Called when push auth times out.
  virtual void OnConfirmationTimeout(uint64_t child_id,
                                     const std::string& child_name) {}

  // Called when child denies the auth request.
  virtual void OnConfirmationDenied(uint64_t child_id) {}

  // Called when child confirms the auth request.
  virtual void OnConfirmationAccepted(uint64_t child_id) {}

 protected:
  ~Allow2ChildShieldObserver() override = default;
};

// Configuration for the child shield display.
struct ChildShieldConfig {
  // Whether to show avatar images.
  bool show_avatars = true;

  // Whether to include "Guest" option.
  bool allow_guest = false;

  // Whether to show "Continue without account" link.
  bool show_skip_link = false;

  // Custom title text (empty for default).
  std::string custom_title;

  // Custom subtitle text (empty for default).
  std::string custom_subtitle;
};

// Controller for the child selection shield UI.
// The shield appears when:
// - App launches (cold start) on a paired shared device
// - Resuming from background after 5+ minutes
// - User explicitly triggers "Switch User"
// - Session timeout
//
// UI Layout:
// +------------------------------------------+
// |                                          |
// |         Who's using Brave?               |
// |                                          |
// |   [Emma]    [Jack]    [Guest]            |
// |    (avatar)  (avatar)  (avatar)          |
// |                                          |
// |  ----------------------------------------|
// |                                          |
// |         Enter PIN: [* * * *]             |
// |                                          |
// |   This helps track time limits and       |
// |   keep you safe                          |
// |                                          |
// +------------------------------------------+
class Allow2ChildShield {
 public:
  using SelectCallback =
      base::OnceCallback<void(bool success, const std::string& error)>;

  // Constructor with API client for push auth flow.
  Allow2ChildShield(Allow2ChildManager* child_manager,
                    Allow2ApiClient* api_client);
  ~Allow2ChildShield();

  // Legacy constructor (without API client, push auth disabled).
  explicit Allow2ChildShield(Allow2ChildManager* child_manager);

  Allow2ChildShield(const Allow2ChildShield&) = delete;
  Allow2ChildShield& operator=(const Allow2ChildShield&) = delete;

  // Observer management.
  void AddObserver(Allow2ChildShieldObserver* observer);
  void RemoveObserver(Allow2ChildShieldObserver* observer);

  // ============================================================================
  // Shield Control
  // ============================================================================

  // Show the child shield.
  void Show();

  // Show with custom configuration.
  void Show(const ChildShieldConfig& config);

  // Dismiss the shield.
  void Dismiss();

  // Check if shield is currently visible.
  bool IsVisible() const;

  // Get current shield state.
  ShieldState GetState() const;

  // Get the current configuration.
  const ChildShieldConfig& GetConfig() const;

  // ============================================================================
  // Child Selection
  // ============================================================================

  // Get list of children to display.
  std::vector<ChildInfo> GetChildren() const;

  // Select a child (moves to PIN entry state).
  void SelectChild(uint64_t child_id);

  // Go back to child selection (from PIN entry).
  void GoBackToSelection();

  // Get currently selected child ID (during PIN entry).
  uint64_t GetSelectedChildId() const;

  // Get name of selected child.
  std::string GetSelectedChildName() const;

  // ============================================================================
  // PIN Entry
  // ============================================================================

  // Submit PIN for the selected child.
  void SubmitPIN(const std::string& pin);

  // Submit PIN with async callback.
  void SubmitPINAsync(const std::string& pin, SelectCallback callback);

  // Clear entered PIN.
  void ClearPIN();

  // Get remaining PIN attempts.
  int GetRemainingPINAttempts() const;

  // Check if PIN entry is locked out.
  bool IsLockedOut() const;

  // Get lockout time remaining in seconds.
  int GetLockoutSecondsRemaining() const;

  // ============================================================================
  // Error Handling
  // ============================================================================

  // Show an error message.
  void ShowError(const std::string& error);

  // Clear error state.
  void ClearError();

  // Get current error message.
  std::string GetError() const;

  // ============================================================================
  // Guest Mode
  // ============================================================================

  // Select guest mode (if allowed).
  void SelectGuest();

  // Skip child selection (if allowed).
  void Skip();

  // ============================================================================
  // Push Authentication (for children with hasAccount: true)
  // ============================================================================

  // Check if network is available.
  bool IsNetworkAvailable() const;

  // Set the network connection tracker for connectivity checks.
  void SetNetworkConnectionTracker(
      network::NetworkConnectionTracker* tracker);

  // Request push authentication for a child with an account.
  // Called internally when selecting a child with hasAccount=true.
  void RequestPushAuth(uint64_t child_id);

  // Cancel the current push auth request.
  void CancelPushAuth();

  // Fall back to PIN entry from push auth.
  void FallbackToPIN();

  // Get the current auth request ID (for status polling).
  std::string GetCurrentAuthRequestId() const;

  // Get seconds remaining until push auth timeout.
  int GetPushAuthSecondsRemaining() const;

  // Check if push auth is currently in progress.
  bool IsPushAuthInProgress() const;

 private:
  // Set state and notify observers.
  void SetState(ShieldState state);

  // Handle PIN validation result.
  void OnPINValidationResult(bool success, const std::string& error);

  // Find child by ID.
  std::optional<ChildInfo> FindChild(uint64_t child_id) const;

  // ============================================================================
  // Push Auth Internals
  // ============================================================================

  // Handle push auth request response.
  void OnPushAuthRequestComplete(const std::string& request_id,
                                  const std::string& error);

  // Handle push auth status poll response.
  void OnPushAuthStatusComplete(const std::string& status,
                                 const std::string& error);

  // Called when push auth timeout expires.
  void OnPushAuthTimeout();

  // Start polling for push auth status.
  void StartPushAuthPolling();

  // Stop polling for push auth status.
  void StopPushAuthPolling();

  // Poll for push auth status.
  void PollPushAuthStatus();

  raw_ptr<Allow2ChildManager> child_manager_;
  raw_ptr<Allow2ApiClient> api_client_ = nullptr;
  raw_ptr<network::NetworkConnectionTracker> network_tracker_ = nullptr;

  ShieldState state_ = ShieldState::kHidden;
  ChildShieldConfig config_;

  uint64_t selected_child_id_ = 0;
  std::string current_error_;

  // Push auth state.
  std::string current_auth_request_id_;
  base::Time push_auth_start_time_;
  base::RepeatingTimer push_auth_poll_timer_;
  base::OneShotTimer push_auth_timeout_timer_;

  // Push auth constants.
  static constexpr int kPushAuthTimeoutSeconds = 60;
  static constexpr int kPushAuthPollIntervalMs = 2000;

  base::ObserverList<Allow2ChildShieldObserver> observers_;
  base::WeakPtrFactory<Allow2ChildShield> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_SHIELD_H_

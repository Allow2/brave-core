/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_SERVICE_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "brave/components/allow2/browser/allow2_child_shield.h"
#include "brave/components/allow2/browser/allow2_usage_tracker.h"
#include "brave/components/allow2/browser/allow2_warning_controller.h"
#include "brave/components/allow2/common/allow2_constants.h"
#include "components/keyed_service/core/keyed_service.h"

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

class PrefService;

namespace allow2 {

class Allow2ApiClient;
class Allow2BlockOverlay;
class Allow2ChildManager;
class Allow2ChildShield;
class Allow2CredentialManager;
class Allow2DeficitTracker;
class Allow2LocalDecision;
class Allow2OfflineCache;
class Allow2PairingHandler;
class Allow2TravelMode;
class Allow2UsageTracker;
class Allow2WarningBanner;
class Allow2WarningController;

// Represents a child account.
struct Child {
  Child();
  ~Child();
  Child(const Child&);
  Child& operator=(const Child&);
  Child(Child&&);
  Child& operator=(Child&&);

  uint64_t id = 0;
  std::string name;
  std::string pin_hash;
  std::string pin_salt;

  // Avatar URL (from linked account or child entity).
  std::string avatar_url;

  // Linked Allow2 account ID (if child has their own account).
  uint64_t linked_account_id = 0;

  // Assigned color (hex string like "#FF5733" or index 0-9).
  std::string color;

  // True if child has their own Allow2 account (can use push authentication).
  // False for "name-only" children who can only use PIN.
  bool has_account = false;
};

// Represents the result of an Allow2 check.
struct CheckResult {
  CheckResult();
  ~CheckResult();
  CheckResult(const CheckResult&);
  CheckResult& operator=(const CheckResult&);
  CheckResult(CheckResult&&);
  CheckResult& operator=(CheckResult&&);

  bool allowed = true;
  int remaining_seconds = 0;
  int64_t expires = 0;
  bool banned = false;
  std::string day_type;
  std::string block_reason;
};

// Observer interface for Allow2 state changes.
class Allow2ServiceObserver : public base::CheckedObserver {
 public:
  // Called when the paired state changes (paired/unpaired).
  virtual void OnPairedStateChanged(bool is_paired) {}

  // Called when the blocking state changes.
  virtual void OnBlockingStateChanged(bool is_blocked,
                                      const std::string& reason) {}

  // Called when remaining time is updated.
  virtual void OnRemainingTimeUpdated(int remaining_seconds) {}

  // Called when a warning threshold is reached.
  virtual void OnWarningThresholdReached(int remaining_seconds) {}

  // Called when the current child changes.
  virtual void OnCurrentChildChanged(const std::optional<Child>& child) {}

  // Called when credentials are invalidated (e.g., 401 from API).
  virtual void OnCredentialsInvalidated() {}

 protected:
  ~Allow2ServiceObserver() override = default;
};

// Main service for Allow2 parental controls integration.
// This service handles:
// - Device pairing with parent's Allow2 account
// - Usage tracking and time limit enforcement
// - Blocking overlay management
// - "Request more time" functionality
//
// SECURITY NOTE: Device cannot unpair itself. Unpair is only possible
// from parent's Allow2 portal/app. When released remotely, device
// receives 401 on next check and clears local credentials.
class Allow2Service : public KeyedService,
                      public Allow2UsageTrackerObserver,
                      public Allow2ChildShieldObserver {
 public:
  using PairCallback =
      base::OnceCallback<void(bool success, const std::string& error)>;
  using CheckCallback = base::OnceCallback<void(const CheckResult& result)>;
  using RequestTimeCallback =
      base::OnceCallback<void(bool success, const std::string& error)>;

  Allow2Service(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      PrefService* local_state,
      PrefService* profile_prefs);
  ~Allow2Service() override;

  Allow2Service(const Allow2Service&) = delete;
  Allow2Service& operator=(const Allow2Service&) = delete;

  // Observer management.
  void AddObserver(Allow2ServiceObserver* observer);
  void RemoveObserver(Allow2ServiceObserver* observer);

  // ============================================================================
  // Pairing (QR/PIN only - device NEVER handles parent credentials)
  // ============================================================================

  // Check if device is paired with an Allow2 account.
  bool IsPaired() const;

  // Represents a pairing session initiated by this device.
  struct PairingSession {
    PairingSession();
    ~PairingSession();
    PairingSession(const PairingSession&);
    PairingSession& operator=(const PairingSession&);
    PairingSession(PairingSession&&);
    PairingSession& operator=(PairingSession&&);

    std::string session_id;
    std::string qr_code_data;      // Base64-encoded QR code image
    std::string web_pairing_url;   // Universal link URL (encoded in QR, enables deep linking)
    std::string pin_code;          // 6-digit PIN for manual entry
  };

  using InitPairingCallback =
      base::OnceCallback<void(bool success, const PairingSession& session,
                              const std::string& error)>;
  using PairingStatusCallback =
      base::OnceCallback<void(bool completed, bool success,
                              const std::string& error)>;

  // Initialize QR code pairing session.
  // Device displays QR code, parent scans with their Allow2 app.
  // Parent authenticates with passkey/biometrics on their device.
  // Device NEVER sees parent credentials.
  void InitQRPairing(const std::string& device_name,
                     InitPairingCallback callback);

  // Initialize PIN code pairing session (fallback for QR).
  // Device displays 6-digit PIN, parent enters in their Allow2 app.
  // Parent authenticates with passkey/biometrics on their device.
  // Device NEVER sees parent credentials.
  void InitPINPairing(const std::string& device_name,
                      InitPairingCallback callback);

  // Check status of pairing session (poll until completed).
  // Returns completed=true when parent has approved the pairing.
  void CheckPairingStatus(const std::string& session_id,
                          PairingStatusCallback callback);

  // Cancel an active pairing session.
  void CancelPairing(const std::string& session_id);

  // Get the list of children associated with the paired account.
  std::vector<Child> GetChildren() const;

  // Get the name of the account owner (controller).
  std::string GetAccountOwnerName() const;

  // ============================================================================
  // Child Selection (Shared Device Mode)
  // ============================================================================

  // Check if device is in shared mode (no specific child locked).
  bool IsSharedDeviceMode() const;

  // Select a child for the current session (shared device mode).
  // Returns false if PIN validation fails.
  bool SelectChild(uint64_t child_id, const std::string& pin);

  // Select a child with debug info output (for PIN debugging).
  // If debug_info is non-null, it will be filled with hash comparison info.
  bool SelectChildWithDebug(uint64_t child_id,
                            const std::string& pin,
                            std::string* debug_info);

  // ============================================================================
  // PIN Lockout (Rate Limiting)
  // ============================================================================

  // Check if a user is currently locked out from PIN entry.
  // Returns true if locked out, and sets |remaining_seconds| if non-null.
  bool IsUserLockedOut(uint64_t child_id, int* remaining_seconds = nullptr) const;

  // Record a failed PIN attempt for a user. Returns the new lockout duration
  // in seconds (0 if no lockout triggered yet).
  int RecordFailedPinAttempt(uint64_t child_id);

  // Clear lockout state for a user (called on successful login).
  void ClearPinLockout(uint64_t child_id);

  // Get all users currently in lockout with their remaining seconds.
  std::map<uint64_t, int> GetLockedOutUsers() const;

  // Get the currently selected child, or nullopt if none selected.
  std::optional<Child> GetCurrentChild() const;

  // Clear the current child selection (return to selection screen).
  void ClearCurrentChild();

  // ============================================================================
  // Usage Tracking and Blocking
  // ============================================================================

  // Check current allowances with the Allow2 API.
  // Should be called periodically (every 10 seconds) and on navigation.
  void CheckAllowance(CheckCallback callback);

  // Track a URL visit for the current activity type.
  void TrackUrl(const std::string& url);

  // Check if browsing is currently blocked.
  bool IsBlocked() const;

  // Get the remaining time in seconds.
  int GetRemainingSeconds() const;

  // Get the current block reason, if blocked.
  std::string GetBlockReason() const;

  // ============================================================================
  // Request More Time
  // ============================================================================

  // Request additional time from parent.
  // |activity_id| is the activity type (e.g., kInternet).
  // |minutes| is the requested duration.
  // |message| is an optional message to the parent.
  void RequestMoreTime(ActivityId activity_id,
                       int minutes,
                       const std::string& message,
                       RequestTimeCallback callback);

  // ============================================================================
  // Enabled State
  // ============================================================================

  // Check if Allow2 is enabled for this profile.
  bool IsEnabled() const;

  // Enable or disable Allow2 for this profile.
  // Note: This does not unpair the device, just disables enforcement.
  void SetEnabled(bool enabled);

  // Get credential manager for testing.
  Allow2CredentialManager* GetCredentialManagerForTesting();

  // ============================================================================
  // Component Accessors
  // ============================================================================

  // Get the block overlay controller.
  Allow2BlockOverlay* GetBlockOverlay();

  // Get the child shield controller.
  Allow2ChildShield* GetChildShield();

  // Get the warning banner controller.
  Allow2WarningBanner* GetWarningBanner();

  // Get the child manager.
  Allow2ChildManager* GetChildManager();

  // Get the pairing handler.
  Allow2PairingHandler* GetPairingHandler();

  // Get the usage tracker.
  Allow2UsageTracker* GetUsageTracker();

  // ============================================================================
  // Offline Authentication
  // ============================================================================

  // Get the offline cache.
  Allow2OfflineCache* GetOfflineCache();

  // Get the local decision engine.
  Allow2LocalDecision* GetLocalDecision();

  // Get the deficit tracker.
  Allow2DeficitTracker* GetDeficitTracker();

  // Get the travel mode handler.
  Allow2TravelMode* GetTravelMode();

  // Check if device is currently offline.
  bool IsOffline() const;

  // Grant time using a voice code (offline capable).
  using VoiceCodeCallback =
      base::OnceCallback<void(bool success, int granted_minutes,
                              const std::string& error)>;
  void GrantTimeWithVoiceCode(const std::string& voice_code,
                               VoiceCodeCallback callback);

  // Show the voice code entry UI.
  void ShowVoiceCodeUI();

  // ============================================================================
  // Tracking Control
  // ============================================================================

  // Start usage tracking for the current child.
  void StartTracking();

  // Stop usage tracking.
  void StopTracking();

  // Pause tracking (e.g., app goes to background).
  void PauseTracking();

  // Resume tracking (e.g., app returns to foreground).
  void ResumeTracking();

  // Check if tracking is active.
  bool IsTrackingActive() const;

  // ============================================================================
  // UI State Helpers
  // ============================================================================

  // Check if the block overlay should be shown.
  bool ShouldShowBlockOverlay() const;

  // Check if the child selection shield should be shown.
  bool ShouldShowChildShield() const;

  // Show the block overlay.
  void ShowBlockOverlay();

  // Dismiss the block overlay (e.g., time was granted).
  void DismissBlockOverlay();

  // Show the child selection shield.
  void ShowChildShield();

  // Dismiss the child selection shield.
  void DismissChildShield();

  // Get a weak pointer to this service.
  base::WeakPtr<Allow2Service> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  // KeyedService overrides.
  void Shutdown() override;

  // Allow2UsageTrackerObserver overrides.
  void OnCheckResult(const CheckResult& result) override;
  void OnTrackingStarted() override;
  void OnTrackingStopped() override;
  void OnCredentialsInvalidated() override;

  // Allow2ChildShieldObserver overrides.
  void OnPINAccepted(uint64_t child_id) override;

  // Start the periodic check timer.
  void StartCheckTimer();

  // Stop the periodic check timer.
  void StopCheckTimer();

  // Internal handler for check result (shared by direct calls and observer).
  void OnCheckResultInternal(const CheckResult& result);

  // Internal handler for credentials invalidated.
  void OnCredentialsInvalidatedInternal();

  // Initialize tracking components (called when device is paired).
  // Creates child manager, usage tracker, block overlay, offline cache, etc.
  void InitializeTrackingComponents();

  // Destroy tracking components (called when device is released).
  // Resets all tracking-related unique_ptrs to nullptr.
  void DestroyTrackingComponents();

  // Set up observers for tracking components.
  void SetupTrackingObservers();

  // Tear down observers for tracking components.
  void TeardownTrackingObservers();

  // Callback when "request time" is clicked in block overlay.
  void OnRequestTimeFromOverlay(int minutes, const std::string& message);

  // Callback when "request time" is clicked in warning banner.
  void OnRequestTimeFromBanner();

  // Notify observers of blocking state change.
  void NotifyBlockingStateChanged(bool is_blocked, const std::string& reason);

  // Notify observers of remaining time update.
  void NotifyRemainingTimeUpdated(int remaining_seconds);

  // Notify observers of warning threshold.
  void NotifyWarningThreshold(WarningLevel level, int remaining_seconds);

  // Check if we should show a warning for the given remaining time.
  bool ShouldShowWarning(int remaining_seconds) const;

  // Cache check result to prefs for offline operation.
  void CacheCheckResult(const CheckResult& result);

  // Load cached check result from prefs.
  std::optional<CheckResult> LoadCachedCheckResult() const;

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<PrefService> local_state_ = nullptr;
  raw_ptr<PrefService> profile_prefs_ = nullptr;

  std::unique_ptr<Allow2ApiClient> api_client_;
  std::unique_ptr<Allow2BlockOverlay> block_overlay_;
  std::unique_ptr<Allow2ChildManager> child_manager_;
  std::unique_ptr<Allow2ChildShield> child_shield_;
  std::unique_ptr<Allow2CredentialManager> credential_manager_;
  std::unique_ptr<Allow2PairingHandler> pairing_handler_;
  std::unique_ptr<Allow2UsageTracker> usage_tracker_;
  std::unique_ptr<Allow2WarningBanner> warning_banner_;
  std::unique_ptr<Allow2WarningController> warning_controller_;

  // Offline authentication components.
  std::unique_ptr<Allow2OfflineCache> offline_cache_;
  std::unique_ptr<Allow2LocalDecision> local_decision_;
  std::unique_ptr<Allow2DeficitTracker> deficit_tracker_;
  std::unique_ptr<Allow2TravelMode> travel_mode_;

  // Periodic check timer.
  base::RepeatingTimer check_timer_;

  // Current blocking state.
  bool is_blocked_ = false;
  std::string block_reason_;
  int remaining_seconds_ = 0;

  // Last warning level that was notified.
  WarningLevel last_warning_level_ = WarningLevel::kNone;

  // Whether tracking components have been initialized.
  // Only true when device is paired. Set to false on release.
  bool tracking_initialized_ = false;

  // PIN lockout tracking (rate limiting).
  // Maps child_id (0 = parent) to failed attempt count.
  std::map<uint64_t, int> pin_failed_attempts_;
  // Maps child_id to lockout expiry time.
  std::map<uint64_t, base::Time> pin_lockout_until_;

  // Observers.
  base::ObserverList<Allow2ServiceObserver> observers_;

  base::WeakPtrFactory<Allow2Service> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_SERVICE_H_

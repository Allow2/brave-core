/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_USAGE_TRACKER_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_USAGE_TRACKER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/timer/timer.h"
#include "brave/components/allow2/common/allow2_constants.h"

namespace allow2 {

class Allow2ApiClient;
struct Credentials;
struct CheckResult;

// Observer interface for usage tracking events.
class Allow2UsageTrackerObserver : public base::CheckedObserver {
 public:
  // Called when a check result is received.
  virtual void OnCheckResult(const CheckResult& result) {}

  // Called when tracking starts.
  virtual void OnTrackingStarted() {}

  // Called when tracking stops.
  virtual void OnTrackingStopped() {}

  // Called when credentials are invalidated (HTTP 401).
  virtual void OnCredentialsInvalidated() {}

 protected:
  ~Allow2UsageTrackerObserver() override = default;
};

// Handles periodic usage tracking with the Allow2 API.
// Performs checks every 10 seconds to:
// - Report activity (log usage)
// - Get remaining time
// - Detect blocking conditions
//
// This is separate from Allow2Service to allow:
// - Independent testing
// - Clear separation of concerns
// - Pause/resume without affecting other service functionality
class Allow2UsageTracker {
 public:
  explicit Allow2UsageTracker(Allow2ApiClient* api_client);
  ~Allow2UsageTracker();

  Allow2UsageTracker(const Allow2UsageTracker&) = delete;
  Allow2UsageTracker& operator=(const Allow2UsageTracker&) = delete;

  // Observer management.
  void AddObserver(Allow2UsageTrackerObserver* observer);
  void RemoveObserver(Allow2UsageTrackerObserver* observer);

  // ============================================================================
  // Tracking Control
  // ============================================================================

  // Start periodic tracking.
  // |credentials| - API credentials for check calls.
  // |child_id| - ID of the child to track.
  void StartTracking(const Credentials& credentials, uint64_t child_id);

  // Stop periodic tracking.
  void StopTracking();

  // Pause tracking temporarily (e.g., app goes to background).
  void PauseTracking();

  // Resume tracking after pause.
  void ResumeTracking();

  // Check if tracking is currently active.
  bool IsTracking() const;

  // Check if tracking is paused.
  bool IsPaused() const;

  // ============================================================================
  // URL Tracking
  // ============================================================================

  // Track a URL visit (triggers activity categorization).
  void TrackUrl(const std::string& url);

  // Track with a specific activity type.
  void TrackActivity(ActivityId activity);

  // ============================================================================
  // Manual Check
  // ============================================================================

  // Perform an immediate check (outside the normal interval).
  // Useful when navigating to important pages.
  using CheckCallback = base::OnceCallback<void(const CheckResult& result)>;
  void CheckNow(CheckCallback callback);

  // ============================================================================
  // Configuration
  // ============================================================================

  // Set the check interval (default: 10 seconds).
  void SetCheckInterval(base::TimeDelta interval);

  // Get the current check interval.
  base::TimeDelta GetCheckInterval() const;

  // Set whether to log usage to API (can be disabled for testing).
  void SetLogUsage(bool log);

  // Get current child ID.
  uint64_t GetChildId() const;

  // Update credentials (e.g., after token refresh).
  void UpdateCredentials(const Credentials& credentials);

  // Update child ID (e.g., after switching child).
  void UpdateChildId(uint64_t child_id);

 private:
  // Timer callback for periodic checks.
  void OnCheckTimer();

  // Handle check result from API.
  void OnCheckComplete(CheckCallback callback, bool success, int http_status,
                       const CheckResult& result);

  // Perform the actual API check.
  void PerformCheck(CheckCallback callback);

  // Notify observers of check result.
  void NotifyCheckResult(const CheckResult& result);

  // Notify observers of credentials invalidated.
  void NotifyCredentialsInvalidated();

  raw_ptr<Allow2ApiClient> api_client_;

  // Current tracking state.
  bool is_tracking_ = false;
  bool is_paused_ = false;
  bool log_usage_ = true;

  // Credentials and child info.
  std::unique_ptr<Credentials> credentials_;
  uint64_t child_id_ = 0;

  // Current tracked activity.
  ActivityId current_activity_ = ActivityId::kInternet;

  // Check timer.
  base::RepeatingTimer check_timer_;
  base::TimeDelta check_interval_ = base::Seconds(kAllow2CheckIntervalSeconds);

  // Observers.
  base::ObserverList<Allow2UsageTrackerObserver> observers_;

  base::WeakPtrFactory<Allow2UsageTracker> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_USAGE_TRACKER_H_

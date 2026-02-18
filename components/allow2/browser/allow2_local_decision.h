/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_LOCAL_DECISION_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_LOCAL_DECISION_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"

namespace allow2 {

class Allow2OfflineCache;

// Result of offline permission check.
// This structure contains all information needed to make UI decisions
// when the device is offline or operating from cached data.
struct LocalDecisionResult {
  LocalDecisionResult();
  ~LocalDecisionResult();
  LocalDecisionResult(const LocalDecisionResult&);
  LocalDecisionResult& operator=(const LocalDecisionResult&);
  LocalDecisionResult(LocalDecisionResult&&);
  LocalDecisionResult& operator=(LocalDecisionResult&&);

  bool allowed = false;
  std::string reason;  // "allowed", "quota_exhausted", "outside_time_block",
                       // "banned", "exception_allowed", "extension_active"
  uint16_t remaining_minutes = 0;
  uint16_t quota_used = 0;
  uint16_t quota_total = 0;
  bool has_extension = false;
  uint16_t extension_remaining = 0;

  // For UI display.
  // Returns a user-friendly message based on the decision result.
  // Examples:
  // - "You have 15 minutes remaining"
  // - "Time's up! Quota exhausted for today"
  // - "Blocked until 3:00 PM"
  // - "This activity is banned"
  // - "Extension active: 10 minutes remaining"
  std::string GetDisplayMessage() const;

  // Helper to create common result types.
  static LocalDecisionResult Allowed(uint16_t remaining_minutes,
                                      uint16_t quota_used,
                                      uint16_t quota_total);
  static LocalDecisionResult QuotaExhausted(uint16_t quota_used,
                                             uint16_t quota_total);
  static LocalDecisionResult OutsideTimeBlock();
  static LocalDecisionResult Banned(const std::string& ban_reason);
  static LocalDecisionResult ExceptionAllowed();
  static LocalDecisionResult ExtensionActive(uint16_t extension_remaining,
                                              uint16_t remaining_minutes);
};

// Local decision engine using cached data.
//
// This engine evaluates activity permissions using locally cached data
// from the Allow2 API. It implements the same decision priority as the
// server:
//
// DECISION PRIORITY (in order):
// 1. EXCEPTIONS - Always allowed (emergency services, whitelisted sites)
// 2. BANS - Blocked until ban expires (explicit ban on activity)
// 3. EXTENSIONS - Allowed during extension window (parent-granted extra time)
// 4. TIME BLOCKS - Blocked outside allowed time windows
// 5. ALLOWANCES - Blocked when daily quota exhausted
//
// The engine is designed to work completely offline once the cache is
// populated with data from a successful API check. This provides a
// seamless experience when network connectivity is intermittent.
//
// USAGE:
//   Allow2LocalDecision decision(offline_cache);
//   LocalDecisionResult result = decision.Check(activity_id);
//   if (!result.allowed) {
//     ShowBlockOverlay(result.reason, result.GetDisplayMessage());
//   }
//
// IMPORTANT: This class does NOT make any network calls. All data comes
// from the Allow2OfflineCache which is populated by Allow2UsageTracker.
class Allow2LocalDecision {
 public:
  explicit Allow2LocalDecision(Allow2OfflineCache* cache);
  ~Allow2LocalDecision();

  Allow2LocalDecision(const Allow2LocalDecision&) = delete;
  Allow2LocalDecision& operator=(const Allow2LocalDecision&) = delete;

  // ============================================================================
  // Permission Checking
  // ============================================================================

  // Check permission for activity at current time.
  // Returns a LocalDecisionResult with allowed=true/false and reason.
  LocalDecisionResult Check(uint8_t activity_id) const;

  // Check permission for specific time (for predictive warnings).
  // Useful for showing "This activity will be blocked at 9:00 PM" messages.
  LocalDecisionResult CheckAt(uint8_t activity_id, base::Time time) const;

  // ============================================================================
  // Usage Logging
  // ============================================================================

  // Log usage locally (called every check interval when allowed).
  // This updates the local quota tracking. The logged usage is later
  // synchronized with the server when connectivity is restored.
  void LogUsage(uint8_t activity_id, uint16_t minutes);

  // ============================================================================
  // Time Calculations
  // ============================================================================

  // Get effective remaining time across all factors.
  // This considers:
  // - Quota remaining
  // - Time block end time
  // - Extension expiry
  // Returns the minimum of all applicable limits.
  uint16_t GetEffectiveRemaining(uint8_t activity_id) const;

  // ============================================================================
  // Extensions
  // ============================================================================

  // Apply offline-granted extension.
  // This is used when a parent grants extra time while offline
  // (e.g., via emergency passcode).
  void ApplyExtension(uint8_t activity_id,
                      uint16_t minutes,
                      base::Time expires_at);

  // ============================================================================
  // State Queries
  // ============================================================================

  // Check if we're in offline mode (cache valid but no network).
  bool IsOfflineMode() const;

  // Get cache age in seconds.
  // Returns -1 if cache is invalid/empty.
  int GetCacheAgeSeconds() const;

  // Check if cache is still valid (not expired).
  // Default expiry is 24 hours.
  bool IsCacheValid() const;

  // ============================================================================
  // Warning Thresholds
  // ============================================================================

  // Get warning thresholds crossed since previous_remaining.
  // Returns a vector of threshold values (in minutes) that were crossed.
  // Used to trigger warning notifications.
  // Example: If previous_remaining=18 and current=4, returns {15, 5}.
  std::vector<int> GetCrossedWarningThresholds(
      uint8_t activity_id,
      uint16_t previous_remaining) const;

  // Get all warning threshold values (in minutes).
  static std::vector<int> GetWarningThresholds();

 private:
  // ============================================================================
  // Decision Helpers
  // ============================================================================

  // Check if activity has an active exception (always allowed).
  bool HasActiveException(uint8_t activity_id) const;

  // Check if activity is currently banned.
  bool IsBanned(uint8_t activity_id) const;

  // Check if activity has an active extension (extra time granted).
  bool HasActiveExtension(uint8_t activity_id) const;

  // Check if current time is within allowed time blocks.
  bool IsWithinTimeBlock(uint8_t activity_id, base::Time time) const;

  // Get remaining quota for activity on given date.
  uint16_t GetQuotaRemaining(uint8_t activity_id, base::Time date) const;

  // Get total quota for activity on given date.
  uint16_t GetQuotaTotal(uint8_t activity_id, base::Time date) const;

  // Get used quota for activity on given date.
  uint16_t GetQuotaUsed(uint8_t activity_id, base::Time date) const;

  // ============================================================================
  // Time Block Helpers
  // ============================================================================

  // Get time slot index (0-47) for time block checking.
  // Each slot represents 30 minutes: slot 0 = 00:00-00:30, slot 1 = 00:30-01:00
  static int GetTimeSlot(base::Time time);

  // Get minutes remaining in current time block.
  // Returns 0 if not in a time block.
  uint16_t GetTimeBlockRemaining(uint8_t activity_id, base::Time time) const;

  // ============================================================================
  // Extension Helpers
  // ============================================================================

  // Get extension remaining minutes.
  // Returns 0 if no active extension.
  uint16_t GetExtensionRemaining(uint8_t activity_id) const;

  // Get extension expiry time.
  // Returns base::Time() if no active extension.
  base::Time GetExtensionExpiry(uint8_t activity_id) const;

  // Non-owning pointer to the offline cache.
  // The cache is owned by Allow2Service.
  raw_ptr<Allow2OfflineCache> cache_;

  // Warning thresholds in minutes.
  // Sorted in descending order for threshold crossing detection.
  static constexpr int kWarningThresholds[] = {15, 5, 1};

  // Maximum cache age before considered invalid (24 hours).
  static constexpr int kMaxCacheAgeSeconds = 86400;
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_LOCAL_DECISION_H_

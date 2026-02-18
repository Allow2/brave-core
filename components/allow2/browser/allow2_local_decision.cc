/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_local_decision.h"

#include <algorithm>
#include <cmath>

#include "base/i18n/time_formatting.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "brave/components/allow2/browser/allow2_offline_cache.h"

namespace allow2 {

// ============================================================================
// LocalDecisionResult Implementation
// ============================================================================

LocalDecisionResult::LocalDecisionResult() = default;
LocalDecisionResult::~LocalDecisionResult() = default;
LocalDecisionResult::LocalDecisionResult(const LocalDecisionResult&) = default;
LocalDecisionResult& LocalDecisionResult::operator=(
    const LocalDecisionResult&) = default;
LocalDecisionResult::LocalDecisionResult(LocalDecisionResult&&) = default;
LocalDecisionResult& LocalDecisionResult::operator=(LocalDecisionResult&&) =
    default;

std::string LocalDecisionResult::GetDisplayMessage() const {
  if (allowed) {
    if (has_extension && extension_remaining > 0) {
      return base::StringPrintf(
          "Extension active: %d minute%s remaining",
          extension_remaining,
          extension_remaining == 1 ? "" : "s");
    }
    if (remaining_minutes > 60) {
      int hours = remaining_minutes / 60;
      int mins = remaining_minutes % 60;
      if (mins > 0) {
        return base::StringPrintf(
            "You have %d hour%s and %d minute%s remaining",
            hours, hours == 1 ? "" : "s",
            mins, mins == 1 ? "" : "s");
      }
      return base::StringPrintf(
          "You have %d hour%s remaining",
          hours, hours == 1 ? "" : "s");
    }
    if (remaining_minutes > 0) {
      return base::StringPrintf(
          "You have %d minute%s remaining",
          remaining_minutes,
          remaining_minutes == 1 ? "" : "s");
    }
    // Exception case - no time limit
    if (reason == "exception_allowed") {
      return "This activity is always allowed";
    }
    return "Time available";
  }

  // Not allowed - show appropriate message
  if (reason == "quota_exhausted") {
    return base::StringPrintf(
        "Time's up! You've used all %d minutes for today",
        quota_total);
  }
  if (reason == "outside_time_block") {
    return "This activity is not allowed right now";
  }
  if (reason == "banned") {
    return "This activity is currently blocked";
  }

  return "Activity not allowed";
}

// Static factory methods
LocalDecisionResult LocalDecisionResult::Allowed(uint16_t remaining_minutes,
                                                  uint16_t quota_used,
                                                  uint16_t quota_total) {
  LocalDecisionResult result;
  result.allowed = true;
  result.reason = "allowed";
  result.remaining_minutes = remaining_minutes;
  result.quota_used = quota_used;
  result.quota_total = quota_total;
  return result;
}

LocalDecisionResult LocalDecisionResult::QuotaExhausted(uint16_t quota_used,
                                                         uint16_t quota_total) {
  LocalDecisionResult result;
  result.allowed = false;
  result.reason = "quota_exhausted";
  result.remaining_minutes = 0;
  result.quota_used = quota_used;
  result.quota_total = quota_total;
  return result;
}

LocalDecisionResult LocalDecisionResult::OutsideTimeBlock() {
  LocalDecisionResult result;
  result.allowed = false;
  result.reason = "outside_time_block";
  result.remaining_minutes = 0;
  return result;
}

LocalDecisionResult LocalDecisionResult::Banned(const std::string& ban_reason) {
  LocalDecisionResult result;
  result.allowed = false;
  result.reason = "banned";
  result.remaining_minutes = 0;
  return result;
}

LocalDecisionResult LocalDecisionResult::ExceptionAllowed() {
  LocalDecisionResult result;
  result.allowed = true;
  result.reason = "exception_allowed";
  // No time limit for exceptions
  result.remaining_minutes = UINT16_MAX;
  return result;
}

LocalDecisionResult LocalDecisionResult::ExtensionActive(
    uint16_t extension_remaining,
    uint16_t remaining_minutes) {
  LocalDecisionResult result;
  result.allowed = true;
  result.reason = "extension_active";
  result.remaining_minutes = remaining_minutes;
  result.has_extension = true;
  result.extension_remaining = extension_remaining;
  return result;
}

// ============================================================================
// Allow2LocalDecision Implementation
// ============================================================================

Allow2LocalDecision::Allow2LocalDecision(Allow2OfflineCache* cache)
    : cache_(cache) {
  DCHECK(cache_);
}

Allow2LocalDecision::~Allow2LocalDecision() = default;

LocalDecisionResult Allow2LocalDecision::Check(uint8_t activity_id) const {
  return CheckAt(activity_id, base::Time::Now());
}

LocalDecisionResult Allow2LocalDecision::CheckAt(uint8_t activity_id,
                                                  base::Time time) const {
  // DECISION PRIORITY (same as server):
  // 1. EXCEPTIONS - Always allowed
  // 2. BANS - Blocked until ban expires
  // 3. EXTENSIONS - Allowed during extension window
  // 4. TIME BLOCKS - Blocked outside allowed windows
  // 5. ALLOWANCES - Blocked when quota exhausted

  // 1. Check for exceptions (emergency services, whitelisted)
  if (HasActiveException(activity_id)) {
    VLOG(2) << "Allow2LocalDecision: Activity " << static_cast<int>(activity_id)
            << " allowed by exception";
    return LocalDecisionResult::ExceptionAllowed();
  }

  // 2. Check for bans
  if (IsBanned(activity_id)) {
    VLOG(2) << "Allow2LocalDecision: Activity " << static_cast<int>(activity_id)
            << " is banned";
    return LocalDecisionResult::Banned("Activity banned");
  }

  // 3. Check for extensions
  if (HasActiveExtension(activity_id)) {
    uint16_t extension_remaining = GetExtensionRemaining(activity_id);
    uint16_t effective_remaining = GetEffectiveRemaining(activity_id);
    VLOG(2) << "Allow2LocalDecision: Activity " << static_cast<int>(activity_id)
            << " has extension, " << extension_remaining << " mins remaining";
    return LocalDecisionResult::ExtensionActive(extension_remaining,
                                                 effective_remaining);
  }

  // 4. Check time blocks
  if (!IsWithinTimeBlock(activity_id, time)) {
    VLOG(2) << "Allow2LocalDecision: Activity " << static_cast<int>(activity_id)
            << " blocked - outside time block";
    return LocalDecisionResult::OutsideTimeBlock();
  }

  // 5. Check allowances (quota)
  uint16_t quota_used = GetQuotaUsed(activity_id, time);
  uint16_t quota_total = GetQuotaTotal(activity_id, time);
  uint16_t quota_remaining = GetQuotaRemaining(activity_id, time);

  if (quota_remaining == 0 && quota_total > 0) {
    VLOG(2) << "Allow2LocalDecision: Activity " << static_cast<int>(activity_id)
            << " blocked - quota exhausted (" << quota_used << "/"
            << quota_total << ")";
    return LocalDecisionResult::QuotaExhausted(quota_used, quota_total);
  }

  // Activity is allowed
  VLOG(2) << "Allow2LocalDecision: Activity " << static_cast<int>(activity_id)
          << " allowed, " << quota_remaining << " mins remaining";
  return LocalDecisionResult::Allowed(quota_remaining, quota_used, quota_total);
}

void Allow2LocalDecision::LogUsage(uint8_t activity_id, uint16_t minutes) {
  if (!cache_) {
    LOG(WARNING) << "Allow2LocalDecision: Cannot log usage - no cache";
    return;
  }

  cache_->IncrementUsage(activity_id, minutes);
  VLOG(2) << "Allow2LocalDecision: Logged " << minutes << " minutes for "
          << "activity " << static_cast<int>(activity_id);
}

uint16_t Allow2LocalDecision::GetEffectiveRemaining(
    uint8_t activity_id) const {
  // Start with quota remaining
  uint16_t remaining = GetQuotaRemaining(activity_id, base::Time::Now());

  // If there's an extension, add extension remaining
  if (HasActiveExtension(activity_id)) {
    uint16_t extension_remaining = GetExtensionRemaining(activity_id);
    // Extension effectively increases remaining time
    remaining = std::max(remaining, extension_remaining);
  }

  // Consider time block end
  uint16_t time_block_remaining =
      GetTimeBlockRemaining(activity_id, base::Time::Now());
  if (time_block_remaining > 0 && time_block_remaining < remaining) {
    remaining = time_block_remaining;
  }

  return remaining;
}

void Allow2LocalDecision::ApplyExtension(uint8_t activity_id,
                                          uint16_t minutes,
                                          base::Time expires_at) {
  if (!cache_) {
    LOG(WARNING) << "Allow2LocalDecision: Cannot apply extension - no cache";
    return;
  }

  cache_->SetExtension(activity_id, minutes, expires_at);
  VLOG(1) << "Allow2LocalDecision: Applied extension of " << minutes
          << " minutes for activity " << static_cast<int>(activity_id);
}

bool Allow2LocalDecision::IsOfflineMode() const {
  if (!cache_) {
    return false;
  }
  return cache_->IsValid() && !cache_->IsOnline();
}

int Allow2LocalDecision::GetCacheAgeSeconds() const {
  if (!cache_ || !cache_->IsValid()) {
    return -1;
  }
  return static_cast<int>(cache_->GetAge().InSeconds());
}

bool Allow2LocalDecision::IsCacheValid() const {
  if (!cache_) {
    return false;
  }
  if (!cache_->IsValid()) {
    return false;
  }
  return cache_->GetAge().InSeconds() < kMaxCacheAgeSeconds;
}

std::vector<int> Allow2LocalDecision::GetCrossedWarningThresholds(
    uint8_t activity_id,
    uint16_t previous_remaining) const {
  std::vector<int> crossed;
  uint16_t current_remaining = GetEffectiveRemaining(activity_id);

  // Check each threshold (sorted descending: 15, 5, 1)
  for (int threshold : kWarningThresholds) {
    // A threshold is crossed if we went from above to at-or-below
    if (previous_remaining > threshold && current_remaining <= threshold) {
      crossed.push_back(threshold);
    }
  }

  return crossed;
}

// static
std::vector<int> Allow2LocalDecision::GetWarningThresholds() {
  return std::vector<int>(
      std::begin(kWarningThresholds),
      std::end(kWarningThresholds));
}

// ============================================================================
// Private Helper Methods
// ============================================================================

bool Allow2LocalDecision::HasActiveException(uint8_t activity_id) const {
  if (!cache_) {
    return false;
  }
  return cache_->HasException(activity_id);
}

bool Allow2LocalDecision::IsBanned(uint8_t activity_id) const {
  if (!cache_) {
    return false;
  }
  return cache_->IsBanned(activity_id);
}

bool Allow2LocalDecision::HasActiveExtension(uint8_t activity_id) const {
  if (!cache_) {
    return false;
  }

  base::Time expiry = cache_->GetExtensionExpiry(activity_id);
  if (expiry.is_null()) {
    return false;
  }

  return base::Time::Now() < expiry;
}

bool Allow2LocalDecision::IsWithinTimeBlock(uint8_t activity_id,
                                             base::Time time) const {
  if (!cache_) {
    // Default to allowed if no cache
    return true;
  }

  // Get time slot index (0-47)
  int slot = GetTimeSlot(time);

  // Get allowed slots from cache
  std::vector<bool> allowed_slots = cache_->GetTimeBlocks(activity_id);

  // If no time blocks configured, activity is allowed all day
  if (allowed_slots.empty()) {
    return true;
  }

  // Check if current slot is allowed
  if (slot >= 0 && static_cast<size_t>(slot) < allowed_slots.size()) {
    return allowed_slots[slot];
  }

  // Invalid slot - default to not allowed
  return false;
}

uint16_t Allow2LocalDecision::GetQuotaRemaining(uint8_t activity_id,
                                                 base::Time date) const {
  uint16_t total = GetQuotaTotal(activity_id, date);
  uint16_t used = GetQuotaUsed(activity_id, date);

  if (used >= total) {
    return 0;
  }
  return total - used;
}

uint16_t Allow2LocalDecision::GetQuotaTotal(uint8_t activity_id,
                                             base::Time date) const {
  if (!cache_) {
    return 0;
  }
  return cache_->GetQuotaTotal(activity_id, date);
}

uint16_t Allow2LocalDecision::GetQuotaUsed(uint8_t activity_id,
                                            base::Time date) const {
  if (!cache_) {
    return 0;
  }
  return cache_->GetQuotaUsed(activity_id, date);
}

// static
int Allow2LocalDecision::GetTimeSlot(base::Time time) {
  // Convert to local time
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);

  // Each slot is 30 minutes
  // slot 0 = 00:00-00:30, slot 1 = 00:30-01:00, etc.
  int slot = exploded.hour * 2;
  if (exploded.minute >= 30) {
    slot++;
  }

  // Clamp to valid range (0-47)
  return std::clamp(slot, 0, 47);
}

uint16_t Allow2LocalDecision::GetTimeBlockRemaining(uint8_t activity_id,
                                                     base::Time time) const {
  if (!cache_) {
    return 0;
  }

  // Get current slot
  int current_slot = GetTimeSlot(time);

  // Get allowed slots
  std::vector<bool> allowed_slots = cache_->GetTimeBlocks(activity_id);
  if (allowed_slots.empty()) {
    // No time blocks - no limit
    return UINT16_MAX;
  }

  // Check if current slot is allowed
  if (current_slot < 0 ||
      static_cast<size_t>(current_slot) >= allowed_slots.size() ||
      !allowed_slots[current_slot]) {
    return 0;  // Not in an allowed slot
  }

  // Count consecutive allowed slots from current slot
  int consecutive = 0;
  for (size_t i = current_slot; i < allowed_slots.size(); i++) {
    if (allowed_slots[i]) {
      consecutive++;
    } else {
      break;
    }
  }

  // Each slot is 30 minutes
  uint16_t remaining = consecutive * 30;

  // Adjust for current position within the slot
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  int minutes_into_slot = exploded.minute % 30;
  remaining -= minutes_into_slot;

  return remaining;
}

uint16_t Allow2LocalDecision::GetExtensionRemaining(
    uint8_t activity_id) const {
  if (!cache_) {
    return 0;
  }

  base::Time expiry = cache_->GetExtensionExpiry(activity_id);
  if (expiry.is_null() || expiry <= base::Time::Now()) {
    return 0;
  }

  base::TimeDelta remaining = expiry - base::Time::Now();
  return static_cast<uint16_t>(remaining.InMinutes());
}

base::Time Allow2LocalDecision::GetExtensionExpiry(uint8_t activity_id) const {
  if (!cache_) {
    return base::Time();
  }
  return cache_->GetExtensionExpiry(activity_id);
}

}  // namespace allow2

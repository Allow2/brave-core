/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CACHE_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CACHE_H_

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/values.h"

class PrefService;

namespace allow2 {

// Number of 30-minute time slots in a day (48 = 24 hours * 2 slots per hour).
inline constexpr size_t kTimeSlotsPerDay = 48;

// Time block representing a 30-minute slot.
struct TimeBlock {
  bool allowed = true;
};

// Activity data for a specific day.
// Contains quota, name, and allowed time blocks.
struct CachedActivity {
  CachedActivity();
  ~CachedActivity();
  CachedActivity(const CachedActivity&);
  CachedActivity& operator=(const CachedActivity&);
  CachedActivity(CachedActivity&&);
  CachedActivity& operator=(CachedActivity&&);

  uint8_t id = 0;
  std::string name;
  uint16_t quota_minutes = 0;
  std::array<TimeBlock, kTimeSlotsPerDay> time_blocks;

  // Check if activity is allowed in the current time slot.
  bool IsAllowedInSlot(size_t slot_index) const;

  // Get the slot index for a given time.
  static size_t GetSlotForTime(base::Time time, const std::string& timezone);
};

// Day data containing day type and all activity configurations.
struct CachedDay {
  CachedDay();
  ~CachedDay();
  CachedDay(const CachedDay&);
  CachedDay& operator=(const CachedDay&);
  CachedDay(CachedDay&&);
  CachedDay& operator=(CachedDay&&);

  base::Time date;
  uint8_t day_type_id = 0;
  std::string day_type_name;
  std::map<uint8_t, CachedActivity> activities;

  // Get activity by ID.
  std::optional<CachedActivity> GetActivity(uint8_t activity_id) const;

  // Check if a date matches this day (ignoring time component).
  bool MatchesDate(base::Time check_date) const;
};

// Restriction from the offline cache.
struct CachedRestriction {
  CachedRestriction();
  ~CachedRestriction();
  CachedRestriction(const CachedRestriction&);
  CachedRestriction& operator=(const CachedRestriction&);

  uint64_t id = 0;
  std::string type;  // "url", "app", etc.
  std::string pattern;
  bool blocked = true;
};

// Extension grant stored in the cache.
// Extensions grant additional time for specific activities.
struct CachedExtension {
  CachedExtension();
  ~CachedExtension();
  CachedExtension(const CachedExtension&);
  CachedExtension& operator=(const CachedExtension&);

  uint64_t id = 0;
  uint64_t child_id = 0;
  uint8_t activity_id = 0;
  uint16_t minutes = 0;
  base::Time expires_at;

  // Check if extension is still valid.
  bool IsValid() const;

  // Check if extension has expired.
  bool IsExpired() const;
};

// Manages offline caching for Allow2 parental control data.
//
// This cache stores day schedules, activity quotas, time blocks, and
// extensions received from the Allow2 server. It enables the browser to
// continue enforcing parental controls even when offline.
//
// DESIGN NOTES:
// - Cache is stored as JSON in preferences for persistence across sessions.
// - Each day's data includes activity quotas and 48 time slots (30 min each).
// - Local usage is tracked separately to detect quota deficits when offline.
// - Extensions from voice codes or QR codes can be applied locally.
// - Cache validity is server-controlled via validUntil timestamp.
//
// OFFLINE BEHAVIOR:
// - When offline, the cache is used to allow/block activities.
// - Local usage is accumulated and synced when connectivity returns.
// - If cache expires while offline, browser enters restrictive mode.
class Allow2OfflineCache {
 public:
  explicit Allow2OfflineCache(PrefService* local_state);
  ~Allow2OfflineCache();

  Allow2OfflineCache(const Allow2OfflineCache&) = delete;
  Allow2OfflineCache& operator=(const Allow2OfflineCache&) = delete;

  // ============================================================================
  // Cache Update
  // ============================================================================

  // Update cache from server response JSON.
  // Expected format:
  // {
  //   "offlineCache": {
  //     "generatedAt": "ISO8601",
  //     "validUntil": "ISO8601",
  //     "childId": 1001,
  //     "timezone": "Australia/Sydney",
  //     "days": [...],
  //     "restrictions": [...],
  //     "extensions": [...]
  //   }
  // }
  // Returns true if parsing and storage succeeded.
  bool UpdateFromResponse(const std::string& json);

  // Update cache from parsed Value (for integration with API client).
  bool UpdateFromValue(const base::Value::Dict& offline_cache_dict);

  // Clear all cached data.
  void Clear();

  // ============================================================================
  // Cache Retrieval
  // ============================================================================

  // Get cached data for a specific date.
  // Returns std::nullopt if no data for that date.
  std::optional<CachedDay> GetDayData(base::Time date) const;

  // Get activity data for a specific date and activity ID.
  std::optional<CachedActivity> GetActivity(base::Time date,
                                             uint8_t activity_id) const;

  // Get all active extensions for a child.
  std::vector<CachedExtension> GetActiveExtensions(uint64_t child_id) const;

  // Get all cached restrictions.
  std::vector<CachedRestriction> GetRestrictions() const;

  // ============================================================================
  // Cache Validity
  // ============================================================================

  // Check if the cache contains valid data.
  bool IsValid() const;

  // Check if the cache has expired (past validUntil).
  bool IsExpired() const;

  // Check if cache has data for the current child.
  bool HasDataForChild(uint64_t child_id) const;

  // Get cache validity timestamps.
  base::Time GetValidUntil() const;
  base::Time GetGeneratedAt() const;

  // ============================================================================
  // Child Management
  // ============================================================================

  // Set the current child ID for cache operations.
  void SetCurrentChildId(uint64_t child_id);

  // Get the current child ID.
  uint64_t GetCurrentChildId() const;

  // Get the child ID the cache was generated for.
  uint64_t GetCachedChildId() const;

  // ============================================================================
  // Timezone
  // ============================================================================

  // Get the timezone the cache was generated for.
  std::string GetCachedTimezone() const;

  // ============================================================================
  // Local Usage Tracking
  // ============================================================================

  // Record local usage for offline deficit detection.
  // Usage is keyed by date and activity ID.
  void RecordUsage(uint8_t activity_id, uint16_t minutes);

  // Get accumulated local usage for a date and activity.
  uint16_t GetLocalUsage(base::Time date, uint8_t activity_id) const;

  // Get all local usage records (for syncing when online).
  std::map<std::string, uint16_t> GetAllLocalUsage() const;

  // Clear local usage after successful sync.
  void ClearLocalUsage();

  // ============================================================================
  // Local Extensions
  // ============================================================================

  // Add a local extension (from voice code or QR scan).
  void AddLocalExtension(const CachedExtension& extension);

  // Get local extensions pending server sync.
  std::vector<CachedExtension> GetLocalExtensions() const;

  // Clear local extensions after successful sync.
  void ClearLocalExtensions();

 private:
  // Load cache from preferences.
  void LoadFromPrefs();

  // Save cache to preferences.
  void SaveToPrefs();

  // Parse a day from JSON dict.
  bool ParseDay(const base::Value::Dict& day_dict, CachedDay* out_day);

  // Parse an activity from JSON dict.
  bool ParseActivity(const base::Value::Dict& activity_dict,
                     uint8_t activity_id,
                     CachedActivity* out_activity);

  // Parse an extension from JSON dict.
  bool ParseExtension(const base::Value::Dict& extension_dict,
                      CachedExtension* out_extension);

  // Parse a restriction from JSON dict.
  bool ParseRestriction(const base::Value::Dict& restriction_dict,
                        CachedRestriction* out_restriction);

  // Generate a usage key for the local usage map.
  static std::string MakeUsageKey(base::Time date, uint8_t activity_id);

  // Parse a usage key back to components.
  static bool ParseUsageKey(const std::string& key,
                            base::Time* out_date,
                            uint8_t* out_activity_id);

  // Serialize extension to dict.
  base::Value::Dict ExtensionToDict(const CachedExtension& extension) const;

  // Profile preferences for cache storage.
  raw_ptr<PrefService> local_state_;

  // Current child ID for operations.
  uint64_t current_child_id_ = 0;

  // Cached child ID (from server response).
  uint64_t cached_child_id_ = 0;

  // Cache generation and validity timestamps.
  base::Time generated_at_;
  base::Time valid_until_;

  // Cached timezone.
  std::string timezone_;

  // Cached days data.
  std::vector<CachedDay> days_;

  // Cached restrictions.
  std::vector<CachedRestriction> restrictions_;

  // Cached extensions from server.
  std::vector<CachedExtension> extensions_;

  // Local extensions pending sync.
  std::vector<CachedExtension> local_extensions_;

  // Local usage tracking: "YYYY-MM-DD-activityId" -> minutes
  std::map<std::string, uint16_t> local_usage_;
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CACHE_H_

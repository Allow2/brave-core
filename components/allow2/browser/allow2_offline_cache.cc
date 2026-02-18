/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_offline_cache.h"

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace allow2 {

namespace {

// JSON keys.
constexpr char kGeneratedAtKey[] = "generatedAt";
constexpr char kValidUntilKey[] = "validUntil";
constexpr char kChildIdKey[] = "childId";
constexpr char kTimezoneKey[] = "timezone";
constexpr char kDaysKey[] = "days";
constexpr char kRestrictionsKey[] = "restrictions";
constexpr char kExtensionsKey[] = "extensions";
constexpr char kDateKey[] = "date";
constexpr char kDayTypeKey[] = "dayType";
constexpr char kIdKey[] = "id";
constexpr char kNameKey[] = "name";
constexpr char kActivitiesKey[] = "activities";
constexpr char kQuotaKey[] = "quota";
constexpr char kTimeBlocksKey[] = "timeBlocks";
constexpr char kActivityIdKey[] = "activityId";
constexpr char kMinutesKey[] = "minutes";
constexpr char kExpiresAtKey[] = "expiresAt";
constexpr char kTypeKey[] = "type";
constexpr char kPatternKey[] = "pattern";
constexpr char kBlockedKey[] = "blocked";

// Parse ISO8601 timestamp string to base::Time.
bool ParseTimestamp(const std::string& timestamp, base::Time* out_time) {
  if (!out_time) {
    return false;
  }
  return base::Time::FromUTCString(timestamp.c_str(), out_time);
}

// Format base::Time as ISO8601 string.
std::string FormatTimestamp(base::Time time) {
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);
  return base::StringPrintf("%04d-%02d-%02dT%02d:%02d:%02dZ",
                            exploded.year, exploded.month, exploded.day_of_month,
                            exploded.hour, exploded.minute, exploded.second);
}

// Format date only (YYYY-MM-DD).
std::string FormatDate(base::Time time) {
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);
  return base::StringPrintf("%04d-%02d-%02d",
                            exploded.year, exploded.month, exploded.day_of_month);
}

// Parse date string (YYYY-MM-DD) to base::Time at midnight UTC.
bool ParseDate(const std::string& date_str, base::Time* out_time) {
  if (!out_time || date_str.length() != 10) {
    return false;
  }
  std::string timestamp = date_str + "T00:00:00Z";
  return base::Time::FromUTCString(timestamp.c_str(), out_time);
}

}  // namespace

// ============================================================================
// Struct Implementations
// ============================================================================

CachedActivity::CachedActivity() {
  // Initialize all time blocks as allowed by default.
  for (auto& block : time_blocks) {
    block.allowed = true;
  }
}

CachedActivity::~CachedActivity() = default;
CachedActivity::CachedActivity(const CachedActivity&) = default;
CachedActivity& CachedActivity::operator=(const CachedActivity&) = default;
CachedActivity::CachedActivity(CachedActivity&&) = default;
CachedActivity& CachedActivity::operator=(CachedActivity&&) = default;

bool CachedActivity::IsAllowedInSlot(size_t slot_index) const {
  if (slot_index >= kTimeSlotsPerDay) {
    return false;
  }
  return time_blocks[slot_index].allowed;
}

// static
size_t CachedActivity::GetSlotForTime(base::Time time,
                                       const std::string& timezone) {
  // For simplicity, use UTC. A full implementation would use ICU
  // for timezone conversion.
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);

  // Each slot is 30 minutes.
  size_t slot = (exploded.hour * 2) + (exploded.minute / 30);
  return std::min(slot, kTimeSlotsPerDay - 1);
}

CachedDay::CachedDay() = default;
CachedDay::~CachedDay() = default;
CachedDay::CachedDay(const CachedDay&) = default;
CachedDay& CachedDay::operator=(const CachedDay&) = default;
CachedDay::CachedDay(CachedDay&&) = default;
CachedDay& CachedDay::operator=(CachedDay&&) = default;

std::optional<CachedActivity> CachedDay::GetActivity(uint8_t activity_id) const {
  auto it = activities.find(activity_id);
  if (it != activities.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool CachedDay::MatchesDate(base::Time check_date) const {
  base::Time::Exploded day_exploded;
  date.UTCExplode(&day_exploded);

  base::Time::Exploded check_exploded;
  check_date.UTCExplode(&check_exploded);

  return day_exploded.year == check_exploded.year &&
         day_exploded.month == check_exploded.month &&
         day_exploded.day_of_month == check_exploded.day_of_month;
}

CachedRestriction::CachedRestriction() = default;
CachedRestriction::~CachedRestriction() = default;
CachedRestriction::CachedRestriction(const CachedRestriction&) = default;
CachedRestriction& CachedRestriction::operator=(const CachedRestriction&) = default;

CachedExtension::CachedExtension() = default;
CachedExtension::~CachedExtension() = default;
CachedExtension::CachedExtension(const CachedExtension&) = default;
CachedExtension& CachedExtension::operator=(const CachedExtension&) = default;

bool CachedExtension::IsValid() const {
  return id != 0 && minutes > 0 && !IsExpired();
}

bool CachedExtension::IsExpired() const {
  return base::Time::Now() > expires_at;
}

// ============================================================================
// Allow2OfflineCache Implementation
// ============================================================================

Allow2OfflineCache::Allow2OfflineCache(PrefService* local_state)
    : local_state_(local_state) {
  DCHECK(local_state_);
  LoadFromPrefs();
}

Allow2OfflineCache::~Allow2OfflineCache() = default;

bool Allow2OfflineCache::UpdateFromResponse(const std::string& json) {
  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    LOG(ERROR) << "Allow2: Failed to parse offline cache JSON";
    return false;
  }

  const base::Value::Dict& root = parsed->GetDict();

  // Check if we have an offlineCache wrapper.
  const base::Value::Dict* cache_dict = root.FindDict("offlineCache");
  if (!cache_dict) {
    // Try parsing as direct offline cache data.
    return UpdateFromValue(root);
  }

  return UpdateFromValue(*cache_dict);
}

bool Allow2OfflineCache::UpdateFromValue(const base::Value::Dict& dict) {
  // Parse metadata.
  const std::string* generated_at_str = dict.FindString(kGeneratedAtKey);
  const std::string* valid_until_str = dict.FindString(kValidUntilKey);

  if (!generated_at_str || !valid_until_str) {
    LOG(ERROR) << "Allow2: Offline cache missing timestamps";
    return false;
  }

  base::Time new_generated_at;
  base::Time new_valid_until;

  if (!ParseTimestamp(*generated_at_str, &new_generated_at) ||
      !ParseTimestamp(*valid_until_str, &new_valid_until)) {
    LOG(ERROR) << "Allow2: Failed to parse offline cache timestamps";
    return false;
  }

  // Parse child ID.
  std::optional<int> child_id_opt = dict.FindInt(kChildIdKey);
  if (!child_id_opt.has_value()) {
    LOG(ERROR) << "Allow2: Offline cache missing childId";
    return false;
  }
  uint64_t new_child_id = static_cast<uint64_t>(*child_id_opt);

  // Parse timezone.
  const std::string* timezone_str = dict.FindString(kTimezoneKey);
  std::string new_timezone = timezone_str ? *timezone_str : "UTC";

  // Parse days.
  std::vector<CachedDay> new_days;
  const base::Value::List* days_list = dict.FindList(kDaysKey);
  if (days_list) {
    for (const auto& day_value : *days_list) {
      if (!day_value.is_dict()) {
        continue;
      }
      CachedDay day;
      if (ParseDay(day_value.GetDict(), &day)) {
        new_days.push_back(std::move(day));
      }
    }
  }

  // Parse restrictions.
  std::vector<CachedRestriction> new_restrictions;
  const base::Value::List* restrictions_list = dict.FindList(kRestrictionsKey);
  if (restrictions_list) {
    for (const auto& restriction_value : *restrictions_list) {
      if (!restriction_value.is_dict()) {
        continue;
      }
      CachedRestriction restriction;
      if (ParseRestriction(restriction_value.GetDict(), &restriction)) {
        new_restrictions.push_back(std::move(restriction));
      }
    }
  }

  // Parse extensions.
  std::vector<CachedExtension> new_extensions;
  const base::Value::List* extensions_list = dict.FindList(kExtensionsKey);
  if (extensions_list) {
    for (const auto& extension_value : *extensions_list) {
      if (!extension_value.is_dict()) {
        continue;
      }
      CachedExtension extension;
      if (ParseExtension(extension_value.GetDict(), &extension)) {
        new_extensions.push_back(std::move(extension));
      }
    }
  }

  // All parsing succeeded - update state.
  generated_at_ = new_generated_at;
  valid_until_ = new_valid_until;
  cached_child_id_ = new_child_id;
  timezone_ = new_timezone;
  days_ = std::move(new_days);
  restrictions_ = std::move(new_restrictions);
  extensions_ = std::move(new_extensions);

  SaveToPrefs();

  VLOG(1) << "Allow2: Offline cache updated. Valid until: "
          << FormatTimestamp(valid_until_) << ", Days: " << days_.size()
          << ", Extensions: " << extensions_.size();

  return true;
}

void Allow2OfflineCache::Clear() {
  generated_at_ = base::Time();
  valid_until_ = base::Time();
  cached_child_id_ = 0;
  timezone_.clear();
  days_.clear();
  restrictions_.clear();
  extensions_.clear();
  local_extensions_.clear();
  local_usage_.clear();

  local_state_->ClearPref(prefs::kAllow2OfflineCache);
  local_state_->ClearPref(prefs::kAllow2LocalUsage);
  local_state_->ClearPref(prefs::kAllow2LocalExtensions);
}

std::optional<CachedDay> Allow2OfflineCache::GetDayData(base::Time date) const {
  for (const auto& day : days_) {
    if (day.MatchesDate(date)) {
      return day;
    }
  }
  return std::nullopt;
}

std::optional<CachedActivity> Allow2OfflineCache::GetActivity(
    base::Time date,
    uint8_t activity_id) const {
  auto day = GetDayData(date);
  if (!day.has_value()) {
    return std::nullopt;
  }
  return day->GetActivity(activity_id);
}

std::vector<CachedExtension> Allow2OfflineCache::GetActiveExtensions(
    uint64_t child_id) const {
  std::vector<CachedExtension> active;

  // Add server extensions.
  for (const auto& ext : extensions_) {
    if (ext.child_id == child_id && ext.IsValid()) {
      active.push_back(ext);
    }
  }

  // Add local extensions.
  for (const auto& ext : local_extensions_) {
    if (ext.child_id == child_id && ext.IsValid()) {
      active.push_back(ext);
    }
  }

  return active;
}

std::vector<CachedRestriction> Allow2OfflineCache::GetRestrictions() const {
  return restrictions_;
}

bool Allow2OfflineCache::IsValid() const {
  return !generated_at_.is_null() && !valid_until_.is_null() &&
         cached_child_id_ != 0 && !days_.empty();
}

bool Allow2OfflineCache::IsExpired() const {
  if (!IsValid()) {
    return true;
  }
  return base::Time::Now() > valid_until_;
}

bool Allow2OfflineCache::HasDataForChild(uint64_t child_id) const {
  return IsValid() && cached_child_id_ == child_id;
}

base::Time Allow2OfflineCache::GetValidUntil() const {
  return valid_until_;
}

base::Time Allow2OfflineCache::GetGeneratedAt() const {
  return generated_at_;
}

void Allow2OfflineCache::SetCurrentChildId(uint64_t child_id) {
  current_child_id_ = child_id;
}

uint64_t Allow2OfflineCache::GetCurrentChildId() const {
  return current_child_id_;
}

uint64_t Allow2OfflineCache::GetCachedChildId() const {
  return cached_child_id_;
}

std::string Allow2OfflineCache::GetCachedTimezone() const {
  return timezone_;
}

void Allow2OfflineCache::RecordUsage(uint8_t activity_id, uint16_t minutes) {
  base::Time now = base::Time::Now();
  std::string key = MakeUsageKey(now, activity_id);

  auto it = local_usage_.find(key);
  if (it != local_usage_.end()) {
    it->second += minutes;
  } else {
    local_usage_[key] = minutes;
  }

  // Persist to prefs.
  base::Value::Dict usage_dict;
  for (const auto& pair : local_usage_) {
    usage_dict.Set(pair.first, static_cast<int>(pair.second));
  }

  std::string json;
  base::JSONWriter::Write(usage_dict, &json);
  local_state_->SetString(prefs::kAllow2LocalUsage, json);
}

uint16_t Allow2OfflineCache::GetLocalUsage(base::Time date,
                                            uint8_t activity_id) const {
  std::string key = MakeUsageKey(date, activity_id);
  auto it = local_usage_.find(key);
  if (it != local_usage_.end()) {
    return it->second;
  }
  return 0;
}

std::map<std::string, uint16_t> Allow2OfflineCache::GetAllLocalUsage() const {
  return local_usage_;
}

void Allow2OfflineCache::ClearLocalUsage() {
  local_usage_.clear();
  local_state_->ClearPref(prefs::kAllow2LocalUsage);
}

void Allow2OfflineCache::AddLocalExtension(const CachedExtension& extension) {
  local_extensions_.push_back(extension);

  // Persist to prefs.
  base::Value::List extensions_list;
  for (const auto& ext : local_extensions_) {
    extensions_list.Append(ExtensionToDict(ext));
  }

  std::string json;
  base::JSONWriter::Write(extensions_list, &json);
  local_state_->SetString(prefs::kAllow2LocalExtensions, json);

  VLOG(1) << "Allow2: Local extension added. Activity: "
          << static_cast<int>(extension.activity_id)
          << ", Minutes: " << extension.minutes;
}

std::vector<CachedExtension> Allow2OfflineCache::GetLocalExtensions() const {
  return local_extensions_;
}

void Allow2OfflineCache::ClearLocalExtensions() {
  local_extensions_.clear();
  local_state_->ClearPref(prefs::kAllow2LocalExtensions);
}

void Allow2OfflineCache::LoadFromPrefs() {
  // Load main cache.
  std::string cache_json = local_state_->GetString(prefs::kAllow2OfflineCache);
  if (!cache_json.empty()) {
    auto parsed = base::JSONReader::Read(cache_json,
                                          base::JSON_ALLOW_TRAILING_COMMAS);
    if (parsed && parsed->is_dict()) {
      UpdateFromValue(parsed->GetDict());
    }
  }

  // Load local usage.
  std::string usage_json = local_state_->GetString(prefs::kAllow2LocalUsage);
  if (!usage_json.empty()) {
    auto parsed = base::JSONReader::Read(usage_json,
                                          base::JSON_ALLOW_TRAILING_COMMAS);
    if (parsed && parsed->is_dict()) {
      for (const auto pair : parsed->GetDict()) {
        if (pair.second.is_int()) {
          local_usage_[pair.first] = static_cast<uint16_t>(pair.second.GetInt());
        }
      }
    }
  }

  // Load local extensions.
  std::string extensions_json = local_state_->GetString(prefs::kAllow2LocalExtensions);
  if (!extensions_json.empty()) {
    auto parsed = base::JSONReader::Read(extensions_json,
                                          base::JSON_ALLOW_TRAILING_COMMAS);
    if (parsed && parsed->is_list()) {
      for (const auto& ext_value : parsed->GetList()) {
        if (!ext_value.is_dict()) {
          continue;
        }
        CachedExtension extension;
        if (ParseExtension(ext_value.GetDict(), &extension)) {
          local_extensions_.push_back(std::move(extension));
        }
      }
    }
  }
}

void Allow2OfflineCache::SaveToPrefs() {
  base::Value::Dict cache_dict;

  // Metadata.
  cache_dict.Set(kGeneratedAtKey, FormatTimestamp(generated_at_));
  cache_dict.Set(kValidUntilKey, FormatTimestamp(valid_until_));
  cache_dict.Set(kChildIdKey, static_cast<int>(cached_child_id_));
  cache_dict.Set(kTimezoneKey, timezone_);

  // Days.
  base::Value::List days_list;
  for (const auto& day : days_) {
    base::Value::Dict day_dict;
    day_dict.Set(kDateKey, FormatDate(day.date));

    base::Value::Dict day_type_dict;
    day_type_dict.Set(kIdKey, static_cast<int>(day.day_type_id));
    day_type_dict.Set(kNameKey, day.day_type_name);
    day_dict.Set(kDayTypeKey, std::move(day_type_dict));

    base::Value::Dict activities_dict;
    for (const auto& activity_pair : day.activities) {
      const CachedActivity& activity = activity_pair.second;
      base::Value::Dict activity_dict;
      activity_dict.Set(kNameKey, activity.name);
      activity_dict.Set(kQuotaKey, static_cast<int>(activity.quota_minutes));

      base::Value::List time_blocks_list;
      for (const auto& block : activity.time_blocks) {
        time_blocks_list.Append(block.allowed ? 1 : 0);
      }
      activity_dict.Set(kTimeBlocksKey, std::move(time_blocks_list));

      activities_dict.Set(base::NumberToString(activity_pair.first),
                          std::move(activity_dict));
    }
    day_dict.Set(kActivitiesKey, std::move(activities_dict));

    days_list.Append(std::move(day_dict));
  }
  cache_dict.Set(kDaysKey, std::move(days_list));

  // Restrictions.
  base::Value::List restrictions_list;
  for (const auto& restriction : restrictions_) {
    base::Value::Dict restriction_dict;
    restriction_dict.Set(kIdKey, static_cast<int>(restriction.id));
    restriction_dict.Set(kTypeKey, restriction.type);
    restriction_dict.Set(kPatternKey, restriction.pattern);
    restriction_dict.Set(kBlockedKey, restriction.blocked);
    restrictions_list.Append(std::move(restriction_dict));
  }
  cache_dict.Set(kRestrictionsKey, std::move(restrictions_list));

  // Extensions.
  base::Value::List extensions_list;
  for (const auto& ext : extensions_) {
    extensions_list.Append(ExtensionToDict(ext));
  }
  cache_dict.Set(kExtensionsKey, std::move(extensions_list));

  std::string json;
  base::JSONWriter::Write(cache_dict, &json);
  local_state_->SetString(prefs::kAllow2OfflineCache, json);
}

bool Allow2OfflineCache::ParseDay(const base::Value::Dict& day_dict,
                                   CachedDay* out_day) {
  if (!out_day) {
    return false;
  }

  // Parse date.
  const std::string* date_str = day_dict.FindString(kDateKey);
  if (!date_str || !ParseDate(*date_str, &out_day->date)) {
    return false;
  }

  // Parse day type.
  const base::Value::Dict* day_type_dict = day_dict.FindDict(kDayTypeKey);
  if (day_type_dict) {
    out_day->day_type_id = static_cast<uint8_t>(
        day_type_dict->FindInt(kIdKey).value_or(0));
    const std::string* day_type_name = day_type_dict->FindString(kNameKey);
    if (day_type_name) {
      out_day->day_type_name = *day_type_name;
    }
  }

  // Parse activities.
  const base::Value::Dict* activities_dict = day_dict.FindDict(kActivitiesKey);
  if (activities_dict) {
    for (const auto pair : *activities_dict) {
      if (!pair.second.is_dict()) {
        continue;
      }

      uint8_t activity_id = 0;
      if (!base::StringToUint(pair.first, reinterpret_cast<unsigned*>(&activity_id))) {
        continue;
      }

      CachedActivity activity;
      if (ParseActivity(pair.second.GetDict(), activity_id, &activity)) {
        out_day->activities[activity_id] = std::move(activity);
      }
    }
  }

  return true;
}

bool Allow2OfflineCache::ParseActivity(const base::Value::Dict& activity_dict,
                                         uint8_t activity_id,
                                         CachedActivity* out_activity) {
  if (!out_activity) {
    return false;
  }

  out_activity->id = activity_id;

  const std::string* name = activity_dict.FindString(kNameKey);
  if (name) {
    out_activity->name = *name;
  }

  out_activity->quota_minutes = static_cast<uint16_t>(
      activity_dict.FindInt(kQuotaKey).value_or(0));

  // Parse time blocks.
  const base::Value::List* time_blocks_list =
      activity_dict.FindList(kTimeBlocksKey);
  if (time_blocks_list) {
    size_t index = 0;
    for (const auto& block_value : *time_blocks_list) {
      if (index >= kTimeSlotsPerDay) {
        break;
      }
      if (block_value.is_int()) {
        out_activity->time_blocks[index].allowed = block_value.GetInt() != 0;
      } else if (block_value.is_bool()) {
        out_activity->time_blocks[index].allowed = block_value.GetBool();
      }
      ++index;
    }
  }

  return true;
}

bool Allow2OfflineCache::ParseExtension(const base::Value::Dict& extension_dict,
                                          CachedExtension* out_extension) {
  if (!out_extension) {
    return false;
  }

  out_extension->id = static_cast<uint64_t>(
      extension_dict.FindInt(kIdKey).value_or(0));
  out_extension->child_id = static_cast<uint64_t>(
      extension_dict.FindInt(kChildIdKey).value_or(0));
  out_extension->activity_id = static_cast<uint8_t>(
      extension_dict.FindInt(kActivityIdKey).value_or(0));
  out_extension->minutes = static_cast<uint16_t>(
      extension_dict.FindInt(kMinutesKey).value_or(0));

  const std::string* expires_at_str = extension_dict.FindString(kExpiresAtKey);
  if (expires_at_str) {
    ParseTimestamp(*expires_at_str, &out_extension->expires_at);
  }

  return out_extension->id != 0 || out_extension->minutes > 0;
}

bool Allow2OfflineCache::ParseRestriction(
    const base::Value::Dict& restriction_dict,
    CachedRestriction* out_restriction) {
  if (!out_restriction) {
    return false;
  }

  out_restriction->id = static_cast<uint64_t>(
      restriction_dict.FindInt(kIdKey).value_or(0));

  const std::string* type = restriction_dict.FindString(kTypeKey);
  if (type) {
    out_restriction->type = *type;
  }

  const std::string* pattern = restriction_dict.FindString(kPatternKey);
  if (pattern) {
    out_restriction->pattern = *pattern;
  }

  out_restriction->blocked = restriction_dict.FindBool(kBlockedKey).value_or(true);

  return !out_restriction->pattern.empty();
}

// static
std::string Allow2OfflineCache::MakeUsageKey(base::Time date,
                                              uint8_t activity_id) {
  return FormatDate(date) + "-" + base::NumberToString(activity_id);
}

// static
bool Allow2OfflineCache::ParseUsageKey(const std::string& key,
                                        base::Time* out_date,
                                        uint8_t* out_activity_id) {
  if (!out_date || !out_activity_id) {
    return false;
  }

  // Key format: "YYYY-MM-DD-activityId"
  if (key.length() < 12) {
    return false;
  }

  std::string date_str = key.substr(0, 10);
  std::string activity_str = key.substr(11);

  if (!ParseDate(date_str, out_date)) {
    return false;
  }

  unsigned activity_id = 0;
  if (!base::StringToUint(activity_str, &activity_id)) {
    return false;
  }

  *out_activity_id = static_cast<uint8_t>(activity_id);
  return true;
}

base::Value::Dict Allow2OfflineCache::ExtensionToDict(
    const CachedExtension& extension) const {
  base::Value::Dict dict;
  dict.Set(kIdKey, static_cast<int>(extension.id));
  dict.Set(kChildIdKey, static_cast<int>(extension.child_id));
  dict.Set(kActivityIdKey, static_cast<int>(extension.activity_id));
  dict.Set(kMinutesKey, static_cast<int>(extension.minutes));
  dict.Set(kExpiresAtKey, FormatTimestamp(extension.expires_at));
  return dict;
}

}  // namespace allow2

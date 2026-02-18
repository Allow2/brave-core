/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_travel_mode.h"

#include "base/i18n/time_formatting.h"
#include "base/time/time.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace allow2 {

Allow2TravelMode::Allow2TravelMode(PrefService* prefs) : prefs_(prefs) {
  DCHECK(prefs_);
}

Allow2TravelMode::~Allow2TravelMode() = default;

void Allow2TravelMode::SetHomeTimezone(const std::string& tz_identifier) {
  prefs_->SetString(prefs::kAllow2HomeTimezone, tz_identifier);
}

std::string Allow2TravelMode::GetHomeTimezone() const {
  return prefs_->GetString(prefs::kAllow2HomeTimezone);
}

std::string Allow2TravelMode::GetDeviceTimezone() const {
  // Get system timezone - this is a simplified implementation.
  // In production, would use icu::TimeZone or platform APIs.
  // For now, return a placeholder that indicates local.
  return "local";
}

bool Allow2TravelMode::IsInTravelMode() const {
  std::string home = GetHomeTimezone();
  if (home.empty()) {
    return false;
  }

  std::string device = GetDeviceTimezone();
  return home != device && device != "local";
}

int Allow2TravelMode::GetTimezoneOffsetDelta() const {
  // Simplified implementation - would need ICU for proper timezone handling.
  // Return 0 for now (no adjustment).
  return 0;
}

base::Time Allow2TravelMode::HomeToDeviceTime(base::Time home_time) const {
  int delta = GetTimezoneOffsetDelta();
  return home_time + base::Seconds(delta);
}

base::Time Allow2TravelMode::DeviceToHomeTime(base::Time device_time) const {
  int delta = GetTimezoneOffsetDelta();
  return device_time - base::Seconds(delta);
}

std::string Allow2TravelMode::GetEffectiveDayType() const {
  // Return cached day type - actual calculation would need
  // calendar data from the Allow2 account.
  return prefs_->GetString(prefs::kAllow2DayTypeToday);
}

int Allow2TravelMode::GetAdjustedRemainingSeconds(
    int raw_remaining_seconds) const {
  // For now, no timezone adjustment to remaining seconds.
  // The day rollover logic would be more complex in production.
  return raw_remaining_seconds;
}

}  // namespace allow2

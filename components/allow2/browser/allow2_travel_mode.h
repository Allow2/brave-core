/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_TRAVEL_MODE_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_TRAVEL_MODE_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"

class PrefService;

namespace allow2 {

// Handles timezone awareness for offline decisions.
// When device travels to different timezone, day boundaries and schedules
// need to be adjusted relative to the home timezone.
class Allow2TravelMode {
 public:
  explicit Allow2TravelMode(PrefService* prefs);
  ~Allow2TravelMode();

  Allow2TravelMode(const Allow2TravelMode&) = delete;
  Allow2TravelMode& operator=(const Allow2TravelMode&) = delete;

  // Set the home timezone (from parent's Allow2 account).
  void SetHomeTimezone(const std::string& tz_identifier);

  // Get the home timezone.
  std::string GetHomeTimezone() const;

  // Get the current device timezone.
  std::string GetDeviceTimezone() const;

  // Check if device is currently in travel mode (different timezone).
  bool IsInTravelMode() const;

  // Get timezone offset difference in seconds.
  // Positive = device is ahead of home, negative = behind.
  int GetTimezoneOffsetDelta() const;

  // Convert a home-timezone time to device-local time.
  base::Time HomeToDeviceTime(base::Time home_time) const;

  // Convert device-local time to home-timezone time.
  base::Time DeviceToHomeTime(base::Time device_time) const;

  // Get the current day type considering travel mode.
  // Uses home timezone to determine the actual day boundary.
  std::string GetEffectiveDayType() const;

  // Get remaining seconds adjusted for timezone.
  // Handles day rollover correctly when traveling.
  int GetAdjustedRemainingSeconds(int raw_remaining_seconds) const;

 private:
  raw_ptr<PrefService> prefs_;
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_TRAVEL_MODE_H_

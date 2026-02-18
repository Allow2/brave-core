/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_DEFICIT_TRACKER_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_DEFICIT_TRACKER_H_

#include <cstdint>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"

class PrefService;

namespace allow2 {

// Tracks deficit time (borrowed time that needs to be repaid).
// When a child exceeds their time limit via a local decision (voice code),
// the excess time is recorded as a deficit that reduces future allowances.
class Allow2DeficitTracker {
 public:
  explicit Allow2DeficitTracker(PrefService* prefs);
  ~Allow2DeficitTracker();

  Allow2DeficitTracker(const Allow2DeficitTracker&) = delete;
  Allow2DeficitTracker& operator=(const Allow2DeficitTracker&) = delete;

  // Get current deficit in seconds for a child.
  int GetDeficitSeconds(uint64_t child_id) const;

  // Add deficit (when borrowing time via voice code).
  void AddDeficit(uint64_t child_id, int seconds);

  // Clear deficit (when synced with server or parent forgives).
  void ClearDeficit(uint64_t child_id);

  // Reduce available time by deficit amount.
  // Returns adjusted remaining seconds after applying deficit.
  int ApplyDeficit(uint64_t child_id, int remaining_seconds) const;

  // Get total deficit across all activities.
  int GetTotalDeficit(uint64_t child_id) const;

  // Check if deficit exceeds maximum allowed borrowing.
  bool IsDeficitExceeded(uint64_t child_id) const;

  // Maximum deficit allowed before blocking borrowing (30 minutes).
  static constexpr int kMaxDeficitSeconds = 30 * 60;

 private:
  raw_ptr<PrefService> prefs_;
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_DEFICIT_TRACKER_H_

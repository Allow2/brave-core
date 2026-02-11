/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_WARNING_CONTROLLER_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_WARNING_CONTROLLER_H_

#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"

namespace allow2 {

// Warning level thresholds.
enum class WarningLevel {
  kNone,           // No warning (plenty of time remaining)
  kGentle,         // 15-10 minutes (auto-dismiss toast)
  kWarning,        // 5-1 minutes (persistent toast with progress)
  kUrgent,         // <1 minute (full-width banner with countdown)
  kBlocked,        // Time's up (full overlay)
};

// Controller for managing time warnings and countdown display.
// Handles the transition between warning levels and provides
// callbacks for UI updates.
class Allow2WarningController {
 public:
  using WarningCallback =
      base::RepeatingCallback<void(WarningLevel level, int remaining_seconds)>;
  using CountdownCallback =
      base::RepeatingCallback<void(int remaining_seconds)>;
  using BlockCallback = base::OnceCallback<void(const std::string& reason)>;

  Allow2WarningController();
  ~Allow2WarningController();

  Allow2WarningController(const Allow2WarningController&) = delete;
  Allow2WarningController& operator=(const Allow2WarningController&) = delete;

  // Set callbacks for warning events.
  void SetWarningCallback(WarningCallback callback);
  void SetCountdownCallback(CountdownCallback callback);
  void SetBlockCallback(BlockCallback callback);

  // Update remaining time from API check result.
  // This triggers warning level transitions and countdown if needed.
  void UpdateRemainingTime(int remaining_seconds);

  // Manually trigger block state with reason.
  void TriggerBlock(const std::string& reason);

  // Get current warning level.
  WarningLevel GetCurrentLevel() const;

  // Get remaining seconds as of last update.
  int GetRemainingSeconds() const;

  // Check if countdown timer is running (for final minute).
  bool IsCountdownActive() const;

  // Stop any active countdown.
  void StopCountdown();

  // Reset warning state (e.g., when child changes or time is granted).
  void Reset();

 private:
  // Calculate warning level for given remaining seconds.
  WarningLevel CalculateLevel(int remaining_seconds) const;

  // Check if level transition should trigger a notification.
  bool ShouldNotifyTransition(WarningLevel from, WarningLevel to) const;

  // Start countdown timer for final minute.
  void StartCountdown();

  // Countdown timer tick.
  void OnCountdownTick();

  WarningCallback warning_callback_;
  CountdownCallback countdown_callback_;
  BlockCallback block_callback_;

  WarningLevel current_level_ = WarningLevel::kNone;
  int remaining_seconds_ = 0;

  base::RepeatingTimer countdown_timer_;

  base::WeakPtrFactory<Allow2WarningController> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_WARNING_CONTROLLER_H_

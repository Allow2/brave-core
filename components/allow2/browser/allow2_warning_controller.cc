/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_warning_controller.h"

#include "base/functional/bind.h"
#include "brave/components/allow2/common/allow2_constants.h"

namespace allow2 {

Allow2WarningController::Allow2WarningController() = default;

Allow2WarningController::~Allow2WarningController() {
  StopCountdown();
}

void Allow2WarningController::SetWarningCallback(WarningCallback callback) {
  warning_callback_ = std::move(callback);
}

void Allow2WarningController::SetCountdownCallback(CountdownCallback callback) {
  countdown_callback_ = std::move(callback);
}

void Allow2WarningController::SetBlockCallback(BlockCallback callback) {
  block_callback_ = std::move(callback);
}

void Allow2WarningController::UpdateRemainingTime(int remaining_seconds) {
  remaining_seconds_ = remaining_seconds;

  WarningLevel new_level = CalculateLevel(remaining_seconds);

  if (ShouldNotifyTransition(current_level_, new_level)) {
    if (warning_callback_) {
      warning_callback_.Run(new_level, remaining_seconds);
    }
  }

  current_level_ = new_level;

  // Start countdown timer for final minute
  if (remaining_seconds <= kWarningThreshold1Min && remaining_seconds > 0) {
    if (!countdown_timer_.IsRunning()) {
      StartCountdown();
    }
  } else {
    StopCountdown();
  }

  // Trigger block if time has run out
  if (remaining_seconds <= 0 && block_callback_) {
    std::move(block_callback_).Run("Time's up");
  }
}

void Allow2WarningController::TriggerBlock(const std::string& reason) {
  current_level_ = WarningLevel::kBlocked;
  StopCountdown();

  if (block_callback_) {
    std::move(block_callback_).Run(reason);
  }
}

WarningLevel Allow2WarningController::GetCurrentLevel() const {
  return current_level_;
}

int Allow2WarningController::GetRemainingSeconds() const {
  return remaining_seconds_;
}

bool Allow2WarningController::IsCountdownActive() const {
  return countdown_timer_.IsRunning();
}

void Allow2WarningController::StopCountdown() {
  countdown_timer_.Stop();
}

void Allow2WarningController::Reset() {
  StopCountdown();
  current_level_ = WarningLevel::kNone;
  remaining_seconds_ = 0;
}

WarningLevel Allow2WarningController::CalculateLevel(
    int remaining_seconds) const {
  if (remaining_seconds <= 0) {
    return WarningLevel::kBlocked;
  }
  if (remaining_seconds <= kWarningThreshold1Min) {
    return WarningLevel::kUrgent;
  }
  if (remaining_seconds <= kWarningThreshold5Min) {
    return WarningLevel::kWarning;
  }
  if (remaining_seconds <= kWarningThreshold15Min) {
    return WarningLevel::kGentle;
  }
  return WarningLevel::kNone;
}

bool Allow2WarningController::ShouldNotifyTransition(WarningLevel from,
                                                      WarningLevel to) const {
  // Always notify when transitioning to a more urgent level
  return to > from;
}

void Allow2WarningController::StartCountdown() {
  countdown_timer_.Start(
      FROM_HERE, base::Seconds(1),
      base::BindRepeating(&Allow2WarningController::OnCountdownTick,
                          weak_ptr_factory_.GetWeakPtr()));
}

void Allow2WarningController::OnCountdownTick() {
  if (remaining_seconds_ > 0) {
    remaining_seconds_--;

    if (countdown_callback_) {
      countdown_callback_.Run(remaining_seconds_);
    }

    // Check for warning thresholds
    if (remaining_seconds_ == kWarningThreshold30Sec ||
        remaining_seconds_ == kWarningThreshold10Sec) {
      if (warning_callback_) {
        warning_callback_.Run(WarningLevel::kUrgent, remaining_seconds_);
      }
    }
  }

  if (remaining_seconds_ <= 0) {
    StopCountdown();
    current_level_ = WarningLevel::kBlocked;
    if (block_callback_) {
      std::move(block_callback_).Run("Time's up");
    }
  }
}

}  // namespace allow2

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_block_overlay.h"

#include <utility>

#include "base/logging.h"

namespace allow2 {

Allow2BlockOverlay::Allow2BlockOverlay() = default;

Allow2BlockOverlay::~Allow2BlockOverlay() = default;

void Allow2BlockOverlay::AddObserver(Allow2BlockOverlayObserver* observer) {
  observers_.AddObserver(observer);
}

void Allow2BlockOverlay::RemoveObserver(Allow2BlockOverlayObserver* observer) {
  observers_.RemoveObserver(observer);
}

void Allow2BlockOverlay::Show(const BlockOverlayConfig& config) {
  config_ = config;
  is_visible_ = true;
  is_request_pending_ = false;

  VLOG(1) << "Allow2: Block overlay shown, reason: "
          << static_cast<int>(config.reason);
}

void Allow2BlockOverlay::Dismiss() {
  if (!is_visible_) {
    return;
  }

  is_visible_ = false;
  is_request_pending_ = false;

  VLOG(1) << "Allow2: Block overlay dismissed";

  for (auto& observer : observers_) {
    observer.OnOverlayDismissed();
  }
}

bool Allow2BlockOverlay::IsVisible() const {
  return is_visible_;
}

const BlockOverlayConfig& Allow2BlockOverlay::GetConfig() const {
  return config_;
}

void Allow2BlockOverlay::SetRequestTimeCallback(RequestTimeCallback callback) {
  request_callback_ = std::move(callback);
}

void Allow2BlockOverlay::HandleRequestMoreTime(int minutes,
                                                const std::string& message) {
  if (!is_visible_ || is_request_pending_) {
    return;
  }

  VLOG(1) << "Allow2: User requesting " << minutes << " more minutes";

  is_request_pending_ = true;

  for (auto& observer : observers_) {
    observer.OnRequestMoreTimeClicked();
  }

  if (request_callback_) {
    std::move(request_callback_).Run(minutes, message);
  }
}

void Allow2BlockOverlay::HandleSwitchUser() {
  if (!is_visible_) {
    return;
  }

  VLOG(1) << "Allow2: User requested switch user";

  for (auto& observer : observers_) {
    observer.OnSwitchUserClicked();
  }
}

void Allow2BlockOverlay::HandleWhyBlocked() {
  if (!is_visible_) {
    return;
  }

  VLOG(1) << "Allow2: User clicked why blocked";

  for (auto& observer : observers_) {
    observer.OnWhyBlockedClicked();
  }
}

bool Allow2BlockOverlay::IsRequestPending() const {
  return is_request_pending_;
}

void Allow2BlockOverlay::SetRequestPending(bool pending) {
  is_request_pending_ = pending;
}

void Allow2BlockOverlay::ShowRequestSent() {
  is_request_pending_ = true;
  VLOG(1) << "Allow2: Request sent, waiting for parent response";
}

void Allow2BlockOverlay::ShowRequestDenied(const std::string& reason) {
  is_request_pending_ = false;
  VLOG(1) << "Allow2: Request denied: " << reason;
}

// static
std::string Allow2BlockOverlay::GetBlockTitle(BlockReason reason) {
  switch (reason) {
    case BlockReason::kTimeLimitReached:
      return "Time's Up!";
    case BlockReason::kTimeBlockActive:
      return "Time Block Active";
    case BlockReason::kActivityBanned:
      return "Activity Blocked";
    case BlockReason::kManualBlock:
      return "Blocked by Parent";
    case BlockReason::kOffline:
      return "Cannot Connect";
  }
  return "Blocked";
}

// static
std::string Allow2BlockOverlay::GetBlockSubtitle(BlockReason reason,
                                                  const std::string& day_type) {
  std::string suffix = day_type.empty() ? "" : " (" + day_type + ")";

  switch (reason) {
    case BlockReason::kTimeLimitReached:
      return "Internet time has ended for today" + suffix;
    case BlockReason::kTimeBlockActive:
      return "Internet is not available during this time" + suffix;
    case BlockReason::kActivityBanned:
      return "This activity is not allowed";
    case BlockReason::kManualBlock:
      return "Your parent has blocked internet access";
    case BlockReason::kOffline:
      return "Cannot verify your internet allowance";
  }
  return "Internet access is currently blocked";
}

// static
std::string Allow2BlockOverlay::GetBlockExplanation(BlockReason reason) {
  switch (reason) {
    case BlockReason::kTimeLimitReached:
      return "Your daily internet time limit has been reached. This helps "
             "ensure a healthy balance between screen time and other "
             "activities. You can request more time from your parent if "
             "needed.";
    case BlockReason::kTimeBlockActive:
      return "Internet access is scheduled to be unavailable during this "
             "time period. This might be for homework time, bedtime, or "
             "family time. The block will automatically end at the "
             "scheduled time.";
    case BlockReason::kActivityBanned:
      return "This type of activity (like gaming or social media) is not "
             "currently allowed. Your parent has set this restriction to "
             "help you focus on other things.";
    case BlockReason::kManualBlock:
      return "Your parent has temporarily blocked internet access. You can "
             "talk to them about when it will be available again.";
    case BlockReason::kOffline:
      return "Unable to connect to the Allow2 service to verify your "
             "internet allowance. Please check your internet connection. "
             "Browsing is paused until we can confirm your time limits.";
  }
  return "Internet access has been restricted.";
}

}  // namespace allow2

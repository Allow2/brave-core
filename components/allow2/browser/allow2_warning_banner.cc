/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_warning_banner.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "brave/components/allow2/common/allow2_constants.h"

namespace allow2 {

// WarningBannerConfig struct out-of-line definitions
WarningBannerConfig::WarningBannerConfig() = default;
WarningBannerConfig::~WarningBannerConfig() = default;
WarningBannerConfig::WarningBannerConfig(const WarningBannerConfig&) = default;
WarningBannerConfig& WarningBannerConfig::operator=(const WarningBannerConfig&) = default;
WarningBannerConfig::WarningBannerConfig(WarningBannerConfig&&) = default;
WarningBannerConfig& WarningBannerConfig::operator=(WarningBannerConfig&&) = default;

Allow2WarningBanner::Allow2WarningBanner() = default;

Allow2WarningBanner::~Allow2WarningBanner() {
  StopCountdown();
  auto_dismiss_timer_.Stop();
}

void Allow2WarningBanner::AddObserver(Allow2WarningBannerObserver* observer) {
  observers_.AddObserver(observer);
}

void Allow2WarningBanner::RemoveObserver(Allow2WarningBannerObserver* observer) {
  observers_.RemoveObserver(observer);
}

void Allow2WarningBanner::Update(const WarningBannerConfig& config) {
  config_ = config;
  countdown_seconds_ = config.remaining_seconds;

  BannerStyle new_style = CalculateStyle(config.level, config.remaining_seconds);

  if (new_style != current_style_) {
    current_style_ = new_style;
    UpdateVisibility();
  }

  // Start countdown for urgent warnings
  if (config.remaining_seconds <= kWarningThreshold1Min &&
      config.remaining_seconds > 0) {
    if (!IsCountdownActive()) {
      StartCountdown();
    }
  }

  // Set up auto-dismiss for gentle warnings
  if (config.level == WarningLevel::kGentle && config.is_dismissible &&
      config.auto_dismiss_seconds > 0) {
    auto_dismiss_timer_.Start(
        FROM_HERE, base::Seconds(config.auto_dismiss_seconds),
        base::BindOnce(&Allow2WarningBanner::OnAutoDismiss,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void Allow2WarningBanner::Show() {
  if (is_visible_) {
    return;
  }

  is_visible_ = true;
  NotifyVisibilityChanged();

  VLOG(1) << "Allow2: Warning banner shown, style: "
          << static_cast<int>(current_style_);
}

void Allow2WarningBanner::Hide() {
  if (!is_visible_) {
    return;
  }

  is_visible_ = false;
  StopCountdown();
  auto_dismiss_timer_.Stop();
  NotifyVisibilityChanged();

  VLOG(1) << "Allow2: Warning banner hidden";
}

void Allow2WarningBanner::Dismiss() {
  if (!is_visible_ || !config_.is_dismissible) {
    return;
  }

  Hide();

  for (auto& observer : observers_) {
    observer.OnBannerDismissed();
  }
}

bool Allow2WarningBanner::IsVisible() const {
  return is_visible_;
}

BannerStyle Allow2WarningBanner::GetStyle() const {
  return current_style_;
}

const WarningBannerConfig& Allow2WarningBanner::GetConfig() const {
  return config_;
}

void Allow2WarningBanner::StartCountdown() {
  if (countdown_timer_.IsRunning()) {
    return;
  }

  countdown_seconds_ = config_.remaining_seconds;
  countdown_timer_.Start(
      FROM_HERE, base::Seconds(1),
      base::BindRepeating(&Allow2WarningBanner::OnCountdownTick,
                          weak_ptr_factory_.GetWeakPtr()));

  VLOG(1) << "Allow2: Countdown started at " << countdown_seconds_ << "s";
}

void Allow2WarningBanner::StopCountdown() {
  countdown_timer_.Stop();
}

bool Allow2WarningBanner::IsCountdownActive() const {
  return countdown_timer_.IsRunning();
}

int Allow2WarningBanner::GetCountdownSeconds() const {
  return countdown_seconds_;
}

// static
std::string Allow2WarningBanner::FormatRemainingTime(int remaining_seconds) {
  if (remaining_seconds <= 0) {
    return "0 seconds";
  }

  if (remaining_seconds < 60) {
    return base::StringPrintf("%d second%s", remaining_seconds,
                              remaining_seconds == 1 ? "" : "s");
  }

  int minutes = remaining_seconds / 60;
  int seconds = remaining_seconds % 60;

  if (seconds == 0 || minutes >= 5) {
    return base::StringPrintf("%d minute%s", minutes,
                              minutes == 1 ? "" : "s");
  }

  return base::StringPrintf("%d:%02d", minutes, seconds);
}

// static
std::string Allow2WarningBanner::GetWarningMessage(
    WarningLevel level,
    int remaining_seconds,
    const std::string& activity_name) {
  std::string time_str = FormatRemainingTime(remaining_seconds);
  std::string activity = activity_name.empty() ? "internet" : activity_name;

  switch (level) {
    case WarningLevel::kGentle:
      return time_str + " of " + activity + " time remaining";
    case WarningLevel::kWarning:
      return "Only " + time_str + " left!";
    case WarningLevel::kUrgent:
      return "Browsing ends in: " + time_str;
    case WarningLevel::kBlocked:
      return "Time's up!";
    default:
      return "";
  }
}

// static
std::string Allow2WarningBanner::GetBannerTitle(WarningLevel level) {
  switch (level) {
    case WarningLevel::kGentle:
      return "Time Reminder";
    case WarningLevel::kWarning:
      return "Time Warning";
    case WarningLevel::kUrgent:
      return "Time Almost Up";
    case WarningLevel::kBlocked:
      return "Time's Up!";
    default:
      return "";
  }
}

// static
std::string Allow2WarningBanner::GetBannerSubtitle(WarningLevel level,
                                                    const std::string& day_type) {
  std::string suffix = day_type.empty() ? "" : " (" + day_type + ")";

  switch (level) {
    case WarningLevel::kGentle:
      return "Your internet time is running low" + suffix;
    case WarningLevel::kWarning:
      return "Make sure to save your work" + suffix;
    case WarningLevel::kUrgent:
      return "Internet will be blocked soon" + suffix;
    case WarningLevel::kBlocked:
      return "Internet time has ended" + suffix;
    default:
      return "";
  }
}

void Allow2WarningBanner::SetRequestTimeCallback(RequestTimeCallback callback) {
  request_callback_ = std::move(callback);
}

void Allow2WarningBanner::HandleRequestTime() {
  VLOG(1) << "Allow2: Request time clicked from warning banner";

  if (request_callback_) {
    std::move(request_callback_).Run();
  }
}

void Allow2WarningBanner::HandleMoreInfo() {
  VLOG(1) << "Allow2: More info clicked from warning banner";

  for (auto& observer : observers_) {
    observer.OnMoreInfoClicked();
  }
}

BannerStyle Allow2WarningBanner::CalculateStyle(WarningLevel level,
                                                 int remaining_seconds) const {
  if (level == WarningLevel::kNone || level == WarningLevel::kBlocked) {
    return BannerStyle::kNone;
  }

  if (remaining_seconds <= kWarningThreshold30Sec) {
    return BannerStyle::kCountdown;
  }

  switch (level) {
    case WarningLevel::kGentle:
      return BannerStyle::kGentle;
    case WarningLevel::kWarning:
      return BannerStyle::kWarning;
    case WarningLevel::kUrgent:
      return BannerStyle::kUrgent;
    default:
      return BannerStyle::kNone;
  }
}

void Allow2WarningBanner::OnCountdownTick() {
  if (countdown_seconds_ > 0) {
    countdown_seconds_--;

    for (auto& observer : observers_) {
      observer.OnCountdownTick(countdown_seconds_);
    }

    // Update style if we crossed a threshold
    BannerStyle new_style =
        CalculateStyle(config_.level, countdown_seconds_);
    if (new_style != current_style_) {
      current_style_ = new_style;
      NotifyVisibilityChanged();
    }
  }

  if (countdown_seconds_ <= 0) {
    StopCountdown();
  }
}

void Allow2WarningBanner::OnAutoDismiss() {
  if (config_.is_dismissible && config_.level == WarningLevel::kGentle) {
    Dismiss();
  }
}

void Allow2WarningBanner::UpdateVisibility() {
  bool should_be_visible =
      current_style_ != BannerStyle::kNone && config_.remaining_seconds > 0;

  if (should_be_visible != is_visible_) {
    is_visible_ = should_be_visible;
    NotifyVisibilityChanged();
  }
}

void Allow2WarningBanner::NotifyVisibilityChanged() {
  for (auto& observer : observers_) {
    observer.OnBannerVisibilityChanged(is_visible_, current_style_);
  }
}

}  // namespace allow2

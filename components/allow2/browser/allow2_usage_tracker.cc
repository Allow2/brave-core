/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_usage_tracker.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "brave/components/allow2/browser/allow2_api_client.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "brave/components/allow2/common/allow2_utils.h"

namespace allow2 {

Allow2UsageTracker::Allow2UsageTracker(Allow2ApiClient* api_client)
    : api_client_(api_client) {
  DCHECK(api_client_);
}

Allow2UsageTracker::~Allow2UsageTracker() {
  StopTracking();
}

void Allow2UsageTracker::AddObserver(Allow2UsageTrackerObserver* observer) {
  observers_.AddObserver(observer);
}

void Allow2UsageTracker::RemoveObserver(Allow2UsageTrackerObserver* observer) {
  observers_.RemoveObserver(observer);
}

void Allow2UsageTracker::StartTracking(const Credentials& credentials,
                                         uint64_t child_id) {
  if (is_tracking_) {
    VLOG(1) << "Allow2: Already tracking, updating credentials";
    UpdateCredentials(credentials);
    UpdateChildId(child_id);
    return;
  }

  credentials_ = std::make_unique<Credentials>(credentials);
  child_id_ = child_id;
  is_tracking_ = true;
  is_paused_ = false;

  // Start the periodic check timer
  check_timer_.Start(
      FROM_HERE, check_interval_,
      base::BindRepeating(&Allow2UsageTracker::OnCheckTimer,
                          weak_ptr_factory_.GetWeakPtr()));

  VLOG(1) << "Allow2: Usage tracking started for child " << child_id;

  for (auto& observer : observers_) {
    observer.OnTrackingStarted();
  }

  // Perform initial check immediately
  PerformCheck(base::DoNothing());
}

void Allow2UsageTracker::StopTracking() {
  if (!is_tracking_) {
    return;
  }

  check_timer_.Stop();
  is_tracking_ = false;
  is_paused_ = false;
  credentials_.reset();
  child_id_ = 0;

  VLOG(1) << "Allow2: Usage tracking stopped";

  for (auto& observer : observers_) {
    observer.OnTrackingStopped();
  }
}

void Allow2UsageTracker::PauseTracking() {
  if (!is_tracking_ || is_paused_) {
    return;
  }

  check_timer_.Stop();
  is_paused_ = true;

  VLOG(1) << "Allow2: Usage tracking paused";
}

void Allow2UsageTracker::ResumeTracking() {
  if (!is_tracking_ || !is_paused_) {
    return;
  }

  is_paused_ = false;

  // Restart the timer
  check_timer_.Start(
      FROM_HERE, check_interval_,
      base::BindRepeating(&Allow2UsageTracker::OnCheckTimer,
                          weak_ptr_factory_.GetWeakPtr()));

  VLOG(1) << "Allow2: Usage tracking resumed";

  // Perform check immediately on resume
  PerformCheck(base::DoNothing());
}

bool Allow2UsageTracker::IsTracking() const {
  return is_tracking_ && !is_paused_;
}

bool Allow2UsageTracker::IsPaused() const {
  return is_paused_;
}

void Allow2UsageTracker::TrackUrl(const std::string& url) {
  if (!is_tracking_) {
    return;
  }

  // Categorize the URL and update current activity
  current_activity_ = CategorizeUrl(url);

  VLOG(2) << "Allow2: URL tracked, activity type: "
          << static_cast<int>(current_activity_);
}

void Allow2UsageTracker::TrackActivity(ActivityId activity) {
  current_activity_ = activity;
}

void Allow2UsageTracker::CheckNow(CheckCallback callback) {
  if (!credentials_) {
    CheckResult result;
    result.allowed = true;  // Default to allowed if not tracking
    std::move(callback).Run(result);
    return;
  }

  PerformCheck(std::move(callback));
}

void Allow2UsageTracker::SetCheckInterval(base::TimeDelta interval) {
  check_interval_ = interval;

  // Restart timer if currently running
  if (check_timer_.IsRunning()) {
    check_timer_.Stop();
    check_timer_.Start(
        FROM_HERE, check_interval_,
        base::BindRepeating(&Allow2UsageTracker::OnCheckTimer,
                            weak_ptr_factory_.GetWeakPtr()));
  }
}

base::TimeDelta Allow2UsageTracker::GetCheckInterval() const {
  return check_interval_;
}

void Allow2UsageTracker::SetLogUsage(bool log) {
  log_usage_ = log;
}

uint64_t Allow2UsageTracker::GetChildId() const {
  return child_id_;
}

void Allow2UsageTracker::UpdateCredentials(const Credentials& credentials) {
  credentials_ = std::make_unique<Credentials>(credentials);
}

void Allow2UsageTracker::UpdateChildId(uint64_t child_id) {
  child_id_ = child_id;

  // Perform check immediately with new child
  if (is_tracking_ && !is_paused_) {
    PerformCheck(base::DoNothing());
  }
}

void Allow2UsageTracker::OnCheckTimer() {
  if (!is_tracking_ || is_paused_) {
    return;
  }

  PerformCheck(base::DoNothing());
}

void Allow2UsageTracker::PerformCheck(CheckCallback callback) {
  if (!credentials_ || child_id_ == 0) {
    CheckResult result;
    result.allowed = true;
    if (callback) {
      std::move(callback).Run(result);
    }
    return;
  }

  std::vector<ActivityId> activities = {current_activity_};

  // Also include internet activity if tracking something else
  if (current_activity_ != ActivityId::kInternet) {
    activities.push_back(ActivityId::kInternet);
  }

  api_client_->Check(
      *credentials_, child_id_, activities, log_usage_,
      base::BindOnce(
          [](base::WeakPtr<Allow2UsageTracker> self, CheckCallback callback,
             const CheckResponse& response) {
            if (!self) {
              return;
            }

            if (response.http_status_code == 401) {
              self->NotifyCredentialsInvalidated();
              return;
            }

            self->OnCheckComplete(std::move(callback), response.success,
                                  response.http_status_code, response.result);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void Allow2UsageTracker::OnCheckComplete(CheckCallback callback, bool success,
                                          int http_status,
                                          const CheckResult& result) {
  if (success) {
    NotifyCheckResult(result);
  } else {
    VLOG(1) << "Allow2: Check failed with status " << http_status;
  }

  if (callback) {
    std::move(callback).Run(result);
  }
}

void Allow2UsageTracker::NotifyCheckResult(const CheckResult& result) {
  for (auto& observer : observers_) {
    observer.OnCheckResult(result);
  }
}

void Allow2UsageTracker::NotifyCredentialsInvalidated() {
  VLOG(1) << "Allow2: Credentials invalidated, stopping tracking";
  StopTracking();

  for (auto& observer : observers_) {
    observer.OnCredentialsInvalidated();
  }
}

}  // namespace allow2

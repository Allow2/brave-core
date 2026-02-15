/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_child_shield.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "brave/components/allow2/browser/allow2_api_client.h"
#include "brave/components/allow2/browser/allow2_child_manager.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "services/network/public/cpp/network_connection_tracker.h"

namespace allow2 {

Allow2ChildShield::Allow2ChildShield(Allow2ChildManager* child_manager,
                                       Allow2ApiClient* api_client)
    : child_manager_(child_manager), api_client_(api_client) {
  DCHECK(child_manager_);
}

Allow2ChildShield::Allow2ChildShield(Allow2ChildManager* child_manager)
    : child_manager_(child_manager), api_client_(nullptr) {
  DCHECK(child_manager_);
}

Allow2ChildShield::~Allow2ChildShield() {
  // Stop any active polling
  StopPushAuthPolling();
}

void Allow2ChildShield::AddObserver(Allow2ChildShieldObserver* observer) {
  observers_.AddObserver(observer);
}

void Allow2ChildShield::RemoveObserver(Allow2ChildShieldObserver* observer) {
  observers_.RemoveObserver(observer);
}

void Allow2ChildShield::Show() {
  ChildShieldConfig default_config;
  Show(default_config);
}

void Allow2ChildShield::Show(const ChildShieldConfig& config) {
  config_ = config;
  selected_child_id_ = 0;
  current_error_.clear();

  SetState(ShieldState::kSelectingChild);

  VLOG(1) << "Allow2: Child shield shown with "
          << child_manager_->GetChildren().size() << " children";
}

void Allow2ChildShield::Dismiss() {
  if (state_ == ShieldState::kHidden) {
    return;
  }

  SetState(ShieldState::kHidden);
  selected_child_id_ = 0;
  current_error_.clear();

  VLOG(1) << "Allow2: Child shield dismissed";

  for (auto& observer : observers_) {
    observer.OnShieldDismissed();
  }
}

bool Allow2ChildShield::IsVisible() const {
  return state_ != ShieldState::kHidden;
}

ShieldState Allow2ChildShield::GetState() const {
  return state_;
}

const ChildShieldConfig& Allow2ChildShield::GetConfig() const {
  return config_;
}

std::vector<ChildInfo> Allow2ChildShield::GetChildren() const {
  return child_manager_->GetChildren();
}

void Allow2ChildShield::SelectChild(uint64_t child_id) {
  if (state_ != ShieldState::kSelectingChild) {
    return;
  }

  std::optional<ChildInfo> child = FindChild(child_id);
  if (!child.has_value()) {
    LOG(ERROR) << "Allow2: Child not found: " << child_id;
    return;
  }

  selected_child_id_ = child_id;
  current_error_.clear();

  VLOG(1) << "Allow2: Child selected: " << child->name;

  for (auto& observer : observers_) {
    observer.OnChildSelected(child_id);
  }

  // Check if we should try push auth or go straight to PIN
  // Push auth is only available for children with accounts AND when online
  if (child->has_account && IsNetworkAvailable() && api_client_) {
    VLOG(1) << "Allow2: Child has account, attempting push auth";
    RequestPushAuth(child_id);
  } else {
    // Go straight to PIN entry
    if (!child->has_account) {
      VLOG(1) << "Allow2: Child has no account, using PIN";
    } else if (!IsNetworkAvailable()) {
      VLOG(1) << "Allow2: Offline, using PIN";
    } else {
      VLOG(1) << "Allow2: No API client, using PIN";
    }
    SetState(ShieldState::kEnteringPIN);
  }
}

void Allow2ChildShield::GoBackToSelection() {
  if (state_ != ShieldState::kEnteringPIN && state_ != ShieldState::kError) {
    return;
  }

  selected_child_id_ = 0;
  current_error_.clear();
  SetState(ShieldState::kSelectingChild);
}

uint64_t Allow2ChildShield::GetSelectedChildId() const {
  return selected_child_id_;
}

std::string Allow2ChildShield::GetSelectedChildName() const {
  std::optional<ChildInfo> child = FindChild(selected_child_id_);
  if (child.has_value()) {
    return child->name;
  }
  return "";
}

void Allow2ChildShield::SubmitPIN(const std::string& pin) {
  SubmitPINAsync(pin, base::DoNothing());
}

void Allow2ChildShield::SubmitPINAsync(const std::string& pin,
                                        SelectCallback callback) {
  if (state_ != ShieldState::kEnteringPIN) {
    std::move(callback).Run(false, "Invalid state");
    return;
  }

  if (selected_child_id_ == 0) {
    std::move(callback).Run(false, "No child selected");
    return;
  }

  if (IsLockedOut()) {
    int remaining = GetLockoutSecondsRemaining();
    std::string error = "PIN entry locked. Try again in " +
                        std::to_string(remaining / 60 + 1) + " minutes.";
    OnPINValidationResult(false, error);
    std::move(callback).Run(false, error);
    return;
  }

  SetState(ShieldState::kValidating);

  VLOG(1) << "Allow2: Validating PIN for child " << selected_child_id_;

  // Validate PIN through child manager
  child_manager_->SelectChildAsync(
      selected_child_id_, pin,
      base::BindOnce(
          [](base::WeakPtr<Allow2ChildShield> self, SelectCallback callback,
             bool success, const std::string& error) {
            if (!self) {
              return;
            }

            self->OnPINValidationResult(success, error);
            std::move(callback).Run(success, error);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void Allow2ChildShield::ClearPIN() {
  // Just clear UI state, nothing else needed
  current_error_.clear();
}

int Allow2ChildShield::GetRemainingPINAttempts() const {
  return child_manager_->GetRemainingPinAttempts();
}

bool Allow2ChildShield::IsLockedOut() const {
  return child_manager_->IsPinLockedOut();
}

int Allow2ChildShield::GetLockoutSecondsRemaining() const {
  return static_cast<int>(
      child_manager_->GetLockoutTimeRemaining().InSeconds());
}

void Allow2ChildShield::ShowError(const std::string& error) {
  current_error_ = error;
  SetState(ShieldState::kError);
}

void Allow2ChildShield::ClearError() {
  current_error_.clear();
  if (state_ == ShieldState::kError) {
    SetState(ShieldState::kEnteringPIN);
  }
}

std::string Allow2ChildShield::GetError() const {
  return current_error_;
}

void Allow2ChildShield::SelectGuest() {
  if (!config_.allow_guest) {
    LOG(WARNING) << "Allow2: Guest mode not allowed";
    return;
  }

  VLOG(1) << "Allow2: Guest mode selected";

  // Clear child selection in manager
  child_manager_->ClearSelection();
  Dismiss();
}

void Allow2ChildShield::Skip() {
  if (!config_.show_skip_link) {
    LOG(WARNING) << "Allow2: Skip not allowed";
    return;
  }

  VLOG(1) << "Allow2: Child selection skipped";

  Dismiss();
}

void Allow2ChildShield::SetState(ShieldState state) {
  if (state_ != state) {
    state_ = state;

    for (auto& observer : observers_) {
      observer.OnShieldStateChanged(state);
    }
  }
}

void Allow2ChildShield::OnPINValidationResult(bool success,
                                               const std::string& error) {
  if (success) {
    current_error_.clear();
    SetState(ShieldState::kSuccess);

    VLOG(1) << "Allow2: PIN accepted for child " << selected_child_id_;

    for (auto& observer : observers_) {
      observer.OnPINAccepted(selected_child_id_);
    }

    // Brief delay before dismissing to show success state
    Dismiss();
  } else {
    current_error_ = error;
    SetState(ShieldState::kError);

    LOG(WARNING) << "Allow2: PIN rejected: " << error;

    for (auto& observer : observers_) {
      observer.OnPINRejected(error);
    }

    // Check if now locked out
    if (IsLockedOut()) {
      for (auto& observer : observers_) {
        observer.OnLockoutStarted(GetLockoutSecondsRemaining());
      }
    }
  }
}

std::optional<ChildInfo> Allow2ChildShield::FindChild(uint64_t child_id) const {
  return child_manager_->GetChild(child_id);
}

// ============================================================================
// Push Authentication Implementation
// ============================================================================

bool Allow2ChildShield::IsNetworkAvailable() const {
  if (!network_tracker_) {
    // Assume online if no tracker is set
    return true;
  }

  network::mojom::ConnectionType connection_type;
  bool synchronous_result = network_tracker_->GetConnectionType(
      &connection_type,
      base::DoNothing());

  if (!synchronous_result) {
    // If we can't get synchronous result, assume online
    return true;
  }

  return connection_type != network::mojom::ConnectionType::CONNECTION_NONE;
}

void Allow2ChildShield::SetNetworkConnectionTracker(
    network::NetworkConnectionTracker* tracker) {
  network_tracker_ = tracker;
}

void Allow2ChildShield::RequestPushAuth(uint64_t child_id) {
  if (!api_client_) {
    LOG(ERROR) << "Allow2: No API client for push auth";
    SetState(ShieldState::kEnteringPIN);
    return;
  }

  std::optional<ChildInfo> child = FindChild(child_id);
  if (!child.has_value()) {
    LOG(ERROR) << "Allow2: Child not found for push auth: " << child_id;
    SetState(ShieldState::kEnteringPIN);
    return;
  }

  VLOG(1) << "Allow2: Requesting push auth for " << child->name;

  SetState(ShieldState::kWaitingForConfirmation);
  push_auth_start_time_ = base::Time::Now();

  // Notify observers we're waiting
  for (auto& observer : observers_) {
    observer.OnWaitingForConfirmation(child_id, child->name);
  }

  // TODO(allow2): Get credentials and device info from credential manager
  // For now, we use placeholder values - this will need to be wired up
  // to the Allow2Service which has access to credentials.
  //
  // The actual implementation would be:
  // api_client_->RequestChildAuth(
  //     credential_manager_->GetCredentials(),
  //     child_id,
  //     credential_manager_->GetDeviceUuid(),
  //     credential_manager_->GetDeviceName(),
  //     base::BindOnce(&Allow2ChildShield::OnPushAuthRequestComplete,
  //                    weak_ptr_factory_.GetWeakPtr()));

  // Start timeout timer
  push_auth_timeout_timer_.Start(
      FROM_HERE,
      base::Seconds(kPushAuthTimeoutSeconds),
      base::BindOnce(&Allow2ChildShield::OnPushAuthTimeout,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Allow2ChildShield::CancelPushAuth() {
  VLOG(1) << "Allow2: Cancelling push auth";

  StopPushAuthPolling();

  if (!current_auth_request_id_.empty() && api_client_) {
    // TODO(allow2): Cancel the request on the server
    // api_client_->CancelChildAuth(credentials, current_auth_request_id_);
  }

  current_auth_request_id_.clear();
}

void Allow2ChildShield::FallbackToPIN() {
  VLOG(1) << "Allow2: Falling back to PIN entry";

  CancelPushAuth();
  SetState(ShieldState::kEnteringPIN);
}

std::string Allow2ChildShield::GetCurrentAuthRequestId() const {
  return current_auth_request_id_;
}

int Allow2ChildShield::GetPushAuthSecondsRemaining() const {
  if (state_ != ShieldState::kWaitingForConfirmation) {
    return 0;
  }

  base::TimeDelta elapsed = base::Time::Now() - push_auth_start_time_;
  int remaining = kPushAuthTimeoutSeconds -
                  static_cast<int>(elapsed.InSeconds());
  return std::max(0, remaining);
}

bool Allow2ChildShield::IsPushAuthInProgress() const {
  return state_ == ShieldState::kWaitingForConfirmation ||
         state_ == ShieldState::kConfirmationTimeout;
}

void Allow2ChildShield::OnPushAuthRequestComplete(
    const std::string& request_id,
    const std::string& error) {
  if (!error.empty()) {
    LOG(WARNING) << "Allow2: Push auth request failed: " << error;
    // Fall back to PIN on error
    SetState(ShieldState::kEnteringPIN);
    return;
  }

  current_auth_request_id_ = request_id;
  VLOG(1) << "Allow2: Push auth request created: " << request_id;

  // Start polling for status
  StartPushAuthPolling();
}

void Allow2ChildShield::OnPushAuthStatusComplete(
    const std::string& status,
    const std::string& error) {
  if (!error.empty()) {
    LOG(WARNING) << "Allow2: Push auth status check failed: " << error;
    // Continue polling, don't fail yet
    return;
  }

  VLOG(1) << "Allow2: Push auth status: " << status;

  if (status == "confirmed") {
    // Success! Child confirmed on their device
    StopPushAuthPolling();

    VLOG(1) << "Allow2: Push auth confirmed for child " << selected_child_id_;

    // Select the child without PIN validation
    child_manager_->ResetPinAttempts();  // Reset any failed PIN attempts

    SetState(ShieldState::kSuccess);

    for (auto& observer : observers_) {
      observer.OnConfirmationAccepted(selected_child_id_);
      observer.OnPINAccepted(selected_child_id_);
    }

    Dismiss();

  } else if (status == "denied") {
    // Child denied the request
    StopPushAuthPolling();

    VLOG(1) << "Allow2: Push auth denied by child";

    SetState(ShieldState::kConfirmationDenied);

    for (auto& observer : observers_) {
      observer.OnConfirmationDenied(selected_child_id_);
    }

    // Show error and go back to selection
    current_error_ = "Request denied. Try again or enter PIN.";

  } else if (status == "expired") {
    // Request expired on server
    StopPushAuthPolling();
    OnPushAuthTimeout();

  } else if (status == "pending") {
    // Still waiting, continue polling
  }
}

void Allow2ChildShield::OnPushAuthTimeout() {
  VLOG(1) << "Allow2: Push auth timeout";

  StopPushAuthPolling();

  std::optional<ChildInfo> child = FindChild(selected_child_id_);
  std::string child_name = child.has_value() ? child->name : "Unknown";

  SetState(ShieldState::kConfirmationTimeout);

  for (auto& observer : observers_) {
    observer.OnConfirmationTimeout(selected_child_id_, child_name);
  }

  // The UI should show:
  // "Still waiting for [name] to confirm...
  //  Make sure push notifications are enabled,
  //  or enter PIN instead."
  // with a "Enter PIN" button that calls FallbackToPIN()
}

void Allow2ChildShield::StartPushAuthPolling() {
  VLOG(1) << "Allow2: Starting push auth polling";

  push_auth_poll_timer_.Start(
      FROM_HERE,
      base::Milliseconds(kPushAuthPollIntervalMs),
      base::BindRepeating(&Allow2ChildShield::PollPushAuthStatus,
                          weak_ptr_factory_.GetWeakPtr()));
}

void Allow2ChildShield::StopPushAuthPolling() {
  push_auth_poll_timer_.Stop();
  push_auth_timeout_timer_.Stop();
}

void Allow2ChildShield::PollPushAuthStatus() {
  if (current_auth_request_id_.empty()) {
    LOG(WARNING) << "Allow2: No auth request ID to poll";
    return;
  }

  if (!api_client_) {
    LOG(ERROR) << "Allow2: No API client for status poll";
    return;
  }

  // TODO(allow2): Actually make the API call
  // api_client_->CheckChildAuthStatus(
  //     credentials,
  //     current_auth_request_id_,
  //     base::BindOnce(&Allow2ChildShield::OnPushAuthStatusComplete,
  //                    weak_ptr_factory_.GetWeakPtr()));

  VLOG(2) << "Allow2: Polling push auth status for " << current_auth_request_id_;
}

}  // namespace allow2

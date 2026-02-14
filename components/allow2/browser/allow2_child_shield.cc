/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_child_shield.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "brave/components/allow2/browser/allow2_child_manager.h"

namespace allow2 {

Allow2ChildShield::Allow2ChildShield(Allow2ChildManager* child_manager)
    : child_manager_(child_manager) {
  DCHECK(child_manager_);
}

Allow2ChildShield::~Allow2ChildShield() = default;

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

  SetState(ShieldState::kEnteringPIN);
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

}  // namespace allow2

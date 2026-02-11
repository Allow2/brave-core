/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_child_manager.h"

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/components/allow2/common/allow2_utils.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace allow2 {

Allow2ChildManager::Allow2ChildManager(PrefService* profile_prefs)
    : profile_prefs_(profile_prefs) {
  DCHECK(profile_prefs_);
  LoadChildrenFromPrefs();

  // Load selected child if any
  std::string child_id_str =
      profile_prefs_->GetString(prefs::kAllow2ChildId);
  if (!child_id_str.empty()) {
    try {
      selected_child_id_ = std::stoull(child_id_str);
    } catch (...) {
      selected_child_id_ = std::nullopt;
    }
  }

  last_activity_time_ = base::Time::Now();
}

Allow2ChildManager::~Allow2ChildManager() = default;

void Allow2ChildManager::AddObserver(Allow2ChildManagerObserver* observer) {
  observers_.AddObserver(observer);
}

void Allow2ChildManager::RemoveObserver(Allow2ChildManagerObserver* observer) {
  observers_.RemoveObserver(observer);
}

void Allow2ChildManager::UpdateChildList(
    const std::vector<ChildInfo>& children) {
  children_ = children;
  SaveChildrenToPrefs();

  for (auto& observer : observers_) {
    observer.OnChildListUpdated(children_);
  }
}

std::vector<ChildInfo> Allow2ChildManager::GetChildren() const {
  return children_;
}

std::optional<ChildInfo> Allow2ChildManager::GetChild(uint64_t child_id) const {
  for (const auto& child : children_) {
    if (child.id == child_id) {
      return child;
    }
  }
  return std::nullopt;
}

bool Allow2ChildManager::HasSelectedChild() const {
  return selected_child_id_.has_value();
}

std::optional<ChildInfo> Allow2ChildManager::GetSelectedChild() const {
  if (!selected_child_id_.has_value()) {
    return std::nullopt;
  }
  return GetChild(*selected_child_id_);
}

bool Allow2ChildManager::SelectChild(uint64_t child_id, const std::string& pin) {
  // Check rate limiting first
  if (IsPinLockedOut()) {
    LOG(WARNING) << "Allow2: PIN entry is locked out";
    return false;
  }

  // Find the child
  std::optional<ChildInfo> child = GetChild(child_id);
  if (!child.has_value()) {
    LOG(WARNING) << "Allow2: Child not found: " << child_id;
    return false;
  }

  // Validate PIN using constant-time comparison
  if (!ValidatePinHash(pin, child->pin_hash, child->pin_salt)) {
    failed_pin_attempts_++;

    if (failed_pin_attempts_ >= kMaxPinAttempts) {
      lockout_expiry_ =
          base::Time::Now() + base::Minutes(kLockoutDurationMinutes);
      LOG(WARNING) << "Allow2: PIN locked out for "
                   << kLockoutDurationMinutes << " minutes";
    }

    for (auto& observer : observers_) {
      observer.OnPinValidationFailed(child_id);
    }

    return false;
  }

  // Success - select child
  selected_child_id_ = child_id;
  failed_pin_attempts_ = 0;
  last_activity_time_ = base::Time::Now();

  // Save to prefs
  profile_prefs_->SetString(prefs::kAllow2ChildId, std::to_string(child_id));

  // Notify observers
  for (auto& observer : observers_) {
    observer.OnChildSelected(child);
  }

  VLOG(1) << "Allow2: Child selected: " << child->name;
  return true;
}

void Allow2ChildManager::SelectChildAsync(uint64_t child_id,
                                           const std::string& pin,
                                           SelectChildCallback callback) {
  // Run synchronous selection on current sequence
  bool success = SelectChild(child_id, pin);
  std::string error;

  if (!success) {
    if (IsPinLockedOut()) {
      error = "PIN entry is locked. Please try again later.";
    } else {
      error = "Incorrect PIN. Please try again.";
    }
  }

  std::move(callback).Run(success, error);
}

void Allow2ChildManager::ClearSelection() {
  selected_child_id_ = std::nullopt;
  profile_prefs_->ClearPref(prefs::kAllow2ChildId);

  for (auto& observer : observers_) {
    observer.OnChildSelected(std::nullopt);
    if (shared_device_mode_) {
      observer.OnChildSelectionRequired();
    }
  }
}

bool Allow2ChildManager::IsSharedDeviceMode() const {
  return shared_device_mode_;
}

void Allow2ChildManager::SetSharedDeviceMode(bool shared) {
  shared_device_mode_ = shared;

  if (shared && !HasSelectedChild()) {
    for (auto& observer : observers_) {
      observer.OnChildSelectionRequired();
    }
  }
}

bool Allow2ChildManager::IsChildSelectionRequired() const {
  if (!shared_device_mode_) {
    return false;
  }

  if (!HasSelectedChild()) {
    return true;
  }

  // Check for session timeout
  return HasSessionTimedOut();
}

void Allow2ChildManager::RecordActivity() {
  last_activity_time_ = base::Time::Now();
}

bool Allow2ChildManager::HasSessionTimedOut() const {
  if (!shared_device_mode_) {
    return false;
  }

  base::TimeDelta elapsed = base::Time::Now() - last_activity_time_;
  return elapsed > base::Minutes(kSessionTimeoutMinutes);
}

int Allow2ChildManager::GetRemainingPinAttempts() const {
  if (IsPinLockedOut()) {
    return 0;
  }
  return std::max(0, kMaxPinAttempts - failed_pin_attempts_);
}

bool Allow2ChildManager::IsPinLockedOut() const {
  if (failed_pin_attempts_ < kMaxPinAttempts) {
    return false;
  }
  return base::Time::Now() < lockout_expiry_;
}

base::TimeDelta Allow2ChildManager::GetLockoutTimeRemaining() const {
  if (!IsPinLockedOut()) {
    return base::TimeDelta();
  }
  return lockout_expiry_ - base::Time::Now();
}

void Allow2ChildManager::ResetPinAttempts() {
  failed_pin_attempts_ = 0;
  lockout_expiry_ = base::Time();
}

void Allow2ChildManager::LoadChildrenFromPrefs() {
  std::string children_json =
      profile_prefs_->GetString(prefs::kAllow2CachedChildren);
  if (children_json.empty()) {
    return;
  }

  auto parsed = base::JSONReader::Read(children_json);
  if (!parsed || !parsed->is_list()) {
    return;
  }

  children_.clear();
  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }

    const base::Value::Dict& dict = item.GetDict();
    ChildInfo child;
    child.id = static_cast<uint64_t>(dict.FindInt("id").value_or(0));

    const std::string* name = dict.FindString("name");
    if (name) {
      child.name = *name;
    }

    const std::string* pin_hash = dict.FindString("pinHash");
    if (pin_hash) {
      child.pin_hash = *pin_hash;
    }

    const std::string* pin_salt = dict.FindString("pinSalt");
    if (pin_salt) {
      child.pin_salt = *pin_salt;
    }

    const std::string* avatar_url = dict.FindString("avatarUrl");
    if (avatar_url) {
      child.avatar_url = *avatar_url;
    }

    children_.push_back(child);
  }
}

void Allow2ChildManager::SaveChildrenToPrefs() {
  base::Value::List children_list;

  for (const auto& child : children_) {
    base::Value::Dict child_dict;
    child_dict.Set("id", static_cast<int>(child.id));
    child_dict.Set("name", child.name);
    child_dict.Set("pinHash", child.pin_hash);
    child_dict.Set("pinSalt", child.pin_salt);
    if (!child.avatar_url.empty()) {
      child_dict.Set("avatarUrl", child.avatar_url);
    }
    children_list.Append(std::move(child_dict));
  }

  std::string json;
  base::JSONWriter::Write(children_list, &json);
  profile_prefs_->SetString(prefs::kAllow2CachedChildren, json);
}

bool Allow2ChildManager::ValidatePinWithRateLimit(uint64_t child_id,
                                                   const std::string& pin) {
  if (IsPinLockedOut()) {
    return false;
  }

  std::optional<ChildInfo> child = GetChild(child_id);
  if (!child.has_value()) {
    return false;
  }

  return ValidatePinHash(pin, child->pin_hash, child->pin_salt);
}

bool Allow2ChildManager::IsLockedToChild(uint64_t child_id) const {
  if (shared_device_mode_) {
    return false;
  }
  return selected_child_id_.has_value() && *selected_child_id_ == child_id;
}

}  // namespace allow2

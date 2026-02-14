/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_service.h"

#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "brave/components/allow2/browser/allow2_api_client.h"
#include "brave/components/allow2/browser/allow2_block_overlay.h"
#include "brave/components/allow2/browser/allow2_child_manager.h"
#include "brave/components/allow2/browser/allow2_child_shield.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/browser/allow2_pairing_handler.h"
#include "brave/components/allow2/browser/allow2_usage_tracker.h"
#include "brave/components/allow2/browser/allow2_warning_banner.h"
#include "brave/components/allow2/browser/allow2_warning_controller.h"
#include "brave/components/allow2/common/allow2_utils.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace allow2 {

// Child struct out-of-line definitions
Child::Child() = default;
Child::~Child() = default;
Child::Child(const Child&) = default;
Child& Child::operator=(const Child&) = default;
Child::Child(Child&&) = default;
Child& Child::operator=(Child&&) = default;

// CheckResult struct out-of-line definitions
CheckResult::CheckResult() = default;
CheckResult::~CheckResult() = default;
CheckResult::CheckResult(const CheckResult&) = default;
CheckResult& CheckResult::operator=(const CheckResult&) = default;
CheckResult::CheckResult(CheckResult&&) = default;
CheckResult& CheckResult::operator=(CheckResult&&) = default;

// PairingSession struct out-of-line definitions
Allow2Service::PairingSession::PairingSession() = default;
Allow2Service::PairingSession::~PairingSession() = default;
Allow2Service::PairingSession::PairingSession(const PairingSession&) = default;
Allow2Service::PairingSession& Allow2Service::PairingSession::operator=(const PairingSession&) = default;
Allow2Service::PairingSession::PairingSession(PairingSession&&) = default;
Allow2Service::PairingSession& Allow2Service::PairingSession::operator=(PairingSession&&) = default;

Allow2Service::Allow2Service(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    PrefService* local_state,
    PrefService* profile_prefs)
    : local_state_(local_state),
      profile_prefs_(profile_prefs),
      api_client_(std::make_unique<Allow2ApiClient>(url_loader_factory)),
      credential_manager_(std::make_unique<Allow2CredentialManager>(local_state)),
      warning_controller_(std::make_unique<Allow2WarningController>()) {
  DCHECK(local_state_);
  DCHECK(profile_prefs_);

  // Initialize all component managers
  block_overlay_ = std::make_unique<Allow2BlockOverlay>();
  child_manager_ = std::make_unique<Allow2ChildManager>(profile_prefs_);
  child_shield_ = std::make_unique<Allow2ChildShield>(child_manager_.get());
  pairing_handler_ = std::make_unique<Allow2PairingHandler>(
      api_client_.get(), credential_manager_.get());
  usage_tracker_ = std::make_unique<Allow2UsageTracker>(api_client_.get());
  warning_banner_ = std::make_unique<Allow2WarningBanner>();

  // Set up warning controller callbacks
  warning_controller_->SetWarningCallback(
      base::BindRepeating(&Allow2Service::NotifyWarningThreshold,
                          weak_ptr_factory_.GetWeakPtr()));

  warning_controller_->SetBlockCallback(
      base::BindOnce(&Allow2Service::NotifyBlockingStateChanged,
                     weak_ptr_factory_.GetWeakPtr(), true));

  // Set up usage tracker observer to receive check results
  usage_tracker_->AddObserver(this);

  // Observe child shield for PIN acceptance events
  child_shield_->AddObserver(this);

  // Set up block overlay request time callback
  block_overlay_->SetRequestTimeCallback(
      base::BindOnce(&Allow2Service::OnRequestTimeFromOverlay,
                     weak_ptr_factory_.GetWeakPtr()));

  // Set up warning banner request time callback
  warning_banner_->SetRequestTimeCallback(
      base::BindOnce(&Allow2Service::OnRequestTimeFromBanner,
                     weak_ptr_factory_.GetWeakPtr()));

  // Load cached state
  is_blocked_ = profile_prefs_->GetBoolean(prefs::kAllow2Blocked);
  remaining_seconds_ = profile_prefs_->GetInteger(prefs::kAllow2RemainingSeconds);

  // Load cached children into child manager
  std::vector<ChildInfo> child_infos;
  auto children = GetChildren();
  for (const auto& child : children) {
    ChildInfo info;
    info.id = child.id;
    info.name = child.name;
    info.pin_hash = child.pin_hash;
    info.pin_salt = child.pin_salt;
    child_infos.push_back(info);
  }
  child_manager_->UpdateChildList(child_infos);

  // Start check timer if paired and enabled
  if (IsPaired() && IsEnabled()) {
    StartCheckTimer();
  }
}

Allow2Service::~Allow2Service() {
  if (usage_tracker_) {
    usage_tracker_->RemoveObserver(this);
  }
  if (child_shield_) {
    child_shield_->RemoveObserver(this);
  }
}

void Allow2Service::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StopCheckTimer();
  StopTracking();
  observers_.Clear();
}

void Allow2Service::AddObserver(Allow2ServiceObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void Allow2Service::RemoveObserver(Allow2ServiceObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

bool Allow2Service::IsPaired() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return credential_manager_->HasCredentials();
}

void Allow2Service::InitQRPairing(const std::string& device_name,
                                   InitPairingCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Generate device token if not already set
  std::string device_token = credential_manager_->GetDeviceToken();
  if (device_token.empty()) {
    device_token = GenerateDeviceToken();
    credential_manager_->SetDeviceToken(device_token);
  }

  credential_manager_->SetDeviceName(device_name);

  // Initialize QR pairing session - device displays QR, parent scans
  // Parent authenticates with passkey on their device, never enters
  // credentials on child's device
  api_client_->InitQRPairing(
      device_token, device_name,
      base::BindOnce(
          [](base::WeakPtr<Allow2Service> self, InitPairingCallback callback,
             const InitPairingResponse& response) {
            if (!self) {
              return;
            }

            if (response.success) {
              PairingSession session;
              session.session_id = response.session_id;
              // Generate Universal Link URL for QR code (enables iOS/Android deep linking)
              session.web_pairing_url = "https://app.allow2.com/pair?sessionId=" +
                  response.session_id + "&deviceName=Brave%20Browser";
              session.qr_code_data = response.qr_code_url;  // QR code image data
              session.pin_code = "";  // QR pairing doesn't use PIN
              std::move(callback).Run(true, session, "");
            } else {
              PairingSession empty_session;
              std::move(callback).Run(false, empty_session, response.error);
            }
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void Allow2Service::InitPINPairing(const std::string& device_name,
                                    InitPairingCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Generate device token if not already set
  std::string device_token = credential_manager_->GetDeviceToken();
  if (device_token.empty()) {
    device_token = GenerateDeviceToken();
    credential_manager_->SetDeviceToken(device_token);
  }

  credential_manager_->SetDeviceName(device_name);

  // Initialize PIN pairing session - device displays 6-digit PIN,
  // parent enters in their Allow2 app and authenticates with passkey
  api_client_->InitPINPairing(
      device_token, device_name,
      base::BindOnce(
          [](base::WeakPtr<Allow2Service> self, InitPairingCallback callback,
             const InitPairingResponse& response) {
            if (!self) {
              return;
            }

            if (response.success) {
              PairingSession session;
              session.session_id = response.session_id;
              // Generate Universal Link URL (enables iOS/Android deep linking)
              session.web_pairing_url = "https://app.allow2.com/pair?sessionId=" +
                  response.session_id + "&pin=" + response.pin_code +
                  "&deviceName=Brave%20Browser";
              session.qr_code_data = "";  // PIN pairing: no pre-generated QR
              session.pin_code = response.pin_code;
              std::move(callback).Run(true, session, "");
            } else {
              PairingSession empty_session;
              std::move(callback).Run(false, empty_session, response.error);
            }
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void Allow2Service::CheckPairingStatus(const std::string& session_id,
                                        PairingStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  api_client_->CheckPairingStatus(
      session_id,
      base::BindOnce(
          [](base::WeakPtr<Allow2Service> self, PairingStatusCallback callback,
             const PairingStatusResponse& response) {
            if (!self) {
              return;
            }

            if (response.completed) {
              if (response.success) {
                // Parent approved - store credentials
                self->credential_manager_->StoreCredentials(
                    response.user_id, response.pair_id, response.pair_token);

                // Cache children list
                base::Value::List children_list;
                for (const auto& child : response.children) {
                  base::Value::Dict child_dict;
                  child_dict.Set("id", static_cast<int>(child.id));
                  child_dict.Set("name", child.name);
                  child_dict.Set("pinHash", child.pin_hash);
                  child_dict.Set("pinSalt", child.pin_salt);
                  children_list.Append(std::move(child_dict));
                }
                std::string children_json;
                base::JSONWriter::Write(children_list, &children_json);
                self->profile_prefs_->SetString(prefs::kAllow2CachedChildren,
                                                children_json);

                // Notify observers
                for (auto& observer : self->observers_) {
                  observer.OnPairedStateChanged(true);
                }

                // Start check timer
                self->StartCheckTimer();

                std::move(callback).Run(true, true, "");
              } else {
                // Parent declined or session expired
                std::move(callback).Run(true, false, response.error);
              }
            } else {
              // Still pending - parent hasn't responded yet
              std::move(callback).Run(false, false, "");
            }
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void Allow2Service::CancelPairing(const std::string& session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  api_client_->CancelPairing(session_id);
}

std::vector<Child> Allow2Service::GetChildren() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<Child> children;

  std::string children_json =
      profile_prefs_->GetString(prefs::kAllow2CachedChildren);
  if (children_json.empty()) {
    return children;
  }

  auto parsed = base::JSONReader::Read(children_json,
                                        base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_list()) {
    return children;
  }

  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }

    const base::Value::Dict& dict = item.GetDict();
    Child child;
    child.id = dict.FindInt("id").value_or(0);
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

    children.push_back(child);
  }

  return children;
}

bool Allow2Service::IsSharedDeviceMode() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return allow2::IsSharedDeviceMode(profile_prefs_);
}

bool Allow2Service::SelectChild(uint64_t child_id, const std::string& pin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto children = GetChildren();
  for (const auto& child : children) {
    if (child.id == child_id) {
      // Validate PIN
      if (!ValidatePinHash(pin, child.pin_hash, child.pin_salt)) {
        LOG(WARNING) << "Allow2: Invalid PIN for child " << child_id;
        return false;
      }

      // Set current child
      profile_prefs_->SetString(prefs::kAllow2ChildId,
                                std::to_string(child_id));

      // Notify observers
      for (auto& observer : observers_) {
        observer.OnCurrentChildChanged(child);
      }

      // Trigger an immediate check
      CheckAllowance(base::DoNothing());

      return true;
    }
  }

  LOG(WARNING) << "Allow2: Child " << child_id << " not found";
  return false;
}

std::optional<Child> Allow2Service::GetCurrentChild() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string child_id_str = GetCurrentChildId(profile_prefs_);
  if (child_id_str.empty()) {
    return std::nullopt;
  }

  uint64_t child_id = std::stoull(child_id_str);
  auto children = GetChildren();
  for (const auto& child : children) {
    if (child.id == child_id) {
      return child;
    }
  }

  return std::nullopt;
}

void Allow2Service::ClearCurrentChild() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  profile_prefs_->ClearPref(prefs::kAllow2ChildId);

  for (auto& observer : observers_) {
    observer.OnCurrentChildChanged(std::nullopt);
  }
}

void Allow2Service::CheckAllowance(CheckCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto credentials = credential_manager_->GetCredentials();
  if (!credentials) {
    CheckResult result;
    result.allowed = true;  // Default to allowed if not paired
    std::move(callback).Run(result);
    return;
  }

  std::string child_id_str = GetCurrentChildId(profile_prefs_);
  if (child_id_str.empty()) {
    // In shared device mode without child selected, allow by default
    CheckResult result;
    result.allowed = true;
    std::move(callback).Run(result);
    return;
  }

  uint64_t child_id = std::stoull(child_id_str);
  std::vector<ActivityId> activities = {ActivityId::kInternet};

  api_client_->Check(
      *credentials, child_id, activities, true,
      base::BindOnce(
          [](base::WeakPtr<Allow2Service> self, CheckCallback callback,
             const CheckResponse& response) {
            if (!self) {
              return;
            }

            if (response.http_status_code == 401) {
              // Credentials invalidated - device was unpaired remotely
              self->OnCredentialsInvalidated();
              return;
            }

            if (response.success) {
              self->OnCheckResult(response.result);
              self->CacheCheckResult(response.result);
              std::move(callback).Run(response.result);
            } else {
              // On error, use cached result if available
              auto cached = self->LoadCachedCheckResult();
              if (cached) {
                std::move(callback).Run(*cached);
              } else {
                CheckResult default_result;
                default_result.allowed = true;
                std::move(callback).Run(default_result);
              }
            }
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void Allow2Service::TrackUrl(const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsPaired() || !IsEnabled()) {
    return;
  }

  // Categorize URL and trigger check with appropriate activity
  [[maybe_unused]] ActivityId activity = CategorizeUrl(url);
  // TODO(allow2): Pass activity to CheckAllowance when API supports it
  CheckAllowance(base::DoNothing());
}

bool Allow2Service::IsBlocked() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_blocked_;
}

int Allow2Service::GetRemainingSeconds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return remaining_seconds_;
}

std::string Allow2Service::GetBlockReason() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return block_reason_;
}

void Allow2Service::RequestMoreTime(ActivityId activity_id,
                                     int minutes,
                                     const std::string& message,
                                     RequestTimeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto credentials = credential_manager_->GetCredentials();
  if (!credentials) {
    std::move(callback).Run(false, "Not paired");
    return;
  }

  std::string child_id_str = GetCurrentChildId(profile_prefs_);
  if (child_id_str.empty()) {
    std::move(callback).Run(false, "No child selected");
    return;
  }

  uint64_t child_id = std::stoull(child_id_str);

  api_client_->RequestTime(
      *credentials, child_id, activity_id, minutes, message,
      base::BindOnce(
          [](RequestTimeCallback callback,
             const RequestTimeResponse& response) {
            std::move(callback).Run(response.success, response.error);
          },
          std::move(callback)));
}

bool Allow2Service::IsEnabled() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return allow2::IsEnabled(profile_prefs_);
}

void Allow2Service::SetEnabled(bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  profile_prefs_->SetBoolean(prefs::kAllow2Enabled, enabled);

  if (enabled && IsPaired()) {
    StartCheckTimer();
  } else {
    StopCheckTimer();
  }
}

Allow2CredentialManager* Allow2Service::GetCredentialManagerForTesting() {
  return credential_manager_.get();
}

// ============================================================================
// Component Accessors
// ============================================================================

Allow2BlockOverlay* Allow2Service::GetBlockOverlay() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return block_overlay_.get();
}

Allow2ChildShield* Allow2Service::GetChildShield() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return child_shield_.get();
}

Allow2WarningBanner* Allow2Service::GetWarningBanner() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return warning_banner_.get();
}

Allow2ChildManager* Allow2Service::GetChildManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return child_manager_.get();
}

Allow2PairingHandler* Allow2Service::GetPairingHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return pairing_handler_.get();
}

Allow2UsageTracker* Allow2Service::GetUsageTracker() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return usage_tracker_.get();
}

// ============================================================================
// Tracking Control
// ============================================================================

void Allow2Service::StartTracking() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!IsPaired() || !IsEnabled()) {
    return;
  }

  auto credentials = credential_manager_->GetCredentials();
  if (!credentials) {
    return;
  }

  std::string child_id_str = GetCurrentChildId(profile_prefs_);
  if (child_id_str.empty()) {
    return;
  }

  uint64_t child_id = std::stoull(child_id_str);
  usage_tracker_->StartTracking(*credentials, child_id);
}

void Allow2Service::StopTracking() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (usage_tracker_) {
    usage_tracker_->StopTracking();
  }
}

void Allow2Service::PauseTracking() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (usage_tracker_) {
    usage_tracker_->PauseTracking();
  }
}

void Allow2Service::ResumeTracking() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (usage_tracker_) {
    usage_tracker_->ResumeTracking();
  }
}

bool Allow2Service::IsTrackingActive() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return usage_tracker_ && usage_tracker_->IsTracking();
}

// ============================================================================
// UI State Helpers
// ============================================================================

bool Allow2Service::ShouldShowBlockOverlay() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Don't show if not paired or not enabled
  if (!IsPaired() || !IsEnabled()) {
    return false;
  }

  // Show if currently blocked
  return is_blocked_;
}

bool Allow2Service::ShouldShowChildShield() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Don't show if not paired or not enabled
  if (!IsPaired() || !IsEnabled()) {
    return false;
  }

  // Show in shared device mode when no child is selected
  if (IsSharedDeviceMode() && !GetCurrentChild().has_value()) {
    return true;
  }

  return false;
}

void Allow2Service::ShowBlockOverlay() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!block_overlay_) {
    return;
  }

  BlockOverlayConfig config;
  config.reason = BlockReason::kTimeLimitReached;
  config.reason_text = block_reason_;
  config.show_request_button = true;
  config.show_switch_user_button = IsSharedDeviceMode();

  block_overlay_->Show(config);
}

void Allow2Service::DismissBlockOverlay() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (block_overlay_) {
    block_overlay_->Dismiss();
  }
}

void Allow2Service::ShowChildShield() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (child_shield_) {
    child_shield_->Show();
  }
}

void Allow2Service::DismissChildShield() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (child_shield_) {
    child_shield_->Dismiss();
  }
}

// ============================================================================
// Internal Callbacks
// ============================================================================

void Allow2Service::OnRequestTimeFromOverlay(int minutes,
                                              const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  RequestMoreTime(ActivityId::kInternet, minutes, message,
                  base::BindOnce([](bool success, const std::string& error) {
                    if (!success) {
                      LOG(WARNING) << "Allow2: Request time failed: " << error;
                    }
                  }));
}

void Allow2Service::OnRequestTimeFromBanner() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Request 15 minutes by default from the warning banner
  OnRequestTimeFromOverlay(15, "");
}

// Allow2ChildShieldObserver implementation
void Allow2Service::OnPINAccepted(uint64_t child_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Store the selected child ID in prefs
  profile_prefs_->SetString(prefs::kAllow2ChildId, std::to_string(child_id));

  // Notify observers of child change
  auto children = GetChildren();
  for (const auto& child : children) {
    if (child.id == child_id) {
      for (auto& observer : observers_) {
        observer.OnCurrentChildChanged(child);
      }
      break;
    }
  }

  // Start tracking for the newly selected child
  StartTracking();

  // Trigger an immediate check
  CheckAllowance(base::DoNothing());

  // Dismiss the child shield
  DismissChildShield();
}

// Allow2UsageTrackerObserver implementation
void Allow2Service::OnCheckResult(const CheckResult& result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  OnCheckResultInternal(result);
}

void Allow2Service::OnTrackingStarted() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "Allow2: Usage tracking started";
}

void Allow2Service::OnTrackingStopped() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "Allow2: Usage tracking stopped";
}

void Allow2Service::OnCredentialsInvalidated() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  OnCredentialsInvalidatedInternal();
}

void Allow2Service::OnCheckResultInternal(const CheckResult& result) {
  bool was_blocked = is_blocked_;
  is_blocked_ = !result.allowed;
  block_reason_ = result.block_reason;
  remaining_seconds_ = result.remaining_seconds;

  // Update prefs
  profile_prefs_->SetBoolean(prefs::kAllow2Blocked, is_blocked_);
  profile_prefs_->SetInteger(prefs::kAllow2RemainingSeconds, remaining_seconds_);
  profile_prefs_->SetString(prefs::kAllow2DayTypeToday, result.day_type);
  profile_prefs_->SetTime(prefs::kAllow2LastCheckTime, base::Time::Now());

  // Update warning controller
  warning_controller_->UpdateRemainingTime(remaining_seconds_);

  // Update warning banner
  if (warning_banner_) {
    WarningBannerConfig banner_config;
    banner_config.remaining_seconds = remaining_seconds_;
    banner_config.day_type = result.day_type;

    if (remaining_seconds_ <= kWarningThreshold30Sec) {
      banner_config.level = WarningLevel::kUrgent;
    } else if (remaining_seconds_ <= kWarningThreshold1Min) {
      banner_config.level = WarningLevel::kUrgent;
    } else if (remaining_seconds_ <= kWarningThreshold5Min) {
      banner_config.level = WarningLevel::kWarning;
    } else if (remaining_seconds_ <= kWarningThreshold15Min) {
      banner_config.level = WarningLevel::kGentle;
    } else {
      banner_config.level = WarningLevel::kNone;
    }

    if (banner_config.level != WarningLevel::kNone) {
      warning_banner_->Update(banner_config);
    } else {
      warning_banner_->Hide();
    }
  }

  // Update block overlay
  if (block_overlay_) {
    if (is_blocked_ && !was_blocked) {
      ShowBlockOverlay();
    } else if (!is_blocked_ && was_blocked) {
      DismissBlockOverlay();
    }
  }

  // Notify observers of state changes
  if (was_blocked != is_blocked_) {
    NotifyBlockingStateChanged(is_blocked_, block_reason_);
  }

  NotifyRemainingTimeUpdated(remaining_seconds_);

  // Cache result
  CacheCheckResult(result);
}

void Allow2Service::OnCredentialsInvalidatedInternal() {
  LOG(WARNING) << "Allow2: Credentials invalidated (device unpaired remotely)";

  // Clear all credentials and state
  credential_manager_->ClearCredentials();
  profile_prefs_->ClearPref(prefs::kAllow2ChildId);
  profile_prefs_->ClearPref(prefs::kAllow2CachedChildren);
  profile_prefs_->ClearPref(prefs::kAllow2Blocked);
  profile_prefs_->ClearPref(prefs::kAllow2RemainingSeconds);

  // Stop check timer and tracking
  StopCheckTimer();
  StopTracking();

  // Reset state
  is_blocked_ = false;
  remaining_seconds_ = 0;
  block_reason_.clear();
  warning_controller_->Reset();

  // Hide any visible UI elements
  DismissBlockOverlay();
  if (warning_banner_) {
    warning_banner_->Hide();
  }

  // Clear child manager
  if (child_manager_) {
    child_manager_->UpdateChildList({});
    child_manager_->ClearSelection();
  }

  // Notify observers
  for (auto& observer : observers_) {
    observer.OnCredentialsInvalidated();
    observer.OnPairedStateChanged(false);
  }
}

void Allow2Service::StartCheckTimer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (check_timer_.IsRunning()) {
    return;
  }

  check_timer_.Start(
      FROM_HERE, base::Seconds(kAllow2CheckIntervalSeconds),
      base::BindRepeating(&Allow2Service::CheckAllowance,
                          weak_ptr_factory_.GetWeakPtr(), base::DoNothing()));
}

void Allow2Service::StopCheckTimer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  check_timer_.Stop();
}


void Allow2Service::NotifyBlockingStateChanged(bool is_blocked,
                                                const std::string& reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnBlockingStateChanged(is_blocked, reason);
  }
}

void Allow2Service::NotifyRemainingTimeUpdated(int remaining_seconds) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnRemainingTimeUpdated(remaining_seconds);
  }
}

void Allow2Service::NotifyWarningThreshold(WarningLevel level,
                                            int remaining_seconds) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  last_warning_level_ = level;
  for (auto& observer : observers_) {
    observer.OnWarningThresholdReached(remaining_seconds);
  }
}

bool Allow2Service::ShouldShowWarning(int remaining_seconds) const {
  return remaining_seconds <= kWarningThreshold15Min && remaining_seconds > 0;
}

void Allow2Service::CacheCheckResult(const CheckResult& result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::Value::Dict dict;
  dict.Set("allowed", result.allowed);
  dict.Set("remaining_seconds", result.remaining_seconds);
  dict.Set("expires", static_cast<double>(result.expires));
  dict.Set("banned", result.banned);
  dict.Set("day_type", result.day_type);
  dict.Set("block_reason", result.block_reason);

  std::string json;
  base::JSONWriter::Write(dict, &json);
  profile_prefs_->SetString(prefs::kAllow2CachedCheckResult, json);
  profile_prefs_->SetTime(prefs::kAllow2CachedCheckExpiry,
                          base::Time::FromTimeT(result.expires));
}

std::optional<CheckResult> Allow2Service::LoadCachedCheckResult() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Check if cache has expired
  base::Time expiry = profile_prefs_->GetTime(prefs::kAllow2CachedCheckExpiry);
  if (expiry < base::Time::Now()) {
    return std::nullopt;
  }

  std::string json = profile_prefs_->GetString(prefs::kAllow2CachedCheckResult);
  if (json.empty()) {
    return std::nullopt;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  const base::Value::Dict& dict = parsed->GetDict();

  CheckResult result;
  result.allowed = dict.FindBool("allowed").value_or(true);
  result.remaining_seconds = dict.FindInt("remaining_seconds").value_or(0);
  result.expires = static_cast<int64_t>(dict.FindDouble("expires").value_or(0));
  result.banned = dict.FindBool("banned").value_or(false);

  const std::string* day_type = dict.FindString("day_type");
  if (day_type) {
    result.day_type = *day_type;
  }

  const std::string* block_reason = dict.FindString("block_reason");
  if (block_reason) {
    result.block_reason = *block_reason;
  }

  return result;
}

}  // namespace allow2

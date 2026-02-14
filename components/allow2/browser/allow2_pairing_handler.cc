/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_pairing_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "brave/components/allow2/browser/allow2_api_client.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "brave/components/allow2/common/allow2_utils.h"

namespace allow2 {

Allow2PairingHandler::Allow2PairingHandler(
    Allow2ApiClient* api_client,
    Allow2CredentialManager* credential_manager)
    : api_client_(api_client), credential_manager_(credential_manager) {
  DCHECK(api_client_);
  DCHECK(credential_manager_);
}

Allow2PairingHandler::~Allow2PairingHandler() {
  CancelPairing();
}

void Allow2PairingHandler::AddObserver(Allow2PairingHandlerObserver* observer) {
  observers_.AddObserver(observer);
}

void Allow2PairingHandler::RemoveObserver(
    Allow2PairingHandlerObserver* observer) {
  observers_.RemoveObserver(observer);
}

void Allow2PairingHandler::StartQRPairing(const std::string& device_name) {
  if (IsPairingInProgress()) {
    CancelPairing();
  }

  device_name_ = device_name;
  mode_ = PairingMode::kQRCode;
  SetState(PairingState::kInitializing);

  // Get or generate device token
  std::string device_token = credential_manager_->GetDeviceToken();
  if (device_token.empty()) {
    device_token = GenerateDeviceToken();
    credential_manager_->SetDeviceToken(device_token);
  }
  credential_manager_->SetDeviceName(device_name);

  VLOG(1) << "Allow2: Initializing QR pairing for device: " << device_name;

  api_client_->InitQRPairing(
      device_token, device_name,
      base::BindOnce(&Allow2PairingHandler::OnInitComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Allow2PairingHandler::StartPINPairing(const std::string& device_name) {
  if (IsPairingInProgress()) {
    CancelPairing();
  }

  device_name_ = device_name;
  mode_ = PairingMode::kPINCode;
  SetState(PairingState::kInitializing);

  // Get or generate device token
  std::string device_token = credential_manager_->GetDeviceToken();
  if (device_token.empty()) {
    device_token = GenerateDeviceToken();
    credential_manager_->SetDeviceToken(device_token);
  }
  credential_manager_->SetDeviceName(device_name);

  VLOG(1) << "Allow2: Initializing PIN pairing for device: " << device_name;

  api_client_->InitPINPairing(
      device_token, device_name,
      base::BindOnce(&Allow2PairingHandler::OnInitComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Allow2PairingHandler::CancelPairing() {
  if (!IsPairingInProgress()) {
    return;
  }

  VLOG(1) << "Allow2: Cancelling pairing session";

  // Notify API to cancel session
  if (!session_id_.empty()) {
    api_client_->CancelPairing(session_id_);
  }

  StopPolling();
  ResetSession();
  SetState(PairingState::kIdle);
}

void Allow2PairingHandler::RetryPairing() {
  if (state_ != PairingState::kExpired && state_ != PairingState::kFailed &&
      state_ != PairingState::kDeclined) {
    return;
  }

  // Retry with the same mode and device name
  if (mode_ == PairingMode::kQRCode) {
    StartQRPairing(device_name_);
  } else if (mode_ == PairingMode::kPINCode) {
    StartPINPairing(device_name_);
  }
}

PairingState Allow2PairingHandler::GetState() const {
  return state_;
}

PairingMode Allow2PairingHandler::GetMode() const {
  return mode_;
}

bool Allow2PairingHandler::IsPairingInProgress() const {
  return state_ == PairingState::kInitializing ||
         state_ == PairingState::kWaiting ||
         state_ == PairingState::kScanned ||
         state_ == PairingState::kAuthenticating;
}

std::string Allow2PairingHandler::GetQRCodeUrl() const {
  return qr_code_url_;
}

std::string Allow2PairingHandler::GetWebPairingUrl() const {
  return web_pairing_url_;
}

std::string Allow2PairingHandler::GetPINCode() const {
  return pin_code_;
}

std::string Allow2PairingHandler::GetSessionId() const {
  return session_id_;
}

base::TimeDelta Allow2PairingHandler::GetTimeRemaining() const {
  if (!IsPairingInProgress()) {
    return base::TimeDelta();
  }

  base::TimeDelta elapsed = base::Time::Now() - session_start_time_;
  base::TimeDelta total = base::Seconds(expires_in_seconds_);

  if (elapsed >= total) {
    return base::TimeDelta();
  }

  return total - elapsed;
}

std::string Allow2PairingHandler::GetLastError() const {
  return last_error_;
}

void Allow2PairingHandler::OnInitComplete(const InitPairingResponse& response) {
  if (!response.success) {
    OnPairingFailure(response.error);
    return;
  }

  session_id_ = response.session_id;
  expires_in_seconds_ = response.expires_in;
  session_start_time_ = base::Time::Now();

  if (mode_ == PairingMode::kQRCode) {
    qr_code_url_ = response.qr_code_url;
    web_pairing_url_ = response.web_pairing_url;
    VLOG(1) << "Allow2: QR code ready, session: " << session_id_;

    for (auto& observer : observers_) {
      observer.OnQRCodeReady(qr_code_url_);
    }
  } else {
    pin_code_ = response.pin_code;
    VLOG(1) << "Allow2: PIN code ready, session: " << session_id_;

    for (auto& observer : observers_) {
      observer.OnPINCodeReady(pin_code_);
    }
  }

  SetState(PairingState::kWaiting);

  // Start polling for status
  StartPolling();

  // Start expiry timer
  expiry_timer_.Start(FROM_HERE, base::Seconds(expires_in_seconds_),
                      base::BindOnce(&Allow2PairingHandler::OnExpiryTimer,
                                     weak_ptr_factory_.GetWeakPtr()));
}

void Allow2PairingHandler::OnStatusCheckComplete(
    const PairingStatusResponse& response) {
  if (!IsPairingInProgress()) {
    return;
  }

  if (response.completed) {
    if (response.success) {
      OnPairingSuccess(response);
    } else {
      if (response.error.find("expired") != std::string::npos) {
        SetState(PairingState::kExpired);
        StopPolling();

        for (auto& observer : observers_) {
          observer.OnPairingExpired();
        }
      } else if (response.error.find("declined") != std::string::npos) {
        SetState(PairingState::kDeclined);
        StopPolling();
        NotifyError("Pairing was declined by parent");
      } else {
        OnPairingFailure(response.error);
      }
    }
    return;
  }

  // Still pending - check if QR was scanned
  if (response.scanned && state_ != PairingState::kScanned) {
    SetState(PairingState::kScanned);
    VLOG(1) << "Allow2: QR code was scanned, waiting for authentication";

    for (auto& observer : observers_) {
      observer.OnQRCodeScanned();
    }
  }
}

void Allow2PairingHandler::OnPairingSuccess(
    const PairingStatusResponse& response) {
  VLOG(1) << "Allow2: Pairing completed successfully";

  StopPolling();
  expiry_timer_.Stop();

  // Store credentials
  bool stored = credential_manager_->StoreCredentials(
      response.user_id, response.pair_id, response.pair_token);

  if (!stored) {
    OnPairingFailure("Failed to store credentials");
    return;
  }

  SetState(PairingState::kCompleted);

  // Convert to Child structs
  std::vector<Child> children;
  for (const auto& child : response.children) {
    children.push_back(child);
  }

  for (auto& observer : observers_) {
    observer.OnPairingCompleted(children);
  }
}

void Allow2PairingHandler::OnPairingFailure(const std::string& error) {
  LOG(ERROR) << "Allow2: Pairing failed: " << error;

  StopPolling();
  expiry_timer_.Stop();
  last_error_ = error;
  SetState(PairingState::kFailed);

  NotifyError(error);
}

void Allow2PairingHandler::StartPolling() {
  poll_timer_.Start(
      FROM_HERE, kPollInterval,
      base::BindRepeating(&Allow2PairingHandler::OnPollTimer,
                          weak_ptr_factory_.GetWeakPtr()));
}

void Allow2PairingHandler::StopPolling() {
  poll_timer_.Stop();
}

void Allow2PairingHandler::OnPollTimer() {
  if (!IsPairingInProgress() || session_id_.empty()) {
    StopPolling();
    return;
  }

  api_client_->CheckPairingStatus(
      session_id_,
      base::BindOnce(&Allow2PairingHandler::OnStatusCheckComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Allow2PairingHandler::OnExpiryTimer() {
  if (state_ == PairingState::kWaiting || state_ == PairingState::kScanned) {
    StopPolling();
    SetState(PairingState::kExpired);

    for (auto& observer : observers_) {
      observer.OnPairingExpired();
    }
  }
}

void Allow2PairingHandler::SetState(PairingState state) {
  if (state_ != state) {
    state_ = state;

    for (auto& observer : observers_) {
      observer.OnPairingStateChanged(state);
    }
  }
}

void Allow2PairingHandler::NotifyError(const std::string& error) {
  for (auto& observer : observers_) {
    observer.OnPairingFailed(error);
  }
}

void Allow2PairingHandler::ResetSession() {
  session_id_.clear();
  qr_code_url_.clear();
  web_pairing_url_.clear();
  pin_code_.clear();
  last_error_.clear();
  expires_in_seconds_ = 0;
  session_start_time_ = base::Time();
}

}  // namespace allow2

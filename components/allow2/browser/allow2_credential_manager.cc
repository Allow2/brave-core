/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_credential_manager.h"

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/os_crypt/sync/os_crypt.h"
#include "components/prefs/pref_service.h"

namespace allow2 {

namespace {

constexpr char kCredentialsUserIdKey[] = "userId";
constexpr char kCredentialsPairIdKey[] = "pairId";
constexpr char kCredentialsPairTokenKey[] = "pairToken";

}  // namespace

Allow2CredentialManager::Allow2CredentialManager(PrefService* local_state)
    : local_state_(local_state) {
  DCHECK(local_state_);
}

Allow2CredentialManager::~Allow2CredentialManager() = default;

bool Allow2CredentialManager::StoreCredentials(const std::string& user_id,
                                                const std::string& pair_id,
                                                const std::string& pair_token) {
  return StoreCredentials(Credentials{user_id, pair_id, pair_token});
}

bool Allow2CredentialManager::StoreCredentials(const Credentials& credentials) {
  if (!credentials.IsValid()) {
    LOG(ERROR) << "Allow2: Cannot store invalid credentials";
    return false;
  }

  // Build JSON structure for credentials
  base::Value::Dict creds_dict;
  creds_dict.Set(kCredentialsUserIdKey, credentials.user_id);
  creds_dict.Set(kCredentialsPairIdKey, credentials.pair_id);
  creds_dict.Set(kCredentialsPairTokenKey, credentials.pair_token);

  std::string json;
  if (!base::JSONWriter::Write(creds_dict, &json)) {
    LOG(ERROR) << "Allow2: Failed to serialize credentials to JSON";
    return false;
  }

  std::string encrypted_encoded;
  if (!EncryptAndEncode(json, &encrypted_encoded)) {
    LOG(ERROR) << "Allow2: Failed to encrypt credentials";
    return false;
  }

  local_state_->SetString(prefs::kAllow2Credentials, encrypted_encoded);
  local_state_->SetTime(prefs::kAllow2PairedAt, base::Time::Now());

  VLOG(1) << "Allow2: Credentials stored successfully";
  return true;
}

std::optional<Credentials> Allow2CredentialManager::GetCredentials() const {
  std::string encrypted_encoded =
      local_state_->GetString(prefs::kAllow2Credentials);

  if (encrypted_encoded.empty()) {
    return std::nullopt;
  }

  std::string json;
  if (!DecodeAndDecrypt(encrypted_encoded, &json)) {
    LOG(ERROR) << "Allow2: Failed to decrypt credentials";
    return std::nullopt;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    LOG(ERROR) << "Allow2: Failed to parse credentials JSON";
    return std::nullopt;
  }

  const base::Value::Dict& dict = parsed->GetDict();

  const std::string* user_id = dict.FindString(kCredentialsUserIdKey);
  const std::string* pair_id = dict.FindString(kCredentialsPairIdKey);
  const std::string* pair_token = dict.FindString(kCredentialsPairTokenKey);

  if (!user_id || !pair_id || !pair_token) {
    LOG(ERROR) << "Allow2: Credentials JSON missing required fields";
    return std::nullopt;
  }

  Credentials credentials;
  credentials.user_id = *user_id;
  credentials.pair_id = *pair_id;
  credentials.pair_token = *pair_token;

  return credentials;
}

bool Allow2CredentialManager::HasCredentials() const {
  std::string encrypted_encoded =
      local_state_->GetString(prefs::kAllow2Credentials);
  return !encrypted_encoded.empty();
}

void Allow2CredentialManager::ClearCredentials() {
  // WARNING: This should only be called when the API returns 401,
  // indicating the device was unpaired remotely by the parent.
  LOG(WARNING) << "Allow2: Clearing credentials (device unpaired remotely)";

  local_state_->ClearPref(prefs::kAllow2Credentials);
  local_state_->ClearPref(prefs::kAllow2DeviceToken);
  local_state_->ClearPref(prefs::kAllow2DeviceName);
  local_state_->ClearPref(prefs::kAllow2PairedAt);
}

std::string Allow2CredentialManager::GetDeviceToken() const {
  return local_state_->GetString(prefs::kAllow2DeviceToken);
}

void Allow2CredentialManager::SetDeviceToken(const std::string& token) {
  local_state_->SetString(prefs::kAllow2DeviceToken, token);
}

std::string Allow2CredentialManager::GetDeviceName() const {
  return local_state_->GetString(prefs::kAllow2DeviceName);
}

void Allow2CredentialManager::SetDeviceName(const std::string& name) {
  local_state_->SetString(prefs::kAllow2DeviceName, name);
}

base::Time Allow2CredentialManager::GetPairedAt() const {
  return local_state_->GetTime(prefs::kAllow2PairedAt);
}

bool Allow2CredentialManager::EncryptAndEncode(
    const std::string& plaintext,
    std::string* encoded_ciphertext) const {
  DCHECK(encoded_ciphertext);

  std::string encrypted;
  if (!OSCrypt::EncryptString(plaintext, &encrypted)) {
    return false;
  }

  *encoded_ciphertext = base::Base64Encode(encrypted);
  return true;
}

bool Allow2CredentialManager::DecodeAndDecrypt(
    const std::string& encoded_ciphertext,
    std::string* plaintext) const {
  DCHECK(plaintext);

  std::string encrypted;
  if (!base::Base64Decode(encoded_ciphertext, &encrypted)) {
    return false;
  }

  return OSCrypt::DecryptString(encrypted, plaintext);
}

}  // namespace allow2

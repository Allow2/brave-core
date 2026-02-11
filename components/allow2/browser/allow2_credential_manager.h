/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CREDENTIAL_MANAGER_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CREDENTIAL_MANAGER_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"

class PrefService;

namespace allow2 {

// Credentials for Allow2 API authentication.
// These are sensitive and stored encrypted using OSCrypt.
struct Credentials {
  std::string user_id;
  std::string pair_id;
  std::string pair_token;

  bool IsValid() const {
    return !user_id.empty() && !pair_id.empty() && !pair_token.empty();
  }
};

// Manages secure storage of Allow2 credentials using OSCrypt.
//
// OSCrypt provides platform-specific encryption:
// - macOS: Keychain
// - Windows: DPAPI (Data Protection API)
// - Linux: libsecret/GNOME Keyring/KWallet
//
// SECURITY NOTES:
// - Credentials are stored in local_state (not profile prefs) to prevent
//   accidental exposure through profile sync.
// - The encrypted data is base64-encoded before storage.
// - There is NO method to retrieve credentials in plain text except through
//   GetCredentials() which decrypts them.
// - Device cannot unpair itself - ClearCredentials() is only called when
//   the API returns 401 (meaning parent has unpaired remotely).
class Allow2CredentialManager {
 public:
  explicit Allow2CredentialManager(PrefService* local_state);
  ~Allow2CredentialManager();

  Allow2CredentialManager(const Allow2CredentialManager&) = delete;
  Allow2CredentialManager& operator=(const Allow2CredentialManager&) = delete;

  // Store credentials securely using OSCrypt encryption.
  // Returns true if storage succeeded.
  bool StoreCredentials(const std::string& user_id,
                        const std::string& pair_id,
                        const std::string& pair_token);

  // Store credentials from a Credentials struct.
  bool StoreCredentials(const Credentials& credentials);

  // Retrieve and decrypt stored credentials.
  // Returns std::nullopt if no credentials stored or decryption fails.
  std::optional<Credentials> GetCredentials() const;

  // Check if valid credentials are stored.
  bool HasCredentials() const;

  // Clear stored credentials.
  // WARNING: This should ONLY be called when API returns 401,
  // indicating the device was unpaired remotely by the parent.
  // Users should NOT be able to trigger this directly.
  void ClearCredentials();

  // Get/set the device token (used for pairing).
  std::string GetDeviceToken() const;
  void SetDeviceToken(const std::string& token);

  // Get/set the device name.
  std::string GetDeviceName() const;
  void SetDeviceName(const std::string& name);

  // Get the pairing timestamp.
  base::Time GetPairedAt() const;

 private:
  // Encrypt a string using OSCrypt and encode as base64.
  bool EncryptAndEncode(const std::string& plaintext,
                        std::string* encoded_ciphertext) const;

  // Decode from base64 and decrypt using OSCrypt.
  bool DecodeAndDecrypt(const std::string& encoded_ciphertext,
                        std::string* plaintext) const;

  raw_ptr<PrefService> local_state_;
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CREDENTIAL_MANAGER_H_

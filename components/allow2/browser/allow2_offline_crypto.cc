/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_offline_crypto.h"

#include <cstring>

#include "base/base64.h"
#include "base/check.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "crypto/aead.h"
#include "crypto/hkdf.h"
#include "crypto/hmac.h"
#include "crypto/keypair.h"
#include "crypto/random.h"
#include "crypto/secure_util.h"
#include "crypto/sign.h"

namespace allow2 {

namespace {

// AES-256-GCM constants.
constexpr size_t kAesKeySize = 32;
constexpr size_t kAesNonceSize = 12;
constexpr size_t kAesTagSize = 16;

// Voice code modulus (6 digits: 000000 to 999999).
constexpr int kVoiceCodeModulus = 1000000;

}  // namespace

bool KeyPair::IsValid() const {
  // Check that keys are not all zeros.
  bool public_key_valid = false;
  bool private_key_valid = false;

  for (size_t i = 0; i < kEd25519PublicKeySize; ++i) {
    if (public_key[i] != 0) {
      public_key_valid = true;
      break;
    }
  }

  for (size_t i = 0; i < kEd25519PrivateKeySize; ++i) {
    if (private_key[i] != 0) {
      private_key_valid = true;
      break;
    }
  }

  return public_key_valid && private_key_valid;
}

// static
KeyPair Allow2OfflineCrypto::GenerateKeyPair() {
  KeyPair result;

  // Use Chromium's crypto::keypair to generate an Ed25519 key.
  crypto::keypair::PrivateKey key =
      crypto::keypair::PrivateKey::GenerateEd25519();

  // Extract the 32-byte private key seed.
  std::array<uint8_t, 32> private_key = key.ToEd25519PrivateKey();
  result.private_key = private_key;

  // Extract the 32-byte public key.
  std::array<uint8_t, 32> public_key = key.ToEd25519PublicKey();
  result.public_key = public_key;

  return result;
}

// static
bool Allow2OfflineCrypto::Sign(
    const std::array<uint8_t, kEd25519PrivateKeySize>& private_key,
    const std::vector<uint8_t>& message,
    std::array<uint8_t, kEd25519SignatureSize>* signature) {
  if (!signature) {
    return false;
  }

  // Create a PrivateKey from the 32-byte seed.
  crypto::keypair::PrivateKey key =
      crypto::keypair::PrivateKey::FromEd25519PrivateKey(private_key);

  // Sign the message using Ed25519.
  std::vector<uint8_t> sig =
      crypto::sign::Sign(crypto::sign::ED25519, key, message);

  if (sig.size() != kEd25519SignatureSize) {
    LOG(ERROR) << "Allow2: Ed25519 signature has unexpected size: "
               << sig.size();
    return false;
  }

  std::copy(sig.begin(), sig.end(), signature->begin());
  return true;
}

// static
bool Allow2OfflineCrypto::Sign(const std::vector<uint8_t>& private_key,
                               const std::vector<uint8_t>& message,
                               std::vector<uint8_t>* signature) {
  if (!signature) {
    return false;
  }

  if (private_key.size() != kEd25519PrivateKeySize) {
    LOG(ERROR) << "Allow2: Invalid private key size: " << private_key.size();
    return false;
  }

  std::array<uint8_t, kEd25519PrivateKeySize> key_array;
  std::copy(private_key.begin(), private_key.end(), key_array.begin());

  std::array<uint8_t, kEd25519SignatureSize> sig_array;
  if (!Sign(key_array, message, &sig_array)) {
    return false;
  }

  signature->assign(sig_array.begin(), sig_array.end());
  return true;
}

// static
bool Allow2OfflineCrypto::Verify(
    const std::array<uint8_t, kEd25519PublicKeySize>& public_key,
    const std::vector<uint8_t>& message,
    const std::array<uint8_t, kEd25519SignatureSize>& signature) {
  // Create a PublicKey from the 32-byte key.
  crypto::keypair::PublicKey key =
      crypto::keypair::PublicKey::FromEd25519PublicKey(public_key);

  // Convert signature array to span.
  std::vector<uint8_t> sig_vec(signature.begin(), signature.end());

  // Verify the signature.
  return crypto::sign::Verify(crypto::sign::ED25519, key, message, sig_vec);
}

// static
bool Allow2OfflineCrypto::Verify(const std::vector<uint8_t>& public_key,
                                 const std::vector<uint8_t>& message,
                                 const std::vector<uint8_t>& signature) {
  if (public_key.size() != kEd25519PublicKeySize) {
    LOG(ERROR) << "Allow2: Invalid public key size: " << public_key.size();
    return false;
  }

  if (signature.size() != kEd25519SignatureSize) {
    LOG(ERROR) << "Allow2: Invalid signature size: " << signature.size();
    return false;
  }

  std::array<uint8_t, kEd25519PublicKeySize> key_array;
  std::copy(public_key.begin(), public_key.end(), key_array.begin());

  std::array<uint8_t, kEd25519SignatureSize> sig_array;
  std::copy(signature.begin(), signature.end(), sig_array.begin());

  return Verify(key_array, message, sig_array);
}

// static
std::array<uint8_t, kHmacSha256Size> Allow2OfflineCrypto::HMAC_SHA256(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& message) {
  return crypto::hmac::SignSha256(key, message);
}

// static
std::string Allow2OfflineCrypto::GenerateHMAC(const std::string& data,
                                              const std::string& key) {
  std::vector<uint8_t> key_bytes(key.begin(), key.end());
  std::vector<uint8_t> data_bytes(data.begin(), data.end());

  std::array<uint8_t, kHmacSha256Size> hmac = HMAC_SHA256(key_bytes, data_bytes);
  return std::string(hmac.begin(), hmac.end());
}

// static
bool Allow2OfflineCrypto::VerifyHMAC(const std::string& data,
                                     const std::string& key,
                                     const std::string& expected_hmac) {
  std::string computed = GenerateHMAC(data, key);
  return ConstantTimeCompare(computed, expected_hmac);
}

// static
std::vector<uint8_t> Allow2OfflineCrypto::DeriveKey(
    const std::vector<uint8_t>& secret,
    const std::vector<uint8_t>& salt,
    const std::string& info,
    size_t length) {
  std::vector<uint8_t> info_bytes(info.begin(), info.end());

  std::string derived = crypto::HkdfSha256(
      std::string_view(reinterpret_cast<const char*>(secret.data()),
                       secret.size()),
      std::string_view(reinterpret_cast<const char*>(salt.data()), salt.size()),
      std::string_view(info.data(), info.size()), length);

  return std::vector<uint8_t>(derived.begin(), derived.end());
}

// static
std::vector<uint8_t> Allow2OfflineCrypto::DeriveKey(
    const std::vector<uint8_t>& secret,
    const std::string& info,
    size_t length) {
  return DeriveKey(secret, std::vector<uint8_t>(), info, length);
}

// static
std::string Allow2OfflineCrypto::DeriveOfflineKey(const std::string& pair_token,
                                                  const std::string& salt) {
  std::vector<uint8_t> secret(pair_token.begin(), pair_token.end());
  std::vector<uint8_t> salt_bytes(salt.begin(), salt.end());
  std::vector<uint8_t> derived =
      DeriveKey(secret, salt_bytes, "allow2-offline-key", kAesKeySize);
  return std::string(derived.begin(), derived.end());
}

// static
int64_t Allow2OfflineCrypto::GetCurrentTimeBucket() {
  base::Time now = base::Time::Now();
  return GetTimeBucket(now.InSecondsFSinceUnixEpoch());
}

// static
int64_t Allow2OfflineCrypto::GetTimeBucket(int64_t unix_seconds) {
  return unix_seconds / kTimeBucketSeconds;
}

// static
bool Allow2OfflineCrypto::IsTimeBucketValid(int64_t bucket,
                                            int tolerance_buckets) {
  int64_t current_bucket = GetCurrentTimeBucket();
  int64_t diff =
      (bucket > current_bucket) ? (bucket - current_bucket)
                                : (current_bucket - bucket);
  return diff <= static_cast<int64_t>(tolerance_buckets);
}

// static
std::string Allow2OfflineCrypto::GenerateVoiceCode(
    const std::vector<uint8_t>& shared_secret,
    const std::string& child_id) {
  return GenerateVoiceCode(shared_secret, child_id, GetCurrentTimeBucket());
}

// static
std::string Allow2OfflineCrypto::GenerateVoiceCode(
    const std::vector<uint8_t>& shared_secret,
    const std::string& child_id,
    int64_t time_bucket) {
  // Create message: child_id + time_bucket as bytes.
  std::vector<uint8_t> message;
  message.insert(message.end(), child_id.begin(), child_id.end());

  // Append time bucket as big-endian 8 bytes.
  for (int i = 7; i >= 0; --i) {
    message.push_back(static_cast<uint8_t>((time_bucket >> (i * 8)) & 0xFF));
  }

  // Compute HMAC-SHA256.
  std::array<uint8_t, kHmacSha256Size> hmac = HMAC_SHA256(shared_secret, message);

  // Extract 6-digit code from first 4 bytes of HMAC.
  // Use dynamic truncation similar to HOTP (RFC 4226).
  uint32_t offset = hmac[kHmacSha256Size - 1] & 0x0F;
  uint32_t code = (static_cast<uint32_t>(hmac[offset] & 0x7F) << 24) |
                  (static_cast<uint32_t>(hmac[offset + 1]) << 16) |
                  (static_cast<uint32_t>(hmac[offset + 2]) << 8) |
                  static_cast<uint32_t>(hmac[offset + 3]);

  code = code % kVoiceCodeModulus;

  // Format as 6-digit zero-padded string.
  return base::StringPrintf("%06d", code);
}

// static
bool Allow2OfflineCrypto::VerifyVoiceCode(
    const std::vector<uint8_t>& shared_secret,
    const std::string& child_id,
    const std::string& code,
    int tolerance_buckets) {
  int64_t current_bucket = GetCurrentTimeBucket();

  // Check code against current bucket and tolerance window.
  for (int i = -tolerance_buckets; i <= tolerance_buckets; ++i) {
    std::string expected =
        GenerateVoiceCode(shared_secret, child_id, current_bucket + i);
    if (ConstantTimeCompare(code, expected)) {
      return true;
    }
  }

  return false;
}

// static
std::string Allow2OfflineCrypto::EncryptForStorage(
    const std::string& plaintext,
    const std::string& device_key) {
  // Derive a 32-byte key from device_key using HKDF.
  std::vector<uint8_t> key_bytes =
      DeriveKey(std::vector<uint8_t>(device_key.begin(), device_key.end()),
                "allow2-storage-encryption", kAesKeySize);

  // Generate random nonce.
  std::vector<uint8_t> nonce = crypto::RandBytesAsVector(kAesNonceSize);

  // Encrypt using AES-256-GCM.
  std::vector<uint8_t> plaintext_bytes(plaintext.begin(), plaintext.end());
  std::optional<std::vector<uint8_t>> ciphertext =
      AESEncrypt(plaintext_bytes, key_bytes, nonce);

  if (!ciphertext.has_value()) {
    LOG(ERROR) << "Allow2: AES encryption failed";
    return "";
  }

  // Combine nonce + ciphertext and encode as base64.
  std::vector<uint8_t> combined;
  combined.insert(combined.end(), nonce.begin(), nonce.end());
  combined.insert(combined.end(), ciphertext->begin(), ciphertext->end());

  return base::Base64Encode(combined);
}

// static
std::optional<std::string> Allow2OfflineCrypto::DecryptFromStorage(
    const std::string& ciphertext,
    const std::string& device_key) {
  // Decode from base64.
  std::string decoded;
  if (!base::Base64Decode(ciphertext, &decoded)) {
    LOG(ERROR) << "Allow2: Base64 decode failed";
    return std::nullopt;
  }

  // Minimum size: nonce (12) + tag (16).
  if (decoded.size() < kAesNonceSize + kAesTagSize) {
    LOG(ERROR) << "Allow2: Ciphertext too short";
    return std::nullopt;
  }

  // Extract nonce and ciphertext.
  std::vector<uint8_t> nonce(decoded.begin(),
                              decoded.begin() + kAesNonceSize);
  std::vector<uint8_t> encrypted(decoded.begin() + kAesNonceSize, decoded.end());

  // Derive key.
  std::vector<uint8_t> key_bytes =
      DeriveKey(std::vector<uint8_t>(device_key.begin(), device_key.end()),
                "allow2-storage-encryption", kAesKeySize);

  // Decrypt.
  std::optional<std::vector<uint8_t>> plaintext =
      AESDecrypt(encrypted, key_bytes, nonce);

  if (!plaintext.has_value()) {
    LOG(ERROR) << "Allow2: AES decryption failed";
    return std::nullopt;
  }

  return std::string(plaintext->begin(), plaintext->end());
}

// static
std::string Allow2OfflineCrypto::GenerateSalt(size_t length) {
  std::vector<uint8_t> salt = crypto::RandBytesAsVector(length);
  return std::string(salt.begin(), salt.end());
}

// static
bool Allow2OfflineCrypto::ConstantTimeCompare(const std::string& a,
                                              const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }

  return crypto::SecureMemEqual(
      base::as_byte_span(a),
      base::as_byte_span(b));
}

// static
std::optional<std::vector<uint8_t>> Allow2OfflineCrypto::AESEncrypt(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& nonce) {
  if (key.size() != kAesKeySize) {
    LOG(ERROR) << "Allow2: Invalid AES key size: " << key.size();
    return std::nullopt;
  }

  if (nonce.size() != kAesNonceSize) {
    LOG(ERROR) << "Allow2: Invalid AES nonce size: " << nonce.size();
    return std::nullopt;
  }

  crypto::Aead aead(crypto::Aead::AES_256_GCM);
  aead.Init(key);

  // Seal returns std::vector<uint8_t> directly.
  std::vector<uint8_t> ciphertext =
      aead.Seal(plaintext, nonce, std::vector<uint8_t>());

  // Check if encryption succeeded (non-empty result).
  if (ciphertext.empty() && !plaintext.empty()) {
    return std::nullopt;
  }

  return ciphertext;
}

// static
std::optional<std::vector<uint8_t>> Allow2OfflineCrypto::AESDecrypt(
    const std::vector<uint8_t>& ciphertext_with_tag,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& nonce) {
  if (key.size() != kAesKeySize) {
    LOG(ERROR) << "Allow2: Invalid AES key size: " << key.size();
    return std::nullopt;
  }

  if (nonce.size() != kAesNonceSize) {
    LOG(ERROR) << "Allow2: Invalid AES nonce size: " << nonce.size();
    return std::nullopt;
  }

  if (ciphertext_with_tag.size() < kAesTagSize) {
    LOG(ERROR) << "Allow2: Ciphertext too short for tag";
    return std::nullopt;
  }

  crypto::Aead aead(crypto::Aead::AES_256_GCM);
  aead.Init(key);

  // Open returns std::optional<std::vector<uint8_t>> directly.
  return aead.Open(ciphertext_with_tag, nonce, std::vector<uint8_t>());
}

}  // namespace allow2

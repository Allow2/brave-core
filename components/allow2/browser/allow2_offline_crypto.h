/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CRYPTO_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CRYPTO_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace allow2 {

// Size constants for Ed25519 keys.
inline constexpr size_t kEd25519PublicKeySize = 32;
inline constexpr size_t kEd25519PrivateKeySize = 32;  // Seed only (RFC 8032)
inline constexpr size_t kEd25519SignatureSize = 64;
inline constexpr size_t kHmacSha256Size = 32;

// Time bucket duration for voice code validation (15 minutes in seconds).
inline constexpr int64_t kTimeBucketSeconds = 15 * 60;

// Ed25519 key pair for signing offline grants.
// The private key is the 32-byte seed (RFC 8032 format), not the
// 64-byte expanded key used internally by some implementations.
struct KeyPair {
  std::array<uint8_t, kEd25519PublicKeySize> public_key;
  std::array<uint8_t, kEd25519PrivateKeySize> private_key;

  bool IsValid() const;
};

// Manages cryptographic operations for offline authorization.
//
// This class provides:
// - Ed25519 signing/verification for offline grants
// - HMAC-SHA256 for voice code generation
// - HKDF-SHA256 for key derivation
// - AES-256-GCM for secure storage encryption
// - Time-based voice code generation/verification
//
// SECURITY NOTES:
// - Ed25519 keys should be stored securely using OSCrypt (see
//   Allow2CredentialManager for the pattern).
// - Voice codes use time-based validation with 15-minute windows.
// - All cryptographic operations use Chromium's //crypto library which
//   wraps BoringSSL.
class Allow2OfflineCrypto {
 public:
  Allow2OfflineCrypto() = delete;
  ~Allow2OfflineCrypto() = delete;

  Allow2OfflineCrypto(const Allow2OfflineCrypto&) = delete;
  Allow2OfflineCrypto& operator=(const Allow2OfflineCrypto&) = delete;

  // =========================================================================
  // Ed25519 Key Operations
  // =========================================================================

  // Generate a new Ed25519 key pair.
  // The private key is the 32-byte seed, the public key is 32 bytes.
  static KeyPair GenerateKeyPair();

  // Sign a message using Ed25519.
  // |private_key| must be a 32-byte Ed25519 seed.
  // |message| is the data to sign.
  // |signature| receives the 64-byte signature on success.
  // Returns true on success, false if the key is invalid.
  static bool Sign(
      const std::array<uint8_t, kEd25519PrivateKeySize>& private_key,
      const std::vector<uint8_t>& message,
      std::array<uint8_t, kEd25519SignatureSize>* signature);

  // Convenience overload using std::vector for all parameters.
  static bool Sign(const std::vector<uint8_t>& private_key,
                   const std::vector<uint8_t>& message,
                   std::vector<uint8_t>* signature);

  // Verify an Ed25519 signature.
  // |public_key| must be a 32-byte Ed25519 public key.
  // |message| is the signed data.
  // |signature| is the 64-byte signature to verify.
  // Returns true if the signature is valid, false otherwise.
  static bool Verify(
      const std::array<uint8_t, kEd25519PublicKeySize>& public_key,
      const std::vector<uint8_t>& message,
      const std::array<uint8_t, kEd25519SignatureSize>& signature);

  // Convenience overload using std::vector for all parameters.
  static bool Verify(const std::vector<uint8_t>& public_key,
                     const std::vector<uint8_t>& message,
                     const std::vector<uint8_t>& signature);

  // =========================================================================
  // HMAC Operations
  // =========================================================================

  // Compute HMAC-SHA256.
  // Used for generating voice codes.
  // |key| is the HMAC key.
  // |message| is the data to authenticate.
  // Returns the 32-byte HMAC.
  static std::array<uint8_t, kHmacSha256Size> HMAC_SHA256(
      const std::vector<uint8_t>& key,
      const std::vector<uint8_t>& message);

  // String-based HMAC for backward compatibility.
  static std::string GenerateHMAC(const std::string& data,
                                  const std::string& key);

  // Verify HMAC with constant-time comparison.
  static bool VerifyHMAC(const std::string& data,
                         const std::string& key,
                         const std::string& expected_hmac);

  // =========================================================================
  // Key Derivation
  // =========================================================================

  // Derive a key using HKDF-SHA256.
  // |secret| is the input keying material.
  // |salt| is the optional salt (can be empty for default salt).
  // |info| is the context/application-specific info.
  // |length| is the desired output key length.
  // Returns the derived key.
  static std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& secret,
                                        const std::vector<uint8_t>& salt,
                                        const std::string& info,
                                        size_t length);

  // Convenience overload with empty salt.
  static std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& secret,
                                        const std::string& info,
                                        size_t length);

  // String-based key derivation for backward compatibility.
  static std::string DeriveOfflineKey(const std::string& pair_token,
                                      const std::string& salt);

  // =========================================================================
  // Time Bucket Operations (for voice codes)
  // =========================================================================

  // Get the current time bucket for voice code validation.
  // Time is divided into 15-minute buckets since Unix epoch.
  static int64_t GetCurrentTimeBucket();

  // Get the time bucket for a specific Unix timestamp.
  static int64_t GetTimeBucket(int64_t unix_seconds);

  // Check if a time bucket is valid within tolerance.
  // |bucket| is the time bucket to check.
  // |tolerance_buckets| is the number of buckets to allow on either side
  //   (default 1 allows current + previous + next = 45 minutes total).
  // Returns true if the bucket is within the valid range.
  static bool IsTimeBucketValid(int64_t bucket, int tolerance_buckets = 1);

  // =========================================================================
  // Voice Code Operations
  // =========================================================================

  // Generate a 6-digit voice code for the current time bucket.
  // |shared_secret| is the pre-shared key between parent app and browser.
  // |child_id| is the child identifier for scoping.
  // Returns the 6-digit code as a string (zero-padded).
  static std::string GenerateVoiceCode(
      const std::vector<uint8_t>& shared_secret,
      const std::string& child_id);

  // Generate a voice code for a specific time bucket.
  static std::string GenerateVoiceCode(
      const std::vector<uint8_t>& shared_secret,
      const std::string& child_id,
      int64_t time_bucket);

  // Verify a voice code with tolerance.
  // |shared_secret| is the pre-shared key.
  // |child_id| is the child identifier.
  // |code| is the 6-digit code to verify.
  // |tolerance_buckets| allows for clock skew (default 1).
  // Returns true if the code is valid within the tolerance window.
  static bool VerifyVoiceCode(const std::vector<uint8_t>& shared_secret,
                              const std::string& child_id,
                              const std::string& code,
                              int tolerance_buckets = 1);

  // =========================================================================
  // Storage Encryption (AES-256-GCM)
  // =========================================================================

  // Encrypt data for offline storage using device-bound key.
  // Uses AES-256-GCM with random IV and HMAC for integrity.
  static std::string EncryptForStorage(const std::string& plaintext,
                                       const std::string& device_key);

  // Decrypt data from offline storage.
  // Returns std::nullopt if decryption or verification fails.
  static std::optional<std::string> DecryptFromStorage(
      const std::string& ciphertext,
      const std::string& device_key);

  // =========================================================================
  // Utility Functions
  // =========================================================================

  // Generate a secure random salt.
  static std::string GenerateSalt(size_t length = 16);

  // Constant-time comparison to prevent timing attacks.
  static bool ConstantTimeCompare(const std::string& a, const std::string& b);

 private:
  // Internal AES-256-GCM encryption.
  static std::optional<std::vector<uint8_t>> AESEncrypt(
      const std::vector<uint8_t>& plaintext,
      const std::vector<uint8_t>& key,
      const std::vector<uint8_t>& nonce);

  // Internal AES-256-GCM decryption.
  static std::optional<std::vector<uint8_t>> AESDecrypt(
      const std::vector<uint8_t>& ciphertext_with_tag,
      const std::vector<uint8_t>& key,
      const std::vector<uint8_t>& nonce);
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CRYPTO_H_

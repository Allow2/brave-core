/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_QR_TOKEN_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_QR_TOKEN_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"

namespace allow2 {

// Grant types supported by QR tokens.
// These allow parents to grant time extensions or modify restrictions
// by scanning a QR code generated in the parent app.
enum class QRGrantType {
  kExtension,  // Grant additional quota minutes to an activity
  kQuota,      // Add to the base quota for an activity
  kEarlier,    // Allow starting an activity earlier than scheduled
  kLiftBan,    // Temporarily lift a ban on an activity
};

// Represents a parsed and validated QR grant.
// Contains all the information needed to apply a time grant
// from a scanned QR code.
struct QRGrant {
  QRGrant();
  ~QRGrant();
  QRGrant(const QRGrant&);
  QRGrant& operator=(const QRGrant&);
  QRGrant(QRGrant&&);
  QRGrant& operator=(QRGrant&&);

  // Type of grant being applied
  QRGrantType type = QRGrantType::kExtension;

  // Target child ID this grant applies to
  uint64_t child_id = 0;

  // Activity ID this grant affects (e.g., internet, gaming, social)
  uint8_t activity_id = 0;

  // Number of minutes granted
  uint16_t minutes = 0;

  // When this grant was issued
  base::Time issued_at;

  // When this grant expires (must be used before this time)
  base::Time expires_at;

  // One-time nonce to prevent replay attacks
  std::string nonce;

  // Device ID this grant is valid for (empty = any device)
  std::string device_id;

  // Key ID from header (identifies which parent key signed this)
  std::string key_id;

  // Check if this grant has expired.
  // Returns true if the current time is past expires_at.
  bool IsExpired() const;

  // Check if this grant is valid for a specific device.
  // Returns true if device_id is empty (valid for any device)
  // or matches the provided device ID.
  bool IsValidForDevice(const std::string& current_device_id) const;

  // Check if this grant is valid for a specific child.
  // Returns true if child_id matches the provided child ID.
  bool IsValidForChild(uint64_t current_child_id) const;

  // Get a human-readable description of the grant type.
  std::string GetTypeDescription() const;
};

// Manages parsing, verification, and generation of Allow2 QR tokens.
//
// QR Token Structure (JSON, base64url encoded):
// {
//   "header": { "alg": "Ed25519", "kid": "p1-parent500" },
//   "grant": {
//     "type": "extension|quota|earlier|lift_ban",
//     "childId": 1001,
//     "activityId": 3,
//     "minutes": 30,
//     "issuedAt": "2026-02-17T10:00:00Z",
//     "expiresAt": "2026-02-17T23:59:59Z",
//     "nonce": "x7k9m2",
//     "deviceId": "brave-abc123"
//   },
//   "signature": "base64url..."
// }
//
// The token is structured as: header.payload.signature
// where each part is base64url encoded.
//
// SECURITY NOTES:
// - Only Ed25519 signatures are supported for forward security
// - Tokens include nonces to prevent replay attacks
// - Device-specific tokens are validated against current device ID
// - Expiry times are enforced strictly
// - Public keys must be obtained from a trusted source (Allow2 API)
class Allow2QRToken {
 public:
  // Ed25519 key size constants
  static constexpr size_t kEd25519PublicKeySize = 32;
  static constexpr size_t kEd25519PrivateKeySize = 32;
  static constexpr size_t kEd25519SignatureSize = 64;

  // Grant limits
  static constexpr uint16_t kMaxGrantMinutes = 480;  // 8 hours max
  static constexpr int kMaxValidityHours = 24;       // Valid for 24h max

  // Parse and verify a QR token from a scanned string.
  //
  // The token should be in the format: header.payload.signature
  // where each part is base64url encoded.
  //
  // |token| - The scanned QR code content
  // |parent_public_key| - The parent's Ed25519 public key (32 bytes)
  //
  // Returns the parsed QRGrant if verification succeeds, or std::nullopt if:
  // - Token format is invalid
  // - JSON parsing fails
  // - Signature verification fails
  // - Token has expired
  //
  // IMPORTANT: Even if parsing succeeds, callers should still verify:
  // - grant.IsValidForDevice(current_device_id)
  // - grant.IsValidForChild(current_child_id)
  // - The nonce has not been used before
  static std::optional<QRGrant> ParseAndVerify(
      const std::string& token,
      const std::vector<uint8_t>& parent_public_key);

  // Overload accepting a fixed-size array for the public key.
  static std::optional<QRGrant> ParseAndVerify(
      const std::string& token,
      const std::array<uint8_t, kEd25519PublicKeySize>& parent_public_key);

  // Generate a QR token (primarily for testing).
  //
  // In production, QR tokens are generated by the parent's Allow2 app.
  // This method is provided for testing and for cases where the
  // Brave browser needs to act as a granter (e.g., parent's browser).
  //
  // |grant| - The grant details to encode
  // |private_key| - The Ed25519 private key to sign with (32 bytes)
  // |key_id| - The key identifier (e.g., "p1-parent500")
  //
  // Returns the base64url encoded token in format: header.payload.signature
  static std::string Generate(
      const QRGrant& grant,
      const std::vector<uint8_t>& private_key,
      const std::string& key_id);

  // Overload accepting a fixed-size array for the private key.
  static std::string Generate(
      const QRGrant& grant,
      const std::array<uint8_t, kEd25519PrivateKeySize>& private_key,
      const std::string& key_id);

  // Check if a nonce has been used (for replay protection).
  // |nonce| - The nonce to check
  // |used_nonces| - Vector of previously used nonces
  //
  // Returns true if the nonce has already been used.
  static bool IsNonceUsed(const std::string& nonce,
                          const std::vector<std::string>& used_nonces);

 private:
  // Parse the JSON payload into a QRGrant structure.
  // Returns std::nullopt if required fields are missing or invalid.
  static std::optional<QRGrant> ParsePayload(const std::string& json);

  // Parse the header JSON to extract the key ID.
  // Returns std::nullopt if header is invalid or algorithm is not Ed25519.
  static std::optional<std::string> ParseHeader(const std::string& json);

  // Verify an Ed25519 signature.
  //
  // |header_payload_b64| - The base64url encoded "header.payload" string
  // |signature_b64| - The base64url encoded signature
  // |public_key| - The Ed25519 public key (32 bytes)
  //
  // Returns true if the signature is valid.
  static bool VerifySignature(
      const std::string& header_payload_b64,
      const std::string& signature_b64,
      const std::vector<uint8_t>& public_key);

  // Sign data with an Ed25519 private key.
  //
  // |data| - The data to sign
  // |private_key| - The Ed25519 private key (32 bytes)
  //
  // Returns the 64-byte signature.
  static std::vector<uint8_t> SignData(
      const std::string& data,
      const std::vector<uint8_t>& private_key);

  // Convert QRGrantType to/from string representation.
  static std::string GrantTypeToString(QRGrantType type);
  static std::optional<QRGrantType> StringToGrantType(const std::string& str);

  // Parse ISO 8601 timestamp string to base::Time.
  static std::optional<base::Time> ParseIso8601(const std::string& timestamp);

  // Format base::Time as ISO 8601 timestamp string.
  static std::string FormatIso8601(base::Time time);
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_QR_TOKEN_H_

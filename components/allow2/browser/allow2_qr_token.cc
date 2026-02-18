/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_qr_token.h"

#include <algorithm>
#include <cmath>

#include "base/base64url.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"

namespace allow2 {

namespace {

// JSON field names for header
constexpr char kHeaderAlgorithmKey[] = "alg";
constexpr char kHeaderKeyIdKey[] = "kid";
constexpr char kEd25519Algorithm[] = "Ed25519";

// JSON field names for grant payload
constexpr char kGrantTypeKey[] = "type";
constexpr char kGrantChildIdKey[] = "childId";
constexpr char kGrantActivityIdKey[] = "activityId";
constexpr char kGrantMinutesKey[] = "minutes";
constexpr char kGrantIssuedAtKey[] = "issuedAt";
constexpr char kGrantExpiresAtKey[] = "expiresAt";
constexpr char kGrantNonceKey[] = "nonce";
constexpr char kGrantDeviceIdKey[] = "deviceId";

// Grant type strings
constexpr char kGrantTypeExtension[] = "extension";
constexpr char kGrantTypeQuota[] = "quota";
constexpr char kGrantTypeEarlier[] = "earlier";
constexpr char kGrantTypeLiftBan[] = "lift_ban";

}  // namespace

// QRGrant implementation

QRGrant::QRGrant() = default;
QRGrant::~QRGrant() = default;
QRGrant::QRGrant(const QRGrant&) = default;
QRGrant& QRGrant::operator=(const QRGrant&) = default;
QRGrant::QRGrant(QRGrant&&) = default;
QRGrant& QRGrant::operator=(QRGrant&&) = default;

bool QRGrant::IsExpired() const {
  return base::Time::Now() > expires_at;
}

bool QRGrant::IsValidForDevice(const std::string& current_device_id) const {
  // Empty device_id means the grant is valid for any device
  if (device_id.empty()) {
    return true;
  }
  return device_id == current_device_id;
}

bool QRGrant::IsValidForChild(uint64_t current_child_id) const {
  return child_id == current_child_id;
}

std::string QRGrant::GetTypeDescription() const {
  switch (type) {
    case QRGrantType::kExtension:
      return "Time Extension";
    case QRGrantType::kQuota:
      return "Quota Addition";
    case QRGrantType::kEarlier:
      return "Start Earlier";
    case QRGrantType::kLiftBan:
      return "Lift Ban";
  }
  return "Unknown";
}

// Allow2QRToken implementation

std::optional<QRGrant> Allow2QRToken::ParseAndVerify(
    const std::string& token,
    const std::vector<uint8_t>& parent_public_key) {
  // Validate public key size
  if (parent_public_key.size() != kEd25519PublicKeySize) {
    LOG(ERROR) << "Allow2QRToken: Invalid public key size: "
               << parent_public_key.size();
    return std::nullopt;
  }

  // Split token into parts: header.payload.signature
  std::vector<std::string> parts = base::SplitString(
      token, ".", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  if (parts.size() != 3) {
    LOG(ERROR) << "Allow2QRToken: Invalid token format, expected 3 parts, got "
               << parts.size();
    return std::nullopt;
  }

  const std::string& header_b64 = parts[0];
  const std::string& payload_b64 = parts[1];
  const std::string& signature_b64 = parts[2];

  // Verify signature first (before parsing payload)
  std::string header_payload = header_b64 + "." + payload_b64;
  if (!VerifySignature(header_payload, signature_b64, parent_public_key)) {
    LOG(ERROR) << "Allow2QRToken: Signature verification failed";
    return std::nullopt;
  }

  // Decode header
  std::string header_json;
  if (!base::Base64UrlDecode(header_b64,
                              base::Base64UrlDecodePolicy::IGNORE_PADDING,
                              &header_json)) {
    LOG(ERROR) << "Allow2QRToken: Failed to decode header";
    return std::nullopt;
  }

  // Parse header and extract key_id
  std::optional<std::string> key_id = ParseHeader(header_json);
  if (!key_id) {
    LOG(ERROR) << "Allow2QRToken: Failed to parse header";
    return std::nullopt;
  }

  // Decode payload
  std::string payload_json;
  if (!base::Base64UrlDecode(payload_b64,
                              base::Base64UrlDecodePolicy::IGNORE_PADDING,
                              &payload_json)) {
    LOG(ERROR) << "Allow2QRToken: Failed to decode payload";
    return std::nullopt;
  }

  // Parse payload
  std::optional<QRGrant> grant = ParsePayload(payload_json);
  if (!grant) {
    LOG(ERROR) << "Allow2QRToken: Failed to parse payload";
    return std::nullopt;
  }

  // Set key_id from header
  grant->key_id = *key_id;

  // Check expiration
  if (grant->IsExpired()) {
    LOG(WARNING) << "Allow2QRToken: Token has expired";
    return std::nullopt;
  }

  // Validate grant limits
  if (grant->minutes > kMaxGrantMinutes) {
    LOG(WARNING) << "Allow2QRToken: Grant exceeds maximum minutes: "
                 << grant->minutes;
    return std::nullopt;
  }

  // Check validity duration
  base::TimeDelta validity = grant->expires_at - grant->issued_at;
  if (validity > base::Hours(kMaxValidityHours)) {
    LOG(WARNING) << "Allow2QRToken: Token validity exceeds maximum duration";
    return std::nullopt;
  }

  return grant;
}

std::optional<QRGrant> Allow2QRToken::ParseAndVerify(
    const std::string& token,
    const std::array<uint8_t, kEd25519PublicKeySize>& parent_public_key) {
  std::vector<uint8_t> key_vec(parent_public_key.begin(),
                                parent_public_key.end());
  return ParseAndVerify(token, key_vec);
}

std::string Allow2QRToken::Generate(
    const QRGrant& grant,
    const std::vector<uint8_t>& private_key,
    const std::string& key_id) {
  // Validate private key size
  if (private_key.size() != kEd25519PrivateKeySize) {
    LOG(ERROR) << "Allow2QRToken: Invalid private key size";
    return std::string();
  }

  // Build header JSON
  base::Value::Dict header_dict;
  header_dict.Set(kHeaderAlgorithmKey, kEd25519Algorithm);
  header_dict.Set(kHeaderKeyIdKey, key_id);

  std::string header_json;
  if (!base::JSONWriter::Write(header_dict, &header_json)) {
    LOG(ERROR) << "Allow2QRToken: Failed to serialize header";
    return std::string();
  }

  // Build payload JSON
  base::Value::Dict payload_dict;
  payload_dict.Set(kGrantTypeKey, GrantTypeToString(grant.type));
  payload_dict.Set(kGrantChildIdKey, static_cast<double>(grant.child_id));
  payload_dict.Set(kGrantActivityIdKey, static_cast<int>(grant.activity_id));
  payload_dict.Set(kGrantMinutesKey, static_cast<int>(grant.minutes));
  payload_dict.Set(kGrantIssuedAtKey, FormatIso8601(grant.issued_at));
  payload_dict.Set(kGrantExpiresAtKey, FormatIso8601(grant.expires_at));
  payload_dict.Set(kGrantNonceKey, grant.nonce);
  if (!grant.device_id.empty()) {
    payload_dict.Set(kGrantDeviceIdKey, grant.device_id);
  }

  std::string payload_json;
  if (!base::JSONWriter::Write(payload_dict, &payload_json)) {
    LOG(ERROR) << "Allow2QRToken: Failed to serialize payload";
    return std::string();
  }

  // Base64url encode header and payload
  std::string header_b64;
  base::Base64UrlEncode(header_json, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &header_b64);

  std::string payload_b64;
  base::Base64UrlEncode(payload_json, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &payload_b64);

  // Create data to sign: header.payload
  std::string data_to_sign = header_b64 + "." + payload_b64;

  // Sign the data
  std::vector<uint8_t> signature = SignData(data_to_sign, private_key);
  if (signature.empty()) {
    LOG(ERROR) << "Allow2QRToken: Failed to sign token";
    return std::string();
  }

  // Base64url encode signature
  std::string signature_b64;
  base::Base64UrlEncode(
      base::span<const uint8_t>(signature),
      base::Base64UrlEncodePolicy::OMIT_PADDING,
      &signature_b64);

  // Combine: header.payload.signature
  return data_to_sign + "." + signature_b64;
}

std::string Allow2QRToken::Generate(
    const QRGrant& grant,
    const std::array<uint8_t, kEd25519PrivateKeySize>& private_key,
    const std::string& key_id) {
  std::vector<uint8_t> key_vec(private_key.begin(), private_key.end());
  return Generate(grant, key_vec, key_id);
}

bool Allow2QRToken::IsNonceUsed(const std::string& nonce,
                                 const std::vector<std::string>& used_nonces) {
  return std::find(used_nonces.begin(), used_nonces.end(), nonce) !=
         used_nonces.end();
}

std::optional<QRGrant> Allow2QRToken::ParsePayload(const std::string& json) {
  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  const base::Value::Dict& dict = parsed->GetDict();

  QRGrant grant;

  // Parse required fields
  const std::string* type_str = dict.FindString(kGrantTypeKey);
  if (!type_str) {
    LOG(ERROR) << "Allow2QRToken: Missing grant type";
    return std::nullopt;
  }

  std::optional<QRGrantType> grant_type = StringToGrantType(*type_str);
  if (!grant_type) {
    LOG(ERROR) << "Allow2QRToken: Invalid grant type: " << *type_str;
    return std::nullopt;
  }
  grant.type = *grant_type;

  // Parse childId (could be integer or double)
  std::optional<double> child_id_d = dict.FindDouble(kGrantChildIdKey);
  if (!child_id_d) {
    std::optional<int> child_id_i = dict.FindInt(kGrantChildIdKey);
    if (!child_id_i) {
      LOG(ERROR) << "Allow2QRToken: Missing or invalid childId";
      return std::nullopt;
    }
    grant.child_id = static_cast<uint64_t>(*child_id_i);
  } else {
    grant.child_id = static_cast<uint64_t>(*child_id_d);
  }

  // Parse activityId
  std::optional<int> activity_id = dict.FindInt(kGrantActivityIdKey);
  if (!activity_id) {
    LOG(ERROR) << "Allow2QRToken: Missing or invalid activityId";
    return std::nullopt;
  }
  grant.activity_id = static_cast<uint8_t>(*activity_id);

  // Parse minutes
  std::optional<int> minutes = dict.FindInt(kGrantMinutesKey);
  if (!minutes) {
    LOG(ERROR) << "Allow2QRToken: Missing or invalid minutes";
    return std::nullopt;
  }
  grant.minutes = static_cast<uint16_t>(*minutes);

  // Parse issuedAt
  const std::string* issued_at_str = dict.FindString(kGrantIssuedAtKey);
  if (!issued_at_str) {
    LOG(ERROR) << "Allow2QRToken: Missing issuedAt";
    return std::nullopt;
  }
  std::optional<base::Time> issued_at = ParseIso8601(*issued_at_str);
  if (!issued_at) {
    LOG(ERROR) << "Allow2QRToken: Invalid issuedAt format";
    return std::nullopt;
  }
  grant.issued_at = *issued_at;

  // Parse expiresAt
  const std::string* expires_at_str = dict.FindString(kGrantExpiresAtKey);
  if (!expires_at_str) {
    LOG(ERROR) << "Allow2QRToken: Missing expiresAt";
    return std::nullopt;
  }
  std::optional<base::Time> expires_at = ParseIso8601(*expires_at_str);
  if (!expires_at) {
    LOG(ERROR) << "Allow2QRToken: Invalid expiresAt format";
    return std::nullopt;
  }
  grant.expires_at = *expires_at;

  // Parse nonce
  const std::string* nonce = dict.FindString(kGrantNonceKey);
  if (!nonce || nonce->empty()) {
    LOG(ERROR) << "Allow2QRToken: Missing or empty nonce";
    return std::nullopt;
  }
  grant.nonce = *nonce;

  // Parse optional deviceId
  const std::string* device_id = dict.FindString(kGrantDeviceIdKey);
  if (device_id) {
    grant.device_id = *device_id;
  }

  return grant;
}

std::optional<std::string> Allow2QRToken::ParseHeader(const std::string& json) {
  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  const base::Value::Dict& dict = parsed->GetDict();

  // Verify algorithm is Ed25519
  const std::string* alg = dict.FindString(kHeaderAlgorithmKey);
  if (!alg || *alg != kEd25519Algorithm) {
    LOG(ERROR) << "Allow2QRToken: Unsupported algorithm: "
               << (alg ? *alg : "(none)");
    return std::nullopt;
  }

  // Extract key ID
  const std::string* kid = dict.FindString(kHeaderKeyIdKey);
  if (!kid || kid->empty()) {
    LOG(ERROR) << "Allow2QRToken: Missing key ID";
    return std::nullopt;
  }

  return *kid;
}

bool Allow2QRToken::VerifySignature(
    const std::string& header_payload_b64,
    const std::string& signature_b64,
    const std::vector<uint8_t>& public_key) {
  // Decode signature
  std::optional<std::vector<uint8_t>> signature =
      base::Base64UrlDecode(signature_b64,
                            base::Base64UrlDecodePolicy::IGNORE_PADDING);
  if (!signature || signature->size() != kEd25519SignatureSize) {
    LOG(ERROR) << "Allow2QRToken: Invalid signature encoding or size";
    return false;
  }

  // Create public key object
  std::array<uint8_t, 32> pub_key_array;
  std::copy_n(public_key.begin(), 32, pub_key_array.begin());
  crypto::keypair::PublicKey ed_pub_key =
      crypto::keypair::PublicKey::FromEd25519PublicKey(pub_key_array);

  // Convert data to bytes
  std::vector<uint8_t> data_bytes(header_payload_b64.begin(),
                                   header_payload_b64.end());

  // Verify signature using crypto::sign
  return crypto::sign::Verify(crypto::sign::ED25519, ed_pub_key, data_bytes,
                               *signature);
}

std::vector<uint8_t> Allow2QRToken::SignData(
    const std::string& data,
    const std::vector<uint8_t>& private_key) {
  // Create private key object
  std::array<uint8_t, 32> priv_key_array;
  std::copy_n(private_key.begin(), 32, priv_key_array.begin());
  crypto::keypair::PrivateKey ed_priv_key =
      crypto::keypair::PrivateKey::FromEd25519PrivateKey(priv_key_array);

  // Convert data to bytes
  std::vector<uint8_t> data_bytes(data.begin(), data.end());

  // Sign using crypto::sign
  return crypto::sign::Sign(crypto::sign::ED25519, ed_priv_key, data_bytes);
}

std::string Allow2QRToken::GrantTypeToString(QRGrantType type) {
  switch (type) {
    case QRGrantType::kExtension:
      return kGrantTypeExtension;
    case QRGrantType::kQuota:
      return kGrantTypeQuota;
    case QRGrantType::kEarlier:
      return kGrantTypeEarlier;
    case QRGrantType::kLiftBan:
      return kGrantTypeLiftBan;
  }
  return kGrantTypeExtension;
}

std::optional<QRGrantType> Allow2QRToken::StringToGrantType(
    const std::string& str) {
  if (str == kGrantTypeExtension) {
    return QRGrantType::kExtension;
  }
  if (str == kGrantTypeQuota) {
    return QRGrantType::kQuota;
  }
  if (str == kGrantTypeEarlier) {
    return QRGrantType::kEarlier;
  }
  if (str == kGrantTypeLiftBan) {
    return QRGrantType::kLiftBan;
  }
  return std::nullopt;
}

std::optional<base::Time> Allow2QRToken::ParseIso8601(
    const std::string& timestamp) {
  // Parse ISO 8601 format: "2026-02-17T10:00:00Z"
  // Format: YYYY-MM-DDTHH:MM:SSZ

  if (timestamp.length() < 20) {
    return std::nullopt;
  }

  // Check format markers
  if (timestamp[4] != '-' || timestamp[7] != '-' || timestamp[10] != 'T' ||
      timestamp[13] != ':' || timestamp[16] != ':') {
    return std::nullopt;
  }

  // Check for Z suffix (UTC)
  if (timestamp.back() != 'Z') {
    return std::nullopt;
  }

  // Parse components
  int year, month, day, hour, minute, second;
  if (sscanf(timestamp.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day,
             &hour, &minute, &second) != 6) {
    return std::nullopt;
  }

  // Validate ranges
  if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    return std::nullopt;
  }

  // Use base::Time::Exploded to construct time
  base::Time::Exploded exploded = {};
  exploded.year = year;
  exploded.month = month;
  exploded.day_of_month = day;
  exploded.hour = hour;
  exploded.minute = minute;
  exploded.second = second;

  base::Time time;
  if (!base::Time::FromUTCExploded(exploded, &time)) {
    return std::nullopt;
  }

  return time;
}

std::string Allow2QRToken::FormatIso8601(base::Time time) {
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           exploded.year, exploded.month, exploded.day_of_month, exploded.hour,
           exploded.minute, exploded.second);

  return std::string(buffer);
}

}  // namespace allow2

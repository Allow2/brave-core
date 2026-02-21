/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_voice_code.h"

#include <algorithm>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "brave/components/allow2/browser/allow2_offline_crypto.h"
#include "crypto/random.h"

namespace allow2 {

namespace {

// Validate that a character is a valid digit (0-9).
bool IsDigit(char c) {
  return c >= '0' && c <= '9';
}

// Parse a single digit character to its numeric value.
int ParseDigit(char c) {
  return c - '0';
}

// Format a number as a zero-padded string of the specified width.
std::string FormatPadded(int value, int width) {
  return base::StringPrintf("%0*d", width, value);
}

}  // namespace

// ============================================================================
// VoiceCodeRequest Implementation
// ============================================================================

std::string VoiceCodeRequest::ToString() const {
  // Format: T A MM NN
  // T: Request type (0-9)
  // A: Activity ID (0-9)
  // MM: Minutes / 5 (00-99)
  // NN: Nonce (00-99)
  uint8_t mm = Allow2VoiceCode::MinutesToIncrements(minutes);

  return FormatPadded(static_cast<int>(type), 1) +
         FormatPadded(activity_id, 1) +
         FormatPadded(mm, 2) +
         FormatPadded(nonce, 2);
}

// static
std::optional<VoiceCodeRequest> VoiceCodeRequest::Parse(
    const std::string& code) {
  // Remove any spaces or dashes for flexible input
  std::string normalized;
  for (char c : code) {
    if (c != ' ' && c != '-') {
      normalized += c;
    }
  }

  // Must be exactly 6 digits
  if (normalized.size() != 6) {
    VLOG(1) << "Allow2VoiceCode: Invalid code length: " << normalized.size();
    return std::nullopt;
  }

  // All characters must be digits
  for (char c : normalized) {
    if (!IsDigit(c)) {
      VLOG(1) << "Allow2VoiceCode: Invalid character in code";
      return std::nullopt;
    }
  }

  // Parse components
  int t = ParseDigit(normalized[0]);
  int a = ParseDigit(normalized[1]);
  int mm = ParseDigit(normalized[2]) * 10 + ParseDigit(normalized[3]);
  int nn = ParseDigit(normalized[4]) * 10 + ParseDigit(normalized[5]);

  // Validate ranges
  if (t > 9 || a > 9 || mm > 99 || nn > 99) {
    VLOG(1) << "Allow2VoiceCode: Value out of range";
    return std::nullopt;
  }

  VoiceCodeRequest request;
  request.type = static_cast<VoiceCodeRequestType>(t);
  request.activity_id = static_cast<uint8_t>(a);
  request.minutes = Allow2VoiceCode::IncrementsToMinutes(static_cast<uint8_t>(mm));
  request.nonce = static_cast<uint8_t>(nn);

  return request;
}

bool VoiceCodeRequest::operator==(const VoiceCodeRequest& other) const {
  return type == other.type &&
         activity_id == other.activity_id &&
         minutes == other.minutes &&
         nonce == other.nonce;
}

// ============================================================================
// Allow2VoiceCode Implementation
// ============================================================================

// static
std::string Allow2VoiceCode::GenerateRequestCode(VoiceCodeRequestType type,
                                                   uint8_t activity_id,
                                                   uint16_t minutes) {
  // Clamp values to valid ranges
  if (activity_id > 9) {
    VLOG(1) << "Allow2VoiceCode: Activity ID clamped from " << activity_id << " to 9";
    activity_id = 9;
  }

  if (minutes > kMaxMinutes) {
    VLOG(1) << "Allow2VoiceCode: Minutes clamped from " << minutes << " to " << kMaxMinutes;
    minutes = kMaxMinutes;
  }

  VoiceCodeRequest request;
  request.type = type;
  request.activity_id = activity_id;
  request.minutes = minutes;
  request.nonce = GenerateNonce();

  return request.ToString();
}

// static
std::optional<VoiceCodeRequest> Allow2VoiceCode::ParseRequestCode(
    const std::string& code) {
  return VoiceCodeRequest::Parse(code);
}

// static
std::string Allow2VoiceCode::GenerateApprovalCode(
    const std::vector<uint8_t>& key,
    const std::vector<std::string>& request_codes) {
  if (key.empty() || request_codes.empty()) {
    LOG(WARNING) << "Allow2VoiceCode: Cannot generate approval with empty key or codes";
    return "";
  }

  int64_t time_bucket = GetCurrentTimeBucket();
  return ComputeApprovalHMAC(key, request_codes, time_bucket);
}

// static
bool Allow2VoiceCode::ValidateApprovalCode(
    const std::vector<uint8_t>& key,
    const std::vector<std::string>& request_codes,
    const std::string& approval_code) {
  if (key.empty() || request_codes.empty() || approval_code.empty()) {
    VLOG(1) << "Allow2VoiceCode: Validation failed - empty parameters";
    return false;
  }

  // Normalize the approval code (remove spaces/dashes)
  std::string normalized;
  for (char c : approval_code) {
    if (c != ' ' && c != '-') {
      normalized += c;
    }
  }

  // Must be 6 digits
  if (normalized.size() != 6) {
    VLOG(1) << "Allow2VoiceCode: Invalid approval code length";
    return false;
  }

  int64_t current_bucket = GetCurrentTimeBucket();

  // Check current time bucket and adjacent buckets (+-1) for timing tolerance
  for (int64_t offset = -1; offset <= 1; ++offset) {
    std::string expected = ComputeApprovalHMAC(key, request_codes,
                                                current_bucket + offset);
    if (Allow2OfflineCrypto::ConstantTimeCompare(normalized, expected)) {
      VLOG(1) << "Allow2VoiceCode: Approval validated at bucket offset " << offset;
      return true;
    }
  }

  VLOG(1) << "Allow2VoiceCode: Approval code did not match any time bucket";
  return false;
}

// static
uint8_t Allow2VoiceCode::MinutesToIncrements(uint16_t minutes) {
  // Round up to nearest 5-minute increment
  uint16_t increments = (minutes + 4) / 5;
  return static_cast<uint8_t>(std::min<uint16_t>(increments, 99));
}

// static
uint16_t Allow2VoiceCode::IncrementsToMinutes(uint8_t increments) {
  return static_cast<uint16_t>(increments) * 5;
}

// static
uint8_t Allow2VoiceCode::GenerateNonce() {
  uint8_t random_bytes[1];
  crypto::RandBytes(random_bytes);
  // Map 0-255 to 0-99
  return random_bytes[0] % 100;
}

// static
std::string Allow2VoiceCode::ComputeApprovalHMAC(
    const std::vector<uint8_t>& key,
    const std::vector<std::string>& request_codes,
    int64_t time_bucket) {
  // Build the data to sign: sorted request codes + time bucket
  std::vector<std::string> sorted_codes = request_codes;
  std::sort(sorted_codes.begin(), sorted_codes.end());

  std::string data;
  for (const auto& code : sorted_codes) {
    data += code;
    data += "|";  // Delimiter
  }
  data += base::NumberToString(time_bucket);

  // Convert key to string for HMAC function
  std::string key_str(key.begin(), key.end());

  // Generate HMAC
  std::string hmac = Allow2OfflineCrypto::GenerateHMAC(data, key_str);

  if (hmac.empty()) {
    LOG(WARNING) << "Allow2VoiceCode: HMAC generation failed";
    return "";
  }

  // Convert HMAC bytes to 6-digit numeric code
  // Use first 4 bytes of HMAC as a 32-bit integer, then mod 10^6
  if (hmac.size() < 4) {
    LOG(WARNING) << "Allow2VoiceCode: HMAC too short";
    return "";
  }

  uint32_t value = (static_cast<uint8_t>(hmac[0]) << 24) |
                   (static_cast<uint8_t>(hmac[1]) << 16) |
                   (static_cast<uint8_t>(hmac[2]) << 8) |
                   (static_cast<uint8_t>(hmac[3]));

  // Mod by 10^6 to get 6 digits
  uint32_t code_value = value % 1000000;

  return FormatPadded(code_value, 6);
}

// static
int64_t Allow2VoiceCode::GetCurrentTimeBucket() {
  base::Time now = base::Time::Now();
  int64_t seconds_since_epoch = (now - base::Time::UnixEpoch()).InSeconds();
  return seconds_since_epoch / kTimeBucketSeconds;
}

}  // namespace allow2

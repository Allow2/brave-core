/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_VOICE_CODE_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_VOICE_CODE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace allow2 {

// Request types for voice codes.
// These map to the first digit (T) of the 6-digit voice code format: T A MM NN
enum class VoiceCodeRequestType : uint8_t {
  kQuota = 0,       // Request more quota minutes for an activity
  kExtend = 1,      // Extend usage past current time block
  kEarlier = 2,     // Start activity earlier than scheduled
  kLiftBan = 3,     // Lift a ban on an activity
  kReserved4 = 4,   // Reserved for future use
  kReserved5 = 5,   // Reserved for future use
  kReserved6 = 6,   // Reserved for future use
  kMultiCode1 = 7,  // Multi-code sequence part 1
  kMultiCode2 = 8,  // Multi-code sequence part 2
  kMultiCode3 = 9,  // Multi-code sequence part 3
};

// Parsed voice code request.
// Voice codes use a 6-digit format: T A MM NN
// - T (1 digit): Request type (0-9)
// - A (1 digit): Activity ID (0-9)
// - MM (2 digits): Amount in 5-minute increments (00-99 = 0-495 minutes)
// - NN (2 digits): Nonce for anti-replay protection (00-99)
struct VoiceCodeRequest {
  VoiceCodeRequestType type;
  uint8_t activity_id;    // Activity ID (0-9)
  uint16_t minutes;       // Actual minutes (MM * 5)
  uint8_t nonce;          // Anti-replay nonce (00-99)

  // Convert request to 6-digit string format.
  std::string ToString() const;

  // Parse a 6-digit voice code string into a request.
  // Returns std::nullopt if the code is invalid.
  static std::optional<VoiceCodeRequest> Parse(const std::string& code);

  // Equality comparison for testing.
  bool operator==(const VoiceCodeRequest& other) const;
};

// Voice code manager for offline parent-child communication.
//
// Voice codes allow parents to grant permissions verbally when the child's
// device is offline. The workflow is:
// 1. Child's device displays a REQUEST code (6 digits)
// 2. Child reads the code to parent over phone
// 3. Parent's app generates an APPROVAL code (6 digits)
// 4. Parent reads the approval code to child
// 5. Child enters the approval code on their device
//
// SECURITY:
// - Request codes include a nonce to prevent replay attacks
// - Approval codes are HMAC-based using a shared symmetric key
// - Time buckets (30-second windows) allow for slight timing drift
// - Adjacent time buckets are checked for tolerance
//
// ANTI-REPLAY:
// - Each request includes a random nonce (00-99)
// - The nonce is included in HMAC computation
// - Device tracks used nonces with timestamps
// - Nonces expire after a configurable period
class Allow2VoiceCode {
 public:
  Allow2VoiceCode() = delete;
  ~Allow2VoiceCode() = delete;

  // ============================================================================
  // Request Code Generation
  // ============================================================================

  // Generate a request code from the specified parameters.
  // |type|: Type of request (quota, extend, earlier, lift_ban)
  // |activity_id|: Activity ID (0-9)
  // |minutes|: Requested minutes (will be rounded to nearest 5-minute increment)
  // Returns: 6-digit request code string
  static std::string GenerateRequestCode(VoiceCodeRequestType type,
                                          uint8_t activity_id,
                                          uint16_t minutes);

  // Parse a request code into its components.
  // |code|: 6-digit request code string
  // Returns: Parsed VoiceCodeRequest or nullopt if invalid
  static std::optional<VoiceCodeRequest> ParseRequestCode(
      const std::string& code);

  // ============================================================================
  // Approval Code Generation (Parent's Device)
  // ============================================================================

  // Generate an approval code for one or more request codes.
  // This is called on the parent's device.
  // |key|: Derived symmetric key for this parent-device pair
  // |request_codes|: One or more request codes being approved
  // Returns: 6-digit approval code string
  static std::string GenerateApprovalCode(
      const std::vector<uint8_t>& key,
      const std::vector<std::string>& request_codes);

  // ============================================================================
  // Approval Code Validation (Child's Device)
  // ============================================================================

  // Validate an approval code against request codes.
  // This is called on the child's device.
  // |key|: Derived symmetric key for this parent-device pair
  // |request_codes|: The request code(s) that were approved
  // |approval_code|: The approval code entered by the child
  // Returns: true if the approval code is valid for the given requests
  //
  // SECURITY: Checks current time bucket and adjacent buckets (+-1) to
  // tolerate slight timing drift between devices.
  static bool ValidateApprovalCode(
      const std::vector<uint8_t>& key,
      const std::vector<std::string>& request_codes,
      const std::string& approval_code);

  // ============================================================================
  // Utility Functions
  // ============================================================================

  // Convert minutes to 5-minute increments (MM value).
  // Returns the MM value (0-99) representing up to 495 minutes.
  static uint8_t MinutesToIncrements(uint16_t minutes);

  // Convert 5-minute increments (MM value) to actual minutes.
  static uint16_t IncrementsToMinutes(uint8_t increments);

  // Get the maximum requestable minutes (99 * 5 = 495).
  static constexpr uint16_t kMaxMinutes = 495;

  // Get the time bucket duration in seconds (30 seconds).
  static constexpr int64_t kTimeBucketSeconds = 30;

 private:
  // Generate a random nonce (00-99).
  static uint8_t GenerateNonce();

  // Compute HMAC for approval code.
  // |key|: Symmetric key for HMAC
  // |request_codes|: Request codes being approved
  // |time_bucket|: Unix timestamp divided by kTimeBucketSeconds
  // Returns: 6-digit approval code derived from HMAC
  static std::string ComputeApprovalHMAC(
      const std::vector<uint8_t>& key,
      const std::vector<std::string>& request_codes,
      int64_t time_bucket);

  // Get the current time bucket (Unix timestamp / kTimeBucketSeconds).
  static int64_t GetCurrentTimeBucket();
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_VOICE_CODE_H_

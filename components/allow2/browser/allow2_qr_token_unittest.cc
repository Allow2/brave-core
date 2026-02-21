/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_qr_token.h"

#include <array>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "brave/components/allow2/browser/allow2_offline_crypto.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

class Allow2QRTokenTest : public testing::Test {
 protected:
  void SetUp() override {
    // Generate a test keypair.
    auto keypair = Allow2OfflineCrypto::GenerateKeyPair();
    test_private_key_.assign(keypair.private_key.begin(),
                              keypair.private_key.end());
    test_public_key_.assign(keypair.public_key.begin(),
                             keypair.public_key.end());
  }

  void TearDown() override {}

  // Create a test grant with reasonable defaults.
  QRGrant CreateTestGrant() {
    QRGrant grant;
    grant.type = QRGrantType::kExtension;
    grant.child_id = 1001;
    grant.activity_id = 3;  // Gaming
    grant.minutes = 30;
    grant.issued_at = base::Time::Now();
    grant.expires_at = base::Time::Now() + base::Hours(1);
    grant.nonce = "test-nonce-123";
    grant.device_id = "brave-device-456";
    return grant;
  }

  std::vector<uint8_t> test_private_key_;
  std::vector<uint8_t> test_public_key_;
};

// Test QRGrant struct defaults.
TEST_F(Allow2QRTokenTest, QRGrant_Defaults) {
  QRGrant grant;
  EXPECT_EQ(QRGrantType::kExtension, grant.type);
  EXPECT_EQ(0u, grant.child_id);
  EXPECT_EQ(0u, grant.activity_id);
  EXPECT_EQ(0u, grant.minutes);
  EXPECT_TRUE(grant.nonce.empty());
  EXPECT_TRUE(grant.device_id.empty());
}

// Test QRGrant::IsExpired.
TEST_F(Allow2QRTokenTest, QRGrant_IsExpired) {
  QRGrant grant;

  // Set to past time - should be expired.
  grant.expires_at = base::Time::Now() - base::Hours(1);
  EXPECT_TRUE(grant.IsExpired());

  // Set to future time - should not be expired.
  grant.expires_at = base::Time::Now() + base::Hours(1);
  EXPECT_FALSE(grant.IsExpired());
}

// Test QRGrant::IsValidForDevice.
TEST_F(Allow2QRTokenTest, QRGrant_IsValidForDevice) {
  QRGrant grant;

  // Empty device_id means valid for any device.
  grant.device_id = "";
  EXPECT_TRUE(grant.IsValidForDevice("any-device"));
  EXPECT_TRUE(grant.IsValidForDevice(""));

  // Specific device_id must match.
  grant.device_id = "device-123";
  EXPECT_TRUE(grant.IsValidForDevice("device-123"));
  EXPECT_FALSE(grant.IsValidForDevice("device-456"));
  EXPECT_FALSE(grant.IsValidForDevice(""));
}

// Test QRGrant::IsValidForChild.
TEST_F(Allow2QRTokenTest, QRGrant_IsValidForChild) {
  QRGrant grant;
  grant.child_id = 1001;

  EXPECT_TRUE(grant.IsValidForChild(1001));
  EXPECT_FALSE(grant.IsValidForChild(1002));
  EXPECT_FALSE(grant.IsValidForChild(0));
}

// Test QRGrant::GetTypeDescription.
TEST_F(Allow2QRTokenTest, QRGrant_GetTypeDescription) {
  QRGrant grant;

  grant.type = QRGrantType::kExtension;
  EXPECT_EQ("extension", grant.GetTypeDescription());

  grant.type = QRGrantType::kQuota;
  EXPECT_EQ("quota", grant.GetTypeDescription());

  grant.type = QRGrantType::kEarlier;
  EXPECT_EQ("earlier", grant.GetTypeDescription());

  grant.type = QRGrantType::kLiftBan;
  EXPECT_EQ("lift_ban", grant.GetTypeDescription());
}

// Test token generation and parsing roundtrip.
TEST_F(Allow2QRTokenTest, GenerateAndParse_Roundtrip) {
  auto original = CreateTestGrant();
  std::string key_id = "test-key-1";

  // Generate token.
  std::string token = Allow2QRToken::Generate(original, test_private_key_, key_id);
  EXPECT_FALSE(token.empty());

  // Token should have three parts separated by dots.
  size_t dot1 = token.find('.');
  size_t dot2 = token.find('.', dot1 + 1);
  EXPECT_NE(std::string::npos, dot1);
  EXPECT_NE(std::string::npos, dot2);

  // Parse and verify.
  auto parsed = Allow2QRToken::ParseAndVerify(token, test_public_key_);
  ASSERT_TRUE(parsed.has_value());

  // Verify all fields match.
  EXPECT_EQ(original.type, parsed->type);
  EXPECT_EQ(original.child_id, parsed->child_id);
  EXPECT_EQ(original.activity_id, parsed->activity_id);
  EXPECT_EQ(original.minutes, parsed->minutes);
  EXPECT_EQ(original.nonce, parsed->nonce);
  EXPECT_EQ(original.device_id, parsed->device_id);
  EXPECT_EQ(key_id, parsed->key_id);
}

// Test parsing fails with invalid token format.
TEST_F(Allow2QRTokenTest, Parse_InvalidFormat) {
  auto result = Allow2QRToken::ParseAndVerify("not-a-valid-token",
                                               test_public_key_);
  EXPECT_FALSE(result.has_value());
}

// Test parsing fails with missing parts.
TEST_F(Allow2QRTokenTest, Parse_MissingParts) {
  // Only one part.
  auto result1 = Allow2QRToken::ParseAndVerify("singlepart", test_public_key_);
  EXPECT_FALSE(result1.has_value());

  // Only two parts.
  auto result2 = Allow2QRToken::ParseAndVerify("part1.part2", test_public_key_);
  EXPECT_FALSE(result2.has_value());
}

// Test parsing fails with wrong signature.
TEST_F(Allow2QRTokenTest, Parse_WrongSignature) {
  auto grant = CreateTestGrant();
  std::string token = Allow2QRToken::Generate(grant, test_private_key_, "key1");

  // Create different keypair.
  auto other_keypair = Allow2OfflineCrypto::GenerateKeyPair();
  std::vector<uint8_t> other_public(other_keypair.public_key.begin(),
                                     other_keypair.public_key.end());

  // Verification should fail with wrong public key.
  auto result = Allow2QRToken::ParseAndVerify(token, other_public);
  EXPECT_FALSE(result.has_value());
}

// Test parsing fails with corrupted signature.
TEST_F(Allow2QRTokenTest, Parse_CorruptedSignature) {
  auto grant = CreateTestGrant();
  std::string token = Allow2QRToken::Generate(grant, test_private_key_, "key1");

  // Find last dot and corrupt signature part.
  size_t last_dot = token.rfind('.');
  ASSERT_NE(std::string::npos, last_dot);

  std::string corrupted = token;
  if (last_dot + 1 < corrupted.size()) {
    corrupted[last_dot + 1] = 'X';  // Corrupt first char of signature.
  }

  auto result = Allow2QRToken::ParseAndVerify(corrupted, test_public_key_);
  EXPECT_FALSE(result.has_value());
}

// Test parsing expired token (should still parse, caller checks expiry).
TEST_F(Allow2QRTokenTest, Parse_ExpiredToken) {
  auto grant = CreateTestGrant();
  grant.expires_at = base::Time::Now() - base::Hours(1);  // Already expired.

  std::string token = Allow2QRToken::Generate(grant, test_private_key_, "key1");

  // Token should parse (signature valid).
  auto result = Allow2QRToken::ParseAndVerify(token, test_public_key_);
  ASSERT_TRUE(result.has_value());

  // But IsExpired should return true.
  EXPECT_TRUE(result->IsExpired());
}

// Test different grant types.
TEST_F(Allow2QRTokenTest, Generate_AllGrantTypes) {
  std::vector<QRGrantType> types = {
      QRGrantType::kExtension,
      QRGrantType::kQuota,
      QRGrantType::kEarlier,
      QRGrantType::kLiftBan,
  };

  for (auto type : types) {
    auto grant = CreateTestGrant();
    grant.type = type;

    std::string token = Allow2QRToken::Generate(grant, test_private_key_, "key");
    auto parsed = Allow2QRToken::ParseAndVerify(token, test_public_key_);

    ASSERT_TRUE(parsed.has_value()) << "Failed for type: "
                                     << static_cast<int>(type);
    EXPECT_EQ(type, parsed->type);
  }
}

// Test maximum grant minutes validation.
TEST_F(Allow2QRTokenTest, Generate_MaxMinutes) {
  auto grant = CreateTestGrant();
  grant.minutes = Allow2QRToken::kMaxGrantMinutes;

  std::string token = Allow2QRToken::Generate(grant, test_private_key_, "key");
  auto parsed = Allow2QRToken::ParseAndVerify(token, test_public_key_);

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(Allow2QRToken::kMaxGrantMinutes, parsed->minutes);
}

// Test nonce usage tracking.
TEST_F(Allow2QRTokenTest, IsNonceUsed) {
  std::vector<std::string> used_nonces = {"nonce1", "nonce2", "nonce3"};

  EXPECT_TRUE(Allow2QRToken::IsNonceUsed("nonce1", used_nonces));
  EXPECT_TRUE(Allow2QRToken::IsNonceUsed("nonce2", used_nonces));
  EXPECT_TRUE(Allow2QRToken::IsNonceUsed("nonce3", used_nonces));
  EXPECT_FALSE(Allow2QRToken::IsNonceUsed("nonce4", used_nonces));
  EXPECT_FALSE(Allow2QRToken::IsNonceUsed("", used_nonces));
}

// Test empty used nonces list.
TEST_F(Allow2QRTokenTest, IsNonceUsed_EmptyList) {
  std::vector<std::string> empty_nonces;

  EXPECT_FALSE(Allow2QRToken::IsNonceUsed("any-nonce", empty_nonces));
}

// Test using array version of public key.
TEST_F(Allow2QRTokenTest, ParseAndVerify_ArrayPublicKey) {
  auto grant = CreateTestGrant();
  std::string token = Allow2QRToken::Generate(grant, test_private_key_, "key1");

  // Use array version.
  std::array<uint8_t, Allow2QRToken::kEd25519PublicKeySize> pub_key_array;
  std::copy(test_public_key_.begin(), test_public_key_.end(),
            pub_key_array.begin());

  auto result = Allow2QRToken::ParseAndVerify(token, pub_key_array);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(grant.child_id, result->child_id);
}

// Test token with empty device ID (valid for any device).
TEST_F(Allow2QRTokenTest, Parse_EmptyDeviceId) {
  auto grant = CreateTestGrant();
  grant.device_id = "";  // Valid for any device.

  std::string token = Allow2QRToken::Generate(grant, test_private_key_, "key1");
  auto parsed = Allow2QRToken::ParseAndVerify(token, test_public_key_);

  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed->device_id.empty());
  EXPECT_TRUE(parsed->IsValidForDevice("any-device-id"));
}

// Test QRGrant copy and move constructors.
TEST_F(Allow2QRTokenTest, QRGrant_CopyMove) {
  QRGrant original = CreateTestGrant();

  // Test copy.
  QRGrant copy = original;
  EXPECT_EQ(original.type, copy.type);
  EXPECT_EQ(original.child_id, copy.child_id);
  EXPECT_EQ(original.minutes, copy.minutes);
  EXPECT_EQ(original.nonce, copy.nonce);

  // Test move.
  std::string original_nonce = original.nonce;
  QRGrant moved = std::move(original);
  EXPECT_EQ(original_nonce, moved.nonce);
}

}  // namespace allow2

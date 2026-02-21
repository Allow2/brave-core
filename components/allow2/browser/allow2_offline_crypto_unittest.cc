/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_offline_crypto.h"

#include <array>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

class Allow2OfflineCryptoTest : public testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Test Ed25519 key pair generation.
TEST_F(Allow2OfflineCryptoTest, GenerateKeyPair) {
  auto keypair = Allow2OfflineCrypto::GenerateKeyPair();

  // Verify key sizes.
  EXPECT_EQ(Allow2OfflineCrypto::kEd25519PrivateKeySize,
            keypair.private_key.size());
  EXPECT_EQ(Allow2OfflineCrypto::kEd25519PublicKeySize,
            keypair.public_key.size());

  // Keys should not be all zeros.
  bool private_all_zero = true;
  bool public_all_zero = true;
  for (size_t i = 0; i < keypair.private_key.size(); ++i) {
    if (keypair.private_key[i] != 0) {
      private_all_zero = false;
    }
  }
  for (size_t i = 0; i < keypair.public_key.size(); ++i) {
    if (keypair.public_key[i] != 0) {
      public_all_zero = false;
    }
  }
  EXPECT_FALSE(private_all_zero);
  EXPECT_FALSE(public_all_zero);
}

// Test that different key pairs are generated each time.
TEST_F(Allow2OfflineCryptoTest, GenerateKeyPair_Unique) {
  auto keypair1 = Allow2OfflineCrypto::GenerateKeyPair();
  auto keypair2 = Allow2OfflineCrypto::GenerateKeyPair();

  // Public keys should differ.
  EXPECT_NE(keypair1.public_key, keypair2.public_key);

  // Private keys should differ.
  EXPECT_NE(keypair1.private_key, keypair2.private_key);
}

// Test Ed25519 sign and verify.
TEST_F(Allow2OfflineCryptoTest, SignAndVerify) {
  auto keypair = Allow2OfflineCrypto::GenerateKeyPair();

  std::string message = "Hello, Allow2 World!";
  std::vector<uint8_t> message_bytes(message.begin(), message.end());

  std::array<uint8_t, Allow2OfflineCrypto::kEd25519SignatureSize> signature;
  bool sign_result = Allow2OfflineCrypto::Sign(
      keypair.private_key, message_bytes, &signature);
  EXPECT_TRUE(sign_result);

  // Verify the signature.
  bool verify_result = Allow2OfflineCrypto::Verify(
      keypair.public_key, message_bytes, signature);
  EXPECT_TRUE(verify_result);
}

// Test that verification fails with wrong message.
TEST_F(Allow2OfflineCryptoTest, VerifyFails_WrongMessage) {
  auto keypair = Allow2OfflineCrypto::GenerateKeyPair();

  std::string message = "Original message";
  std::vector<uint8_t> message_bytes(message.begin(), message.end());

  std::array<uint8_t, Allow2OfflineCrypto::kEd25519SignatureSize> signature;
  Allow2OfflineCrypto::Sign(keypair.private_key, message_bytes, &signature);

  // Try to verify with different message.
  std::string wrong_message = "Different message";
  std::vector<uint8_t> wrong_bytes(wrong_message.begin(), wrong_message.end());

  bool verify_result = Allow2OfflineCrypto::Verify(
      keypair.public_key, wrong_bytes, signature);
  EXPECT_FALSE(verify_result);
}

// Test that verification fails with wrong key.
TEST_F(Allow2OfflineCryptoTest, VerifyFails_WrongKey) {
  auto keypair1 = Allow2OfflineCrypto::GenerateKeyPair();
  auto keypair2 = Allow2OfflineCrypto::GenerateKeyPair();

  std::string message = "Test message";
  std::vector<uint8_t> message_bytes(message.begin(), message.end());

  std::array<uint8_t, Allow2OfflineCrypto::kEd25519SignatureSize> signature;
  Allow2OfflineCrypto::Sign(keypair1.private_key, message_bytes, &signature);

  // Try to verify with different public key.
  bool verify_result = Allow2OfflineCrypto::Verify(
      keypair2.public_key, message_bytes, signature);
  EXPECT_FALSE(verify_result);
}

// Test HMAC-SHA256.
TEST_F(Allow2OfflineCryptoTest, HMAC_SHA256) {
  std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                               0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  std::string message = "Test message for HMAC";
  std::vector<uint8_t> message_bytes(message.begin(), message.end());

  auto hmac = Allow2OfflineCrypto::HMAC_SHA256(key, message_bytes);

  // HMAC should be 32 bytes.
  EXPECT_EQ(32u, hmac.size());

  // Same inputs should produce same output.
  auto hmac2 = Allow2OfflineCrypto::HMAC_SHA256(key, message_bytes);
  EXPECT_EQ(hmac, hmac2);
}

// Test that HMAC differs with different keys.
TEST_F(Allow2OfflineCryptoTest, HMAC_DifferentKeys) {
  std::vector<uint8_t> key1 = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> key2 = {0x05, 0x06, 0x07, 0x08};
  std::string message = "Test message";
  std::vector<uint8_t> message_bytes(message.begin(), message.end());

  auto hmac1 = Allow2OfflineCrypto::HMAC_SHA256(key1, message_bytes);
  auto hmac2 = Allow2OfflineCrypto::HMAC_SHA256(key2, message_bytes);

  EXPECT_NE(hmac1, hmac2);
}

// Test that HMAC differs with different messages.
TEST_F(Allow2OfflineCryptoTest, HMAC_DifferentMessages) {
  std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
  std::string message1 = "Message 1";
  std::string message2 = "Message 2";
  std::vector<uint8_t> bytes1(message1.begin(), message1.end());
  std::vector<uint8_t> bytes2(message2.begin(), message2.end());

  auto hmac1 = Allow2OfflineCrypto::HMAC_SHA256(key, bytes1);
  auto hmac2 = Allow2OfflineCrypto::HMAC_SHA256(key, bytes2);

  EXPECT_NE(hmac1, hmac2);
}

// Test HKDF key derivation.
TEST_F(Allow2OfflineCryptoTest, DeriveKey) {
  std::vector<uint8_t> secret = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  std::string info = "allow2-voice-code";

  auto derived = Allow2OfflineCrypto::DeriveKey(secret, info, 32);

  // Should be requested length.
  EXPECT_EQ(32u, derived.size());

  // Should not be all zeros.
  bool all_zero = true;
  for (uint8_t b : derived) {
    if (b != 0) {
      all_zero = false;
      break;
    }
  }
  EXPECT_FALSE(all_zero);

  // Same inputs should produce same output.
  auto derived2 = Allow2OfflineCrypto::DeriveKey(secret, info, 32);
  EXPECT_EQ(derived, derived2);
}

// Test that derived keys differ with different info.
TEST_F(Allow2OfflineCryptoTest, DeriveKey_DifferentInfo) {
  std::vector<uint8_t> secret = {0x01, 0x02, 0x03, 0x04};
  std::string info1 = "context-1";
  std::string info2 = "context-2";

  auto derived1 = Allow2OfflineCrypto::DeriveKey(secret, info1, 32);
  auto derived2 = Allow2OfflineCrypto::DeriveKey(secret, info2, 32);

  EXPECT_NE(derived1, derived2);
}

// Test voice code generation.
TEST_F(Allow2OfflineCryptoTest, GenerateVoiceCode) {
  std::vector<uint8_t> shared_secret = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  std::string child_id = "child123";

  auto code = Allow2OfflineCrypto::GenerateVoiceCode(shared_secret, child_id);

  // Voice code should be 6 digits.
  EXPECT_EQ(6u, code.length());

  // All characters should be digits.
  for (char c : code) {
    EXPECT_GE(c, '0');
    EXPECT_LE(c, '9');
  }
}

// Test voice code verification (same time bucket).
TEST_F(Allow2OfflineCryptoTest, VerifyVoiceCode_SameTimeBucket) {
  std::vector<uint8_t> shared_secret = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  std::string child_id = "child456";

  auto code = Allow2OfflineCrypto::GenerateVoiceCode(shared_secret, child_id);

  // Should verify with tolerance.
  bool result = Allow2OfflineCrypto::VerifyVoiceCode(
      shared_secret, child_id, code, 1);
  EXPECT_TRUE(result);
}

// Test voice code fails with wrong code.
TEST_F(Allow2OfflineCryptoTest, VerifyVoiceCode_WrongCode) {
  std::vector<uint8_t> shared_secret = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  std::string child_id = "child789";

  // Generate code then verify with wrong code.
  Allow2OfflineCrypto::GenerateVoiceCode(shared_secret, child_id);

  bool result = Allow2OfflineCrypto::VerifyVoiceCode(
      shared_secret, child_id, "000000", 1);
  EXPECT_FALSE(result);
}

// Test voice code fails with wrong child.
TEST_F(Allow2OfflineCryptoTest, VerifyVoiceCode_WrongChild) {
  std::vector<uint8_t> shared_secret = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

  auto code = Allow2OfflineCrypto::GenerateVoiceCode(shared_secret, "child-A");

  // Should fail for different child.
  bool result = Allow2OfflineCrypto::VerifyVoiceCode(
      shared_secret, "child-B", code, 1);
  EXPECT_FALSE(result);
}

// Test encryption and decryption.
TEST_F(Allow2OfflineCryptoTest, EncryptDecrypt) {
  std::string plaintext = "Sensitive data that needs encryption";
  std::string device_key = "device-encryption-key-123";

  auto ciphertext = Allow2OfflineCrypto::EncryptForStorage(plaintext, device_key);

  // Ciphertext should not equal plaintext.
  EXPECT_NE(ciphertext, plaintext);

  // Should be able to decrypt.
  auto decrypted = Allow2OfflineCrypto::DecryptFromStorage(ciphertext, device_key);
  ASSERT_TRUE(decrypted.has_value());
  EXPECT_EQ(plaintext, *decrypted);
}

// Test decryption fails with wrong key.
TEST_F(Allow2OfflineCryptoTest, DecryptFails_WrongKey) {
  std::string plaintext = "Secret message";
  std::string key1 = "correct-key";
  std::string key2 = "wrong-key";

  auto ciphertext = Allow2OfflineCrypto::EncryptForStorage(plaintext, key1);

  auto decrypted = Allow2OfflineCrypto::DecryptFromStorage(ciphertext, key2);
  EXPECT_FALSE(decrypted.has_value());
}

// Test decryption fails with corrupted ciphertext.
TEST_F(Allow2OfflineCryptoTest, DecryptFails_CorruptedData) {
  std::string plaintext = "Test data";
  std::string device_key = "test-key";

  auto ciphertext = Allow2OfflineCrypto::EncryptForStorage(plaintext, device_key);

  // Corrupt the ciphertext.
  std::string corrupted = ciphertext;
  if (!corrupted.empty()) {
    corrupted[corrupted.length() / 2] ^= 0xFF;
  }

  auto decrypted = Allow2OfflineCrypto::DecryptFromStorage(corrupted, device_key);
  EXPECT_FALSE(decrypted.has_value());
}

// Test empty plaintext encryption.
TEST_F(Allow2OfflineCryptoTest, EncryptDecrypt_EmptyString) {
  std::string plaintext;
  std::string device_key = "key";

  auto ciphertext = Allow2OfflineCrypto::EncryptForStorage(plaintext, device_key);
  auto decrypted = Allow2OfflineCrypto::DecryptFromStorage(ciphertext, device_key);

  ASSERT_TRUE(decrypted.has_value());
  EXPECT_EQ(plaintext, *decrypted);
}

// Test encryption produces different output each time (due to nonce).
TEST_F(Allow2OfflineCryptoTest, Encrypt_DifferentNonce) {
  std::string plaintext = "Same message";
  std::string device_key = "same-key";

  auto ciphertext1 = Allow2OfflineCrypto::EncryptForStorage(plaintext, device_key);
  auto ciphertext2 = Allow2OfflineCrypto::EncryptForStorage(plaintext, device_key);

  // Should differ due to random nonce.
  EXPECT_NE(ciphertext1, ciphertext2);

  // But both should decrypt correctly.
  auto decrypted1 = Allow2OfflineCrypto::DecryptFromStorage(ciphertext1, device_key);
  auto decrypted2 = Allow2OfflineCrypto::DecryptFromStorage(ciphertext2, device_key);

  ASSERT_TRUE(decrypted1.has_value());
  ASSERT_TRUE(decrypted2.has_value());
  EXPECT_EQ(plaintext, *decrypted1);
  EXPECT_EQ(plaintext, *decrypted2);
}

}  // namespace allow2

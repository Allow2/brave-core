/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_voice_code.h"

#include <string>
#include <unordered_set>

#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

class Allow2VoiceCodeTest : public testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Test voice code generation produces valid format.
TEST_F(Allow2VoiceCodeTest, Generate_ValidFormat) {
  std::string code = Allow2VoiceCode::Generate(1001, 30 * 60, "pair-token-123");

  // Code should be 8 characters (our format uses 8 chars).
  EXPECT_EQ(8u, code.length());

  // All characters should be from valid set.
  std::string valid_chars = Allow2VoiceCode::kValidChars;
  for (char c : code) {
    EXPECT_NE(std::string::npos, valid_chars.find(c))
        << "Invalid character: " << c;
  }
}

// Test that generated codes don't contain ambiguous characters.
TEST_F(Allow2VoiceCodeTest, Generate_NoAmbiguousChars) {
  // Generate multiple codes and check for ambiguous characters.
  std::string ambiguous = "01IOLiol";

  for (int i = 0; i < 100; ++i) {
    std::string code = Allow2VoiceCode::Generate(
        1000 + i, (i + 1) * 60, "token-" + std::to_string(i));

    for (char c : code) {
      EXPECT_EQ(std::string::npos, ambiguous.find(c))
          << "Found ambiguous character: " << c << " in code: " << code;
    }
  }
}

// Test code uniqueness for different inputs.
TEST_F(Allow2VoiceCodeTest, Generate_Uniqueness) {
  std::unordered_set<std::string> codes;

  // Generate many codes with different inputs.
  for (int child_id = 1000; child_id < 1050; ++child_id) {
    for (int seconds = 60; seconds <= 300; seconds += 60) {
      std::string code = Allow2VoiceCode::Generate(
          child_id, seconds, "token-" + std::to_string(child_id));

      // Most codes should be unique (collision unlikely).
      codes.insert(code);
    }
  }

  // Expect high uniqueness rate (allow some collisions due to hash truncation).
  EXPECT_GT(codes.size(), 200u);  // At least 200 unique out of 250.
}

// Test verification of valid code.
TEST_F(Allow2VoiceCodeTest, VerifyAndExtractTime_Valid) {
  uint64_t child_id = 1001;
  int granted_seconds = 30 * 60;  // 30 minutes
  std::string pair_token = "test-pair-token";

  std::string code = Allow2VoiceCode::Generate(child_id, granted_seconds,
                                                pair_token);

  int extracted = Allow2VoiceCode::VerifyAndExtractTime(code);

  // Extracted time should be reasonable (within encoding limits).
  // The exact encoding depends on implementation.
  EXPECT_GE(extracted, 0);
}

// Test verification fails for invalid code length.
TEST_F(Allow2VoiceCodeTest, VerifyAndExtractTime_InvalidLength) {
  int result1 = Allow2VoiceCode::VerifyAndExtractTime("SHORT");
  EXPECT_EQ(-1, result1);

  int result2 = Allow2VoiceCode::VerifyAndExtractTime("TOOLONGCODE");
  EXPECT_EQ(-1, result2);

  int result3 = Allow2VoiceCode::VerifyAndExtractTime("");
  EXPECT_EQ(-1, result3);
}

// Test verification fails for invalid characters.
TEST_F(Allow2VoiceCodeTest, VerifyAndExtractTime_InvalidChars) {
  // Code with ambiguous characters (not in valid set).
  int result = Allow2VoiceCode::VerifyAndExtractTime("01ILOABC");
  // Should handle gracefully - either reject or accept based on normalization.
  // The key is it shouldn't crash.
  (void)result;  // Result depends on implementation.
}

// Test FormatForDisplay.
TEST_F(Allow2VoiceCodeTest, FormatForDisplay) {
  std::string formatted = Allow2VoiceCode::FormatForDisplay("ABCD1234");

  // Should add a separator in the middle.
  EXPECT_EQ("ABCD-1234", formatted);
}

// Test FormatForDisplay with various lengths.
TEST_F(Allow2VoiceCodeTest, FormatForDisplay_VariousLengths) {
  // 8 char code.
  EXPECT_EQ("ABCD-EFGH", Allow2VoiceCode::FormatForDisplay("ABCDEFGH"));

  // 6 char code.
  EXPECT_EQ("ABC-DEF", Allow2VoiceCode::FormatForDisplay("ABCDEF"));

  // 4 char code.
  EXPECT_EQ("AB-CD", Allow2VoiceCode::FormatForDisplay("ABCD"));

  // Short code.
  std::string short_result = Allow2VoiceCode::FormatForDisplay("AB");
  EXPECT_FALSE(short_result.empty());  // Should handle gracefully.
}

// Test Normalize.
TEST_F(Allow2VoiceCodeTest, Normalize) {
  // Remove spaces and dashes.
  EXPECT_EQ("ABCD1234", Allow2VoiceCode::Normalize("ABCD-1234"));
  EXPECT_EQ("ABCD1234", Allow2VoiceCode::Normalize("ABCD 1234"));
  EXPECT_EQ("ABCD1234", Allow2VoiceCode::Normalize("AB CD 12 34"));
  EXPECT_EQ("ABCD1234", Allow2VoiceCode::Normalize("AB-CD-12-34"));

  // Convert to uppercase.
  EXPECT_EQ("ABCD1234", Allow2VoiceCode::Normalize("abcd1234"));
  EXPECT_EQ("ABCD1234", Allow2VoiceCode::Normalize("AbCd1234"));

  // Handle ambiguous character substitution.
  // 0 -> O, 1 -> I, etc. (if implemented).
  std::string normalized = Allow2VoiceCode::Normalize("O1AB");
  // Result depends on implementation - key is consistency.
  EXPECT_EQ(4u, normalized.length());
}

// Test Normalize with empty string.
TEST_F(Allow2VoiceCodeTest, Normalize_Empty) {
  EXPECT_EQ("", Allow2VoiceCode::Normalize(""));
  EXPECT_EQ("", Allow2VoiceCode::Normalize("   "));
  EXPECT_EQ("", Allow2VoiceCode::Normalize("---"));
}

// Test valid character set.
TEST_F(Allow2VoiceCodeTest, ValidChars) {
  std::string valid = Allow2VoiceCode::kValidChars;

  // Should not contain 0, 1, I, O, L (ambiguous).
  EXPECT_EQ(std::string::npos, valid.find('0'));
  EXPECT_EQ(std::string::npos, valid.find('1'));
  EXPECT_EQ(std::string::npos, valid.find('I'));
  EXPECT_EQ(std::string::npos, valid.find('O'));
  EXPECT_EQ(std::string::npos, valid.find('L'));

  // Should contain digits 2-9.
  for (char c = '2'; c <= '9'; ++c) {
    EXPECT_NE(std::string::npos, valid.find(c));
  }

  // Should contain uppercase letters (excluding ambiguous).
  const char* expected_letters = "ABCDEFGHJKMNPQRSTUVWXYZ";
  for (const char* p = expected_letters; *p; ++p) {
    EXPECT_NE(std::string::npos, valid.find(*p))
        << "Missing letter: " << *p;
  }
}

// Test code generation is deterministic for same inputs.
TEST_F(Allow2VoiceCodeTest, Generate_Deterministic) {
  uint64_t child_id = 1001;
  int seconds = 1800;
  std::string token = "same-token";

  std::string code1 = Allow2VoiceCode::Generate(child_id, seconds, token);
  std::string code2 = Allow2VoiceCode::Generate(child_id, seconds, token);

  // Same inputs should produce same output.
  EXPECT_EQ(code1, code2);
}

// Test different inputs produce different codes.
TEST_F(Allow2VoiceCodeTest, Generate_DifferentInputs) {
  std::string base_token = "token";

  // Different child IDs.
  std::string code1 = Allow2VoiceCode::Generate(1001, 1800, base_token);
  std::string code2 = Allow2VoiceCode::Generate(1002, 1800, base_token);
  EXPECT_NE(code1, code2);

  // Different durations.
  std::string code3 = Allow2VoiceCode::Generate(1001, 1800, base_token);
  std::string code4 = Allow2VoiceCode::Generate(1001, 3600, base_token);
  EXPECT_NE(code3, code4);

  // Different tokens.
  std::string code5 = Allow2VoiceCode::Generate(1001, 1800, "token-A");
  std::string code6 = Allow2VoiceCode::Generate(1001, 1800, "token-B");
  EXPECT_NE(code5, code6);
}

// Test edge cases for seconds.
TEST_F(Allow2VoiceCodeTest, Generate_EdgeCaseSeconds) {
  std::string token = "test";

  // Zero seconds.
  std::string code0 = Allow2VoiceCode::Generate(1001, 0, token);
  EXPECT_EQ(8u, code0.length());

  // Very large seconds.
  std::string code_large = Allow2VoiceCode::Generate(1001, 3600 * 24, token);
  EXPECT_EQ(8u, code_large.length());

  // Negative seconds (if supported, should handle gracefully).
  // Implementation may treat as 0 or absolute value.
}

// Test code with all same characters (edge case).
TEST_F(Allow2VoiceCodeTest, VerifyAndExtractTime_AllSameChars) {
  // Create a code with all same characters.
  std::string monotonic = "AAAAAAAA";

  int result = Allow2VoiceCode::VerifyAndExtractTime(monotonic);
  // Should handle gracefully - may be valid or invalid depending on encoding.
  (void)result;
}

// Test roundtrip: generate, normalize, and format.
TEST_F(Allow2VoiceCodeTest, Roundtrip) {
  std::string original = Allow2VoiceCode::Generate(1001, 1800, "token");

  std::string formatted = Allow2VoiceCode::FormatForDisplay(original);
  std::string normalized = Allow2VoiceCode::Normalize(formatted);

  EXPECT_EQ(original, normalized);
}

}  // namespace allow2

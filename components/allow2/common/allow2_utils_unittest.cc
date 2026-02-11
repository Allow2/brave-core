/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/common/allow2_utils.h"

#include "base/test/task_environment.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

class Allow2UtilsTest : public testing::Test {
 protected:
  void SetUp() override {
    RegisterProfilePrefs(profile_prefs_.registry());
    RegisterLocalStatePrefs(local_state_.registry());
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple profile_prefs_;
  TestingPrefServiceSimple local_state_;
};

TEST_F(Allow2UtilsTest, GenerateDeviceToken) {
  std::string token1 = GenerateDeviceToken();
  std::string token2 = GenerateDeviceToken();

  // Tokens should start with the prefix
  EXPECT_TRUE(token1.find(kDeviceTokenPrefix) == 0);
  EXPECT_TRUE(token2.find(kDeviceTokenPrefix) == 0);

  // Tokens should be unique
  EXPECT_NE(token1, token2);

  // Tokens should be reasonably long (prefix + UUID)
  EXPECT_GT(token1.length(), 36u);
}

TEST_F(Allow2UtilsTest, CategorizeUrl_Social) {
  EXPECT_EQ(ActivityId::kSocial, CategorizeUrl("https://facebook.com/"));
  EXPECT_EQ(ActivityId::kSocial, CategorizeUrl("https://www.instagram.com/"));
  EXPECT_EQ(ActivityId::kSocial, CategorizeUrl("https://twitter.com/user"));
  EXPECT_EQ(ActivityId::kSocial, CategorizeUrl("https://m.facebook.com/"));
  EXPECT_EQ(ActivityId::kSocial, CategorizeUrl("https://reddit.com/r/brave"));
}

TEST_F(Allow2UtilsTest, CategorizeUrl_Gaming) {
  EXPECT_EQ(ActivityId::kGaming, CategorizeUrl("https://roblox.com/"));
  EXPECT_EQ(ActivityId::kGaming, CategorizeUrl("https://www.minecraft.net/"));
  EXPECT_EQ(ActivityId::kGaming, CategorizeUrl("https://twitch.tv/stream"));
  EXPECT_EQ(ActivityId::kGaming, CategorizeUrl("https://discord.com/invite"));
}

TEST_F(Allow2UtilsTest, CategorizeUrl_Education) {
  EXPECT_EQ(ActivityId::kEducation, CategorizeUrl("https://khanacademy.org/"));
  EXPECT_EQ(ActivityId::kEducation, CategorizeUrl("https://www.coursera.org/"));
  EXPECT_EQ(ActivityId::kEducation, CategorizeUrl("https://duolingo.com/"));
}

TEST_F(Allow2UtilsTest, CategorizeUrl_GeneralInternet) {
  EXPECT_EQ(ActivityId::kInternet, CategorizeUrl("https://google.com/"));
  EXPECT_EQ(ActivityId::kInternet, CategorizeUrl("https://brave.com/"));
  EXPECT_EQ(ActivityId::kInternet, CategorizeUrl("https://example.org/"));
  EXPECT_EQ(ActivityId::kInternet, CategorizeUrl("https://news.ycombinator.com/"));
}

TEST_F(Allow2UtilsTest, CategorizeUrl_InvalidUrl) {
  EXPECT_EQ(ActivityId::kInternet, CategorizeUrl("not-a-url"));
  EXPECT_EQ(ActivityId::kInternet, CategorizeUrl(""));
}

TEST_F(Allow2UtilsTest, HashPin) {
  std::string hash1 = HashPin("1234", "salt123");
  std::string hash2 = HashPin("1234", "salt123");
  std::string hash3 = HashPin("1234", "different_salt");
  std::string hash4 = HashPin("5678", "salt123");

  // Same input should produce same hash
  EXPECT_EQ(hash1, hash2);

  // Different salt should produce different hash
  EXPECT_NE(hash1, hash3);

  // Different PIN should produce different hash
  EXPECT_NE(hash1, hash4);

  // Hash should start with sha256: prefix
  EXPECT_TRUE(hash1.find("sha256:") == 0);

  // Hash should have correct length (sha256: + 64 hex chars)
  EXPECT_EQ(hash1.length(), 7u + 64u);
}

TEST_F(Allow2UtilsTest, ValidatePinHash) {
  std::string salt = "test_salt_123";
  std::string pin = "1234";
  std::string hash = HashPin(pin, salt);

  // Correct PIN should validate
  EXPECT_TRUE(ValidatePinHash(pin, hash, salt));

  // Wrong PIN should not validate
  EXPECT_FALSE(ValidatePinHash("5678", hash, salt));
  EXPECT_FALSE(ValidatePinHash("", hash, salt));

  // Empty hash or salt should not validate
  EXPECT_FALSE(ValidatePinHash(pin, "", salt));
  EXPECT_FALSE(ValidatePinHash(pin, hash, ""));
}

TEST_F(Allow2UtilsTest, IsPaired_NotPaired) {
  // No credentials set
  EXPECT_FALSE(IsPaired(&local_state_));
}

TEST_F(Allow2UtilsTest, IsPaired_Paired) {
  local_state_.SetString(prefs::kAllow2Credentials, "encrypted_data");
  EXPECT_TRUE(IsPaired(&local_state_));
}

TEST_F(Allow2UtilsTest, IsEnabled) {
  // Default should be enabled
  EXPECT_TRUE(IsEnabled(&profile_prefs_));

  // Explicitly disabled
  profile_prefs_.SetBoolean(prefs::kAllow2Enabled, false);
  EXPECT_FALSE(IsEnabled(&profile_prefs_));

  // Re-enabled
  profile_prefs_.SetBoolean(prefs::kAllow2Enabled, true);
  EXPECT_TRUE(IsEnabled(&profile_prefs_));
}

TEST_F(Allow2UtilsTest, IsSharedDeviceMode) {
  // Default (no child ID) should be shared mode
  EXPECT_TRUE(IsSharedDeviceMode(&profile_prefs_));

  // With child ID set, not shared mode
  profile_prefs_.SetString(prefs::kAllow2ChildId, "12345");
  EXPECT_FALSE(IsSharedDeviceMode(&profile_prefs_));

  // Empty child ID should be shared mode
  profile_prefs_.SetString(prefs::kAllow2ChildId, "");
  EXPECT_TRUE(IsSharedDeviceMode(&profile_prefs_));
}

TEST_F(Allow2UtilsTest, GetCurrentChildId) {
  EXPECT_EQ("", GetCurrentChildId(&profile_prefs_));

  profile_prefs_.SetString(prefs::kAllow2ChildId, "12345");
  EXPECT_EQ("12345", GetCurrentChildId(&profile_prefs_));
}

TEST_F(Allow2UtilsTest, GetCurrentTimezone) {
  std::string tz = GetCurrentTimezone();

  // Should return something (either a valid timezone or UTC fallback)
  EXPECT_FALSE(tz.empty());

  // Should contain a "/" for IANA format, or be "UTC"
  if (tz != "UTC") {
    EXPECT_NE(tz.find("/"), std::string::npos);
  }
}

}  // namespace allow2

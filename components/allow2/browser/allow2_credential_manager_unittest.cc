/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_credential_manager.h"

#include "base/test/task_environment.h"
#include "brave/components/allow2/common/allow2_utils.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/os_crypt/sync/os_crypt_mocker.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

class Allow2CredentialManagerTest : public testing::Test {
 protected:
  void SetUp() override {
    OSCryptMocker::SetUp();
    RegisterLocalStatePrefs(local_state_.registry());
    credential_manager_ = std::make_unique<Allow2CredentialManager>(&local_state_);
  }

  void TearDown() override {
    credential_manager_.reset();
    OSCryptMocker::TearDown();
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple local_state_;
  std::unique_ptr<Allow2CredentialManager> credential_manager_;
};

TEST_F(Allow2CredentialManagerTest, HasCredentials_Empty) {
  EXPECT_FALSE(credential_manager_->HasCredentials());
}

TEST_F(Allow2CredentialManagerTest, StoreAndGetCredentials) {
  EXPECT_TRUE(credential_manager_->StoreCredentials("user123", "pair456", "token789"));
  EXPECT_TRUE(credential_manager_->HasCredentials());

  auto creds = credential_manager_->GetCredentials();
  ASSERT_TRUE(creds.has_value());
  EXPECT_EQ("user123", creds->user_id);
  EXPECT_EQ("pair456", creds->pair_id);
  EXPECT_EQ("token789", creds->pair_token);
}

TEST_F(Allow2CredentialManagerTest, StoreCredentials_Struct) {
  Credentials creds;
  creds.user_id = "user_abc";
  creds.pair_id = "pair_def";
  creds.pair_token = "token_ghi";

  EXPECT_TRUE(credential_manager_->StoreCredentials(creds));

  auto retrieved = credential_manager_->GetCredentials();
  ASSERT_TRUE(retrieved.has_value());
  EXPECT_EQ(creds.user_id, retrieved->user_id);
  EXPECT_EQ(creds.pair_id, retrieved->pair_id);
  EXPECT_EQ(creds.pair_token, retrieved->pair_token);
}

TEST_F(Allow2CredentialManagerTest, StoreCredentials_InvalidEmpty) {
  EXPECT_FALSE(credential_manager_->StoreCredentials("", "pair", "token"));
  EXPECT_FALSE(credential_manager_->StoreCredentials("user", "", "token"));
  EXPECT_FALSE(credential_manager_->StoreCredentials("user", "pair", ""));
}

TEST_F(Allow2CredentialManagerTest, ClearCredentials) {
  EXPECT_TRUE(credential_manager_->StoreCredentials("user", "pair", "token"));
  EXPECT_TRUE(credential_manager_->HasCredentials());

  credential_manager_->ClearCredentials();
  EXPECT_FALSE(credential_manager_->HasCredentials());
  EXPECT_FALSE(credential_manager_->GetCredentials().has_value());
}

TEST_F(Allow2CredentialManagerTest, DeviceToken) {
  EXPECT_EQ("", credential_manager_->GetDeviceToken());

  credential_manager_->SetDeviceToken("device_token_123");
  EXPECT_EQ("device_token_123", credential_manager_->GetDeviceToken());

  // Clear credentials should also clear device token
  credential_manager_->ClearCredentials();
  EXPECT_EQ("", credential_manager_->GetDeviceToken());
}

TEST_F(Allow2CredentialManagerTest, DeviceName) {
  EXPECT_EQ("", credential_manager_->GetDeviceName());

  credential_manager_->SetDeviceName("My Test Device");
  EXPECT_EQ("My Test Device", credential_manager_->GetDeviceName());

  // Clear credentials should also clear device name
  credential_manager_->ClearCredentials();
  EXPECT_EQ("", credential_manager_->GetDeviceName());
}

TEST_F(Allow2CredentialManagerTest, PairedAt) {
  // Default should be null time
  EXPECT_EQ(base::Time(), credential_manager_->GetPairedAt());

  // Store credentials sets paired time
  EXPECT_TRUE(credential_manager_->StoreCredentials("user", "pair", "token"));
  EXPECT_NE(base::Time(), credential_manager_->GetPairedAt());
  EXPECT_LE(credential_manager_->GetPairedAt(), base::Time::Now());
}

TEST_F(Allow2CredentialManagerTest, CredentialsAreEncrypted) {
  EXPECT_TRUE(credential_manager_->StoreCredentials("user123", "pair456", "token789"));

  // The stored value should not be plaintext
  std::string stored = local_state_.GetString(prefs::kAllow2Credentials);
  EXPECT_FALSE(stored.empty());
  EXPECT_EQ(std::string::npos, stored.find("user123"));
  EXPECT_EQ(std::string::npos, stored.find("pair456"));
  EXPECT_EQ(std::string::npos, stored.find("token789"));
}

TEST_F(Allow2CredentialManagerTest, CredentialsIsValid) {
  Credentials valid;
  valid.user_id = "user";
  valid.pair_id = "pair";
  valid.pair_token = "token";
  EXPECT_TRUE(valid.IsValid());

  Credentials empty_user;
  empty_user.user_id = "";
  empty_user.pair_id = "pair";
  empty_user.pair_token = "token";
  EXPECT_FALSE(empty_user.IsValid());

  Credentials empty_pair;
  empty_pair.user_id = "user";
  empty_pair.pair_id = "";
  empty_pair.pair_token = "token";
  EXPECT_FALSE(empty_pair.IsValid());

  Credentials empty_token;
  empty_token.user_id = "user";
  empty_token.pair_id = "pair";
  empty_token.pair_token = "";
  EXPECT_FALSE(empty_token.IsValid());
}

}  // namespace allow2

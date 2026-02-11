/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_service.h"

#include "base/test/task_environment.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/common/allow2_utils.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/os_crypt/sync/os_crypt_mocker.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

class MockAllow2ServiceObserver : public Allow2ServiceObserver {
 public:
  MOCK_METHOD(void, OnPairedStateChanged, (bool is_paired), (override));
  MOCK_METHOD(void, OnBlockingStateChanged, (bool is_blocked, const std::string& reason), (override));
  MOCK_METHOD(void, OnRemainingTimeUpdated, (int remaining_seconds), (override));
  MOCK_METHOD(void, OnWarningThresholdReached, (int remaining_seconds), (override));
  MOCK_METHOD(void, OnCurrentChildChanged, (const std::optional<Child>& child), (override));
  MOCK_METHOD(void, OnCredentialsInvalidated, (), (override));
};

class Allow2ServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    OSCryptMocker::SetUp();
    RegisterProfilePrefs(profile_prefs_.registry());
    RegisterLocalStatePrefs(local_state_.registry());

    test_url_loader_factory_ =
        std::make_unique<network::TestURLLoaderFactory>();
    shared_url_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            test_url_loader_factory_.get());

    service_ = std::make_unique<Allow2Service>(
        shared_url_loader_factory_, &local_state_, &profile_prefs_);
  }

  void TearDown() override {
    service_.reset();
    OSCryptMocker::TearDown();
  }

  void SetupPairedState() {
    auto* cred_manager = service_->GetCredentialManagerForTesting();
    cred_manager->StoreCredentials("user123", "pair456", "token789");

    // Add test children
    profile_prefs_.SetString(prefs::kAllow2CachedChildren,
        R"([{"id": 1001, "name": "Emma", "pinHash": "sha256:abc123", "pinSalt": "salt1"},
            {"id": 1002, "name": "Jack", "pinHash": "sha256:def456", "pinSalt": "salt2"}])");
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple profile_prefs_;
  TestingPrefServiceSimple local_state_;
  std::unique_ptr<network::TestURLLoaderFactory> test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
  std::unique_ptr<Allow2Service> service_;
};

TEST_F(Allow2ServiceTest, InitialState_NotPaired) {
  EXPECT_FALSE(service_->IsPaired());
  EXPECT_TRUE(service_->IsEnabled());
  EXPECT_TRUE(service_->IsSharedDeviceMode());
  EXPECT_FALSE(service_->IsBlocked());
  EXPECT_EQ(0, service_->GetRemainingSeconds());
}

TEST_F(Allow2ServiceTest, IsPaired) {
  EXPECT_FALSE(service_->IsPaired());

  SetupPairedState();
  EXPECT_TRUE(service_->IsPaired());
}

TEST_F(Allow2ServiceTest, GetChildren) {
  SetupPairedState();

  auto children = service_->GetChildren();
  ASSERT_EQ(2u, children.size());
  EXPECT_EQ(1001u, children[0].id);
  EXPECT_EQ("Emma", children[0].name);
  EXPECT_EQ(1002u, children[1].id);
  EXPECT_EQ("Jack", children[1].name);
}

TEST_F(Allow2ServiceTest, SharedDeviceMode) {
  SetupPairedState();

  EXPECT_TRUE(service_->IsSharedDeviceMode());
  EXPECT_FALSE(service_->GetCurrentChild().has_value());

  profile_prefs_.SetString(prefs::kAllow2ChildId, "1001");
  EXPECT_FALSE(service_->IsSharedDeviceMode());

  auto child = service_->GetCurrentChild();
  ASSERT_TRUE(child.has_value());
  EXPECT_EQ(1001u, child->id);
  EXPECT_EQ("Emma", child->name);
}

TEST_F(Allow2ServiceTest, ClearCurrentChild) {
  SetupPairedState();
  profile_prefs_.SetString(prefs::kAllow2ChildId, "1001");

  MockAllow2ServiceObserver observer;
  service_->AddObserver(&observer);

  EXPECT_CALL(observer, OnCurrentChildChanged(testing::Eq(std::nullopt)));

  service_->ClearCurrentChild();
  EXPECT_TRUE(service_->IsSharedDeviceMode());

  service_->RemoveObserver(&observer);
}

TEST_F(Allow2ServiceTest, SetEnabled) {
  EXPECT_TRUE(service_->IsEnabled());

  service_->SetEnabled(false);
  EXPECT_FALSE(service_->IsEnabled());

  service_->SetEnabled(true);
  EXPECT_TRUE(service_->IsEnabled());
}

TEST_F(Allow2ServiceTest, AddRemoveObserver) {
  MockAllow2ServiceObserver observer;
  service_->AddObserver(&observer);

  // No crash when removing
  service_->RemoveObserver(&observer);
}

TEST_F(Allow2ServiceTest, TrackUrl_NotPaired) {
  // Should not crash when not paired
  service_->TrackUrl("https://facebook.com");
}

TEST_F(Allow2ServiceTest, TrackUrl_Paired) {
  SetupPairedState();
  profile_prefs_.SetString(prefs::kAllow2ChildId, "1001");

  // Should not crash when paired
  service_->TrackUrl("https://facebook.com");
}

TEST_F(Allow2ServiceTest, CheckAllowance_NotPaired) {
  bool callback_called = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* called, const CheckResult& result) {
        *called = true;
        EXPECT_TRUE(result.allowed);  // Default to allowed when not paired
      },
      &callback_called));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(Allow2ServiceTest, CheckAllowance_NoChildSelected) {
  SetupPairedState();
  // Don't set child ID

  bool callback_called = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* called, const CheckResult& result) {
        *called = true;
        EXPECT_TRUE(result.allowed);  // Default to allowed when no child
      },
      &callback_called));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(Allow2ServiceTest, RequestMoreTime_NotPaired) {
  bool callback_called = false;
  service_->RequestMoreTime(
      ActivityId::kInternet, 30, "Please give me more time",
      base::BindOnce(
          [](bool* called, bool success, const std::string& error) {
            *called = true;
            EXPECT_FALSE(success);
            EXPECT_EQ("Not paired", error);
          },
          &callback_called));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(Allow2ServiceTest, RequestMoreTime_NoChildSelected) {
  SetupPairedState();
  // Don't set child ID

  bool callback_called = false;
  service_->RequestMoreTime(
      ActivityId::kInternet, 30, "Please give me more time",
      base::BindOnce(
          [](bool* called, bool success, const std::string& error) {
            *called = true;
            EXPECT_FALSE(success);
            EXPECT_EQ("No child selected", error);
          },
          &callback_called));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(Allow2ServiceTest, BlockReason) {
  EXPECT_EQ("", service_->GetBlockReason());
  EXPECT_FALSE(service_->IsBlocked());
}

}  // namespace allow2

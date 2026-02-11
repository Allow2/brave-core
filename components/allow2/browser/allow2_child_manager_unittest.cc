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
#include "crypto/sha2.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

namespace {

// Helper to create SHA256 hash for PIN
std::string HashPin(const std::string& pin, const std::string& salt) {
  std::string salted = pin + salt;
  std::string hash = crypto::SHA256HashString(salted);

  // Convert to hex string with sha256: prefix
  std::string hex = "sha256:";
  for (unsigned char c : hash) {
    hex += base::StringPrintf("%02x", c);
  }
  return hex;
}

}  // namespace

class Allow2ChildManagerTest : public testing::Test {
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

  void SetupPairedStateWithChildren() {
    auto* cred_manager = service_->GetCredentialManagerForTesting();
    cred_manager->StoreCredentials("user123", "pair456", "token789");

    // Add test children with proper PIN hashes
    base::Value::List children;

    base::Value::Dict child1;
    child1.Set("id", 1001);
    child1.Set("name", "Emma");
    child1.Set("pinHash", HashPin("1234", "salt_emma"));
    child1.Set("pinSalt", "salt_emma");
    children.Append(std::move(child1));

    base::Value::Dict child2;
    child2.Set("id", 1002);
    child2.Set("name", "Jack");
    child2.Set("pinHash", HashPin("5678", "salt_jack"));
    child2.Set("pinSalt", "salt_jack");
    children.Append(std::move(child2));

    std::string json;
    base::JSONWriter::Write(children, &json);
    profile_prefs_.SetString(prefs::kAllow2CachedChildren, json);
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple profile_prefs_;
  TestingPrefServiceSimple local_state_;
  std::unique_ptr<network::TestURLLoaderFactory> test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
  std::unique_ptr<Allow2Service> service_;
};

// ============================================================================
// PIN Verification Tests
// ============================================================================

TEST_F(Allow2ChildManagerTest, SelectChild_CorrectPin) {
  SetupPairedStateWithChildren();

  auto children = service_->GetChildren();
  ASSERT_EQ(2u, children.size());

  // Select Emma with correct PIN
  bool success = service_->SelectChild(1001, "1234");
  EXPECT_TRUE(success);

  auto current = service_->GetCurrentChild();
  ASSERT_TRUE(current.has_value());
  EXPECT_EQ(1001u, current->id);
  EXPECT_EQ("Emma", current->name);
}

TEST_F(Allow2ChildManagerTest, SelectChild_WrongPin) {
  SetupPairedStateWithChildren();

  // Try to select Emma with wrong PIN
  bool success = service_->SelectChild(1001, "9999");
  EXPECT_FALSE(success);

  // Should not be selected
  EXPECT_FALSE(service_->GetCurrentChild().has_value());
  EXPECT_TRUE(service_->IsSharedDeviceMode());
}

TEST_F(Allow2ChildManagerTest, SelectChild_EmptyPin) {
  SetupPairedStateWithChildren();

  bool success = service_->SelectChild(1001, "");
  EXPECT_FALSE(success);
  EXPECT_FALSE(service_->GetCurrentChild().has_value());
}

TEST_F(Allow2ChildManagerTest, SelectChild_InvalidChildId) {
  SetupPairedStateWithChildren();

  // Try to select non-existent child
  bool success = service_->SelectChild(9999, "1234");
  EXPECT_FALSE(success);
  EXPECT_FALSE(service_->GetCurrentChild().has_value());
}

TEST_F(Allow2ChildManagerTest, SelectChild_MultipleChildren) {
  SetupPairedStateWithChildren();

  // Select first child
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));
  EXPECT_EQ(1001u, service_->GetCurrentChild()->id);

  // Switch to second child
  EXPECT_TRUE(service_->SelectChild(1002, "5678"));
  EXPECT_EQ(1002u, service_->GetCurrentChild()->id);
}

TEST_F(Allow2ChildManagerTest, SelectChild_SwitchWithWrongPin) {
  SetupPairedStateWithChildren();

  // Select first child
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));
  EXPECT_EQ(1001u, service_->GetCurrentChild()->id);

  // Try to switch with wrong PIN
  EXPECT_FALSE(service_->SelectChild(1002, "0000"));

  // Should still be on first child
  EXPECT_EQ(1001u, service_->GetCurrentChild()->id);
}

// ============================================================================
// PIN Timing Attack Protection Tests
// ============================================================================

TEST_F(Allow2ChildManagerTest, PinValidation_ConstantTime) {
  SetupPairedStateWithChildren();

  // These should take roughly the same time (constant-time comparison)
  // We can't directly test timing, but we test the behavior is consistent
  const int iterations = 100;

  for (int i = 0; i < iterations; i++) {
    // Wrong PIN - first digit different
    EXPECT_FALSE(service_->SelectChild(1001, "9234"));

    // Wrong PIN - last digit different
    EXPECT_FALSE(service_->SelectChild(1001, "1239"));

    // Wrong PIN - completely different
    EXPECT_FALSE(service_->SelectChild(1001, "0000"));
  }
}

TEST_F(Allow2ChildManagerTest, PinValidation_DifferentLengths) {
  SetupPairedStateWithChildren();

  // Too short
  EXPECT_FALSE(service_->SelectChild(1001, "123"));

  // Too long
  EXPECT_FALSE(service_->SelectChild(1001, "12345"));

  // Correct length but wrong
  EXPECT_FALSE(service_->SelectChild(1001, "0000"));

  // Correct
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));
}

// ============================================================================
// Child Selection State Management
// ============================================================================

TEST_F(Allow2ChildManagerTest, ClearCurrentChild) {
  SetupPairedStateWithChildren();

  // Select a child
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));
  EXPECT_FALSE(service_->IsSharedDeviceMode());
  EXPECT_TRUE(service_->GetCurrentChild().has_value());

  // Clear selection
  service_->ClearCurrentChild();
  EXPECT_TRUE(service_->IsSharedDeviceMode());
  EXPECT_FALSE(service_->GetCurrentChild().has_value());
}

TEST_F(Allow2ChildManagerTest, SharedDeviceMode_NoPairing) {
  // Not paired - should be in shared mode
  EXPECT_TRUE(service_->IsSharedDeviceMode());
  EXPECT_FALSE(service_->GetCurrentChild().has_value());
}

TEST_F(Allow2ChildManagerTest, SharedDeviceMode_PairedNoChild) {
  SetupPairedStateWithChildren();

  // Paired but no child selected
  EXPECT_TRUE(service_->IsSharedDeviceMode());
  EXPECT_FALSE(service_->GetCurrentChild().has_value());
}

TEST_F(Allow2ChildManagerTest, SharedDeviceMode_ChildSelected) {
  SetupPairedStateWithChildren();

  service_->SelectChild(1001, "1234");

  // Child selected - not in shared mode
  EXPECT_FALSE(service_->IsSharedDeviceMode());
  EXPECT_TRUE(service_->GetCurrentChild().has_value());
}

// ============================================================================
// Observer Notifications
// ============================================================================

TEST_F(Allow2ChildManagerTest, Observer_ChildSelection) {
  SetupPairedStateWithChildren();

  MockAllow2ServiceObserver observer;
  service_->AddObserver(&observer);

  // Expect notification when child is selected
  EXPECT_CALL(observer, OnCurrentChildChanged(testing::_)).Times(1);

  service_->SelectChild(1001, "1234");

  testing::Mock::VerifyAndClearExpectations(&observer);
  service_->RemoveObserver(&observer);
}

TEST_F(Allow2ChildManagerTest, Observer_ChildCleared) {
  SetupPairedStateWithChildren();
  service_->SelectChild(1001, "1234");

  MockAllow2ServiceObserver observer;
  service_->AddObserver(&observer);

  // Expect notification when child is cleared
  EXPECT_CALL(observer, OnCurrentChildChanged(testing::Eq(std::nullopt)))
      .Times(1);

  service_->ClearCurrentChild();

  testing::Mock::VerifyAndClearExpectations(&observer);
  service_->RemoveObserver(&observer);
}

TEST_F(Allow2ChildManagerTest, Observer_NoNotificationOnFailedPin) {
  SetupPairedStateWithChildren();

  MockAllow2ServiceObserver observer;
  service_->AddObserver(&observer);

  // Should NOT notify on failed PIN
  EXPECT_CALL(observer, OnCurrentChildChanged(testing::_)).Times(0);

  service_->SelectChild(1001, "wrong");

  testing::Mock::VerifyAndClearExpectations(&observer);
  service_->RemoveObserver(&observer);
}

// ============================================================================
// Persistence Tests
// ============================================================================

TEST_F(Allow2ChildManagerTest, ChildSelection_Persisted) {
  SetupPairedStateWithChildren();

  service_->SelectChild(1001, "1234");

  // Verify it's stored in prefs
  std::string stored_id = profile_prefs_.GetString(prefs::kAllow2ChildId);
  EXPECT_EQ("1001", stored_id);
}

TEST_F(Allow2ChildManagerTest, ChildSelection_LoadedOnStartup) {
  SetupPairedStateWithChildren();

  // Simulate already selected child in prefs
  profile_prefs_.SetString(prefs::kAllow2ChildId, "1002");

  // Create new service instance (simulates app restart)
  auto new_service = std::make_unique<Allow2Service>(
      shared_url_loader_factory_, &local_state_, &profile_prefs_);

  auto current = new_service->GetCurrentChild();
  ASSERT_TRUE(current.has_value());
  EXPECT_EQ(1002u, current->id);
  EXPECT_EQ("Jack", current->name);
}

TEST_F(Allow2ChildManagerTest, ClearChild_RemovesFromPrefs) {
  SetupPairedStateWithChildren();

  service_->SelectChild(1001, "1234");
  EXPECT_FALSE(profile_prefs_.GetString(prefs::kAllow2ChildId).empty());

  service_->ClearCurrentChild();
  EXPECT_TRUE(profile_prefs_.GetString(prefs::kAllow2ChildId).empty());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(Allow2ChildManagerTest, SelectChild_NotPaired) {
  // Try to select child when not paired
  bool success = service_->SelectChild(1001, "1234");
  EXPECT_FALSE(success);
  EXPECT_FALSE(service_->GetCurrentChild().has_value());
}

TEST_F(Allow2ChildManagerTest, SelectChild_NoChildren) {
  auto* cred_manager = service_->GetCredentialManagerForTesting();
  cred_manager->StoreCredentials("user123", "pair456", "token789");

  // Paired but no children cached
  bool success = service_->SelectChild(1001, "1234");
  EXPECT_FALSE(success);
}

TEST_F(Allow2ChildManagerTest, GetChildren_Empty) {
  auto children = service_->GetChildren();
  EXPECT_TRUE(children.empty());
}

TEST_F(Allow2ChildManagerTest, GetChildren_Malformed) {
  auto* cred_manager = service_->GetCredentialManagerForTesting();
  cred_manager->StoreCredentials("user123", "pair456", "token789");

  // Set malformed JSON
  profile_prefs_.SetString(prefs::kAllow2CachedChildren, "{ invalid json }");

  auto children = service_->GetChildren();
  EXPECT_TRUE(children.empty());
}

}  // namespace allow2

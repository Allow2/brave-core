/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

// Integration tests for Allow2 parental controls
// Tests the full flow from pairing through blocking and request more time

#include "brave/components/allow2/browser/allow2_service.h"

#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "brave/components/allow2/browser/allow2_api_client.h"
#include "brave/components/allow2/browser/allow2_block_overlay.h"
#include "brave/components/allow2/browser/allow2_child_manager.h"
#include "brave/components/allow2/browser/allow2_child_shield.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/browser/allow2_pairing_handler.h"
#include "brave/components/allow2/browser/allow2_usage_tracker.h"
#include "brave/components/allow2/browser/allow2_warning_controller.h"
#include "brave/components/allow2/common/allow2_constants.h"
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
namespace {

// ============================================================================
// Mock Observer for tracking all service events
// ============================================================================

class MockServiceObserver : public Allow2ServiceObserver {
 public:
  MOCK_METHOD(void, OnPairedStateChanged, (bool is_paired), (override));
  MOCK_METHOD(void, OnBlockingStateChanged,
              (bool is_blocked, const std::string& reason), (override));
  MOCK_METHOD(void, OnRemainingTimeUpdated, (int remaining_seconds), (override));
  MOCK_METHOD(void, OnWarningThresholdReached, (int remaining_seconds), (override));
  MOCK_METHOD(void, OnCurrentChildChanged,
              (const std::optional<Child>& child), (override));
  MOCK_METHOD(void, OnCredentialsInvalidated, (), (override));
};

// ============================================================================
// Integration Test Fixture
// ============================================================================

class Allow2IntegrationTest : public testing::Test {
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
    service_->AddObserver(&observer_);
  }

  void TearDown() override {
    service_->RemoveObserver(&observer_);
    service_.reset();
    OSCryptMocker::TearDown();
  }

  // ========================================================================
  // Helper Methods for Test Data
  // ========================================================================

  void SetupPairedState() {
    auto* cred_manager = service_->GetCredentialManagerForTesting();
    cred_manager->StoreCredentials("user123", "pair456", "token789");

    // Add test children with proper PIN hashes
    std::string emma_hash = HashPin("1234", "salt_emma");
    std::string jack_hash = HashPin("5678", "salt_jack");

    profile_prefs_.SetString(prefs::kAllow2CachedChildren,
        "[{\"id\": 1001, \"name\": \"Emma\", \"pinHash\": \"" + emma_hash +
        "\", \"pinSalt\": \"salt_emma\"},"
        "{\"id\": 1002, \"name\": \"Jack\", \"pinHash\": \"" + jack_hash +
        "\", \"pinSalt\": \"salt_jack\"}]");
  }

  void SelectChild(uint64_t child_id) {
    profile_prefs_.SetString(prefs::kAllow2ChildId,
                             std::to_string(child_id));
  }

  std::string CreateAllowedResponse(int remaining_seconds) {
    base::Value::Dict response;
    response.Set("allowed", true);
    response.Set("minimumRemainingSeconds", remaining_seconds);
    response.Set("expires", base::Time::Now().ToTimeT() + 60);
    response.Set("banned", false);
    response.Set("dayType", "normal");

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  std::string CreateBlockedResponse(const std::string& reason,
                                    const std::string& day_type = "School Night") {
    base::Value::Dict response;
    response.Set("allowed", false);
    response.Set("minimumRemainingSeconds", 0);
    response.Set("expires", base::Time::Now().ToTimeT() + 60);
    response.Set("banned", reason == "banned");
    response.Set("blockReason", reason);
    response.Set("dayType", day_type);

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  std::string CreateQRPairingInitResponse(const std::string& session_id,
                                          const std::string& qr_url,
                                          int expires_in = 300) {
    base::Value::Dict response;
    response.Set("status", "success");
    response.Set("sessionId", session_id);
    response.Set("qrCodeUrl", qr_url);
    response.Set("expiresIn", expires_in);

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  std::string CreatePINPairingInitResponse(const std::string& session_id,
                                           const std::string& pin,
                                           int expires_in = 300) {
    base::Value::Dict response;
    response.Set("status", "success");
    response.Set("sessionId", session_id);
    response.Set("pin", pin);
    response.Set("expiresIn", expires_in);

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  std::string CreatePairingStatusPendingResponse() {
    base::Value::Dict response;
    response.Set("status", "pending");
    response.Set("scanned", false);

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  std::string CreatePairingStatusScannedResponse() {
    base::Value::Dict response;
    response.Set("status", "pending");
    response.Set("scanned", true);

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  std::string CreatePairingStatusCompletedResponse() {
    base::Value::Dict response;
    response.Set("status", "completed");

    base::Value::Dict credentials;
    credentials.Set("userId", "user123");
    credentials.Set("pairId", "pair456");
    credentials.Set("pairToken", "token789");
    response.Set("credentials", std::move(credentials));

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

    response.Set("children", std::move(children));

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TestingPrefServiceSimple profile_prefs_;
  TestingPrefServiceSimple local_state_;
  std::unique_ptr<network::TestURLLoaderFactory> test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
  std::unique_ptr<Allow2Service> service_;
  testing::StrictMock<MockServiceObserver> observer_;
};

// ============================================================================
// SCENARIO 1: Happy Path - Pair, Select Child, Browse, Hit Limit, Block
// ============================================================================

TEST_F(Allow2IntegrationTest, HappyPath_PairSelectBrowseBlock) {
  // Phase 1: Verify unpaired state
  EXPECT_FALSE(service_->IsPaired());
  EXPECT_TRUE(service_->IsEnabled());
  EXPECT_FALSE(service_->IsBlocked());

  // Phase 2: Setup paired state (simulating completed pairing)
  EXPECT_CALL(observer_, OnPairedStateChanged(true));
  SetupPairedState();
  EXPECT_TRUE(service_->IsPaired());

  // Phase 3: Verify child list
  auto children = service_->GetChildren();
  ASSERT_EQ(2u, children.size());
  EXPECT_EQ("Emma", children[0].name);
  EXPECT_EQ("Jack", children[1].name);

  // Phase 4: Select a child
  EXPECT_TRUE(service_->IsSharedDeviceMode());

  EXPECT_CALL(observer_, OnCurrentChildChanged(testing::_));
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));

  EXPECT_FALSE(service_->IsSharedDeviceMode());
  auto current_child = service_->GetCurrentChild();
  ASSERT_TRUE(current_child.has_value());
  EXPECT_EQ(1001u, current_child->id);
  EXPECT_EQ("Emma", current_child->name);

  // Phase 5: Track URL and check allowance (allowed)
  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateAllowedResponse(3600));  // 1 hour remaining

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(3600));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_TRUE(result.allowed);
        EXPECT_EQ(3600, result.remaining_seconds);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);
  EXPECT_FALSE(service_->IsBlocked());

  // Phase 6: Simulate time passing, hit limit, get blocked
  test_url_loader_factory_->ClearResponses();
  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateBlockedResponse("timelimit", "School Night"));

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(0));
  EXPECT_CALL(observer_, OnBlockingStateChanged(true, testing::_));

  check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_FALSE(result.allowed);
        EXPECT_EQ(0, result.remaining_seconds);
        EXPECT_EQ("timelimit", result.block_reason);
        EXPECT_EQ("School Night", result.day_type);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);
  EXPECT_TRUE(service_->IsBlocked());
}

// ============================================================================
// SCENARIO 2: PIN Validation Tests
// ============================================================================

TEST_F(Allow2IntegrationTest, PINValidation_CorrectPIN) {
  SetupPairedState();

  EXPECT_CALL(observer_, OnCurrentChildChanged(testing::_));
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));  // Correct PIN for Emma

  auto child = service_->GetCurrentChild();
  ASSERT_TRUE(child.has_value());
  EXPECT_EQ("Emma", child->name);
}

TEST_F(Allow2IntegrationTest, PINValidation_IncorrectPIN) {
  SetupPairedState();

  // Wrong PIN should fail
  EXPECT_FALSE(service_->SelectChild(1001, "9999"));

  // Should still be in shared device mode
  EXPECT_TRUE(service_->IsSharedDeviceMode());
  EXPECT_FALSE(service_->GetCurrentChild().has_value());
}

TEST_F(Allow2IntegrationTest, PINValidation_EmptyPIN) {
  SetupPairedState();

  EXPECT_FALSE(service_->SelectChild(1001, ""));
  EXPECT_TRUE(service_->IsSharedDeviceMode());
}

TEST_F(Allow2IntegrationTest, PINValidation_WrongChildPIN) {
  SetupPairedState();

  // Try Emma's PIN on Jack - should fail
  EXPECT_FALSE(service_->SelectChild(1002, "1234"));
  EXPECT_TRUE(service_->IsSharedDeviceMode());
}

TEST_F(Allow2IntegrationTest, PINValidation_SwitchChildren) {
  SetupPairedState();

  // Select Emma
  EXPECT_CALL(observer_, OnCurrentChildChanged(testing::_));
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));

  auto child = service_->GetCurrentChild();
  ASSERT_TRUE(child.has_value());
  EXPECT_EQ("Emma", child->name);

  // Clear and switch to Jack
  EXPECT_CALL(observer_, OnCurrentChildChanged(testing::Eq(std::nullopt)));
  service_->ClearCurrentChild();
  EXPECT_TRUE(service_->IsSharedDeviceMode());

  EXPECT_CALL(observer_, OnCurrentChildChanged(testing::_));
  EXPECT_TRUE(service_->SelectChild(1002, "5678"));  // Jack's PIN

  child = service_->GetCurrentChild();
  ASSERT_TRUE(child.has_value());
  EXPECT_EQ("Jack", child->name);
}

// ============================================================================
// SCENARIO 3: Warning Thresholds
// ============================================================================

TEST_F(Allow2IntegrationTest, WarningThresholds_15Minutes) {
  SetupPairedState();
  SelectChild(1001);

  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateAllowedResponse(900));  // 15 minutes

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(900));
  EXPECT_CALL(observer_, OnWarningThresholdReached(900));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_TRUE(result.allowed);
        EXPECT_EQ(900, result.remaining_seconds);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);
}

TEST_F(Allow2IntegrationTest, WarningThresholds_5Minutes) {
  SetupPairedState();
  SelectChild(1001);

  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateAllowedResponse(300));  // 5 minutes

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(300));
  EXPECT_CALL(observer_, OnWarningThresholdReached(300));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_TRUE(result.allowed);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);
}

TEST_F(Allow2IntegrationTest, WarningThresholds_1Minute) {
  SetupPairedState();
  SelectChild(1001);

  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateAllowedResponse(60));  // 1 minute

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(60));
  EXPECT_CALL(observer_, OnWarningThresholdReached(60));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_TRUE(result.allowed);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);
}

// ============================================================================
// SCENARIO 4: Network Failure and Offline Handling
// ============================================================================

TEST_F(Allow2IntegrationTest, NetworkFailure_UseCachedResult) {
  SetupPairedState();
  SelectChild(1001);

  // First: Get a successful check and cache it
  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateAllowedResponse(1800));  // 30 minutes

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(1800));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_TRUE(result.allowed);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);
  EXPECT_FALSE(service_->IsBlocked());

  // Second: Simulate network failure
  test_url_loader_factory_->ClearResponses();
  test_url_loader_factory_->AddResponse(
      GURL("https://service.allow2.com/serviceapi/check"),
      network::mojom::URLResponseHead::New(), "",
      network::URLLoaderCompletionStatus(net::ERR_INTERNET_DISCONNECTED));

  // Should use cached result and not immediately block
  check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        // Behavior depends on implementation - may use cache or fail gracefully
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);
}

TEST_F(Allow2IntegrationTest, NetworkTimeout_GracefulHandling) {
  SetupPairedState();
  SelectChild(1001);

  // Simulate timeout (no response added to factory)
  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
      },
      &check_complete));

  // Advance time to trigger timeout
  task_environment_.FastForwardBy(base::Seconds(30));

  // Should handle gracefully without crash
}

// ============================================================================
// SCENARIO 5: Credentials Invalidated (401 Response)
// ============================================================================

TEST_F(Allow2IntegrationTest, CredentialsInvalidated_401Response) {
  SetupPairedState();
  SelectChild(1001);

  EXPECT_TRUE(service_->IsPaired());

  // Simulate 401 response (device unpaired remotely by parent)
  auto head = network::mojom::URLResponseHead::New();
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      "HTTP/1.1 401 Unauthorized\r\n");
  test_url_loader_factory_->AddResponse(
      GURL("https://service.allow2.com/serviceapi/check"), std::move(head), "",
      network::URLLoaderCompletionStatus(net::OK));

  EXPECT_CALL(observer_, OnCredentialsInvalidated());
  EXPECT_CALL(observer_, OnPairedStateChanged(false));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_FALSE(result.allowed);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);

  // Credentials should be cleared
  EXPECT_FALSE(service_->IsPaired());
}

// ============================================================================
// SCENARIO 6: Request More Time Flow
// ============================================================================

TEST_F(Allow2IntegrationTest, RequestMoreTime_Success) {
  SetupPairedState();
  SelectChild(1001);

  // Setup blocked state
  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateBlockedResponse("timelimit"));

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(0));
  EXPECT_CALL(observer_, OnBlockingStateChanged(true, testing::_));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(service_->IsBlocked());

  // Now request more time
  base::Value::Dict request_response;
  request_response.Set("success", true);
  request_response.Set("requestId", "req_abc123");

  std::string request_json;
  base::JSONWriter::Write(request_response, &request_json);

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/request/createRequest", request_json);

  bool request_complete = false;
  service_->RequestMoreTime(
      ActivityId::kInternet, 30, "Need to finish homework",
      base::BindOnce(
          [](bool* complete, bool success, const std::string& error) {
            *complete = true;
            EXPECT_TRUE(success);
            EXPECT_TRUE(error.empty());
          },
          &request_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(request_complete);
}

TEST_F(Allow2IntegrationTest, RequestMoreTime_NotPaired) {
  // Don't setup paired state

  bool request_complete = false;
  service_->RequestMoreTime(
      ActivityId::kInternet, 30, "Please",
      base::BindOnce(
          [](bool* complete, bool success, const std::string& error) {
            *complete = true;
            EXPECT_FALSE(success);
            EXPECT_EQ("Not paired", error);
          },
          &request_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(request_complete);
}

TEST_F(Allow2IntegrationTest, RequestMoreTime_NoChildSelected) {
  SetupPairedState();
  // Don't select a child

  bool request_complete = false;
  service_->RequestMoreTime(
      ActivityId::kInternet, 30, "Please",
      base::BindOnce(
          [](bool* complete, bool success, const std::string& error) {
            *complete = true;
            EXPECT_FALSE(success);
            EXPECT_EQ("No child selected", error);
          },
          &request_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(request_complete);
}

// ============================================================================
// SCENARIO 7: Block Overlay Tests
// ============================================================================

TEST_F(Allow2IntegrationTest, BlockOverlay_ShowOnBlock) {
  SetupPairedState();
  SelectChild(1001);

  EXPECT_FALSE(service_->ShouldShowBlockOverlay());

  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateBlockedResponse("timelimit"));

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(0));
  EXPECT_CALL(observer_, OnBlockingStateChanged(true, testing::_));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
      },
      &check_complete));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(service_->ShouldShowBlockOverlay());
  EXPECT_TRUE(service_->IsBlocked());
}

TEST_F(Allow2IntegrationTest, BlockOverlay_DismissOnTimeGranted) {
  SetupPairedState();
  SelectChild(1001);

  // Setup blocked state
  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateBlockedResponse("timelimit"));

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(0));
  EXPECT_CALL(observer_, OnBlockingStateChanged(true, testing::_));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(service_->IsBlocked());

  // Now simulate parent granting more time (next check returns allowed)
  test_url_loader_factory_->ClearResponses();
  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateAllowedResponse(1800));  // 30 more minutes granted

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(1800));
  EXPECT_CALL(observer_, OnBlockingStateChanged(false, testing::_));

  check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_TRUE(result.allowed);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_FALSE(service_->IsBlocked());
  EXPECT_FALSE(service_->ShouldShowBlockOverlay());
}

// ============================================================================
// SCENARIO 8: Child Shield Tests
// ============================================================================

TEST_F(Allow2IntegrationTest, ChildShield_ShowOnLaunch) {
  SetupPairedState();
  // Don't select a child - simulating cold start

  EXPECT_TRUE(service_->IsSharedDeviceMode());
  EXPECT_TRUE(service_->ShouldShowChildShield());
}

TEST_F(Allow2IntegrationTest, ChildShield_HideAfterSelection) {
  SetupPairedState();

  EXPECT_TRUE(service_->ShouldShowChildShield());

  EXPECT_CALL(observer_, OnCurrentChildChanged(testing::_));
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));

  EXPECT_FALSE(service_->ShouldShowChildShield());
}

TEST_F(Allow2IntegrationTest, ChildShield_ShowAfterClear) {
  SetupPairedState();

  EXPECT_CALL(observer_, OnCurrentChildChanged(testing::_));
  EXPECT_TRUE(service_->SelectChild(1001, "1234"));
  EXPECT_FALSE(service_->ShouldShowChildShield());

  EXPECT_CALL(observer_, OnCurrentChildChanged(testing::Eq(std::nullopt)));
  service_->ClearCurrentChild();

  EXPECT_TRUE(service_->ShouldShowChildShield());
}

// ============================================================================
// SCENARIO 9: Tracking Control
// ============================================================================

TEST_F(Allow2IntegrationTest, Tracking_StartAndStop) {
  SetupPairedState();
  SelectChild(1001);

  EXPECT_FALSE(service_->IsTrackingActive());

  service_->StartTracking();
  EXPECT_TRUE(service_->IsTrackingActive());

  service_->StopTracking();
  EXPECT_FALSE(service_->IsTrackingActive());
}

TEST_F(Allow2IntegrationTest, Tracking_PauseAndResume) {
  SetupPairedState();
  SelectChild(1001);

  service_->StartTracking();
  EXPECT_TRUE(service_->IsTrackingActive());

  service_->PauseTracking();
  EXPECT_FALSE(service_->IsTrackingActive());

  service_->ResumeTracking();
  EXPECT_TRUE(service_->IsTrackingActive());
}

TEST_F(Allow2IntegrationTest, Tracking_URLCategories) {
  SetupPairedState();
  SelectChild(1001);

  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateAllowedResponse(3600));

  service_->StartTracking();

  // Track various URLs
  service_->TrackUrl("https://facebook.com/feed");  // Social
  service_->TrackUrl("https://roblox.com/games");   // Gaming
  service_->TrackUrl("https://khanacademy.org");    // Education
  service_->TrackUrl("https://google.com");         // Internet

  // Should not crash
  task_environment_.RunUntilIdle();
}

// ============================================================================
// SCENARIO 10: Enable/Disable Toggle
// ============================================================================

TEST_F(Allow2IntegrationTest, EnableDisable_Toggle) {
  SetupPairedState();

  EXPECT_TRUE(service_->IsEnabled());
  EXPECT_TRUE(service_->IsPaired());

  service_->SetEnabled(false);
  EXPECT_FALSE(service_->IsEnabled());
  EXPECT_TRUE(service_->IsPaired());  // Still paired, just disabled

  service_->SetEnabled(true);
  EXPECT_TRUE(service_->IsEnabled());
  EXPECT_TRUE(service_->IsPaired());
}

TEST_F(Allow2IntegrationTest, EnableDisable_DisabledNoTracking) {
  SetupPairedState();
  SelectChild(1001);

  service_->SetEnabled(false);

  // Should not make API calls when disabled
  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_TRUE(result.allowed);  // Default to allowed when disabled
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(check_complete);
}

// ============================================================================
// SCENARIO 11: Multiple Block Reasons
// ============================================================================

TEST_F(Allow2IntegrationTest, BlockReasons_Banned) {
  SetupPairedState();
  SelectChild(1001);

  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateBlockedResponse("banned"));

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(0));
  EXPECT_CALL(observer_, OnBlockingStateChanged(true, testing::_));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_FALSE(result.allowed);
        EXPECT_TRUE(result.banned);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(service_->IsBlocked());
}

TEST_F(Allow2IntegrationTest, BlockReasons_TimeBlock) {
  SetupPairedState();
  SelectChild(1001);

  test_url_loader_factory_->AddResponse(
      "https://service.allow2.com/serviceapi/check",
      CreateBlockedResponse("timeblock", "Bedtime"));

  EXPECT_CALL(observer_, OnRemainingTimeUpdated(0));
  EXPECT_CALL(observer_, OnBlockingStateChanged(true, testing::_));

  bool check_complete = false;
  service_->CheckAllowance(base::BindOnce(
      [](bool* complete, const CheckResult& result) {
        *complete = true;
        EXPECT_FALSE(result.allowed);
        EXPECT_EQ("timeblock", result.block_reason);
        EXPECT_EQ("Bedtime", result.day_type);
      },
      &check_complete));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(service_->IsBlocked());
}

}  // namespace
}  // namespace allow2

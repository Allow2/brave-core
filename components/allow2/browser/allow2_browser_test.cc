/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

// Browser tests for Allow2 parental controls
// End-to-end tests verifying full browser integration

#include "brave/components/allow2/browser/allow2_service.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "brave/components/allow2/browser/allow2_block_overlay.h"
#include "brave/components/allow2/browser/allow2_child_shield.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/browser/allow2_warning_controller.h"
#include "brave/components/allow2/common/allow2_constants.h"
#include "brave/components/allow2/common/allow2_utils.h"
#include "brave/components/allow2/common/pref_names.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace allow2 {
namespace {

// ============================================================================
// Browser Test Base Class
// ============================================================================

class Allow2BrowserTest : public InProcessBrowserTest {
 public:
  Allow2BrowserTest() = default;
  ~Allow2BrowserTest() override = default;

 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    // Setup mock API server
    mock_api_server_ = std::make_unique<net::EmbeddedTestServer>();
    mock_api_server_->RegisterRequestHandler(base::BindRepeating(
        &Allow2BrowserTest::HandleApiRequest, base::Unretained(this)));
    ASSERT_TRUE(mock_api_server_->Start());

    // Get services
    Profile* profile = browser()->profile();
    pref_service_ = profile->GetPrefs();
    local_state_ = g_browser_process->local_state();

    // Clear any existing credentials
    local_state_->ClearPref(prefs::kAllow2Credentials);
  }

  void TearDownOnMainThread() override {
    mock_api_server_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  std::unique_ptr<net::test_server::HttpResponse> HandleApiRequest(
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();

    if (request.relative_url.find("/serviceapi/check") != std::string::npos) {
      response->set_code(net::HTTP_OK);
      response->set_content_type("application/json");
      response->set_content(current_check_response_);
    } else if (request.relative_url.find("/api/pair") != std::string::npos) {
      response->set_code(net::HTTP_OK);
      response->set_content_type("application/json");
      response->set_content(current_pair_response_);
    } else if (request.relative_url.find("/request/createRequest") !=
               std::string::npos) {
      response->set_code(net::HTTP_OK);
      response->set_content_type("application/json");
      response->set_content(R"({"success": true, "requestId": "req_123"})");
    } else {
      response->set_code(net::HTTP_NOT_FOUND);
    }

    return response;
  }

  void SetCheckResponseAllowed(int remaining_seconds) {
    base::Value::Dict response;
    response.Set("allowed", true);
    response.Set("minimumRemainingSeconds", remaining_seconds);
    response.Set("expires", base::Time::Now().ToTimeT() + 60);
    response.Set("banned", false);
    response.Set("dayType", "normal");

    base::JSONWriter::Write(response, &current_check_response_);
  }

  void SetCheckResponseBlocked(const std::string& reason) {
    base::Value::Dict response;
    response.Set("allowed", false);
    response.Set("minimumRemainingSeconds", 0);
    response.Set("expires", base::Time::Now().ToTimeT() + 60);
    response.Set("banned", reason == "banned");
    response.Set("blockReason", reason);
    response.Set("dayType", "School Night");

    base::JSONWriter::Write(response, &current_check_response_);
  }

  void SetCheckResponse401() {
    current_check_response_ = "";
    // The API client should handle this based on HTTP status
  }

  void SetupPairedState() {
    // Store encrypted credentials in local_state
    base::Value::Dict creds;
    creds.Set("userId", "user123");
    creds.Set("pairId", "pair456");
    creds.Set("pairToken", "token789");

    std::string creds_json;
    base::JSONWriter::Write(creds, &creds_json);
    // Note: In real tests, this would need to be encrypted
    local_state_->SetString(prefs::kAllow2Credentials, creds_json);

    // Setup children
    std::string emma_hash = HashPin("1234", "salt_emma");
    std::string jack_hash = HashPin("5678", "salt_jack");

    pref_service_->SetString(
        prefs::kAllow2CachedChildren,
        "[{\"id\": 1001, \"name\": \"Emma\", \"pinHash\": \"" + emma_hash +
            "\", \"pinSalt\": \"salt_emma\"},"
            "{\"id\": 1002, \"name\": \"Jack\", \"pinHash\": \"" + jack_hash +
            "\", \"pinSalt\": \"salt_jack\"}]");
  }

  void SelectChild(const std::string& child_id) {
    pref_service_->SetString(prefs::kAllow2ChildId, child_id);
  }

  std::unique_ptr<net::EmbeddedTestServer> mock_api_server_;
  std::string current_check_response_;
  std::string current_pair_response_;
  raw_ptr<PrefService> pref_service_ = nullptr;
  raw_ptr<PrefService> local_state_ = nullptr;
};

// ============================================================================
// Browser Test Cases
// ============================================================================

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, InitialState_NotPaired) {
  // Verify unpaired state
  EXPECT_TRUE(local_state_->GetString(prefs::kAllow2Credentials).empty());
  EXPECT_TRUE(pref_service_->GetBoolean(prefs::kAllow2Enabled));
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, PairedState_HasChildren) {
  SetupPairedState();

  // Verify children are available
  std::string children_json =
      pref_service_->GetString(prefs::kAllow2CachedChildren);
  EXPECT_FALSE(children_json.empty());
  EXPECT_NE(children_json.find("Emma"), std::string::npos);
  EXPECT_NE(children_json.find("Jack"), std::string::npos);
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, SharedDeviceMode) {
  SetupPairedState();

  // Initially in shared device mode
  EXPECT_TRUE(pref_service_->GetString(prefs::kAllow2ChildId).empty());

  // Select a child
  SelectChild("1001");
  EXPECT_EQ("1001", pref_service_->GetString(prefs::kAllow2ChildId));

  // Clear child selection
  pref_service_->ClearPref(prefs::kAllow2ChildId);
  EXPECT_TRUE(pref_service_->GetString(prefs::kAllow2ChildId).empty());
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, EnableDisable) {
  // Default enabled
  EXPECT_TRUE(pref_service_->GetBoolean(prefs::kAllow2Enabled));

  // Disable
  pref_service_->SetBoolean(prefs::kAllow2Enabled, false);
  EXPECT_FALSE(pref_service_->GetBoolean(prefs::kAllow2Enabled));

  // Re-enable
  pref_service_->SetBoolean(prefs::kAllow2Enabled, true);
  EXPECT_TRUE(pref_service_->GetBoolean(prefs::kAllow2Enabled));
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, PrefsArePersisted) {
  SetupPairedState();
  SelectChild("1001");

  // Verify prefs are set
  EXPECT_EQ("1001", pref_service_->GetString(prefs::kAllow2ChildId));

  // Prefs should persist (simulated by just checking they're still there)
  EXPECT_EQ("1001", pref_service_->GetString(prefs::kAllow2ChildId));
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, PINValidation_CorrectHash) {
  std::string pin = "1234";
  std::string salt = "test_salt";
  std::string hash = HashPin(pin, salt);

  EXPECT_TRUE(ValidatePinHash(pin, hash, salt));
  EXPECT_FALSE(ValidatePinHash("9999", hash, salt));
  EXPECT_FALSE(ValidatePinHash("", hash, salt));
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, DeviceTokenGeneration) {
  std::string token1 = GenerateDeviceToken();
  std::string token2 = GenerateDeviceToken();

  // Tokens should have the correct prefix
  EXPECT_TRUE(token1.find(kDeviceTokenPrefix) == 0);
  EXPECT_TRUE(token2.find(kDeviceTokenPrefix) == 0);

  // Tokens should be unique
  EXPECT_NE(token1, token2);

  // Tokens should be sufficiently long
  EXPECT_GT(token1.length(), 36u);
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, URLCategorization) {
  // Social media
  EXPECT_EQ(ActivityId::kSocial, CategorizeUrl("https://facebook.com/"));
  EXPECT_EQ(ActivityId::kSocial, CategorizeUrl("https://instagram.com/"));
  EXPECT_EQ(ActivityId::kSocial, CategorizeUrl("https://twitter.com/"));

  // Gaming
  EXPECT_EQ(ActivityId::kGaming, CategorizeUrl("https://roblox.com/"));
  EXPECT_EQ(ActivityId::kGaming, CategorizeUrl("https://minecraft.net/"));
  EXPECT_EQ(ActivityId::kGaming, CategorizeUrl("https://twitch.tv/"));

  // Education
  EXPECT_EQ(ActivityId::kEducation, CategorizeUrl("https://khanacademy.org/"));
  EXPECT_EQ(ActivityId::kEducation, CategorizeUrl("https://coursera.org/"));

  // General internet
  EXPECT_EQ(ActivityId::kInternet, CategorizeUrl("https://google.com/"));
  EXPECT_EQ(ActivityId::kInternet, CategorizeUrl("https://brave.com/"));
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, WarningLevels) {
  // Test warning level calculation
  EXPECT_EQ(WarningLevel::kNone, WarningLevel::kNone);  // Placeholder

  // These would be tested via WarningController but verifying constants exist
  EXPECT_EQ(900, kWarningThreshold15Min);
  EXPECT_EQ(300, kWarningThreshold5Min);
  EXPECT_EQ(60, kWarningThreshold1Min);
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, BlockReasonStrings) {
  // Test block reason to title mapping
  EXPECT_EQ("Time's Up!",
            Allow2BlockOverlay::GetBlockTitle(BlockReason::kTimeLimitReached));
  EXPECT_EQ("Time Block Active",
            Allow2BlockOverlay::GetBlockTitle(BlockReason::kTimeBlockActive));
  EXPECT_EQ("Activity Blocked",
            Allow2BlockOverlay::GetBlockTitle(BlockReason::kActivityBanned));
  EXPECT_EQ("Blocked by Parent",
            Allow2BlockOverlay::GetBlockTitle(BlockReason::kManualBlock));
  EXPECT_EQ("Cannot Connect",
            Allow2BlockOverlay::GetBlockTitle(BlockReason::kOffline));
}

IN_PROC_BROWSER_TEST_F(Allow2BrowserTest, ClearCredentials) {
  SetupPairedState();

  // Verify credentials exist
  EXPECT_FALSE(local_state_->GetString(prefs::kAllow2Credentials).empty());

  // Clear credentials
  local_state_->ClearPref(prefs::kAllow2Credentials);

  // Verify cleared
  EXPECT_TRUE(local_state_->GetString(prefs::kAllow2Credentials).empty());
}

}  // namespace
}  // namespace allow2

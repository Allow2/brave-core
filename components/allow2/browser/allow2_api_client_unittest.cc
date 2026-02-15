/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_api_client.h"

#include "base/json/json_writer.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

class Allow2ApiClientTest : public testing::Test {
 protected:
  void SetUp() override {
    test_url_loader_factory_ =
        std::make_unique<network::TestURLLoaderFactory>();
    shared_url_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            test_url_loader_factory_.get());
    api_client_ =
        std::make_unique<Allow2ApiClient>(shared_url_loader_factory_);
  }

  void TearDown() override { api_client_.reset(); }

  std::string CreateSuccessfulCheckResponse(bool allowed,
                                            int remaining_seconds = 3600) {
    base::Value::Dict response;
    response.Set("allowed", allowed);
    response.Set("minimumRemainingSeconds", remaining_seconds);
    response.Set("expires", 1234567890);
    response.Set("banned", false);
    response.Set("dayType", "normal");

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  std::string CreateBlockedCheckResponse(const std::string& reason) {
    base::Value::Dict response;
    response.Set("allowed", false);
    response.Set("minimumRemainingSeconds", 0);
    response.Set("expires", 1234567890);
    response.Set("banned", reason == "banned");
    response.Set("dayType", "normal");
    response.Set("blockReason", reason);

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  std::string CreateErrorResponse(const std::string& error_message) {
    base::Value::Dict response;
    response.Set("success", false);
    response.Set("error", error_message);

    std::string json;
    base::JSONWriter::Write(response, &json);
    return json;
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<network::TestURLLoaderFactory> test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
  std::unique_ptr<Allow2ApiClient> api_client_;
};

// ============================================================================
// Check Tests
// ============================================================================

TEST_F(Allow2ApiClientTest, Check_Allowed) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities = {ActivityId::kInternet};

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/serviceapi/check",
      CreateSuccessfulCheckResponse(true, 3600));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_TRUE(captured_response.result.allowed);
  EXPECT_EQ(3600, captured_response.result.remaining_seconds);
  EXPECT_FALSE(captured_response.result.banned);
}

TEST_F(Allow2ApiClientTest, Check_BlockedTimeLimit) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities = {ActivityId::kInternet};

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/serviceapi/check",
      CreateBlockedCheckResponse("timelimit"));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_FALSE(captured_response.result.allowed);
  EXPECT_EQ(0, captured_response.result.remaining_seconds);
  EXPECT_EQ("timelimit", captured_response.result.block_reason);
}

TEST_F(Allow2ApiClientTest, Check_BlockedBanned) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities = {ActivityId::kInternet};

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/serviceapi/check",
      CreateBlockedCheckResponse("banned"));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_FALSE(captured_response.result.allowed);
  EXPECT_TRUE(captured_response.result.banned);
  EXPECT_EQ("banned", captured_response.result.block_reason);
}

TEST_F(Allow2ApiClientTest, Check_BlockedTimeBlock) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities = {ActivityId::kInternet};

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/serviceapi/check",
      CreateBlockedCheckResponse("timeblock"));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_FALSE(captured_response.result.allowed);
  EXPECT_EQ("timeblock", captured_response.result.block_reason);
}

TEST_F(Allow2ApiClientTest, Check_401Unauthorized) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities = {ActivityId::kInternet};

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  // Simulate 401 response (device unpaired remotely)
  auto head = network::mojom::URLResponseHead::New();
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      "HTTP/1.1 401 Unauthorized\r\n");
  test_url_loader_factory_->AddResponse(
      GURL("https://api.allow2.com/serviceapi/check"), std::move(head), "",
      network::URLLoaderCompletionStatus(net::OK));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(captured_response.success);
  EXPECT_EQ(401, captured_response.http_status_code);
}

TEST_F(Allow2ApiClientTest, Check_NetworkError) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities = {ActivityId::kInternet};

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  test_url_loader_factory_->AddResponse(
      GURL("https://api.allow2.com/serviceapi/check"),
      network::mojom::URLResponseHead::New(), "",
      network::URLLoaderCompletionStatus(net::ERR_INTERNET_DISCONNECTED));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(captured_response.success);
  EXPECT_FALSE(captured_response.error.empty());
}

// ============================================================================
// Request More Time Tests
// ============================================================================

TEST_F(Allow2ApiClientTest, RequestTime_Success) {
  bool callback_called = false;
  RequestTimeResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  api_client_->RequestTime(
      creds, 1001, ActivityId::kInternet, 30, "Please give me more time",
      base::BindOnce(
          [](bool* called, RequestTimeResponse* captured,
             const RequestTimeResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  base::Value::Dict response;
  response.Set("success", true);
  response.Set("requestId", "req_123");

  std::string json;
  base::JSONWriter::Write(response, &json);

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/request/createRequest", json);

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_EQ("req_123", captured_response.request_id);
}

TEST_F(Allow2ApiClientTest, RequestTime_Unauthorized) {
  bool callback_called = false;
  RequestTimeResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "invalid_token";

  api_client_->RequestTime(
      creds, 1001, ActivityId::kInternet, 30, "Please",
      base::BindOnce(
          [](bool* called, RequestTimeResponse* captured,
             const RequestTimeResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  auto head = network::mojom::URLResponseHead::New();
  head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
      "HTTP/1.1 401 Unauthorized\r\n");
  test_url_loader_factory_->AddResponse(
      GURL("https://api.allow2.com/request/createRequest"), std::move(head),
      "", network::URLLoaderCompletionStatus(net::OK));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(captured_response.success);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(Allow2ApiClientTest, Check_MultipleActivities) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities = {ActivityId::kInternet,
                                        ActivityId::kGaming,
                                        ActivityId::kSocialMedia};

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/serviceapi/check",
      CreateSuccessfulCheckResponse(true, 1800));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
}

TEST_F(Allow2ApiClientTest, Check_EmptyActivities) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities;  // Empty

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  // Should still make the request
  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/serviceapi/check",
      CreateSuccessfulCheckResponse(true, 3600));

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
}

TEST_F(Allow2ApiClientTest, Check_MalformedJSON) {
  bool callback_called = false;
  CheckResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  std::vector<ActivityId> activities = {ActivityId::kInternet};

  api_client_->Check(
      creds, 1001, activities, true,
      base::BindOnce(
          [](bool* called, CheckResponse* captured,
             const CheckResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/serviceapi/check",
      "{ invalid json }");

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(captured_response.success);
}

// ============================================================================
// Child Authentication Tests
// ============================================================================

TEST_F(Allow2ApiClientTest, ChildAuthRequest_Success) {
  bool callback_called = false;
  ChildAuthRequestResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  api_client_->RequestChildAuth(
      creds, 1001, "device-uuid-123", "Family iPad - Brave",
      base::BindOnce(
          [](bool* called, ChildAuthRequestResponse* captured,
             const ChildAuthRequestResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  base::Value::Dict response;
  response.Set("requestId", "auth-req-abc123");
  response.Set("method", "push");
  response.Set("expiresIn", 60);

  std::string json;
  base::JSONWriter::Write(response, &json);

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/api/auth/child/request", json);

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_EQ("auth-req-abc123", captured_response.request_id);
  EXPECT_EQ("push", captured_response.method);
  EXPECT_EQ(60, captured_response.expires_in);
}

TEST_F(Allow2ApiClientTest, ChildAuthRequest_Error) {
  bool callback_called = false;
  ChildAuthRequestResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  api_client_->RequestChildAuth(
      creds, 1001, "device-uuid-123", "Family iPad - Brave",
      base::BindOnce(
          [](bool* called, ChildAuthRequestResponse* captured,
             const ChildAuthRequestResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  base::Value::Dict response;
  response.Set("error", "Child does not have push notifications enabled");

  std::string json;
  base::JSONWriter::Write(response, &json);

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/api/auth/child/request", json);

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(captured_response.success);
  EXPECT_FALSE(captured_response.error.empty());
}

TEST_F(Allow2ApiClientTest, ChildAuthStatus_Pending) {
  bool callback_called = false;
  ChildAuthStatusResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  api_client_->CheckChildAuthStatus(
      creds, "auth-req-abc123",
      base::BindOnce(
          [](bool* called, ChildAuthStatusResponse* captured,
             const ChildAuthStatusResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  base::Value::Dict response;
  response.Set("status", "pending");
  response.Set("expiresIn", 45);

  std::string json;
  base::JSONWriter::Write(response, &json);

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/api/auth/child/status/auth-req-abc123", json);

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_EQ("pending", captured_response.status);
  EXPECT_EQ(45, captured_response.expires_in);
}

TEST_F(Allow2ApiClientTest, ChildAuthStatus_Confirmed) {
  bool callback_called = false;
  ChildAuthStatusResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  api_client_->CheckChildAuthStatus(
      creds, "auth-req-abc123",
      base::BindOnce(
          [](bool* called, ChildAuthStatusResponse* captured,
             const ChildAuthStatusResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  base::Value::Dict response;
  response.Set("status", "confirmed");
  response.Set("childId", 1001);
  response.Set("childName", "Emma");

  std::string json;
  base::JSONWriter::Write(response, &json);

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/api/auth/child/status/auth-req-abc123", json);

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_EQ("confirmed", captured_response.status);
  EXPECT_EQ(1001u, captured_response.child_id);
  EXPECT_EQ("Emma", captured_response.child_name);
}

TEST_F(Allow2ApiClientTest, ChildAuthStatus_Denied) {
  bool callback_called = false;
  ChildAuthStatusResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  api_client_->CheckChildAuthStatus(
      creds, "auth-req-abc123",
      base::BindOnce(
          [](bool* called, ChildAuthStatusResponse* captured,
             const ChildAuthStatusResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  base::Value::Dict response;
  response.Set("status", "denied");

  std::string json;
  base::JSONWriter::Write(response, &json);

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/api/auth/child/status/auth-req-abc123", json);

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_EQ("denied", captured_response.status);
}

TEST_F(Allow2ApiClientTest, ChildAuthStatus_Expired) {
  bool callback_called = false;
  ChildAuthStatusResponse captured_response;

  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  api_client_->CheckChildAuthStatus(
      creds, "auth-req-abc123",
      base::BindOnce(
          [](bool* called, ChildAuthStatusResponse* captured,
             const ChildAuthStatusResponse& response) {
            *called = true;
            *captured = response;
          },
          &callback_called, &captured_response));

  base::Value::Dict response;
  response.Set("status", "expired");

  std::string json;
  base::JSONWriter::Write(response, &json);

  test_url_loader_factory_->AddResponse(
      "https://api.allow2.com/api/auth/child/status/auth-req-abc123", json);

  task_environment_.RunUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(captured_response.success);
  EXPECT_EQ("expired", captured_response.status);
}

}  // namespace allow2

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_usage_tracker.h"

#include <memory>

#include "base/test/task_environment.h"
#include "brave/components/allow2/browser/allow2_api_client.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {
namespace {

class MockObserver : public Allow2UsageTrackerObserver {
 public:
  MOCK_METHOD(void, OnCheckResult, (const CheckResult& result), (override));
  MOCK_METHOD(void, OnTrackingStarted, (), (override));
  MOCK_METHOD(void, OnTrackingStopped, (), (override));
  MOCK_METHOD(void, OnCredentialsInvalidated, (), (override));
};

class Allow2UsageTrackerTest : public testing::Test {
 protected:
  void SetUp() override {
    api_client_ = std::make_unique<Allow2ApiClient>(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_));
    tracker_ = std::make_unique<Allow2UsageTracker>(api_client_.get());
    tracker_->AddObserver(&observer_);
  }

  void TearDown() override {
    tracker_->RemoveObserver(&observer_);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  network::TestURLLoaderFactory test_url_loader_factory_;
  std::unique_ptr<Allow2ApiClient> api_client_;
  std::unique_ptr<Allow2UsageTracker> tracker_;
  testing::StrictMock<MockObserver> observer_;
};

TEST_F(Allow2UsageTrackerTest, InitialState) {
  EXPECT_FALSE(tracker_->IsTracking());
  EXPECT_FALSE(tracker_->IsPaused());
  EXPECT_EQ(0u, tracker_->GetChildId());
}

TEST_F(Allow2UsageTrackerTest, StartTracking) {
  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  EXPECT_CALL(observer_, OnTrackingStarted());

  tracker_->StartTracking(creds, 1001);

  EXPECT_TRUE(tracker_->IsTracking());
  EXPECT_EQ(1001u, tracker_->GetChildId());
}

TEST_F(Allow2UsageTrackerTest, StopTracking) {
  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  EXPECT_CALL(observer_, OnTrackingStarted());
  tracker_->StartTracking(creds, 1001);

  EXPECT_CALL(observer_, OnTrackingStopped());
  tracker_->StopTracking();

  EXPECT_FALSE(tracker_->IsTracking());
}

TEST_F(Allow2UsageTrackerTest, PauseAndResume) {
  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  EXPECT_CALL(observer_, OnTrackingStarted());
  tracker_->StartTracking(creds, 1001);

  tracker_->PauseTracking();
  EXPECT_TRUE(tracker_->IsPaused());
  EXPECT_FALSE(tracker_->IsTracking());

  tracker_->ResumeTracking();
  EXPECT_FALSE(tracker_->IsPaused());
  EXPECT_TRUE(tracker_->IsTracking());
}

TEST_F(Allow2UsageTrackerTest, SetCheckInterval) {
  base::TimeDelta new_interval = base::Seconds(30);
  tracker_->SetCheckInterval(new_interval);
  EXPECT_EQ(new_interval, tracker_->GetCheckInterval());
}

TEST_F(Allow2UsageTrackerTest, TrackUrl) {
  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  EXPECT_CALL(observer_, OnTrackingStarted());
  tracker_->StartTracking(creds, 1001);

  // Track a social media URL
  tracker_->TrackUrl("https://facebook.com/");

  // Track a gaming URL
  tracker_->TrackUrl("https://roblox.com/games");
}

TEST_F(Allow2UsageTrackerTest, UpdateChildId) {
  Credentials creds;
  creds.user_id = "user123";
  creds.pair_id = "pair456";
  creds.pair_token = "token789";

  EXPECT_CALL(observer_, OnTrackingStarted());
  tracker_->StartTracking(creds, 1001);

  tracker_->UpdateChildId(1002);
  EXPECT_EQ(1002u, tracker_->GetChildId());
}

}  // namespace
}  // namespace allow2

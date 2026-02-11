/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_block_overlay.h"

#include <memory>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {
namespace {

class MockObserver : public Allow2BlockOverlayObserver {
 public:
  MOCK_METHOD(void, OnRequestMoreTimeClicked, (), (override));
  MOCK_METHOD(void, OnSwitchUserClicked, (), (override));
  MOCK_METHOD(void, OnWhyBlockedClicked, (), (override));
  MOCK_METHOD(void, OnOverlayDismissed, (), (override));
};

class Allow2BlockOverlayTest : public testing::Test {
 protected:
  void SetUp() override {
    overlay_ = std::make_unique<Allow2BlockOverlay>();
    overlay_->AddObserver(&observer_);
  }

  void TearDown() override {
    overlay_->RemoveObserver(&observer_);
  }

  std::unique_ptr<Allow2BlockOverlay> overlay_;
  testing::StrictMock<MockObserver> observer_;
};

TEST_F(Allow2BlockOverlayTest, InitialState) {
  EXPECT_FALSE(overlay_->IsVisible());
  EXPECT_FALSE(overlay_->IsRequestPending());
}

TEST_F(Allow2BlockOverlayTest, ShowOverlay) {
  BlockOverlayConfig config;
  config.reason = BlockReason::kTimeLimitReached;
  config.day_type = "School Night";

  overlay_->Show(config);

  EXPECT_TRUE(overlay_->IsVisible());
  EXPECT_EQ(BlockReason::kTimeLimitReached, overlay_->GetConfig().reason);
  EXPECT_EQ("School Night", overlay_->GetConfig().day_type);
}

TEST_F(Allow2BlockOverlayTest, DismissOverlay) {
  BlockOverlayConfig config;
  config.reason = BlockReason::kTimeLimitReached;
  overlay_->Show(config);

  EXPECT_CALL(observer_, OnOverlayDismissed());
  overlay_->Dismiss();

  EXPECT_FALSE(overlay_->IsVisible());
}

TEST_F(Allow2BlockOverlayTest, HandleRequestMoreTime) {
  BlockOverlayConfig config;
  overlay_->Show(config);

  bool callback_called = false;
  overlay_->SetRequestTimeCallback(
      base::BindOnce([](bool* called, int minutes, const std::string& msg) {
        *called = true;
        EXPECT_EQ(30, minutes);
        EXPECT_EQ("Need to finish homework", msg);
      }, &callback_called));

  EXPECT_CALL(observer_, OnRequestMoreTimeClicked());
  overlay_->HandleRequestMoreTime(30, "Need to finish homework");

  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(overlay_->IsRequestPending());
}

TEST_F(Allow2BlockOverlayTest, HandleSwitchUser) {
  BlockOverlayConfig config;
  overlay_->Show(config);

  EXPECT_CALL(observer_, OnSwitchUserClicked());
  overlay_->HandleSwitchUser();
}

TEST_F(Allow2BlockOverlayTest, HandleWhyBlocked) {
  BlockOverlayConfig config;
  overlay_->Show(config);

  EXPECT_CALL(observer_, OnWhyBlockedClicked());
  overlay_->HandleWhyBlocked();
}

TEST_F(Allow2BlockOverlayTest, GetBlockTitle) {
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

TEST_F(Allow2BlockOverlayTest, GetBlockSubtitle) {
  EXPECT_EQ("Internet time has ended for today (School Night)",
            Allow2BlockOverlay::GetBlockSubtitle(BlockReason::kTimeLimitReached,
                                                  "School Night"));
  EXPECT_EQ("Internet time has ended for today",
            Allow2BlockOverlay::GetBlockSubtitle(BlockReason::kTimeLimitReached,
                                                  ""));
}

TEST_F(Allow2BlockOverlayTest, ShowRequestSent) {
  BlockOverlayConfig config;
  overlay_->Show(config);

  overlay_->ShowRequestSent();
  EXPECT_TRUE(overlay_->IsRequestPending());
}

TEST_F(Allow2BlockOverlayTest, ShowRequestDenied) {
  BlockOverlayConfig config;
  overlay_->Show(config);

  overlay_->SetRequestPending(true);
  overlay_->ShowRequestDenied("Parent declined");
  EXPECT_FALSE(overlay_->IsRequestPending());
}

}  // namespace
}  // namespace allow2

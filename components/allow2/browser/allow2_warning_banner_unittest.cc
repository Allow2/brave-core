/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_warning_banner.h"

#include <memory>

#include "base/test/task_environment.h"
#include "brave/components/allow2/common/allow2_constants.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {
namespace {

class MockObserver : public Allow2WarningBannerObserver {
 public:
  MOCK_METHOD(void,
              OnBannerVisibilityChanged,
              (bool visible, BannerStyle style),
              (override));
  MOCK_METHOD(void, OnCountdownTick, (int remaining_seconds), (override));
  MOCK_METHOD(void, OnBannerDismissed, (), (override));
  MOCK_METHOD(void, OnMoreInfoClicked, (), (override));
};

class Allow2WarningBannerTest : public testing::Test {
 protected:
  void SetUp() override {
    banner_ = std::make_unique<Allow2WarningBanner>();
    banner_->AddObserver(&observer_);
  }

  void TearDown() override {
    banner_->RemoveObserver(&observer_);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<Allow2WarningBanner> banner_;
  testing::StrictMock<MockObserver> observer_;
};

TEST_F(Allow2WarningBannerTest, InitialState) {
  EXPECT_FALSE(banner_->IsVisible());
  EXPECT_EQ(BannerStyle::kNone, banner_->GetStyle());
  EXPECT_FALSE(banner_->IsCountdownActive());
}

TEST_F(Allow2WarningBannerTest, FormatRemainingTime) {
  EXPECT_EQ("15 minutes",
            Allow2WarningBanner::FormatRemainingTime(900));
  EXPECT_EQ("5 minutes",
            Allow2WarningBanner::FormatRemainingTime(300));
  EXPECT_EQ("1 minute",
            Allow2WarningBanner::FormatRemainingTime(60));
  EXPECT_EQ("45 seconds",
            Allow2WarningBanner::FormatRemainingTime(45));
  EXPECT_EQ("1 second",
            Allow2WarningBanner::FormatRemainingTime(1));
  EXPECT_EQ("0 seconds",
            Allow2WarningBanner::FormatRemainingTime(0));
}

TEST_F(Allow2WarningBannerTest, GetWarningMessage) {
  EXPECT_EQ("15 minutes of internet time remaining",
            Allow2WarningBanner::GetWarningMessage(WarningLevel::kGentle, 900,
                                                    ""));
  EXPECT_EQ("Only 5 minutes left!",
            Allow2WarningBanner::GetWarningMessage(WarningLevel::kWarning, 300,
                                                    ""));
  EXPECT_EQ("Browsing ends in: 45 seconds",
            Allow2WarningBanner::GetWarningMessage(WarningLevel::kUrgent, 45,
                                                    ""));
}

TEST_F(Allow2WarningBannerTest, GetBannerTitle) {
  EXPECT_EQ("Time Reminder",
            Allow2WarningBanner::GetBannerTitle(WarningLevel::kGentle));
  EXPECT_EQ("Time Warning",
            Allow2WarningBanner::GetBannerTitle(WarningLevel::kWarning));
  EXPECT_EQ("Time Almost Up",
            Allow2WarningBanner::GetBannerTitle(WarningLevel::kUrgent));
  EXPECT_EQ("Time's Up!",
            Allow2WarningBanner::GetBannerTitle(WarningLevel::kBlocked));
}

TEST_F(Allow2WarningBannerTest, GetBannerSubtitle) {
  EXPECT_EQ("Your internet time is running low (School Night)",
            Allow2WarningBanner::GetBannerSubtitle(WarningLevel::kGentle,
                                                    "School Night"));
  EXPECT_EQ("Make sure to save your work",
            Allow2WarningBanner::GetBannerSubtitle(WarningLevel::kWarning, ""));
}

TEST_F(Allow2WarningBannerTest, UpdateGentleWarning) {
  WarningBannerConfig config;
  config.remaining_seconds = 900;  // 15 minutes
  config.level = WarningLevel::kGentle;
  config.auto_dismiss_seconds = 0;  // Disable auto-dismiss for test

  EXPECT_CALL(observer_, OnBannerVisibilityChanged(true, BannerStyle::kGentle));

  banner_->Update(config);

  EXPECT_TRUE(banner_->IsVisible());
  EXPECT_EQ(BannerStyle::kGentle, banner_->GetStyle());
}

TEST_F(Allow2WarningBannerTest, UpdateUrgentWarning) {
  WarningBannerConfig config;
  config.remaining_seconds = 30;  // 30 seconds
  config.level = WarningLevel::kUrgent;

  EXPECT_CALL(observer_,
              OnBannerVisibilityChanged(true, BannerStyle::kCountdown));

  banner_->Update(config);

  EXPECT_TRUE(banner_->IsVisible());
  EXPECT_EQ(BannerStyle::kCountdown, banner_->GetStyle());
  EXPECT_TRUE(banner_->IsCountdownActive());
}

TEST_F(Allow2WarningBannerTest, Countdown) {
  WarningBannerConfig config;
  config.remaining_seconds = 5;
  config.level = WarningLevel::kUrgent;

  EXPECT_CALL(observer_,
              OnBannerVisibilityChanged(true, BannerStyle::kCountdown));
  banner_->Update(config);

  // Fast forward and check countdown ticks
  EXPECT_CALL(observer_, OnCountdownTick(4));
  task_environment_.FastForwardBy(base::Seconds(1));

  EXPECT_CALL(observer_, OnCountdownTick(3));
  task_environment_.FastForwardBy(base::Seconds(1));

  EXPECT_EQ(3, banner_->GetCountdownSeconds());
}

TEST_F(Allow2WarningBannerTest, HideBanner) {
  WarningBannerConfig config;
  config.remaining_seconds = 300;
  config.level = WarningLevel::kWarning;

  EXPECT_CALL(observer_, OnBannerVisibilityChanged(true, BannerStyle::kWarning));
  banner_->Update(config);

  EXPECT_CALL(observer_, OnBannerVisibilityChanged(false, BannerStyle::kWarning));
  banner_->Hide();

  EXPECT_FALSE(banner_->IsVisible());
}

TEST_F(Allow2WarningBannerTest, DismissibleBanner) {
  WarningBannerConfig config;
  config.remaining_seconds = 900;
  config.level = WarningLevel::kGentle;
  config.is_dismissible = true;
  config.auto_dismiss_seconds = 0;

  EXPECT_CALL(observer_, OnBannerVisibilityChanged(true, BannerStyle::kGentle));
  banner_->Update(config);

  EXPECT_CALL(observer_, OnBannerVisibilityChanged(false, BannerStyle::kGentle));
  EXPECT_CALL(observer_, OnBannerDismissed());
  banner_->Dismiss();
}

TEST_F(Allow2WarningBannerTest, HandleMoreInfo) {
  WarningBannerConfig config;
  config.remaining_seconds = 300;
  config.level = WarningLevel::kWarning;

  EXPECT_CALL(observer_, OnBannerVisibilityChanged(true, BannerStyle::kWarning));
  banner_->Update(config);

  EXPECT_CALL(observer_, OnMoreInfoClicked());
  banner_->HandleMoreInfo();
}

TEST_F(Allow2WarningBannerTest, HandleRequestTime) {
  bool callback_called = false;
  banner_->SetRequestTimeCallback(
      base::BindOnce([](bool* called) { *called = true; }, &callback_called));

  WarningBannerConfig config;
  config.remaining_seconds = 300;
  config.level = WarningLevel::kWarning;

  EXPECT_CALL(observer_, OnBannerVisibilityChanged(true, BannerStyle::kWarning));
  banner_->Update(config);

  banner_->HandleRequestTime();

  EXPECT_TRUE(callback_called);
}

}  // namespace
}  // namespace allow2

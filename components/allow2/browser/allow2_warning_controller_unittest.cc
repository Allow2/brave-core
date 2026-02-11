/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_warning_controller.h"

#include "base/test/task_environment.h"
#include "brave/components/allow2/common/allow2_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {

class Allow2WarningControllerTest : public testing::Test {
 protected:
  void SetUp() override {
    controller_ = std::make_unique<Allow2WarningController>();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<Allow2WarningController> controller_;
};

TEST_F(Allow2WarningControllerTest, InitialState) {
  EXPECT_EQ(WarningLevel::kNone, controller_->GetCurrentLevel());
  EXPECT_EQ(0, controller_->GetRemainingSeconds());
  EXPECT_FALSE(controller_->IsCountdownActive());
}

TEST_F(Allow2WarningControllerTest, LevelCalculation_Plenty) {
  controller_->UpdateRemainingTime(3600);  // 1 hour
  EXPECT_EQ(WarningLevel::kNone, controller_->GetCurrentLevel());
}

TEST_F(Allow2WarningControllerTest, LevelCalculation_Gentle) {
  controller_->UpdateRemainingTime(kWarningThreshold15Min);
  EXPECT_EQ(WarningLevel::kGentle, controller_->GetCurrentLevel());

  controller_->UpdateRemainingTime(600);  // 10 minutes
  EXPECT_EQ(WarningLevel::kGentle, controller_->GetCurrentLevel());
}

TEST_F(Allow2WarningControllerTest, LevelCalculation_Warning) {
  controller_->UpdateRemainingTime(kWarningThreshold5Min);
  EXPECT_EQ(WarningLevel::kWarning, controller_->GetCurrentLevel());

  controller_->UpdateRemainingTime(180);  // 3 minutes
  EXPECT_EQ(WarningLevel::kWarning, controller_->GetCurrentLevel());
}

TEST_F(Allow2WarningControllerTest, LevelCalculation_Urgent) {
  controller_->UpdateRemainingTime(kWarningThreshold1Min);
  EXPECT_EQ(WarningLevel::kUrgent, controller_->GetCurrentLevel());

  controller_->UpdateRemainingTime(30);
  EXPECT_EQ(WarningLevel::kUrgent, controller_->GetCurrentLevel());
}

TEST_F(Allow2WarningControllerTest, LevelCalculation_Blocked) {
  controller_->UpdateRemainingTime(0);
  EXPECT_EQ(WarningLevel::kBlocked, controller_->GetCurrentLevel());

  controller_->UpdateRemainingTime(-5);  // Negative (overdue)
  EXPECT_EQ(WarningLevel::kBlocked, controller_->GetCurrentLevel());
}

TEST_F(Allow2WarningControllerTest, CountdownStartsAtOneMinute) {
  controller_->UpdateRemainingTime(120);  // 2 minutes
  EXPECT_FALSE(controller_->IsCountdownActive());

  controller_->UpdateRemainingTime(60);  // 1 minute
  EXPECT_TRUE(controller_->IsCountdownActive());

  controller_->UpdateRemainingTime(30);  // 30 seconds
  EXPECT_TRUE(controller_->IsCountdownActive());
}

TEST_F(Allow2WarningControllerTest, CountdownStopsAboveOneMinute) {
  controller_->UpdateRemainingTime(50);
  EXPECT_TRUE(controller_->IsCountdownActive());

  controller_->UpdateRemainingTime(120);  // Back above 1 minute
  EXPECT_FALSE(controller_->IsCountdownActive());
}

TEST_F(Allow2WarningControllerTest, WarningCallback) {
  std::vector<std::pair<WarningLevel, int>> received_warnings;

  controller_->SetWarningCallback(
      base::BindRepeating([](std::vector<std::pair<WarningLevel, int>>* out,
                             WarningLevel level, int seconds) {
        out->push_back({level, seconds});
      }, &received_warnings));

  // Transition from None to Gentle
  controller_->UpdateRemainingTime(900);
  ASSERT_EQ(1u, received_warnings.size());
  EXPECT_EQ(WarningLevel::kGentle, received_warnings[0].first);

  // Transition from Gentle to Warning
  controller_->UpdateRemainingTime(300);
  ASSERT_EQ(2u, received_warnings.size());
  EXPECT_EQ(WarningLevel::kWarning, received_warnings[1].first);

  // Transition from Warning to Urgent
  controller_->UpdateRemainingTime(60);
  ASSERT_EQ(3u, received_warnings.size());
  EXPECT_EQ(WarningLevel::kUrgent, received_warnings[2].first);
}

TEST_F(Allow2WarningControllerTest, NoCallbackForSameLevel) {
  int callback_count = 0;

  controller_->SetWarningCallback(
      base::BindRepeating([](int* count, WarningLevel, int) {
        (*count)++;
      }, &callback_count));

  controller_->UpdateRemainingTime(900);  // Gentle
  EXPECT_EQ(1, callback_count);

  controller_->UpdateRemainingTime(800);  // Still Gentle
  EXPECT_EQ(1, callback_count);  // No new callback

  controller_->UpdateRemainingTime(700);  // Still Gentle
  EXPECT_EQ(1, callback_count);  // No new callback
}

TEST_F(Allow2WarningControllerTest, BlockCallback) {
  bool blocked = false;
  std::string block_reason;

  controller_->SetBlockCallback(
      base::BindOnce([](bool* blocked, std::string* reason,
                        const std::string& r) {
        *blocked = true;
        *reason = r;
      }, &blocked, &block_reason));

  controller_->UpdateRemainingTime(0);
  EXPECT_TRUE(blocked);
  EXPECT_EQ("Time's up", block_reason);
}

TEST_F(Allow2WarningControllerTest, TriggerBlock) {
  bool blocked = false;

  controller_->SetBlockCallback(
      base::BindOnce([](bool* blocked, const std::string&) {
        *blocked = true;
      }, &blocked));

  controller_->TriggerBlock("Custom reason");
  EXPECT_TRUE(blocked);
  EXPECT_EQ(WarningLevel::kBlocked, controller_->GetCurrentLevel());
  EXPECT_FALSE(controller_->IsCountdownActive());
}

TEST_F(Allow2WarningControllerTest, Reset) {
  controller_->UpdateRemainingTime(30);
  EXPECT_EQ(WarningLevel::kUrgent, controller_->GetCurrentLevel());
  EXPECT_TRUE(controller_->IsCountdownActive());

  controller_->Reset();
  EXPECT_EQ(WarningLevel::kNone, controller_->GetCurrentLevel());
  EXPECT_EQ(0, controller_->GetRemainingSeconds());
  EXPECT_FALSE(controller_->IsCountdownActive());
}

TEST_F(Allow2WarningControllerTest, CountdownCallback) {
  std::vector<int> countdown_values;

  controller_->SetCountdownCallback(
      base::BindRepeating([](std::vector<int>* out, int seconds) {
        out->push_back(seconds);
      }, &countdown_values));

  controller_->UpdateRemainingTime(5);  // 5 seconds

  // Advance time to trigger countdown ticks
  for (int i = 0; i < 3; i++) {
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Should have received countdown callbacks
  EXPECT_GE(countdown_values.size(), 2u);
  if (countdown_values.size() >= 2) {
    // Values should be decreasing
    EXPECT_GT(countdown_values[0], countdown_values[1]);
  }
}

TEST_F(Allow2WarningControllerTest, StopCountdown) {
  controller_->UpdateRemainingTime(30);
  EXPECT_TRUE(controller_->IsCountdownActive());

  controller_->StopCountdown();
  EXPECT_FALSE(controller_->IsCountdownActive());
}

}  // namespace allow2

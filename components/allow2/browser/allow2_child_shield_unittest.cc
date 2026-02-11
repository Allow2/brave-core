/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_child_shield.h"

#include <memory>

#include "base/test/task_environment.h"
#include "brave/components/allow2/browser/allow2_child_manager.h"
#include "brave/components/allow2/common/allow2_utils.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {
namespace {

class MockObserver : public Allow2ChildShieldObserver {
 public:
  MOCK_METHOD(void, OnShieldStateChanged, (ShieldState state), (override));
  MOCK_METHOD(void, OnChildSelected, (uint64_t child_id), (override));
  MOCK_METHOD(void, OnPINAccepted, (uint64_t child_id), (override));
  MOCK_METHOD(void, OnPINRejected, (const std::string& error), (override));
  MOCK_METHOD(void, OnShieldDismissed, (), (override));
  MOCK_METHOD(void, OnLockoutStarted, (int lockout_seconds), (override));
};

class Allow2ChildShieldTest : public testing::Test {
 protected:
  void SetUp() override {
    RegisterProfilePrefs(pref_service_.registry());
    child_manager_ = std::make_unique<Allow2ChildManager>(&pref_service_);
    shield_ = std::make_unique<Allow2ChildShield>(child_manager_.get());
    shield_->AddObserver(&observer_);

    // Add test children
    std::vector<ChildInfo> children = {
        CreateChild(1001, "Emma", "1234"),
        CreateChild(1002, "Jack", "5678"),
    };
    child_manager_->UpdateChildList(children);
  }

  void TearDown() override {
    shield_->RemoveObserver(&observer_);
  }

  ChildInfo CreateChild(uint64_t id,
                        const std::string& name,
                        const std::string& pin) {
    ChildInfo child;
    child.id = id;
    child.name = name;
    child.pin_salt = "testsalt";
    child.pin_hash = HashPin(pin, child.pin_salt);
    return child;
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple pref_service_;
  std::unique_ptr<Allow2ChildManager> child_manager_;
  std::unique_ptr<Allow2ChildShield> shield_;
  testing::StrictMock<MockObserver> observer_;
};

TEST_F(Allow2ChildShieldTest, InitialState) {
  EXPECT_FALSE(shield_->IsVisible());
  EXPECT_EQ(ShieldState::kHidden, shield_->GetState());
}

TEST_F(Allow2ChildShieldTest, ShowShield) {
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSelectingChild));

  shield_->Show();

  EXPECT_TRUE(shield_->IsVisible());
  EXPECT_EQ(ShieldState::kSelectingChild, shield_->GetState());
}

TEST_F(Allow2ChildShieldTest, GetChildren) {
  auto children = shield_->GetChildren();
  EXPECT_EQ(2u, children.size());
  EXPECT_EQ("Emma", children[0].name);
  EXPECT_EQ("Jack", children[1].name);
}

TEST_F(Allow2ChildShieldTest, SelectChild) {
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSelectingChild));
  shield_->Show();

  EXPECT_CALL(observer_, OnChildSelected(1001));
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kEnteringPIN));

  shield_->SelectChild(1001);

  EXPECT_EQ(ShieldState::kEnteringPIN, shield_->GetState());
  EXPECT_EQ(1001u, shield_->GetSelectedChildId());
  EXPECT_EQ("Emma", shield_->GetSelectedChildName());
}

TEST_F(Allow2ChildShieldTest, GoBackToSelection) {
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSelectingChild));
  shield_->Show();

  EXPECT_CALL(observer_, OnChildSelected(1001));
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kEnteringPIN));
  shield_->SelectChild(1001);

  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSelectingChild));
  shield_->GoBackToSelection();

  EXPECT_EQ(ShieldState::kSelectingChild, shield_->GetState());
  EXPECT_EQ(0u, shield_->GetSelectedChildId());
}

TEST_F(Allow2ChildShieldTest, SubmitCorrectPIN) {
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSelectingChild));
  shield_->Show();

  EXPECT_CALL(observer_, OnChildSelected(1001));
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kEnteringPIN));
  shield_->SelectChild(1001);

  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kValidating));
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSuccess));
  EXPECT_CALL(observer_, OnPINAccepted(1001));
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kHidden));
  EXPECT_CALL(observer_, OnShieldDismissed());

  shield_->SubmitPIN("1234");

  EXPECT_FALSE(shield_->IsVisible());
}

TEST_F(Allow2ChildShieldTest, SubmitWrongPIN) {
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSelectingChild));
  shield_->Show();

  EXPECT_CALL(observer_, OnChildSelected(1001));
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kEnteringPIN));
  shield_->SelectChild(1001);

  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kValidating));
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kError));
  EXPECT_CALL(observer_, OnPINRejected(testing::_));

  shield_->SubmitPIN("wrong");

  EXPECT_EQ(ShieldState::kError, shield_->GetState());
  EXPECT_FALSE(shield_->GetError().empty());
}

TEST_F(Allow2ChildShieldTest, DismissShield) {
  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSelectingChild));
  shield_->Show();

  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kHidden));
  EXPECT_CALL(observer_, OnShieldDismissed());

  shield_->Dismiss();

  EXPECT_FALSE(shield_->IsVisible());
}

TEST_F(Allow2ChildShieldTest, ShowWithConfig) {
  ChildShieldConfig config;
  config.allow_guest = true;
  config.show_skip_link = true;
  config.custom_title = "Custom Title";

  EXPECT_CALL(observer_, OnShieldStateChanged(ShieldState::kSelectingChild));
  shield_->Show(config);

  EXPECT_TRUE(shield_->GetConfig().allow_guest);
  EXPECT_TRUE(shield_->GetConfig().show_skip_link);
  EXPECT_EQ("Custom Title", shield_->GetConfig().custom_title);
}

}  // namespace
}  // namespace allow2

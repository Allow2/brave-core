/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_pairing_handler.h"

#include <memory>

#include "base/test/task_environment.h"
#include "brave/components/allow2/browser/allow2_api_client.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/common/allow2_utils.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/os_crypt/sync/os_crypt_mocker.h"
#include "components/prefs/testing_pref_service.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace allow2 {
namespace {

class MockObserver : public Allow2PairingHandlerObserver {
 public:
  MOCK_METHOD(void, OnPairingStateChanged, (PairingState state), (override));
  MOCK_METHOD(void, OnQRCodeReady, (const std::string& qr_code_url), (override));
  MOCK_METHOD(void, OnPINCodeReady, (const std::string& pin_code), (override));
  MOCK_METHOD(void,
              OnPairingCompleted,
              (const std::vector<Child>& children),
              (override));
  MOCK_METHOD(void, OnPairingFailed, (const std::string& error), (override));
  MOCK_METHOD(void, OnPairingExpired, (), (override));
  MOCK_METHOD(void, OnQRCodeScanned, (), (override));
};

class Allow2PairingHandlerTest : public testing::Test {
 protected:
  void SetUp() override {
    OSCryptMocker::SetUp();
    RegisterLocalStatePrefs(local_state_.registry());

    api_client_ = std::make_unique<Allow2ApiClient>(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_));
    credential_manager_ =
        std::make_unique<Allow2CredentialManager>(&local_state_);
    handler_ = std::make_unique<Allow2PairingHandler>(api_client_.get(),
                                                       credential_manager_.get());
    handler_->AddObserver(&observer_);
  }

  void TearDown() override {
    handler_->RemoveObserver(&observer_);
    OSCryptMocker::TearDown();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  network::TestURLLoaderFactory test_url_loader_factory_;
  TestingPrefServiceSimple local_state_;
  std::unique_ptr<Allow2ApiClient> api_client_;
  std::unique_ptr<Allow2CredentialManager> credential_manager_;
  std::unique_ptr<Allow2PairingHandler> handler_;
  testing::StrictMock<MockObserver> observer_;
};

TEST_F(Allow2PairingHandlerTest, InitialState) {
  EXPECT_EQ(PairingState::kIdle, handler_->GetState());
  EXPECT_EQ(PairingMode::kNone, handler_->GetMode());
  EXPECT_FALSE(handler_->IsPairingInProgress());
  EXPECT_TRUE(handler_->GetQRCodeUrl().empty());
  EXPECT_TRUE(handler_->GetPINCode().empty());
}

TEST_F(Allow2PairingHandlerTest, StartQRPairingTransitionsToInitializing) {
  EXPECT_CALL(observer_, OnPairingStateChanged(PairingState::kInitializing));

  handler_->StartQRPairing("Test Device");

  EXPECT_EQ(PairingState::kInitializing, handler_->GetState());
  EXPECT_EQ(PairingMode::kQRCode, handler_->GetMode());
  EXPECT_TRUE(handler_->IsPairingInProgress());
}

TEST_F(Allow2PairingHandlerTest, StartPINPairingTransitionsToInitializing) {
  EXPECT_CALL(observer_, OnPairingStateChanged(PairingState::kInitializing));

  handler_->StartPINPairing("Test Device");

  EXPECT_EQ(PairingState::kInitializing, handler_->GetState());
  EXPECT_EQ(PairingMode::kPINCode, handler_->GetMode());
  EXPECT_TRUE(handler_->IsPairingInProgress());
}

TEST_F(Allow2PairingHandlerTest, CancelPairing) {
  EXPECT_CALL(observer_, OnPairingStateChanged(PairingState::kInitializing));
  handler_->StartQRPairing("Test Device");

  EXPECT_CALL(observer_, OnPairingStateChanged(PairingState::kIdle));
  handler_->CancelPairing();

  EXPECT_EQ(PairingState::kIdle, handler_->GetState());
  EXPECT_FALSE(handler_->IsPairingInProgress());
}

TEST_F(Allow2PairingHandlerTest, DeviceTokenIsGenerated) {
  EXPECT_TRUE(credential_manager_->GetDeviceToken().empty());

  EXPECT_CALL(observer_, OnPairingStateChanged(PairingState::kInitializing));
  handler_->StartQRPairing("Test Device");

  EXPECT_FALSE(credential_manager_->GetDeviceToken().empty());
  EXPECT_EQ("Test Device", credential_manager_->GetDeviceName());
}

TEST_F(Allow2PairingHandlerTest, ReusesExistingDeviceToken) {
  credential_manager_->SetDeviceToken("existing_token");

  EXPECT_CALL(observer_, OnPairingStateChanged(PairingState::kInitializing));
  handler_->StartQRPairing("Test Device");

  EXPECT_EQ("existing_token", credential_manager_->GetDeviceToken());
}

}  // namespace
}  // namespace allow2

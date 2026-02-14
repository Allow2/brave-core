/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/settings/brave_parental_freedom_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/values.h"
#include "brave/browser/allow2/allow2_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

namespace allow2 {

BraveParentalFreedomHandler::BraveParentalFreedomHandler() = default;

BraveParentalFreedomHandler::~BraveParentalFreedomHandler() {
  if (auto* service = GetService()) {
    service->RemoveObserver(this);
  }
}

// static
void BraveParentalFreedomHandler::AddLoadTimeData(
    content::WebUIDataSource* data_source,
    Profile* profile) {
  // Add any load-time data needed by the settings page.
  auto* service = Allow2ServiceFactory::GetForProfileIfExists(profile);
  data_source->AddBoolean("allow2Enabled", service ? service->IsEnabled() : false);
}

void BraveParentalFreedomHandler::RegisterMessages() {
  profile_ = Profile::FromWebUI(web_ui());

  web_ui()->RegisterMessageCallback(
      "getPairingStatus",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleGetPairingStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "initQRPairing",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleInitQRPairing,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "initPINPairing",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleInitPINPairing,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "checkPairingStatus",
      base::BindRepeating(
          &BraveParentalFreedomHandler::HandleCheckPairingStatus,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelPairing",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleCancelPairing,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "selectChild",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleSelectChild,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clearCurrentChild",
      base::BindRepeating(
          &BraveParentalFreedomHandler::HandleClearCurrentChild,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getAllow2Enabled",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleGetAllow2Enabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setAllow2Enabled",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleSetAllow2Enabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getBlockingStatus",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleGetBlockingStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestMoreTime",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleRequestMoreTime,
                          base::Unretained(this)));
}

void BraveParentalFreedomHandler::OnJavascriptAllowed() {
  if (auto* service = GetService()) {
    service->AddObserver(this);
  }
}

void BraveParentalFreedomHandler::OnJavascriptDisallowed() {
  if (auto* service = GetService()) {
    service->RemoveObserver(this);
  }
}

// Allow2ServiceObserver overrides
void BraveParentalFreedomHandler::OnPairedStateChanged(bool is_paired) {
  FirePairingStateChanged();
}

void BraveParentalFreedomHandler::OnBlockingStateChanged(
    bool is_blocked,
    const std::string& reason) {
  FireBlockingStateChanged();
}

void BraveParentalFreedomHandler::OnRemainingTimeUpdated(
    int remaining_seconds) {
  FireBlockingStateChanged();
}

void BraveParentalFreedomHandler::OnCurrentChildChanged(
    const std::optional<Child>& child) {
  FirePairingStateChanged();
}

void BraveParentalFreedomHandler::OnCredentialsInvalidated() {
  FirePairingStateChanged();
}

void BraveParentalFreedomHandler::HandleGetPairingStatus(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 1u);
  const std::string& callback_id = args[0].GetString();
  AllowJavascript();

  auto* service = GetService();
  base::Value::Dict result;

  if (!service) {
    result.Set("isPaired", false);
    result.Set("children", base::Value::List());
    result.Set("currentChild", base::Value());
    ResolveJavascriptCallback(base::Value(callback_id), result);
    return;
  }

  result.Set("isPaired", service->IsPaired());

  // Build children list
  base::Value::List children_list;
  for (const auto& child : service->GetChildren()) {
    base::Value::Dict child_dict;
    child_dict.Set("id", static_cast<double>(child.id));
    child_dict.Set("name", child.name);
    children_list.Append(std::move(child_dict));
  }
  result.Set("children", std::move(children_list));

  // Current child
  auto current_child = service->GetCurrentChild();
  if (current_child) {
    base::Value::Dict child_dict;
    child_dict.Set("id", static_cast<double>(current_child->id));
    child_dict.Set("name", current_child->name);
    result.Set("currentChild", std::move(child_dict));
  } else {
    result.Set("currentChild", base::Value());
  }

  ResolveJavascriptCallback(base::Value(callback_id), result);
}

void BraveParentalFreedomHandler::HandleInitQRPairing(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 2u);
  const std::string& callback_id = args[0].GetString();
  const std::string& device_name = args[1].GetString();
  AllowJavascript();

  auto* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id), result);
    return;
  }

  service->InitQRPairing(
      device_name,
      base::BindOnce(&BraveParentalFreedomHandler::OnInitPairingComplete,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void BraveParentalFreedomHandler::HandleInitPINPairing(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 2u);
  const std::string& callback_id = args[0].GetString();
  const std::string& device_name = args[1].GetString();
  AllowJavascript();

  auto* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id), result);
    return;
  }

  service->InitPINPairing(
      device_name,
      base::BindOnce(&BraveParentalFreedomHandler::OnInitPairingComplete,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void BraveParentalFreedomHandler::HandleCheckPairingStatus(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 2u);
  const std::string& callback_id = args[0].GetString();
  const std::string& session_id = args[1].GetString();
  AllowJavascript();

  auto* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("completed", true);
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id), result);
    return;
  }

  service->CheckPairingStatus(
      session_id,
      base::BindOnce(&BraveParentalFreedomHandler::OnCheckPairingStatusComplete,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void BraveParentalFreedomHandler::HandleCancelPairing(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 1u);
  const std::string& session_id = args[0].GetString();

  auto* service = GetService();
  if (service) {
    service->CancelPairing(session_id);
  }
}

void BraveParentalFreedomHandler::HandleSelectChild(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 3u);
  const std::string& callback_id = args[0].GetString();
  uint64_t child_id = static_cast<uint64_t>(args[1].GetDouble());
  const std::string& pin = args[2].GetString();
  AllowJavascript();

  auto* service = GetService();
  base::Value::Dict result;

  if (!service) {
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id), result);
    return;
  }

  bool success = service->SelectChild(child_id, pin);
  result.Set("success", success);
  if (!success) {
    result.Set("error", "Invalid PIN");
  }

  ResolveJavascriptCallback(base::Value(callback_id), result);
}

void BraveParentalFreedomHandler::HandleClearCurrentChild(
    const base::Value::List& args) {
  auto* service = GetService();
  if (service) {
    service->ClearCurrentChild();
  }
}

void BraveParentalFreedomHandler::HandleGetAllow2Enabled(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 1u);
  const std::string& callback_id = args[0].GetString();
  AllowJavascript();

  auto* service = GetService();
  bool enabled = service ? service->IsEnabled() : false;

  ResolveJavascriptCallback(base::Value(callback_id), base::Value(enabled));
}

void BraveParentalFreedomHandler::HandleSetAllow2Enabled(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 1u);
  bool enabled = args[0].GetBool();

  auto* service = GetService();
  if (service) {
    service->SetEnabled(enabled);
  }
}

void BraveParentalFreedomHandler::HandleGetBlockingStatus(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 1u);
  const std::string& callback_id = args[0].GetString();
  AllowJavascript();

  auto* service = GetService();
  base::Value::Dict result;

  if (!service) {
    result.Set("isBlocked", false);
    result.Set("reason", "");
    result.Set("remainingSeconds", 0);
    ResolveJavascriptCallback(base::Value(callback_id), result);
    return;
  }

  result.Set("isBlocked", service->IsBlocked());
  result.Set("reason", service->GetBlockReason());
  result.Set("remainingSeconds", service->GetRemainingSeconds());

  ResolveJavascriptCallback(base::Value(callback_id), result);
}

void BraveParentalFreedomHandler::HandleRequestMoreTime(
    const base::Value::List& args) {
  CHECK_EQ(args.size(), 3u);
  const std::string& callback_id = args[0].GetString();
  int minutes = args[1].GetInt();
  const std::string& message = args[2].GetString();
  AllowJavascript();

  auto* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id), result);
    return;
  }

  service->RequestMoreTime(
      ActivityId::kInternet, minutes, message,
      base::BindOnce(&BraveParentalFreedomHandler::OnRequestMoreTimeComplete,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void BraveParentalFreedomHandler::OnInitPairingComplete(
    const std::string& callback_id,
    bool success,
    const Allow2Service::PairingSession& session,
    const std::string& error) {
  base::Value::Dict result;
  result.Set("success", success);

  if (success) {
    result.Set("sessionId", session.session_id);
    result.Set("qrCodeUrl", session.qr_code_data);
    result.Set("webPairingUrl", session.web_pairing_url);
    result.Set("pinCode", session.pin_code);
  } else {
    result.Set("error", error);
  }

  ResolveJavascriptCallback(base::Value(callback_id), result);
}

void BraveParentalFreedomHandler::OnCheckPairingStatusComplete(
    const std::string& callback_id,
    bool completed,
    bool success,
    const std::string& error) {
  base::Value::Dict result;
  result.Set("completed", completed);
  result.Set("success", success);
  if (!error.empty()) {
    result.Set("error", error);
  }

  ResolveJavascriptCallback(base::Value(callback_id), result);
}

void BraveParentalFreedomHandler::OnRequestMoreTimeComplete(
    const std::string& callback_id,
    bool success,
    const std::string& error) {
  base::Value::Dict result;
  result.Set("success", success);
  if (!error.empty()) {
    result.Set("error", error);
  }

  ResolveJavascriptCallback(base::Value(callback_id), result);
}

Allow2Service* BraveParentalFreedomHandler::GetService() {
  if (!profile_) {
    return nullptr;
  }
  return Allow2ServiceFactory::GetForProfile(profile_);
}

void BraveParentalFreedomHandler::FirePairingStateChanged() {
  FireWebUIListener("pairing-state-changed");
}

void BraveParentalFreedomHandler::FireBlockingStateChanged() {
  FireWebUIListener("blocking-state-changed");
}

}  // namespace allow2

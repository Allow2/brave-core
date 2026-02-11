/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/settings/brave_parental_freedom_handler.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "brave/browser/allow2/allow2_service_factory.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "brave/components/allow2/common/pref_names.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

namespace allow2 {

namespace {

// Convert Child struct to base::Value::Dict for JavaScript.
base::Value::Dict ChildToDict(const Child& child) {
  base::Value::Dict dict;
  dict.Set("id", base::NumberToString(child.id));
  dict.Set("name", child.name);
  return dict;
}

// Convert list of children to base::Value::List.
base::Value::List ChildrenToList(const std::vector<Child>& children) {
  base::Value::List list;
  for (const auto& child : children) {
    list.Append(ChildToDict(child));
  }
  return list;
}

}  // namespace

BraveParentalFreedomHandler::BraveParentalFreedomHandler() = default;

BraveParentalFreedomHandler::~BraveParentalFreedomHandler() {
  if (Allow2Service* service = GetService()) {
    service->RemoveObserver(this);
  }
}

// static
void BraveParentalFreedomHandler::AddLoadTimeData(
    content::WebUIDataSource* data_source,
    Profile* profile) {
  DCHECK(data_source);
  DCHECK(profile);

  // Add Allow2-related strings for the settings page.
  data_source->AddBoolean("allow2Enabled",
                          profile->GetPrefs()->GetBoolean(prefs::kAllow2Enabled));

  // Check if device is paired by looking for stored children.
  const std::string& cached_children =
      profile->GetPrefs()->GetString(prefs::kAllow2CachedChildren);
  data_source->AddBoolean("allow2IsPaired", !cached_children.empty());
}

void BraveParentalFreedomHandler::RegisterMessages() {
  profile_ = Profile::FromWebUI(web_ui());

  web_ui()->RegisterMessageCallback(
      "allow2GetPairingStatus",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleGetPairingStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2InitQRPairing",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleInitQRPairing,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2InitPINPairing",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleInitPINPairing,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2CheckPairingStatus",
      base::BindRepeating(
          &BraveParentalFreedomHandler::HandleCheckPairingStatus,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2CancelPairing",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleCancelPairing,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2SelectChild",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleSelectChild,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2ClearCurrentChild",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleClearCurrentChild,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2GetEnabled",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleGetAllow2Enabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2SetEnabled",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleSetAllow2Enabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2GetBlockingStatus",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleGetBlockingStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "allow2RequestMoreTime",
      base::BindRepeating(&BraveParentalFreedomHandler::HandleRequestMoreTime,
                          base::Unretained(this)));
}

void BraveParentalFreedomHandler::OnJavascriptAllowed() {
  if (Allow2Service* service = GetService()) {
    service->AddObserver(this);
  }

  // Register for pref changes.
  pref_change_registrar_.Init(profile_->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kAllow2Enabled,
      base::BindRepeating(&BraveParentalFreedomHandler::FirePairingStateChanged,
                          base::Unretained(this)));
}

void BraveParentalFreedomHandler::OnJavascriptDisallowed() {
  if (Allow2Service* service = GetService()) {
    service->RemoveObserver(this);
  }
  pref_change_registrar_.RemoveAll();
}

// Allow2ServiceObserver implementation.

void BraveParentalFreedomHandler::OnPairedStateChanged(bool is_paired) {
  FirePairingStateChanged();
}

void BraveParentalFreedomHandler::OnBlockingStateChanged(
    bool is_blocked,
    const std::string& reason) {
  FireBlockingStateChanged();
}

void BraveParentalFreedomHandler::OnRemainingTimeUpdated(int remaining_seconds) {
  if (IsJavascriptAllowed()) {
    FireWebUIListener("allow2-remaining-time-changed",
                      base::Value(remaining_seconds));
  }
}

void BraveParentalFreedomHandler::OnCurrentChildChanged(
    const std::optional<Child>& child) {
  FirePairingStateChanged();
}

void BraveParentalFreedomHandler::OnCredentialsInvalidated() {
  FirePairingStateChanged();
}

// Message handlers.

void BraveParentalFreedomHandler::HandleGetPairingStatus(
    const base::Value::List& args) {
  CHECK_EQ(1U, args.size());
  const std::string& callback_id = args[0].GetString();

  AllowJavascript();

  base::Value::Dict result;

  Allow2Service* service = GetService();
  if (!service) {
    result.Set("isPaired", false);
    result.Set("children", base::Value::List());
    result.Set("currentChild", base::Value());
    result.Set("isEnabled", false);
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(std::move(result)));
    return;
  }

  result.Set("isPaired", service->IsPaired());
  result.Set("isEnabled", service->IsEnabled());
  result.Set("children", ChildrenToList(service->GetChildren()));

  std::optional<Child> current_child = service->GetCurrentChild();
  if (current_child.has_value()) {
    result.Set("currentChild", ChildToDict(current_child.value()));
  } else {
    result.Set("currentChild", base::Value());
  }

  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(std::move(result)));
}

void BraveParentalFreedomHandler::HandleInitQRPairing(
    const base::Value::List& args) {
  CHECK_EQ(2U, args.size());
  const std::string& callback_id = args[0].GetString();
  const std::string& device_name = args[1].GetString();

  AllowJavascript();

  Allow2Service* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(std::move(result)));
    return;
  }

  service->InitQRPairing(
      device_name,
      base::BindOnce(&BraveParentalFreedomHandler::OnInitPairingComplete,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void BraveParentalFreedomHandler::HandleInitPINPairing(
    const base::Value::List& args) {
  CHECK_EQ(2U, args.size());
  const std::string& callback_id = args[0].GetString();
  const std::string& device_name = args[1].GetString();

  AllowJavascript();

  Allow2Service* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(std::move(result)));
    return;
  }

  service->InitPINPairing(
      device_name,
      base::BindOnce(&BraveParentalFreedomHandler::OnInitPairingComplete,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void BraveParentalFreedomHandler::OnInitPairingComplete(
    const std::string& callback_id,
    bool success,
    const Allow2Service::PairingSession& session,
    const std::string& error) {
  if (!IsJavascriptAllowed()) {
    return;
  }

  base::Value::Dict result;
  result.Set("success", success);
  if (success) {
    result.Set("sessionId", session.session_id);
    result.Set("qrCodeUrl", session.qr_code_url);
    result.Set("pinCode", session.pin_code);
  } else {
    result.Set("error", error);
  }

  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(std::move(result)));
}

void BraveParentalFreedomHandler::HandleCheckPairingStatus(
    const base::Value::List& args) {
  CHECK_EQ(2U, args.size());
  const std::string& callback_id = args[0].GetString();
  const std::string& session_id = args[1].GetString();

  AllowJavascript();

  Allow2Service* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("completed", false);
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(std::move(result)));
    return;
  }

  service->CheckPairingStatus(
      session_id,
      base::BindOnce(&BraveParentalFreedomHandler::OnCheckPairingStatusComplete,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void BraveParentalFreedomHandler::OnCheckPairingStatusComplete(
    const std::string& callback_id,
    bool completed,
    bool success,
    const std::string& error) {
  if (!IsJavascriptAllowed()) {
    return;
  }

  base::Value::Dict result;
  result.Set("completed", completed);
  result.Set("success", success);
  if (!error.empty()) {
    result.Set("error", error);
  }

  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(std::move(result)));
}

void BraveParentalFreedomHandler::HandleCancelPairing(
    const base::Value::List& args) {
  CHECK_EQ(1U, args.size());
  const std::string& session_id = args[0].GetString();

  if (Allow2Service* service = GetService()) {
    service->CancelPairing(session_id);
  }
}

void BraveParentalFreedomHandler::HandleSelectChild(
    const base::Value::List& args) {
  CHECK_EQ(3U, args.size());
  const std::string& callback_id = args[0].GetString();
  const std::string& child_id_str = args[1].GetString();
  const std::string& pin = args[2].GetString();

  AllowJavascript();

  uint64_t child_id = 0;
  if (!base::StringToUint64(child_id_str, &child_id)) {
    base::Value::Dict result;
    result.Set("success", false);
    result.Set("error", "Invalid child ID");
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(std::move(result)));
    return;
  }

  Allow2Service* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(std::move(result)));
    return;
  }

  bool success = service->SelectChild(child_id, pin);

  base::Value::Dict result;
  result.Set("success", success);
  if (!success) {
    result.Set("error", "Invalid PIN");
  }
  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(std::move(result)));
}

void BraveParentalFreedomHandler::HandleClearCurrentChild(
    const base::Value::List& args) {
  if (Allow2Service* service = GetService()) {
    service->ClearCurrentChild();
  }
}

void BraveParentalFreedomHandler::HandleGetAllow2Enabled(
    const base::Value::List& args) {
  CHECK_EQ(1U, args.size());
  const std::string& callback_id = args[0].GetString();

  AllowJavascript();

  bool enabled = false;
  if (Allow2Service* service = GetService()) {
    enabled = service->IsEnabled();
  }

  ResolveJavascriptCallback(base::Value(callback_id), base::Value(enabled));
}

void BraveParentalFreedomHandler::HandleSetAllow2Enabled(
    const base::Value::List& args) {
  CHECK_EQ(1U, args.size());
  bool enabled = args[0].GetBool();

  if (Allow2Service* service = GetService()) {
    service->SetEnabled(enabled);
  }
}

void BraveParentalFreedomHandler::HandleGetBlockingStatus(
    const base::Value::List& args) {
  CHECK_EQ(1U, args.size());
  const std::string& callback_id = args[0].GetString();

  AllowJavascript();

  base::Value::Dict result;

  Allow2Service* service = GetService();
  if (!service) {
    result.Set("isBlocked", false);
    result.Set("reason", "");
    result.Set("remainingSeconds", 0);
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(std::move(result)));
    return;
  }

  result.Set("isBlocked", service->IsBlocked());
  result.Set("reason", service->GetBlockReason());
  result.Set("remainingSeconds", service->GetRemainingSeconds());

  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(std::move(result)));
}

void BraveParentalFreedomHandler::HandleRequestMoreTime(
    const base::Value::List& args) {
  CHECK_EQ(3U, args.size());
  const std::string& callback_id = args[0].GetString();
  int minutes = args[1].GetInt();
  const std::string& message = args[2].GetString();

  AllowJavascript();

  Allow2Service* service = GetService();
  if (!service) {
    base::Value::Dict result;
    result.Set("success", false);
    result.Set("error", "Allow2 service not available");
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(std::move(result)));
    return;
  }

  service->RequestMoreTime(
      ActivityId::kInternet, minutes, message,
      base::BindOnce(&BraveParentalFreedomHandler::OnRequestMoreTimeComplete,
                     weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void BraveParentalFreedomHandler::OnRequestMoreTimeComplete(
    const std::string& callback_id,
    bool success,
    const std::string& error) {
  if (!IsJavascriptAllowed()) {
    return;
  }

  base::Value::Dict result;
  result.Set("success", success);
  if (!success) {
    result.Set("error", error);
  }

  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(std::move(result)));
}

Allow2Service* BraveParentalFreedomHandler::GetService() {
  if (!profile_) {
    return nullptr;
  }
  return Allow2ServiceFactory::GetForProfile(profile_);
}

void BraveParentalFreedomHandler::FirePairingStateChanged() {
  if (!IsJavascriptAllowed()) {
    return;
  }

  Allow2Service* service = GetService();
  if (!service) {
    return;
  }

  base::Value::Dict state;
  state.Set("isPaired", service->IsPaired());
  state.Set("isEnabled", service->IsEnabled());
  state.Set("children", ChildrenToList(service->GetChildren()));

  std::optional<Child> current_child = service->GetCurrentChild();
  if (current_child.has_value()) {
    state.Set("currentChild", ChildToDict(current_child.value()));
  } else {
    state.Set("currentChild", base::Value());
  }

  FireWebUIListener("allow2-pairing-state-changed",
                    base::Value(std::move(state)));
}

void BraveParentalFreedomHandler::FireBlockingStateChanged() {
  if (!IsJavascriptAllowed()) {
    return;
  }

  Allow2Service* service = GetService();
  if (!service) {
    return;
  }

  base::Value::Dict state;
  state.Set("isBlocked", service->IsBlocked());
  state.Set("reason", service->GetBlockReason());
  state.Set("remainingSeconds", service->GetRemainingSeconds());

  FireWebUIListener("allow2-blocking-state-changed",
                    base::Value(std::move(state)));
}

}  // namespace allow2

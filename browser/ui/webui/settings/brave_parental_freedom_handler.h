/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_WEBUI_SETTINGS_BRAVE_PARENTAL_FREEDOM_HANDLER_H_
#define BRAVE_BROWSER_UI_WEBUI_SETTINGS_BRAVE_PARENTAL_FREEDOM_HANDLER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "components/prefs/pref_change_registrar.h"

namespace content {
class WebUIDataSource;
}

class Profile;

namespace allow2 {

// WebUI handler for the "Parental Freedom" settings section.
// Manages Allow2 parental controls settings including:
// - Device pairing/unpairing status
// - Child selection for shared devices
// - Enable/disable Allow2 for this profile
// - View current status and remaining time
class BraveParentalFreedomHandler : public settings::SettingsPageUIHandler,
                                    public Allow2ServiceObserver {
 public:
  BraveParentalFreedomHandler();
  BraveParentalFreedomHandler(const BraveParentalFreedomHandler&) = delete;
  BraveParentalFreedomHandler& operator=(const BraveParentalFreedomHandler&) =
      delete;
  ~BraveParentalFreedomHandler() override;

  // Adds load-time data to the WebUI data source.
  static void AddLoadTimeData(content::WebUIDataSource* data_source,
                              Profile* profile);

 private:
  // SettingsPageUIHandler overrides:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // Allow2ServiceObserver overrides:
  void OnPairedStateChanged(bool is_paired) override;
  void OnBlockingStateChanged(bool is_blocked,
                              const std::string& reason) override;
  void OnRemainingTimeUpdated(int remaining_seconds) override;
  void OnCurrentChildChanged(const std::optional<Child>& child) override;
  void OnCredentialsInvalidated() override;

  // JavaScript message handlers.

  // Get the current pairing status.
  // Args: [callback_id]
  // Returns: { isPaired: bool, children: [...], currentChild: {...} | null }
  void HandleGetPairingStatus(const base::Value::List& args);

  // Initialize QR code pairing.
  // Args: [callback_id, device_name]
  // Returns: { success: bool, sessionId: string, qrCodeUrl: string, error: string }
  void HandleInitQRPairing(const base::Value::List& args);

  // Initialize PIN code pairing.
  // Args: [callback_id, device_name]
  // Returns: { success: bool, sessionId: string, pinCode: string, error: string }
  void HandleInitPINPairing(const base::Value::List& args);

  // Check pairing status (poll until complete).
  // Args: [callback_id, session_id]
  // Returns: { completed: bool, success: bool, error: string }
  void HandleCheckPairingStatus(const base::Value::List& args);

  // Cancel active pairing session.
  // Args: [session_id]
  void HandleCancelPairing(const base::Value::List& args);

  // Select a child for the current session.
  // Args: [callback_id, child_id, pin]
  // Returns: { success: bool, error: string }
  void HandleSelectChild(const base::Value::List& args);

  // Clear the current child selection.
  // Args: []
  void HandleClearCurrentChild(const base::Value::List& args);

  // Get/set whether Allow2 is enabled.
  // Args: [callback_id] / [enabled]
  void HandleGetAllow2Enabled(const base::Value::List& args);
  void HandleSetAllow2Enabled(const base::Value::List& args);

  // Get current blocking status.
  // Args: [callback_id]
  // Returns: { isBlocked: bool, reason: string, remainingSeconds: int }
  void HandleGetBlockingStatus(const base::Value::List& args);

  // Request more time from parent.
  // Args: [callback_id, minutes, message]
  // Returns: { success: bool, error: string }
  void HandleRequestMoreTime(const base::Value::List& args);

  // Callbacks for async operations.
  void OnInitPairingComplete(const std::string& callback_id,
                             bool success,
                             const Allow2Service::PairingSession& session,
                             const std::string& error);
  void OnCheckPairingStatusComplete(const std::string& callback_id,
                                    bool completed,
                                    bool success,
                                    const std::string& error);
  void OnRequestMoreTimeComplete(const std::string& callback_id,
                                 bool success,
                                 const std::string& error);

  // Helper to get Allow2Service.
  Allow2Service* GetService();

  // Notify WebUI of state changes.
  void FirePairingStateChanged();
  void FireBlockingStateChanged();

  raw_ptr<Profile> profile_ = nullptr;
  PrefChangeRegistrar pref_change_registrar_;

  base::WeakPtrFactory<BraveParentalFreedomHandler> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_UI_WEBUI_SETTINGS_BRAVE_PARENTAL_FREEDOM_HANDLER_H_

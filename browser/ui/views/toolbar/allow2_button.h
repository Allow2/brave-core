/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_ALLOW2_BUTTON_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_ALLOW2_BUTTON_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/menu_button_controller.h"

class Browser;
class Profile;

namespace views {
class MenuRunner;
}  // namespace views

namespace allow2 {

// Toolbar button that shows the current Allow2 user and provides
// a dropdown menu to switch users.
//
// The button displays:
// - Current user's initials in a colored circle (when authenticated)
// - "?" icon when no user is authenticated
// - Dropdown menu with "Switch User" option
class Allow2Button : public ToolbarButton,
                     public Allow2ServiceObserver {
  METADATA_HEADER(Allow2Button, ToolbarButton)

 public:
  Allow2Button(Browser* browser, Profile* profile);
  ~Allow2Button() override;

  Allow2Button(const Allow2Button&) = delete;
  Allow2Button& operator=(const Allow2Button&) = delete;

  // Update the button appearance based on current user state.
  void UpdateAppearance();

  // Check if the button should be visible (device is paired).
  bool ShouldBeVisible() const;

 private:
  // ToolbarButton overrides:
  void OnThemeChanged() override;

  // Allow2ServiceObserver overrides:
  void OnCurrentChildChanged(const std::optional<Child>& child) override;
  void OnPairedStateChanged(bool is_paired) override;

  // Button click handler.
  void OnButtonPressed(const ui::Event& event);

  // Show the dropdown menu.
  void ShowMenu();

  // Menu action handlers.
  void OnSwitchUserSelected();

  // Get the Allow2 service.
  Allow2Service* GetService() const;

  // Get initials from name for avatar display.
  static std::u16string GetInitials(const std::string& name);

  // Get color for user avatar.
  static SkColor GetAvatarColor(uint64_t child_id);

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<views::MenuButtonController> menu_button_controller_ = nullptr;

  // Current authenticated child (cached for display).
  std::optional<Child> current_child_;

  // Menu runner for the dropdown.
  std::unique_ptr<views::MenuRunner> menu_runner_;

  base::WeakPtrFactory<Allow2Button> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_ALLOW2_BUTTON_H_

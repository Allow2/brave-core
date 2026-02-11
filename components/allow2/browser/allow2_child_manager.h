/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_MANAGER_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_MANAGER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/time/time.h"

class PrefService;

namespace allow2 {

// Represents a child account with PIN information.
struct ChildInfo {
  uint64_t id = 0;
  std::string name;
  std::string pin_hash;
  std::string pin_salt;
  std::string avatar_url;  // Optional avatar URL
};

// Observer interface for child selection changes.
class Allow2ChildManagerObserver : public base::CheckedObserver {
 public:
  // Called when the current child selection changes.
  virtual void OnChildSelected(const std::optional<ChildInfo>& child) {}

  // Called when the child list is updated.
  virtual void OnChildListUpdated(const std::vector<ChildInfo>& children) {}

  // Called when PIN validation fails.
  virtual void OnPinValidationFailed(uint64_t child_id) {}

  // Called when child selection is required (shared device mode).
  virtual void OnChildSelectionRequired() {}

 protected:
  ~Allow2ChildManagerObserver() override = default;
};

// Manages child selection for shared device mode.
// Handles:
// - Child list caching
// - Child selection with PIN verification
// - Session management (when to re-prompt for child selection)
//
// SECURITY NOTES:
// - PINs are validated using constant-time comparison to prevent timing attacks
// - PIN hashes are never logged or exposed
// - Failed PIN attempts are rate-limited
class Allow2ChildManager {
 public:
  using SelectChildCallback =
      base::OnceCallback<void(bool success, const std::string& error)>;

  explicit Allow2ChildManager(PrefService* profile_prefs);
  ~Allow2ChildManager();

  Allow2ChildManager(const Allow2ChildManager&) = delete;
  Allow2ChildManager& operator=(const Allow2ChildManager&) = delete;

  // Observer management.
  void AddObserver(Allow2ChildManagerObserver* observer);
  void RemoveObserver(Allow2ChildManagerObserver* observer);

  // ============================================================================
  // Child List Management
  // ============================================================================

  // Update the cached list of children (called after pairing or refresh).
  void UpdateChildList(const std::vector<ChildInfo>& children);

  // Get the list of available children.
  std::vector<ChildInfo> GetChildren() const;

  // Get information about a specific child.
  std::optional<ChildInfo> GetChild(uint64_t child_id) const;

  // ============================================================================
  // Child Selection
  // ============================================================================

  // Check if a child is currently selected.
  bool HasSelectedChild() const;

  // Get the currently selected child.
  std::optional<ChildInfo> GetSelectedChild() const;

  // Select a child with PIN verification.
  // Returns false if PIN is incorrect or child not found.
  bool SelectChild(uint64_t child_id, const std::string& pin);

  // Async version with callback.
  void SelectChildAsync(uint64_t child_id,
                        const std::string& pin,
                        SelectChildCallback callback);

  // Clear the current child selection (switch user flow).
  void ClearSelection();

  // ============================================================================
  // Shared Device Mode
  // ============================================================================

  // Check if device is in shared mode.
  bool IsSharedDeviceMode() const;

  // Set shared device mode.
  void SetSharedDeviceMode(bool shared);

  // Check if child selection is required.
  // Returns true if in shared mode and no child selected,
  // or if the selection has timed out.
  bool IsChildSelectionRequired() const;

  // Mark the time of last user activity (for session timeout).
  void RecordActivity();

  // Check if the session has timed out (should re-prompt for child).
  bool HasSessionTimedOut() const;

  // ============================================================================
  // PIN Rate Limiting
  // ============================================================================

  // Get the number of remaining PIN attempts before lockout.
  int GetRemainingPinAttempts() const;

  // Check if PIN entry is currently locked out.
  bool IsPinLockedOut() const;

  // Get time until lockout expires.
  base::TimeDelta GetLockoutTimeRemaining() const;

  // Reset PIN attempt counter (e.g., after successful entry).
  void ResetPinAttempts();

 private:
  // Load children from preferences.
  void LoadChildrenFromPrefs();

  // Save children to preferences.
  void SaveChildrenToPrefs();

  // Validate PIN with rate limiting.
  bool ValidatePinWithRateLimit(uint64_t child_id, const std::string& pin);

  // Check if child_id matches locked child.
  bool IsLockedToChild(uint64_t child_id) const;

  raw_ptr<PrefService> profile_prefs_;

  // Cached list of children.
  std::vector<ChildInfo> children_;

  // Currently selected child ID (empty if none).
  std::optional<uint64_t> selected_child_id_;

  // Session management.
  base::Time last_activity_time_;
  bool shared_device_mode_ = true;

  // PIN rate limiting.
  int failed_pin_attempts_ = 0;
  base::Time lockout_expiry_;

  // Rate limiting constants.
  static constexpr int kMaxPinAttempts = 5;
  static constexpr int kLockoutDurationMinutes = 5;
  static constexpr int kSessionTimeoutMinutes = 5;

  base::ObserverList<Allow2ChildManagerObserver> observers_;
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_MANAGER_H_

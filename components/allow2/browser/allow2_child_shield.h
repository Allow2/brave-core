/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_SHIELD_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_SHIELD_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"

namespace allow2 {

struct ChildInfo;
class Allow2ChildManager;

// Shield state for UI display.
enum class ShieldState {
  kHidden,            // Shield not visible
  kSelectingChild,    // User is selecting which child they are
  kEnteringPIN,       // User has selected child, entering PIN
  kValidating,        // Validating PIN with server
  kError,             // Showing error (wrong PIN, locked out)
  kSuccess,           // PIN accepted, about to dismiss
};

// Observer interface for child shield events.
class Allow2ChildShieldObserver : public base::CheckedObserver {
 public:
  // Called when shield state changes.
  virtual void OnShieldStateChanged(ShieldState state) {}

  // Called when a child is selected (before PIN entry).
  virtual void OnChildSelected(uint64_t child_id) {}

  // Called when PIN validation succeeds.
  virtual void OnPINAccepted(uint64_t child_id) {}

  // Called when PIN validation fails.
  virtual void OnPINRejected(const std::string& error) {}

  // Called when shield is dismissed.
  virtual void OnShieldDismissed() {}

  // Called when lockout starts.
  virtual void OnLockoutStarted(int lockout_seconds) {}

 protected:
  ~Allow2ChildShieldObserver() override = default;
};

// Configuration for the child shield display.
struct ChildShieldConfig {
  // Whether to show avatar images.
  bool show_avatars = true;

  // Whether to include "Guest" option.
  bool allow_guest = false;

  // Whether to show "Continue without account" link.
  bool show_skip_link = false;

  // Custom title text (empty for default).
  std::string custom_title;

  // Custom subtitle text (empty for default).
  std::string custom_subtitle;
};

// Controller for the child selection shield UI.
// The shield appears when:
// - App launches (cold start) on a paired shared device
// - Resuming from background after 5+ minutes
// - User explicitly triggers "Switch User"
// - Session timeout
//
// UI Layout:
// +------------------------------------------+
// |                                          |
// |         Who's using Brave?               |
// |                                          |
// |   [Emma]    [Jack]    [Guest]            |
// |    (avatar)  (avatar)  (avatar)          |
// |                                          |
// |  ----------------------------------------|
// |                                          |
// |         Enter PIN: [* * * *]             |
// |                                          |
// |   This helps track time limits and       |
// |   keep you safe                          |
// |                                          |
// +------------------------------------------+
class Allow2ChildShield {
 public:
  using SelectCallback =
      base::OnceCallback<void(bool success, const std::string& error)>;

  explicit Allow2ChildShield(Allow2ChildManager* child_manager);
  ~Allow2ChildShield();

  Allow2ChildShield(const Allow2ChildShield&) = delete;
  Allow2ChildShield& operator=(const Allow2ChildShield&) = delete;

  // Observer management.
  void AddObserver(Allow2ChildShieldObserver* observer);
  void RemoveObserver(Allow2ChildShieldObserver* observer);

  // ============================================================================
  // Shield Control
  // ============================================================================

  // Show the child shield.
  void Show();

  // Show with custom configuration.
  void Show(const ChildShieldConfig& config);

  // Dismiss the shield.
  void Dismiss();

  // Check if shield is currently visible.
  bool IsVisible() const;

  // Get current shield state.
  ShieldState GetState() const;

  // Get the current configuration.
  const ChildShieldConfig& GetConfig() const;

  // ============================================================================
  // Child Selection
  // ============================================================================

  // Get list of children to display.
  std::vector<ChildInfo> GetChildren() const;

  // Select a child (moves to PIN entry state).
  void SelectChild(uint64_t child_id);

  // Go back to child selection (from PIN entry).
  void GoBackToSelection();

  // Get currently selected child ID (during PIN entry).
  uint64_t GetSelectedChildId() const;

  // Get name of selected child.
  std::string GetSelectedChildName() const;

  // ============================================================================
  // PIN Entry
  // ============================================================================

  // Submit PIN for the selected child.
  void SubmitPIN(const std::string& pin);

  // Submit PIN with async callback.
  void SubmitPINAsync(const std::string& pin, SelectCallback callback);

  // Clear entered PIN.
  void ClearPIN();

  // Get remaining PIN attempts.
  int GetRemainingPINAttempts() const;

  // Check if PIN entry is locked out.
  bool IsLockedOut() const;

  // Get lockout time remaining in seconds.
  int GetLockoutSecondsRemaining() const;

  // ============================================================================
  // Error Handling
  // ============================================================================

  // Show an error message.
  void ShowError(const std::string& error);

  // Clear error state.
  void ClearError();

  // Get current error message.
  std::string GetError() const;

  // ============================================================================
  // Guest Mode
  // ============================================================================

  // Select guest mode (if allowed).
  void SelectGuest();

  // Skip child selection (if allowed).
  void Skip();

 private:
  // Set state and notify observers.
  void SetState(ShieldState state);

  // Handle PIN validation result.
  void OnPINValidationResult(bool success, const std::string& error);

  // Find child by ID.
  std::optional<ChildInfo> FindChild(uint64_t child_id) const;

  raw_ptr<Allow2ChildManager> child_manager_;

  ShieldState state_ = ShieldState::kHidden;
  ChildShieldConfig config_;

  uint64_t selected_child_id_ = 0;
  std::string current_error_;

  base::ObserverList<Allow2ChildShieldObserver> observers_;
  base::WeakPtrFactory<Allow2ChildShield> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_CHILD_SHIELD_H_

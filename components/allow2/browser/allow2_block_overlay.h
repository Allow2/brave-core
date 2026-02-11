/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_BLOCK_OVERLAY_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_BLOCK_OVERLAY_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"

namespace allow2 {

// Block reason types for categorized display.
enum class BlockReason {
  kTimeLimitReached,  // Daily/activity time limit reached
  kTimeBlockActive,   // Scheduled time block (e.g., bedtime)
  kActivityBanned,    // Activity type is banned
  kManualBlock,       // Parent manually blocked
  kOffline,           // Cannot verify allowance (offline mode)
};

// Observer interface for block overlay events.
class Allow2BlockOverlayObserver : public base::CheckedObserver {
 public:
  // Called when user requests more time.
  virtual void OnRequestMoreTimeClicked() {}

  // Called when user wants to switch user.
  virtual void OnSwitchUserClicked() {}

  // Called when user wants to see why they're blocked.
  virtual void OnWhyBlockedClicked() {}

  // Called when overlay is dismissed (e.g., time granted).
  virtual void OnOverlayDismissed() {}

 protected:
  ~Allow2BlockOverlayObserver() override = default;
};

// Configuration for the block overlay display.
struct BlockOverlayConfig {
  // Primary block reason.
  BlockReason reason = BlockReason::kTimeLimitReached;

  // Human-readable reason string (e.g., "School Night", "Bedtime").
  std::string reason_text;

  // Day type name (e.g., "School Night", "Weekend").
  std::string day_type;

  // Whether the "Request More Time" button is visible.
  bool show_request_button = true;

  // Whether the "Switch User" button is visible.
  bool show_switch_user_button = true;

  // Whether the "Why am I blocked?" link is visible.
  bool show_why_blocked_link = true;

  // Custom message from parent (optional).
  std::string parent_message;

  // Time block end time (for scheduled blocks, empty if not applicable).
  std::string block_ends_at;
};

// Controller for the block overlay UI.
// This is a platform-agnostic controller that manages the state
// and logic for the block overlay. Platform-specific UI implementations
// (Views on desktop, WebUI, etc.) observe this controller.
//
// The overlay appears when:
// - Time limit is reached (remaining seconds <= 0)
// - A scheduled time block starts
// - Activity is banned
// - Parent manually blocks the device
class Allow2BlockOverlay {
 public:
  using RequestTimeCallback =
      base::OnceCallback<void(int minutes, const std::string& message)>;

  Allow2BlockOverlay();
  ~Allow2BlockOverlay();

  Allow2BlockOverlay(const Allow2BlockOverlay&) = delete;
  Allow2BlockOverlay& operator=(const Allow2BlockOverlay&) = delete;

  // Observer management.
  void AddObserver(Allow2BlockOverlayObserver* observer);
  void RemoveObserver(Allow2BlockOverlayObserver* observer);

  // ============================================================================
  // Overlay Control
  // ============================================================================

  // Show the block overlay with the given configuration.
  void Show(const BlockOverlayConfig& config);

  // Dismiss the overlay (e.g., when time is granted).
  void Dismiss();

  // Check if overlay is currently visible.
  bool IsVisible() const;

  // Get the current configuration.
  const BlockOverlayConfig& GetConfig() const;

  // ============================================================================
  // User Actions
  // ============================================================================

  // Called when user clicks "Request More Time".
  // |callback| will be invoked with the requested minutes and message.
  void SetRequestTimeCallback(RequestTimeCallback callback);

  // Handle "Request More Time" click.
  void HandleRequestMoreTime(int minutes, const std::string& message);

  // Handle "Switch User" click.
  void HandleSwitchUser();

  // Handle "Why am I blocked?" click.
  void HandleWhyBlocked();

  // ============================================================================
  // Request State
  // ============================================================================

  // Check if a time request is pending.
  bool IsRequestPending() const;

  // Set request pending state.
  void SetRequestPending(bool pending);

  // Show request sent confirmation.
  void ShowRequestSent();

  // Show request denied message.
  void ShowRequestDenied(const std::string& reason);

  // ============================================================================
  // Reason Formatting
  // ============================================================================

  // Get the main title for the block overlay.
  static std::string GetBlockTitle(BlockReason reason);

  // Get the subtitle/explanation for the block.
  static std::string GetBlockSubtitle(BlockReason reason,
                                      const std::string& day_type);

  // Get a user-friendly description of why they're blocked.
  static std::string GetBlockExplanation(BlockReason reason);

 private:
  bool is_visible_ = false;
  bool is_request_pending_ = false;

  BlockOverlayConfig config_;
  RequestTimeCallback request_callback_;

  base::ObserverList<Allow2BlockOverlayObserver> observers_;
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_BLOCK_OVERLAY_H_

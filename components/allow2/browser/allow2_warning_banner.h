/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_WARNING_BANNER_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_WARNING_BANNER_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/timer/timer.h"
#include "brave/components/allow2/browser/allow2_warning_controller.h"

namespace allow2 {

// Warning banner style/urgency level.
enum class BannerStyle {
  kNone,       // No banner
  kGentle,     // Subtle toast (15-10 min remaining)
  kWarning,    // Yellow/orange warning (5-1 min remaining)
  kUrgent,     // Red urgent banner (<1 min remaining)
  kCountdown,  // Full-width countdown (<30 sec remaining)
};

// Observer interface for warning banner events.
class Allow2WarningBannerObserver : public base::CheckedObserver {
 public:
  // Called when banner visibility or style changes.
  virtual void OnBannerVisibilityChanged(bool visible, BannerStyle style) {}

  // Called every second during countdown.
  virtual void OnCountdownTick(int remaining_seconds) {}

  // Called when user dismisses the banner (if dismissible).
  virtual void OnBannerDismissed() {}

  // Called when user clicks for more info.
  virtual void OnMoreInfoClicked() {}

 protected:
  ~Allow2WarningBannerObserver() override = default;
};

// Configuration for the warning banner display.
struct WarningBannerConfig {
  WarningBannerConfig();
  ~WarningBannerConfig();
  WarningBannerConfig(const WarningBannerConfig&);
  WarningBannerConfig& operator=(const WarningBannerConfig&);
  WarningBannerConfig(WarningBannerConfig&&);
  WarningBannerConfig& operator=(WarningBannerConfig&&);

  // Current remaining seconds.
  int remaining_seconds = 0;

  // Current warning level.
  WarningLevel level = WarningLevel::kNone;

  // Day type name for context (e.g., "School Night").
  std::string day_type;

  // Activity name (e.g., "Internet", "Gaming").
  std::string activity_name;

  // Whether the banner can be dismissed.
  bool is_dismissible = true;

  // Whether to show "Request Time" action.
  bool show_request_action = true;

  // Whether to show progress bar.
  bool show_progress_bar = true;

  // Auto-dismiss delay for gentle warnings (0 = no auto-dismiss).
  int auto_dismiss_seconds = 5;
};

// Controller for the time remaining warning banner UI.
//
// Warning progression (as defined in architecture):
// - 15 minutes: Gentle toast (auto-dismiss 5s)
// - 5 minutes: Warning - persistent toast with progress bar
// - 1 minute: Urgent - full-width banner with live countdown
// - 30 seconds: Critical - countdown with visual emphasis
// - 0 seconds: Time's up - triggers block overlay
//
// UI integration:
// - Toast style for 15/5 minute warnings (top-right corner)
// - Full-width banner for 1 minute/countdown (top of viewport)
class Allow2WarningBanner {
 public:
  using RequestTimeCallback = base::OnceCallback<void()>;

  Allow2WarningBanner();
  ~Allow2WarningBanner();

  Allow2WarningBanner(const Allow2WarningBanner&) = delete;
  Allow2WarningBanner& operator=(const Allow2WarningBanner&) = delete;

  // Observer management.
  void AddObserver(Allow2WarningBannerObserver* observer);
  void RemoveObserver(Allow2WarningBannerObserver* observer);

  // ============================================================================
  // Banner Control
  // ============================================================================

  // Update the banner with new configuration.
  void Update(const WarningBannerConfig& config);

  // Show the banner with current configuration.
  void Show();

  // Hide the banner.
  void Hide();

  // Dismiss the banner (if dismissible).
  void Dismiss();

  // Check if banner is currently visible.
  bool IsVisible() const;

  // Get current banner style.
  BannerStyle GetStyle() const;

  // Get current configuration.
  const WarningBannerConfig& GetConfig() const;

  // ============================================================================
  // Countdown
  // ============================================================================

  // Start countdown from current remaining seconds.
  void StartCountdown();

  // Stop countdown.
  void StopCountdown();

  // Check if countdown is active.
  bool IsCountdownActive() const;

  // Get current countdown value.
  int GetCountdownSeconds() const;

  // ============================================================================
  // Formatting
  // ============================================================================

  // Format remaining time for display.
  // Returns "15 minutes", "5 minutes", "1 minute", "45 seconds", etc.
  static std::string FormatRemainingTime(int remaining_seconds);

  // Get the warning message for the current level.
  static std::string GetWarningMessage(WarningLevel level,
                                       int remaining_seconds,
                                       const std::string& activity_name);

  // Get the banner title.
  static std::string GetBannerTitle(WarningLevel level);

  // Get the subtitle/description.
  static std::string GetBannerSubtitle(WarningLevel level,
                                       const std::string& day_type);

  // ============================================================================
  // Actions
  // ============================================================================

  // Set callback for "Request More Time" action.
  void SetRequestTimeCallback(RequestTimeCallback callback);

  // Handle "Request More Time" click.
  void HandleRequestTime();

  // Handle "More Info" click.
  void HandleMoreInfo();

 private:
  // Calculate banner style from warning level.
  BannerStyle CalculateStyle(WarningLevel level, int remaining_seconds) const;

  // Countdown timer tick.
  void OnCountdownTick();

  // Auto-dismiss timer callback.
  void OnAutoDismiss();

  // Update visibility based on configuration.
  void UpdateVisibility();

  // Notify observers of visibility change.
  void NotifyVisibilityChanged();

  bool is_visible_ = false;
  BannerStyle current_style_ = BannerStyle::kNone;
  WarningBannerConfig config_;

  int countdown_seconds_ = 0;
  base::RepeatingTimer countdown_timer_;
  base::OneShotTimer auto_dismiss_timer_;

  RequestTimeCallback request_callback_;

  base::ObserverList<Allow2WarningBannerObserver> observers_;
  base::WeakPtrFactory<Allow2WarningBanner> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_WARNING_BANNER_H_

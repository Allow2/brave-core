/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_CHILD_SELECT_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_CHILD_SELECT_VIEW_H_

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;

namespace views {
class Label;
class MdTextButton;
}  // namespace views

namespace allow2 {

// Number of digits in the PIN.
constexpr size_t kPinDigitCount = 4;

// Child selection shield shown on shared devices.
// This modal dialog appears:
// - On browser launch (cold start) when device is paired in shared mode
// - After resuming from background (>5 minutes)
// - When "Switch User" is explicitly selected
//
// Children must enter their PIN to select their profile and begin tracking.
class Allow2ChildSelectView : public views::DialogDelegateView,
                              public views::TextfieldController {
  METADATA_HEADER(Allow2ChildSelectView, views::DialogDelegateView)

 public:
  // Returns true if authentication succeeded (dialog should close),
  // false if PIN was invalid (dialog stays open for retry).
  // The third parameter (out) receives debug info for display on failure.
  using ChildSelectedCallback = base::RepeatingCallback<bool(
      uint64_t child_id,
      const std::string& pin,
      std::string* debug_info)>;
  using GuestCallback = base::OnceClosure;

  // Shows the child selection dialog for the given browser.
  // |owner_name| is the name of the account owner (shown instead of "Guest").
  // |service| is used to query lockout state for rate limiting.
  static void Show(Browser* browser,
                   const std::vector<Child>& children,
                   const std::string& owner_name,
                   base::WeakPtr<Allow2Service> service,
                   ChildSelectedCallback child_selected_callback,
                   GuestCallback guest_callback);

  // Returns true if a child select view is currently showing.
  static bool IsShowing();

  Allow2ChildSelectView(const Allow2ChildSelectView&) = delete;
  Allow2ChildSelectView& operator=(const Allow2ChildSelectView&) = delete;

 private:
  // Represents a clickable child button with round avatar.
  class ChildButton : public views::View {
    METADATA_HEADER(ChildButton, views::View)

   public:
    ChildButton(const Child& child,
                base::RepeatingCallback<void(uint64_t)> on_click);
    ~ChildButton() override;

    uint64_t child_id() const { return child_id_; }

    // Set selected state (highlights the button).
    void SetSelected(bool selected);

    // Set lockout state with remaining seconds.
    // When locked_out is true, button is disabled and shows countdown.
    void SetLockedOut(bool locked_out, int remaining_seconds);

    // Check if currently locked out.
    bool IsLockedOut() const { return locked_out_; }

    // views::View overrides:
    bool OnMousePressed(const ui::MouseEvent& event) override;

   private:
    // Create round avatar view with initials and color.
    std::unique_ptr<views::View> CreateAvatarView(const Child& child);

    // Get initials from name (first letter of first two words).
    static std::u16string GetInitials(const std::string& name);

    // Get color for avatar background.
    static SkColor GetAvatarColor(const Child& child);

    // Format lockout time as "Xm Ys".
    static std::u16string FormatLockoutTime(int remaining_seconds);

    uint64_t child_id_ = 0;
    bool selected_ = false;
    bool locked_out_ = false;
    base::RepeatingCallback<void(uint64_t)> on_click_;
    raw_ptr<views::View> avatar_view_ = nullptr;
    raw_ptr<views::Label> name_label_ = nullptr;
    raw_ptr<views::Label> lockout_label_ = nullptr;
  };

  // Creates and configures a PIN digit field.
  views::Textfield* CreatePinDigitField();

  Allow2ChildSelectView(Browser* browser,
                        const std::vector<Child>& children,
                        const std::string& owner_name,
                        base::WeakPtr<Allow2Service> service,
                        ChildSelectedCallback child_selected_callback,
                        GuestCallback guest_callback);
  ~Allow2ChildSelectView() override;

  // views::DialogDelegateView overrides:
  ui::mojom::ModalType GetModalType() const override;
  bool ShouldShowCloseButton() const override;
  bool ShouldShowWindowTitle() const override;

  // views::TextfieldController overrides:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // Child selection handler.
  void OnChildSelected(uint64_t child_id);

  // Button click handlers.
  void OnConfirmClicked();
  void OnGuestClicked();

  // Update UI based on selected child.
  void UpdatePinVisibility();

  // Show PIN error message and clear all fields.
  void ShowPinError(const std::string& message);

  // Clear PIN error message.
  void ClearPinError();

  // Get the combined PIN from all digit fields.
  std::u16string GetCombinedPin() const;

  // Check if all PIN digit fields are filled.
  bool IsPinComplete() const;

  // Clear all PIN digit fields and focus the first one.
  void ClearPinFields();

  // Focus the next PIN digit field after the given index.
  void FocusNextPinField(size_t current_index);

  // Focus the previous PIN digit field before the given index.
  void FocusPreviousPinField(size_t current_index);

  // Handle pasted text by distributing digits across fields.
  void HandlePastedPin(const std::u16string& pasted_text);

  // Get the index of a PIN digit field, or -1 if not found.
  int GetPinFieldIndex(views::Textfield* field) const;

  // Update confirm button enabled state based on PIN completion.
  void UpdateConfirmButtonState();

  // Return to account selection screen (clear selection, hide PIN).
  void ReturnToAccountSelection();

  // Update lockout state for all child buttons.
  void UpdateLockoutStates();

  // Called periodically by timer to refresh lockout countdowns.
  void OnLockoutTimerTick();

  // Start lockout refresh timer.
  void StartLockoutTimer();

  // Stop lockout refresh timer.
  void StopLockoutTimer();

  raw_ptr<Browser> browser_ = nullptr;
  std::vector<Child> children_;
  std::string owner_name_;
  std::optional<uint64_t> selected_child_id_;
  std::vector<raw_ptr<ChildButton>> child_buttons_;

  raw_ptr<views::View> logo_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::View> children_container_ = nullptr;
  raw_ptr<views::View> pin_container_ = nullptr;
  raw_ptr<views::Label> pin_label_ = nullptr;
  raw_ptr<views::View> pin_digits_container_ = nullptr;
  std::array<raw_ptr<views::Textfield>, kPinDigitCount> pin_digit_fields_{};
  raw_ptr<views::Label> pin_error_label_ = nullptr;
  raw_ptr<views::MdTextButton> confirm_button_ = nullptr;
  raw_ptr<views::MdTextButton> guest_button_ = nullptr;

  // Flag to prevent re-entrancy during auto-submit.
  bool is_submitting_ = false;

  // Service weak pointer for lockout state queries.
  base::WeakPtr<Allow2Service> service_;

  // Timer for refreshing lockout countdown display.
  base::RepeatingTimer lockout_timer_;

  ChildSelectedCallback child_selected_callback_;
  GuestCallback guest_callback_;

  base::WeakPtrFactory<Allow2ChildSelectView> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_CHILD_SELECT_VIEW_H_

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_CHILD_SELECT_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_CHILD_SELECT_VIEW_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;

namespace views {
class Label;
class MdTextButton;
class Textfield;
}  // namespace views

namespace allow2 {

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
  using ChildSelectedCallback = base::OnceCallback<void(uint64_t child_id,
                                                        const std::string& pin)>;
  using GuestCallback = base::OnceClosure;

  // Shows the child selection dialog for the given browser.
  static void Show(Browser* browser,
                   const std::vector<Child>& children,
                   ChildSelectedCallback child_selected_callback,
                   GuestCallback guest_callback);

  // Returns true if a child select view is currently showing.
  static bool IsShowing();

  Allow2ChildSelectView(const Allow2ChildSelectView&) = delete;
  Allow2ChildSelectView& operator=(const Allow2ChildSelectView&) = delete;

 private:
  // Represents a clickable child button.
  class ChildButton : public views::View {
    METADATA_HEADER(ChildButton, views::View)

   public:
    ChildButton(const Child& child,
                base::RepeatingCallback<void(uint64_t)> on_click);
    ~ChildButton() override;

    uint64_t child_id() const { return child_id_; }

    // Set selected state (highlights the button).
    void SetSelected(bool selected);

    // views::View overrides:
    bool OnMousePressed(const ui::MouseEvent& event) override;

   private:
    uint64_t child_id_ = 0;
    bool selected_ = false;
    base::RepeatingCallback<void(uint64_t)> on_click_;
    raw_ptr<views::Label> name_label_ = nullptr;
  };

  Allow2ChildSelectView(Browser* browser,
                        const std::vector<Child>& children,
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

  // Show PIN error message.
  void ShowPinError(const std::string& message);

  // Clear PIN error message.
  void ClearPinError();

  raw_ptr<Browser> browser_ = nullptr;
  std::vector<Child> children_;
  std::optional<uint64_t> selected_child_id_;
  std::vector<raw_ptr<ChildButton>> child_buttons_;

  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::View> children_container_ = nullptr;
  raw_ptr<views::View> pin_container_ = nullptr;
  raw_ptr<views::Label> pin_label_ = nullptr;
  raw_ptr<views::Textfield> pin_field_ = nullptr;
  raw_ptr<views::Label> pin_error_label_ = nullptr;
  raw_ptr<views::MdTextButton> confirm_button_ = nullptr;
  raw_ptr<views::MdTextButton> guest_button_ = nullptr;

  ChildSelectedCallback child_selected_callback_;
  GuestCallback guest_callback_;

  base::WeakPtrFactory<Allow2ChildSelectView> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_CHILD_SELECT_VIEW_H_

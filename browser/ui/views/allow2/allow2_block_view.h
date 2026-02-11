/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_BLOCK_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_BLOCK_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;

namespace views {
class Label;
class MdTextButton;
class StyledLabel;
}  // namespace views

namespace allow2 {

// Modal overlay view shown when browsing is blocked by Allow2.
// Displays the block reason and provides options to:
// - Request more time from parent
// - Switch to a different user
// - View why browsing is blocked
//
// This view takes over the entire browser content area and cannot be dismissed
// except through parent approval or user switching.
class Allow2BlockView : public views::DialogDelegateView {
  METADATA_HEADER(Allow2BlockView, views::DialogDelegateView)

 public:
  using RequestTimeCallback = base::OnceCallback<void(int minutes,
                                                       const std::string& message)>;
  using SwitchUserCallback = base::OnceClosure;

  // Shows the block overlay for the given browser.
  static void Show(Browser* browser,
                   const std::string& reason,
                   const std::string& day_type,
                   RequestTimeCallback request_time_callback,
                   SwitchUserCallback switch_user_callback);

  // Returns true if a block view is currently showing for any browser.
  static bool IsShowing();

  Allow2BlockView(const Allow2BlockView&) = delete;
  Allow2BlockView& operator=(const Allow2BlockView&) = delete;

  // Updates the displayed block reason.
  void SetBlockReason(const std::string& reason);

  // Updates the day type display.
  void SetDayType(const std::string& day_type);

 private:
  Allow2BlockView(Browser* browser,
                  const std::string& reason,
                  const std::string& day_type,
                  RequestTimeCallback request_time_callback,
                  SwitchUserCallback switch_user_callback);
  ~Allow2BlockView() override;

  // views::DialogDelegateView overrides:
  ui::mojom::ModalType GetModalType() const override;
  bool ShouldShowCloseButton() const override;
  bool ShouldShowWindowTitle() const override;

  // Button click handlers.
  void OnRequestTimeClicked();
  void OnSwitchUserClicked();
  void OnWhyBlockedClicked();

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> reason_label_ = nullptr;
  raw_ptr<views::Label> day_type_label_ = nullptr;
  raw_ptr<views::MdTextButton> request_time_button_ = nullptr;
  raw_ptr<views::MdTextButton> switch_user_button_ = nullptr;
  raw_ptr<views::StyledLabel> why_blocked_link_ = nullptr;

  RequestTimeCallback request_time_callback_;
  SwitchUserCallback switch_user_callback_;

  base::WeakPtrFactory<Allow2BlockView> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_UI_VIEWS_ALLOW2_ALLOW2_BLOCK_VIEW_H_

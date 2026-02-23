/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/allow2/allow2_block_view_delegate.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "brave/browser/ui/views/allow2/allow2_block_view.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "brave/components/allow2/common/allow2_constants.h"

namespace allow2 {

Allow2BlockViewDelegate::Allow2BlockViewDelegate() = default;

Allow2BlockViewDelegate::~Allow2BlockViewDelegate() = default;

void Allow2BlockViewDelegate::SetBrowser(Browser* browser) {
  browser_ = browser;
}

void Allow2BlockViewDelegate::SetService(base::WeakPtr<Allow2Service> service) {
  service_ = service;
}

void Allow2BlockViewDelegate::ShowBlockOverlay(const BlockOverlayConfig& config) {
  if (!browser_) {
    return;
  }

  // Don't show if already showing.
  if (Allow2BlockView::IsShowing()) {
    return;
  }

  Allow2BlockView::Show(
      browser_,
      config.reason_text,
      config.day_type,
      base::BindOnce(&Allow2BlockViewDelegate::OnRequestTime,
                     base::Unretained(this)),
      base::BindOnce(&Allow2BlockViewDelegate::OnSwitchUser,
                     base::Unretained(this)));
}

void Allow2BlockViewDelegate::DismissBlockOverlay() {
  // The Allow2BlockView handles its own dismissal through the dialog system.
  // When time is granted, the service will update state and the view will close.
}

bool Allow2BlockViewDelegate::IsBlockOverlayVisible() const {
  return Allow2BlockView::IsShowing();
}

void Allow2BlockViewDelegate::OnRequestTime(int minutes,
                                             const std::string& message) {
  if (service_) {
    service_->RequestMoreTime(ActivityId::kInternet, minutes, message,
                               base::DoNothing());
  }
}

void Allow2BlockViewDelegate::OnSwitchUser() {
  if (service_) {
    // Clear the current child, which will trigger the child selection shield
    // via the Allow2ServiceObserver mechanism in the tab helper.
    service_->ClearCurrentChild();
  }
}

}  // namespace allow2

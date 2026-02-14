/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_ALLOW2_ALLOW2_BLOCK_VIEW_DELEGATE_H_
#define BRAVE_BROWSER_ALLOW2_ALLOW2_BLOCK_VIEW_DELEGATE_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/allow2/browser/allow2_block_overlay.h"

class Browser;

namespace allow2 {

class Allow2Service;

// Views implementation of Allow2BlockOverlayDelegate.
// This adapter connects the platform-agnostic Allow2BlockOverlay controller
// to the Views-based Allow2BlockView UI.
class Allow2BlockViewDelegate : public Allow2BlockOverlayDelegate {
 public:
  Allow2BlockViewDelegate();
  ~Allow2BlockViewDelegate() override;

  Allow2BlockViewDelegate(const Allow2BlockViewDelegate&) = delete;
  Allow2BlockViewDelegate& operator=(const Allow2BlockViewDelegate&) = delete;

  // Set the browser to show the overlay in.
  // Must be called before ShowBlockOverlay().
  void SetBrowser(Browser* browser);

  // Set the service to callback into for actions.
  void SetService(base::WeakPtr<Allow2Service> service);

  // Allow2BlockOverlayDelegate implementation:
  void ShowBlockOverlay(const BlockOverlayConfig& config) override;
  void DismissBlockOverlay() override;
  bool IsBlockOverlayVisible() const override;

 private:
  void OnRequestTime(int minutes, const std::string& message);
  void OnSwitchUser();

  raw_ptr<Browser> browser_ = nullptr;
  base::WeakPtr<Allow2Service> service_;
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_ALLOW2_ALLOW2_BLOCK_VIEW_DELEGATE_H_

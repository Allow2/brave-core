/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_ALLOW2_ALLOW2_TAB_HELPER_H_
#define BRAVE_BROWSER_ALLOW2_ALLOW2_TAB_HELPER_H_

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace allow2 {

class Allow2Service;

// Tab helper that observes navigation events and reports them to Allow2Service.
// This enables usage tracking for parental controls.
//
// The helper:
// - Tracks page loads and reports URLs to Allow2 for time tracking
// - Triggers blocking overlay when browsing is blocked
// - Skips tracking for incognito/private tabs (configurable)
class Allow2TabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<Allow2TabHelper> {
 public:
  ~Allow2TabHelper() override;

  Allow2TabHelper(const Allow2TabHelper&) = delete;
  Allow2TabHelper& operator=(const Allow2TabHelper&) = delete;

  // Returns the Allow2TabHelper for the given WebContents, creating one if
  // necessary.
  static Allow2TabHelper* GetOrCreate(content::WebContents* contents);

  // Check if navigation should be blocked.
  bool ShouldBlockNavigation(const GURL& url) const;

  // Manually trigger a block check (e.g., after timer expires).
  void CheckBlocking();

 private:
  friend class content::WebContentsUserData<Allow2TabHelper>;

  explicit Allow2TabHelper(content::WebContents* contents);

  // content::WebContentsObserver overrides:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void WebContentsDestroyed() override;

  // Get the Allow2Service for this tab's profile.
  Allow2Service* GetService() const;

  // Track URL visit.
  void TrackUrl(const GURL& url);

  // Show block overlay if needed.
  void MaybeShowBlockOverlay();

  // Hide block overlay.
  void DismissBlockOverlay();

  // Whether this tab should be tracked.
  bool ShouldTrack() const;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_ALLOW2_ALLOW2_TAB_HELPER_H_

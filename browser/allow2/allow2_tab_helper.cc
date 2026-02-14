/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/allow2/allow2_tab_helper.h"

#include "brave/browser/allow2/allow2_service_factory.h"
#include "brave/components/allow2/browser/allow2_block_overlay.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

#if defined(TOOLKIT_VIEWS)
#include "brave/browser/allow2/allow2_block_view_delegate.h"
#endif

namespace allow2 {

// static
Allow2TabHelper* Allow2TabHelper::GetOrCreate(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }

  Allow2TabHelper* helper = FromWebContents(contents);
  if (!helper) {
    CreateForWebContents(contents);
    helper = FromWebContents(contents);
  }
  return helper;
}

Allow2TabHelper::Allow2TabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<Allow2TabHelper>(*contents) {}

Allow2TabHelper::~Allow2TabHelper() = default;

void Allow2TabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }

  if (!navigation_handle->HasCommitted()) {
    return;
  }

  if (!ShouldTrack()) {
    return;
  }

  const GURL& url = navigation_handle->GetURL();

  // Track the URL visit.
  TrackUrl(url);

  // Check if browsing should be blocked.
  MaybeShowBlockOverlay();
}

void Allow2TabHelper::DidStartLoading() {
  // Could show a pre-navigation check here if needed.
}

void Allow2TabHelper::DidStopLoading() {
  // Navigation complete, ensure blocking overlay is shown if needed.
  if (ShouldTrack()) {
    MaybeShowBlockOverlay();
  }
}

void Allow2TabHelper::WebContentsDestroyed() {
  // Cleanup if needed.
}

bool Allow2TabHelper::ShouldBlockNavigation(const GURL& url) const {
  Allow2Service* service = GetService();
  if (!service) {
    return false;
  }

  if (!service->IsPaired()) {
    return false;
  }

  if (!service->IsEnabled()) {
    return false;
  }

  return service->IsBlocked();
}

void Allow2TabHelper::CheckBlocking() {
  MaybeShowBlockOverlay();
}

Allow2Service* Allow2TabHelper::GetService() const {
  auto* contents = web_contents();
  if (!contents) {
    return nullptr;
  }

  Profile* profile =
      Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile) {
    return nullptr;
  }

  return Allow2ServiceFactory::GetForProfile(profile);
}

void Allow2TabHelper::TrackUrl(const GURL& url) {
  Allow2Service* service = GetService();
  if (!service) {
    return;
  }

  // Only track HTTP/HTTPS URLs.
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return;
  }

  service->TrackUrl(url.spec());
}

void Allow2TabHelper::MaybeShowBlockOverlay() {
  Allow2Service* service = GetService();
  if (!service) {
    return;
  }

  if (!service->ShouldShowBlockOverlay()) {
    return;
  }

#if defined(TOOLKIT_VIEWS)
  // Ensure delegate is set up and connected to the overlay.
  EnsureBlockViewDelegate();
#endif

  // Use the service to show the overlay (which will use the delegate).
  service->ShowBlockOverlay();
}

#if defined(TOOLKIT_VIEWS)
void Allow2TabHelper::EnsureBlockViewDelegate() {
  Allow2Service* service = GetService();
  if (!service) {
    return;
  }

  // Find the browser for this tab.
  Browser* browser = chrome::FindBrowserWithTab(web_contents());
  if (!browser) {
    return;
  }

  // Create delegate if needed.
  if (!block_view_delegate_) {
    block_view_delegate_ = std::make_unique<Allow2BlockViewDelegate>();
  }

  // Update the delegate with current browser and service.
  block_view_delegate_->SetBrowser(browser);
  block_view_delegate_->SetService(service->GetWeakPtr());

  // Register delegate with the overlay controller.
  Allow2BlockOverlay* overlay = service->GetBlockOverlay();
  if (overlay && overlay->GetDelegate() != block_view_delegate_.get()) {
    overlay->SetDelegate(block_view_delegate_.get());
  }
}
#endif  // defined(TOOLKIT_VIEWS)

void Allow2TabHelper::DismissBlockOverlay() {
  // Block view handles its own dismissal.
}

bool Allow2TabHelper::ShouldTrack() const {
  auto* contents = web_contents();
  if (!contents) {
    return false;
  }

  Profile* profile =
      Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile) {
    return false;
  }

  // Don't track incognito by default.
  // TODO: Make this configurable via Allow2 settings.
  if (profile->IsOffTheRecord()) {
    return false;
  }

  Allow2Service* service = GetService();
  if (!service) {
    return false;
  }

  // Only track if paired and enabled.
  return service->IsPaired() && service->IsEnabled();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(Allow2TabHelper);

}  // namespace allow2

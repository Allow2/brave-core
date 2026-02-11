// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Combine
import Foundation
import Preferences
import SwiftUI
import UIKit

// MARK: - BrowserViewController + Parental Freedom

extension BrowserViewController {

  // MARK: - Setup

  /// Set up Allow2 parental controls tracking and observers
  func setupParentalFreedom() {
    guard Allow2Manager.shared.isEnabled else { return }

    // Subscribe to state changes
    Allow2Manager.shared.stateDidChange
      .receive(on: DispatchQueue.main)
      .sink { [weak self] state in
        self?.handleAllow2StateChange(state)
      }
      .store(in: &allow2Cancellables)

    // Subscribe to block state changes
    Allow2Manager.shared.blockStateDidChange
      .receive(on: DispatchQueue.main)
      .sink { [weak self] isBlocked in
        if isBlocked {
          self?.showAllow2BlockOverlay()
        } else {
          self?.hideAllow2BlockOverlay()
        }
      }
      .store(in: &allow2Cancellables)

    // Subscribe to warnings
    Allow2Manager.shared.warningDidTrigger
      .receive(on: DispatchQueue.main)
      .sink { [weak self] level in
        self?.showAllow2Warning(level: level)
      }
      .store(in: &allow2Cancellables)

    // Check if child selection is needed on launch
    checkChildSelectionNeeded()
  }

  // MARK: - State Handling

  private func handleAllow2StateChange(_ state: Allow2State) {
    switch state {
    case .requireChildSelection:
      showChildSelectionShield()

    case .blocked(let result):
      showAllow2BlockOverlay(result: result)

    case .unblocked:
      hideAllow2BlockOverlay()

    case .childSelected:
      hideChildSelectionShield()
      Allow2Manager.shared.startTracking()

    case .childCleared:
      Allow2Manager.shared.stopTracking()

    case .unpaired:
      hideAllow2BlockOverlay()
      hideChildSelectionShield()

    default:
      break
    }
  }

  // MARK: - Child Selection

  private func checkChildSelectionNeeded() {
    guard Allow2Manager.shared.isPaired else { return }

    // Show child selection if in shared device mode
    if Allow2Manager.shared.isSharedDevice {
      // Check if we should show on launch
      if Preferences.Allow2.showChildShieldOnLaunch.value {
        showChildSelectionShield()
      }
    } else {
      // Child is already selected, start tracking
      Allow2Manager.shared.startTracking()
    }
  }

  /// Show the child selection shield overlay
  func showChildSelectionShield() {
    guard allow2ChildShieldController == nil else { return }

    let children = Allow2Preferences.cachedChildren

    let childSelectView = ChildSelectView(
      children: children,
      onChildSelected: { [weak self] child, pin in
        let success = Allow2Manager.shared.selectChild(child, pin: pin)
        if success {
          self?.hideChildSelectionShield()
        }
        return success
      },
      onDismiss: { [weak self] in
        // Guest mode - clear child and dismiss
        Allow2Manager.shared.clearChildSelection()
        self?.hideChildSelectionShield()
      }
    )

    let hostingController = UIHostingController(
      rootView: NavigationStack { childSelectView }
    )
    hostingController.modalPresentationStyle = .overFullScreen
    hostingController.modalTransitionStyle = .crossDissolve
    hostingController.isModalInPresentation = true  // Prevent dismissal

    allow2ChildShieldController = hostingController

    present(hostingController, animated: true)
  }

  /// Hide the child selection shield overlay
  func hideChildSelectionShield() {
    allow2ChildShieldController?.dismiss(animated: true) { [weak self] in
      self?.allow2ChildShieldController = nil
    }
  }

  // MARK: - Block Overlay

  /// Show the block overlay
  func showAllow2BlockOverlay(result: Allow2CheckResult? = nil) {
    guard allow2BlockViewController == nil else { return }

    let checkResult = result ?? Allow2Manager.shared.lastCheckResult
    guard let checkResult = checkResult else { return }

    let dayType = checkResult.dayTypes?.today?.name

    let blockView = BlockView(
      checkResult: checkResult,
      dayType: dayType,
      onRequestMoreTime: { [weak self] in
        self?.showRequestMoreTimeSheet()
      },
      onSwitchUser: { [weak self] in
        self?.hideAllow2BlockOverlay()
        self?.showChildSelectionShield()
      },
      onWhyBlocked: { [weak self] in
        self?.showWhyBlockedSheet(result: checkResult)
      }
    )

    let hostingController = UIHostingController(rootView: blockView)
    hostingController.modalPresentationStyle = .overFullScreen
    hostingController.modalTransitionStyle = .crossDissolve
    hostingController.isModalInPresentation = true

    allow2BlockViewController = hostingController

    present(hostingController, animated: true)
  }

  /// Hide the block overlay
  func hideAllow2BlockOverlay() {
    allow2BlockViewController?.dismiss(animated: true) { [weak self] in
      self?.allow2BlockViewController = nil
    }
  }

  // MARK: - Warnings

  /// Show a warning toast based on level
  func showAllow2Warning(level: WarningLevel) {
    // Dismiss any existing warning toast
    allow2WarningToast?.dismiss(animated: false)

    switch level {
    case .blocked:
      showAllow2BlockOverlay()
      return

    case .urgent(let seconds):
      // Show full-width urgent banner for final minute
      let banner = UrgentCountdownBanner(
        seconds: seconds,
        onTimeUp: { [weak self] in
          self?.showAllow2BlockOverlay()
        },
        onRequestMoreTime: { [weak self] in
          self?.showRequestMoreTimeSheet()
        }
      )
      banner.show(in: self)
      allow2UrgentBanner = banner

    case .gentle, .warning:
      let toast = WarningToast(
        level: level,
        onRequestMoreTime: { [weak self] in
          self?.showRequestMoreTimeSheet()
        },
        onDismiss: nil
      )

      toast.show(in: self, delay: 0, autoDismiss: level == .gentle(minutes: 0))
      allow2WarningToast = toast

    case .none:
      break
    }
  }

  // MARK: - Request More Time

  private func showRequestMoreTimeSheet() {
    let requestView = RequestMoreTimeView(
      activity: .internet,
      onRequest: { [weak self] duration, message in
        try await Allow2Manager.shared.requestMoreTime(
          activity: .internet,
          duration: duration.rawValue,
          message: message
        )
      },
      onCancel: {}
    )

    let hostingController = UIHostingController(rootView: requestView)

    if let presented = presentedViewController {
      presented.present(hostingController, animated: true)
    } else {
      present(hostingController, animated: true)
    }
  }

  private func showWhyBlockedSheet(result: Allow2CheckResult) {
    let whyBlockedView = WhyBlockedView(checkResult: result)
    let hostingController = UIHostingController(rootView: whyBlockedView)

    if let presented = presentedViewController {
      presented.present(hostingController, animated: true)
    } else {
      present(hostingController, animated: true)
    }
  }

  // MARK: - URL Tracking

  /// Track Allow2 usage for a URL navigation
  func trackAllow2Usage(url: URL?) {
    guard Allow2Manager.shared.isEnabled,
      Allow2Manager.shared.currentChild != nil,
      let url = url,
      url.scheme == "http" || url.scheme == "https"
    else {
      return
    }

    // Don't track private browsing
    guard !(tabManager.selectedTab?.isPrivate ?? true) else {
      return
    }

    // Classify the domain for activity type
    let activity = classifyDomain(url.host ?? "")

    // Log browsing activity
    Task {
      let result = await Allow2Manager.shared.check(
        activities: [activity],
        log: true
      )

      if let result = result, !result.allowed {
        await MainActor.run {
          showAllow2BlockOverlay(result: result)
        }
      }
    }
  }

  /// Classify a domain into an activity type
  private func classifyDomain(_ domain: String) -> Allow2Activity {
    let socialDomains = [
      "facebook.com", "fb.com", "instagram.com", "twitter.com", "x.com",
      "tiktok.com", "snapchat.com", "reddit.com", "pinterest.com",
      "tumblr.com", "linkedin.com",
    ]

    let gamingDomains = [
      "roblox.com", "minecraft.net", "twitch.tv", "steam.com",
      "epicgames.com", "ea.com", "playstation.com", "xbox.com",
      "nintendo.com", "itch.io",
    ]

    let lowercaseDomain = domain.lowercased()

    for socialDomain in socialDomains {
      if lowercaseDomain.contains(socialDomain) {
        return .social
      }
    }

    for gamingDomain in gamingDomains {
      if lowercaseDomain.contains(gamingDomain) {
        return .gaming
      }
    }

    return .internet
  }
}

// MARK: - Stored Properties Extension

extension BrowserViewController {

  private struct AssociatedKeys {
    static var cancellables = "allow2Cancellables"
    static var childShieldController = "allow2ChildShieldController"
    static var blockViewController = "allow2BlockViewController"
    static var warningToast = "allow2WarningToast"
    static var urgentBanner = "allow2UrgentBanner"
  }

  var allow2Cancellables: Set<AnyCancellable> {
    get {
      objc_getAssociatedObject(self, &AssociatedKeys.cancellables) as? Set<AnyCancellable> ?? []
    }
    set {
      objc_setAssociatedObject(
        self,
        &AssociatedKeys.cancellables,
        newValue,
        .OBJC_ASSOCIATION_RETAIN_NONATOMIC
      )
    }
  }

  var allow2ChildShieldController: UIViewController? {
    get {
      objc_getAssociatedObject(self, &AssociatedKeys.childShieldController) as? UIViewController
    }
    set {
      objc_setAssociatedObject(
        self,
        &AssociatedKeys.childShieldController,
        newValue,
        .OBJC_ASSOCIATION_RETAIN_NONATOMIC
      )
    }
  }

  var allow2BlockViewController: UIViewController? {
    get {
      objc_getAssociatedObject(self, &AssociatedKeys.blockViewController) as? UIViewController
    }
    set {
      objc_setAssociatedObject(
        self,
        &AssociatedKeys.blockViewController,
        newValue,
        .OBJC_ASSOCIATION_RETAIN_NONATOMIC
      )
    }
  }

  var allow2WarningToast: WarningToast? {
    get {
      objc_getAssociatedObject(self, &AssociatedKeys.warningToast) as? WarningToast
    }
    set {
      objc_setAssociatedObject(
        self,
        &AssociatedKeys.warningToast,
        newValue,
        .OBJC_ASSOCIATION_RETAIN_NONATOMIC
      )
    }
  }

  var allow2UrgentBanner: UrgentCountdownBanner? {
    get {
      objc_getAssociatedObject(self, &AssociatedKeys.urgentBanner) as? UrgentCountdownBanner
    }
    set {
      objc_setAssociatedObject(
        self,
        &AssociatedKeys.urgentBanner,
        newValue,
        .OBJC_ASSOCIATION_RETAIN_NONATOMIC
      )
    }
  }
}

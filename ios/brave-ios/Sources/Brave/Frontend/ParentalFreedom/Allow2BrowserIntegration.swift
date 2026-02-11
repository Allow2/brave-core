// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Combine
import SwiftUI
import UIKit
import os.log

// MARK: - Allow2 Browser Integration

/// Coordinator for integrating Allow2 with the Brave browser
/// Handles overlay presentation, navigation blocking, and warning display
public final class Allow2BrowserIntegration {

  // MARK: - Singleton

  public static let shared = Allow2BrowserIntegration()

  // MARK: - Properties

  private var cancellables: Set<AnyCancellable> = []

  /// Current warning toast being displayed
  private weak var currentWarningToast: WarningToast?

  /// Current urgent countdown banner
  private weak var currentUrgentBanner: UrgentCountdownBanner?

  /// Whether the block overlay is currently showing
  private(set) var isBlockOverlayShowing = false

  /// The view controller for block overlay
  private weak var blockOverlayViewController: UIViewController?

  // MARK: - Initialization

  private init() {
    setupSubscriptions()
  }

  // MARK: - Setup

  private func setupSubscriptions() {
    // Subscribe to block state changes
    Allow2Manager.shared.blockStateDidChange
      .receive(on: DispatchQueue.main)
      .sink { [weak self] isBlocked in
        self?.handleBlockStateChange(isBlocked)
      }
      .store(in: &cancellables)

    // Subscribe to warnings
    Allow2Manager.shared.warningDidTrigger
      .receive(on: DispatchQueue.main)
      .sink { [weak self] level in
        self?.handleWarning(level)
      }
      .store(in: &cancellables)

    // Subscribe to state changes
    Allow2Manager.shared.stateDidChange
      .receive(on: DispatchQueue.main)
      .sink { [weak self] state in
        self?.handleStateChange(state)
      }
      .store(in: &cancellables)

    // Subscribe to usage tracker warnings
    Allow2UsageTracker.shared.warningDidTrigger
      .receive(on: DispatchQueue.main)
      .sink { [weak self] level in
        self?.handleWarning(level)
      }
      .store(in: &cancellables)

    // Subscribe to usage tracker block state
    Allow2UsageTracker.shared.blockStateDidChange
      .receive(on: DispatchQueue.main)
      .sink { [weak self] isBlocked in
        self?.handleBlockStateChange(isBlocked)
      }
      .store(in: &cancellables)
  }

  // MARK: - Integration Methods

  /// Initialize Allow2 integration on app launch
  /// Call this from AppDelegate or SceneDelegate
  public func initialize() {
    // Check for credential migration
    Allow2CredentialManager.shared.migrateIfNeeded()

    // Check if we need to show child selection
    if Allow2Manager.shared.isEnabled && Allow2Manager.shared.isSharedDevice {
      if Preferences.Allow2.showChildShieldOnLaunch.value {
        // Will be shown when root view controller is available
        Logger.module.info("Allow2: Will show child selection on launch")
      }
    }

    // Start tracking if child is already selected
    if Allow2Manager.shared.isEnabled,
       Allow2Manager.shared.currentChild != nil {
      Allow2Manager.shared.startTracking()
    }

    Logger.module.info("Allow2 Browser Integration: Initialized")
  }

  /// Check if a URL should be allowed
  /// - Parameter url: URL to check
  /// - Returns: True if navigation should be allowed
  public func shouldAllowNavigation(to url: URL) -> Bool {
    guard Allow2Manager.shared.isEnabled else {
      return true
    }

    // Always allow certain URLs
    if isAllowedURL(url) {
      return true
    }

    // Check current block state
    if Allow2Manager.shared.isBlocked {
      return false
    }

    // Track the navigation (async check)
    Allow2Manager.shared.trackNavigation(to: url)

    return true
  }

  /// Present child selection shield
  /// - Parameter viewController: Parent view controller
  public func presentChildSelectionIfNeeded(on viewController: UIViewController) {
    guard Allow2Manager.shared.isEnabled,
          Allow2Manager.shared.isSharedDevice,
          Preferences.Allow2.showChildShieldOnLaunch.value
    else {
      return
    }

    presentChildSelection(on: viewController)
  }

  /// Present child selection sheet
  /// - Parameter viewController: Parent view controller
  public func presentChildSelection(on viewController: UIViewController) {
    let children = Allow2Manager.shared.cachedChildren

    let childSelectView = ChildSelectView(
      children: children,
      onChildSelected: { child, pin in
        return Allow2Manager.shared.selectChild(child, pin: pin)
      },
      onDismiss: {
        viewController.dismiss(animated: true)
      }
    )

    let hostingController = UIHostingController(rootView:
      NavigationStack {
        childSelectView
      }
    )
    hostingController.modalPresentationStyle = .fullScreen
    hostingController.isModalInPresentation = true

    viewController.present(hostingController, animated: true)
  }

  // MARK: - Block Overlay

  /// Show block overlay
  /// - Parameter viewController: Parent view controller
  public func showBlockOverlay(on viewController: UIViewController) {
    guard !isBlockOverlayShowing,
          let result = Allow2Manager.shared.lastCheckResult,
          !result.allowed
    else {
      return
    }

    let blockView = BlockView(
      checkResult: result,
      dayType: result.dayTypes?.today?.name,
      onRequestMoreTime: { [weak self, weak viewController] in
        self?.presentRequestMoreTime(on: viewController)
      },
      onSwitchUser: { [weak self, weak viewController] in
        guard let vc = viewController else { return }
        self?.hideBlockOverlay()
        self?.presentChildSelection(on: vc)
      },
      onWhyBlocked: { [weak self, weak viewController] in
        self?.presentWhyBlocked(result: result, on: viewController)
      }
    )

    let hostingController = UIHostingController(rootView: blockView)
    hostingController.modalPresentationStyle = .fullScreen
    hostingController.isModalInPresentation = true

    viewController.present(hostingController, animated: true)

    blockOverlayViewController = hostingController
    isBlockOverlayShowing = true

    Logger.module.info("Allow2: Showing block overlay")
  }

  /// Hide block overlay
  public func hideBlockOverlay() {
    blockOverlayViewController?.dismiss(animated: true)
    blockOverlayViewController = nil
    isBlockOverlayShowing = false
  }

  // MARK: - Request More Time

  private func presentRequestMoreTime(on viewController: UIViewController?) {
    guard let viewController = viewController else { return }

    let requestView = RequestMoreTimeView(
      activity: .internet,
      onRequest: { duration, message in
        try await Allow2Manager.shared.requestMoreTime(
          activity: .internet,
          duration: duration.rawValue,
          message: message
        )
      },
      onCancel: {
        viewController.presentedViewController?.dismiss(animated: true)
      }
    )

    let hostingController = UIHostingController(rootView: requestView)
    hostingController.modalPresentationStyle = .formSheet

    if let presented = viewController.presentedViewController {
      presented.present(hostingController, animated: true)
    } else {
      viewController.present(hostingController, animated: true)
    }
  }

  private func presentWhyBlocked(
    result: Allow2CheckResult,
    on viewController: UIViewController?
  ) {
    guard let viewController = viewController else { return }

    let whyBlockedView = WhyBlockedView(checkResult: result)

    let hostingController = UIHostingController(rootView: whyBlockedView)
    hostingController.modalPresentationStyle = .formSheet

    if let presented = viewController.presentedViewController {
      presented.present(hostingController, animated: true)
    } else {
      viewController.present(hostingController, animated: true)
    }
  }

  // MARK: - Event Handlers

  private func handleBlockStateChange(_ isBlocked: Bool) {
    if isBlocked {
      // Find the top view controller to present on
      if let topVC = topViewController() {
        showBlockOverlay(on: topVC)
      }
    } else {
      hideBlockOverlay()
    }
  }

  private func handleWarning(_ level: WarningLevel) {
    // Dismiss any existing warning
    currentWarningToast?.dismiss(animated: true)
    currentUrgentBanner?.dismiss(animated: true)

    guard let topVC = topViewController() else { return }

    switch level {
    case .urgent(let seconds):
      // Show full-width urgent banner for last minute
      let banner = UrgentCountdownBanner(
        seconds: seconds,
        onTimeUp: { [weak self] in
          // Re-check status when time runs out
          Task {
            await Allow2Manager.shared.check()
          }
        },
        onRequestMoreTime: { [weak self] in
          self?.presentRequestMoreTime(on: topVC)
        }
      )
      banner.show(in: topVC)
      currentUrgentBanner = banner

    case .warning, .gentle:
      // Show toast notification
      let toast = WarningToast(
        level: level,
        onRequestMoreTime: { [weak self] in
          self?.presentRequestMoreTime(on: topVC)
        },
        onDismiss: nil
      )
      toast.show(in: topVC)
      currentWarningToast = toast

    case .blocked, .none:
      break
    }
  }

  private func handleStateChange(_ state: Allow2State) {
    switch state {
    case .requireChildSelection:
      if let topVC = topViewController() {
        presentChildSelection(on: topVC)
      }

    case .unpaired:
      hideBlockOverlay()
      currentWarningToast?.dismiss(animated: false)
      currentUrgentBanner?.dismiss(animated: false)

    default:
      break
    }
  }

  // MARK: - Helpers

  private func isAllowedURL(_ url: URL) -> Bool {
    // Allow certain system URLs
    let allowedSchemes = ["about", "brave", "internal"]
    if let scheme = url.scheme, allowedSchemes.contains(scheme) {
      return true
    }

    // Allow Allow2 related URLs
    if let host = url.host?.lowercased(),
       host.contains("allow2.com") {
      return true
    }

    return false
  }

  private func topViewController() -> UIViewController? {
    guard let windowScene = UIApplication.shared.connectedScenes
            .compactMap({ $0 as? UIWindowScene })
            .first(where: { $0.activationState == .foregroundActive }),
          let window = windowScene.windows.first(where: { $0.isKeyWindow }),
          var topVC = window.rootViewController
    else {
      return nil
    }

    while let presented = topVC.presentedViewController {
      topVC = presented
    }

    return topVC
  }
}

// MARK: - Settings Entry Point

extension Allow2BrowserIntegration {

  /// Create the settings view for Parental Freedom
  public func makeSettingsView() -> some View {
    ParentalFreedomSettingsView()
  }

  /// Create UIViewController for settings
  public func makeSettingsViewController() -> UIViewController {
    return UIHostingController(rootView: ParentalFreedomSettingsView())
  }
}

// MARK: - Preferences Extension

import Preferences

extension Preferences.Allow2 {
  /// Session timeout preference (seconds)
  public static let sessionTimeout = Option<Int>(
    key: "allow2.sessionTimeout",
    default: 300
  )
}

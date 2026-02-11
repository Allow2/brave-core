// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Combine
import Foundation
import os.log

// MARK: - Allow2 Usage Tracker

/// Timer-based usage tracking service that periodically checks Allow2 API
/// and triggers appropriate warnings and blocks
public final class Allow2UsageTracker {

  // MARK: - Singleton

  public static let shared = Allow2UsageTracker()

  // MARK: - Properties

  /// Timer for periodic checks
  private var checkTimer: Timer?

  /// Last successful check result
  public private(set) var lastCheckResult: Allow2CheckResult?

  /// Last check timestamp
  public private(set) var lastCheckTime: Date?

  /// Whether tracking is currently active
  public private(set) var isTracking = false

  /// Current activities being tracked
  private var trackedActivities: Set<Allow2Activity> = [.internet]

  /// Publishers
  public let checkResultDidChange = PassthroughSubject<Allow2CheckResult, Never>()
  public let blockStateDidChange = PassthroughSubject<Bool, Never>()
  public let warningDidTrigger = PassthroughSubject<WarningLevel, Never>()
  public let trackingStateDidChange = PassthroughSubject<Bool, Never>()

  /// Cached check for offline scenarios
  private var cachedResult: CachedCheckResult?

  /// Set of already-triggered warning thresholds to prevent duplicate warnings
  private var triggeredWarnings: Set<Int> = []

  /// Background task identifier for continuing checks briefly in background
  private var backgroundTaskID: UIBackgroundTaskIdentifier = .invalid

  // MARK: - Initialization

  private init() {
    setupNotifications()
  }

  // MARK: - Setup

  private func setupNotifications() {
    NotificationCenter.default.addObserver(
      self,
      selector: #selector(appDidEnterBackground),
      name: UIApplication.didEnterBackgroundNotification,
      object: nil
    )

    NotificationCenter.default.addObserver(
      self,
      selector: #selector(appWillEnterForeground),
      name: UIApplication.willEnterForegroundNotification,
      object: nil
    )
  }

  // MARK: - Tracking Control

  /// Start periodic usage tracking
  /// - Parameters:
  ///   - activities: Activities to track (default: internet)
  ///   - immediate: Whether to perform an immediate check
  public func startTracking(
    activities: Set<Allow2Activity> = [.internet],
    immediate: Bool = true
  ) {
    guard !isTracking else { return }

    trackedActivities = activities
    isTracking = true
    triggeredWarnings.removeAll()

    Logger.module.info("Allow2 Usage Tracker: Started tracking")

    // Set up periodic timer
    checkTimer = Timer.scheduledTimer(
      withTimeInterval: Allow2Constants.Timing.checkIntervalSeconds,
      repeats: true
    ) { [weak self] _ in
      Task {
        await self?.performCheck(log: true)
      }
    }

    // Perform immediate check if requested
    if immediate {
      Task {
        await performCheck(log: true)
      }
    }

    trackingStateDidChange.send(true)
  }

  /// Stop usage tracking
  public func stopTracking() {
    guard isTracking else { return }

    checkTimer?.invalidate()
    checkTimer = nil
    isTracking = false

    Logger.module.info("Allow2 Usage Tracker: Stopped tracking")
    trackingStateDidChange.send(false)
  }

  /// Pause tracking temporarily (e.g., when in background)
  public func pauseTracking() {
    checkTimer?.invalidate()
    checkTimer = nil
    Logger.module.debug("Allow2 Usage Tracker: Paused")
  }

  /// Resume tracking after pause
  public func resumeTracking() {
    guard isTracking else { return }

    checkTimer = Timer.scheduledTimer(
      withTimeInterval: Allow2Constants.Timing.checkIntervalSeconds,
      repeats: true
    ) { [weak self] _ in
      Task {
        await self?.performCheck(log: true)
      }
    }

    // Immediate check on resume
    Task {
      await performCheck(log: true)
    }

    Logger.module.debug("Allow2 Usage Tracker: Resumed")
  }

  // MARK: - Check Operations

  /// Perform a usage check
  /// - Parameters:
  ///   - activities: Activities to check (nil = use tracked activities)
  ///   - log: Whether to log usage time
  /// - Returns: Check result or nil if check failed
  @discardableResult
  public func performCheck(
    activities: [Allow2Activity]? = nil,
    log: Bool
  ) async -> Allow2CheckResult? {
    guard let credentials = Allow2CredentialManager.shared.retrieveCredentials(),
          let childId = Allow2Preferences.selectedChildId.value
    else {
      Logger.module.warning("Allow2 Usage Tracker: Cannot check - no credentials or child selected")
      return nil
    }

    let activitiesToCheck = activities ?? Array(trackedActivities)

    do {
      let result = try await Allow2APIClient.shared.check(
        credentials: credentials,
        childId: childId,
        activities: activitiesToCheck,
        log: log
      )

      await MainActor.run {
        processCheckResult(result)
      }

      return result

    } catch Allow2APIError.unauthorized {
      // Device has been unpaired remotely
      await MainActor.run {
        handleRemoteUnpair()
      }
      return nil

    } catch {
      Logger.module.error("Allow2 Usage Tracker: Check failed - \(error.localizedDescription)")

      // Try to use cached result if available and not expired
      if let cached = cachedResult, !cached.isExpired {
        return cached.result
      }

      return nil
    }
  }

  /// Check without logging (for status display only)
  public func checkStatus(activities: [Allow2Activity]? = nil) async -> Allow2CheckResult? {
    return await performCheck(activities: activities, log: false)
  }

  // MARK: - Navigation Tracking

  /// Track navigation to a URL
  /// - Parameter url: URL being navigated to
  public func trackNavigation(to url: URL) {
    guard isTracking else { return }

    // Determine activity type based on domain
    let activities = determineActivities(for: url)

    Task {
      await performCheck(activities: activities, log: true)
    }
  }

  /// Determine which activities apply to a given URL
  private func determineActivities(for url: URL) -> [Allow2Activity] {
    guard let host = url.host?.lowercased() else {
      return [.internet]
    }

    var activities: [Allow2Activity] = [.internet]

    // Check for social media
    if Allow2Constants.socialMediaDomains.contains(where: { host.contains($0) }) {
      activities.append(.social)
    }

    // Check for gaming
    if Allow2Constants.gamingDomains.contains(where: { host.contains($0) }) {
      activities.append(.gaming)
    }

    return activities
  }

  // MARK: - Result Processing

  private func processCheckResult(_ result: Allow2CheckResult) {
    let previouslyBlocked = lastCheckResult.map { !$0.allowed } ?? false
    let nowBlocked = !result.allowed

    lastCheckResult = result
    lastCheckTime = Date()

    // Cache the result
    cacheResult(result)

    // Publish result
    checkResultDidChange.send(result)

    // Check for block state change
    if previouslyBlocked != nowBlocked {
      blockStateDidChange.send(nowBlocked)
      Logger.module.info("Allow2 Usage Tracker: Block state changed to \(nowBlocked)")
    }

    // Check for warnings
    if result.allowed, let remaining = result.minimumRemainingSeconds {
      checkForWarnings(remainingSeconds: remaining)
    }
  }

  private func checkForWarnings(remainingSeconds: Int) {
    // Find the appropriate warning threshold
    for threshold in Allow2Constants.WarningThreshold.allThresholds {
      if remainingSeconds <= threshold && !triggeredWarnings.contains(threshold) {
        triggeredWarnings.insert(threshold)

        let level = WarningLevel.from(seconds: remainingSeconds)
        if level != .none {
          warningDidTrigger.send(level)
          Logger.module.info("Allow2 Usage Tracker: Warning triggered at \(remainingSeconds) seconds")
        }
        break
      }
    }
  }

  private func handleRemoteUnpair() {
    Logger.module.warning("Allow2 Usage Tracker: Device unpaired remotely")

    stopTracking()
    Allow2CredentialManager.shared.clearAll()
    Allow2Preferences.clearAll()

    // Notify observers
    NotificationCenter.default.post(
      name: .allow2DeviceUnpaired,
      object: nil
    )
  }

  // MARK: - Caching

  private func cacheResult(_ result: Allow2CheckResult) {
    // Find the shortest expiry time from activities
    let expiryTime: TimeInterval

    let expiries = result.activities.values.compactMap { $0.expires }
    if let minExpiry = expiries.min() {
      expiryTime = minExpiry
    } else {
      // Default cache duration: 5 minutes
      expiryTime = Date().timeIntervalSince1970 + 300
    }

    cachedResult = CachedCheckResult(
      result: result,
      expiresAt: Date(timeIntervalSince1970: expiryTime)
    )
  }

  /// Clear cached result
  public func clearCache() {
    cachedResult = nil
    lastCheckResult = nil
    lastCheckTime = nil
    triggeredWarnings.removeAll()
  }

  // MARK: - App Lifecycle

  @objc private func appDidEnterBackground() {
    // Start a background task to complete any pending check
    backgroundTaskID = UIApplication.shared.beginBackgroundTask { [weak self] in
      self?.endBackgroundTask()
    }

    // Pause the timer but allow current operation to complete
    pauseTracking()

    // End background task after a short delay
    DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
      self?.endBackgroundTask()
    }
  }

  @objc private func appWillEnterForeground() {
    if isTracking {
      resumeTracking()
    }
  }

  private func endBackgroundTask() {
    guard backgroundTaskID != .invalid else { return }
    UIApplication.shared.endBackgroundTask(backgroundTaskID)
    backgroundTaskID = .invalid
  }
}

// MARK: - Cached Check Result

private struct CachedCheckResult {
  let result: Allow2CheckResult
  let expiresAt: Date

  var isExpired: Bool {
    return Date() > expiresAt
  }
}

// MARK: - Notifications

public extension Notification.Name {
  static let allow2DeviceUnpaired = Notification.Name("Allow2DeviceUnpaired")
  static let allow2CheckResultChanged = Notification.Name("Allow2CheckResultChanged")
  static let allow2BlockStateChanged = Notification.Name("Allow2BlockStateChanged")
  static let allow2WarningTriggered = Notification.Name("Allow2WarningTriggered")
}

// MARK: - UIKit Import

import UIKit

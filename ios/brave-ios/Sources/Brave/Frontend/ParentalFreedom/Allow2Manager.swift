// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Combine
import CryptoKit
import Foundation
import Preferences
import UIKit
import os.log

/// Activity types supported by Allow2 API
public enum Allow2Activity: Int, Codable, CaseIterable {
  case internet = 1
  case gaming = 3
  case screenTime = 8
  case social = 9

  /// Display name for the activity
  public var displayName: String {
    switch self {
    case .internet: return "Internet"
    case .gaming: return "Gaming"
    case .screenTime: return "Screen Time"
    case .social: return "Social Media"
    }
  }
}

/// Singleton manager for Allow2 parental controls integration.
/// Handles pairing, API communication, caching, and session management.
public final class Allow2Manager {

  // MARK: - Singleton

  public static let shared = Allow2Manager()

  // MARK: - Constants
  // Note: Endpoints are now centralized in Allow2Constants
  // - api.allow2.com for pairing endpoints
  // - service.allow2.com for check/usage endpoints (direct access for performance)

  // MARK: - Properties

  /// Current child selected for tracking (nil = shared device mode)
  public private(set) var currentChild: Allow2Child?

  /// Last check result from API
  public private(set) var lastCheckResult: Allow2CheckResult?

  /// Timer for periodic usage checks
  private var checkTimer: Timer?

  /// Publisher for state changes
  public let stateDidChange = PassthroughSubject<Allow2State, Never>()

  /// Publisher for block state changes
  public let blockStateDidChange = PassthroughSubject<Bool, Never>()

  /// Publisher for warnings
  public let warningDidTrigger = PassthroughSubject<WarningLevel, Never>()

  /// Cached children list
  public var cachedChildren: [Allow2Child] {
    return Allow2Preferences.cachedChildren
  }

  /// Whether the device is paired with Allow2
  public var isPaired: Bool {
    return Allow2Preferences.credentials != nil
  }

  /// Whether Allow2 is enabled
  public var isEnabled: Bool {
    return Allow2Preferences.isEnabled.value && isPaired
  }

  /// Whether the device is in shared mode (no specific child selected)
  public var isSharedDevice: Bool {
    return Allow2Preferences.selectedChildId.value == nil
  }

  /// Whether the current session is blocked
  public var isBlocked: Bool {
    guard let result = lastCheckResult else { return false }
    return !result.allowed
  }

  /// Last time the app went to background
  private var lastBackgroundTime: Date?

  /// Network session
  private lazy var urlSession: URLSession = {
    let config = URLSessionConfiguration.default
    config.timeoutIntervalForRequest = 30
    config.timeoutIntervalForResource = 60
    return URLSession(configuration: config)
  }()

  // MARK: - Initialization

  private init() {
    setupNotifications()
    loadCurrentChild()
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

  private func loadCurrentChild() {
    guard let childId = Allow2Preferences.selectedChildId.value else {
      currentChild = nil
      return
    }

    currentChild = cachedChildren.first { $0.id == childId }
  }

  // MARK: - Pairing (QR/PIN only - device NEVER handles parent credentials)

  /// Current pairing device token (stored for multi-step pairing)
  private var currentDeviceToken: String?

  /// Initialize QR code pairing session.
  /// Device displays QR code, parent scans with their Allow2 app.
  /// Parent authenticates with passkey/biometrics on their device.
  /// Device NEVER sees parent credentials.
  /// - Parameter deviceName: Name for this device
  /// - Returns: Pairing session with QR code URL
  public func initQRPairing(deviceName: String) async throws -> PairingSession {
    let deviceToken = Allow2CredentialManager.shared.generateDeviceToken()
    currentDeviceToken = deviceToken

    // Store device token securely
    try? Allow2CredentialManager.shared.storeDeviceToken(deviceToken)

    let response = try await Allow2APIClient.shared.initQRPairing(
      deviceName: deviceName,
      deviceToken: deviceToken
    )

    return PairingSession(
      sessionId: response.sessionId,
      qrCodeUrl: response.qrCodeUrl ?? Allow2Constants.DeepLink.pairingURL(sessionId: response.sessionId),
      pinCode: response.pinCode
    )
  }

  /// Initialize PIN code pairing session.
  /// Device displays 6-digit PIN, parent enters in their Allow2 app.
  /// Parent authenticates with passkey/biometrics on their device.
  /// Device NEVER sees parent credentials.
  /// - Parameter deviceName: Name for this device
  /// - Returns: Pairing session with PIN code
  public func initPINPairing(deviceName: String) async throws -> PairingSession {
    let deviceToken = Allow2CredentialManager.shared.generateDeviceToken()
    currentDeviceToken = deviceToken

    // Store device token securely
    try? Allow2CredentialManager.shared.storeDeviceToken(deviceToken)

    let response = try await Allow2APIClient.shared.initPINPairing(
      deviceName: deviceName,
      deviceToken: deviceToken
    )

    return PairingSession(
      sessionId: response.sessionId,
      qrCodeUrl: response.qrCodeUrl ?? "",
      pinCode: response.pinCode
    )
  }

  /// Check status of pairing session (poll until completed).
  /// Returns completed=true when parent has approved the pairing.
  /// - Parameter sessionId: Session ID from initQRPairing/initPINPairing
  /// - Returns: Pairing status
  public func checkPairingStatus(sessionId: String) async throws -> PairingStatus {
    let statusResponse = try await Allow2APIClient.shared.checkPairingStatus(sessionId: sessionId)

    if statusResponse.completed && statusResponse.success {
      // Parent approved - store credentials securely
      guard let credentials = statusResponse.credentials,
        let deviceToken = currentDeviceToken
      else {
        throw Allow2Error.pairingFailed("Missing credentials in response")
      }

      let allow2Credentials = Allow2Credentials(
        userId: credentials.userId,
        pairId: credentials.pairId,
        pairToken: deviceToken
      )

      // Store in Keychain via credential manager
      try Allow2CredentialManager.shared.storeCredentials(allow2Credentials)

      // Also store in Allow2Preferences for backward compatibility
      Allow2Preferences.credentials = allow2Credentials

      // Cache children list
      if let children = statusResponse.children {
        Allow2Preferences.cachedChildren = children
      }

      // Enable Allow2
      Allow2Preferences.isEnabled.value = true

      stateDidChange.send(.paired)
      currentDeviceToken = nil

      Logger.module.info("Allow2: Pairing completed successfully")
    }

    return PairingStatus(
      completed: statusResponse.completed,
      success: statusResponse.success,
      error: statusResponse.error
    )
  }

  /// Cancel an active pairing session.
  /// - Parameter sessionId: Session ID to cancel
  public func cancelPairing(sessionId: String) {
    currentDeviceToken = nil

    Task {
      try? await Allow2APIClient.shared.cancelPairing(sessionId: sessionId)
    }
  }

  /// Handle unpair (server-initiated via 401 response)
  private func handleUnpair() {
    // Clear all credentials securely
    Allow2CredentialManager.shared.clearAll()
    Allow2Preferences.clearAll()

    // Clear usage tracker state
    Allow2UsageTracker.shared.clearCache()
    Allow2UsageTracker.shared.stopTracking()

    currentChild = nil
    lastCheckResult = nil
    stopTracking()

    // Post notification for UI updates
    NotificationCenter.default.post(name: .allow2DeviceUnpaired, object: nil)

    stateDidChange.send(.unpaired)

    Logger.module.warning("Allow2: Device unpaired")
  }

  // MARK: - Child Selection

  /// Select a child for tracking (validates PIN)
  /// - Parameters:
  ///   - child: The child to select
  ///   - pin: The child's PIN
  /// - Returns: True if PIN is valid
  public func selectChild(_ child: Allow2Child, pin: String) -> Bool {
    guard validatePin(pin, for: child) else {
      return false
    }

    currentChild = child
    Allow2Preferences.selectedChildId.value = child.id

    stateDidChange.send(.childSelected(child))
    startTracking()

    return true
  }

  /// Validate PIN for a child using constant-time comparison
  /// - Parameters:
  ///   - pin: Entered PIN
  ///   - child: Child to validate against
  /// - Returns: True if PIN is valid
  private func validatePin(_ pin: String, for child: Allow2Child) -> Bool {
    // Use the dedicated PIN verifier for secure comparison
    return Allow2PINVerifier.verifyPIN(pin, for: child)
  }

  /// Clear current child selection (for shared device mode)
  public func clearChildSelection() {
    currentChild = nil
    Allow2Preferences.selectedChildId.value = nil
    stopTracking()
    stateDidChange.send(.childCleared)
  }

  // MARK: - Usage Tracking

  /// Start periodic usage tracking
  public func startTracking() {
    guard isEnabled, currentChild != nil else { return }

    stopTracking()

    // Use the dedicated usage tracker
    Allow2UsageTracker.shared.startTracking(
      activities: [.internet],
      immediate: true
    )

    // Also maintain local timer for backward compatibility
    checkTimer = Timer.scheduledTimer(
      withTimeInterval: Allow2Constants.Timing.checkIntervalSeconds,
      repeats: true
    ) { [weak self] _ in
      Task {
        await self?.performCheck()
      }
    }

    // Perform initial check
    Task {
      await performCheck()
    }

    Logger.module.info("Allow2: Started tracking")
  }

  /// Stop usage tracking
  public func stopTracking() {
    checkTimer?.invalidate()
    checkTimer = nil

    Allow2UsageTracker.shared.stopTracking()

    Logger.module.info("Allow2: Stopped tracking")
  }

  /// Check API for current usage status
  /// Uses service.allow2.com for direct access (performance optimization)
  /// - Parameters:
  ///   - activities: Activities to check/log
  ///   - log: Whether to log usage
  public func check(
    activities: [Allow2Activity] = [.internet],
    log: Bool = true
  ) async -> Allow2CheckResult? {
    guard let credentials = Allow2Preferences.credentials,
      let childId = currentChild?.id
    else {
      return nil
    }

    do {
      // Use the dedicated API client with service.allow2.com endpoint
      let result = try await Allow2APIClient.shared.check(
        credentials: credentials,
        childId: childId,
        activities: activities,
        log: log
      )

      lastCheckResult = result
      processCheckResult(result)

      return result

    } catch Allow2APIError.unauthorized {
      // Device unpaired remotely
      handleUnpair()
      return nil

    } catch {
      Logger.module.error("Allow2 check failed: \(error.localizedDescription)")
      return nil
    }
  }

  /// Perform periodic check
  private func performCheck() async {
    _ = await check(activities: [.internet], log: true)
  }

  /// Process check result and trigger appropriate warnings
  private func processCheckResult(_ result: Allow2CheckResult) {
    let wasBlocked = isBlocked

    // Check for block state change
    if result.allowed != !wasBlocked {
      blockStateDidChange.send(!result.allowed)
    }

    // Check for warnings based on remaining time
    if let remaining = result.minimumRemainingSeconds {
      let level = WarningLevel.from(seconds: remaining)
      if level != .none {
        warningDidTrigger.send(level)
      }
    }
  }

  // MARK: - Request More Time

  /// Request additional time from parent
  /// - Parameters:
  ///   - activity: Activity to request time for
  ///   - duration: Duration in minutes
  ///   - message: Optional message to parent
  public func requestMoreTime(
    activity: Allow2Activity,
    duration: Int,
    message: String?
  ) async throws {
    guard let credentials = Allow2Preferences.credentials,
      let childId = currentChild?.id
    else {
      throw Allow2Error.notPaired
    }

    do {
      try await Allow2APIClient.shared.requestMoreTime(
        credentials: credentials,
        childId: childId,
        activity: activity,
        duration: duration,
        message: message
      )

      Logger.module.info("Allow2: More time request sent for \(activity.displayName)")

    } catch Allow2APIError.unauthorized {
      handleUnpair()
      throw Allow2Error.notPaired
    }
  }

  // MARK: - App Lifecycle

  @objc private func appDidEnterBackground() {
    lastBackgroundTime = Date()
    stopTracking()
  }

  @objc private func appWillEnterForeground() {
    guard isEnabled else { return }

    // Check if we need to show child selection shield
    if isSharedDevice {
      let shouldShowShield: Bool
      if let backgroundTime = lastBackgroundTime {
        let elapsed = Date().timeIntervalSince(backgroundTime)
        shouldShowShield = elapsed > Allow2Constants.Timing.backgroundResumeThreshold
      } else {
        shouldShowShield = true
      }

      if shouldShowShield {
        stateDidChange.send(.requireChildSelection)
      }
    } else if currentChild != nil {
      startTracking()
    }
  }

  // MARK: - Helpers

  private func generateDeviceToken() -> String {
    let uuid = UUID().uuidString
    let timestamp = String(Int(Date().timeIntervalSince1970))
    return "BRAVE_\(uuid)_\(timestamp)"
  }
}

// MARK: - State Enum

public enum Allow2State {
  case unpaired
  case paired
  case childSelected(Allow2Child)
  case childCleared
  case requireChildSelection
  case blocked(Allow2CheckResult)
  case unblocked
}

// MARK: - Warning Level

public enum WarningLevel: Equatable {
  case none
  case gentle(minutes: Int)  // 15, 10 min
  case warning(minutes: Int)  // 5, 3 min
  case urgent(seconds: Int)  // 60, 30, 10 sec
  case blocked

  static func from(seconds: Int) -> WarningLevel {
    switch seconds {
    case ...0:
      return .blocked
    case 1...60:
      return .urgent(seconds: seconds)
    case 61...300:
      return .warning(minutes: seconds / 60)
    case 301...900:
      return .gentle(minutes: seconds / 60)
    default:
      return .none
    }
  }
}

// MARK: - Error Types

public enum Allow2Error: Error, LocalizedError {
  case notPaired
  case invalidCredentials
  case invalidResponse
  case serverError(Int)
  case pairingFailed(String)
  case notImplemented
  case pinValidationFailed

  public var errorDescription: String? {
    switch self {
    case .notPaired:
      return "Device is not paired with Allow2"
    case .invalidCredentials:
      return "Invalid email or password"
    case .invalidResponse:
      return "Invalid response from server"
    case .serverError(let code):
      return "Server error: \(code)"
    case .pairingFailed(let message):
      return "Pairing failed: \(message)"
    case .notImplemented:
      return "This feature is not yet implemented"
    case .pinValidationFailed:
      return "Invalid PIN"
    }
  }
}

// MARK: - Integration with Usage Tracker

extension Allow2Manager {

  /// Integrate with the usage tracker for navigation events
  /// - Parameter url: URL being navigated to
  public func trackNavigation(to url: URL) {
    Allow2UsageTracker.shared.trackNavigation(to: url)
  }

  /// Get the usage tracker instance
  public var usageTracker: Allow2UsageTracker {
    return Allow2UsageTracker.shared
  }

  /// Get the credential manager instance
  public var credentialManager: Allow2CredentialManager {
    return Allow2CredentialManager.shared
  }

  /// Get the API client instance
  public var apiClient: Allow2APIClient {
    return Allow2APIClient.shared
  }
}

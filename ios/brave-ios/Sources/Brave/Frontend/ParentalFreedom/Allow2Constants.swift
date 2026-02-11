// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Foundation

// MARK: - Allow2 Constants

/// Central configuration constants for Allow2 integration
///
/// ENVIRONMENT VARIABLE OVERRIDE:
/// Set ALLOW2_BASE_URL to override BOTH api and service hosts.
/// Example: ALLOW2_BASE_URL=https://staging.allow2.com
/// This is useful for staging/development environments.
/// See ~/ai/allow2/STAGING_ENV.md for full documentation.
public enum Allow2Constants {

  // MARK: - Environment Variable Override

  /// Environment variable name for base URL override
  public static let baseURLEnvVar = "ALLOW2_BASE_URL"

  /// Check if using a custom/staging endpoint
  public static var isUsingCustomEndpoint: Bool {
    return ProcessInfo.processInfo.environment[baseURLEnvVar] != nil
  }

  /// Get the custom base URL from environment variable, if set
  private static var customBaseURL: String? {
    return ProcessInfo.processInfo.environment[baseURLEnvVar]
  }

  // MARK: - API Hosts

  /// Default API host for pairing and authentication endpoints
  public static let defaultApiHost = "api.allow2.com"

  /// Default service host for usage tracking and check endpoints
  public static let defaultServiceHost = "service.allow2.com"

  /// API host (uses ALLOW2_BASE_URL if set, otherwise default)
  public static var apiHost: String {
    if let custom = customBaseURL, let url = URL(string: custom) {
      return url.host ?? defaultApiHost
    }
    return defaultApiHost
  }

  /// Service host (uses ALLOW2_BASE_URL if set, otherwise default)
  public static var serviceHost: String {
    if let custom = customBaseURL, let url = URL(string: custom) {
      return url.host ?? defaultServiceHost
    }
    return defaultServiceHost
  }

  // MARK: - API Base URLs

  /// Base URL for pairing API endpoints (respects ALLOW2_BASE_URL override)
  public static var apiBaseURL: URL {
    if let custom = customBaseURL, let url = URL(string: custom) {
      return url
    }
    return URL(string: "https://\(defaultApiHost)")!
  }

  /// Base URL for service API endpoints (respects ALLOW2_BASE_URL override)
  public static var serviceBaseURL: URL {
    if let custom = customBaseURL, let url = URL(string: custom) {
      return url
    }
    return URL(string: "https://\(defaultServiceHost)")!
  }

  // MARK: - Pairing Endpoints (api.allow2.com)

  public enum PairingEndpoint {
    /// Initialize QR code pairing session
    public static let initQR = apiBaseURL.appendingPathComponent("api/pair/qr/init")

    /// Initialize PIN code pairing session
    public static let initPIN = apiBaseURL.appendingPathComponent("api/pair/pin/init")

    /// Check pairing status (poll until completed)
    public static func status(sessionId: String) -> URL {
      var components = URLComponents(url: apiBaseURL.appendingPathComponent("api/pair/qr/status"),
                                     resolvingAgainstBaseURL: false)!
      components.path = "/api/pair/qr/status/\(sessionId)"
      return components.url!
    }

    /// Cancel active pairing session
    public static let cancel = apiBaseURL.appendingPathComponent("api/pair/cancel")
  }

  // MARK: - Service Endpoints (service.allow2.com)

  public enum ServiceEndpoint {
    /// Check usage status and log activity
    public static let check = serviceBaseURL.appendingPathComponent("serviceapi/check")
  }

  // MARK: - Request Endpoints (api.allow2.com)

  public enum RequestEndpoint {
    /// Request more time from parent
    public static let createRequest = apiBaseURL.appendingPathComponent("request/createRequest")
  }

  // MARK: - Activity IDs

  /// Activity types for usage tracking
  public enum ActivityID: Int, CaseIterable, Codable {
    /// General internet browsing
    case internet = 1

    /// Gaming activity
    case gaming = 3

    /// Screen time tracking
    case screenTime = 8

    /// Social media usage
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

    /// System image name for the activity
    public var systemImageName: String {
      switch self {
      case .internet: return "globe"
      case .gaming: return "gamecontroller"
      case .screenTime: return "clock"
      case .social: return "bubble.left.and.bubble.right"
      }
    }
  }

  // MARK: - Timing Constants

  public enum Timing {
    /// Interval between periodic usage checks (seconds)
    public static let checkIntervalSeconds: TimeInterval = 10

    /// Buffer time before cache expiry to trigger refresh (seconds)
    public static let cacheExpiryBuffer: TimeInterval = 30

    /// Time threshold for requiring child re-selection after background (seconds)
    public static let backgroundResumeThreshold: TimeInterval = 300  // 5 minutes

    /// Polling interval when waiting for pairing completion (seconds)
    public static let pairingPollInterval: TimeInterval = 2.0

    /// Maximum time to wait for pairing completion (seconds)
    public static let pairingTimeout: TimeInterval = 300  // 5 minutes
  }

  // MARK: - Warning Thresholds

  public enum WarningThreshold {
    /// Gentle warning threshold (seconds)
    public static let gentle: Int = 900  // 15 minutes

    /// Warning threshold (seconds)
    public static let warning: Int = 300  // 5 minutes

    /// Urgent warning threshold (seconds)
    public static let urgent: Int = 60  // 1 minute

    /// All thresholds at which to show warnings
    public static let allThresholds: [Int] = [900, 600, 300, 180, 60, 30, 10]
  }

  // MARK: - Keychain Keys

  public enum KeychainKey {
    /// Service name for keychain items
    public static let serviceName = "com.brave.allow2"

    /// Key for storing credentials
    public static let credentials = "allow2.credentials"

    /// Key for storing device token
    public static let deviceToken = "allow2.deviceToken"
  }

  // MARK: - UserDefaults Keys

  public enum UserDefaultsKey {
    /// Prefix for all Allow2 keys
    public static let prefix = "allow2."

    /// Whether Allow2 is enabled
    public static let isEnabled = "\(prefix)enabled"

    /// Selected child ID
    public static let selectedChildId = "\(prefix)selectedChildId"

    /// Last check timestamp
    public static let lastCheckTime = "\(prefix)lastCheckTime"

    /// Cached children list
    public static let cachedChildren = "\(prefix)cachedChildren"

    /// Show child shield on launch
    public static let showChildShieldOnLaunch = "\(prefix)showChildShieldOnLaunch"

    /// Session timeout (seconds)
    public static let sessionTimeout = "\(prefix)sessionTimeout"

    /// Last background timestamp
    public static let lastBackgroundTime = "\(prefix)lastBackgroundTime"
  }

  // MARK: - HTTP Headers

  public enum HTTPHeader {
    public static let contentType = "Content-Type"
    public static let accept = "Accept"
    public static let applicationJSON = "application/json"
    public static let userAgent = "User-Agent"
    public static let braveUserAgent = "Brave-iOS/Allow2"
  }

  // MARK: - Network Configuration

  public enum Network {
    /// Request timeout interval (seconds)
    public static let requestTimeout: TimeInterval = 30

    /// Resource timeout interval (seconds)
    public static let resourceTimeout: TimeInterval = 60
  }

  // MARK: - Deep Link URLs

  public enum DeepLink {
    /// Allow2 app URL scheme for QR pairing
    public static let pairScheme = "allow2"

    /// Generate QR code URL for pairing session
    public static func pairingURL(sessionId: String) -> String {
      return "\(pairScheme)://pair?s=\(sessionId)"
    }
  }

  // MARK: - External URLs

  public enum ExternalURL {
    /// Allow2 website
    public static let website = URL(string: "https://allow2.com")!

    /// Allow2 parent signup
    public static let signup = URL(string: "https://allow2.com/signup")!

    /// Allow2 help center
    public static let help = URL(string: "https://allow2.com/help")!

    /// Help for blocked users
    public static let blockedHelp = URL(string: "https://allow2.com/help/blocked")!

    /// Developer documentation
    public static let developer = URL(string: "https://developer.allow2.com")!
  }

  // MARK: - Localization Keys

  public enum LocalizationKey {
    public static let settingsTitle = "IDS_ALLOW2_SETTINGS_TITLE"
    public static let settingsSubtitle = "IDS_ALLOW2_SETTINGS_SUBTITLE"
    public static let pairTitle = "IDS_ALLOW2_PAIR_TITLE"
    public static let childSelectTitle = "IDS_ALLOW2_CHILD_SELECT_TITLE"
    public static let enterPin = "IDS_ALLOW2_ENTER_PIN"
    public static let blockedTitle = "IDS_ALLOW2_BLOCKED_TITLE"
    public static let blockedSubtitle = "IDS_ALLOW2_BLOCKED_SUBTITLE"
    public static let requestMoreTime = "IDS_ALLOW2_REQUEST_MORE_TIME"
    public static let switchUser = "IDS_ALLOW2_SWITCH_USER"
    public static let warning15min = "IDS_ALLOW2_WARNING_15MIN"
    public static let warning5min = "IDS_ALLOW2_WARNING_5MIN"
    public static let warning1min = "IDS_ALLOW2_WARNING_1MIN"
    public static let unpaired = "IDS_ALLOW2_UNPAIRED"
  }

  // MARK: - Social Media Domains

  /// Domains that trigger social media activity tracking
  public static let socialMediaDomains: Set<String> = [
    "facebook.com",
    "instagram.com",
    "twitter.com",
    "x.com",
    "tiktok.com",
    "snapchat.com",
    "linkedin.com",
    "pinterest.com",
    "reddit.com",
    "tumblr.com",
    "discord.com",
    "whatsapp.com",
    "messenger.com",
    "telegram.org",
    "signal.org",
  ]

  // MARK: - Gaming Domains

  /// Domains that trigger gaming activity tracking
  public static let gamingDomains: Set<String> = [
    "roblox.com",
    "minecraft.net",
    "fortnite.com",
    "epicgames.com",
    "steam.com",
    "steampowered.com",
    "twitch.tv",
    "xbox.com",
    "playstation.com",
    "nintendo.com",
    "unity.com",
    "unrealengine.com",
  ]
}

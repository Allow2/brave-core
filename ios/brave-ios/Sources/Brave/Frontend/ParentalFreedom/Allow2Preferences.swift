// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Foundation
import Preferences
import Security

// MARK: - Allow2 Credentials

/// Secure credentials for Allow2 API authentication
public struct Allow2Credentials: Codable, Equatable {
  public let userId: String
  public let pairId: String
  public let pairToken: String

  public init(userId: String, pairId: String, pairToken: String) {
    self.userId = userId
    self.pairId = pairId
    self.pairToken = pairToken
  }
}

// MARK: - Allow2 Child

/// Represents a child profile in Allow2
public struct Allow2Child: Codable, Equatable, Identifiable {
  public let id: UInt64
  public let name: String
  public let pinHash: String  // "sha256:..." format
  public let pinSalt: String

  public init(id: UInt64, name: String, pinHash: String, pinSalt: String) {
    self.id = id
    self.name = name
    self.pinHash = pinHash
    self.pinSalt = pinSalt
  }
}

// MARK: - Allow2 Preferences

/// Extension to Preferences for Allow2-related settings
extension Preferences {
  public final class Allow2 {
    /// Whether Allow2 parental controls are enabled
    public static let isEnabled = Option<Bool>(
      key: "allow2.enabled",
      default: false
    )

    /// Currently selected child ID (nil = shared device mode)
    public static let selectedChildId = Option<UInt64?>(
      key: "allow2.selectedChildId",
      default: nil
    )

    /// Last check timestamp
    public static let lastCheckTime = Option<Date?>(
      key: "allow2.lastCheckTime",
      default: nil
    )

    /// Whether the child selection shield should be shown on next launch
    public static let showChildShieldOnLaunch = Option<Bool>(
      key: "allow2.showChildShieldOnLaunch",
      default: true
    )

    /// Session timeout for shared device mode (seconds)
    public static let sessionTimeout = Option<Int>(
      key: "allow2.sessionTimeout",
      default: 300
    )
  }
}

// MARK: - Allow2Preferences Helper

/// Helper class for managing Allow2 preferences and secure credential storage
public enum Allow2Preferences {

  // MARK: - Convenience Accessors

  /// Whether Allow2 is enabled
  public static var isEnabled: Preferences.Option<Bool> {
    return Preferences.Allow2.isEnabled
  }

  /// Selected child ID
  public static var selectedChildId: Preferences.Option<UInt64?> {
    return Preferences.Allow2.selectedChildId
  }

  /// Last check time
  public static var lastCheckTime: Preferences.Option<Date?> {
    return Preferences.Allow2.lastCheckTime
  }

  // MARK: - Secure Credentials (Keychain)

  /// Credentials stored securely in Keychain
  public static var credentials: Allow2Credentials? {
    get {
      guard let data = KeychainHelper.load(key: KeychainKeys.credentials) else {
        return nil
      }
      return try? JSONDecoder().decode(Allow2Credentials.self, from: data)
    }
    set {
      if let newValue = newValue,
        let data = try? JSONEncoder().encode(newValue)
      {
        KeychainHelper.save(key: KeychainKeys.credentials, data: data)
      } else {
        KeychainHelper.delete(key: KeychainKeys.credentials)
      }
    }
  }

  // MARK: - Cached Children List

  /// Cached children list (stored in UserDefaults as Data)
  public static var cachedChildren: [Allow2Child] {
    get {
      guard let data = Preferences.defaultContainer.data(forKey: CacheKeys.children) else {
        return []
      }
      return (try? JSONDecoder().decode([Allow2Child].self, from: data)) ?? []
    }
    set {
      if let data = try? JSONEncoder().encode(newValue) {
        Preferences.defaultContainer.set(data, forKey: CacheKeys.children)
      } else {
        Preferences.defaultContainer.removeObject(forKey: CacheKeys.children)
      }
    }
  }

  // MARK: - Clear All

  /// Clear all Allow2 data (called on unpair)
  public static func clearAll() {
    // Clear preferences
    Preferences.Allow2.isEnabled.reset()
    Preferences.Allow2.selectedChildId.reset()
    Preferences.Allow2.lastCheckTime.reset()
    Preferences.Allow2.showChildShieldOnLaunch.reset()

    // Clear cached children
    Preferences.defaultContainer.removeObject(forKey: CacheKeys.children)

    // Clear keychain credentials
    KeychainHelper.delete(key: KeychainKeys.credentials)
  }

  // MARK: - Keys

  private enum KeychainKeys {
    static let credentials = "allow2.credentials"
  }

  private enum CacheKeys {
    static let children = "allow2.cachedChildren"
  }
}

// MARK: - Keychain Helper

/// Simple Keychain wrapper for secure credential storage
enum KeychainHelper {

  private static let serviceName = "com.brave.allow2"

  /// Save data to Keychain
  static func save(key: String, data: Data) {
    let query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: serviceName,
      kSecAttrAccount as String: key,
      kSecValueData as String: data,
      kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlock,
    ]

    // Delete existing item first
    SecItemDelete(query as CFDictionary)

    // Add new item
    let status = SecItemAdd(query as CFDictionary, nil)
    if status != errSecSuccess && status != errSecDuplicateItem {
      assertionFailure("Keychain save failed with status: \(status)")
    }
  }

  /// Load data from Keychain
  static func load(key: String) -> Data? {
    let query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: serviceName,
      kSecAttrAccount as String: key,
      kSecReturnData as String: true,
      kSecMatchLimit as String: kSecMatchLimitOne,
    ]

    var result: AnyObject?
    let status = SecItemCopyMatching(query as CFDictionary, &result)

    guard status == errSecSuccess else {
      return nil
    }

    return result as? Data
  }

  /// Delete data from Keychain
  static func delete(key: String) {
    let query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: serviceName,
      kSecAttrAccount as String: key,
    ]

    SecItemDelete(query as CFDictionary)
  }

  /// Check if a key exists in Keychain
  static func exists(key: String) -> Bool {
    let query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: serviceName,
      kSecAttrAccount as String: key,
      kSecReturnData as String: false,
    ]

    let status = SecItemCopyMatching(query as CFDictionary, nil)
    return status == errSecSuccess
  }
}

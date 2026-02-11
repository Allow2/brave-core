// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Foundation
import Security
import os.log

// MARK: - Allow2 Credential Manager

/// Secure credential storage manager using iOS Keychain Services
/// All sensitive data (tokens, credentials) are stored encrypted in the Keychain
public final class Allow2CredentialManager {

  // MARK: - Singleton

  public static let shared = Allow2CredentialManager()

  // MARK: - Properties

  private let serviceName = Allow2Constants.KeychainKey.serviceName

  // MARK: - Initialization

  private init() {}

  // MARK: - Credentials Management

  /// Store Allow2 credentials securely
  /// - Parameter credentials: Credentials to store
  /// - Throws: KeychainError if storage fails
  public func storeCredentials(_ credentials: Allow2Credentials) throws {
    let data = try JSONEncoder().encode(credentials)
    try save(key: Allow2Constants.KeychainKey.credentials, data: data)
    Logger.module.info("Allow2: Credentials stored successfully")
  }

  /// Retrieve stored credentials
  /// - Returns: Stored credentials or nil if not found
  public func retrieveCredentials() -> Allow2Credentials? {
    guard let data = load(key: Allow2Constants.KeychainKey.credentials) else {
      return nil
    }

    do {
      return try JSONDecoder().decode(Allow2Credentials.self, from: data)
    } catch {
      Logger.module.error("Allow2: Failed to decode credentials: \(error.localizedDescription)")
      return nil
    }
  }

  /// Delete stored credentials
  public func deleteCredentials() {
    delete(key: Allow2Constants.KeychainKey.credentials)
    Logger.module.info("Allow2: Credentials deleted")
  }

  /// Check if credentials exist
  public var hasCredentials: Bool {
    return exists(key: Allow2Constants.KeychainKey.credentials)
  }

  // MARK: - Device Token Management

  /// Store device token securely
  /// - Parameter token: Device token to store
  public func storeDeviceToken(_ token: String) throws {
    guard let data = token.data(using: .utf8) else {
      throw KeychainError.encodingFailed
    }
    try save(key: Allow2Constants.KeychainKey.deviceToken, data: data)
  }

  /// Retrieve stored device token
  /// - Returns: Device token or nil if not found
  public func retrieveDeviceToken() -> String? {
    guard let data = load(key: Allow2Constants.KeychainKey.deviceToken) else {
      return nil
    }
    return String(data: data, encoding: .utf8)
  }

  /// Generate a new device token
  /// - Returns: New unique device token
  public func generateDeviceToken() -> String {
    let uuid = UUID().uuidString
    let timestamp = String(Int(Date().timeIntervalSince1970))
    return "BRAVE_IOS_\(uuid)_\(timestamp)"
  }

  // MARK: - Clear All

  /// Clear all Allow2 keychain items
  public func clearAll() {
    delete(key: Allow2Constants.KeychainKey.credentials)
    delete(key: Allow2Constants.KeychainKey.deviceToken)
    Logger.module.info("Allow2: All keychain items cleared")
  }

  // MARK: - Keychain Operations

  /// Save data to Keychain
  private func save(key: String, data: Data) throws {
    // First try to delete any existing item
    delete(key: key)

    let query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: serviceName,
      kSecAttrAccount as String: key,
      kSecValueData as String: data,
      kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlock,
    ]

    let status = SecItemAdd(query as CFDictionary, nil)

    guard status == errSecSuccess else {
      Logger.module.error("Allow2 Keychain: Save failed with status \(status)")
      throw KeychainError.saveFailed(status)
    }
  }

  /// Load data from Keychain
  private func load(key: String) -> Data? {
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
      if status != errSecItemNotFound {
        Logger.module.error("Allow2 Keychain: Load failed with status \(status)")
      }
      return nil
    }

    return result as? Data
  }

  /// Delete item from Keychain
  private func delete(key: String) {
    let query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: serviceName,
      kSecAttrAccount as String: key,
    ]

    let status = SecItemDelete(query as CFDictionary)

    if status != errSecSuccess && status != errSecItemNotFound {
      Logger.module.warning("Allow2 Keychain: Delete returned status \(status)")
    }
  }

  /// Check if item exists in Keychain
  private func exists(key: String) -> Bool {
    let query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: serviceName,
      kSecAttrAccount as String: key,
      kSecReturnData as String: false,
    ]

    let status = SecItemCopyMatching(query as CFDictionary, nil)
    return status == errSecSuccess
  }

  /// Update existing item in Keychain
  private func update(key: String, data: Data) throws {
    let query: [String: Any] = [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: serviceName,
      kSecAttrAccount as String: key,
    ]

    let attributes: [String: Any] = [
      kSecValueData as String: data
    ]

    let status = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)

    guard status == errSecSuccess else {
      Logger.module.error("Allow2 Keychain: Update failed with status \(status)")
      throw KeychainError.updateFailed(status)
    }
  }
}

// MARK: - Keychain Error

public enum KeychainError: Error, LocalizedError {
  case saveFailed(OSStatus)
  case loadFailed(OSStatus)
  case updateFailed(OSStatus)
  case deleteFailed(OSStatus)
  case encodingFailed
  case decodingFailed

  public var errorDescription: String? {
    switch self {
    case .saveFailed(let status):
      return "Failed to save to keychain (status: \(status))"
    case .loadFailed(let status):
      return "Failed to load from keychain (status: \(status))"
    case .updateFailed(let status):
      return "Failed to update keychain item (status: \(status))"
    case .deleteFailed(let status):
      return "Failed to delete keychain item (status: \(status))"
    case .encodingFailed:
      return "Failed to encode data for keychain"
    case .decodingFailed:
      return "Failed to decode data from keychain"
    }
  }
}

// MARK: - Credential Validation

extension Allow2CredentialManager {

  /// Validate credentials structure
  /// - Parameter credentials: Credentials to validate
  /// - Returns: True if credentials appear valid
  public func validateCredentials(_ credentials: Allow2Credentials) -> Bool {
    // Basic validation - ensure fields are not empty
    guard !credentials.userId.isEmpty,
          !credentials.pairId.isEmpty,
          !credentials.pairToken.isEmpty
    else {
      return false
    }

    return true
  }

  /// Check if stored credentials are valid
  public var areCredentialsValid: Bool {
    guard let credentials = retrieveCredentials() else {
      return false
    }
    return validateCredentials(credentials)
  }
}

// MARK: - Migration Support

extension Allow2CredentialManager {

  /// Migrate credentials from legacy storage if needed
  /// This supports migration from older app versions
  public func migrateIfNeeded() {
    // Check for legacy UserDefaults storage
    let legacyKey = "allow2.legacyCredentials"
    guard let legacyData = UserDefaults.standard.data(forKey: legacyKey) else {
      return
    }

    // Attempt to decode and migrate
    do {
      let credentials = try JSONDecoder().decode(Allow2Credentials.self, from: legacyData)
      try storeCredentials(credentials)

      // Clear legacy storage after successful migration
      UserDefaults.standard.removeObject(forKey: legacyKey)
      Logger.module.info("Allow2: Migrated credentials from legacy storage")
    } catch {
      Logger.module.error("Allow2: Failed to migrate legacy credentials: \(error.localizedDescription)")
    }
  }
}

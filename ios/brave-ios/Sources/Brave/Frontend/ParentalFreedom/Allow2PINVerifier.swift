// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import CryptoKit
import Foundation

// MARK: - Allow2 PIN Verifier

/// Utility for secure PIN verification using constant-time comparison
/// to prevent timing attacks
public enum Allow2PINVerifier {

  // MARK: - Hash Formats

  /// Supported hash formats
  public enum HashFormat: String {
    case sha256 = "sha256"
    case sha512 = "sha512"
  }

  // MARK: - PIN Verification

  /// Verify a PIN against a stored hash
  /// - Parameters:
  ///   - pin: The entered PIN to verify
  ///   - child: The child whose PIN hash to verify against
  /// - Returns: True if PIN is valid
  public static func verifyPIN(_ pin: String, for child: Allow2Child) -> Bool {
    // Hash the entered PIN with the child's salt
    let computedHash = hashPIN(pin, salt: child.pinSalt)

    // Perform constant-time comparison
    return constantTimeCompare(computedHash, child.pinHash)
  }

  /// Hash a PIN with the given salt
  /// - Parameters:
  ///   - pin: PIN to hash
  ///   - salt: Salt value
  ///   - format: Hash format (default: sha256)
  /// - Returns: Formatted hash string (e.g., "sha256:abcd1234...")
  public static func hashPIN(
    _ pin: String,
    salt: String,
    format: HashFormat = .sha256
  ) -> String {
    let saltedPin = pin + salt
    let inputData = Data(saltedPin.utf8)

    switch format {
    case .sha256:
      let hash = SHA256.hash(data: inputData)
      let hashString = hash.compactMap { String(format: "%02x", $0) }.joined()
      return "\(format.rawValue):\(hashString)"

    case .sha512:
      let hash = SHA512.hash(data: inputData)
      let hashString = hash.compactMap { String(format: "%02x", $0) }.joined()
      return "\(format.rawValue):\(hashString)"
    }
  }

  // MARK: - Constant-Time Comparison

  /// Perform a constant-time comparison of two strings
  /// This prevents timing attacks by ensuring the comparison takes
  /// the same amount of time regardless of where differences occur
  /// - Parameters:
  ///   - a: First string
  ///   - b: Second string
  /// - Returns: True if strings are equal
  public static func constantTimeCompare(_ a: String, _ b: String) -> Bool {
    // First, ensure same length (this is not constant-time, but the
    // hash lengths should always match for valid comparisons)
    guard a.count == b.count else {
      return false
    }

    var result: UInt8 = 0

    // XOR each byte pair
    for (byteA, byteB) in zip(a.utf8, b.utf8) {
      result |= byteA ^ byteB
    }

    return result == 0
  }

  /// Perform constant-time comparison of two byte arrays
  /// - Parameters:
  ///   - a: First byte array
  ///   - b: Second byte array
  /// - Returns: True if arrays are equal
  public static func constantTimeCompare(_ a: [UInt8], _ b: [UInt8]) -> Bool {
    guard a.count == b.count else {
      return false
    }

    var result: UInt8 = 0

    for i in 0..<a.count {
      result |= a[i] ^ b[i]
    }

    return result == 0
  }

  /// Perform constant-time comparison of two Data objects
  /// - Parameters:
  ///   - a: First Data
  ///   - b: Second Data
  /// - Returns: True if Data objects are equal
  public static func constantTimeCompare(_ a: Data, _ b: Data) -> Bool {
    guard a.count == b.count else {
      return false
    }

    var result: UInt8 = 0

    for i in 0..<a.count {
      result |= a[i] ^ b[i]
    }

    return result == 0
  }

  // MARK: - PIN Validation

  /// Validate PIN format
  /// - Parameter pin: PIN to validate
  /// - Returns: True if PIN format is valid
  public static func isValidPINFormat(_ pin: String) -> Bool {
    // PIN must be exactly 4 digits
    guard pin.count == 4 else {
      return false
    }

    // All characters must be digits
    return pin.allSatisfy { $0.isNumber }
  }

  /// Validate PIN strength (optional - not used for simple 4-digit PINs)
  /// - Parameter pin: PIN to check
  /// - Returns: Strength assessment
  public static func pinStrength(_ pin: String) -> PINStrength {
    guard isValidPINFormat(pin) else {
      return .invalid
    }

    // Check for repeating digits (e.g., 1111)
    if Set(pin).count == 1 {
      return .weak
    }

    // Check for sequential digits (e.g., 1234, 4321)
    let digits = pin.compactMap { Int(String($0)) }
    let isAscending = zip(digits, digits.dropFirst()).allSatisfy { $1 == $0 + 1 }
    let isDescending = zip(digits, digits.dropFirst()).allSatisfy { $1 == $0 - 1 }

    if isAscending || isDescending {
      return .weak
    }

    // Check for common PINs
    let commonPINs = ["0000", "1111", "2222", "3333", "4444", "5555",
                      "6666", "7777", "8888", "9999", "1234", "4321",
                      "0123", "3210", "1122", "2211", "1212", "2121"]

    if commonPINs.contains(pin) {
      return .weak
    }

    return .strong
  }

  // MARK: - Hash Parsing

  /// Parse hash format from a stored hash string
  /// - Parameter hashString: Stored hash (e.g., "sha256:abcd1234...")
  /// - Returns: Tuple of (format, hashValue) or nil if invalid
  public static func parseHash(_ hashString: String) -> (format: HashFormat, hash: String)? {
    let components = hashString.split(separator: ":", maxSplits: 1)
    guard components.count == 2 else {
      return nil
    }

    let formatString = String(components[0])
    let hashValue = String(components[1])

    guard let format = HashFormat(rawValue: formatString) else {
      return nil
    }

    return (format, hashValue)
  }
}

// MARK: - PIN Strength

public enum PINStrength {
  case invalid
  case weak
  case strong

  public var description: String {
    switch self {
    case .invalid:
      return "Invalid PIN format"
    case .weak:
      return "Weak PIN - easily guessable"
    case .strong:
      return "Strong PIN"
    }
  }

  public var isAcceptable: Bool {
    return self == .weak || self == .strong
  }
}

// MARK: - String Extension

extension String {
  /// Performs a constant-time comparison to prevent timing attacks
  /// - Parameter other: String to compare against
  /// - Returns: True if strings are equal
  func secureCompare(to other: String) -> Bool {
    return Allow2PINVerifier.constantTimeCompare(self, other)
  }
}

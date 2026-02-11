// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Foundation

// MARK: - Allow2 Check Result

/// Response model from Allow2 check API
public struct Allow2CheckResult: Codable {
  /// Whether the requested activities are allowed
  public let allowed: Bool

  /// Activity-specific results keyed by activity ID
  public let activities: [String: ActivityResult]

  /// Day type information
  public let dayTypes: DayTypes?

  // MARK: - Computed Properties

  /// The minimum remaining seconds across all activities
  public var minimumRemainingSeconds: Int? {
    let remainingTimes = activities.values.compactMap { $0.remaining }
    return remainingTimes.min()
  }

  /// The soonest time block end across all activities
  public var soonestTimeBlockEnd: Date? {
    let endTimes = activities.values.compactMap { $0.timeblock?.endsDate }
    return endTimes.min()
  }

  /// Human-readable explanation of why access is blocked
  public var explanation: String {
    var reasons: [String] = []

    for (_, activity) in activities {
      if activity.banned {
        reasons.append("You are currently banned from \(activity.name).")
      } else if let timeblock = activity.timeblock, !timeblock.allowed {
        reasons.append("You cannot use \(activity.name) at this time.")
      } else if let remaining = activity.remaining, remaining <= 0 {
        reasons.append("You've used all your \(activity.name) time today.")
      }
    }

    return reasons.isEmpty ? "Access is restricted." : reasons.joined(separator: "\n")
  }

  /// Get result for a specific activity
  public func result(for activity: Allow2Activity) -> ActivityResult? {
    return activities[String(activity.rawValue)]
  }
}

// MARK: - Activity Result

/// Result for a specific activity type
public struct ActivityResult: Codable {
  /// Activity ID
  public let id: Int

  /// Activity name (e.g., "Internet", "Gaming")
  public let name: String

  /// Remaining time in seconds
  public let remaining: Int?

  /// Cache expiry timestamp
  public let expires: TimeInterval?

  /// Whether the child is banned from this activity
  public let banned: Bool

  /// Time block restrictions
  public let timeblock: TimeBlockResult?

  // MARK: - Computed Properties

  /// Whether this activity is currently allowed
  public var isAllowed: Bool {
    if banned { return false }
    if let timeblock = timeblock, !timeblock.allowed { return false }
    if let remaining = remaining, remaining <= 0 { return false }
    return true
  }

  /// Remaining time as TimeInterval
  public var remainingTimeInterval: TimeInterval? {
    guard let remaining = remaining else { return nil }
    return TimeInterval(remaining)
  }

  /// Cache expiry date
  public var expiresDate: Date? {
    guard let expires = expires else { return nil }
    return Date(timeIntervalSince1970: expires)
  }

  /// Formatted remaining time string (e.g., "15:00", "1:30:00")
  public var formattedRemainingTime: String? {
    guard let remaining = remaining, remaining > 0 else { return nil }

    let hours = remaining / 3600
    let minutes = (remaining % 3600) / 60
    let seconds = remaining % 60

    if hours > 0 {
      return String(format: "%d:%02d:%02d", hours, minutes, seconds)
    } else {
      return String(format: "%d:%02d", minutes, seconds)
    }
  }
}

// MARK: - Time Block Result

/// Time block restriction information
public struct TimeBlockResult: Codable {
  /// Whether access is allowed during current time block
  public let allowed: Bool

  /// Timestamp when this time block ends
  public let ends: TimeInterval?

  /// Time block end date
  public var endsDate: Date? {
    guard let ends = ends else { return nil }
    return Date(timeIntervalSince1970: ends)
  }

  /// Formatted time block end string
  public var formattedEndsTime: String? {
    guard let endsDate = endsDate else { return nil }
    let formatter = DateFormatter()
    formatter.dateStyle = .none
    formatter.timeStyle = .short
    return formatter.string(from: endsDate)
  }
}

// MARK: - Day Types

/// Information about day type (e.g., "School Night", "Weekend")
public struct DayTypes: Codable {
  /// Today's day type
  public let today: DayType?

  /// Tomorrow's day type
  public let tomorrow: DayType?
}

// MARK: - Day Type

/// Individual day type information
public struct DayType: Codable {
  /// Day type ID
  public let id: Int

  /// Day type name (e.g., "School Night", "Weekend")
  public let name: String
}

// MARK: - Request More Time

/// Request for additional time
public struct MoreTimeRequest: Codable {
  /// Activity to request time for
  public let activityId: Int

  /// Requested duration in minutes
  public let duration: Int

  /// Optional message to parent
  public let message: String?

  public init(activity: Allow2Activity, duration: Int, message: String? = nil) {
    self.activityId = activity.rawValue
    self.duration = duration
    self.message = message
  }
}

// MARK: - Request Duration Options

/// Predefined duration options for requesting more time
public enum MoreTimeDuration: Int, CaseIterable {
  case fifteenMinutes = 15
  case thirtyMinutes = 30
  case oneHour = 60
  case untilBedtime = -1  // Special value for "until bedtime"

  public var displayString: String {
    switch self {
    case .fifteenMinutes:
      return "15 minutes"
    case .thirtyMinutes:
      return "30 minutes"
    case .oneHour:
      return "1 hour"
    case .untilBedtime:
      return "Until bedtime"
    }
  }
}

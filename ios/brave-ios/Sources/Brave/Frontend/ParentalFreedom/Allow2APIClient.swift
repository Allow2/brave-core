// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Foundation
import os.log

// MARK: - Allow2 API Client

/// Dedicated network layer for Allow2 API communication
/// Uses URLSession with async/await for all network operations
public final class Allow2APIClient {

  // MARK: - Singleton

  public static let shared = Allow2APIClient()

  // MARK: - Properties

  private lazy var urlSession: URLSession = {
    let config = URLSessionConfiguration.default
    config.timeoutIntervalForRequest = Allow2Constants.Network.requestTimeout
    config.timeoutIntervalForResource = Allow2Constants.Network.resourceTimeout
    config.waitsForConnectivity = true
    return URLSession(configuration: config)
  }()

  private let decoder: JSONDecoder = {
    let decoder = JSONDecoder()
    decoder.keyDecodingStrategy = .convertFromSnakeCase
    return decoder
  }()

  private let encoder: JSONEncoder = {
    let encoder = JSONEncoder()
    encoder.keyEncodingStrategy = .convertToSnakeCase
    return encoder
  }()

  // MARK: - Initialization

  private init() {}

  // MARK: - Pairing API (api.allow2.com)

  /// Initialize QR code pairing session
  /// - Parameters:
  ///   - deviceName: Human-readable name for this device
  ///   - deviceToken: Unique device identifier token
  /// - Returns: Pairing session with QR code URL and session ID
  public func initQRPairing(
    deviceName: String,
    deviceToken: String
  ) async throws -> InitPairingResponse {
    let body = InitPairingRequest(
      uuid: UUID().uuidString,
      name: deviceName,
      platform: "iOS",
      deviceToken: deviceToken
    )

    return try await post(
      url: Allow2Constants.PairingEndpoint.initQR,
      body: body,
      responseType: InitPairingResponse.self
    )
  }

  /// Initialize PIN code pairing session
  /// - Parameters:
  ///   - deviceName: Human-readable name for this device
  ///   - deviceToken: Unique device identifier token
  /// - Returns: Pairing session with PIN code and session ID
  public func initPINPairing(
    deviceName: String,
    deviceToken: String
  ) async throws -> InitPairingResponse {
    let body = InitPairingRequest(
      uuid: UUID().uuidString,
      name: deviceName,
      platform: "iOS",
      deviceToken: deviceToken
    )

    return try await post(
      url: Allow2Constants.PairingEndpoint.initPIN,
      body: body,
      responseType: InitPairingResponse.self
    )
  }

  /// Check pairing status (poll until completed)
  /// - Parameter sessionId: Session ID from init pairing
  /// - Returns: Current pairing status
  public func checkPairingStatus(sessionId: String) async throws -> PairingStatusResponse {
    let url = Allow2Constants.PairingEndpoint.status(sessionId: sessionId)
    return try await get(url: url, responseType: PairingStatusResponse.self)
  }

  /// Cancel active pairing session
  /// - Parameter sessionId: Session ID to cancel
  public func cancelPairing(sessionId: String) async throws {
    let body = CancelPairingRequest(sessionId: sessionId)

    let _ = try await post(
      url: Allow2Constants.PairingEndpoint.cancel,
      body: body,
      responseType: EmptyResponse.self
    )
  }

  // MARK: - Service API (service.allow2.com)

  /// Check usage status and optionally log activity
  /// - Parameters:
  ///   - credentials: Allow2 credentials
  ///   - childId: ID of the child to check
  ///   - activities: Activities to check/log
  ///   - log: Whether to log usage time
  /// - Returns: Check result with activity status
  public func check(
    credentials: Allow2Credentials,
    childId: UInt64,
    activities: [Allow2Activity],
    log: Bool
  ) async throws -> Allow2CheckResult {
    let activityEntries = activities.map { activity -> ActivityEntry in
      return ActivityEntry(id: activity.rawValue, log: log)
    }

    let body = CheckRequest(
      userId: credentials.userId,
      pairId: credentials.pairId,
      pairToken: credentials.pairToken,
      childId: childId,
      activities: activityEntries,
      tz: TimeZone.current.identifier
    )

    return try await post(
      url: Allow2Constants.ServiceEndpoint.check,
      body: body,
      responseType: Allow2CheckResult.self
    )
  }

  // MARK: - Request API (api.allow2.com)

  /// Request more time from parent
  /// - Parameters:
  ///   - credentials: Allow2 credentials
  ///   - childId: ID of the requesting child
  ///   - activity: Activity to request time for
  ///   - duration: Requested duration in minutes
  ///   - message: Optional message to parent
  public func requestMoreTime(
    credentials: Allow2Credentials,
    childId: UInt64,
    activity: Allow2Activity,
    duration: Int,
    message: String?
  ) async throws {
    let body = MoreTimeRequestBody(
      userId: credentials.userId,
      pairId: credentials.pairId,
      pairToken: credentials.pairToken,
      childId: childId,
      activityId: activity.rawValue,
      requestedMinutes: duration,
      message: message
    )

    let _ = try await post(
      url: Allow2Constants.RequestEndpoint.createRequest,
      body: body,
      responseType: EmptyResponse.self
    )
  }

  // MARK: - Generic HTTP Methods

  private func get<T: Decodable>(
    url: URL,
    responseType: T.Type
  ) async throws -> T {
    var request = URLRequest(url: url)
    request.httpMethod = "GET"
    request.setValue(
      Allow2Constants.HTTPHeader.applicationJSON,
      forHTTPHeaderField: Allow2Constants.HTTPHeader.accept
    )
    request.setValue(
      Allow2Constants.HTTPHeader.braveUserAgent,
      forHTTPHeaderField: Allow2Constants.HTTPHeader.userAgent
    )

    return try await execute(request: request, responseType: responseType)
  }

  private func post<T: Encodable, R: Decodable>(
    url: URL,
    body: T,
    responseType: R.Type
  ) async throws -> R {
    var request = URLRequest(url: url)
    request.httpMethod = "POST"
    request.setValue(
      Allow2Constants.HTTPHeader.applicationJSON,
      forHTTPHeaderField: Allow2Constants.HTTPHeader.contentType
    )
    request.setValue(
      Allow2Constants.HTTPHeader.applicationJSON,
      forHTTPHeaderField: Allow2Constants.HTTPHeader.accept
    )
    request.setValue(
      Allow2Constants.HTTPHeader.braveUserAgent,
      forHTTPHeaderField: Allow2Constants.HTTPHeader.userAgent
    )
    request.httpBody = try encoder.encode(body)

    return try await execute(request: request, responseType: responseType)
  }

  private func execute<T: Decodable>(
    request: URLRequest,
    responseType: T.Type
  ) async throws -> T {
    let (data, response) = try await urlSession.data(for: request)

    guard let httpResponse = response as? HTTPURLResponse else {
      Logger.module.error("Allow2 API: Invalid response type")
      throw Allow2APIError.invalidResponse
    }

    // Handle 401 - device unpaired remotely
    if httpResponse.statusCode == 401 {
      Logger.module.warning("Allow2 API: Device unpaired (401)")
      throw Allow2APIError.unauthorized
    }

    // Handle other error status codes
    guard (200...299).contains(httpResponse.statusCode) else {
      Logger.module.error("Allow2 API: Server error \(httpResponse.statusCode)")
      throw Allow2APIError.serverError(httpResponse.statusCode)
    }

    // Handle empty response
    if data.isEmpty, T.self == EmptyResponse.self {
      return EmptyResponse() as! T
    }

    do {
      return try decoder.decode(T.self, from: data)
    } catch {
      Logger.module.error("Allow2 API: Decoding failed: \(error.localizedDescription)")
      throw Allow2APIError.decodingFailed(error)
    }
  }
}

// MARK: - API Error Types

public enum Allow2APIError: Error, LocalizedError {
  case invalidResponse
  case unauthorized
  case serverError(Int)
  case decodingFailed(Error)
  case networkError(Error)

  public var errorDescription: String? {
    switch self {
    case .invalidResponse:
      return "Invalid response from server"
    case .unauthorized:
      return "Device is no longer authorized"
    case .serverError(let code):
      return "Server error: \(code)"
    case .decodingFailed(let error):
      return "Failed to parse response: \(error.localizedDescription)"
    case .networkError(let error):
      return "Network error: \(error.localizedDescription)"
    }
  }
}

// MARK: - Request Models

struct InitPairingRequest: Encodable {
  let uuid: String
  let name: String
  let platform: String
  let deviceToken: String
}

struct CancelPairingRequest: Encodable {
  let sessionId: String
}

struct CheckRequest: Encodable {
  let userId: String
  let pairId: String
  let pairToken: String
  let childId: UInt64
  let activities: [ActivityEntry]
  let tz: String
}

struct ActivityEntry: Encodable {
  let id: Int
  let log: Bool
}

struct MoreTimeRequestBody: Encodable {
  let userId: String
  let pairId: String
  let pairToken: String
  let childId: UInt64
  let activityId: Int
  let requestedMinutes: Int
  let message: String?
}

// MARK: - Response Models

public struct InitPairingResponse: Decodable {
  public let sessionId: String
  public let qrCodeUrl: String?
  public let pinCode: String?
  public let expiresIn: Int?
}

public struct PairingStatusResponse: Decodable {
  public let status: String
  public let scanned: Bool?
  public let completed: Bool
  public let success: Bool
  public let error: String?
  public let credentials: PairingCredentialsResponse?
  public let children: [Allow2Child]?

  enum CodingKeys: String, CodingKey {
    case status
    case scanned
    case completed
    case success
    case error
    case credentials
    case children
  }

  public init(from decoder: Decoder) throws {
    let container = try decoder.container(keyedBy: CodingKeys.self)

    status = try container.decodeIfPresent(String.self, forKey: .status) ?? "pending"
    scanned = try container.decodeIfPresent(Bool.self, forKey: .scanned)

    // Handle completion states
    if let completedValue = try? container.decode(Bool.self, forKey: .completed) {
      completed = completedValue
    } else {
      completed = status == "completed"
    }

    if let successValue = try? container.decode(Bool.self, forKey: .success) {
      success = successValue
    } else {
      success = completed
    }

    error = try container.decodeIfPresent(String.self, forKey: .error)
    credentials = try container.decodeIfPresent(PairingCredentialsResponse.self, forKey: .credentials)
    children = try container.decodeIfPresent([Allow2Child].self, forKey: .children)
  }
}

public struct PairingCredentialsResponse: Decodable {
  public let userId: String
  public let pairId: String
}

struct EmptyResponse: Decodable {
  init() {}
}

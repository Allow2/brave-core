// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import XCTest
@testable import Brave

/// Mock URLSession for testing network requests
class MockURLProtocol: URLProtocol {
  static var requestHandler: ((URLRequest) throws -> (HTTPURLResponse, Data))?

  override class func canInit(with request: URLRequest) -> Bool {
    return true
  }

  override class func canonicalRequest(for request: URLRequest) -> URLRequest {
    return request
  }

  override func startLoading() {
    guard let handler = MockURLProtocol.requestHandler else {
      fatalError("Handler is unavailable.")
    }

    do {
      let (response, data) = try handler(request)
      client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
      client?.urlProtocol(self, didLoad: data)
      client?.urlProtocolDidFinishLoading(self)
    } catch {
      client?.urlProtocol(self, didFailWithError: error)
    }
  }

  override func stopLoading() {}
}

/// Tests for Allow2 API client functionality
class Allow2APIClientTests: XCTestCase {

  var manager: Allow2Manager!
  var mockSession: URLSession!

  override func setUp() {
    super.setUp()

    let config = URLSessionConfiguration.ephemeral
    config.protocolClasses = [MockURLProtocol.self]
    mockSession = URLSession(configuration: config)

    manager = Allow2Manager.shared
    Allow2Preferences.clearAll()
  }

  override func tearDown() {
    MockURLProtocol.requestHandler = nil
    Allow2Preferences.clearAll()
    super.tearDown()
  }

  // MARK: - Check API Tests

  func testCheck_Allowed() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "allowed": true,
        "minimumRemainingSeconds": 3600,
        "expires": 1234567890,
        "banned": false,
        "dayType": "normal"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let result = await manager.check(activities: [.internet], log: true)

    XCTAssertNotNil(result)
    XCTAssertTrue(result?.allowed ?? false)
    XCTAssertEqual(result?.minimumRemainingSeconds, 3600)
    XCTAssertFalse(result?.banned ?? true)
  }

  func testCheck_BlockedTimeLimit() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "allowed": false,
        "minimumRemainingSeconds": 0,
        "expires": 1234567890,
        "banned": false,
        "dayType": "normal",
        "blockReason": "timelimit"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let result = await manager.check(activities: [.internet], log: true)

    XCTAssertNotNil(result)
    XCTAssertFalse(result?.allowed ?? true)
    XCTAssertEqual(result?.minimumRemainingSeconds, 0)
    XCTAssertEqual(result?.blockReason, "timelimit")
  }

  func testCheck_BlockedBanned() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "allowed": false,
        "minimumRemainingSeconds": 0,
        "expires": 1234567890,
        "banned": true,
        "dayType": "normal",
        "blockReason": "banned"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let result = await manager.check(activities: [.internet], log: true)

    XCTAssertNotNil(result)
    XCTAssertFalse(result?.allowed ?? true)
    XCTAssertTrue(result?.banned ?? false)
    XCTAssertEqual(result?.blockReason, "banned")
  }

  func testCheck_BlockedTimeBlock() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "allowed": false,
        "minimumRemainingSeconds": 1800,
        "expires": 1234567890,
        "banned": false,
        "dayType": "normal",
        "blockReason": "timeblock"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let result = await manager.check(activities: [.internet], log: true)

    XCTAssertNotNil(result)
    XCTAssertFalse(result?.allowed ?? true)
    XCTAssertEqual(result?.blockReason, "timeblock")
  }

  func testCheck_401Unauthorized() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 401,
        httpVersion: nil,
        headerFields: nil
      )!

      return (response, Data())
    }

    let result = await manager.check(activities: [.internet], log: true)

    // Should return nil and unpair device
    XCTAssertNil(result)
    XCTAssertFalse(manager.isPaired)
    XCTAssertNil(Allow2Preferences.credentials)
  }

  func testCheck_NetworkError() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      throw URLError(.notConnectedToInternet)
    }

    let result = await manager.check(activities: [.internet], log: true)

    // Should return nil but stay paired
    XCTAssertNil(result)
    XCTAssertTrue(manager.isPaired)
  }

  func testCheck_NotPaired() async throws {
    // Don't set up credentials
    let result = await manager.check(activities: [.internet], log: true)

    XCTAssertNil(result)
  }

  func testCheck_NoChildSelected() async throws {
    setupPairedState()
    // Don't select a child

    let result = await manager.check(activities: [.internet], log: true)

    XCTAssertNil(result)
  }

  // MARK: - Request More Time Tests

  func testRequestMoreTime_Success() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "success": true,
        "requestId": "req_123"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    try await manager.requestMoreTime(
      activity: .internet,
      duration: 30,
      message: "Please give me more time"
    )

    // Should not throw
  }

  func testRequestMoreTime_Unauthorized() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 401,
        httpVersion: nil,
        headerFields: nil
      )!

      return (response, Data())
    }

    do {
      try await manager.requestMoreTime(
        activity: .internet,
        duration: 30,
        message: nil
      )
      XCTFail("Should have thrown error")
    } catch Allow2Error.notPaired {
      // Expected
      XCTAssertFalse(manager.isPaired)
    } catch {
      XCTFail("Wrong error type: \(error)")
    }
  }

  func testRequestMoreTime_NotPaired() async throws {
    // Don't set up credentials

    do {
      try await manager.requestMoreTime(
        activity: .internet,
        duration: 30,
        message: nil
      )
      XCTFail("Should have thrown error")
    } catch Allow2Error.notPaired {
      // Expected
    } catch {
      XCTFail("Wrong error type: \(error)")
    }
  }

  func testRequestMoreTime_NoChildSelected() async throws {
    setupPairedState()
    // Don't select child

    do {
      try await manager.requestMoreTime(
        activity: .internet,
        duration: 30,
        message: nil
      )
      XCTFail("Should have thrown error")
    } catch Allow2Error.notPaired {
      // Expected (no child = not paired effectively)
    } catch {
      XCTFail("Wrong error type: \(error)")
    }
  }

  // MARK: - Pairing Tests

  func testInitQRPairing_Success() async throws {
    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "sessionId": "session_123",
        "qrCodeUrl": "https://api.allow2.com/qr/abc123",
        "pinCode": "123456"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let session = try await manager.initQRPairing(deviceName: "Test Device")

    XCTAssertEqual(session.sessionId, "session_123")
    XCTAssertEqual(session.qrCodeUrl, "https://api.allow2.com/qr/abc123")
    XCTAssertEqual(session.pinCode, "123456")
  }

  func testInitPINPairing_Success() async throws {
    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "sessionId": "session_456",
        "pinCode": "654321"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let session = try await manager.initPINPairing(deviceName: "Test Device")

    XCTAssertEqual(session.sessionId, "session_456")
    XCTAssertEqual(session.pinCode, "654321")
  }

  func testCheckPairingStatus_Completed() async throws {
    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "completed": true,
        "success": true,
        "credentials": [
          "userId": "user123",
          "pairId": "pair456"
        ],
        "children": [
          [
            "id": 1001,
            "name": "Emma",
            "pinHash": "sha256:abc",
            "pinSalt": "salt1"
          ]
        ]
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let status = try await manager.checkPairingStatus(sessionId: "session_123")

    XCTAssertTrue(status.completed)
    XCTAssertTrue(status.success)
    XCTAssertNil(status.error)

    // Should store credentials
    XCTAssertTrue(manager.isPaired)
    XCTAssertEqual(manager.cachedChildren.count, 1)
  }

  func testCheckPairingStatus_Pending() async throws {
    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "completed": false,
        "success": false
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let status = try await manager.checkPairingStatus(sessionId: "session_123")

    XCTAssertFalse(status.completed)
    XCTAssertFalse(status.success)
  }

  func testCheckPairingStatus_Failed() async throws {
    MockURLProtocol.requestHandler = { request in
      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "completed": true,
        "success": false,
        "error": "Parent rejected pairing"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let status = try await manager.checkPairingStatus(sessionId: "session_123")

    XCTAssertTrue(status.completed)
    XCTAssertFalse(status.success)
    XCTAssertEqual(status.error, "Parent rejected pairing")
  }

  // MARK: - Multiple Activities Test

  func testCheck_MultipleActivities() async throws {
    setupPairedState()

    MockURLProtocol.requestHandler = { request in
      // Verify request body contains multiple activities
      if let body = request.httpBody,
         let json = try? JSONSerialization.jsonObject(with: body) as? [String: Any],
         let activities = json["activities"] as? [[String: Any]] {
        XCTAssertEqual(activities.count, 3)
      }

      let response = HTTPURLResponse(
        url: request.url!,
        statusCode: 200,
        httpVersion: nil,
        headerFields: nil
      )!

      let json: [String: Any] = [
        "allowed": true,
        "minimumRemainingSeconds": 1800,
        "dayType": "normal"
      ]

      let data = try JSONSerialization.data(withJSONObject: json)
      return (response, data)
    }

    let result = await manager.check(
      activities: [.internet, .gaming, .social],
      log: true
    )

    XCTAssertNotNil(result)
  }

  // MARK: - Helper Methods

  private func setupPairedState() {
    let credentials = Allow2Credentials(
      userId: "user123",
      pairId: "pair456",
      pairToken: "token789"
    )
    Allow2Preferences.credentials = credentials
    Allow2Preferences.isEnabled.value = true

    let child = Allow2Child(
      id: 1001,
      name: "Test Child",
      pinHash: "sha256:abc",
      pinSalt: "salt"
    )
    Allow2Preferences.cachedChildren = [child]
    Allow2Preferences.selectedChildId.value = 1001
  }
}

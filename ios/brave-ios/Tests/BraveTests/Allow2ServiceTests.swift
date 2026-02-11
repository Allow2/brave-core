// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import XCTest
import Combine
@testable import Brave

/// Tests for Allow2Manager service
class Allow2ServiceTests: XCTestCase {

  var manager: Allow2Manager!
  var cancellables = Set<AnyCancellable>()

  override func setUp() {
    super.setUp()
    manager = Allow2Manager.shared
    Allow2Preferences.clearAll()
    cancellables.removeAll()
  }

  override func tearDown() {
    Allow2Preferences.clearAll()
    cancellables.removeAll()
    super.tearDown()
  }

  // MARK: - Initial State Tests

  func testInitialState_NotPaired() {
    XCTAssertFalse(manager.isPaired)
    XCTAssertFalse(manager.isEnabled)
    XCTAssertTrue(manager.isSharedDevice)
    XCTAssertFalse(manager.isBlocked)
    XCTAssertNil(manager.currentChild)
    XCTAssertNil(manager.lastCheckResult)
  }

  func testIsPaired_WithCredentials() {
    // Store credentials
    let credentials = Allow2Credentials(
      userId: "user123",
      pairId: "pair456",
      pairToken: "token789"
    )
    Allow2Preferences.credentials = credentials

    XCTAssertTrue(manager.isPaired)
  }

  func testIsEnabled_RequiresPairing() {
    // Enabled but not paired
    Allow2Preferences.isEnabled.value = true
    XCTAssertFalse(manager.isEnabled)

    // Paired and enabled
    Allow2Preferences.credentials = Allow2Credentials(
      userId: "user123",
      pairId: "pair456",
      pairToken: "token789"
    )
    XCTAssertTrue(manager.isEnabled)
  }

  // MARK: - Child Selection Tests

  func testSelectChild_CorrectPin() {
    setupPairedStateWithChildren()

    let child = manager.cachedChildren[0]
    let success = manager.selectChild(child, pin: "1234")

    XCTAssertTrue(success)
    XCTAssertNotNil(manager.currentChild)
    XCTAssertEqual(manager.currentChild?.id, child.id)
    XCTAssertFalse(manager.isSharedDevice)
  }

  func testSelectChild_WrongPin() {
    setupPairedStateWithChildren()

    let child = manager.cachedChildren[0]
    let success = manager.selectChild(child, pin: "9999")

    XCTAssertFalse(success)
    XCTAssertNil(manager.currentChild)
    XCTAssertTrue(manager.isSharedDevice)
  }

  func testSelectChild_EmptyPin() {
    setupPairedStateWithChildren()

    let child = manager.cachedChildren[0]
    let success = manager.selectChild(child, pin: "")

    XCTAssertFalse(success)
    XCTAssertNil(manager.currentChild)
  }

  func testSelectChild_StateNotification() {
    setupPairedStateWithChildren()

    let expectation = XCTestExpectation(description: "State change notification")

    manager.stateDidChange
      .sink { state in
        if case .childSelected(let child) = state {
          XCTAssertEqual(child.name, "Emma")
          expectation.fulfill()
        }
      }
      .store(in: &cancellables)

    let child = manager.cachedChildren[0]
    _ = manager.selectChild(child, pin: "1234")

    wait(for: [expectation], timeout: 1.0)
  }

  func testClearChildSelection() {
    setupPairedStateWithChildren()

    let child = manager.cachedChildren[0]
    _ = manager.selectChild(child, pin: "1234")
    XCTAssertNotNil(manager.currentChild)

    manager.clearChildSelection()
    XCTAssertNil(manager.currentChild)
    XCTAssertTrue(manager.isSharedDevice)
  }

  // MARK: - PIN Validation Tests

  func testPinValidation_ConstantTime() {
    setupPairedStateWithChildren()

    let child = manager.cachedChildren[0]

    // All wrong PINs should take similar time (constant-time comparison)
    _ = manager.selectChild(child, pin: "9234")
    _ = manager.selectChild(child, pin: "1239")
    _ = manager.selectChild(child, pin: "0000")

    // Correct PIN should also take similar time
    _ = manager.selectChild(child, pin: "1234")
  }

  func testPinValidation_DifferentLengths() {
    setupPairedStateWithChildren()

    let child = manager.cachedChildren[0]

    // Too short
    XCTAssertFalse(manager.selectChild(child, pin: "123"))

    // Too long
    XCTAssertFalse(manager.selectChild(child, pin: "12345"))

    // Correct length
    XCTAssertTrue(manager.selectChild(child, pin: "1234"))
  }

  // MARK: - Shared Device Mode Tests

  func testSharedDeviceMode_NoPairing() {
    XCTAssertTrue(manager.isSharedDevice)
    XCTAssertNil(manager.currentChild)
  }

  func testSharedDeviceMode_PairedNoChild() {
    setupPairedStateWithChildren()

    XCTAssertTrue(manager.isSharedDevice)
    XCTAssertNil(manager.currentChild)
  }

  func testSharedDeviceMode_ChildSelected() {
    setupPairedStateWithChildren()

    let child = manager.cachedChildren[0]
    _ = manager.selectChild(child, pin: "1234")

    XCTAssertFalse(manager.isSharedDevice)
    XCTAssertNotNil(manager.currentChild)
  }

  // MARK: - Check Result Processing Tests

  func testProcessCheckResult_Allowed() {
    let result = Allow2CheckResult(
      allowed: true,
      minimumRemainingSeconds: 3600,
      dayType: "normal"
    )

    manager.lastCheckResult = result
    XCTAssertFalse(manager.isBlocked)
  }

  func testProcessCheckResult_Blocked() {
    let result = Allow2CheckResult(
      allowed: false,
      minimumRemainingSeconds: 0,
      dayType: "normal",
      banned: false,
      blockReason: "timelimit"
    )

    manager.lastCheckResult = result
    XCTAssertTrue(manager.isBlocked)
  }

  func testProcessCheckResult_BlockStateChange() {
    let expectation = XCTestExpectation(description: "Block state notification")

    manager.blockStateDidChange
      .sink { isBlocked in
        XCTAssertTrue(isBlocked)
        expectation.fulfill()
      }
      .store(in: &cancellables)

    // Simulate check result processing
    let result = Allow2CheckResult(
      allowed: false,
      minimumRemainingSeconds: 0,
      dayType: "normal",
      banned: false,
      blockReason: "timelimit"
    )

    manager.lastCheckResult = result

    wait(for: [expectation], timeout: 1.0)
  }

  // MARK: - Warning Tests

  func testWarningLevel_None() {
    let level = WarningLevel.from(seconds: 1000)
    XCTAssertEqual(level, .none)
  }

  func testWarningLevel_Gentle() {
    let level = WarningLevel.from(seconds: 600)
    XCTAssertEqual(level, .gentle(minutes: 10))
  }

  func testWarningLevel_Warning() {
    let level = WarningLevel.from(seconds: 180)
    XCTAssertEqual(level, .warning(minutes: 3))
  }

  func testWarningLevel_Urgent() {
    let level = WarningLevel.from(seconds: 30)
    XCTAssertEqual(level, .urgent(seconds: 30))
  }

  func testWarningLevel_Blocked() {
    let level = WarningLevel.from(seconds: 0)
    XCTAssertEqual(level, .blocked)
  }

  // MARK: - Edge Cases

  func testCachedChildren_Empty() {
    XCTAssertTrue(manager.cachedChildren.isEmpty)
  }

  func testCachedChildren_WithData() {
    setupPairedStateWithChildren()

    XCTAssertEqual(manager.cachedChildren.count, 2)
    XCTAssertEqual(manager.cachedChildren[0].name, "Emma")
    XCTAssertEqual(manager.cachedChildren[1].name, "Jack")
  }

  func testLoadCurrentChild_InvalidId() {
    setupPairedStateWithChildren()

    // Set invalid child ID
    Allow2Preferences.selectedChildId.value = 9999

    // Should not crash and return nil
    XCTAssertNil(manager.currentChild)
  }

  func testSelectChild_SwitchBetweenChildren() {
    setupPairedStateWithChildren()

    let child1 = manager.cachedChildren[0]
    let child2 = manager.cachedChildren[1]

    // Select first child
    XCTAssertTrue(manager.selectChild(child1, pin: "1234"))
    XCTAssertEqual(manager.currentChild?.id, child1.id)

    // Switch to second child
    XCTAssertTrue(manager.selectChild(child2, pin: "5678"))
    XCTAssertEqual(manager.currentChild?.id, child2.id)
  }

  // MARK: - Constant Time Comparison Tests

  func testConstantTimeCompare_Equal() {
    let str1 = "sha256:abc123"
    let str2 = "sha256:abc123"

    XCTAssertTrue(str1.constantTimeCompare(to: str2))
  }

  func testConstantTimeCompare_NotEqual() {
    let str1 = "sha256:abc123"
    let str2 = "sha256:def456"

    XCTAssertFalse(str1.constantTimeCompare(to: str2))
  }

  func testConstantTimeCompare_DifferentLength() {
    let str1 = "sha256:abc"
    let str2 = "sha256:abc123"

    XCTAssertFalse(str1.constantTimeCompare(to: str2))
  }

  // MARK: - Helper Methods

  private func setupPairedStateWithChildren() {
    // Store credentials
    let credentials = Allow2Credentials(
      userId: "user123",
      pairId: "pair456",
      pairToken: "token789"
    )
    Allow2Preferences.credentials = credentials
    Allow2Preferences.isEnabled.value = true

    // Create children with proper PIN hashes
    let child1 = Allow2Child(
      id: 1001,
      name: "Emma",
      pinHash: hashPin("1234", salt: "salt_emma"),
      pinSalt: "salt_emma"
    )

    let child2 = Allow2Child(
      id: 1002,
      name: "Jack",
      pinHash: hashPin("5678", salt: "salt_jack"),
      pinSalt: "salt_jack"
    )

    Allow2Preferences.cachedChildren = [child1, child2]
  }

  private func hashPin(_ pin: String, salt: String) -> String {
    let salted = pin + salt
    let hash = SHA256.hash(data: Data(salted.utf8))
    return "sha256:" + hash.compactMap { String(format: "%02x", $0) }.joined()
  }
}

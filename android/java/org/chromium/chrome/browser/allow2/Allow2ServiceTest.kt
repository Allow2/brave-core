// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.chrome.browser.allow2

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.google.common.truth.Truth.assertThat
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.MockitoAnnotations
import org.mockito.kotlin.*
import java.security.MessageDigest

/**
 * Unit tests for Allow2Service
 */
@ExperimentalCoroutinesApi
@RunWith(AndroidJUnit4::class)
class Allow2ServiceTest {

    @Mock
    private lateinit var mockApiClient: Allow2ApiClient

    @Mock
    private lateinit var mockCredentialManager: Allow2CredentialManager

    private lateinit var service: Allow2Service

    @Before
    fun setUp() {
        MockitoAnnotations.openMocks(this)
        service = Allow2Service(mockApiClient, mockCredentialManager)
    }

    @After
    fun tearDown() {
        service.clearAll()
    }

    // MARK: - Initial State Tests

    @Test
    fun testInitialState_NotPaired() {
        whenever(mockCredentialManager.hasCredentials()).thenReturn(false)

        assertThat(service.isPaired).isFalse()
        assertThat(service.isEnabled).isFalse()
        assertThat(service.isSharedDevice).isTrue()
        assertThat(service.isBlocked).isFalse()
        assertThat(service.currentChild).isNull()
    }

    @Test
    fun testIsPaired_WithCredentials() {
        whenever(mockCredentialManager.hasCredentials()).thenReturn(true)

        assertThat(service.isPaired).isTrue()
    }

    @Test
    fun testIsEnabled_RequiresPairing() {
        // Enabled but not paired
        whenever(mockCredentialManager.hasCredentials()).thenReturn(false)
        service.setEnabled(true)

        assertThat(service.isEnabled).isFalse()

        // Paired and enabled
        whenever(mockCredentialManager.hasCredentials()).thenReturn(true)
        assertThat(service.isEnabled).isTrue()
    }

    // MARK: - Child Selection Tests

    @Test
    fun testSelectChild_CorrectPin() {
        setupPairedStateWithChildren()

        val child = service.cachedChildren[0]
        val success = service.selectChild(child, "1234")

        assertThat(success).isTrue()
        assertThat(service.currentChild).isNotNull()
        assertThat(service.currentChild?.id).isEqualTo(child.id)
        assertThat(service.isSharedDevice).isFalse()
    }

    @Test
    fun testSelectChild_WrongPin() {
        setupPairedStateWithChildren()

        val child = service.cachedChildren[0]
        val success = service.selectChild(child, "9999")

        assertThat(success).isFalse()
        assertThat(service.currentChild).isNull()
        assertThat(service.isSharedDevice).isTrue()
    }

    @Test
    fun testSelectChild_EmptyPin() {
        setupPairedStateWithChildren()

        val child = service.cachedChildren[0]
        val success = service.selectChild(child, "")

        assertThat(success).isFalse()
        assertThat(service.currentChild).isNull()
    }

    @Test
    fun testSelectChild_InvalidChildId() {
        setupPairedStateWithChildren()

        val invalidChild = Allow2Child(
            id = 9999,
            name = "Invalid",
            pinHash = "sha256:abc",
            pinSalt = "salt"
        )

        val success = service.selectChild(invalidChild, "1234")

        assertThat(success).isFalse()
        assertThat(service.currentChild).isNull()
    }

    @Test
    fun testSelectChild_SwitchBetweenChildren() {
        setupPairedStateWithChildren()

        val child1 = service.cachedChildren[0]
        val child2 = service.cachedChildren[1]

        // Select first child
        assertThat(service.selectChild(child1, "1234")).isTrue()
        assertThat(service.currentChild?.id).isEqualTo(child1.id)

        // Switch to second child
        assertThat(service.selectChild(child2, "5678")).isTrue()
        assertThat(service.currentChild?.id).isEqualTo(child2.id)
    }

    @Test
    fun testClearChildSelection() {
        setupPairedStateWithChildren()

        val child = service.cachedChildren[0]
        service.selectChild(child, "1234")
        assertThat(service.currentChild).isNotNull()

        service.clearCurrentChild()
        assertThat(service.currentChild).isNull()
        assertThat(service.isSharedDevice).isTrue()
    }

    // MARK: - PIN Validation Tests

    @Test
    fun testPinValidation_DifferentLengths() {
        setupPairedStateWithChildren()

        val child = service.cachedChildren[0]

        // Too short
        assertThat(service.selectChild(child, "123")).isFalse()

        // Too long
        assertThat(service.selectChild(child, "12345")).isFalse()

        // Correct length
        assertThat(service.selectChild(child, "1234")).isTrue()
    }

    @Test
    fun testConstantTimeCompare_Equal() {
        val str1 = "sha256:abc123"
        val str2 = "sha256:abc123"

        assertThat(constantTimeCompare(str1, str2)).isTrue()
    }

    @Test
    fun testConstantTimeCompare_NotEqual() {
        val str1 = "sha256:abc123"
        val str2 = "sha256:def456"

        assertThat(constantTimeCompare(str1, str2)).isFalse()
    }

    @Test
    fun testConstantTimeCompare_DifferentLength() {
        val str1 = "sha256:abc"
        val str2 = "sha256:abc123"

        assertThat(constantTimeCompare(str1, str2)).isFalse()
    }

    // MARK: - Check API Tests

    @Test
    fun testCheck_Allowed() = runTest {
        setupPairedStateWithChild()

        val checkResult = Allow2CheckResult(
            allowed = true,
            minimumRemainingSeconds = 3600,
            dayType = "normal"
        )

        whenever(mockApiClient.check(any(), any(), any(), any()))
            .thenReturn(checkResult)

        val result = service.check(listOf(Allow2Activity.INTERNET), log = true)

        assertThat(result).isNotNull()
        assertThat(result?.allowed).isTrue()
        assertThat(result?.minimumRemainingSeconds).isEqualTo(3600)
    }

    @Test
    fun testCheck_BlockedTimeLimit() = runTest {
        setupPairedStateWithChild()

        val checkResult = Allow2CheckResult(
            allowed = false,
            minimumRemainingSeconds = 0,
            dayType = "normal",
            banned = false,
            blockReason = "timelimit"
        )

        whenever(mockApiClient.check(any(), any(), any(), any()))
            .thenReturn(checkResult)

        val result = service.check(listOf(Allow2Activity.INTERNET), log = true)

        assertThat(result).isNotNull()
        assertThat(result?.allowed).isFalse()
        assertThat(result?.blockReason).isEqualTo("timelimit")
        assertThat(service.isBlocked).isTrue()
    }

    @Test
    fun testCheck_BlockedBanned() = runTest {
        setupPairedStateWithChild()

        val checkResult = Allow2CheckResult(
            allowed = false,
            minimumRemainingSeconds = 0,
            dayType = "normal",
            banned = true,
            blockReason = "banned"
        )

        whenever(mockApiClient.check(any(), any(), any(), any()))
            .thenReturn(checkResult)

        val result = service.check(listOf(Allow2Activity.INTERNET), log = true)

        assertThat(result).isNotNull()
        assertThat(result?.allowed).isFalse()
        assertThat(result?.banned).isTrue()
        assertThat(result?.blockReason).isEqualTo("banned")
    }

    @Test
    fun testCheck_BlockedTimeBlock() = runTest {
        setupPairedStateWithChild()

        val checkResult = Allow2CheckResult(
            allowed = false,
            minimumRemainingSeconds = 1800,
            dayType = "normal",
            banned = false,
            blockReason = "timeblock"
        )

        whenever(mockApiClient.check(any(), any(), any(), any()))
            .thenReturn(checkResult)

        val result = service.check(listOf(Allow2Activity.INTERNET), log = true)

        assertThat(result).isNotNull()
        assertThat(result?.allowed).isFalse()
        assertThat(result?.blockReason).isEqualTo("timeblock")
    }

    @Test
    fun testCheck_401Unauthorized() = runTest {
        setupPairedStateWithChild()

        whenever(mockApiClient.check(any(), any(), any(), any()))
            .thenThrow(Allow2Exception.Unauthorized())

        val result = service.check(listOf(Allow2Activity.INTERNET), log = true)

        assertThat(result).isNull()
        verify(mockCredentialManager).clearCredentials()
    }

    @Test
    fun testCheck_NetworkError() = runTest {
        setupPairedStateWithChild()

        whenever(mockApiClient.check(any(), any(), any(), any()))
            .thenThrow(Allow2Exception.NetworkError("Connection failed"))

        val result = service.check(listOf(Allow2Activity.INTERNET), log = true)

        assertThat(result).isNull()
        // Should NOT clear credentials on network error
        verify(mockCredentialManager, never()).clearCredentials()
    }

    @Test
    fun testCheck_NotPaired() = runTest {
        whenever(mockCredentialManager.getCredentials()).thenReturn(null)

        val result = service.check(listOf(Allow2Activity.INTERNET), log = true)

        assertThat(result).isNull()
    }

    @Test
    fun testCheck_NoChildSelected() = runTest {
        setupPairedState()
        // Don't select child

        val result = service.check(listOf(Allow2Activity.INTERNET), log = true)

        assertThat(result).isNull()
    }

    // MARK: - Request More Time Tests

    @Test
    fun testRequestMoreTime_Success() = runTest {
        setupPairedStateWithChild()

        whenever(mockApiClient.requestMoreTime(any(), any(), any(), any(), any()))
            .thenReturn(Unit)

        service.requestMoreTime(
            activity = Allow2Activity.INTERNET,
            duration = 30,
            message = "Please give me more time"
        )

        verify(mockApiClient).requestMoreTime(any(), any(), any(), eq(30), any())
    }

    @Test
    fun testRequestMoreTime_Unauthorized() = runTest {
        setupPairedStateWithChild()

        whenever(mockApiClient.requestMoreTime(any(), any(), any(), any(), any()))
            .thenThrow(Allow2Exception.Unauthorized())

        try {
            service.requestMoreTime(
                activity = Allow2Activity.INTERNET,
                duration = 30,
                message = null
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.NotPaired) {
            // Expected
            verify(mockCredentialManager).clearCredentials()
        }
    }

    @Test
    fun testRequestMoreTime_NotPaired() = runTest {
        whenever(mockCredentialManager.getCredentials()).thenReturn(null)

        try {
            service.requestMoreTime(
                activity = Allow2Activity.INTERNET,
                duration = 30,
                message = null
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.NotPaired) {
            // Expected
        }
    }

    // MARK: - Pairing Tests

    @Test
    fun testInitQRPairing_Success() = runTest {
        val mockSession = PairingSession(
            sessionId = "session_123",
            qrCodeUrl = "https://api.allow2.com/qr/abc",
            pinCode = "123456"
        )

        whenever(mockApiClient.initQRPairing(any()))
            .thenReturn(mockSession)

        val session = service.initQRPairing("Test Device")

        assertThat(session.sessionId).isEqualTo("session_123")
        assertThat(session.qrCodeUrl).isNotEmpty()
        assertThat(session.pinCode).isEqualTo("123456")
    }

    @Test
    fun testInitPINPairing_Success() = runTest {
        val mockSession = PairingSession(
            sessionId = "session_456",
            qrCodeUrl = "",
            pinCode = "654321"
        )

        whenever(mockApiClient.initPINPairing(any()))
            .thenReturn(mockSession)

        val session = service.initPINPairing("Test Device")

        assertThat(session.sessionId).isEqualTo("session_456")
        assertThat(session.pinCode).isEqualTo("654321")
    }

    @Test
    fun testCheckPairingStatus_Completed() = runTest {
        val mockStatus = PairingStatus(
            completed = true,
            success = true,
            error = null,
            credentials = Allow2Credentials(
                userId = "user123",
                pairId = "pair456",
                pairToken = "token789"
            ),
            children = listOf(
                Allow2Child(1001, "Emma", "sha256:abc", "salt")
            )
        )

        whenever(mockApiClient.checkPairingStatus(any()))
            .thenReturn(mockStatus)

        val status = service.checkPairingStatus("session_123")

        assertThat(status.completed).isTrue()
        assertThat(status.success).isTrue()

        verify(mockCredentialManager).storeCredentials(any())
    }

    @Test
    fun testCheckPairingStatus_Pending() = runTest {
        val mockStatus = PairingStatus(
            completed = false,
            success = false,
            error = null
        )

        whenever(mockApiClient.checkPairingStatus(any()))
            .thenReturn(mockStatus)

        val status = service.checkPairingStatus("session_123")

        assertThat(status.completed).isFalse()
        assertThat(status.success).isFalse()
    }

    // MARK: - Warning Level Tests

    @Test
    fun testWarningLevel_None() {
        val level = WarningLevel.from(1000)
        assertThat(level).isEqualTo(WarningLevel.NONE)
    }

    @Test
    fun testWarningLevel_Gentle() {
        val level = WarningLevel.from(600)
        assertThat(level).isEqualTo(WarningLevel.GENTLE)
    }

    @Test
    fun testWarningLevel_Warning() {
        val level = WarningLevel.from(180)
        assertThat(level).isEqualTo(WarningLevel.WARNING)
    }

    @Test
    fun testWarningLevel_Urgent() {
        val level = WarningLevel.from(30)
        assertThat(level).isEqualTo(WarningLevel.URGENT)
    }

    @Test
    fun testWarningLevel_Blocked() {
        val level = WarningLevel.from(0)
        assertThat(level).isEqualTo(WarningLevel.BLOCKED)
    }

    // MARK: - Helper Methods

    private fun setupPairedState() {
        val credentials = Allow2Credentials(
            userId = "user123",
            pairId = "pair456",
            pairToken = "token789"
        )

        whenever(mockCredentialManager.hasCredentials()).thenReturn(true)
        whenever(mockCredentialManager.getCredentials()).thenReturn(credentials)

        service.setEnabled(true)
    }

    private fun setupPairedStateWithChildren() {
        setupPairedState()

        val child1 = Allow2Child(
            id = 1001,
            name = "Emma",
            pinHash = hashPin("1234", "salt_emma"),
            pinSalt = "salt_emma"
        )

        val child2 = Allow2Child(
            id = 1002,
            name = "Jack",
            pinHash = hashPin("5678", "salt_jack"),
            pinSalt = "salt_jack"
        )

        service.cachedChildren = listOf(child1, child2)
    }

    private fun setupPairedStateWithChild() {
        setupPairedStateWithChildren()
        service.selectChild(service.cachedChildren[0], "1234")
    }

    private fun hashPin(pin: String, salt: String): String {
        val salted = pin + salt
        val bytes = MessageDigest.getInstance("SHA-256").digest(salted.toByteArray())
        return "sha256:" + bytes.joinToString("") { "%02x".format(it) }
    }

    private fun constantTimeCompare(a: String, b: String): Boolean {
        if (a.length != b.length) return false
        var result = 0
        for (i in a.indices) {
            result = result or (a[i].code xor b[i].code)
        }
        return result == 0
    }
}

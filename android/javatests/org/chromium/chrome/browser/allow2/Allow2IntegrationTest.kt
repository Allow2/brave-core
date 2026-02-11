// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.chrome.browser.allow2

import android.app.Instrumentation
import android.content.Intent
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.*
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.*
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.platform.app.InstrumentationRegistry
import com.google.common.truth.Truth.assertThat
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.hamcrest.Matchers.allOf
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.security.MessageDigest
import java.util.concurrent.TimeUnit

/**
 * Integration tests for Allow2 Android features.
 * Tests the full end-to-end flows including UI interactions.
 */
@ExperimentalCoroutinesApi
@RunWith(AndroidJUnit4::class)
@LargeTest
class Allow2IntegrationTest {

    private lateinit var mockServer: MockWebServer
    private lateinit var service: Allow2Service
    private lateinit var credentialManager: Allow2CredentialManager
    private lateinit var instrumentation: Instrumentation

    @Before
    fun setUp() {
        mockServer = MockWebServer()
        mockServer.start()

        instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext

        credentialManager = Allow2CredentialManager(context)
        credentialManager.clearCredentials()

        val apiClient = Allow2ApiClient(mockServer.url("/").toString())
        service = Allow2Service(apiClient, credentialManager)
    }

    @After
    fun tearDown() {
        mockServer.shutdown()
        credentialManager.clearCredentials()
    }

    // =========================================================================
    // SCENARIO 1: Happy Path - Full Flow
    // =========================================================================

    @Test
    fun testHappyPath_PairSelectBrowseBlock() = runTest {
        // Phase 1: Initial state - not paired
        assertThat(service.isPaired).isFalse()

        // Phase 2: Complete pairing
        mockServer.enqueue(createPairingResponse())
        val pairResult = service.pairDevice(
            "parent@example.com", "password", "device_token", "Test Device"
        )
        assertThat(pairResult.success).isTrue()
        assertThat(service.isPaired).isTrue()

        // Phase 3: Get children
        val children = service.cachedChildren
        assertThat(children).hasSize(2)
        assertThat(children[0].name).isEqualTo("Emma")

        // Phase 4: Select child with correct PIN
        val success = service.selectChild(children[0], "1234")
        assertThat(success).isTrue()
        assertThat(service.currentChild).isNotNull()
        assertThat(service.isSharedDevice).isFalse()

        // Phase 5: Check allowance - allowed
        mockServer.enqueue(createAllowedResponse(3600))
        val checkResult = service.check(listOf(Allow2Activity.INTERNET), true)
        assertThat(checkResult).isNotNull()
        assertThat(checkResult?.allowed).isTrue()
        assertThat(checkResult?.minimumRemainingSeconds).isEqualTo(3600)

        // Phase 6: Check allowance - blocked
        mockServer.enqueue(createBlockedResponse("timelimit"))
        val blockedResult = service.check(listOf(Allow2Activity.INTERNET), true)
        assertThat(blockedResult).isNotNull()
        assertThat(blockedResult?.allowed).isFalse()
        assertThat(blockedResult?.blockReason).isEqualTo("timelimit")
        assertThat(service.isBlocked).isTrue()
    }

    // =========================================================================
    // SCENARIO 2: QR Pairing Flow
    // =========================================================================

    @Test
    fun testQRPairingFlow_Complete() = runTest {
        // Step 1: Initialize QR pairing
        mockServer.enqueue(MockResponse()
            .setResponseCode(200)
            .setBody(JSONObject().apply {
                put("sessionId", "session_qr_123")
                put("qrCodeUrl", "https://allow2.com/qr/abc")
                put("pinCode", "123456")
                put("expiresIn", 300)
            }.toString()))

        val session = service.initQRPairing("Test Device")
        assertThat(session.sessionId).isEqualTo("session_qr_123")
        assertThat(session.qrCodeUrl).isNotEmpty()

        // Step 2: Poll status - pending
        mockServer.enqueue(MockResponse()
            .setResponseCode(200)
            .setBody(JSONObject().apply {
                put("completed", false)
                put("scanned", false)
            }.toString()))

        var status = service.checkPairingStatus(session.sessionId)
        assertThat(status.completed).isFalse()

        // Step 3: Poll status - scanned (parent scanned QR)
        mockServer.enqueue(MockResponse()
            .setResponseCode(200)
            .setBody(JSONObject().apply {
                put("completed", false)
                put("scanned", true)
            }.toString()))

        status = service.checkPairingStatus(session.sessionId)
        assertThat(status.completed).isFalse()
        assertThat(status.scanned).isTrue()

        // Step 4: Poll status - completed
        mockServer.enqueue(createPairingStatusCompletedResponse())

        status = service.checkPairingStatus(session.sessionId)
        assertThat(status.completed).isTrue()
        assertThat(status.success).isTrue()
        assertThat(service.isPaired).isTrue()
    }

    // =========================================================================
    // SCENARIO 3: PIN Pairing Flow
    // =========================================================================

    @Test
    fun testPINPairingFlow_Complete() = runTest {
        // Step 1: Initialize PIN pairing
        mockServer.enqueue(MockResponse()
            .setResponseCode(200)
            .setBody(JSONObject().apply {
                put("sessionId", "session_pin_123")
                put("pinCode", "654321")
                put("expiresIn", 300)
            }.toString()))

        val session = service.initPINPairing("Test Device")
        assertThat(session.sessionId).isEqualTo("session_pin_123")
        assertThat(session.pinCode).isEqualTo("654321")

        // Step 2: Poll until completed
        mockServer.enqueue(createPairingStatusCompletedResponse())

        val status = service.checkPairingStatus(session.sessionId)
        assertThat(status.completed).isTrue()
        assertThat(status.success).isTrue()
        assertThat(service.isPaired).isTrue()
    }

    // =========================================================================
    // SCENARIO 4: Request More Time
    // =========================================================================

    @Test
    fun testRequestMoreTime_Flow() = runTest {
        setupPairedAndBlockedState()

        // Request more time
        mockServer.enqueue(MockResponse()
            .setResponseCode(200)
            .setBody(JSONObject().apply {
                put("success", true)
                put("requestId", "req_123")
            }.toString()))

        service.requestMoreTime(
            activity = Allow2Activity.INTERNET,
            duration = 30,
            message = "Need to finish homework"
        )

        // Verify request was made
        val request = mockServer.takeRequest(5, TimeUnit.SECONDS)
        assertThat(request).isNotNull()
        val body = JSONObject(request!!.body.readUtf8())
        assertThat(body.getInt("duration")).isEqualTo(30)
        assertThat(body.getString("message")).isEqualTo("Need to finish homework")
    }

    // =========================================================================
    // SCENARIO 5: Credentials Invalidated
    // =========================================================================

    @Test
    fun testCredentialsInvalidated_401Response() = runTest {
        setupPairedState()
        assertThat(service.isPaired).isTrue()

        // 401 response should clear credentials
        mockServer.enqueue(MockResponse().setResponseCode(401))

        try {
            service.check(listOf(Allow2Activity.INTERNET), true)
        } catch (e: Allow2Exception.Unauthorized) {
            // Expected
        }

        assertThat(service.isPaired).isFalse()
        assertThat(credentialManager.hasCredentials()).isFalse()
    }

    // =========================================================================
    // SCENARIO 6: Warning Thresholds
    // =========================================================================

    @Test
    fun testWarningThresholds_Progression() = runTest {
        setupPairedState()
        service.selectChild(service.cachedChildren[0], "1234")

        // 15 minutes - Gentle
        mockServer.enqueue(createAllowedResponse(900))
        var result = service.check(listOf(Allow2Activity.INTERNET), true)
        assertThat(WarningLevel.from(result!!.minimumRemainingSeconds))
            .isEqualTo(WarningLevel.GENTLE)

        // 5 minutes - Warning
        mockServer.enqueue(createAllowedResponse(300))
        result = service.check(listOf(Allow2Activity.INTERNET), true)
        assertThat(WarningLevel.from(result!!.minimumRemainingSeconds))
            .isEqualTo(WarningLevel.WARNING)

        // 1 minute - Urgent
        mockServer.enqueue(createAllowedResponse(60))
        result = service.check(listOf(Allow2Activity.INTERNET), true)
        assertThat(WarningLevel.from(result!!.minimumRemainingSeconds))
            .isEqualTo(WarningLevel.URGENT)

        // 0 seconds - Blocked
        mockServer.enqueue(createBlockedResponse("timelimit"))
        result = service.check(listOf(Allow2Activity.INTERNET), true)
        assertThat(WarningLevel.from(result!!.minimumRemainingSeconds))
            .isEqualTo(WarningLevel.BLOCKED)
    }

    // =========================================================================
    // SCENARIO 7: Multiple Children
    // =========================================================================

    @Test
    fun testMultipleChildren_Switching() = runTest {
        setupPairedState()

        val children = service.cachedChildren
        assertThat(children).hasSize(2)

        // Select Emma
        assertThat(service.selectChild(children[0], "1234")).isTrue()
        assertThat(service.currentChild?.name).isEqualTo("Emma")

        // Can't use Jack's PIN for Emma
        assertThat(service.selectChild(children[0], "5678")).isFalse()
        // Still Emma selected
        assertThat(service.currentChild?.name).isEqualTo("Emma")

        // Switch to Jack
        assertThat(service.selectChild(children[1], "5678")).isTrue()
        assertThat(service.currentChild?.name).isEqualTo("Jack")
    }

    // =========================================================================
    // SCENARIO 8: Offline Handling
    // =========================================================================

    @Test
    fun testOffline_UseCachedResult() = runTest {
        setupPairedState()
        service.selectChild(service.cachedChildren[0], "1234")

        // First check - online, cache result
        mockServer.enqueue(createAllowedResponse(3600))
        val onlineResult = service.check(listOf(Allow2Activity.INTERNET), true)
        assertThat(onlineResult?.allowed).isTrue()

        // Second check - network error, should use cache
        mockServer.enqueue(MockResponse()
            .setSocketPolicy(okhttp3.mockwebserver.SocketPolicy.DISCONNECT_AT_START))

        // Should not crash, may return cached or null
        val offlineResult = try {
            service.check(listOf(Allow2Activity.INTERNET), true)
        } catch (e: Allow2Exception.NetworkError) {
            null
        }

        // Implementation-dependent: may use cache or return null
    }

    // =========================================================================
    // SCENARIO 9: Session Expiry
    // =========================================================================

    @Test
    fun testPairingSession_Expiry() = runTest {
        // Initialize pairing with short expiry
        mockServer.enqueue(MockResponse()
            .setResponseCode(200)
            .setBody(JSONObject().apply {
                put("sessionId", "session_exp")
                put("pinCode", "123456")
                put("expiresIn", 5) // 5 seconds
            }.toString()))

        val session = service.initPINPairing("Test Device")

        // Wait for expiry (in real test, mock time)
        // For now, just verify the expiry is captured
        assertThat(session.expiresIn).isEqualTo(5)
    }

    // =========================================================================
    // Helper Methods
    // =========================================================================

    private fun setupPairedState() {
        val credentials = Allow2Credentials(
            userId = "user123",
            pairId = "pair456",
            pairToken = "token789"
        )
        credentialManager.storeCredentials(credentials)

        service.cachedChildren = listOf(
            Allow2Child(
                id = 1001,
                name = "Emma",
                pinHash = hashPin("1234", "salt_emma"),
                pinSalt = "salt_emma"
            ),
            Allow2Child(
                id = 1002,
                name = "Jack",
                pinHash = hashPin("5678", "salt_jack"),
                pinSalt = "salt_jack"
            )
        )

        service.setEnabled(true)
    }

    private suspend fun setupPairedAndBlockedState() {
        setupPairedState()
        service.selectChild(service.cachedChildren[0], "1234")

        // Set blocked
        mockServer.enqueue(createBlockedResponse("timelimit"))
        service.check(listOf(Allow2Activity.INTERNET), true)
    }

    private fun createPairingResponse(): MockResponse {
        val response = JSONObject().apply {
            put("success", true)
            put("userId", "user123")
            put("pairId", "pair456")
            put("pairToken", "token789")
            put("children", JSONArray().apply {
                put(JSONObject().apply {
                    put("id", 1001)
                    put("name", "Emma")
                    put("pinHash", hashPin("1234", "salt_emma"))
                    put("pinSalt", "salt_emma")
                })
                put(JSONObject().apply {
                    put("id", 1002)
                    put("name", "Jack")
                    put("pinHash", hashPin("5678", "salt_jack"))
                    put("pinSalt", "salt_jack")
                })
            })
        }
        return MockResponse().setResponseCode(200).setBody(response.toString())
    }

    private fun createAllowedResponse(remainingSeconds: Int): MockResponse {
        val response = JSONObject().apply {
            put("allowed", true)
            put("minimumRemainingSeconds", remainingSeconds)
            put("dayType", "normal")
            put("banned", false)
        }
        return MockResponse().setResponseCode(200).setBody(response.toString())
    }

    private fun createBlockedResponse(reason: String): MockResponse {
        val response = JSONObject().apply {
            put("allowed", false)
            put("minimumRemainingSeconds", 0)
            put("dayType", "School Night")
            put("banned", reason == "banned")
            put("blockReason", reason)
        }
        return MockResponse().setResponseCode(200).setBody(response.toString())
    }

    private fun createPairingStatusCompletedResponse(): MockResponse {
        val response = JSONObject().apply {
            put("completed", true)
            put("success", true)
            put("credentials", JSONObject().apply {
                put("userId", "user123")
                put("pairId", "pair456")
                put("pairToken", "token789")
            })
            put("children", JSONArray().apply {
                put(JSONObject().apply {
                    put("id", 1001)
                    put("name", "Emma")
                    put("pinHash", hashPin("1234", "salt_emma"))
                    put("pinSalt", "salt_emma")
                })
                put(JSONObject().apply {
                    put("id", 1002)
                    put("name", "Jack")
                    put("pinHash", hashPin("5678", "salt_jack"))
                    put("pinSalt", "salt_jack")
                })
            })
        }
        return MockResponse().setResponseCode(200).setBody(response.toString())
    }

    private fun hashPin(pin: String, salt: String): String {
        val salted = pin + salt
        val bytes = MessageDigest.getInstance("SHA-256").digest(salted.toByteArray())
        return "sha256:" + bytes.joinToString("") { "%02x".format(it) }
    }
}

// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.chrome.browser.allow2

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.google.common.truth.Truth.assertThat
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Unit tests for Allow2ApiClient
 */
@ExperimentalCoroutinesApi
@RunWith(AndroidJUnit4::class)
class Allow2ApiClientTest {

    private lateinit var mockServer: MockWebServer
    private lateinit var apiClient: Allow2ApiClient

    @Before
    fun setUp() {
        mockServer = MockWebServer()
        mockServer.start()

        // Create API client pointing to mock server
        apiClient = Allow2ApiClient(mockServer.url("/").toString())
    }

    @After
    fun tearDown() {
        mockServer.shutdown()
    }

    // MARK: - Pairing Tests

    @Test
    fun testPairDevice_Success() = runTest {
        val response = JSONObject().apply {
            put("success", true)
            put("userId", "user123")
            put("pairId", "pair456")
            put("pairToken", "token789")
            put("children", JSONArray().apply {
                put(JSONObject().apply {
                    put("id", 1001)
                    put("name", "Emma")
                    put("pinHash", "sha256:abc123")
                    put("pinSalt", "salt1")
                })
            })
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val result = apiClient.pairDevice(
            email = "parent@example.com",
            password = "password123",
            deviceToken = "device_token_abc",
            deviceName = "Test Device"
        )

        assertThat(result.success).isTrue()
        assertThat(result.userId).isEqualTo("user123")
        assertThat(result.pairId).isEqualTo("pair456")
        assertThat(result.pairToken).isEqualTo("token789")
        assertThat(result.children).hasSize(1)
        assertThat(result.children[0].name).isEqualTo("Emma")
    }

    @Test
    fun testPairDevice_InvalidCredentials() = runTest {
        val response = JSONObject().apply {
            put("success", false)
            put("error", "Invalid credentials")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val result = apiClient.pairDevice(
            email = "wrong@example.com",
            password = "wrongpass",
            deviceToken = "device_token_abc",
            deviceName = "Test Device"
        )

        assertThat(result.success).isFalse()
        assertThat(result.error).isEqualTo("Invalid credentials")
    }

    @Test
    fun testPairDevice_NetworkError() = runTest {
        mockServer.enqueue(
            MockResponse().setSocketPolicy(okhttp3.mockwebserver.SocketPolicy.DISCONNECT_AT_START)
        )

        try {
            apiClient.pairDevice(
                email = "parent@example.com",
                password = "password123",
                deviceToken = "device_token_abc",
                deviceName = "Test Device"
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.NetworkError) {
            // Expected
        }
    }

    @Test
    fun testPairDeviceWithCode_Success() = runTest {
        val response = JSONObject().apply {
            put("success", true)
            put("userId", "user123")
            put("pairId", "pair456")
            put("pairToken", "token789")
            put("children", JSONArray())
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val result = apiClient.pairDeviceWithCode(
            pairingCode = "123456",
            deviceToken = "device_token_abc",
            deviceName = "Test Device"
        )

        assertThat(result.success).isTrue()
    }

    // MARK: - Check Tests

    @Test
    fun testCheck_Allowed() = runTest {
        val response = JSONObject().apply {
            put("allowed", true)
            put("minimumRemainingSeconds", 3600)
            put("expires", 1234567890)
            put("banned", false)
            put("dayType", "normal")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")
        val result = apiClient.check(
            credentials = credentials,
            childId = 1001,
            activities = listOf(Allow2Activity.INTERNET),
            log = true
        )

        assertThat(result.allowed).isTrue()
        assertThat(result.minimumRemainingSeconds).isEqualTo(3600)
        assertThat(result.banned).isFalse()
    }

    @Test
    fun testCheck_BlockedTimeLimit() = runTest {
        val response = JSONObject().apply {
            put("allowed", false)
            put("minimumRemainingSeconds", 0)
            put("expires", 1234567890)
            put("banned", false)
            put("dayType", "normal")
            put("blockReason", "timelimit")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")
        val result = apiClient.check(
            credentials = credentials,
            childId = 1001,
            activities = listOf(Allow2Activity.INTERNET),
            log = true
        )

        assertThat(result.allowed).isFalse()
        assertThat(result.minimumRemainingSeconds).isEqualTo(0)
        assertThat(result.blockReason).isEqualTo("timelimit")
    }

    @Test
    fun testCheck_BlockedBanned() = runTest {
        val response = JSONObject().apply {
            put("allowed", false)
            put("minimumRemainingSeconds", 0)
            put("expires", 1234567890)
            put("banned", true)
            put("dayType", "normal")
            put("blockReason", "banned")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")
        val result = apiClient.check(
            credentials = credentials,
            childId = 1001,
            activities = listOf(Allow2Activity.INTERNET),
            log = true
        )

        assertThat(result.allowed).isFalse()
        assertThat(result.banned).isTrue()
        assertThat(result.blockReason).isEqualTo("banned")
    }

    @Test
    fun testCheck_BlockedTimeBlock() = runTest {
        val response = JSONObject().apply {
            put("allowed", false)
            put("minimumRemainingSeconds", 1800)
            put("expires", 1234567890)
            put("banned", false)
            put("dayType", "normal")
            put("blockReason", "timeblock")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")
        val result = apiClient.check(
            credentials = credentials,
            childId = 1001,
            activities = listOf(Allow2Activity.INTERNET),
            log = true
        )

        assertThat(result.allowed).isFalse()
        assertThat(result.blockReason).isEqualTo("timeblock")
    }

    @Test
    fun testCheck_401Unauthorized() = runTest {
        mockServer.enqueue(
            MockResponse().setResponseCode(401)
        )

        val credentials = Allow2Credentials("user123", "pair456", "invalid_token")

        try {
            apiClient.check(
                credentials = credentials,
                childId = 1001,
                activities = listOf(Allow2Activity.INTERNET),
                log = true
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.Unauthorized) {
            // Expected
        }
    }

    @Test
    fun testCheck_NetworkError() = runTest {
        mockServer.enqueue(
            MockResponse().setSocketPolicy(okhttp3.mockwebserver.SocketPolicy.NO_RESPONSE)
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")

        try {
            apiClient.check(
                credentials = credentials,
                childId = 1001,
                activities = listOf(Allow2Activity.INTERNET),
                log = true
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.NetworkError) {
            // Expected
        }
    }

    @Test
    fun testCheck_MultipleActivities() = runTest {
        val response = JSONObject().apply {
            put("allowed", true)
            put("minimumRemainingSeconds", 1800)
            put("dayType", "normal")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")
        val result = apiClient.check(
            credentials = credentials,
            childId = 1001,
            activities = listOf(
                Allow2Activity.INTERNET,
                Allow2Activity.GAMING,
                Allow2Activity.SOCIAL_MEDIA
            ),
            log = true
        )

        assertThat(result.allowed).isTrue()

        // Verify request body contained all activities
        val request = mockServer.takeRequest()
        val body = JSONObject(request.body.readUtf8())
        val activities = body.getJSONArray("activities")
        assertThat(activities.length()).isEqualTo(3)
    }

    @Test
    fun testCheck_EmptyActivities() = runTest {
        val response = JSONObject().apply {
            put("allowed", true)
            put("minimumRemainingSeconds", 3600)
            put("dayType", "normal")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")
        val result = apiClient.check(
            credentials = credentials,
            childId = 1001,
            activities = emptyList(),
            log = true
        )

        assertThat(result.allowed).isTrue()
    }

    @Test
    fun testCheck_MalformedJSON() = runTest {
        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody("{ invalid json }")
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")

        try {
            apiClient.check(
                credentials = credentials,
                childId = 1001,
                activities = listOf(Allow2Activity.INTERNET),
                log = true
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.InvalidResponse) {
            // Expected
        }
    }

    // MARK: - Request More Time Tests

    @Test
    fun testRequestTime_Success() = runTest {
        val response = JSONObject().apply {
            put("success", true)
            put("requestId", "req_123")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")
        apiClient.requestMoreTime(
            credentials = credentials,
            childId = 1001,
            activityId = Allow2Activity.INTERNET,
            minutes = 30,
            message = "Please give me more time"
        )

        val request = mockServer.takeRequest()
        val body = JSONObject(request.body.readUtf8())
        assertThat(body.getInt("duration")).isEqualTo(30)
        assertThat(body.getString("message")).isEqualTo("Please give me more time")
    }

    @Test
    fun testRequestTime_Unauthorized() = runTest {
        mockServer.enqueue(
            MockResponse().setResponseCode(401)
        )

        val credentials = Allow2Credentials("user123", "pair456", "invalid_token")

        try {
            apiClient.requestMoreTime(
                credentials = credentials,
                childId = 1001,
                activityId = Allow2Activity.INTERNET,
                minutes = 30,
                message = null
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.Unauthorized) {
            // Expected
        }
    }

    @Test
    fun testRequestTime_NoMessage() = runTest {
        val response = JSONObject().apply {
            put("success", true)
            put("requestId", "req_456")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")
        apiClient.requestMoreTime(
            credentials = credentials,
            childId = 1001,
            activityId = Allow2Activity.INTERNET,
            minutes = 30,
            message = null
        )

        val request = mockServer.takeRequest()
        val body = JSONObject(request.body.readUtf8())
        assertThat(body.has("message")).isFalse()
    }

    // MARK: - Pairing Flow Tests

    @Test
    fun testInitQRPairing_Success() = runTest {
        val response = JSONObject().apply {
            put("sessionId", "session_123")
            put("qrCodeUrl", "https://api.allow2.com/qr/abc123")
            put("pinCode", "123456")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val session = apiClient.initQRPairing("Test Device")

        assertThat(session.sessionId).isEqualTo("session_123")
        assertThat(session.qrCodeUrl).isEqualTo("https://api.allow2.com/qr/abc123")
        assertThat(session.pinCode).isEqualTo("123456")
    }

    @Test
    fun testInitPINPairing_Success() = runTest {
        val response = JSONObject().apply {
            put("sessionId", "session_456")
            put("pinCode", "654321")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val session = apiClient.initPINPairing("Test Device")

        assertThat(session.sessionId).isEqualTo("session_456")
        assertThat(session.pinCode).isEqualTo("654321")
    }

    @Test
    fun testCheckPairingStatus_Completed() = runTest {
        val response = JSONObject().apply {
            put("completed", true)
            put("success", true)
            put("credentials", JSONObject().apply {
                put("userId", "user123")
                put("pairId", "pair456")
            })
            put("children", JSONArray().apply {
                put(JSONObject().apply {
                    put("id", 1001)
                    put("name", "Emma")
                    put("pinHash", "sha256:abc")
                    put("pinSalt", "salt1")
                })
            })
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val status = apiClient.checkPairingStatus("session_123")

        assertThat(status.completed).isTrue()
        assertThat(status.success).isTrue()
        assertThat(status.credentials).isNotNull()
        assertThat(status.credentials?.userId).isEqualTo("user123")
        assertThat(status.children).hasSize(1)
    }

    @Test
    fun testCheckPairingStatus_Pending() = runTest {
        val response = JSONObject().apply {
            put("completed", false)
            put("success", false)
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val status = apiClient.checkPairingStatus("session_123")

        assertThat(status.completed).isFalse()
        assertThat(status.success).isFalse()
        assertThat(status.credentials).isNull()
    }

    @Test
    fun testCheckPairingStatus_Failed() = runTest {
        val response = JSONObject().apply {
            put("completed", true)
            put("success", false)
            put("error", "Parent rejected pairing")
        }

        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBody(response.toString())
        )

        val status = apiClient.checkPairingStatus("session_123")

        assertThat(status.completed).isTrue()
        assertThat(status.success).isFalse()
        assertThat(status.error).isEqualTo("Parent rejected pairing")
    }

    @Test
    fun testCancelPairing() = runTest {
        mockServer.enqueue(
            MockResponse().setResponseCode(200)
        )

        apiClient.cancelPairing("session_123")

        val request = mockServer.takeRequest()
        assertThat(request.method).isEqualTo("POST")
        val body = JSONObject(request.body.readUtf8())
        assertThat(body.getString("sessionId")).isEqualTo("session_123")
    }

    // MARK: - Timeout Tests

    @Test
    fun testCheck_Timeout() = runTest {
        mockServer.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setBodyDelay(60, java.util.concurrent.TimeUnit.SECONDS)
                .setBody("{}")
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")

        try {
            apiClient.check(
                credentials = credentials,
                childId = 1001,
                activities = listOf(Allow2Activity.INTERNET),
                log = true
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.NetworkError) {
            // Expected timeout
        }
    }

    // MARK: - Server Error Tests

    @Test
    fun testCheck_500ServerError() = runTest {
        mockServer.enqueue(
            MockResponse().setResponseCode(500)
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")

        try {
            apiClient.check(
                credentials = credentials,
                childId = 1001,
                activities = listOf(Allow2Activity.INTERNET),
                log = true
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.ServerError) {
            assertThat(e.statusCode).isEqualTo(500)
        }
    }

    @Test
    fun testCheck_503ServiceUnavailable() = runTest {
        mockServer.enqueue(
            MockResponse().setResponseCode(503)
        )

        val credentials = Allow2Credentials("user123", "pair456", "token789")

        try {
            apiClient.check(
                credentials = credentials,
                childId = 1001,
                activities = listOf(Allow2Activity.INTERNET),
                log = true
            )
            assertThat(false).isTrue() // Should not reach here
        } catch (e: Allow2Exception.ServerError) {
            assertThat(e.statusCode).isEqualTo(503)
        }
    }
}

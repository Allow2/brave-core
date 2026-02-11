/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.services

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.chromium.chrome.browser.allow2.Allow2Constants
import org.chromium.chrome.browser.allow2.models.*
import org.chromium.chrome.browser.allow2.util.Allow2DeviceInfo
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL
import javax.net.ssl.HttpsURLConnection

/**
 * HTTP client for Allow2 API communication.
 * Uses HttpURLConnection for compatibility with Chromium's network stack.
 */
class Allow2ApiClient(private val context: Context) {

    private val credentialManager by lazy {
        Allow2CredentialManager.getInstance(context)
    }

    // ==================== Pairing API ====================

    /**
     * Initialize QR code pairing session.
     */
    suspend fun initQrPairing(): ApiResult<QrPairingResponse> = withContext(Dispatchers.IO) {
        try {
            val requestBody = JSONObject().apply {
                put("uuid", Allow2DeviceInfo.getDeviceId(context))
                put("name", Allow2DeviceInfo.getDeviceName(context))
                put("platform", Allow2DeviceInfo.getPlatform())
                put("deviceToken", Allow2DeviceInfo.getDeviceId(context))
            }

            val response = postJson(
                "${Allow2Constants.API_HOST}${Allow2Constants.PAIR_QR_INIT}",
                requestBody
            )

            when (response) {
                is ApiResult.Success -> {
                    val json = response.data
                    val qrResponse = QrPairingResponse(
                        sessionId = json.optString("sessionId"),
                        qrCodeUrl = json.optString("qrCodeUrl"),
                        pin = json.optString("pin", ""),
                        expiresIn = json.optLong("expiresIn", 300)
                    )
                    ApiResult.Success(qrResponse)
                }
                is ApiResult.Error -> response
                is ApiResult.Unauthorized -> response
            }
        } catch (e: Exception) {
            ApiResult.Error("Failed to initialize pairing: ${e.message}")
        }
    }

    /**
     * Poll QR pairing status.
     */
    suspend fun pollQrStatus(sessionId: String): ApiResult<PairingStatusResponse> =
        withContext(Dispatchers.IO) {
            try {
                val response = getJson(
                    "${Allow2Constants.API_HOST}${Allow2Constants.PAIR_QR_STATUS}/$sessionId"
                )

                when (response) {
                    is ApiResult.Success -> {
                        val json = response.data
                        val status = json.optString("status", "pending")

                        val statusResponse = when (status) {
                            "completed" -> {
                                val credentialsJson = json.optJSONObject("credentials")
                                val childrenJson = json.optJSONArray("children")

                                PairingStatusResponse(
                                    status = PairingStatus.COMPLETED,
                                    scanned = true,
                                    credentials = credentialsJson?.let {
                                        Allow2Credentials.fromJson(it)
                                    },
                                    children = childrenJson?.let {
                                        Allow2Child.fromJsonArray(it)
                                    }
                                )
                            }
                            "pending" -> {
                                PairingStatusResponse(
                                    status = PairingStatus.PENDING,
                                    scanned = json.optBoolean("scanned", false),
                                    credentials = null,
                                    children = null
                                )
                            }
                            "expired" -> {
                                PairingStatusResponse(
                                    status = PairingStatus.EXPIRED,
                                    scanned = false,
                                    credentials = null,
                                    children = null
                                )
                            }
                            else -> {
                                PairingStatusResponse(
                                    status = PairingStatus.FAILED,
                                    scanned = false,
                                    credentials = null,
                                    children = null
                                )
                            }
                        }
                        ApiResult.Success(statusResponse)
                    }
                    is ApiResult.Error -> response
                    is ApiResult.Unauthorized -> response
                }
            } catch (e: Exception) {
                ApiResult.Error("Failed to poll status: ${e.message}")
            }
        }

    /**
     * Initialize PIN pairing session.
     */
    suspend fun initPinPairing(): ApiResult<PinPairingResponse> = withContext(Dispatchers.IO) {
        try {
            val requestBody = JSONObject().apply {
                put("uuid", Allow2DeviceInfo.getDeviceId(context))
                put("name", Allow2DeviceInfo.getDeviceName(context))
                put("platform", Allow2DeviceInfo.getPlatform())
                put("deviceToken", Allow2DeviceInfo.getDeviceId(context))
            }

            val response = postJson(
                "${Allow2Constants.API_HOST}${Allow2Constants.PAIR_PIN_INIT}",
                requestBody
            )

            when (response) {
                is ApiResult.Success -> {
                    val json = response.data
                    val pinResponse = PinPairingResponse(
                        sessionId = json.optString("sessionId"),
                        pin = json.optString("pin"),
                        expiresIn = json.optLong("expiresIn", 300)
                    )
                    ApiResult.Success(pinResponse)
                }
                is ApiResult.Error -> response
                is ApiResult.Unauthorized -> response
            }
        } catch (e: Exception) {
            ApiResult.Error("Failed to initialize PIN pairing: ${e.message}")
        }
    }

    // ==================== Service API ====================

    /**
     * Perform usage check for the current child.
     */
    suspend fun check(
        childId: Long,
        activities: List<Allow2Activity> = listOf(Allow2Activity.INTERNET)
    ): ApiResult<Allow2CheckResult> = withContext(Dispatchers.IO) {
        try {
            val credentials = credentialManager.getCredentials()
            if (!credentials.isValid()) {
                return@withContext ApiResult.Error("No valid credentials")
            }

            val activitiesArray = JSONArray().apply {
                activities.forEach { activity ->
                    put(JSONObject().apply {
                        put("id", activity.id)
                        put("log", true)
                    })
                }
            }

            val requestBody = JSONObject().apply {
                put("userId", credentials.userId)
                put("pairId", credentials.pairId)
                put("pairToken", credentials.pairToken)
                put("childId", childId.toString())
                put("activities", activitiesArray)
                put("tz", Allow2DeviceInfo.getTimezone())
            }

            val response = postJson(
                "${Allow2Constants.SERVICE_HOST}${Allow2Constants.SERVICE_CHECK}",
                requestBody
            )

            when (response) {
                is ApiResult.Success -> {
                    val checkResult = Allow2CheckResult.fromJson(response.data)
                    ApiResult.Success(checkResult)
                }
                is ApiResult.Error -> response
                is ApiResult.Unauthorized -> {
                    // 401 means device has been unpaired remotely
                    credentialManager.clearAll()
                    response
                }
            }
        } catch (e: Exception) {
            ApiResult.Error("Check failed: ${e.message}")
        }
    }

    /**
     * Request more time for an activity.
     */
    suspend fun requestMoreTime(
        childId: Long,
        activityId: Int,
        requestedMinutes: Int,
        message: String
    ): ApiResult<RequestResponse> = withContext(Dispatchers.IO) {
        try {
            val credentials = credentialManager.getCredentials()
            if (!credentials.isValid()) {
                return@withContext ApiResult.Error("No valid credentials")
            }

            val requestBody = JSONObject().apply {
                put("userId", credentials.userId)
                put("pairId", credentials.pairId)
                put("pairToken", credentials.pairToken)
                put("childId", childId.toString())
                put("activityId", activityId)
                put("requestedMinutes", requestedMinutes)
                put("message", message)
            }

            val response = postJson(
                "${Allow2Constants.API_HOST}${Allow2Constants.REQUEST_CREATE}",
                requestBody
            )

            when (response) {
                is ApiResult.Success -> {
                    val json = response.data
                    ApiResult.Success(
                        RequestResponse(
                            success = json.optBoolean("success", true),
                            requestId = json.optString("requestId"),
                            message = json.optString("message")
                        )
                    )
                }
                is ApiResult.Error -> response
                is ApiResult.Unauthorized -> {
                    credentialManager.clearAll()
                    response
                }
            }
        } catch (e: Exception) {
            ApiResult.Error("Request failed: ${e.message}")
        }
    }

    // ==================== HTTP Helpers ====================

    private fun postJson(urlString: String, body: JSONObject): ApiResult<JSONObject> {
        var connection: HttpURLConnection? = null
        try {
            val url = URL(urlString)
            connection = (url.openConnection() as HttpsURLConnection).apply {
                requestMethod = "POST"
                setRequestProperty("Content-Type", "application/json")
                setRequestProperty("Accept", "application/json")
                doOutput = true
                connectTimeout = 30000
                readTimeout = 30000
            }

            OutputStreamWriter(connection.outputStream).use { writer ->
                writer.write(body.toString())
                writer.flush()
            }

            val responseCode = connection.responseCode

            return when {
                responseCode == 401 -> ApiResult.Unauthorized()
                responseCode in 200..299 -> {
                    val responseBody = BufferedReader(
                        InputStreamReader(connection.inputStream)
                    ).use { it.readText() }
                    ApiResult.Success(JSONObject(responseBody))
                }
                else -> {
                    val errorBody = try {
                        BufferedReader(
                            InputStreamReader(connection.errorStream)
                        ).use { it.readText() }
                    } catch (e: Exception) {
                        "Unknown error"
                    }
                    ApiResult.Error("HTTP $responseCode: $errorBody")
                }
            }
        } catch (e: Exception) {
            return ApiResult.Error("Network error: ${e.message}")
        } finally {
            connection?.disconnect()
        }
    }

    private fun getJson(urlString: String): ApiResult<JSONObject> {
        var connection: HttpURLConnection? = null
        try {
            val url = URL(urlString)
            connection = (url.openConnection() as HttpsURLConnection).apply {
                requestMethod = "GET"
                setRequestProperty("Accept", "application/json")
                connectTimeout = 30000
                readTimeout = 30000
            }

            val responseCode = connection.responseCode

            return when {
                responseCode == 401 -> ApiResult.Unauthorized()
                responseCode in 200..299 -> {
                    val responseBody = BufferedReader(
                        InputStreamReader(connection.inputStream)
                    ).use { it.readText() }
                    ApiResult.Success(JSONObject(responseBody))
                }
                else -> {
                    val errorBody = try {
                        BufferedReader(
                            InputStreamReader(connection.errorStream)
                        ).use { it.readText() }
                    } catch (e: Exception) {
                        "Unknown error"
                    }
                    ApiResult.Error("HTTP $responseCode: $errorBody")
                }
            }
        } catch (e: Exception) {
            return ApiResult.Error("Network error: ${e.message}")
        } finally {
            connection?.disconnect()
        }
    }

    companion object {
        private const val TAG = "Allow2ApiClient"

        @Volatile
        private var instance: Allow2ApiClient? = null

        fun getInstance(context: Context): Allow2ApiClient {
            return instance ?: synchronized(this) {
                instance ?: Allow2ApiClient(context.applicationContext).also {
                    instance = it
                }
            }
        }
    }
}

// ==================== Response Data Classes ====================

sealed class ApiResult<out T> {
    data class Success<T>(val data: T) : ApiResult<T>()
    data class Error(val message: String) : ApiResult<Nothing>()
    class Unauthorized : ApiResult<Nothing>()
}

data class QrPairingResponse(
    val sessionId: String,
    val qrCodeUrl: String,
    val pin: String,
    val expiresIn: Long
)

data class PinPairingResponse(
    val sessionId: String,
    val pin: String,
    val expiresIn: Long
)

enum class PairingStatus {
    PENDING,
    COMPLETED,
    EXPIRED,
    FAILED
}

data class PairingStatusResponse(
    val status: PairingStatus,
    val scanned: Boolean,
    val credentials: Allow2Credentials?,
    val children: List<Allow2Child>?
)

data class RequestResponse(
    val success: Boolean,
    val requestId: String?,
    val message: String?
)

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.services

import android.content.Context
import android.content.Intent
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.chromium.chrome.browser.allow2.Allow2Constants
import org.chromium.chrome.browser.allow2.models.*
import org.chromium.chrome.browser.allow2.ui.Allow2BlockOverlayActivity
import org.chromium.chrome.browser.allow2.util.Allow2PinVerifier

/**
 * Main service singleton for Allow2 integration.
 * Coordinates between credential management, API calls, and UI.
 */
class Allow2Service private constructor(private val context: Context) {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val credentialManager by lazy {
        Allow2CredentialManager.getInstance(context)
    }

    private val apiClient by lazy {
        Allow2ApiClient.getInstance(context)
    }

    private val usageTracker by lazy {
        Allow2UsageTracker.getInstance(context)
    }

    // ==================== State Flows ====================

    private val _pairingState = MutableStateFlow<Allow2PairingState>(Allow2PairingState.Idle)
    val pairingState: StateFlow<Allow2PairingState> = _pairingState.asStateFlow()

    private val _isBlocked = MutableStateFlow(false)
    val isBlocked: StateFlow<Boolean> = _isBlocked.asStateFlow()

    private val _checkResult = MutableStateFlow<Allow2CheckResult?>(null)
    val checkResult: StateFlow<Allow2CheckResult?> = _checkResult.asStateFlow()

    private var pairingJob: Job? = null
    private var statusPollJob: Job? = null

    // ==================== State Queries ====================

    /**
     * Check if device is paired with Allow2.
     */
    fun isPaired(): Boolean = credentialManager.hasCredentials()

    /**
     * Check if Allow2 is enabled.
     */
    fun isEnabled(): Boolean = credentialManager.isEnabled()

    /**
     * Get the list of children.
     */
    fun getChildren(): List<Allow2Child> = credentialManager.getChildren()

    /**
     * Get the currently selected child.
     */
    fun getCurrentChild(): Allow2Child? = credentialManager.getCurrentChild()

    /**
     * Check if a child is currently selected.
     */
    fun hasSelectedChild(): Boolean = credentialManager.getCurrentChildId() != null

    /**
     * Check if device is in shared mode.
     */
    fun isSharedDeviceMode(): Boolean = credentialManager.isSharedDeviceMode()

    // ==================== Pairing ====================

    /**
     * Start QR code pairing process.
     */
    fun startQrPairing() {
        pairingJob?.cancel()
        pairingJob = scope.launch {
            _pairingState.value = Allow2PairingState.Initializing

            when (val result = apiClient.initQrPairing()) {
                is ApiResult.Success -> {
                    val response = result.data
                    _pairingState.value = Allow2PairingState.QrCodeReady(
                        sessionId = response.sessionId,
                        qrCodeUrl = response.qrCodeUrl,
                        pin = response.pin,
                        expiresIn = response.expiresIn
                    )
                    startPollStatus(response.sessionId)
                }
                is ApiResult.Error -> {
                    _pairingState.value = Allow2PairingState.Failed(
                        error = result.message,
                        retryable = true
                    )
                }
                is ApiResult.Unauthorized -> {
                    _pairingState.value = Allow2PairingState.Failed(
                        error = "Unauthorized",
                        retryable = false
                    )
                }
            }
        }
    }

    /**
     * Start PIN pairing process.
     */
    fun startPinPairing() {
        pairingJob?.cancel()
        pairingJob = scope.launch {
            _pairingState.value = Allow2PairingState.Initializing

            when (val result = apiClient.initPinPairing()) {
                is ApiResult.Success -> {
                    val response = result.data
                    _pairingState.value = Allow2PairingState.QrCodeReady(
                        sessionId = response.sessionId,
                        qrCodeUrl = "", // No QR for PIN mode
                        pin = response.pin,
                        expiresIn = response.expiresIn
                    )
                    startPollStatus(response.sessionId)
                }
                is ApiResult.Error -> {
                    _pairingState.value = Allow2PairingState.Failed(
                        error = result.message,
                        retryable = true
                    )
                }
                is ApiResult.Unauthorized -> {
                    _pairingState.value = Allow2PairingState.Failed(
                        error = "Unauthorized",
                        retryable = false
                    )
                }
            }
        }
    }

    /**
     * Poll for pairing status.
     */
    private fun startPollStatus(sessionId: String) {
        statusPollJob?.cancel()
        statusPollJob = scope.launch {
            val startTime = System.currentTimeMillis()
            val timeoutMs = Allow2Constants.PAIRING_TIMEOUT_MS

            while (isActive) {
                // Check for timeout
                if (System.currentTimeMillis() - startTime > timeoutMs) {
                    _pairingState.value = Allow2PairingState.Expired
                    break
                }

                when (val result = apiClient.pollQrStatus(sessionId)) {
                    is ApiResult.Success -> {
                        val response = result.data
                        when (response.status) {
                            PairingStatus.COMPLETED -> {
                                // Save credentials and children
                                response.credentials?.let { creds ->
                                    credentialManager.saveCredentials(creds)
                                }
                                response.children?.let { children ->
                                    credentialManager.saveChildren(children)
                                }

                                _pairingState.value = Allow2PairingState.Completed(
                                    credentials = response.credentials ?: Allow2Credentials.empty(),
                                    children = response.children ?: emptyList()
                                )
                                break
                            }
                            PairingStatus.EXPIRED -> {
                                _pairingState.value = Allow2PairingState.Expired
                                break
                            }
                            PairingStatus.FAILED -> {
                                _pairingState.value = Allow2PairingState.Failed(
                                    error = "Pairing failed",
                                    retryable = true
                                )
                                break
                            }
                            PairingStatus.PENDING -> {
                                if (response.scanned) {
                                    _pairingState.value = Allow2PairingState.Scanned(sessionId)
                                }
                                // Continue polling
                            }
                        }
                    }
                    is ApiResult.Error -> {
                        Log.e(TAG, "Poll error: ${result.message}")
                        // Continue polling on transient errors
                    }
                    is ApiResult.Unauthorized -> {
                        _pairingState.value = Allow2PairingState.Failed(
                            error = "Unauthorized",
                            retryable = false
                        )
                        break
                    }
                }

                delay(Allow2Constants.PAIRING_POLL_INTERVAL_MS)
            }
        }
    }

    /**
     * Cancel ongoing pairing.
     */
    fun cancelPairing() {
        pairingJob?.cancel()
        statusPollJob?.cancel()
        _pairingState.value = Allow2PairingState.Cancelled
    }

    /**
     * Reset pairing state for retry.
     */
    fun resetPairingState() {
        _pairingState.value = Allow2PairingState.Idle
    }

    // ==================== Child Selection ====================

    /**
     * Verify PIN and select a child.
     */
    fun selectChild(child: Allow2Child, pin: String): Boolean {
        if (child.hasPIN()) {
            if (!Allow2PinVerifier.verify(pin, child)) {
                return false
            }
        }

        credentialManager.saveCurrentChildId(child.id)
        startTracking()
        return true
    }

    /**
     * Clear current child selection.
     */
    fun clearChildSelection() {
        stopTracking()
        credentialManager.clearCurrentChild()
    }

    /**
     * Check if we need to show the child selection shield.
     */
    fun needsChildSelection(): Boolean {
        if (!isPaired()) return false
        if (!isSharedDeviceMode()) {
            // In single-child mode, auto-select if only one child
            val children = getChildren()
            if (children.size == 1) {
                credentialManager.saveCurrentChildId(children[0].id)
                return false
            }
        }
        return !hasSelectedChild()
    }

    // ==================== Usage Tracking ====================

    /**
     * Start usage tracking.
     */
    fun startTracking() {
        usageTracker.setListener(trackerListener)
        usageTracker.startTracking()
    }

    /**
     * Stop usage tracking.
     */
    fun stopTracking() {
        usageTracker.stopTracking()
    }

    /**
     * Notify navigation event.
     */
    fun onNavigate(url: String) {
        usageTracker.onNavigate(url)
    }

    /**
     * Handle app resume.
     */
    fun onResume() {
        usageTracker.onResume()
    }

    /**
     * Handle app pause.
     */
    fun onPause() {
        usageTracker.onPause()
    }

    private val trackerListener = object : Allow2UsageTracker.UsageTrackerListener {
        override fun onAllowed(result: Allow2CheckResult) {
            _isBlocked.value = false
            _checkResult.value = result
        }

        override fun onBlocked(result: Allow2CheckResult) {
            _isBlocked.value = true
            _checkResult.value = result
            showBlockOverlay(result)
        }

        override fun onWarning(type: Allow2UsageTracker.WarningType, remainingSeconds: Int) {
            showWarning(type, remainingSeconds)
        }

        override fun onSessionExpired() {
            clearChildSelection()
            serviceListener?.onNeedChildSelection()
        }

        override fun onUnpaired() {
            handleUnpaired()
        }

        override fun onError(message: String) {
            Log.e(TAG, "Tracker error: $message")
        }
    }

    // ==================== Request More Time ====================

    /**
     * Request more time from parent.
     */
    suspend fun requestMoreTime(
        activityId: Int = Allow2Activity.INTERNET.id,
        minutes: Int = 30,
        message: String
    ): Result<String> {
        val childId = credentialManager.getCurrentChildId()
            ?: return Result.failure(Exception("No child selected"))

        return when (val result = apiClient.requestMoreTime(childId, activityId, minutes, message)) {
            is ApiResult.Success -> {
                if (result.data.success) {
                    Result.success(result.data.message ?: "Request sent")
                } else {
                    Result.failure(Exception(result.data.message ?: "Request failed"))
                }
            }
            is ApiResult.Error -> {
                Result.failure(Exception(result.message))
            }
            is ApiResult.Unauthorized -> {
                handleUnpaired()
                Result.failure(Exception("Device unpaired"))
            }
        }
    }

    // ==================== UI Helpers ====================

    private fun showBlockOverlay(result: Allow2CheckResult) {
        val intent = Intent(context, Allow2BlockOverlayActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            putExtra(Allow2Constants.EXTRA_BLOCK_REASON, result.getBlockReason())
            putExtra(Allow2Constants.EXTRA_TIME_REMAINING, result.getRemainingSeconds())
            result.todayDayType?.let {
                putExtra(Allow2Constants.EXTRA_DAY_TYPE, it.name)
            }
        }
        context.startActivity(intent)
    }

    private fun showWarning(type: Allow2UsageTracker.WarningType, remainingSeconds: Int) {
        serviceListener?.onTimeWarning(type, remainingSeconds)
    }

    private fun handleUnpaired() {
        stopTracking()
        credentialManager.clearAll()
        _isBlocked.value = false
        _checkResult.value = null
        serviceListener?.onDeviceUnpaired()
    }

    // ==================== Service Listener ====================

    interface Allow2ServiceListener {
        fun onNeedChildSelection()
        fun onTimeWarning(type: Allow2UsageTracker.WarningType, remainingSeconds: Int)
        fun onDeviceUnpaired()
    }

    private var serviceListener: Allow2ServiceListener? = null

    fun setServiceListener(listener: Allow2ServiceListener?) {
        serviceListener = listener
    }

    // ==================== Lifecycle ====================

    fun destroy() {
        stopTracking()
        pairingJob?.cancel()
        statusPollJob?.cancel()
        scope.cancel()
    }

    companion object {
        private const val TAG = "Allow2Service"

        @Volatile
        private var instance: Allow2Service? = null

        fun getInstance(context: Context): Allow2Service {
            return instance ?: synchronized(this) {
                instance ?: Allow2Service(context.applicationContext).also {
                    instance = it
                }
            }
        }
    }
}

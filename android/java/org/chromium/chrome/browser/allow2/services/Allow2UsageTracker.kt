/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.services

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import org.chromium.chrome.browser.allow2.Allow2Constants
import org.chromium.chrome.browser.allow2.models.Allow2Activity
import org.chromium.chrome.browser.allow2.models.Allow2CheckResult

/**
 * Tracks browser usage and reports to Allow2 service.
 * Uses a Handler for periodic checks and triggers callbacks for UI updates.
 */
class Allow2UsageTracker(private val context: Context) {

    private val handler = Handler(Looper.getMainLooper())
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val credentialManager by lazy {
        Allow2CredentialManager.getInstance(context)
    }

    private val apiClient by lazy {
        Allow2ApiClient.getInstance(context)
    }

    private var isTracking = false
    private var lastUrl: String? = null
    private var listener: UsageTrackerListener? = null

    private val checkRunnable = object : Runnable {
        override fun run() {
            if (isTracking) {
                performCheck()
                handler.postDelayed(this, Allow2Constants.CHECK_INTERVAL_MS)
            }
        }
    }

    // ==================== Public API ====================

    /**
     * Set the usage tracker listener.
     */
    fun setListener(listener: UsageTrackerListener?) {
        this.listener = listener
    }

    /**
     * Start tracking usage.
     */
    fun startTracking() {
        if (!credentialManager.hasCredentials()) {
            Log.d(TAG, "No credentials, not starting tracker")
            return
        }

        if (credentialManager.getCurrentChildId() == null) {
            Log.d(TAG, "No child selected, not starting tracker")
            return
        }

        if (isTracking) {
            return
        }

        Log.d(TAG, "Starting usage tracker")
        isTracking = true

        // Perform immediate check
        performCheck()

        // Schedule periodic checks
        handler.postDelayed(checkRunnable, Allow2Constants.CHECK_INTERVAL_MS)
    }

    /**
     * Stop tracking usage.
     */
    fun stopTracking() {
        Log.d(TAG, "Stopping usage tracker")
        isTracking = false
        handler.removeCallbacks(checkRunnable)
    }

    /**
     * Called when user navigates to a new URL.
     * This triggers an immediate check if the activity type changes.
     */
    fun onNavigate(url: String) {
        if (!isTracking) return

        // Detect activity type from URL
        val activity = Allow2Activity.detectFromUrl(url)

        // If activity type changed or significant navigation, perform check
        val lastActivity = lastUrl?.let { Allow2Activity.detectFromUrl(it) }
        if (lastActivity != activity || lastUrl != url) {
            lastUrl = url
            performCheck(listOf(activity))
        }
    }

    /**
     * Called when the app resumes from background.
     */
    fun onResume() {
        if (!credentialManager.hasCredentials()) return

        val lastCheck = credentialManager.getLastCheckTime()
        val timeSinceLastCheck = System.currentTimeMillis() - lastCheck

        // If more than session timeout, need to re-verify child
        if (timeSinceLastCheck > Allow2Constants.SESSION_TIMEOUT_MS) {
            listener?.onSessionExpired()
            return
        }

        startTracking()
    }

    /**
     * Called when the app goes to background.
     */
    fun onPause() {
        stopTracking()
    }

    /**
     * Force an immediate check.
     */
    fun forceCheck() {
        performCheck()
    }

    /**
     * Get the cached check result.
     */
    fun getCachedResult(): Allow2CheckResult? {
        return credentialManager.getCachedCheckResult()
    }

    // ==================== Internal Methods ====================

    private fun performCheck(activities: List<Allow2Activity> = listOf(Allow2Activity.INTERNET)) {
        val childId = credentialManager.getCurrentChildId() ?: return

        scope.launch {
            try {
                when (val result = apiClient.check(childId, activities)) {
                    is ApiResult.Success -> {
                        val checkResult = result.data
                        credentialManager.saveLastCheckResult(checkResult)

                        if (checkResult.allowed) {
                            listener?.onAllowed(checkResult)
                        } else {
                            listener?.onBlocked(checkResult)
                        }

                        // Check for warnings
                        checkWarnings(checkResult)
                    }
                    is ApiResult.Error -> {
                        Log.e(TAG, "Check failed: ${result.message}")
                        // Use cached result if available
                        val cached = credentialManager.getCachedCheckResult()
                        if (cached != null && !cached.isExpired()) {
                            if (cached.allowed) {
                                listener?.onAllowed(cached)
                            } else {
                                listener?.onBlocked(cached)
                            }
                        } else {
                            listener?.onError(result.message)
                        }
                    }
                    is ApiResult.Unauthorized -> {
                        Log.w(TAG, "Device unpaired remotely")
                        stopTracking()
                        listener?.onUnpaired()
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Check exception", e)
                listener?.onError(e.message ?: "Unknown error")
            }
        }
    }

    private fun checkWarnings(result: Allow2CheckResult) {
        val remaining = result.getRemainingSeconds()

        when {
            remaining <= Allow2Constants.WARNING_1_MIN -> {
                listener?.onWarning(WarningType.CRITICAL, remaining)
            }
            remaining <= Allow2Constants.WARNING_5_MIN -> {
                listener?.onWarning(WarningType.HIGH, remaining)
            }
            remaining <= Allow2Constants.WARNING_15_MIN -> {
                listener?.onWarning(WarningType.LOW, remaining)
            }
        }
    }

    // ==================== Listener Interface ====================

    interface UsageTrackerListener {
        /**
         * Called when usage is allowed.
         */
        fun onAllowed(result: Allow2CheckResult)

        /**
         * Called when usage is blocked.
         */
        fun onBlocked(result: Allow2CheckResult)

        /**
         * Called when a time warning should be shown.
         */
        fun onWarning(type: WarningType, remainingSeconds: Int)

        /**
         * Called when the session has expired (needs child re-selection).
         */
        fun onSessionExpired()

        /**
         * Called when the device has been unpaired remotely.
         */
        fun onUnpaired()

        /**
         * Called when an error occurs.
         */
        fun onError(message: String)
    }

    enum class WarningType {
        LOW,      // 15 minutes
        MEDIUM,   // 5 minutes
        HIGH,     // 5 minutes (duplicate for compatibility)
        CRITICAL  // 1 minute or less
    }

    companion object {
        private const val TAG = "Allow2UsageTracker"

        @Volatile
        private var instance: Allow2UsageTracker? = null

        fun getInstance(context: Context): Allow2UsageTracker {
            return instance ?: synchronized(this) {
                instance ?: Allow2UsageTracker(context.applicationContext).also {
                    instance = it
                }
            }
        }
    }
}

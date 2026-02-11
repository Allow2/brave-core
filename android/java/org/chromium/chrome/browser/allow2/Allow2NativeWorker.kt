/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2

import android.content.Context
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.chromium.base.ContextUtils
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.allow2.models.Allow2CheckResult
import org.chromium.chrome.browser.allow2.services.Allow2Service
import org.chromium.chrome.browser.allow2.services.Allow2UsageTracker

/**
 * Native worker class that integrates Allow2 with Brave's browser process.
 * This class bridges between the Kotlin service layer and the native C++ browser.
 *
 * It follows the BraveSyncWorker pattern and implements lifecycle-aware
 * integration with Brave's tab and navigation systems.
 */
class Allow2NativeWorker private constructor(context: Context) :
    Allow2Bridge.Allow2BridgeObserver {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val service: Allow2Service = Allow2Service.getInstance(context)
    private var bridge: Allow2Bridge? = null

    private val _isBlocked = MutableStateFlow(false)
    val isBlocked: StateFlow<Boolean> = _isBlocked.asStateFlow()

    private val _currentCheckResult = MutableStateFlow<Allow2CheckResult?>(null)
    val currentCheckResult: StateFlow<Allow2CheckResult?> = _currentCheckResult.asStateFlow()

    private var isInitialized = false

    // ==================== Initialization ====================

    /**
     * Initialize the native worker and JNI bridge.
     * Must be called on the UI thread.
     */
    fun initialize() {
        ThreadUtils.assertOnUiThread()

        if (isInitialized) {
            Log.w(TAG, "Allow2NativeWorker already initialized")
            return
        }

        try {
            bridge = Allow2Bridge.getInstance()
            bridge?.setObserver(this)

            // Sync initial state
            scope.launch {
                service.isBlocked.collect { blocked ->
                    _isBlocked.value = blocked
                    bridge?.notifyBlockedStateChanged(blocked)
                }
            }

            scope.launch {
                service.checkResult.collect { result ->
                    _currentCheckResult.value = result
                }
            }

            isInitialized = true
            Log.i(TAG, "Allow2NativeWorker initialized successfully")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to initialize Allow2NativeWorker", e)
        }
    }

    /**
     * Shutdown the native worker.
     */
    fun shutdown() {
        ThreadUtils.assertOnUiThread()

        bridge?.setObserver(null)
        bridge = null
        isInitialized = false

        Log.i(TAG, "Allow2NativeWorker shutdown")
    }

    // ==================== Navigation Integration ====================

    /**
     * Check if navigation to the given URL should be allowed.
     * Called from native code before navigating.
     *
     * @param url The URL being navigated to
     * @return true if navigation should proceed, false if blocked
     */
    fun shouldAllowNavigation(url: String): Boolean {
        if (!isInitialized) return true
        if (!service.isPaired() || !service.isEnabled()) return true

        val blocked = _isBlocked.value
        if (blocked) {
            Log.d(TAG, "Navigation blocked: $url")
            showBlockOverlay()
            return false
        }

        return true
    }

    /**
     * Called when a navigation completes successfully.
     * Notifies the service to track usage.
     *
     * @param url The URL that was navigated to
     */
    fun onNavigationComplete(url: String) {
        if (!isInitialized) return
        service.onNavigate(url)
    }

    /**
     * Called when a new tab is created.
     *
     * @param tabId The ID of the new tab
     */
    fun onTabCreated(tabId: Long) {
        if (!isInitialized) return
        // Reserved for future tab-specific tracking
        Log.d(TAG, "Tab created: $tabId")
    }

    /**
     * Called when a tab is closed.
     *
     * @param tabId The ID of the closed tab
     */
    fun onTabClosed(tabId: Long) {
        if (!isInitialized) return
        // Reserved for future tab-specific tracking
        Log.d(TAG, "Tab closed: $tabId")
    }

    // ==================== Lifecycle Integration ====================

    /**
     * Called when the browser activity resumes.
     */
    fun onResume() {
        if (!isInitialized) return
        service.onResume()
    }

    /**
     * Called when the browser activity pauses.
     */
    fun onPause() {
        if (!isInitialized) return
        service.onPause()
    }

    // ==================== Allow2Bridge.Allow2BridgeObserver ====================

    override fun onBlockedStateChanged(isBlocked: Boolean) {
        _isBlocked.value = isBlocked
        if (isBlocked) {
            showBlockOverlay()
        }
    }

    override fun onWarning(warningLevel: Int, remainingSeconds: Int) {
        scope.launch {
            val warningType = when (warningLevel) {
                1 -> Allow2UsageTracker.WarningType.LOW
                2 -> Allow2UsageTracker.WarningType.MEDIUM
                3 -> Allow2UsageTracker.WarningType.HIGH
                4 -> Allow2UsageTracker.WarningType.CRITICAL
                else -> return@launch
            }
            // Notify observers about the warning
            Log.d(TAG, "Warning: level=$warningLevel, remaining=${remainingSeconds}s")
        }
    }

    override fun onUnpaired() {
        _isBlocked.value = false
        _currentCheckResult.value = null
        Log.i(TAG, "Device unpaired from Allow2")
    }

    override fun onNeedChildSelection() {
        showChildSelectDialog()
    }

    // ==================== UI Helpers ====================

    private fun showBlockOverlay() {
        scope.launch(Dispatchers.Main) {
            bridge?.showBlockOverlay()
        }
    }

    private fun showChildSelectDialog() {
        // This will be called from the browser activity
        // The actual dialog showing is handled by the activity
        Log.d(TAG, "Need child selection")
    }

    // ==================== Query Methods ====================

    /**
     * Check if device is paired with Allow2.
     */
    fun isPaired(): Boolean = service.isPaired()

    /**
     * Check if Allow2 is enabled.
     */
    fun isEnabled(): Boolean = service.isEnabled()

    /**
     * Check if child selection is needed.
     */
    fun needsChildSelection(): Boolean = service.needsChildSelection()

    /**
     * Get the current child's name.
     */
    fun getCurrentChildName(): String? = service.getCurrentChild()?.name

    /**
     * Get the remaining time in seconds.
     */
    fun getRemainingSeconds(): Int = service.checkResult.value?.getRemainingSeconds() ?: -1

    /**
     * Get the service instance for direct access.
     */
    fun getService(): Allow2Service = service

    companion object {
        private const val TAG = "Allow2NativeWorker"

        @Volatile
        private var instance: Allow2NativeWorker? = null

        /**
         * Get the singleton instance.
         */
        @JvmStatic
        fun getInstance(): Allow2NativeWorker {
            return instance ?: synchronized(this) {
                instance ?: Allow2NativeWorker(
                    ContextUtils.getApplicationContext()
                ).also { instance = it }
            }
        }

        /**
         * Check if Allow2 integration is available and paired.
         */
        @JvmStatic
        fun isAvailable(): Boolean {
            return instance?.isPaired() == true
        }
    }
}

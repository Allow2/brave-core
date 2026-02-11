/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2

import android.app.Activity
import android.content.Intent
import android.util.Log
import androidx.fragment.app.FragmentActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.allow2.models.Allow2Child
import org.chromium.chrome.browser.allow2.services.Allow2Service
import org.chromium.chrome.browser.allow2.services.Allow2UsageTracker
import org.chromium.chrome.browser.allow2.ui.Allow2BlockOverlayActivity
import org.chromium.chrome.browser.allow2.ui.Allow2ChildSelectDialog
import org.chromium.chrome.browser.allow2.ui.Allow2PairingActivity
import org.chromium.chrome.browser.allow2.ui.Allow2WarningBanner
import java.lang.ref.WeakReference

/**
 * Browser integration class for Allow2 parental controls.
 * This class integrates Allow2 with Brave's browser UI and navigation system.
 *
 * Usage:
 * 1. Call [initialize] in the main browser activity's onCreate
 * 2. Call [onResume] and [onPause] in the activity lifecycle
 * 3. Call [shouldAllowNavigation] before navigating to a URL
 * 4. Call [onNavigationComplete] after navigation completes
 */
class Allow2BrowserIntegration private constructor() :
    Allow2Service.Allow2ServiceListener {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private var activityRef: WeakReference<FragmentActivity>? = null
    private var warningBanner: Allow2WarningBanner? = null

    private val nativeWorker: Allow2NativeWorker by lazy {
        Allow2NativeWorker.getInstance()
    }

    private val service: Allow2Service by lazy {
        nativeWorker.getService()
    }

    private var isInitialized = false
    private var pendingChildSelection = false

    // ==================== Initialization ====================

    /**
     * Initialize the Allow2 integration with the browser activity.
     * Should be called from the main browser activity's onCreate.
     *
     * @param activity The browser activity
     * @param warningBanner Optional warning banner view for time warnings
     */
    fun initialize(activity: FragmentActivity, warningBanner: Allow2WarningBanner? = null) {
        ThreadUtils.assertOnUiThread()

        if (isInitialized) {
            Log.w(TAG, "Already initialized")
            return
        }

        activityRef = WeakReference(activity)
        this.warningBanner = warningBanner

        // Initialize native worker
        nativeWorker.initialize()

        // Set up service listener
        service.setServiceListener(this)

        // Observe blocked state
        nativeWorker.isBlocked
            .onEach { blocked ->
                if (blocked) {
                    showBlockOverlay()
                }
            }
            .launchIn(scope)

        isInitialized = true

        // Check if child selection is needed on startup
        if (service.needsChildSelection()) {
            pendingChildSelection = true
        }

        Log.i(TAG, "Allow2BrowserIntegration initialized")
    }

    /**
     * Shutdown the integration.
     * Should be called from the browser activity's onDestroy.
     */
    fun shutdown() {
        ThreadUtils.assertOnUiThread()

        service.setServiceListener(null)
        nativeWorker.shutdown()

        activityRef = null
        warningBanner = null
        isInitialized = false

        Log.i(TAG, "Allow2BrowserIntegration shutdown")
    }

    // ==================== Lifecycle ====================

    /**
     * Called when the browser activity resumes.
     */
    fun onResume() {
        if (!isInitialized) return

        nativeWorker.onResume()

        // Show child selection if pending
        if (pendingChildSelection) {
            pendingChildSelection = false
            showChildSelectDialog()
        }
    }

    /**
     * Called when the browser activity pauses.
     */
    fun onPause() {
        if (!isInitialized) return
        nativeWorker.onPause()
    }

    // ==================== Navigation ====================

    /**
     * Check if navigation to the URL should be allowed.
     * Call this before initiating navigation.
     *
     * @param url The URL to navigate to
     * @return true if navigation should proceed, false if blocked
     */
    fun shouldAllowNavigation(url: String): Boolean {
        if (!isInitialized) return true
        return nativeWorker.shouldAllowNavigation(url)
    }

    /**
     * Notify that navigation completed successfully.
     * Call this after navigation completes.
     *
     * @param url The URL that was navigated to
     */
    fun onNavigationComplete(url: String) {
        if (!isInitialized) return
        nativeWorker.onNavigationComplete(url)
    }

    // ==================== Tab Events ====================

    /**
     * Called when a new tab is created.
     */
    fun onTabCreated(tabId: Long) {
        if (!isInitialized) return
        nativeWorker.onTabCreated(tabId)
    }

    /**
     * Called when a tab is closed.
     */
    fun onTabClosed(tabId: Long) {
        if (!isInitialized) return
        nativeWorker.onTabClosed(tabId)
    }

    // ==================== Allow2Service.Allow2ServiceListener ====================

    override fun onNeedChildSelection() {
        val activity = activityRef?.get()
        if (activity != null && !activity.isFinishing && !activity.isDestroyed) {
            showChildSelectDialog()
        } else {
            pendingChildSelection = true
        }
    }

    override fun onTimeWarning(type: Allow2UsageTracker.WarningType, remainingSeconds: Int) {
        warningBanner?.showWarning(type, remainingSeconds)
    }

    override fun onDeviceUnpaired() {
        Log.i(TAG, "Device unpaired")
        // Optionally show a notification or toast
    }

    // ==================== UI Actions ====================

    /**
     * Show the block overlay activity.
     */
    fun showBlockOverlay() {
        val activity = activityRef?.get() ?: return
        if (activity.isFinishing || activity.isDestroyed) return

        val intent = Intent(activity, Allow2BlockOverlayActivity::class.java)
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        activity.startActivity(intent)
    }

    /**
     * Show the child selection dialog.
     */
    fun showChildSelectDialog() {
        val activity = activityRef?.get() ?: return
        if (activity.isFinishing || activity.isDestroyed) return

        val dialog = Allow2ChildSelectDialog.newInstance()
        dialog.setListener(object : Allow2ChildSelectDialog.ChildSelectListener {
            override fun onChildSelected(child: Allow2Child) {
                Log.d(TAG, "Child selected: ${child.name}")
                // Start tracking after child selection
                service.startTracking()
            }
        })
        dialog.show(activity.supportFragmentManager, Allow2ChildSelectDialog.TAG)
    }

    /**
     * Show the pairing activity.
     */
    fun showPairingActivity() {
        val activity = activityRef?.get() ?: return
        if (activity.isFinishing || activity.isDestroyed) return

        val intent = Intent(activity, Allow2PairingActivity::class.java)
        activity.startActivityForResult(intent, REQUEST_CODE_PAIRING)
    }

    /**
     * Handle activity result from pairing or child selection.
     */
    fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        when (requestCode) {
            REQUEST_CODE_PAIRING -> {
                if (resultCode == Activity.RESULT_OK) {
                    Log.d(TAG, "Pairing completed successfully")
                    // Check if child selection is needed
                    if (service.needsChildSelection()) {
                        showChildSelectDialog()
                    }
                }
            }
            REQUEST_CODE_CHILD_SELECT -> {
                if (resultCode == Activity.RESULT_OK) {
                    Log.d(TAG, "Child selection completed")
                }
            }
        }
    }

    // ==================== Query Methods ====================

    /**
     * Check if device is paired with Allow2.
     */
    fun isPaired(): Boolean = nativeWorker.isPaired()

    /**
     * Check if Allow2 is enabled.
     */
    fun isEnabled(): Boolean = nativeWorker.isEnabled()

    /**
     * Check if currently blocked.
     */
    fun isBlocked(): Boolean = nativeWorker.isBlocked.value

    /**
     * Check if child selection is needed.
     */
    fun needsChildSelection(): Boolean = nativeWorker.needsChildSelection()

    /**
     * Get the current child's name.
     */
    fun getCurrentChildName(): String? = nativeWorker.getCurrentChildName()

    /**
     * Get remaining time in seconds (-1 if unlimited).
     */
    fun getRemainingSeconds(): Int = nativeWorker.getRemainingSeconds()

    /**
     * Get the Allow2Service for advanced operations.
     */
    fun getService(): Allow2Service = service

    companion object {
        private const val TAG = "Allow2BrowserIntegration"

        const val REQUEST_CODE_PAIRING = 1001
        const val REQUEST_CODE_CHILD_SELECT = 1002
        const val REQUEST_CODE_BLOCK_OVERLAY = 1003

        @Volatile
        private var instance: Allow2BrowserIntegration? = null

        /**
         * Get the singleton instance.
         */
        @JvmStatic
        fun getInstance(): Allow2BrowserIntegration {
            return instance ?: synchronized(this) {
                instance ?: Allow2BrowserIntegration().also { instance = it }
            }
        }

        /**
         * Check if Allow2 is available and paired.
         */
        @JvmStatic
        fun isAvailable(): Boolean {
            return Allow2NativeWorker.isAvailable()
        }
    }
}

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2;

import android.content.Context;
import android.content.Intent;

import androidx.annotation.Nullable;

import org.chromium.base.ContextUtils;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.chrome.browser.allow2.models.Allow2Child;
import org.chromium.chrome.browser.allow2.services.Allow2CredentialManager;
import org.chromium.chrome.browser.allow2.services.Allow2Service;
import org.chromium.chrome.browser.allow2.services.Allow2UsageTracker;
import org.chromium.chrome.browser.allow2.ui.Allow2BlockOverlayActivity;
import org.chromium.chrome.browser.allow2.ui.Allow2ChildSelectDialog;

import java.util.List;

/**
 * Java-side JNI bridge for Allow2 parental controls integration.
 * This class bridges between native C++ code and the Kotlin Allow2Service.
 *
 * Following the BraveSyncWorker pattern for JNI integration.
 */
@JNINamespace("allow2::android")
public class Allow2Bridge implements Allow2Service.Allow2ServiceListener {

    private static Allow2Bridge sInstance;

    private long mNativePtr;
    private final Context mContext;
    private final Allow2Service mService;
    private final Allow2CredentialManager mCredentialManager;

    private Allow2BridgeObserver mObserver;

    /**
     * Interface for observing Allow2 state changes from native code.
     */
    public interface Allow2BridgeObserver {
        void onBlockedStateChanged(boolean isBlocked);
        void onWarning(int warningLevel, int remainingSeconds);
        void onUnpaired();
        void onNeedChildSelection();
    }

    /**
     * Get the singleton instance, creating if necessary.
     */
    public static Allow2Bridge getInstance() {
        if (sInstance == null) {
            sInstance = new Allow2Bridge();
        }
        return sInstance;
    }

    /**
     * Check if Allow2 is available (device is paired).
     */
    public static boolean isAvailable() {
        if (sInstance == null) {
            return false;
        }
        return sInstance.isPaired();
    }

    private Allow2Bridge() {
        mContext = ContextUtils.getApplicationContext();
        mService = Allow2Service.getInstance(mContext);
        mCredentialManager = Allow2CredentialManager.getInstance(mContext);

        // Register as listener for service events
        mService.setServiceListener(this);

        // Initialize native bridge
        Allow2BridgeJni.get().init(this);
    }

    /**
     * Called from native to set the native pointer.
     */
    @CalledByNative
    private void setNativePtr(long nativePtr) {
        mNativePtr = nativePtr;
    }

    /**
     * Set the observer for bridge events.
     */
    public void setObserver(@Nullable Allow2BridgeObserver observer) {
        mObserver = observer;
    }

    /**
     * Destroy the native bridge.
     */
    public void destroy() {
        if (mNativePtr != 0) {
            Allow2BridgeJni.get().destroy(mNativePtr);
            mNativePtr = 0;
        }
        mService.setServiceListener(null);
        sInstance = null;
    }

    // ==================== Pairing State (Called from Native) ====================

    @CalledByNative
    public boolean isPaired() {
        return mService.isPaired();
    }

    @CalledByNative
    public boolean isEnabled() {
        return mService.isEnabled();
    }

    @CalledByNative
    public boolean needsChildSelection() {
        return mService.needsChildSelection();
    }

    // ==================== Navigation Callbacks (Called from Native) ====================

    @CalledByNative
    public void onNavigate(String url) {
        mService.onNavigate(url);
    }

    @CalledByNative
    public void onNavigationComplete(String url, int activityType) {
        // Future: Could track specific activity types separately
        mService.onNavigate(url);
    }

    // ==================== Lifecycle Callbacks (Called from Native) ====================

    @CalledByNative
    public void onBrowserForeground() {
        mService.onResume();
    }

    @CalledByNative
    public void onBrowserBackground() {
        mService.onPause();
    }

    // ==================== Settings (Called from Native) ====================

    @CalledByNative
    @Nullable
    public String getCurrentChildName() {
        Allow2Child child = mService.getCurrentChild();
        return child != null ? child.getName() : null;
    }

    @CalledByNative
    public int getChildCount() {
        List<Allow2Child> children = mService.getChildren();
        return children != null ? children.size() : 0;
    }

    // ==================== Service Listener Implementation ====================

    @Override
    public void onNeedChildSelection() {
        if (mObserver != null) {
            mObserver.onNeedChildSelection();
        }
        if (mNativePtr != 0) {
            Allow2BridgeJni.get().notifyNeedChildSelection(mNativePtr);
        }
    }

    @Override
    public void onTimeWarning(Allow2UsageTracker.WarningType type, int remainingSeconds) {
        int warningLevel = convertWarningType(type);
        if (mObserver != null) {
            mObserver.onWarning(warningLevel, remainingSeconds);
        }
        if (mNativePtr != 0) {
            Allow2BridgeJni.get().notifyWarning(mNativePtr, warningLevel, remainingSeconds);
        }
    }

    @Override
    public void onDeviceUnpaired() {
        if (mObserver != null) {
            mObserver.onUnpaired();
        }
        if (mNativePtr != 0) {
            Allow2BridgeJni.get().notifyUnpaired(mNativePtr);
        }
    }

    // ==================== Public Methods for UI ====================

    /**
     * Check if navigation to a URL should be allowed.
     * Returns true if navigation should proceed, false if blocked.
     */
    public boolean shouldAllowNavigation(String url) {
        if (!isPaired() || !isEnabled()) {
            return true;
        }

        // Check current blocked state from service
        Boolean blocked = mService.getIsBlocked().getValue();
        if (blocked != null && blocked) {
            return false;
        }

        return true;
    }

    /**
     * Show the child selection dialog.
     */
    public void showChildSelectDialog(androidx.fragment.app.FragmentManager fragmentManager) {
        Allow2ChildSelectDialog dialog = Allow2ChildSelectDialog.Companion.newInstance();
        dialog.show(fragmentManager, Allow2ChildSelectDialog.TAG);
    }

    /**
     * Show the block overlay activity.
     */
    public void showBlockOverlay() {
        Intent intent = new Intent(mContext, Allow2BlockOverlayActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        mContext.startActivity(intent);
    }

    /**
     * Get the Allow2Service instance.
     */
    public Allow2Service getService() {
        return mService;
    }

    // ==================== Internal Helpers ====================

    /**
     * Notify native about blocked state change.
     */
    public void notifyBlockedStateChanged(boolean isBlocked) {
        if (mObserver != null) {
            mObserver.onBlockedStateChanged(isBlocked);
        }
        if (mNativePtr != 0) {
            Allow2BridgeJni.get().notifyBlockedStateChanged(mNativePtr, isBlocked);
        }
    }

    private int convertWarningType(Allow2UsageTracker.WarningType type) {
        switch (type) {
            case LOW:
                return 1;
            case MEDIUM:
                return 2;
            case HIGH:
                return 3;
            case CRITICAL:
                return 4;
            default:
                return 0;
        }
    }

    // ==================== JNI Interface ====================

    @NativeMethods
    interface Natives {
        void init(Allow2Bridge caller);
        void destroy(long nativeAllow2Bridge);
        void notifyBlockedStateChanged(long nativeAllow2Bridge, boolean isBlocked);
        void notifyWarning(long nativeAllow2Bridge, int warningLevel, int remainingSeconds);
        void notifyUnpaired(long nativeAllow2Bridge);
        void notifyNeedChildSelection(long nativeAllow2Bridge);
        boolean isAvailable();
    }
}

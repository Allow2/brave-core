/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.util

import android.content.Context
import android.os.Build
import android.provider.Settings
import org.chromium.chrome.browser.allow2.Allow2Constants
import java.util.UUID

/**
 * Utility class for device information used in pairing.
 */
object Allow2DeviceInfo {

    private var cachedDeviceId: String? = null

    /**
     * Get or generate a unique device identifier.
     * Uses Android ID if available, otherwise generates a UUID.
     *
     * @param context Application context
     * @return Unique device identifier
     */
    fun getDeviceId(context: Context): String {
        cachedDeviceId?.let { return it }

        val androidId = Settings.Secure.getString(
            context.contentResolver,
            Settings.Secure.ANDROID_ID
        )

        val deviceId = if (!androidId.isNullOrBlank()) {
            // Prefix with constant to identify Brave devices
            "${Allow2Constants.DEVICE_TOKEN_PREFIX}${androidId}"
        } else {
            // Fallback to UUID stored in preferences
            val prefs = context.getSharedPreferences(
                Allow2Constants.PREF_FILE_NAME,
                Context.MODE_PRIVATE
            )
            val storedUuid = prefs.getString("device_uuid", null)
            if (storedUuid != null) {
                storedUuid
            } else {
                val newUuid = "${Allow2Constants.DEVICE_TOKEN_PREFIX}${UUID.randomUUID()}"
                prefs.edit().putString("device_uuid", newUuid).apply()
                newUuid
            }
        }

        cachedDeviceId = deviceId
        return deviceId
    }

    /**
     * Get a human-readable device name.
     *
     * @param context Application context
     * @return Device name string
     */
    fun getDeviceName(context: Context): String {
        val manufacturer = Build.MANUFACTURER.replaceFirstChar {
            if (it.isLowerCase()) it.titlecase() else it.toString()
        }
        val model = Build.MODEL

        // If model already contains manufacturer, just return model
        return if (model.lowercase().startsWith(manufacturer.lowercase())) {
            "$model - Brave Browser"
        } else {
            "$manufacturer $model - Brave Browser"
        }
    }

    /**
     * Get the current timezone identifier.
     *
     * @return Timezone ID (e.g., "America/New_York")
     */
    fun getTimezone(): String {
        return java.util.TimeZone.getDefault().id
    }

    /**
     * Get the platform name for API requests.
     *
     * @return Platform string
     */
    fun getPlatform(): String {
        return Allow2Constants.PLATFORM
    }

    /**
     * Get Android SDK version.
     *
     * @return SDK version integer
     */
    fun getSdkVersion(): Int {
        return Build.VERSION.SDK_INT
    }

    /**
     * Get Android version name.
     *
     * @return Version name string (e.g., "13")
     */
    fun getVersionName(): String {
        return Build.VERSION.RELEASE
    }

    /**
     * Check if device supports biometric authentication.
     *
     * @param context Application context
     * @return true if biometrics are available
     */
    fun hasBiometricSupport(context: Context): Boolean {
        return try {
            val biometricManager = androidx.biometric.BiometricManager.from(context)
            biometricManager.canAuthenticate(
                androidx.biometric.BiometricManager.Authenticators.BIOMETRIC_WEAK
            ) == androidx.biometric.BiometricManager.BIOMETRIC_SUCCESS
        } catch (e: Exception) {
            false
        }
    }
}

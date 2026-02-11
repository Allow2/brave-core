/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.services

import android.content.Context
import android.content.SharedPreferences
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import org.chromium.chrome.browser.allow2.Allow2Constants
import org.chromium.chrome.browser.allow2.models.Allow2Child
import org.chromium.chrome.browser.allow2.models.Allow2CheckResult
import org.chromium.chrome.browser.allow2.models.Allow2Credentials
import org.json.JSONArray
import org.json.JSONObject

/**
 * Manages secure storage of Allow2 credentials and preferences.
 * Uses EncryptedSharedPreferences for sensitive data (credentials)
 * and regular SharedPreferences for non-sensitive data (children list, settings).
 */
class Allow2CredentialManager(private val context: Context) {

    private val regularPrefs: SharedPreferences by lazy {
        context.getSharedPreferences(
            Allow2Constants.PREF_FILE_NAME,
            Context.MODE_PRIVATE
        )
    }

    private val securePrefs: SharedPreferences by lazy {
        try {
            val masterKey = MasterKey.Builder(context)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()

            EncryptedSharedPreferences.create(
                context,
                Allow2Constants.SECURE_PREF_FILE_NAME,
                masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
            )
        } catch (e: Exception) {
            // Fallback to regular prefs if encryption fails (should not happen on modern devices)
            android.util.Log.e(TAG, "Failed to create encrypted prefs, using regular prefs", e)
            regularPrefs
        }
    }

    // ==================== Credentials Management ====================

    /**
     * Save Allow2 credentials securely.
     */
    fun saveCredentials(credentials: Allow2Credentials) {
        securePrefs.edit().apply {
            putString(Allow2Constants.SECURE_PREF_USER_ID, credentials.userId)
            putString(Allow2Constants.SECURE_PREF_PAIR_ID, credentials.pairId)
            putString(Allow2Constants.SECURE_PREF_PAIR_TOKEN, credentials.pairToken)
            apply()
        }
    }

    /**
     * Retrieve stored credentials.
     */
    fun getCredentials(): Allow2Credentials {
        val userId = securePrefs.getString(Allow2Constants.SECURE_PREF_USER_ID, "") ?: ""
        val pairId = securePrefs.getString(Allow2Constants.SECURE_PREF_PAIR_ID, "") ?: ""
        val pairToken = securePrefs.getString(Allow2Constants.SECURE_PREF_PAIR_TOKEN, "") ?: ""

        return Allow2Credentials(userId, pairId, pairToken)
    }

    /**
     * Check if valid credentials are stored.
     */
    fun hasCredentials(): Boolean {
        return getCredentials().isValid()
    }

    /**
     * Clear all credentials (called on 401 response or manual unpair).
     */
    fun clearCredentials() {
        securePrefs.edit().apply {
            remove(Allow2Constants.SECURE_PREF_USER_ID)
            remove(Allow2Constants.SECURE_PREF_PAIR_ID)
            remove(Allow2Constants.SECURE_PREF_PAIR_TOKEN)
            apply()
        }
    }

    // ==================== Children Management ====================

    /**
     * Save the list of children.
     */
    fun saveChildren(children: List<Allow2Child>) {
        val jsonArray = Allow2Child.toJsonArray(children)
        regularPrefs.edit()
            .putString(Allow2Constants.PREF_CHILDREN, jsonArray.toString())
            .apply()
    }

    /**
     * Retrieve the list of children.
     */
    fun getChildren(): List<Allow2Child> {
        val jsonString = regularPrefs.getString(Allow2Constants.PREF_CHILDREN, null)
            ?: return emptyList()

        return try {
            val jsonArray = JSONArray(jsonString)
            Allow2Child.fromJsonArray(jsonArray)
        } catch (e: Exception) {
            android.util.Log.e(TAG, "Failed to parse children", e)
            emptyList()
        }
    }

    /**
     * Get a specific child by ID.
     */
    fun getChild(childId: Long): Allow2Child? {
        return getChildren().find { it.id == childId }
    }

    /**
     * Clear the children list.
     */
    fun clearChildren() {
        regularPrefs.edit()
            .remove(Allow2Constants.PREF_CHILDREN)
            .apply()
    }

    // ==================== Current Child Selection ====================

    /**
     * Save the currently selected child ID.
     */
    fun saveCurrentChildId(childId: Long) {
        regularPrefs.edit()
            .putLong(Allow2Constants.PREF_CHILD_ID, childId)
            .apply()
    }

    /**
     * Get the currently selected child ID.
     */
    fun getCurrentChildId(): Long? {
        val childId = regularPrefs.getLong(Allow2Constants.PREF_CHILD_ID, -1)
        return if (childId >= 0) childId else null
    }

    /**
     * Get the currently selected child.
     */
    fun getCurrentChild(): Allow2Child? {
        val childId = getCurrentChildId() ?: return null
        return getChild(childId)
    }

    /**
     * Clear the current child selection.
     */
    fun clearCurrentChild() {
        regularPrefs.edit()
            .remove(Allow2Constants.PREF_CHILD_ID)
            .apply()
    }

    // ==================== Settings ====================

    /**
     * Check if Allow2 is enabled.
     */
    fun isEnabled(): Boolean {
        return regularPrefs.getBoolean(Allow2Constants.PREF_ENABLED, true)
    }

    /**
     * Set Allow2 enabled state.
     */
    fun setEnabled(enabled: Boolean) {
        regularPrefs.edit()
            .putBoolean(Allow2Constants.PREF_ENABLED, enabled)
            .apply()
    }

    /**
     * Check if device is in shared mode (requires child selection on launch).
     */
    fun isSharedDeviceMode(): Boolean {
        return regularPrefs.getBoolean(Allow2Constants.PREF_SHARED_DEVICE_MODE, true)
    }

    /**
     * Set shared device mode.
     */
    fun setSharedDeviceMode(shared: Boolean) {
        regularPrefs.edit()
            .putBoolean(Allow2Constants.PREF_SHARED_DEVICE_MODE, shared)
            .apply()
    }

    // ==================== Check Result Caching ====================

    /**
     * Save the last check result for caching.
     */
    fun saveLastCheckResult(result: Allow2CheckResult) {
        regularPrefs.edit().apply {
            putLong(Allow2Constants.PREF_LAST_CHECK_TIME, System.currentTimeMillis())
            putString(Allow2Constants.PREF_CACHED_RESULT, result.toJson().toString())
            apply()
        }
    }

    /**
     * Get the cached check result.
     */
    fun getCachedCheckResult(): Allow2CheckResult? {
        val jsonString = regularPrefs.getString(Allow2Constants.PREF_CACHED_RESULT, null)
            ?: return null

        return try {
            val json = JSONObject(jsonString)
            val result = Allow2CheckResult.fromJson(json)

            // Check if result is expired
            if (result.isExpired()) {
                clearCachedCheckResult()
                null
            } else {
                result
            }
        } catch (e: Exception) {
            android.util.Log.e(TAG, "Failed to parse cached result", e)
            null
        }
    }

    /**
     * Get the last check time.
     */
    fun getLastCheckTime(): Long {
        return regularPrefs.getLong(Allow2Constants.PREF_LAST_CHECK_TIME, 0)
    }

    /**
     * Clear the cached check result.
     */
    fun clearCachedCheckResult() {
        regularPrefs.edit().apply {
            remove(Allow2Constants.PREF_LAST_CHECK_TIME)
            remove(Allow2Constants.PREF_CACHED_RESULT)
            apply()
        }
    }

    // ==================== Complete Reset ====================

    /**
     * Clear all Allow2 data (used when device is unpaired remotely).
     */
    fun clearAll() {
        clearCredentials()
        clearChildren()
        clearCurrentChild()
        clearCachedCheckResult()

        regularPrefs.edit().apply {
            remove(Allow2Constants.PREF_ENABLED)
            remove(Allow2Constants.PREF_SHARED_DEVICE_MODE)
            apply()
        }
    }

    companion object {
        private const val TAG = "Allow2CredentialManager"

        @Volatile
        private var instance: Allow2CredentialManager? = null

        fun getInstance(context: Context): Allow2CredentialManager {
            return instance ?: synchronized(this) {
                instance ?: Allow2CredentialManager(context.applicationContext).also {
                    instance = it
                }
            }
        }
    }
}

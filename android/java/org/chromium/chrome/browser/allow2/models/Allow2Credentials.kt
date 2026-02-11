/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.models

import org.json.JSONObject

/**
 * Data class representing Allow2 device pairing credentials.
 * These credentials are stored securely using EncryptedSharedPreferences.
 */
data class Allow2Credentials(
    val userId: String,
    val pairId: String,
    val pairToken: String
) {
    /**
     * Check if credentials are valid (non-empty).
     */
    fun isValid(): Boolean {
        return userId.isNotBlank() && pairId.isNotBlank() && pairToken.isNotBlank()
    }

    /**
     * Convert credentials to JSON for API requests.
     */
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("userId", userId)
            put("pairId", pairId)
            put("pairToken", pairToken)
        }
    }

    companion object {
        /**
         * Create credentials from JSON response.
         */
        fun fromJson(json: JSONObject): Allow2Credentials {
            return Allow2Credentials(
                userId = json.optString("userId", ""),
                pairId = json.optString("pairId", ""),
                pairToken = json.optString("pairToken", "")
            )
        }

        /**
         * Create empty/invalid credentials.
         */
        fun empty(): Allow2Credentials {
            return Allow2Credentials("", "", "")
        }
    }
}

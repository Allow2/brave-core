/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.models

import org.json.JSONArray
import org.json.JSONObject

/**
 * Data class representing a child profile from Allow2.
 * PIN hash and salt are used for local verification without sending the PIN to the server.
 */
data class Allow2Child(
    val id: Long,
    val name: String,
    val pinHash: String,
    val pinSalt: String,
    val avatarUrl: String? = null
) {
    /**
     * Check if this child has a PIN set.
     */
    fun hasPIN(): Boolean {
        return pinHash.isNotBlank() && pinSalt.isNotBlank()
    }

    /**
     * Convert to JSON for storage.
     */
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("id", id)
            put("name", name)
            put("pinHash", pinHash)
            put("pinSalt", pinSalt)
            if (avatarUrl != null) {
                put("avatarUrl", avatarUrl)
            }
        }
    }

    companion object {
        /**
         * Create a child from JSON response.
         */
        fun fromJson(json: JSONObject): Allow2Child {
            return Allow2Child(
                id = json.optLong("id", 0),
                name = json.optString("name", ""),
                pinHash = json.optString("pinHash", ""),
                pinSalt = json.optString("pinSalt", ""),
                avatarUrl = json.optString("avatarUrl", null)
            )
        }

        /**
         * Parse a list of children from JSON array.
         */
        fun fromJsonArray(jsonArray: JSONArray): List<Allow2Child> {
            val children = mutableListOf<Allow2Child>()
            for (i in 0 until jsonArray.length()) {
                val childJson = jsonArray.optJSONObject(i)
                if (childJson != null) {
                    children.add(fromJson(childJson))
                }
            }
            return children
        }

        /**
         * Convert a list of children to JSON array for storage.
         */
        fun toJsonArray(children: List<Allow2Child>): JSONArray {
            val jsonArray = JSONArray()
            for (child in children) {
                jsonArray.put(child.toJson())
            }
            return jsonArray
        }

        /**
         * Create a guest child profile.
         */
        fun guest(): Allow2Child {
            return Allow2Child(
                id = -1,
                name = "Guest",
                pinHash = "",
                pinSalt = "",
                avatarUrl = null
            )
        }
    }
}

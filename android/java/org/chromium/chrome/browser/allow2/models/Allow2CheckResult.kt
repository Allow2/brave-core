/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.models

import org.json.JSONObject

/**
 * Data class representing the result of an Allow2 check API call.
 */
data class Allow2CheckResult(
    val allowed: Boolean,
    val activities: Map<Int, ActivityResult>,
    val todayDayType: DayType?,
    val tomorrowDayType: DayType?,
    val expiresAt: Long
) {
    /**
     * Get the primary activity result (Internet).
     */
    fun getPrimaryActivityResult(): ActivityResult? {
        return activities[Allow2Activity.INTERNET.id]
    }

    /**
     * Get the remaining time in seconds for the primary activity.
     */
    fun getRemainingSeconds(): Int {
        return getPrimaryActivityResult()?.remaining ?: 0
    }

    /**
     * Check if the cached result is still valid.
     */
    fun isExpired(): Boolean {
        return System.currentTimeMillis() / 1000 > expiresAt
    }

    /**
     * Get a user-friendly block reason.
     */
    fun getBlockReason(): String {
        val activity = getPrimaryActivityResult()
        return when {
            activity == null -> "Access restricted"
            activity.banned -> "Access has been blocked"
            activity.timeblock != null && !activity.timeblock.allowed ->
                "Not allowed during ${todayDayType?.name ?: "this time"}"
            activity.remaining <= 0 -> "Time limit reached for today"
            else -> "Access restricted"
        }
    }

    /**
     * Convert to JSON for caching.
     */
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("allowed", allowed)
            put("expiresAt", expiresAt)

            val activitiesJson = JSONObject()
            activities.forEach { (id, result) ->
                activitiesJson.put(id.toString(), result.toJson())
            }
            put("activities", activitiesJson)

            todayDayType?.let { put("todayDayType", it.toJson()) }
            tomorrowDayType?.let { put("tomorrowDayType", it.toJson()) }
        }
    }

    companion object {
        /**
         * Parse check result from API response.
         */
        fun fromJson(json: JSONObject): Allow2CheckResult {
            val activitiesJson = json.optJSONObject("activities")
            val activities = mutableMapOf<Int, ActivityResult>()

            if (activitiesJson != null) {
                val keys = activitiesJson.keys()
                while (keys.hasNext()) {
                    val key = keys.next()
                    val activityId = key.toIntOrNull()
                    if (activityId != null) {
                        val activityJson = activitiesJson.optJSONObject(key)
                        if (activityJson != null) {
                            activities[activityId] = ActivityResult.fromJson(activityJson)
                        }
                    }
                }
            }

            val dayTypesJson = json.optJSONObject("dayTypes")
            val todayDayType = dayTypesJson?.optJSONObject("today")?.let { DayType.fromJson(it) }
            val tomorrowDayType = dayTypesJson?.optJSONObject("tomorrow")?.let { DayType.fromJson(it) }

            // Calculate expiry from the activity expires field, or default to 60 seconds
            var expiresAt = System.currentTimeMillis() / 1000 + 60
            activities.values.firstOrNull()?.let { activity ->
                if (activity.expires > 0) {
                    expiresAt = activity.expires
                }
            }

            return Allow2CheckResult(
                allowed = json.optBoolean("allowed", true),
                activities = activities,
                todayDayType = todayDayType,
                tomorrowDayType = tomorrowDayType,
                expiresAt = expiresAt
            )
        }

        /**
         * Create a default "allowed" result.
         */
        fun allowed(): Allow2CheckResult {
            return Allow2CheckResult(
                allowed = true,
                activities = emptyMap(),
                todayDayType = null,
                tomorrowDayType = null,
                expiresAt = System.currentTimeMillis() / 1000 + 60
            )
        }

        /**
         * Create a default "blocked" result.
         */
        fun blocked(reason: String = "Access restricted"): Allow2CheckResult {
            return Allow2CheckResult(
                allowed = false,
                activities = mapOf(
                    Allow2Activity.INTERNET.id to ActivityResult(
                        id = Allow2Activity.INTERNET.id,
                        name = "Internet",
                        remaining = 0,
                        expires = 0,
                        banned = true,
                        timeblock = null
                    )
                ),
                todayDayType = null,
                tomorrowDayType = null,
                expiresAt = System.currentTimeMillis() / 1000 + 60
            )
        }
    }
}

/**
 * Result for a specific activity.
 */
data class ActivityResult(
    val id: Int,
    val name: String,
    val remaining: Int,
    val expires: Long,
    val banned: Boolean,
    val timeblock: TimeBlock?
) {
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("id", id)
            put("name", name)
            put("remaining", remaining)
            put("expires", expires)
            put("banned", banned)
            timeblock?.let { put("timeblock", it.toJson()) }
        }
    }

    companion object {
        fun fromJson(json: JSONObject): ActivityResult {
            val timeblockJson = json.optJSONObject("timeblock")
            return ActivityResult(
                id = json.optInt("id", 0),
                name = json.optString("name", ""),
                remaining = json.optInt("remaining", 0),
                expires = json.optLong("expires", 0),
                banned = json.optBoolean("banned", false),
                timeblock = timeblockJson?.let { TimeBlock.fromJson(it) }
            )
        }
    }
}

/**
 * Time block information (scheduled restrictions).
 */
data class TimeBlock(
    val allowed: Boolean,
    val ends: Long
) {
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("allowed", allowed)
            put("ends", ends)
        }
    }

    companion object {
        fun fromJson(json: JSONObject): TimeBlock {
            return TimeBlock(
                allowed = json.optBoolean("allowed", true),
                ends = json.optLong("ends", 0)
            )
        }
    }
}

/**
 * Day type information (e.g., "School Night", "Weekend").
 */
data class DayType(
    val id: Int,
    val name: String
) {
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("id", id)
            put("name", name)
        }
    }

    companion object {
        fun fromJson(json: JSONObject): DayType {
            return DayType(
                id = json.optInt("id", 0),
                name = json.optString("name", "")
            )
        }
    }
}

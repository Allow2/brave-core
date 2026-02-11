/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2

/**
 * Constants for Allow2 parental controls integration.
 *
 * ENVIRONMENT VARIABLE OVERRIDE:
 * Set ALLOW2_BASE_URL to override BOTH api and service hosts.
 * Example: ALLOW2_BASE_URL=https://staging.allow2.com
 * This is useful for staging/development environments.
 * See ~/ai/allow2/STAGING_ENV.md for full documentation.
 */
object Allow2Constants {
    // Environment variable name for base URL override
    const val BASE_URL_ENV_VAR = "ALLOW2_BASE_URL"

    // Default API Hosts (can be overridden by ALLOW2_BASE_URL)
    const val DEFAULT_API_HOST = "https://api.allow2.com"
    const val DEFAULT_SERVICE_HOST = "https://service.allow2.com"

    // Dynamic hosts - use these in code (resolved at runtime)
    val API_HOST: String
        get() = System.getenv(BASE_URL_ENV_VAR) ?: DEFAULT_API_HOST
    val SERVICE_HOST: String
        get() = System.getenv(BASE_URL_ENV_VAR) ?: DEFAULT_SERVICE_HOST

    // Pairing Endpoints
    const val PAIR_QR_INIT = "/api/pair/qr/init"
    const val PAIR_QR_STATUS = "/api/pair/qr/status"
    const val PAIR_PIN_INIT = "/api/pair/pin/init"
    const val PAIR_PIN_STATUS = "/api/pair/pin/status"

    // Service Endpoints
    const val SERVICE_CHECK = "/serviceapi/check"
    const val REQUEST_CREATE = "/request/createRequest"

    // Activity IDs (from Allow2 API)
    const val ACTIVITY_INTERNET = 1
    const val ACTIVITY_GAMING = 3
    const val ACTIVITY_SCREEN_TIME = 8
    const val ACTIVITY_SOCIAL = 9

    // Timing Constants
    const val CHECK_INTERVAL_MS = 10_000L // 10 seconds
    const val SESSION_TIMEOUT_MS = 5 * 60 * 1000L // 5 minutes
    const val PAIRING_POLL_INTERVAL_MS = 2_000L // 2 seconds
    const val PAIRING_TIMEOUT_MS = 5 * 60 * 1000L // 5 minutes

    // Warning Thresholds (in seconds)
    const val WARNING_15_MIN = 15 * 60
    const val WARNING_5_MIN = 5 * 60
    const val WARNING_1_MIN = 60

    // SharedPreferences Keys (non-sensitive)
    const val PREF_FILE_NAME = "allow2_prefs"
    const val PREF_ENABLED = "allow2.enabled"
    const val PREF_CHILD_ID = "allow2.child_id"
    const val PREF_CHILDREN = "allow2.children"
    const val PREF_SHARED_DEVICE_MODE = "allow2.shared_device"
    const val PREF_LAST_CHECK_TIME = "allow2.last_check"
    const val PREF_CACHED_RESULT = "allow2.cached_result"

    // Encrypted SharedPreferences (sensitive)
    const val SECURE_PREF_FILE_NAME = "allow2_secure_prefs"
    const val SECURE_PREF_USER_ID = "userId"
    const val SECURE_PREF_PAIR_ID = "pairId"
    const val SECURE_PREF_PAIR_TOKEN = "pairToken"

    // Device Info
    const val PLATFORM = "Android"
    const val DEVICE_TOKEN_PREFIX = "BRAVE_ANDROID_"

    // Request Codes
    const val REQUEST_CODE_CHILD_SELECT = 1001
    const val REQUEST_CODE_PAIRING = 1002
    const val REQUEST_CODE_BLOCK_OVERLAY = 1003

    // Intent Extras
    const val EXTRA_BLOCK_REASON = "extra_block_reason"
    const val EXTRA_TIME_REMAINING = "extra_time_remaining"
    const val EXTRA_DAY_TYPE = "extra_day_type"
    const val EXTRA_CHILD_ID = "extra_child_id"
    const val EXTRA_ACTIVITY_ID = "extra_activity_id"

    // Notification Channel
    const val NOTIFICATION_CHANNEL_ID = "allow2_warnings"
    const val NOTIFICATION_CHANNEL_NAME = "Parental Freedom Warnings"
}

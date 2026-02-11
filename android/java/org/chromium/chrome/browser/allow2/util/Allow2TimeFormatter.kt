/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.util

import android.content.Context
import java.util.concurrent.TimeUnit

/**
 * Utility class for formatting time values for display.
 */
object Allow2TimeFormatter {

    /**
     * Format seconds into a human-readable time string.
     *
     * Examples:
     * - 3661 -> "1h 1m"
     * - 125 -> "2m 5s"
     * - 45 -> "45s"
     *
     * @param seconds Number of seconds to format
     * @return Formatted time string
     */
    fun formatDuration(seconds: Int): String {
        if (seconds <= 0) {
            return "0s"
        }

        val hours = TimeUnit.SECONDS.toHours(seconds.toLong())
        val minutes = TimeUnit.SECONDS.toMinutes(seconds.toLong()) % 60
        val secs = seconds % 60

        return buildString {
            if (hours > 0) {
                append("${hours}h")
                if (minutes > 0) {
                    append(" ${minutes}m")
                }
            } else if (minutes > 0) {
                append("${minutes}m")
                if (secs > 0) {
                    append(" ${secs}s")
                }
            } else {
                append("${secs}s")
            }
        }
    }

    /**
     * Format seconds into a countdown display (MM:SS or HH:MM:SS).
     *
     * @param seconds Number of seconds remaining
     * @return Formatted countdown string
     */
    fun formatCountdown(seconds: Int): String {
        if (seconds <= 0) {
            return "00:00"
        }

        val hours = TimeUnit.SECONDS.toHours(seconds.toLong())
        val minutes = TimeUnit.SECONDS.toMinutes(seconds.toLong()) % 60
        val secs = seconds % 60

        return if (hours > 0) {
            String.format("%02d:%02d:%02d", hours, minutes, secs)
        } else {
            String.format("%02d:%02d", minutes, secs)
        }
    }

    /**
     * Format remaining time into a user-friendly message.
     *
     * @param seconds Number of seconds remaining
     * @return Human-readable time remaining message
     */
    fun formatTimeRemaining(seconds: Int): String {
        if (seconds <= 0) {
            return "No time remaining"
        }

        val hours = TimeUnit.SECONDS.toHours(seconds.toLong())
        val minutes = TimeUnit.SECONDS.toMinutes(seconds.toLong()) % 60

        return when {
            hours > 1 -> "About ${hours} hours remaining"
            hours == 1L -> "About 1 hour remaining"
            minutes > 1 -> "${minutes} minutes remaining"
            minutes == 1L -> "1 minute remaining"
            else -> "Less than a minute remaining"
        }
    }

    /**
     * Get the appropriate warning level based on remaining time.
     *
     * @param seconds Number of seconds remaining
     * @return Warning level (NONE, LOW, MEDIUM, HIGH, CRITICAL)
     */
    fun getWarningLevel(seconds: Int): WarningLevel {
        return when {
            seconds <= 60 -> WarningLevel.CRITICAL  // 1 minute or less
            seconds <= 300 -> WarningLevel.HIGH      // 5 minutes or less
            seconds <= 900 -> WarningLevel.MEDIUM    // 15 minutes or less
            seconds <= 1800 -> WarningLevel.LOW      // 30 minutes or less
            else -> WarningLevel.NONE
        }
    }

    /**
     * Format timestamp to a readable time.
     *
     * @param timestamp Unix timestamp in seconds
     * @return Formatted time string (e.g., "3:30 PM")
     */
    fun formatTimestamp(timestamp: Long): String {
        val sdf = java.text.SimpleDateFormat("h:mm a", java.util.Locale.getDefault())
        return sdf.format(java.util.Date(timestamp * 1000))
    }

    /**
     * Warning level enum for time remaining.
     */
    enum class WarningLevel {
        NONE,
        LOW,      // 30 minutes
        MEDIUM,   // 15 minutes
        HIGH,     // 5 minutes
        CRITICAL  // 1 minute or less
    }
}

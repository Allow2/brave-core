/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.ui.compose

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

/**
 * Allow2 theme colors and styling for Compose UI.
 */
object Allow2Colors {
    // Brand colors
    val Primary = Color(0xFF5D4EFF)
    val PrimaryDark = Color(0xFF4338CA)
    val Secondary = Color(0xFF10B981)
    val SecondaryDark = Color(0xFF059669)

    // Status colors
    val Allowed = Color(0xFF10B981)
    val Blocked = Color(0xFFEF4444)
    val Warning = Color(0xFFF59E0B)
    val WarningCritical = Color(0xFFDC2626)

    // Background colors
    val BackgroundLight = Color(0xFFF8FAFC)
    val BackgroundDark = Color(0xFF1E293B)
    val SurfaceLight = Color(0xFFFFFFFF)
    val SurfaceDark = Color(0xFF334155)

    // Text colors
    val TextPrimaryLight = Color(0xFF1E293B)
    val TextPrimaryDark = Color(0xFFF8FAFC)
    val TextSecondaryLight = Color(0xFF64748B)
    val TextSecondaryDark = Color(0xFF94A3B8)
}

private val LightColorScheme = lightColorScheme(
    primary = Allow2Colors.Primary,
    onPrimary = Color.White,
    secondary = Allow2Colors.Secondary,
    onSecondary = Color.White,
    background = Allow2Colors.BackgroundLight,
    onBackground = Allow2Colors.TextPrimaryLight,
    surface = Allow2Colors.SurfaceLight,
    onSurface = Allow2Colors.TextPrimaryLight,
    error = Allow2Colors.Blocked,
    onError = Color.White
)

private val DarkColorScheme = darkColorScheme(
    primary = Allow2Colors.Primary,
    onPrimary = Color.White,
    secondary = Allow2Colors.Secondary,
    onSecondary = Color.White,
    background = Allow2Colors.BackgroundDark,
    onBackground = Allow2Colors.TextPrimaryDark,
    surface = Allow2Colors.SurfaceDark,
    onSurface = Allow2Colors.TextPrimaryDark,
    error = Allow2Colors.Blocked,
    onError = Color.White
)

@Composable
fun Allow2Theme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme

    MaterialTheme(
        colorScheme = colorScheme,
        content = content
    )
}

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.models

import org.chromium.chrome.browser.allow2.Allow2Constants

/**
 * Enum representing Allow2 activity types for usage tracking.
 */
enum class Allow2Activity(val id: Int, val displayName: String) {
    INTERNET(Allow2Constants.ACTIVITY_INTERNET, "Internet"),
    GAMING(Allow2Constants.ACTIVITY_GAMING, "Gaming"),
    SCREEN_TIME(Allow2Constants.ACTIVITY_SCREEN_TIME, "Screen Time"),
    SOCIAL(Allow2Constants.ACTIVITY_SOCIAL, "Social Media");

    companion object {
        /**
         * Get activity type by ID.
         */
        fun fromId(id: Int): Allow2Activity? {
            return values().find { it.id == id }
        }

        /**
         * Detect activity type from URL.
         */
        fun detectFromUrl(url: String): Allow2Activity {
            val lowercaseUrl = url.lowercase()

            // Social media detection
            val socialDomains = listOf(
                "facebook.com", "fb.com",
                "twitter.com", "x.com",
                "instagram.com",
                "tiktok.com",
                "snapchat.com",
                "reddit.com",
                "discord.com",
                "whatsapp.com",
                "telegram.org",
                "linkedin.com",
                "pinterest.com",
                "tumblr.com",
                "twitch.tv"
            )
            if (socialDomains.any { lowercaseUrl.contains(it) }) {
                return SOCIAL
            }

            // Gaming detection
            val gamingDomains = listOf(
                "steam", "steampowered.com",
                "epicgames.com",
                "roblox.com",
                "minecraft.net",
                "xbox.com",
                "playstation.com",
                "nintendo.com",
                "ea.com",
                "ubisoft.com",
                "blizzard.com",
                "battle.net",
                "leagueoflegends.com",
                "fortnite.com",
                "itch.io",
                "gog.com",
                "kongregate.com",
                "miniclip.com",
                "poki.com",
                "crazygames.com"
            )
            if (gamingDomains.any { lowercaseUrl.contains(it) }) {
                return GAMING
            }

            // Default to Internet
            return INTERNET
        }
    }
}

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_COMMON_ALLOW2_CONSTANTS_H_
#define BRAVE_COMPONENTS_ALLOW2_COMMON_ALLOW2_CONSTANTS_H_

#include <cstdint>

namespace allow2 {

// ============================================================================
// API Hosts
// ============================================================================
//
// Allow2 uses two API hosts for different purposes:
// - api.allow2.com: Device pairing, authentication, general API
// - service.allow2.com: Usage logging, permission checks (serviceapi)
//
// The serviceapi endpoints use service.allow2.com directly for performance.
// All other endpoints use api.allow2.com.
//
// ENVIRONMENT VARIABLE OVERRIDE:
// Set ALLOW2_BASE_URL to override BOTH api and service hosts.
// Example: ALLOW2_BASE_URL=https://staging.allow2.com
// This is useful for staging/development environments.
// See ~/ai/allow2/STAGING_ENV.md for full documentation.

// Environment variable name for base URL override
inline constexpr char kAllow2BaseUrlEnvVar[] = "ALLOW2_BASE_URL";

// Default API host for pairing, authentication, and general endpoints
inline constexpr char kAllow2DefaultApiHost[] = "api.allow2.com";
inline constexpr char kAllow2DefaultApiBaseUrl[] = "https://api.allow2.com";

// Default service host for usage checks and logging (direct, not proxied)
inline constexpr char kAllow2DefaultServiceHost[] = "service.allow2.com";
inline constexpr char kAllow2DefaultServiceBaseUrl[] = "https://service.allow2.com";

// Legacy aliases for compatibility (use GetAllow2ApiBaseUrl() instead)
inline constexpr char kAllow2ApiHost[] = "api.allow2.com";
inline constexpr char kAllow2ApiBaseUrl[] = "https://api.allow2.com";
inline constexpr char kAllow2ServiceHost[] = "service.allow2.com";
inline constexpr char kAllow2ServiceBaseUrl[] = "https://service.allow2.com";

// ============================================================================
// API Endpoints
// ============================================================================

// Pairing endpoint - pairs device with parent account
// Host: api.allow2.com
// POST /api/pairDevice
// Body: {"user": "email", "pass": "password", "deviceToken": "...", "name": "..."}
inline constexpr char kAllow2PairDeviceEndpoint[] = "/api/pairDevice";

// Check endpoint - checks allowances and logs activity
// Host: service.allow2.com (use kAllow2ServiceBaseUrl)
// POST /serviceapi/check
// Body: {"userId": "...", "pairId": "...", "childId": "...",
//        "activities": [...], "tz": "Australia/Sydney"}
inline constexpr char kAllow2CheckEndpoint[] = "/serviceapi/check";

// Request more time endpoint
// Host: api.allow2.com
// POST /request/createRequest
inline constexpr char kAllow2RequestTimeEndpoint[] = "/request/createRequest";

// ============================================================================
// Activity IDs (from Allow2 API)
// ============================================================================

// Activity types for tracking and blocking
enum class ActivityId : uint32_t {
  kInternet = 1,       // General internet browsing
  kGaming = 3,         // Gaming-related sites
  kScreenTime = 8,     // Total screen time
  kSocial = 9,         // Social media sites
  kEducation = 10,     // Educational content
  kEntertainment = 11, // Entertainment sites
};

// ============================================================================
// Check Interval
// ============================================================================

// Interval between usage checks in seconds (10 seconds as per design).
inline constexpr int kAllow2CheckIntervalSeconds = 10;

// Grace period after app resume before showing child selection (5 minutes).
inline constexpr int kAllow2ResumeGracePeriodSeconds = 300;

// ============================================================================
// Warning Thresholds (in seconds)
// ============================================================================

inline constexpr int kWarningThreshold15Min = 900;   // 15 minutes
inline constexpr int kWarningThreshold5Min = 300;    // 5 minutes
inline constexpr int kWarningThreshold1Min = 60;     // 1 minute
inline constexpr int kWarningThreshold30Sec = 30;    // 30 seconds
inline constexpr int kWarningThreshold10Sec = 10;    // 10 seconds

// ============================================================================
// API Request Keys
// ============================================================================

// Pairing request/response keys
inline constexpr char kPairUserKey[] = "user";
inline constexpr char kPairPasswordKey[] = "pass";
inline constexpr char kPairDeviceTokenKey[] = "deviceToken";
inline constexpr char kPairDeviceNameKey[] = "name";
inline constexpr char kPairStatusKey[] = "status";
inline constexpr char kPairIdKey[] = "pairId";
inline constexpr char kPairUserIdKey[] = "userId";
inline constexpr char kPairChildrenKey[] = "children";

// Check request/response keys
inline constexpr char kCheckUserIdKey[] = "userId";
inline constexpr char kCheckPairIdKey[] = "pairId";
inline constexpr char kCheckChildIdKey[] = "childId";
inline constexpr char kCheckActivitiesKey[] = "activities";
inline constexpr char kCheckTimezoneKey[] = "tz";
inline constexpr char kCheckAllowedKey[] = "allowed";
inline constexpr char kCheckRemainingKey[] = "remaining";
inline constexpr char kCheckExpiresKey[] = "expires";
inline constexpr char kCheckBannedKey[] = "banned";
inline constexpr char kCheckTimeblockKey[] = "timeblock";
inline constexpr char kCheckDayTypesKey[] = "dayTypes";

// Child object keys
inline constexpr char kChildIdKey[] = "id";
inline constexpr char kChildNameKey[] = "name";
inline constexpr char kChildPinHashKey[] = "pinHash";
inline constexpr char kChildPinSaltKey[] = "pinSalt";

// Activity object keys
inline constexpr char kActivityIdKey[] = "id";
inline constexpr char kActivityLogKey[] = "log";
inline constexpr char kActivityNameKey[] = "name";

// ============================================================================
// API Response Status Values
// ============================================================================

inline constexpr char kStatusSuccess[] = "success";
inline constexpr char kStatusError[] = "error";

// ============================================================================
// Device Token Generation
// ============================================================================

// Prefix for generated device tokens
inline constexpr char kDeviceTokenPrefix[] = "BRAVE_DESKTOP_";

// ============================================================================
// Domain Categories (for enhanced activity tracking)
// ============================================================================

// Well-known social media domains
inline constexpr const char* kSocialDomains[] = {
    "facebook.com",
    "instagram.com",
    "tiktok.com",
    "twitter.com",
    "x.com",
    "snapchat.com",
    "pinterest.com",
    "reddit.com",
    "tumblr.com",
    "linkedin.com",
};

// Well-known gaming domains
inline constexpr const char* kGamingDomains[] = {
    "roblox.com",
    "minecraft.net",
    "fortnite.com",
    "epicgames.com",
    "steam.com",
    "steampowered.com",
    "twitch.tv",
    "discord.com",
};

// Well-known education domains
inline constexpr const char* kEducationDomains[] = {
    "khanacademy.org",
    "coursera.org",
    "edx.org",
    "udemy.com",
    "duolingo.com",
    "quizlet.com",
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_COMMON_ALLOW2_CONSTANTS_H_

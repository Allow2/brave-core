/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_COMMON_PREF_NAMES_H_
#define BRAVE_COMPONENTS_ALLOW2_COMMON_PREF_NAMES_H_

#include "build/build_config.h"

namespace allow2 {
namespace prefs {

// ============================================================================
// Non-sensitive settings (stored in profile prefs)
// ============================================================================

// Whether Allow2 parental controls are enabled for this profile.
inline constexpr char kAllow2Enabled[] = "brave.allow2.enabled";

// The child ID if device is locked to a specific child.
// Empty string means shared device mode (requires child selection on launch).
inline constexpr char kAllow2ChildId[] = "brave.allow2.child_id";

// Cached list of children as JSON array.
// Structure: [{"id": 1001, "name": "Emma", "pinHash": "sha256:...", "pinSalt": "..."}]
inline constexpr char kAllow2CachedChildren[] = "brave.allow2.children";

// Timestamp of last successful check with Allow2 API.
inline constexpr char kAllow2LastCheckTime[] = "brave.allow2.last_check";

// Cached check result as JSON for offline operation.
inline constexpr char kAllow2CachedCheckResult[] =
    "brave.allow2.cached_check_result";

// Expiry timestamp of the cached check result.
inline constexpr char kAllow2CachedCheckExpiry[] =
    "brave.allow2.cached_check_expiry";

// Whether the device is currently in a blocked state.
inline constexpr char kAllow2Blocked[] = "brave.allow2.blocked";

// The day type info (e.g., "School Night", "Weekend").
inline constexpr char kAllow2DayTypeToday[] = "brave.allow2.day_type_today";

// Remaining time in seconds for internet activity (from last check).
inline constexpr char kAllow2RemainingSeconds[] =
    "brave.allow2.remaining_seconds";

// ============================================================================
// Sensitive credentials (encrypted via OSCrypt, stored in local_state)
// These MUST be stored in local_state, not profile prefs, for security.
// ============================================================================

// Encrypted credentials JSON containing userId, pairId, and pairToken.
// Structure (before encryption): {"userId": "...", "pairId": "...", "pairToken": "..."}
// OSCrypt encrypts using:
// - macOS: Keychain
// - Windows: DPAPI
// - Linux: libsecret/GNOME Keyring/KWallet
inline constexpr char kAllow2Credentials[] = "brave.allow2.credentials";

// Device token used for pairing (generated on first pair attempt).
inline constexpr char kAllow2DeviceToken[] = "brave.allow2.device_token";

// Device name registered with Allow2 service.
inline constexpr char kAllow2DeviceName[] = "brave.allow2.device_name";

// Timestamp when device was paired.
inline constexpr char kAllow2PairedAt[] = "brave.allow2.paired_at";

// Name of the account owner (controller account name).
// Displayed in child selection dialog instead of "Guest".
inline constexpr char kAllow2AccountOwnerName[] = "brave.allow2.account_owner_name";

// Owner/parent PIN hash for authentication on shared devices.
// Parents must also authenticate before browsing.
inline constexpr char kAllow2OwnerPinHash[] = "brave.allow2.owner_pin_hash";

// Salt for owner PIN hash.
inline constexpr char kAllow2OwnerPinSalt[] = "brave.allow2.owner_pin_salt";

// ============================================================================
// Offline cache (stored in local_state for persistence)
// ============================================================================

// Complete offline cache JSON containing days, activities, time blocks.
// Updated from server check responses for offline operation.
inline constexpr char kAllow2OfflineCache[] = "brave.allow2.offline_cache";

// Local usage tracking for offline deficit detection.
// Format: JSON object mapping "YYYY-MM-DD-activityId" -> minutes used.
inline constexpr char kAllow2LocalUsage[] = "brave.allow2.local_usage";

// Local extensions pending sync (from voice codes or QR scans).
// Format: JSON array of extension objects.
inline constexpr char kAllow2LocalExtensions[] = "brave.allow2.local_extensions";

// Deficit pool tracking borrowed time that needs to be repaid.
// Format: JSON object mapping child_id -> deficit_seconds.
inline constexpr char kAllow2DeficitPool[] = "brave.allow2.deficit_pool";

// Home timezone for travel mode (timezone of parent's Allow2 account).
inline constexpr char kAllow2HomeTimezone[] = "brave.allow2.home_timezone";

// Timestamp of last local decision made while offline.
inline constexpr char kAllow2LastLocalDecision[] =
    "brave.allow2.last_local_decision";

// Time windows cache for offline time block enforcement.
// Format: JSON object mapping child_id -> array of time windows.
inline constexpr char kAllow2TimeWindows[] = "brave.allow2.time_windows";

}  // namespace prefs
}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_COMMON_PREF_NAMES_H_

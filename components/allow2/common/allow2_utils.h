/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_COMMON_ALLOW2_UTILS_H_
#define BRAVE_COMPONENTS_ALLOW2_COMMON_ALLOW2_UTILS_H_

#include <string>

#include "brave/components/allow2/common/allow2_constants.h"

class PrefService;
class PrefRegistrySimple;

namespace allow2 {

// Register Allow2 preferences for profile prefs.
void RegisterProfilePrefs(PrefRegistrySimple* registry);

// Register Allow2 preferences for local state (sensitive credentials).
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// Check if Allow2 is paired (has valid credentials).
bool IsPaired(PrefService* local_state);

// Check if Allow2 is enabled for the current profile.
bool IsEnabled(PrefService* profile_prefs);

// Check if the device is in shared mode (no specific child locked).
bool IsSharedDeviceMode(PrefService* profile_prefs);

// Get the current child ID, or empty string if shared device.
std::string GetCurrentChildId(PrefService* profile_prefs);

// Generate a unique device token for pairing.
std::string GenerateDeviceToken();

// Categorize a URL domain into an activity type.
ActivityId CategorizeUrl(const std::string& url);

// Validate a PIN against a stored hash using constant-time comparison.
// Returns true if the PIN matches the hash.
bool ValidatePinHash(const std::string& entered_pin,
                     const std::string& stored_hash,
                     const std::string& salt);

// Hash a PIN using SHA-256 with the given salt.
std::string HashPin(const std::string& pin, const std::string& salt);

// Get the current timezone string (e.g., "Australia/Sydney").
std::string GetCurrentTimezone();

// ============================================================================
// Base URL Functions (support ALLOW2_BASE_URL environment variable override)
// ============================================================================

// Get the API base URL. Returns ALLOW2_BASE_URL env var if set,
// otherwise returns the default https://api.allow2.com
std::string GetAllow2ApiBaseUrl();

// Get the Service base URL. Returns ALLOW2_BASE_URL env var if set,
// otherwise returns the default https://service.allow2.com
std::string GetAllow2ServiceBaseUrl();

// Check if using a custom/staging environment
bool IsUsingCustomEndpoint();

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_COMMON_ALLOW2_UTILS_H_

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/common/allow2_utils.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

#include "base/uuid.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "crypto/sha2.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "url/gurl.h"

namespace allow2 {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kAllow2Enabled, true);
  registry->RegisterStringPref(prefs::kAllow2ChildId, std::string());
  registry->RegisterStringPref(prefs::kAllow2CachedChildren, std::string());
  registry->RegisterTimePref(prefs::kAllow2LastCheckTime, base::Time());
  registry->RegisterStringPref(prefs::kAllow2CachedCheckResult, std::string());
  registry->RegisterTimePref(prefs::kAllow2CachedCheckExpiry, base::Time());
  registry->RegisterBooleanPref(prefs::kAllow2Blocked, false);
  registry->RegisterStringPref(prefs::kAllow2DayTypeToday, std::string());
  registry->RegisterIntegerPref(prefs::kAllow2RemainingSeconds, 0);
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kAllow2Credentials, std::string());
  registry->RegisterStringPref(prefs::kAllow2DeviceToken, std::string());
  registry->RegisterStringPref(prefs::kAllow2DeviceName, std::string());
  registry->RegisterTimePref(prefs::kAllow2PairedAt, base::Time());
}

bool IsPaired(PrefService* local_state) {
  if (!local_state) {
    return false;
  }
  const std::string& credentials =
      local_state->GetString(prefs::kAllow2Credentials);
  return !credentials.empty();
}

bool IsEnabled(PrefService* profile_prefs) {
  if (!profile_prefs) {
    return false;
  }
  return profile_prefs->GetBoolean(prefs::kAllow2Enabled);
}

bool IsSharedDeviceMode(PrefService* profile_prefs) {
  if (!profile_prefs) {
    return true;
  }
  const std::string& child_id =
      profile_prefs->GetString(prefs::kAllow2ChildId);
  return child_id.empty();
}

std::string GetCurrentChildId(PrefService* profile_prefs) {
  if (!profile_prefs) {
    return std::string();
  }
  return profile_prefs->GetString(prefs::kAllow2ChildId);
}

std::string GenerateDeviceToken() {
  // Generate a unique device token with Brave prefix
  return std::string(kDeviceTokenPrefix) +
         base::Uuid::GenerateRandomV4().AsLowercaseString();
}

ActivityId CategorizeUrl(const std::string& url_string) {
  GURL url(url_string);
  if (!url.is_valid()) {
    return ActivityId::kInternet;
  }

  std::string host(url.host());

  // Remove www. prefix if present
  if (base::StartsWith(host, "www.", base::CompareCase::INSENSITIVE_ASCII)) {
    host = host.substr(4);
  }

  // Check social domains
  for (const char* domain : kSocialDomains) {
    if (host == domain || base::EndsWith(host, std::string(".") + domain,
                                          base::CompareCase::INSENSITIVE_ASCII)) {
      return ActivityId::kSocial;
    }
  }

  // Check gaming domains
  for (const char* domain : kGamingDomains) {
    if (host == domain || base::EndsWith(host, std::string(".") + domain,
                                          base::CompareCase::INSENSITIVE_ASCII)) {
      return ActivityId::kGaming;
    }
  }

  // Check education domains
  for (const char* domain : kEducationDomains) {
    if (host == domain || base::EndsWith(host, std::string(".") + domain,
                                          base::CompareCase::INSENSITIVE_ASCII)) {
      return ActivityId::kEducation;
    }
  }

  // Default to general internet
  return ActivityId::kInternet;
}

std::string HashPin(const std::string& pin, const std::string& salt) {
  std::string salted_pin = pin + salt;
  std::string hash = crypto::SHA256HashString(salted_pin);

  // Convert to hex string
  std::string hex_hash;
  hex_hash.reserve(hash.size() * 2);
  for (unsigned char c : hash) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", c);
    hex_hash += buf;
  }

  return "sha256:" + hex_hash;
}

bool ValidatePinHash(const std::string& entered_pin,
                     const std::string& stored_hash,
                     const std::string& salt) {
  if (entered_pin.empty() || stored_hash.empty() || salt.empty()) {
    return false;
  }

  std::string computed_hash = HashPin(entered_pin, salt);

  // Constant-time comparison to prevent timing attacks
  if (computed_hash.size() != stored_hash.size()) {
    return false;
  }

  volatile uint8_t result = 0;
  for (size_t i = 0; i < computed_hash.size(); ++i) {
    result |= static_cast<uint8_t>(computed_hash[i]) ^
              static_cast<uint8_t>(stored_hash[i]);
  }

  return result == 0;
}

std::string GetCurrentTimezone() {
  // Get the system's default timezone using ICU.
  // This is critical for time-based parental controls (bedtime, school hours)
  // to work correctly regardless of the device's location.
  std::unique_ptr<icu::TimeZone> zone(icu::TimeZone::createDefault());
  icu::UnicodeString id;
  zone->getID(id);

  // Convert ICU UnicodeString to std::string
  std::string timezone_id;
  id.toUTF8String(timezone_id);

  // Return the timezone ID (e.g., "America/New_York", "Europe/London")
  // If somehow empty, fall back to UTC
  return timezone_id.empty() ? "UTC" : timezone_id;
}

// ============================================================================
// Base URL Functions (support ALLOW2_BASE_URL environment variable override)
// ============================================================================

std::string GetAllow2ApiBaseUrl() {
  // Check for environment variable override
  const char* env_base_url = std::getenv(kAllow2BaseUrlEnvVar);
  if (env_base_url && env_base_url[0] != '\0') {
    return std::string(env_base_url);
  }
  return std::string(kAllow2DefaultApiBaseUrl);
}

std::string GetAllow2ServiceBaseUrl() {
  // Check for environment variable override
  // When ALLOW2_BASE_URL is set, use it for BOTH api and service endpoints
  const char* env_base_url = std::getenv(kAllow2BaseUrlEnvVar);
  if (env_base_url && env_base_url[0] != '\0') {
    return std::string(env_base_url);
  }
  return std::string(kAllow2DefaultServiceBaseUrl);
}

bool IsUsingCustomEndpoint() {
  const char* env_base_url = std::getenv(kAllow2BaseUrlEnvVar);
  return env_base_url && env_base_url[0] != '\0';
}

}  // namespace allow2

# Allow2 Offline Authentication Library Design

## Document Information

- **Version:** 1.0
- **Status:** Draft
- **Author:** System Architecture Designer
- **Date:** 2026-02-18
- **License:** MPL 2.0

## Executive Summary

This document describes the architecture for the Allow2 Offline Authentication Library, a set of C++ components enabling offline permission decisions in the Brave browser. The library is designed to be:

1. **Reusable** - Components can be extracted as a standalone library
2. **Secure** - Uses Ed25519 and HMAC-SHA256 cryptography
3. **Resilient** - Graceful degradation when connectivity is lost
4. **Integrable** - Seamlessly plugs into existing Allow2Service

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Component Specifications](#component-specifications)
3. [Interface Definitions](#interface-definitions)
4. [Dependency Graph](#dependency-graph)
5. [Integration Points](#integration-points)
6. [Security Considerations](#security-considerations)
7. [Data Flow Diagrams](#data-flow-diagrams)
8. [Test Strategy](#test-strategy)

---

## Architecture Overview

### Design Principles

1. **Offline-First**: All decisions possible without network connectivity
2. **Cryptographic Integrity**: No forged grants or approvals possible
3. **Time-Bounded**: All grants and caches have explicit expiration
4. **Minimal Trust**: Child device cannot escalate privileges
5. **Auditability**: All offline decisions logged for sync when online

### Component Hierarchy

```
+------------------------------------------------------------------+
|                      Allow2Service (existing)                     |
+------------------------------------------------------------------+
                                |
                                v
+------------------------------------------------------------------+
|                  Allow2OfflineCoordinator (new)                   |
|  - Orchestrates offline decision flow                            |
|  - Decides when to use cached vs online data                     |
+------------------------------------------------------------------+
          |           |           |           |           |
          v           v           v           v           v
+----------+  +----------+  +----------+  +----------+  +----------+
| Offline  |  | Voice    |  | QR       |  | Offline  |  | Local    |
| Crypto   |  | Code     |  | Token    |  | Cache    |  | Decision |
+----------+  +----------+  +----------+  +----------+  +----------+
                                              |
                                              v
                              +----------+  +----------+
                              | Deficit  |  | Travel   |
                              | Tracker  |  | Mode     |
                              +----------+  +----------+
```

### File Structure

```
brave/components/allow2/browser/
  |-- allow2_offline_crypto.h/cc          # Core cryptographic utilities
  |-- allow2_voice_code.h/cc              # Voice code request/approval
  |-- allow2_qr_token.h/cc                # QR-based grant tokens
  |-- allow2_offline_cache.h/cc           # Multi-day permission cache
  |-- allow2_local_decision.h/cc          # Offline decision engine
  |-- allow2_deficit_tracker.h/cc         # Deficit pool management
  |-- allow2_travel_mode.h/cc             # Timezone handling
  |-- allow2_offline_coordinator.h/cc     # Orchestration layer
```

---

## Component Specifications

### 1. allow2_offline_crypto.h/cc

**Purpose:** Core cryptographic utilities using Chromium's `//crypto` library.

#### Class Diagram

```
+--------------------------------------------------+
|            Allow2OfflineCrypto                   |
+--------------------------------------------------+
| - key_pair_: Ed25519KeyPair                      |
| - device_secret_: std::vector<uint8_t>           |
| - pref_service_: raw_ptr<PrefService>            |
+--------------------------------------------------+
| + GenerateKeyPair(): bool                        |
| + LoadKeyPair(): bool                            |
| + HasKeyPair(): bool                             |
| + GetPublicKey(): std::vector<uint8_t>           |
| + Sign(data): std::vector<uint8_t>               |
| + Verify(data, sig, pubkey): bool                |
| + ComputeHMAC(key, data): std::vector<uint8_t>   |
| + DeriveKey(secret, salt, info): SecretKey       |
| + GenerateNonce(): uint32_t                      |
| + GetTimeBucket(): uint32_t                      |
| + SecureCompare(a, b): bool                      |
+--------------------------------------------------+
| - EncryptKeyPair(plaintext): std::string         |
| - DecryptKeyPair(ciphertext): std::vector<uint8_t>|
+--------------------------------------------------+

+--------------------------------------------------+
|               Ed25519KeyPair                     |
+--------------------------------------------------+
| + public_key: std::array<uint8_t, 32>            |
| + private_key: std::array<uint8_t, 64>           |
| + IsValid(): bool                                |
+--------------------------------------------------+

+--------------------------------------------------+
|                  SecretKey                       |
+--------------------------------------------------+
| + key: std::array<uint8_t, 32>                   |
| + Clear(): void                                  |
| + ~SecretKey() { Clear(); }                      |
+--------------------------------------------------+
```

#### Key Methods

| Method | Description |
|--------|-------------|
| `GenerateKeyPair()` | Creates new Ed25519 key pair, stores encrypted in prefs |
| `LoadKeyPair()` | Loads and decrypts stored key pair |
| `Sign(data)` | Signs data with device's private key |
| `Verify(data, sig, pubkey)` | Verifies Ed25519 signature |
| `ComputeHMAC(key, data)` | HMAC-SHA256 computation |
| `DeriveKey(secret, salt, info)` | HKDF key derivation |
| `GetTimeBucket()` | Returns current 15-minute bucket index |
| `SecureCompare(a, b)` | Constant-time comparison |

#### Dependencies

- `//crypto` (Chromium crypto library)
- `//components/os_crypt/sync` (for key storage encryption)
- `//base` (time, memory utilities)

---

### 2. allow2_voice_code.h/cc

**Purpose:** Voice code system for phone-based offline authentication.

#### Voice Code Format

```
Format: "T A MM NN"

T  = Type (1 digit)
     1 = Grant extra time
     2 = Lift ban temporarily
     3 = Extend bedtime

A  = Activity (1 digit)
     1 = Internet (general)
     3 = Gaming
     8 = Screen time
     9 = Social media

MM = Minutes / 5 (2 digits, 00-99)
     Example: 06 = 30 minutes, 12 = 60 minutes

NN = Nonce/Check (2 digits)
     HMAC-derived verification code
```

#### Class Diagram

```
+--------------------------------------------------+
|            Allow2VoiceCode                       |
+--------------------------------------------------+
| - crypto_: raw_ptr<Allow2OfflineCrypto>          |
| - shared_secret_: std::vector<uint8_t>           |
| - last_approval_bucket_: uint32_t                |
+--------------------------------------------------+
| + GenerateRequestCode(type, activity): string    |
| + ValidateApprovalCode(code, expected): Result   |
| + ParseApprovalCode(code): ApprovalCodeData      |
| + SetSharedSecret(secret): void                  |
| + GetTimeBucketWindow(): uint32_t                |
+--------------------------------------------------+

+--------------------------------------------------+
|              VoiceCodeType                       |
+--------------------------------------------------+
| kGrantTime = 1                                   |
| kLiftBan = 2                                     |
| kExtendBedtime = 3                               |
+--------------------------------------------------+

+--------------------------------------------------+
|            ApprovalCodeData                      |
+--------------------------------------------------+
| + type: VoiceCodeType                            |
| + activity: ActivityId                           |
| + minutes: int                                   |
| + nonce: uint8_t                                 |
| + is_valid: bool                                 |
+--------------------------------------------------+

+--------------------------------------------------+
|           VoiceCodeResult                        |
+--------------------------------------------------+
| + success: bool                                  |
| + error: std::string                             |
| + grant_type: VoiceCodeType                      |
| + activity_id: ActivityId                        |
| + minutes_granted: int                           |
| + expires_at: base::Time                         |
+--------------------------------------------------+
```

#### Validation Flow

```
Parent Phone                Child Device (Brave)
     |                              |
     |  1. Child says "Request"     |
     |<-----------------------------|
     |                              |
     |  2. Parent enters code       |
     |  (using Allow2 app)          |
     |                              |
     |  3. App shows approval code  |
     |                              |
     |  4. Parent reads code aloud  |
     |----------------------------->|
     |                              |
     |  5. Child enters code        |
     |                              |
     |  6. HMAC validation          |
     |  (15-min bucket + secret)    |
     |                              |
     |  7. Grant applied locally    |
     |                              |
```

#### Security Properties

- **Replay Protection:** Codes valid only within 15-minute time bucket
- **Forgery Prevention:** HMAC-SHA256 with shared secret
- **One-Time Use:** Each approval tracked to prevent replay within bucket

---

### 3. allow2_qr_token.h/cc

**Purpose:** QR code-based grant tokens with Ed25519 signatures.

#### Token Format (Binary)

```
+--------+--------+--------+--------+--------+--------+--------+--------+
| Version| Type   | ChildID (4 bytes)       | ActivityID      | Minutes |
+--------+--------+--------+--------+--------+--------+--------+--------+
| Expiry (8 bytes, Unix timestamp)                                      |
+--------+--------+--------+--------+--------+--------+--------+--------+
| Ed25519 Signature (64 bytes)                                          |
+--------+--------+--------+--------+--------+--------+--------+--------+

Total: 80 bytes (encoded as base64 = ~108 chars)
```

#### Class Diagram

```
+--------------------------------------------------+
|             Allow2QRToken                        |
+--------------------------------------------------+
| - crypto_: raw_ptr<Allow2OfflineCrypto>          |
| - parent_public_keys_: vector<PublicKey>         |
+--------------------------------------------------+
| + ParseToken(base64): GrantToken                 |
| + ValidateToken(token): ValidationResult         |
| + IsExpired(token): bool                         |
| + RegisterParentPublicKey(key): void             |
| + ClearPublicKeys(): void                        |
+--------------------------------------------------+

+--------------------------------------------------+
|               GrantToken                         |
+--------------------------------------------------+
| + version: uint8_t                               |
| + type: GrantType                                |
| + child_id: uint64_t                             |
| + activity_id: ActivityId                        |
| + minutes: uint16_t                              |
| + expires_at: base::Time                         |
| + signature: std::array<uint8_t, 64>             |
| + raw_data: std::vector<uint8_t>                 |
+--------------------------------------------------+

+--------------------------------------------------+
|                GrantType                         |
+--------------------------------------------------+
| kExtraTime = 1      // Add minutes to activity   |
| kUnlimitedUntil = 2 // No limit until expiry     |
| kBanLift = 3        // Remove ban temporarily    |
| kBedtimeExtend = 4  // Extend bedtime            |
+--------------------------------------------------+

+--------------------------------------------------+
|            ValidationResult                      |
+--------------------------------------------------+
| + valid: bool                                    |
| + error: std::string                             |
| + error_code: ValidationErrorCode                |
| + signer_index: int                              |
+--------------------------------------------------+

+--------------------------------------------------+
|          ValidationErrorCode                     |
+--------------------------------------------------+
| kValid = 0                                       |
| kInvalidFormat = 1                               |
| kExpired = 2                                     |
| kInvalidSignature = 3                            |
| kUnknownSigner = 4                               |
| kChildMismatch = 5                               |
| kVersionUnsupported = 6                          |
+--------------------------------------------------+
```

#### QR Token Flow

```
Parent App                     Child Device
    |                              |
    | 1. Parent creates grant      |
    | (signs with private key)     |
    |                              |
    | 2. Displays QR code          |
    |                              |
    | 3. Child scans QR            |
    |<-----------------------------|
    |                              |
    | 4. Verify signature          |
    | (against registered pubkeys) |
    |                              |
    | 5. Check expiry, child ID    |
    |                              |
    | 6. Apply grant locally       |
    |                              |
```

---

### 4. allow2_offline_cache.h/cc

**Purpose:** Multi-day cache for pre-resolved permission data.

#### Cache Structure

```
+--------------------------------------------------+
|           DayCacheEntry                          |
+--------------------------------------------------+
| + date: base::Time (start of day)                |
| + day_type_id: int                               |
| + day_type_name: std::string                     |
| + quotas: map<ActivityId, QuotaInfo>             |
| + time_blocks: vector<TimeBlock>                 |
| + extensions: vector<Extension>                  |
| + bans: vector<Ban>                              |
| + fetched_at: base::Time                         |
| + expires_at: base::Time                         |
| + server_signature: std::vector<uint8_t>         |
+--------------------------------------------------+

+--------------------------------------------------+
|              QuotaInfo                           |
+--------------------------------------------------+
| + activity_id: ActivityId                        |
| + daily_limit_minutes: int                       |
| + used_minutes: int                              |
| + bonus_minutes: int                             |
| + carry_over_minutes: int                        |
+--------------------------------------------------+

+--------------------------------------------------+
|              TimeBlock                           |
+--------------------------------------------------+
| + start_time: base::TimeDelta (from midnight)    |
| + end_time: base::TimeDelta                      |
| + activities_blocked: vector<ActivityId>         |
| + reason: std::string (e.g., "Bedtime")          |
+--------------------------------------------------+

+--------------------------------------------------+
|              Extension                           |
+--------------------------------------------------+
| + activity_id: ActivityId                        |
| + minutes: int                                   |
| + expires_at: base::Time                         |
| + granted_by: std::string                        |
| + reason: std::string                            |
+--------------------------------------------------+

+--------------------------------------------------+
|                  Ban                             |
+--------------------------------------------------+
| + activity_id: ActivityId                        |
| + reason: std::string                            |
| + expires_at: base::Time (null = indefinite)     |
| + can_request_lift: bool                         |
+--------------------------------------------------+
```

#### Class Diagram

```
+--------------------------------------------------+
|           Allow2OfflineCache                     |
+--------------------------------------------------+
| - crypto_: raw_ptr<Allow2OfflineCrypto>          |
| - pref_service_: raw_ptr<PrefService>            |
| - cache_: map<ChildId, ChildCache>               |
| - cache_days_: int (default: 3)                  |
+--------------------------------------------------+
| + StoreDayData(child_id, date, data): bool       |
| + GetDayData(child_id, date): DayCacheEntry?     |
| + GetTodayData(child_id): DayCacheEntry?         |
| + GetTomorrowData(child_id): DayCacheEntry?      |
| + IsCacheValid(child_id): bool                   |
| + IsCacheStale(child_id): bool                   |
| + GetCacheAge(child_id): base::TimeDelta         |
| + ClearCache(child_id): void                     |
| + ClearAllCaches(): void                         |
| + PruneExpiredEntries(): void                    |
| + SetMaxCacheDays(days): void                    |
+--------------------------------------------------+

+--------------------------------------------------+
|              ChildCache                          |
+--------------------------------------------------+
| + child_id: uint64_t                             |
| + days: map<Date, DayCacheEntry>                 |
| + last_sync: base::Time                          |
| + timezone: std::string                          |
+--------------------------------------------------+
```

#### Cache Validity States

| State | Description | Action |
|-------|-------------|--------|
| `Valid` | Cache exists, not expired | Use for decisions |
| `Stale` | Cache exists, >24h old | Use with warning, try refresh |
| `Expired` | Cache exists, >72h old | Degraded mode |
| `Missing` | No cache for child | Block with offline message |

---

### 5. allow2_local_decision.h/cc

**Purpose:** Offline decision engine using cached data.

#### Class Diagram

```
+--------------------------------------------------+
|          Allow2LocalDecision                     |
+--------------------------------------------------+
| - cache_: raw_ptr<Allow2OfflineCache>            |
| - deficit_tracker_: raw_ptr<DeficitTracker>      |
| - usage_log_: map<ChildId, vector<UsageEntry>>   |
| - pending_sync_: vector<DecisionRecord>          |
+--------------------------------------------------+
| + EvaluatePermission(req): DecisionResult        |
| + RecordUsage(child_id, activity, mins): void    |
| + GetRemainingTime(child_id, activity): int      |
| + IsInTimeBlock(child_id): TimeBlockInfo?        |
| + IsBanned(child_id, activity): BanInfo?         |
| + ApplyGrant(child_id, grant): bool              |
| + GetPendingSyncRecords(): vector<DecisionRecord>|
| + ClearSyncedRecords(ids): void                  |
+--------------------------------------------------+

+--------------------------------------------------+
|           PermissionRequest                      |
+--------------------------------------------------+
| + child_id: uint64_t                             |
| + activity_id: ActivityId                        |
| + requested_minutes: int (0 = check only)        |
| + url: std::string (optional, for categorization)|
| + timestamp: base::Time                          |
+--------------------------------------------------+

+--------------------------------------------------+
|            DecisionResult                        |
+--------------------------------------------------+
| + allowed: bool                                  |
| + reason: DecisionReason                         |
| + remaining_minutes: int                         |
| + block_ends_at: base::Time (optional)           |
| + confidence: DecisionConfidence                 |
| + cache_age_minutes: int                         |
| + message: std::string                           |
+--------------------------------------------------+

+--------------------------------------------------+
|           DecisionReason                         |
+--------------------------------------------------+
| kAllowed = 0                                     |
| kQuotaExceeded = 1                               |
| kTimeBlock = 2                                   |
| kBanned = 3                                      |
| kCacheExpired = 4                                |
| kNoCache = 5                                     |
| kChildNotFound = 6                               |
+--------------------------------------------------+

+--------------------------------------------------+
|          DecisionConfidence                      |
+--------------------------------------------------+
| kHigh = 0    // Fresh cache (<1h)                |
| kMedium = 1  // Stale cache (1-24h)              |
| kLow = 2     // Old cache (24-72h)               |
| kNone = 3    // No cache, default deny           |
+--------------------------------------------------+

+--------------------------------------------------+
|            DecisionRecord                        |
+--------------------------------------------------+
| + id: std::string (UUID)                         |
| + child_id: uint64_t                             |
| + activity_id: ActivityId                        |
| + decision: DecisionResult                       |
| + timestamp: base::Time                          |
| + usage_minutes: int                             |
| + was_offline: bool                              |
| + grants_applied: vector<GrantId>                |
+--------------------------------------------------+
```

#### Decision Algorithm

```
EvaluatePermission(request):
    1. Get cache entry for child's today
       IF no cache:
           RETURN {allowed: false, reason: kNoCache, confidence: kNone}

    2. Check cache validity
       confidence = ComputeConfidence(cache.fetched_at)

    3. Check for bans
       IF activity is banned:
           RETURN {allowed: false, reason: kBanned, ...}

    4. Check time blocks
       IF in time block:
           RETURN {allowed: false, reason: kTimeBlock, block_ends_at: ...}

    5. Calculate remaining time
       total_allowed = quota.daily_limit + quota.bonus + extensions
       used = quota.used + local_usage[today]
       remaining = total_allowed - used

    6. Check quota
       IF remaining <= 0:
           check_deficit = deficit_tracker.CanUseDeficit(child, activity)
           IF check_deficit:
               RETURN {allowed: true, remaining: deficit_minutes, ...}
           RETURN {allowed: false, reason: kQuotaExceeded, ...}

    7. RETURN {allowed: true, remaining: remaining, confidence: ...}
```

---

### 6. allow2_deficit_tracker.h/cc

**Purpose:** Track and manage time deficits for payback system.

#### Deficit Model

```
When child exceeds quota offline:
  - Deficit accrues at 1:1 rate
  - Example: 10 minutes over = 10 minute deficit

On next sync:
  - Server calculates payback rate (default 50%)
  - 10 minute deficit = 5 minute deduction from next day

Until sync:
  - Local tracker maintains deficit
  - Can apply local payback policy
  - Limits maximum offline deficit (e.g., 60 minutes)
```

#### Class Diagram

```
+--------------------------------------------------+
|           Allow2DeficitTracker                   |
+--------------------------------------------------+
| - pref_service_: raw_ptr<PrefService>            |
| - deficits_: map<ChildActivity, DeficitEntry>    |
| - max_deficit_minutes_: int (default: 60)        |
| - payback_rate_: float (default: 0.5)            |
+--------------------------------------------------+
| + RecordOverage(child, activity, mins): void     |
| + GetDeficit(child, activity): int               |
| + GetTotalDeficit(child): int                    |
| + ApplyPayback(child, activity, mins): int       |
| + CanAccrueMoreDeficit(child, activity): bool    |
| + GetAllDeficits(): map<ChildActivity, int>      |
| + ClearDeficit(child, activity): void            |
| + ClearAllDeficits(child): void                  |
| + SyncWithServer(server_deficits): void          |
| + SetMaxDeficitMinutes(mins): void               |
| + SetPaybackRate(rate): void                     |
+--------------------------------------------------+

+--------------------------------------------------+
|              DeficitEntry                        |
+--------------------------------------------------+
| + child_id: uint64_t                             |
| + activity_id: ActivityId                        |
| + deficit_minutes: int                           |
| + accrued_at: base::Time                         |
| + last_payback: base::Time                       |
| + synced_with_server: bool                       |
+--------------------------------------------------+

+--------------------------------------------------+
|           ChildActivity (key type)               |
+--------------------------------------------------+
| + child_id: uint64_t                             |
| + activity_id: ActivityId                        |
| + operator<(): bool                              |
| + operator==(): bool                             |
+--------------------------------------------------+
```

#### Persistence

Deficits are persisted in prefs as JSON:

```json
{
  "deficits": [
    {
      "childId": 1001,
      "activityId": 1,
      "minutes": 15,
      "accruedAt": "2026-02-18T10:30:00Z",
      "synced": false
    }
  ]
}
```

---

### 7. allow2_travel_mode.h/cc

**Purpose:** Handle timezone changes and extended offline periods.

#### Class Diagram

```
+--------------------------------------------------+
|           Allow2TravelMode                       |
+--------------------------------------------------+
| - pref_service_: raw_ptr<PrefService>            |
| - cache_: raw_ptr<Allow2OfflineCache>            |
| - home_timezone_: std::string                    |
| - current_timezone_: std::string                 |
| - travel_detected_: bool                         |
| - travel_start_: base::Time                      |
+--------------------------------------------------+
| + DetectTimezoneChange(): TravelStatus           |
| + GetHomeTimezone(): std::string                 |
| + GetCurrentTimezone(): std::string              |
| + IsInTravelMode(): bool                         |
| + GetTravelDuration(): base::TimeDelta           |
| + SetHomeTimezone(tz): void                      |
| + RequestExtendedCache(): CacheExtension         |
| + GetTimezoneOffset(): base::TimeDelta           |
| + ShouldNotifyParent(): bool                     |
| + GetParentNotificationFlags(): Flags            |
+--------------------------------------------------+

+--------------------------------------------------+
|             TravelStatus                         |
+--------------------------------------------------+
| + detected: bool                                 |
| + home_timezone: std::string                     |
| + current_timezone: std::string                  |
| + offset_hours: int                              |
| + direction: TravelDirection                     |
| + duration_hours: int (if known)                 |
+--------------------------------------------------+

+--------------------------------------------------+
|           TravelDirection                        |
+--------------------------------------------------+
| kNone = 0                                        |
| kEast = 1  // Ahead of home time                 |
| kWest = 2  // Behind home time                   |
+--------------------------------------------------+

+--------------------------------------------------+
|           CacheExtension                         |
+--------------------------------------------------+
| + requested: bool                                |
| + extra_days: int                                |
| + reason: std::string                            |
| + approved: bool (requires parent approval)      |
+--------------------------------------------------+

+--------------------------------------------------+
|       ParentNotificationFlags                    |
+--------------------------------------------------+
| + timezone_changed: bool                         |
| + extended_offline: bool                         |
| + cache_expiring_soon: bool                      |
| + deficit_accrued: bool                          |
| + unusual_usage_pattern: bool                    |
+--------------------------------------------------+
```

#### Timezone Detection

```
DetectTimezoneChange():
    1. Get current system timezone
    2. Compare with stored home timezone
    3. IF different AND difference > 2 hours:
           travel_detected_ = true
           travel_start_ = now
           RETURN TravelStatus{detected: true, ...}
    4. RETURN TravelStatus{detected: false}
```

#### Extended Cache Mode

When travel is detected:
- Request 7-day cache instead of standard 3-day
- Lower cache refresh frequency
- Set parent notification flag
- Allow more lenient deficit policy

---

### 8. allow2_offline_coordinator.h/cc

**Purpose:** Orchestration layer integrating all offline components.

#### Class Diagram

```
+--------------------------------------------------+
|        Allow2OfflineCoordinator                  |
+--------------------------------------------------+
| - crypto_: unique_ptr<Allow2OfflineCrypto>       |
| - voice_code_: unique_ptr<Allow2VoiceCode>       |
| - qr_token_: unique_ptr<Allow2QRToken>           |
| - cache_: unique_ptr<Allow2OfflineCache>         |
| - decision_: unique_ptr<Allow2LocalDecision>     |
| - deficit_: unique_ptr<Allow2DeficitTracker>     |
| - travel_: unique_ptr<Allow2TravelMode>          |
| - is_offline_: bool                              |
| - last_online_: base::Time                       |
+--------------------------------------------------+
| + Initialize(): bool                             |
| + Shutdown(): void                               |
| + SetOfflineMode(offline): void                  |
| + IsOfflineMode(): bool                          |
| + CheckPermission(request): DecisionResult       |
| + ProcessVoiceCode(code): VoiceCodeResult        |
| + ProcessQRToken(token): ValidationResult        |
| + GetOfflineStatus(): OfflineStatus              |
| + SyncWithServer(): SyncResult                   |
| + RegisterObserver(observer): void               |
| + UnregisterObserver(observer): void             |
+--------------------------------------------------+

+--------------------------------------------------+
|             OfflineStatus                        |
+--------------------------------------------------+
| + is_offline: bool                               |
| + offline_duration: base::TimeDelta              |
| + cache_status: CacheStatus                      |
| + pending_sync_count: int                        |
| + deficit_total_minutes: int                     |
| + travel_mode_active: bool                       |
| + last_successful_sync: base::Time               |
+--------------------------------------------------+

+--------------------------------------------------+
|       Allow2OfflineObserver                      |
+--------------------------------------------------+
| + OnOfflineModeChanged(is_offline): void         |
| + OnCacheStatusChanged(status): void             |
| + OnGrantApplied(grant): void                    |
| + OnDeficitChanged(child, activity, mins): void  |
| + OnTravelModeChanged(travel_status): void       |
| + OnSyncRequired(): void                         |
+--------------------------------------------------+

+--------------------------------------------------+
|              SyncResult                          |
+--------------------------------------------------+
| + success: bool                                  |
| + error: std::string                             |
| + records_synced: int                            |
| + cache_refreshed: bool                          |
| + deficits_reconciled: bool                      |
| + server_time: base::Time                        |
+--------------------------------------------------+
```

---

## Interface Definitions

### Header File Template

All headers follow the existing pattern from `allow2_credential_manager.h`:

```cpp
/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CRYPTO_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CRYPTO_H_

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"

class PrefService;

namespace allow2 {

// [Class definitions here]

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_OFFLINE_CRYPTO_H_
```

### Key Interface: Allow2OfflineCrypto

```cpp
namespace allow2 {

// Ed25519 key pair for device identity and signing.
struct Ed25519KeyPair {
  std::array<uint8_t, 32> public_key;
  std::array<uint8_t, 64> private_key;

  bool IsValid() const;
  void Clear();
};

// RAII wrapper for sensitive key material.
class SecretKey {
 public:
  SecretKey();
  ~SecretKey();

  SecretKey(const SecretKey&) = delete;
  SecretKey& operator=(const SecretKey&) = delete;
  SecretKey(SecretKey&&);
  SecretKey& operator=(SecretKey&&);

  std::array<uint8_t, 32> key;
  void Clear();
};

// Core cryptographic utilities for offline authentication.
//
// Uses Chromium's //crypto library for all operations:
// - Ed25519: Key generation, signing, verification
// - HMAC-SHA256: Message authentication
// - HKDF: Key derivation
// - Random: Nonce generation
//
// SECURITY NOTES:
// - Private keys are encrypted via OSCrypt before storage
// - All key material is cleared from memory after use
// - Time-based operations use monotonic time where possible
class Allow2OfflineCrypto {
 public:
  explicit Allow2OfflineCrypto(PrefService* local_state);
  ~Allow2OfflineCrypto();

  Allow2OfflineCrypto(const Allow2OfflineCrypto&) = delete;
  Allow2OfflineCrypto& operator=(const Allow2OfflineCrypto&) = delete;

  // Key pair management.
  // Generates and stores an Ed25519 key pair for this device.
  // Returns false if generation or storage fails.
  bool GenerateKeyPair();

  // Loads existing key pair from encrypted storage.
  // Returns false if no key pair exists or decryption fails.
  bool LoadKeyPair();

  // Checks if a valid key pair is loaded.
  bool HasKeyPair() const;

  // Gets the public key (32 bytes).
  // Returns empty vector if no key pair loaded.
  std::vector<uint8_t> GetPublicKey() const;

  // Signing operations.
  // Signs data with the device's private key.
  // Returns empty vector if signing fails.
  std::vector<uint8_t> Sign(const std::vector<uint8_t>& data) const;

  // Verifies an Ed25519 signature.
  // Returns true if signature is valid.
  static bool Verify(const std::vector<uint8_t>& data,
                     const std::vector<uint8_t>& signature,
                     const std::vector<uint8_t>& public_key);

  // HMAC operations.
  // Computes HMAC-SHA256(key, data).
  static std::vector<uint8_t> ComputeHMAC(
      const std::vector<uint8_t>& key,
      const std::vector<uint8_t>& data);

  // Key derivation.
  // Derives a key using HKDF-SHA256.
  static SecretKey DeriveKey(const std::vector<uint8_t>& secret,
                             const std::vector<uint8_t>& salt,
                             const std::string& info,
                             size_t key_length = 32);

  // Time operations.
  // Returns current 15-minute time bucket index.
  // Used for voice code validation windows.
  static uint32_t GetTimeBucket();

  // Generates a cryptographically secure random nonce.
  static uint32_t GenerateNonce();

  // Constant-time comparison to prevent timing attacks.
  static bool SecureCompare(const std::vector<uint8_t>& a,
                            const std::vector<uint8_t>& b);

 private:
  bool EncryptKeyPair(const Ed25519KeyPair& key_pair, std::string* output) const;
  bool DecryptKeyPair(const std::string& encrypted, Ed25519KeyPair* output) const;

  raw_ptr<PrefService> local_state_;
  std::optional<Ed25519KeyPair> key_pair_;
};

}  // namespace allow2
```

### Key Interface: Allow2LocalDecision

```cpp
namespace allow2 {

// Request for permission evaluation.
struct PermissionRequest {
  uint64_t child_id = 0;
  ActivityId activity_id = ActivityId::kInternet;
  int requested_minutes = 0;  // 0 = check only, >0 = consume time
  std::string url;  // Optional, for activity categorization
  base::Time timestamp;
};

// Result of permission evaluation.
struct DecisionResult {
  bool allowed = false;
  DecisionReason reason = DecisionReason::kNoCache;
  int remaining_minutes = 0;
  std::optional<base::Time> block_ends_at;
  DecisionConfidence confidence = DecisionConfidence::kNone;
  int cache_age_minutes = 0;
  std::string message;
};

// Offline decision engine using cached permission data.
//
// This class is the core of offline functionality. It evaluates
// permission requests using locally cached data when the device
// cannot reach the Allow2 server.
//
// DECISION HIERARCHY:
// 1. Check for explicit bans (always deny)
// 2. Check time blocks (deny if in blocked period)
// 3. Check quota (allow if remaining time, deny if exceeded)
// 4. Consider deficit system for minor overages
//
// CONFIDENCE LEVELS:
// - High: Cache < 1 hour old, decisions reliable
// - Medium: Cache 1-24 hours old, proceed with caution
// - Low: Cache 24-72 hours old, notify user of degraded mode
// - None: No cache, default to deny with explanation
class Allow2LocalDecision {
 public:
  Allow2LocalDecision(Allow2OfflineCache* cache,
                      Allow2DeficitTracker* deficit_tracker);
  ~Allow2LocalDecision();

  Allow2LocalDecision(const Allow2LocalDecision&) = delete;
  Allow2LocalDecision& operator=(const Allow2LocalDecision&) = delete;

  // Evaluate a permission request against cached data.
  // This is the main entry point for offline decisions.
  DecisionResult EvaluatePermission(const PermissionRequest& request);

  // Record usage locally (for quota tracking).
  // Called periodically while activity is ongoing.
  void RecordUsage(uint64_t child_id, ActivityId activity, int minutes);

  // Get remaining time for an activity.
  // Returns -1 if unknown (no cache).
  int GetRemainingTime(uint64_t child_id, ActivityId activity);

  // Check if currently in a time block.
  // Returns nullopt if not in a block.
  std::optional<TimeBlockInfo> IsInTimeBlock(uint64_t child_id);

  // Check if activity is banned.
  // Returns nullopt if not banned.
  std::optional<BanInfo> IsBanned(uint64_t child_id, ActivityId activity);

  // Apply a grant (from voice code or QR token).
  // Returns true if grant was successfully applied.
  bool ApplyGrant(uint64_t child_id, const Grant& grant);

  // Sync management.
  // Get records that need to be synced to server.
  std::vector<DecisionRecord> GetPendingSyncRecords();

  // Clear records after successful sync.
  void ClearSyncedRecords(const std::vector<std::string>& record_ids);

 private:
  raw_ptr<Allow2OfflineCache> cache_;
  raw_ptr<Allow2DeficitTracker> deficit_tracker_;
  std::map<uint64_t, std::vector<UsageEntry>> usage_log_;
  std::vector<DecisionRecord> pending_sync_;
};

}  // namespace allow2
```

---

## Dependency Graph

```
                    +-------------------+
                    |   PrefService     |
                    | (local_state,     |
                    |  profile_prefs)   |
                    +-------------------+
                            |
            +---------------+---------------+
            |               |               |
            v               v               v
+-------------------+ +-------------+ +-------------+
| Allow2Offline     | | Allow2      | | Allow2      |
| Crypto            | | OfflineCache| | DeficitTrack|
| (//crypto)        | |             | |             |
+-------------------+ +-------------+ +-------------+
            |               |               |
            |   +-----------+               |
            |   |                           |
            v   v                           |
+-------------------+                       |
| Allow2VoiceCode   |                       |
| Allow2QRToken     |                       |
+-------------------+                       |
            |                               |
            +---------------+---------------+
                            |
                            v
                    +-------------------+
                    | Allow2LocalDeci.. |
                    +-------------------+
                            |
                            v
                    +-------------------+
                    | Allow2TravelMode  |
                    +-------------------+
                            |
                            v
                    +-------------------+
                    | Allow2Offline     |
                    | Coordinator       |
                    +-------------------+
                            |
                            v
                    +-------------------+
                    | Allow2Service     |
                    | (existing)        |
                    +-------------------+
```

### Build Dependencies (BUILD.gn additions)

```gn
static_library("browser") {
  sources = [
    # ... existing sources ...

    # New offline auth components
    "allow2_offline_crypto.cc",
    "allow2_offline_crypto.h",
    "allow2_voice_code.cc",
    "allow2_voice_code.h",
    "allow2_qr_token.cc",
    "allow2_qr_token.h",
    "allow2_offline_cache.cc",
    "allow2_offline_cache.h",
    "allow2_local_decision.cc",
    "allow2_local_decision.h",
    "allow2_deficit_tracker.cc",
    "allow2_deficit_tracker.h",
    "allow2_travel_mode.cc",
    "allow2_travel_mode.h",
    "allow2_offline_coordinator.cc",
    "allow2_offline_coordinator.h",
  ]

  deps = [
    # ... existing deps ...
    "//crypto",  # Already included
    "//components/os_crypt/sync",  # Already included
  ]
}

source_set("unit_tests") {
  testonly = true

  sources = [
    # ... existing test sources ...

    # New offline auth tests
    "allow2_offline_crypto_unittest.cc",
    "allow2_voice_code_unittest.cc",
    "allow2_qr_token_unittest.cc",
    "allow2_offline_cache_unittest.cc",
    "allow2_local_decision_unittest.cc",
    "allow2_deficit_tracker_unittest.cc",
    "allow2_travel_mode_unittest.cc",
    "allow2_offline_coordinator_unittest.cc",
  ]

  deps = [
    # ... existing test deps ...
  ]
}
```

---

## Integration Points

### 1. Allow2Service Integration

The `Allow2OfflineCoordinator` integrates with the existing `Allow2Service`:

```cpp
// In allow2_service.h, add member:
class Allow2Service : public KeyedService {
  // ... existing members ...

 private:
  std::unique_ptr<Allow2OfflineCoordinator> offline_coordinator_;
};

// In allow2_service.cc, modify CheckAllowance:
void Allow2Service::CheckAllowance(CheckCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Try online check first
  if (IsOnline()) {
    PerformOnlineCheck(std::move(callback));
    return;
  }

  // Fall back to offline decision
  if (offline_coordinator_ && offline_coordinator_->IsInitialized()) {
    PermissionRequest request;
    request.child_id = GetCurrentChildId();
    request.activity_id = ActivityId::kInternet;
    request.timestamp = base::Time::Now();

    DecisionResult decision = offline_coordinator_->CheckPermission(request);

    CheckResult result;
    result.allowed = decision.allowed;
    result.remaining_seconds = decision.remaining_minutes * 60;
    result.block_reason = decision.message;

    std::move(callback).Run(result);
    return;
  }

  // No cache available, return blocked
  CheckResult result;
  result.allowed = false;
  result.block_reason = "Unable to verify allowance (offline)";
  std::move(callback).Run(result);
}
```

### 2. Cache Population

Cache is populated during successful online checks:

```cpp
void Allow2Service::OnCheckComplete(const CheckResponse& response) {
  if (response.success) {
    // Store in offline cache
    if (offline_coordinator_) {
      DayCacheEntry entry;
      entry.date = base::Time::Now().LocalMidnight();
      entry.day_type_name = response.result.day_type;
      // ... populate from response ...

      offline_coordinator_->GetCache()->StoreDayData(
          GetCurrentChildId(), entry.date, entry);
    }
  }
}
```

### 3. Grant Application

Voice codes and QR tokens are processed through the coordinator:

```cpp
// Called from UI when user enters voice code
void Allow2Service::ProcessVoiceCode(const std::string& code,
                                     VoiceCodeCallback callback) {
  if (!offline_coordinator_) {
    std::move(callback).Run({.success = false, .error = "Offline mode unavailable"});
    return;
  }

  VoiceCodeResult result = offline_coordinator_->ProcessVoiceCode(code);
  std::move(callback).Run(result);
}

// Called from camera/QR scanner
void Allow2Service::ProcessQRToken(const std::string& token,
                                   QRTokenCallback callback) {
  if (!offline_coordinator_) {
    std::move(callback).Run({.valid = false, .error = "Offline mode unavailable"});
    return;
  }

  ValidationResult result = offline_coordinator_->ProcessQRToken(token);
  std::move(callback).Run(result);
}
```

### 4. Pref Registration

New preferences for offline system:

```cpp
// In pref_names.h, add:
namespace prefs {
// ... existing prefs ...

// Offline auth key pair (encrypted via OSCrypt)
inline constexpr char kAllow2OfflineKeyPair[] =
    "brave.allow2.offline_keypair";

// Multi-day cache as JSON
inline constexpr char kAllow2OfflineCache[] =
    "brave.allow2.offline_cache";

// Deficit tracker state as JSON
inline constexpr char kAllow2DeficitState[] =
    "brave.allow2.deficit_state";

// Home timezone for travel detection
inline constexpr char kAllow2HomeTimezone[] =
    "brave.allow2.home_timezone";

// Travel mode flags
inline constexpr char kAllow2TravelModeActive[] =
    "brave.allow2.travel_mode_active";

// Pending sync records as JSON
inline constexpr char kAllow2PendingSyncRecords[] =
    "brave.allow2.pending_sync";

}  // namespace prefs
```

---

## Security Considerations

### Threat Model

| Threat | Mitigation |
|--------|------------|
| **Forged voice codes** | HMAC-SHA256 with shared secret; 15-minute windows |
| **Replayed voice codes** | Tracking of used codes within time bucket |
| **Forged QR tokens** | Ed25519 signature verification against registered parent keys |
| **Cache tampering** | Server-signed cache entries; integrity checks on load |
| **Key extraction** | Private keys encrypted via OSCrypt (platform keychain) |
| **Timing attacks** | Constant-time comparison for all sensitive comparisons |
| **Clock manipulation** | Server time validation on sync; monotonic time for intervals |
| **Deficit abuse** | Maximum deficit cap; payback rate enforced |

### Key Storage

```
Private keys are protected using the platform's secure storage:

macOS:    Keychain (via OSCrypt)
Windows:  DPAPI (Data Protection API, via OSCrypt)
Linux:    libsecret/GNOME Keyring/KWallet (via OSCrypt)

The key is encrypted before storage in prefs, ensuring that
even if an attacker gains access to the preference file,
they cannot extract the private key without access to the
platform's secure storage.
```

### Shared Secret Management

```
Voice code shared secret is established during pairing:

1. During pairing, server generates a random shared secret
2. Secret is transmitted encrypted to device
3. Secret is stored encrypted in local_state via OSCrypt
4. Parent's Allow2 app receives the same secret
5. HMAC validation uses this shared secret

Secret rotation:
- New secret generated on each re-pairing
- Optional rotation during sync (server-initiated)
```

### Audit Logging

All offline decisions are logged for sync:

```cpp
struct DecisionRecord {
  std::string id;           // UUID for this record
  uint64_t child_id;
  ActivityId activity_id;
  bool allowed;
  DecisionReason reason;
  base::Time timestamp;
  int usage_minutes;
  bool was_offline;
  std::vector<std::string> grants_applied;
  DecisionConfidence confidence;
};
```

---

## Data Flow Diagrams

### Online to Offline Transition

```
  Device                          Server
    |                               |
    | 1. Normal online operation    |
    |<----------------------------->|
    |                               |
    | 2. Network lost               |
    X-------------------------------X
    |                               |
    | 3. Detect offline             |
    |                               |
    | 4. Switch to offline mode     |
    |                               |
    | 5. Load cached data           |
    |                               |
    | 6. Local decision making      |
    |                               |
    | 7. Track local usage          |
    |                               |
    | 8. Accept voice codes/QR      |
    |                               |
    | 9. Network restored           |
    |<----------------------------->|
    |                               |
    | 10. Sync pending records      |
    |<----------------------------->|
    |                               |
    | 11. Refresh cache             |
    |<----------------------------->|
    |                               |
    | 12. Resume online operation   |
    |<----------------------------->|
```

### Voice Code Flow

```
  Child Device         Parent Phone         Server
       |                    |                  |
       | "I want more time" |                  |
       |------------------->|                  |
       |                    |                  |
       |                    | (Offline)        |
       |                    |                  |
       |                    | Open Allow2 app  |
       |                    |                  |
       |                    | Enter grant:     |
       |                    | - Type: Time     |
       |                    | - Activity: Net  |
       |                    | - Minutes: 30    |
       |                    |                  |
       |                    | App calculates:  |
       |                    | HMAC(secret,     |
       |                    |   type+activity+ |
       |                    |   mins+bucket)   |
       |                    |                  |
       |                    | Shows code:      |
       |  "1 1 06 42"       | "1 1 06 42"     |
       |<-------------------|                  |
       |                    |                  |
       | Child enters code  |                  |
       |                    |                  |
       | Validate HMAC      |                  |
       | (same calculation) |                  |
       |                    |                  |
       | Apply 30 min grant |                  |
       |                    |                  |
       | Log for sync       |                  |
       |                    |                  |
       | (Later, online)    |                  |
       |------------------------------------- >|
       |                    |  Sync grant log  |
       |<--------------------------------------|
       |                    |                  |
```

### QR Token Flow

```
  Child Device         Parent Phone         Server
       |                    |                  |
       | "Scan QR for time" |                  |
       |------------------->|                  |
       |                    |                  |
       |                    | Open Allow2 app  |
       |                    |                  |
       |                    | Create grant:    |
       |                    | - Child: Emma    |
       |                    | - Type: Time     |
       |                    | - Activity: Net  |
       |                    | - Minutes: 60    |
       |                    | - Expires: 4hr   |
       |                    |                  |
       |                    | Sign with        |
       |                    | parent's Ed25519 |
       |                    | private key      |
       |                    |                  |
       |                    | Display QR code  |
       |    [QR CODE]       |                  |
       |<-------------------|                  |
       |                    |                  |
       | Scan QR            |                  |
       |                    |                  |
       | Parse token        |                  |
       |                    |                  |
       | Verify signature   |                  |
       | (against stored    |                  |
       |  parent pubkey)    |                  |
       |                    |                  |
       | Check expiry       |                  |
       |                    |                  |
       | Check child ID     |                  |
       |                    |                  |
       | Apply 60 min grant |                  |
       |                    |                  |
       | Log for sync       |                  |
       |                    |                  |
```

---

## Test Strategy

### Unit Tests

Each component has comprehensive unit tests:

#### allow2_offline_crypto_unittest.cc
```cpp
TEST(Allow2OfflineCryptoTest, GenerateKeyPair)
TEST(Allow2OfflineCryptoTest, LoadKeyPair)
TEST(Allow2OfflineCryptoTest, SignAndVerify)
TEST(Allow2OfflineCryptoTest, SignAndVerify_InvalidSignature)
TEST(Allow2OfflineCryptoTest, ComputeHMAC)
TEST(Allow2OfflineCryptoTest, DeriveKey)
TEST(Allow2OfflineCryptoTest, GetTimeBucket)
TEST(Allow2OfflineCryptoTest, SecureCompare_Equal)
TEST(Allow2OfflineCryptoTest, SecureCompare_NotEqual)
TEST(Allow2OfflineCryptoTest, SecureCompare_DifferentLengths)
```

#### allow2_voice_code_unittest.cc
```cpp
TEST(Allow2VoiceCodeTest, GenerateRequestCode)
TEST(Allow2VoiceCodeTest, ValidateApprovalCode_Valid)
TEST(Allow2VoiceCodeTest, ValidateApprovalCode_Invalid)
TEST(Allow2VoiceCodeTest, ValidateApprovalCode_Expired)
TEST(Allow2VoiceCodeTest, ValidateApprovalCode_Replay)
TEST(Allow2VoiceCodeTest, ParseApprovalCode)
TEST(Allow2VoiceCodeTest, TimeBucketTransition)
```

#### allow2_qr_token_unittest.cc
```cpp
TEST(Allow2QRTokenTest, ParseToken_Valid)
TEST(Allow2QRTokenTest, ParseToken_Invalid)
TEST(Allow2QRTokenTest, ValidateToken_ValidSignature)
TEST(Allow2QRTokenTest, ValidateToken_InvalidSignature)
TEST(Allow2QRTokenTest, ValidateToken_Expired)
TEST(Allow2QRTokenTest, ValidateToken_WrongChild)
TEST(Allow2QRTokenTest, ValidateToken_UnknownSigner)
```

#### allow2_local_decision_unittest.cc
```cpp
TEST(Allow2LocalDecisionTest, EvaluatePermission_Allowed)
TEST(Allow2LocalDecisionTest, EvaluatePermission_QuotaExceeded)
TEST(Allow2LocalDecisionTest, EvaluatePermission_TimeBlock)
TEST(Allow2LocalDecisionTest, EvaluatePermission_Banned)
TEST(Allow2LocalDecisionTest, EvaluatePermission_NoCache)
TEST(Allow2LocalDecisionTest, EvaluatePermission_StaleCache)
TEST(Allow2LocalDecisionTest, RecordUsage)
TEST(Allow2LocalDecisionTest, ApplyGrant)
```

### Integration Tests

```cpp
// allow2_offline_integration_test.cc

// Tests full offline flow:
// 1. Populate cache while online
// 2. Transition to offline
// 3. Make decisions from cache
// 4. Apply voice code grant
// 5. Apply QR token grant
// 6. Track deficit
// 7. Transition back to online
// 8. Verify sync records
TEST_F(Allow2OfflineIntegrationTest, FullOfflineFlow)

// Tests graceful degradation:
// 1. Cache becomes stale
// 2. Verify confidence level drops
// 3. Cache expires
// 4. Verify default deny
TEST_F(Allow2OfflineIntegrationTest, CacheDegradation)

// Tests travel mode:
// 1. Change system timezone
// 2. Verify travel detection
// 3. Verify extended cache request
// 4. Verify parent notification flags
TEST_F(Allow2OfflineIntegrationTest, TravelMode)
```

### Security Tests

```cpp
// allow2_offline_security_unittest.cc

// Verify timing attack protection
TEST(Allow2OfflineSecurityTest, SecureCompare_ConstantTime)

// Verify key storage security
TEST(Allow2OfflineSecurityTest, KeyPairEncryption)

// Verify HMAC cannot be forged
TEST(Allow2OfflineSecurityTest, VoiceCodeForgeryPrevention)

// Verify signature verification
TEST(Allow2OfflineSecurityTest, QRTokenSignatureRequired)

// Verify replay prevention
TEST(Allow2OfflineSecurityTest, VoiceCodeReplayPrevention)

// Verify clock manipulation defense
TEST(Allow2OfflineSecurityTest, ClockManipulationDetection)
```

---

## Implementation Phases

### Phase 1: Core Crypto (Week 1-2)
- `allow2_offline_crypto.h/cc`
- Unit tests
- Integration with OSCrypt

### Phase 2: Voice Code System (Week 2-3)
- `allow2_voice_code.h/cc`
- Unit tests
- UI for code entry (separate task)

### Phase 3: QR Token System (Week 3-4)
- `allow2_qr_token.h/cc`
- Unit tests
- Camera/QR scanner integration (separate task)

### Phase 4: Cache & Decision Engine (Week 4-5)
- `allow2_offline_cache.h/cc`
- `allow2_local_decision.h/cc`
- Unit tests

### Phase 5: Deficit & Travel (Week 5-6)
- `allow2_deficit_tracker.h/cc`
- `allow2_travel_mode.h/cc`
- Unit tests

### Phase 6: Coordinator & Integration (Week 6-7)
- `allow2_offline_coordinator.h/cc`
- Allow2Service integration
- Integration tests

### Phase 7: Security Hardening (Week 7-8)
- Security review
- Penetration testing
- Security tests

---

## Appendix A: Voice Code Examples

| Code | Meaning |
|------|---------|
| `1 1 06 XX` | Grant 30 min internet time |
| `1 3 12 XX` | Grant 60 min gaming time |
| `2 1 00 XX` | Lift internet ban (duration TBD by code) |
| `3 1 04 XX` | Extend bedtime by 20 minutes |

## Appendix B: QR Token Example

```
Base64: eyJ2IjoxLCJ0IjoxLCJjIjoxMDAxLCJhIjoxLCJtIjo2MCwiZSI6MTcwODMwMDAwMCwicyI6IjxzaWc+In0=

Decoded:
{
  "v": 1,           // Version
  "t": 1,           // Type: Extra time
  "c": 1001,        // Child ID
  "a": 1,           // Activity: Internet
  "m": 60,          // Minutes: 60
  "e": 1708300000,  // Expires: Unix timestamp
  "s": "<sig>"      // Ed25519 signature (base64)
}
```

## Appendix C: Cache JSON Structure

```json
{
  "children": {
    "1001": {
      "days": {
        "2026-02-18": {
          "dayTypeId": 1,
          "dayTypeName": "School Day",
          "quotas": {
            "1": {"limit": 60, "used": 30, "bonus": 0},
            "3": {"limit": 30, "used": 0, "bonus": 15}
          },
          "timeBlocks": [
            {"start": "21:00", "end": "07:00", "blocked": [1,3,8,9], "reason": "Bedtime"}
          ],
          "bans": [],
          "fetchedAt": "2026-02-18T10:00:00Z",
          "expiresAt": "2026-02-21T10:00:00Z",
          "signature": "base64-sig"
        }
      },
      "lastSync": "2026-02-18T10:00:00Z",
      "timezone": "Australia/Sydney"
    }
  }
}
```

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-02-18 | System Architect | Initial design document |

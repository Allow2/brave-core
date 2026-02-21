# Allow2 Parental Controls - Developer Guide

## Overview

Allow2 provides parental controls for Brave browser. Parents pair devices with their Allow2 account, then children must authenticate to use the browser with usage tracking and time limits.

## Why Allow2?

### The Problem with Platform-Native Screen Time

Apple Screen Time was previously integrated into Brave but was disabled due to significant performance issues:

- **Performance overhead**: Screen Time APIs caused noticeable lag during browsing
- **Inconsistent behavior**: Different iOS versions had varying reliability
- **Limited cross-platform support**: Screen Time only works on Apple devices

### Allow2 Advantages

| Feature | Allow2 | Apple Screen Time |
|---------|--------|-------------------|
| **Cross-platform** | iOS, Android, Windows, macOS, Linux | Apple devices only |
| **Performance** | Lightweight API calls (10-second intervals) | Heavy system integration |
| **Shared devices** | Child selection shield with PIN | Device-locked |
| **Real-time control** | Parent app approve/deny requests | Delayed sync |
| **Philosophy** | "Parental Freedom" - trust-based | Surveillance-focused |

### Parental Freedom Philosophy

Allow2 operates on a trust-based model rather than surveillance:

- **Transparent**: Children know when they're being managed
- **Collaborative**: Children can request more time with reasons
- **Educational**: Teaches time management, not just enforcement
- **Respectful**: No hidden tracking or keystroke logging
- **Empowering**: Parents set boundaries, children learn within them

---

## Service Lifecycle

```
NOT PAIRED                          PAIRED                           RELEASED
(minimal service)                   (full service)                   (cleanup)
     │                                   │                               │
     │ QR pairing                        │ 401 response                  │
     ▼                                   ▼                               ▼
┌──────────────┐               ┌──────────────────┐              ┌──────────────┐
│ api_client_  │               │ ALL components   │              │ Clear prefs  │
│ credential_  │ ─────────────►│ initialized      │─────────────►│ Destroy      │
│ pairing_     │               │ Timer running    │              │ components   │
└──────────────┘               └──────────────────┘              └──────────────┘
```

### Key Rule: Nothing Runs Unless Paired

- **Not Paired**: Only pairing components exist (api_client, credential_manager, pairing_handler)
- **Paired**: All tracking components are created and active
- **Released (401)**: Everything is destroyed, returns to "not paired" state

---

## Browser Startup Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                     BROWSER STARTUP                              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌─────────────────┐
                    │   IsPaired()?   │
                    └────────┬────────┘
                             │
              ┌──────────────┴──────────────┐
              │ NO                          │ YES
              ▼                             ▼
     ┌────────────────┐           ┌────────────────────┐
     │ Service exists │           │ ShouldShowChild    │
     │ but dormant.   │           │ Shield()?          │
     │ Only pairing   │           └─────────┬──────────┘
     │ available.     │                     │
     └────────────────┘          ┌──────────┴──────────┐
                                 │ YES                 │ NO
                                 ▼                     ▼
                        ┌──────────────┐      ┌──────────────┐
                        │ Show Child   │      │ Normal       │
                        │ Selection    │      │ Browsing     │
                        └──────┬───────┘      │ (tracked)    │
                               │              └──────────────┘
                               ▼
                      Child authenticates
                               │
                               ▼
                      Normal Browsing
```

---

## Pairing Flow

**CRITICAL: Device NEVER handles parent credentials. Authentication happens on the parent's device using passkeys/biometrics.**

### Method 1: QR Code Pairing (Primary)

```
DEVICE                           SERVER                          PARENT'S PHONE
   │                                │                                   │
   │ POST /api/pair/qr/init         │                                   │
   │ { uuid, name, deviceToken }    │                                   │
   │ ───────────────────────────────>│                                   │
   │                                │                                   │
   │ { sessionId, qrCodeUrl }       │                                   │
   │ <───────────────────────────────│                                   │
   │                                │                                   │
   │ Display QR code                │                                   │
   │ ════════════════               │                                   │
   │                                │                                   │
   │ Poll /api/pair/qr/status       │                                   │
   │ ───────────────────────────────>│                                   │
   │                                │      Parent scans QR code         │
   │                                │ <───────────────────────────────────
   │                                │                                   │
   │                                │      Parent auth (Face ID)        │
   │                                │      POST /api/pair/qr/complete   │
   │                                │ <───────────────────────────────────
   │                                │                                   │
   │ { status: "completed",         │                                   │
   │   credentials, children }      │                                   │
   │ <───────────────────────────────│                                   │
```

### Method 2: PIN Code Pairing (Fallback)

```
DEVICE                           SERVER                          PARENT'S PHONE
   │                                │                                   │
   │ POST /api/pair/pin/init        │                                   │
   │ ───────────────────────────────>│                                   │
   │                                │                                   │
   │ { sessionId, pin: "123456" }   │                                   │
   │ <───────────────────────────────│                                   │
   │                                │                                   │
   │ Display PIN: "123456"          │      Parent enters PIN            │
   │ ════════════════════           │      POST /api/pair/pin/verify    │
   │                                │ <───────────────────────────────────
   │                                │                                   │
   │ { status: "completed" }        │                                   │
   │ <───────────────────────────────│                                   │
```

### Pairing UI

```
┌─────────────────────────────────────────────────────────────────┐
│                     Connect to Allow2                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│       ┌─────────────────────────────────────────────┐           │
│       │                                             │           │
│       │              [QR CODE IMAGE]                │           │
│       │                                             │           │
│       └─────────────────────────────────────────────┘           │
│                                                                  │
│            Ask your parent to scan this                         │
│            in their Allow2 app                                  │
│                                                                  │
│  ─────────────────────────────────────────────────────────────  │
│                                                                  │
│            or show code: [ 1 2 3 4 5 6 ]                        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Why not username/password?** Parent credentials should never be entered on the child's device. The QR/PIN approach ensures authentication happens on the parent's trusted phone using secure biometrics.

---

## Child Shield (Shared Device Support)

For devices shared among family members, a selection shield appears on launch:

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│                    Who's using Brave?                            │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                                                             ││
│  │  [QR CODE]    Scan with your Allow2 app                    ││
│  │               to log in instantly                          ││
│  │                                                             ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                  │
│  ─────────────────── or select your profile ──────────────────  │
│                                                                  │
│         ┌─────────┐    ┌─────────┐    ┌─────────┐               │
│         │  Emma   │    │  Jack   │    │  Guest  │               │
│         │  📱     │    │  🔢     │    │  (👤)   │               │
│         └─────────┘    └─────────┘    └─────────┘               │
│                                                                  │
│              📱 = Has app (scan QR)    🔢 = PIN only            │
│                                                                  │
│  ─────────────────────────────────────────────────────────────  │
│                                                                  │
│                    Enter PIN: [• • • •]                          │
│                                                                  │
│              [ 🔓 Ask Parent to Unlock ]                        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### When the Shield Appears

| Trigger | Condition | Action |
|---------|-----------|--------|
| App launch (cold start) | Paired + shared device mode | Show shield |
| Resume from background (>30-60 sec) | Paired + shared device mode | Show shield |
| Explicit "Switch User" | Paired (any mode) | Show shield |
| Session timeout | Configurable by parent | Show shield |
| System sleep/wake | Always | Show shield |
| Screen lock | Always | Show shield |
| App minimize (>60 sec) | Configurable | Show shield |

---

## Child Authentication Methods

When the browser starts on a paired device, the child must authenticate. Multiple methods are available:

```
┌─────────────────────────────────────────────────────────────────┐
│                  "Who's using Brave?"                            │
│                                                                  │
│     [Emma]     [Jack]     [Parent Override]                      │
│                                                                  │
│  After selecting child, authentication options:                  │
│                                                                  │
│  ┌──────────────┬───────────────────────────────────────────┐   │
│  │    METHOD    │  DESCRIPTION                              │   │
│  ├──────────────┼───────────────────────────────────────────┤   │
│  │ PIN          │ Enter 4-digit PIN (works offline)         │   │
│  │ Push Auth    │ Parent confirms in app (requires online)  │   │
│  │ Ask Parent   │ QR/Voice code exchange (works offline)    │   │
│  └──────────────┴───────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### Authentication Flow Decision Tree

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│  Child taps their name on shield                                         │
│                    │                                                     │
│                    ▼                                                     │
│           ┌───────────────┐                                              │
│           │ Has network?  │                                              │
│           └───────┬───────┘                                              │
│                   │                                                      │
│      ┌────────────┼────────────┐                                         │
│      ▼ No                      ▼ Yes                                     │
│  ┌────────┐              ┌──────────────┐                                │
│  │  PIN   │              │ hasAccount?  │                                │
│  │ entry  │              └──────┬───────┘                                │
│  └────────┘                     │                                        │
│                    ┌────────────┼────────────┐                           │
│                    ▼ No                      ▼ Yes                       │
│               ┌────────┐              ┌────────────────┐                 │
│               │  PIN   │              │ Push to child  │                 │
│               │ entry  │              │ (phone/watch)  │                 │
│               └────────┘              └───────┬────────┘                 │
│                                               │                          │
│                                    ┌──────────┼──────────┐               │
│                                    ▼ Timeout              ▼ Confirmed    │
│                               ┌────────┐            ┌──────────┐         │
│                               │  PIN   │            │ Session  │         │
│                               │ entry  │            │ starts   │         │
│                               └────────┘            └──────────┘         │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### Method 1: Push/Socket/Watch (Full Account Children)

Children with their own Allow2 account authenticate via push notification:

```
DEVICE (Brave)                    CHILD'S PHONE/WATCH           SERVER
┌──────────────┐                 ┌───────────────────┐
│              │                 │                   │
│  Who's using │                 │                   │
│    Brave?    │                 │                   │
│              │                 │                   │
│  [Emma] taps │ ───────────────────────────────────────────────>
│              │                 │                   │    Send push
│  "Waiting    │                 │  📱 PUSH:         │    Notify socket
│   for Emma   │                 │  "Family iPad     │
│   to         │                 │   wants to log    │
│   confirm"   │                 │   you in"         │<─────────────
│              │                 │                   │
│  [PIN        │                 │  ⌚ WATCH:        │
│   instead]   │                 │  [Yes] [No]       │
│              │                 │                   │
│              │                 │  Child taps Yes   │
│              │<────────────────────────────────────────────────
│  Logged in   │                 │  ✓ Confirmed      │
│  as Emma!    │                 │                   │
│              │                 │                   │
└──────────────┘                 └───────────────────┘
```

### Method 2: PIN Authentication (Name-Only or Fallback)

For children without accounts, or when push times out, or offline:

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│         ┌─────────┐                                             │
│         │  Jack   │  ← Selected (name-only child)               │
│         │  🔢     │                                             │
│         └─────────┘                                             │
│                                                                  │
│                    Enter PIN: [• • • •]                          │
│                                                                  │
│              [ 🔓 Ask Parent to Unlock ]                        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Security Measures:**
- PIN hashed with SHA-256 + unique salt
- Constant-time comparison prevents timing attacks
- Rate limiting: 5 attempts, then 5-minute lockout
- PIN cached locally for offline validation

### Method 3: Ask Parent (QR/Voice Code)

The "Ask Parent" method works when child and parent are in different locations or offline:

```
┌─────────────────────────────────────────────────────────────────┐
│                 ASK PARENT TO UNLOCK                             │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                                                         │    │
│  │  If parent is HERE:        If parent is REMOTE:         │    │
│  │                                                         │    │
│  │  ┌─────────────────┐       Read this code:              │    │
│  │  │                 │                                    │    │
│  │  │   [QR CODE]     │       ┌─────────────────────┐      │    │
│  │  │                 │       │   4  0  31  57      │      │    │
│  │  │                 │       └─────────────────────┘      │    │
│  │  └─────────────────┘                                    │    │
│  │  Scan with Allow2 app     (Child reads to parent)       │    │
│  │                                                         │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  Enter approval code: [  _  _  _  _  _  _  ]                    │
│                                                                  │
│  [Use PIN instead]                          [Verify]             │
└─────────────────────────────────────────────────────────────────┘
```

### Channel Independence

Request and response channels are **independent and interchangeable**:

| Request Method | Response Method | Scenario |
|----------------|-----------------|----------|
| QR Scan | Push notification | Same room, online |
| QR Scan | Voice (read code) | Same room, offline |
| Voice (read code) | Push notification | Phone call, parent online |
| Voice (read code) | Voice (read code) | Phone call, both offline |

This means:
- Child can **read a request code** to parent over phone
- Parent can **read back an approval code**
- OR parent's app can **push** the approval directly
- The channels are fully independent

---

## Usage Tracking and Blocking

### Usage Tracking

Tracking hooks into the browser's navigation system:

1. **URL Navigation**: Every navigation triggers an Allow2 check
2. **Periodic Timer**: Every 10 seconds, a status check runs
3. **Private Mode**: Excluded from tracking (configurable by parent)

### Block Overlay

When `allowed == false`, a full-screen overlay prevents browsing:

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│                         Time's Up!                               │
│                                                                  │
│              ┌────────────────────────────────┐                  │
│              │                                │                  │
│              │           BLOCKED              │                  │
│              │                                │                  │
│              │   Internet time has ended      │                  │
│              │   for today (School Night)     │                  │
│              │                                │                  │
│              └────────────────────────────────┘                  │
│                                                                  │
│  ┌──────────────────────┐    ┌──────────────────────┐           │
│  │   Request More Time  │    │   Switch User        │           │
│  └──────────────────────┘    └──────────────────────┘           │
│                                                                  │
│                    Why am I blocked?                             │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Countdown Warnings

| Time Remaining | Type | Visual |
|----------------|------|--------|
| 15 minutes | Gentle | Toast (auto-dismiss 5s) |
| 5 minutes | Warning | Persistent toast with progress bar |
| 1 minute | Urgent | Full-width banner with live countdown |
| 0 minutes | Block | Full overlay |

### Session End Triggers

| Trigger | Timeout | Rationale |
|---------|---------|-----------|
| App quit/restart | Immediate | Obvious end |
| System sleep/wake | Immediate | Security |
| Screen lock | Immediate | Security |
| App backgrounded | 30-60 sec | Switched apps |
| Window minimized | 60 sec | Not actively using |
| Network disconnect | 2 min | Can't verify status |
| Idle timeout | 5 min (configurable) | Inactivity |

---

## Component Initialization

### Minimal (Not Paired)

```cpp
// Only these are created:
api_client_        // For pairing API calls
credential_manager_ // For storing credentials after pairing
pairing_handler_   // For QR pairing flow
```

### Full (Paired)

```cpp
// Created by InitializeTrackingComponents():
child_manager_      // Child list management
child_shield_       // Child selection UI controller
usage_tracker_      // Usage tracking and API checks
warning_controller_ // Time warning logic
warning_banner_     // Warning UI
block_overlay_      // Block screen UI
offline_cache_      // Offline settings cache
deficit_tracker_    // Borrowed time tracking
travel_mode_        // Timezone handling
local_decision_     // Offline permission checks
```

---

## API Integration

### API Hosts

| Host | Purpose | Endpoints |
|------|---------|-----------|
| `api.allow2.com` | Device pairing, authentication | `/api/pair/*`, `/api/pairDevice` |
| `service.allow2.com` | Usage logging, permission checks | `/serviceapi/*` |

### Check Endpoint (Usage Tracking)

Called every 10 seconds and on navigation:

```
POST https://service.allow2.com/serviceapi/check

Request:
{
    "userId": "67890",
    "pairId": "12345",
    "pairToken": "token...",
    "childId": "1001",
    "activities": [{ "id": 1, "log": true }],
    "tz": "Australia/Sydney"
}

Response:
{
    "allowed": true,
    "activities": {
        "1": {
            "id": 1,
            "name": "Internet",
            "remaining": 1800,
            "expires": 1699999999,
            "banned": false,
            "timeblock": {
                "allowed": true,
                "ends": 1700006400
            }
        }
    },
    "dayTypes": {
        "today": { "id": 2, "name": "School Night" },
        "tomorrow": { "id": 1, "name": "Weekend" }
    }
}
```

### Activity Types

| Activity ID | Name | Triggered By |
|-------------|------|--------------|
| 1 | Internet | Any web navigation |
| 3 | Gaming | Game-related domains |
| 8 | ScreenTime | Time spent in browser |
| 9 | Social | Social media domains |

### Server-Initiated Release (401 Response)

When a device receives HTTP 401 from any Allow2 API:

1. Clear all stored credentials
2. Clear cached children list
3. Set `isPaired = false`
4. Show user message: "This device is no longer managed by Allow2"

---

## Security Model

1. **Device cannot unpair itself** - Only parent can unpair from Allow2 portal
2. **401 triggers cleanup** - When API returns 401, credentials are cleared
3. **PIN validation is local** - Uses cached hash for offline operation
4. **Voice codes are HMAC-signed** - Cryptographically verified
5. **QR tokens are Ed25519-signed** - Cryptographically verified
6. **Transport security** - All API calls use HTTPS with certificate pinning
7. **No sensitive logging** - PINs, tokens, hashes never appear in logs

### PIN Hashing

**Never store or transmit PINs in plain text.**

```
Server Response:
{
    "children": [
        {
            "id": 1001,
            "name": "Emma",
            "pinHash": "sha256:a1b2c3d4e5f6...",
            "pinSalt": "randomsalt123"
        }
    ]
}

Local Validation:
1. User enters PIN
2. Device computes: SHA256(enteredPIN + pinSalt)
3. Compare result to stored pinHash
```

### Constant-Time Comparison

To prevent timing attacks:

```cpp
bool ConstantTimeCompare(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  uint8_t result = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    result |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
  }
  return result == 0;
}
```

---

## Cross-Platform Credential Storage

| Platform | Non-Sensitive Data | Sensitive Credentials |
|----------|-------------------|----------------------|
| **Windows** | PrefService (JSON file) | OSCrypt -> DPAPI |
| **macOS** | PrefService (JSON file) | OSCrypt -> Keychain |
| **Linux** | PrefService (JSON file) | OSCrypt -> libsecret/Keyring |
| **iOS** | Preferences (UserDefaults) | Keychain (kSecClass) |
| **Android** | SharedPreferences | EncryptedSharedPreferences (AES-256) |

---

## File Structure

### Desktop Files (C++)

```
components/allow2/
├── common/
│   ├── pref_names.h                      # Preference keys
│   ├── allow2_constants.h                # API URLs, activity IDs
│   └── BUILD.gn
├── browser/
│   ├── allow2_service.h/.cc              # Main service singleton
│   ├── allow2_service_factory.h/.cc      # Creates service per profile
│   ├── allow2_credential_manager.h/.cc   # OSCrypt credential storage
│   ├── allow2_api_client.h/.cc           # HTTP client for Allow2 API
│   ├── allow2_child_shield.h/.cc         # Child selection logic
│   ├── allow2_child_manager.h/.cc        # Child list management
│   ├── allow2_usage_tracker.h/.cc        # Usage tracking
│   ├── allow2_warning_controller.h/.cc   # Countdown and warning logic
│   ├── allow2_block_overlay.h/.cc        # Block screen controller
│   ├── allow2_voice_code.h/.cc           # Voice code generation/validation
│   ├── allow2_qr_token.h/.cc             # QR token generation/validation
│   ├── allow2_offline_cache.h/.cc        # Offline settings cache
│   ├── allow2_local_decision.h/.cc       # Offline permission checks
│   ├── allow2_deficit_tracker.h/.cc      # Borrowed time tracking
│   ├── allow2_travel_mode.h/.cc          # Timezone handling
│   └── BUILD.gn
├── docs/
│   └── ALLOW2.md                         # This file
└── resources/
    └── allow2_strings.grdp               # Localized strings

browser/ui/views/allow2/
├── allow2_block_view.h/.cc               # Block overlay (desktop)
├── allow2_child_select_view.h/.cc        # Child selection dialog
├── allow2_warning_banner.h/.cc           # Countdown banner
└── allow2_voice_code_view.h/.cc          # Voice code UI
```

### iOS Files

```
ios/brave-ios/Sources/Brave/Frontend/
├── ParentalFreedom/
│   ├── Allow2Manager.swift               # Singleton, API calls, caching
│   ├── Allow2CredentialManager.swift     # Keychain storage
│   ├── Allow2CheckResult.swift           # Response model
│   ├── Allow2Child.swift                 # Child model with PIN hash
│   ├── Allow2WarningManager.swift        # Countdown and warnings
│   ├── Allow2Preferences.swift           # Preferences extension
│   └── Views/
│       ├── ParentalFreedomSettingsView.swift  # Main settings section
│       ├── PairingView.swift                  # QR/code pairing UI
│       ├── ChildSelectView.swift              # Child selection shield
│       ├── BlockView.swift                    # Block overlay
│       ├── RequestMoreTimeView.swift          # Request form
│       └── WarningToast.swift                 # Countdown toast
```

### Android Files (Kotlin)

```
browser/allow2/
├── Allow2Service.kt                      # Main service
├── Allow2CredentialManager.kt            # EncryptedSharedPreferences
├── Allow2ApiClient.kt                    # Retrofit API client
├── Allow2WarningManager.kt               # Countdown and warnings
└── ui/
    ├── ParentalFreedomSettingsFragment.kt    # Settings section
    ├── ChildSelectBottomSheet.kt             # Child selection UI
    ├── BlockOverlayActivity.kt               # Block overlay
    └── WarningSnackbar.kt                    # Warning notifications
```

---

## Preferences Reference

### Profile Prefs (per-profile)

| Pref | Type | Purpose |
|------|------|---------|
| `kAllow2Enabled` | bool | Is Allow2 enabled for this profile |
| `kAllow2ChildId` | string | Currently selected child ID |
| `kAllow2CachedChildren` | string | JSON list of children |
| `kAllow2Blocked` | bool | Is browsing currently blocked |
| `kAllow2RemainingSeconds` | int | Remaining time in seconds |
| `kAllow2LastCheckTime` | time | Last API check timestamp |
| `kAllow2CachedCheckResult` | string | Cached check response |
| `kAllow2CachedCheckExpiry` | time | Cache expiry time |
| `kAllow2DayTypeToday` | string | Current day type |

### Local State Prefs (browser-wide, encrypted)

| Pref | Type | Purpose |
|------|------|---------|
| `kAllow2Credentials` | string | Encrypted pairing credentials |
| `kAllow2DeviceToken` | string | Unique device identifier |
| `kAllow2OfflineCache` | string | Offline settings cache |

---

## Testing Scenarios

| Scenario | Expected Behavior |
|----------|-------------------|
| **Unpaired States** | |
| First launch, not paired | Settings shows "Parental Freedom" with "Set Up" option |
| Browse while unpaired | Normal browsing, no tracking, no restrictions |
| **Pairing Flow** | |
| Pair via QR code | Scan -> confirm children list -> paired |
| Pair via manual code | Enter 6-digit code -> confirm -> paired |
| **Paired - Shared Device** | |
| Launch after pairing (no child selected) | Show child selection shield |
| Resume from background (>5 min) | Show child selection shield |
| Child enters wrong PIN | Show error, allow retry |
| Child selects correctly | Dismiss shield, start tracking |
| **Tracking & Blocking** | |
| Time runs out mid-session | Show block overlay |
| 15 min remaining | Show gentle toast (auto-dismiss) |
| 5 min remaining | Show persistent toast with countdown |
| 1 min remaining | Show urgent banner with live countdown |
| **Requests** | |
| Child requests more time | Show pending state on block screen |
| Parent approves | Next check() reflects new allowance, dismiss block |
| Parent denies | Block remains, show "Request denied" |
| **Edge Cases** | |
| Device unpaired remotely (401 response) | Clear all credentials, show "No longer managed" |
| Network offline | Use cached check result until expires |
| Private browsing tab | No tracking, no blocking (configurable) |

---

## Common Issues

| Issue | Cause | Fix |
|-------|-------|-----|
| Crash on startup | Prefs not registered | Ensure RegisterLocalStatePrefs is called |
| No child shield | tracking_initialized_ is false | Check IsPaired() returns true |
| Null pointer crash | Component accessed before init | Add null check |
| Timer not running | Not paired or not enabled | Check both conditions |
| 401 not clearing state | OnCredentialsInvalidated not called | Check observer registration |

---

## Implementation Phases

### Phase 1: Core Integration
- Allow2 SDK/Manager implementation
- Preferences storage (encrypted credentials)
- Basic pairing flow (QR + manual code)
- Settings UI entry point ("Parental Freedom")

### Phase 2: Child Shield
- Child selection overlay
- PIN validation (SHA-256 + salt)
- Session management
- "Switch User" flow

### Phase 3: Usage Tracking
- URL tracking hooks (navigation events)
- Periodic check timer (10-second interval)
- Private mode exclusion
- Activity logging (Internet, Social, Gaming)

### Phase 4: Blocking
- Block overlay UI
- Block reason explanation
- "Request More Time" flow
- Day type display (School Night, Weekend)

### Phase 5: Warnings & Countdown
- Warning manager (15min, 5min, 1min)
- Toast notifications
- Live countdown timer

### Phase 6: Offline Auth
- Voice code generation/validation
- QR token generation/validation
- Channel-independent request/response
- Offline cache and local decisions

### Phase 7: Polish
- Animations and transitions
- Localization (i18n)
- Accessibility (VoiceOver, TalkBack)
- Performance optimization

---

## Localization Keys

| Key | English |
|-----|---------|
| `IDS_ALLOW2_SETTINGS_TITLE` | Parental Freedom |
| `IDS_ALLOW2_SETTINGS_SUBTITLE` | Manage screen time with Allow2 |
| `IDS_ALLOW2_PAIR_TITLE` | Connect to Allow2 |
| `IDS_ALLOW2_CHILD_SELECT_TITLE` | Who's using Brave? |
| `IDS_ALLOW2_ENTER_PIN` | Enter PIN |
| `IDS_ALLOW2_BLOCKED_TITLE` | Time's Up! |
| `IDS_ALLOW2_BLOCKED_SUBTITLE` | Internet time has ended |
| `IDS_ALLOW2_REQUEST_MORE_TIME` | Request More Time |
| `IDS_ALLOW2_SWITCH_USER` | Switch User |
| `IDS_ALLOW2_ASK_PARENT` | Ask Parent to Unlock |
| `IDS_ALLOW2_WARNING_15MIN` | 15 minutes of internet time remaining |
| `IDS_ALLOW2_WARNING_5MIN` | Only 5 minutes left! |
| `IDS_ALLOW2_WARNING_1MIN` | Browsing ends in: |
| `IDS_ALLOW2_UNPAIRED` | This device is no longer managed |

---

## References

- Allow2 API Documentation: https://developer.allow2.com
- Allow2 Parent App: iOS App Store / Google Play
- Brave Screen Time Integration (deprecated): `browser/screen_time/`
## Future: Profile Identity Binding

See [PROFILE_IDENTITY_BINDING.md](./PROFILE_IDENTITY_BINDING.md) for the planned capability to bind Allow2 child/parent identities to browser profiles, creating a unified identity layer.

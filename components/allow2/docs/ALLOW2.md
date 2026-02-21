# Allow2 Parental Controls - Developer Guide

## Overview

Allow2 provides parental controls for Brave browser. Parents pair devices with their Allow2 account, then children must authenticate to use the browser with usage tracking and time limits.

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

## Ask Parent Flow (QR/Voice Code)

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

Request and response channels are **independent**:

| Request Method | Response Method | Scenario |
|----------------|-----------------|----------|
| QR Scan | Push notification | Same room, online |
| QR Scan | Voice (read code) | Same room, offline |
| Voice (read code) | Push notification | Phone call, parent online |
| Voice (read code) | Voice (read code) | Phone call, both offline |

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

## Key Files

| File | Purpose |
|------|---------|
| `allow2_service.h/cc` | Main service, lifecycle management |
| `allow2_service_factory.cc` | Creates service per browser profile |
| `allow2_child_shield.h/cc` | Child selection logic |
| `allow2_child_select_view.h/cc` | Child selection UI |
| `allow2_voice_code.h/cc` | Voice code generation/validation |
| `allow2_qr_token.h/cc` | QR token generation/validation |
| `allow2_offline_cache.h/cc` | Offline settings cache |
| `allow2_credential_manager.h/cc` | Secure credential storage |

## Preferences

### Profile Prefs (per-profile)

| Pref | Type | Purpose |
|------|------|---------|
| `kAllow2Enabled` | bool | Is Allow2 enabled for this profile |
| `kAllow2ChildId` | string | Currently selected child ID |
| `kAllow2CachedChildren` | string | JSON list of children |
| `kAllow2Blocked` | bool | Is browsing currently blocked |
| `kAllow2RemainingSeconds` | int | Remaining time in seconds |

### Local State Prefs (browser-wide)

| Pref | Type | Purpose |
|------|------|---------|
| `kAllow2Credentials` | string | Encrypted pairing credentials |
| `kAllow2DeviceToken` | string | Unique device identifier |
| `kAllow2OfflineCache` | string | Offline settings cache |

## Security Model

1. **Device cannot unpair itself** - Only parent can unpair from Allow2 portal
2. **401 triggers cleanup** - When API returns 401, credentials are cleared
3. **PIN validation is local** - Uses cached hash for offline operation
4. **Voice codes are HMAC-signed** - Cryptographically verified
5. **QR tokens are Ed25519-signed** - Cryptographically verified

## Testing

When testing Allow2:

1. **Not Paired State**: Verify only pairing UI is accessible
2. **Pairing**: Use QR code from Allow2 web/app
3. **Child Selection**: Verify shield appears after pairing
4. **Offline Mode**: Disconnect network, verify local decisions work
5. **Release**: Trigger 401 response, verify cleanup

## Common Issues

| Issue | Cause | Fix |
|-------|-------|-----|
| Crash on startup | Prefs not registered | Ensure RegisterLocalStatePrefs is called |
| No child shield | tracking_initialized_ is false | Check IsPaired() returns true |
| Null pointer crash | Component accessed before init | Add null check |
| Timer not running | Not paired or not enabled | Check both conditions |

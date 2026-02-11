# Allow2 Parental Controls Integration for Brave Browser

This document describes the Allow2 parental controls integration architecture for brave-core across all platforms (iOS, Android, Windows, macOS, Linux).

---

## 1. Why Allow2?

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

## 2. Implementation Architecture

### Entry Point: Settings > "Parental Freedom"

The Allow2 integration appears as a top-level settings section, not buried under Privacy:

```
Settings
├── Shields & Privacy
├── Search
├── Web3
├── Parental Freedom     <-- NEW (top-level visibility)
├── Appearance
├── Downloads
└── ...
```

**Rationale**: Parents searching for controls should find them immediately. "Parental Freedom" emphasizes the philosophy rather than using restrictive terminology.

### Pairing Flow

**CRITICAL: Device NEVER handles parent credentials. Authentication happens on the parent's device using passkeys/biometrics.**

#### Method 1: QR Code Pairing (Primary)

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
   │                                │      GET /api/pair/qr/options     │
   │                                │ <───────────────────────────────────
   │                                │                                   │
   │                                │      { challenge }                 │
   │                                │ ─────────────────────────────────>│
   │                                │                                   │
   │                                │      Parent auth (Face ID)        │
   │                                │      POST /api/pair/qr/complete   │
   │                                │ <───────────────────────────────────
   │                                │                                   │
   │ { status: "completed",         │                                   │
   │   credentials, children }      │                                   │
   │ <───────────────────────────────│                                   │
   │                                │                                   │
   │ Store credentials              │                                   │
   │ ════════════════               │                                   │
```

#### Method 2: PIN Code Pairing (Fallback)

```
DEVICE                           SERVER                          PARENT'S PHONE
   │                                │                                   │
   │ POST /api/pair/pin/init        │                                   │
   │ { uuid, name, deviceToken }    │                                   │
   │ ───────────────────────────────>│                                   │
   │                                │                                   │
   │ { sessionId, pin: "123456" }   │                                   │
   │ <───────────────────────────────│                                   │
   │                                │                                   │
   │ Display PIN: "123456"          │                                   │
   │ ════════════════════           │                                   │
   │                                │                                   │
   │ Poll status...                 │      Parent enters PIN            │
   │                                │      POST /api/pair/pin/verify    │
   │                                │ <───────────────────────────────────
   │                                │                                   │
   │ { status: "completed" }        │                                   │
   │ <───────────────────────────────│                                   │
```

#### Pairing UI

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

#### Method 3: Child-Initiated Self-Enrollment (Trust-Based)

Children can proactively enroll their own devices using their Allow2 app:

```
CHILD'S NEW DEVICE                 CHILD'S PHONE (Allow2 App)           PARENT
────────────────────               ──────────────────────────           ──────

Opens Brave Settings
→ Parental Freedom
→ Shows QR code

        ──────────────────────────>
                                   Child scans QR with their app

                                   App detects account type and
                                   shows appropriate dialog...
```

**Scenario A: Account has NO parents (user IS a parent)**

```
                                   ┌─────────────────────────────┐
                                   │ Add Device                  │
                                   │                             │
                                   │ "School Laptop - Brave"     │
                                   │                             │
                                   │ This device will be added   │
                                   │ to your account.            │
                                   │                             │
                                   │     [Cancel]    [Add]       │
                                   └─────────────────────────────┘
```

Simple flow - device is added directly to the user's account.

**Scenario B: Account has at least one parent (child of 1+ families)**

```
                                   ┌─────────────────────────────────────┐
                                   │ Add Device to Family                │
                                   │                                     │
                                   │ "School Laptop - Brave"             │
                                   │                                     │
                                   │ Which family should manage          │
                                   │ this device?                        │
                                   │                                     │
                                   │ ┌─────────────────────────────────┐ │
                                   │ │ 👨‍👩‍👧 Add to Bob's Family          │ │
                                   │ └─────────────────────────────────┘ │
                                   │                                     │
                                   │ ┌─────────────────────────────────┐ │
                                   │ │ 👩‍👧 Add to Mary's Family          │ │
                                   │ └─────────────────────────────────┘ │
                                   │                                     │
                                   │          [Cancel]                   │
                                   │                                     │
                                   │ ▼ Advanced                          │
                                   └─────────────────────────────────────┘

    User taps "Advanced":

                                   ┌─────────────────────────────────────┐
                                   │ Add Device to Family                │
                                   │                                     │
                                   │ "School Laptop - Brave"             │
                                   │                                     │
                                   │ ┌─────────────────────────────────┐ │
                                   │ │ 👨‍👩‍👧 Add to Bob's Family          │ │
                                   │ └─────────────────────────────────┘ │
                                   │                                     │
                                   │ ┌─────────────────────────────────┐ │
                                   │ │ 👩‍👧 Add to Mary's Family          │ │
                                   │ └─────────────────────────────────┘ │
                                   │                                     │
                                   │ ┌─────────────────────────────────┐ │
                                   │ │ ⚠️ Add to MY account             │ │
                                   │ │ (Not managed by your parents)   │ │
                                   │ └─────────────────────────────────┘ │
                                   │                                     │
                                   │          [Cancel]                   │
                                   └─────────────────────────────────────┘
```

**Behavior:**

1. **Account with NO parents**: User sees simple "Add" dialog. Device is added to their account directly.

2. **Account with 1+ parents**:
   - Shows family selection dialog with parent family options
   - "Add to [Parent's Name]'s Family" buttons for each parent
   - Cancel button always visible
   - "Advanced" expandable section reveals:
     - "Add to MY account" option with warning that device won't be managed by any parents

3. **After selection**:
   - Device receives credentials for the chosen family/account
   - Parent receives push notification: "[Child] added '[Device Name]'"
   - If added to own account, no notification to parents (independent)

**Benefits:**
- **Zero parent overhead** - children manage their own device enrollment
- **Trust-based** - aligns with "Parental Freedom" philosophy
- **Scales** - works for multiple devices without parent involvement
- **Child buy-in** - voluntary enrollment reduces resistance
- **Multi-family support** - children of divorced parents can choose which family manages each device
- **Independence option** - older children can add devices to their own account when appropriate

**Server Requirements:**
- Child accounts can initiate pairing for their family
- Server returns list of parent accounts for multi-family scenarios
- Parent receives push notification when child adds device (except for own account)
- Optional: configurable "require parent approval" setting

**When to use:**
- Child gets new phone, tablet, laptop
- Installing Brave on school computer
- Any device child wants managed under family rules
- Teen setting up independent devices for themselves

### Child Shield (Shared Device Support)

For devices shared among family members, a selection shield appears on launch:

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│                    Who's using Brave?                            │
│                                                                  │
│         ┌─────────┐    ┌─────────┐    ┌─────────┐               │
│         │  Emma   │    │  Jack   │    │  Guest  │               │
│         │  (👧)   │    │  (👦)   │    │  (👤)   │               │
│         └─────────┘    └─────────┘    └─────────┘               │
│                                                                  │
│  ─────────────────────────────────────────────────────────────  │
│                                                                  │
│                    Enter PIN: [• • • •]                          │
│                                                                  │
│         This helps track time limits and keep you safe          │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**When the shield appears:**

| Trigger | Condition | Action |
|---------|-----------|--------|
| App launch (cold start) | Paired + shared device mode | Show shield |
| Resume from background (>5 min) | Paired + shared device mode | Show shield |
| Explicit "Switch User" | Paired (any mode) | Show shield |
| Session timeout | Configurable by parent | Show shield |

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

### Cross-Platform Credential Storage

| Platform | Non-Sensitive Data | Sensitive Credentials |
|----------|-------------------|----------------------|
| **Windows** | PrefService (JSON file) | OSCrypt -> DPAPI |
| **macOS** | PrefService (JSON file) | OSCrypt -> Keychain |
| **Linux** | PrefService (JSON file) | OSCrypt -> libsecret/Keyring |
| **iOS** | Preferences (UserDefaults) | Keychain (kSecClass) |
| **Android** | SharedPreferences | EncryptedSharedPreferences (AES-256) |

---

## 3. Security Rationale

### PIN Hashing

**Never store or transmit PINs in plain text.**

The server sends SHA-256 hashes with salt; the device validates locally:

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

To prevent timing attacks that could leak PIN information:

```swift
func constantTimeCompare(a: String, b: String) -> Bool {
    guard a.count == b.count else { return false }
    var result: UInt8 = 0
    for (charA, charB) in zip(a.utf8, b.utf8) {
        result |= charA ^ charB
    }
    return result == 0
}
```

### No Device-Initiated Unpair

**Critical security feature**: The device cannot unpair itself.

- Unpairing is ONLY possible from the parent's Allow2 portal/app
- When unpairing occurs remotely, the next `check()` call receives HTTP 401
- Device then clears local credentials and shows "No longer managed"

This prevents children from simply removing the app or resetting settings.

### Transport Security

- All API calls use HTTPS
- Certificate pinning prevents MITM attacks
- API tokens stored encrypted per platform (see Credential Storage above)

### Private Browsing Behavior

- Private tabs are never tracked (no URLs logged)
- Blocking behavior in private mode is configurable by parent:
  - Allow private browsing normally
  - Block private browsing entirely
  - Time-limited private browsing

### Additional Security Measures

1. **No sensitive logging**: PINs, tokens, hashes never appear in logs or crash reports
2. **Cache expiry**: Cached check results expire per server TTL; stale cache requires re-check
3. **Bypass-resistant overlay**: Platform modal prevents interaction with underlying content
4. **Reinstall protection**: Device fingerprinting prevents pairing bypass (requires Allow2 backend)

---

## 4. API Integration

### API Hosts

Allow2 uses two API hosts for different purposes:

| Host | Purpose | Endpoints |
|------|---------|-----------|
| `api.allow2.com` | Device pairing, authentication | `/api/pair/*`, `/api/pairDevice` |
| `service.allow2.com` | Usage logging, permission checks | `/serviceapi/*` |

The serviceapi endpoints use `service.allow2.com` directly for performance.

### QR Pairing Endpoints

#### Initialize QR Session (Device calls this)
```
POST https://api.allow2.com/api/pair/qr/init

Request:
{
    "uuid": "device-uuid-xxx",
    "name": "Family iPad - Brave Browser",
    "platform": "iOS",
    "deviceToken": "BRAVE_DEVICE_TOKEN_XXX"
}

Response:
{
    "status": "success",
    "sessionId": "abc123-session-id",
    "qrCodeUrl": "allow2://pair?s=abc123-session-id",
    "expiresIn": 300
}
```

#### Poll Status (Device polls this)
```
GET https://api.allow2.com/api/pair/qr/status/:sessionId

Response (waiting):
{ "status": "pending", "scanned": false }

Response (parent scanned):
{ "status": "pending", "scanned": true }

Response (completed):
{
    "status": "completed",
    "credentials": {
        "userId": "67890",
        "pairId": "12345",
        "pairToken": "token..."
    },
    "children": [
        { "id": 1001, "name": "Emma", "pinHash": "sha256:...", "pinSalt": "..." },
        { "id": 1002, "name": "Jack", "pinHash": "sha256:...", "pinSalt": "..." }
    ]
}
```

### PIN Pairing Endpoints

#### Initialize PIN Session (Device calls this)
```
POST https://api.allow2.com/api/pair/pin/init

Request:
{
    "uuid": "device-uuid-xxx",
    "name": "Family iPad - Brave Browser",
    "platform": "iOS",
    "deviceToken": "BRAVE_DEVICE_TOKEN_XXX"
}

Response:
{
    "status": "success",
    "sessionId": "def456-session-id",
    "pin": "123456",
    "expiresIn": 300
}
```

### Check Endpoint (Usage Tracking)

Called every 10 seconds and on navigation. Uses `service.allow2.com` directly.

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
            "remaining": 1800,           // seconds remaining
            "expires": 1699999999,       // cache expiry timestamp
            "banned": false,
            "timeblock": {
                "allowed": true,
                "ends": 1700006400       // when scheduled block starts
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

### Request Endpoint (Request More Time)

```
POST https://api.allow2.com/request/createRequest

Request:
{
    "userId": "67890",
    "pairId": "12345",
    "pairToken": "token...",
    "childId": "1001",
    "activityId": 1,
    "requestedMinutes": 30,
    "message": "I need to finish my homework"
}
```

Parent receives push notification, approves/denies in Allow2 app. Next `check()` call reflects the updated allowance.

### Server-Initiated Release (401 Response)

When a device receives HTTP 401 from any Allow2 API:

1. Clear all stored credentials
2. Clear cached children list
3. Set `isPaired = false`
4. Show user message: "This device is no longer managed by Allow2"

This enables parents to release devices remotely without requiring device access.

---

## 5. File Structure

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
├── Browser/
│   └── BrowserViewController/
│       └── BVC+ParentalFreedom.swift     # BVC extension for tracking/blocking
└── Settings/
    └── Features/
        └── ParentalFreedom/
            └── ParentalFreedomSectionView.swift  # Top-level settings entry
```

### Desktop Files (C++)

```
components/allow2/
├── common/
│   ├── pref_names.h                      # Preference keys
│   ├── allow2_constants.h                # API URLs, activity IDs
│   └── BUILD.gn
├── browser/
│   ├── allow2_service.h/.cc              # Main service singleton
│   ├── allow2_credential_manager.h/.cc   # OSCrypt credential storage
│   ├── allow2_api_client.h/.cc           # HTTP client for Allow2 API
│   ├── allow2_warning_controller.h/.cc   # Countdown and warning logic
│   └── BUILD.gn
└── resources/
    └── allow2_strings.grdp               # Localized strings

browser/ui/
├── views/allow2/
│   ├── allow2_block_view.h/.cc           # Block overlay (desktop)
│   ├── allow2_child_select_view.h/.cc    # Child selection dialog
│   └── allow2_warning_banner.h/.cc       # Countdown banner
└── webui/settings/
    └── brave_parental_freedom_handler.h/.cc  # Settings page handler
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

## 6. Implementation Phases

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
- Time block alerts
- Sound effects (optional)

### Phase 6: Polish
- Animations and transitions
- Localization (i18n)
- Accessibility (VoiceOver, TalkBack)
- Performance optimization
- Offline handling (cached results)

---

## 7. Testing Scenarios

| Scenario | Expected Behavior |
|----------|-------------------|
| **Unpaired States** | |
| First launch, not paired | Settings shows "Parental Freedom" with "Set Up" option |
| Browse while unpaired | Normal browsing, no tracking, no restrictions |
| **Pairing Flow** | |
| Pair via QR code | Scan -> confirm children list -> paired |
| Pair via manual code | Enter 6-digit code -> confirm -> paired |
| Pair fails (wrong password) | Show error, allow retry |
| **Paired - Shared Device** | |
| Launch after pairing (no child selected) | Show child selection shield |
| Resume from background (>5 min) | Show child selection shield |
| Child enters wrong PIN | Show error, allow retry |
| Child selects correctly | Dismiss shield, start tracking |
| **Paired - Locked to Child** | |
| Launch with childId set | Skip shield, start tracking immediately |
| **Tracking & Blocking** | |
| Time runs out mid-session | Show block overlay |
| Scheduled time block starts | Show block overlay with schedule info |
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
| "Switch User" from settings | Show child shield regardless of current state |

---

## 8. Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                           BRAVE BROWSER                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────┐    ┌──────────────────┐    ┌─────────────────┐    │
│  │   Settings  │───>│   Allow2Manager   │<───│ BrowserViewCtrl │    │
│  │   (Pair)    │    │   (Singleton)     │    │   (Track/Block) │    │
│  └─────────────┘    └────────┬─────────┘    └─────────────────┘    │
│                              │                                       │
│         ┌────────────────────┼────────────────────┐                 │
│         │                    │                    │                 │
│         v                    v                    v                 │
│  ┌─────────────┐    ┌──────────────┐    ┌─────────────────┐        │
│  │   Local     │    │   Warning    │    │   Block         │        │
│  │   Cache     │    │   Manager    │    │   Overlay       │        │
│  └─────────────┘    └──────────────┘    └─────────────────┘        │
│                                                                      │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               │ HTTPS (every 10s or on navigation)
                               v
┌─────────────────────────────────────────────────────────────────────┐
│                        ALLOW2 API                                    │
├─────────────────────────────────────────────────────────────────────┤
│  POST /api/pairDevice         - Device pairing                      │
│  POST /serviceapi/check       - Usage tracking & status             │
│  POST /request/createRequest  - Request more time                   │
│                                                                      │
│  401 Response                 - Remote unpair trigger               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 9. Preference Keys Reference

### Desktop (C++ PrefService)

```cpp
namespace allow2::prefs {

// Non-sensitive (stored in profile prefs)
inline constexpr char kAllow2Enabled[] = "brave.allow2.enabled";
inline constexpr char kAllow2ChildId[] = "brave.allow2.child_id";
inline constexpr char kAllow2LastCheckTime[] = "brave.allow2.last_check";
inline constexpr char kAllow2CachedChildren[] = "brave.allow2.children";

// Sensitive (encrypted via OSCrypt, stored in local_state)
inline constexpr char kAllow2Credentials[] = "brave.allow2.credentials";

}  // namespace allow2::prefs
```

### iOS (Swift Preferences)

```swift
extension Preferences {
    final class Allow2 {
        static let isEnabled = Option<Bool>(key: "allow2.enabled", default: true)
        static let childId = Option<String?>(key: "allow2.childId", default: nil)
        static let cachedChildren = Option<Data?>(key: "allow2.children", default: nil)
        // Credentials stored separately in Keychain
    }
}
```

### Android (SharedPreferences)

```kotlin
// Regular SharedPreferences
const val PREF_ALLOW2_ENABLED = "allow2.enabled"
const val PREF_ALLOW2_CHILD_ID = "allow2.child_id"
const val PREF_ALLOW2_CHILDREN = "allow2.children"

// EncryptedSharedPreferences (separate file)
const val SECURE_PREF_USER_ID = "userId"
const val SECURE_PREF_PAIR_ID = "pairId"
const val SECURE_PREF_PAIR_TOKEN = "pairToken"
```

---

## 10. Localization Keys

Key strings requiring translation:

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
| `IDS_ALLOW2_WARNING_15MIN` | 15 minutes of internet time remaining |
| `IDS_ALLOW2_WARNING_5MIN` | Only 5 minutes left! |
| `IDS_ALLOW2_WARNING_1MIN` | Browsing ends in: |
| `IDS_ALLOW2_UNPAIRED` | This device is no longer managed |

---

## References

- Allow2 API Documentation: https://developer.allow2.com
- Allow2 Parent App: iOS App Store / Google Play
- Brave Screen Time Integration (deprecated): `browser/screen_time/`

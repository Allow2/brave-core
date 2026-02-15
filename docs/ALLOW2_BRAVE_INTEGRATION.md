# Allow2 Integration Architecture for brave-core

## Overview

This document outlines how Allow2 parental controls integrate into modern brave-core across all platforms (iOS, Android, Desktop). The design follows existing Brave patterns (similar to Screen Time integration) while adding Allow2-specific features.

---

## 1. PAIRING FLOW

### Entry Point: Settings > "Parental Freedom"

A top-level settings section (not buried under Privacy) - visible and accessible:

```
┌─────────────────────────────────────────┐
│  Settings                               │
├─────────────────────────────────────────┤
│  🔒 Shields & Privacy                   │
│  🔍 Search                              │
│  🌐 Web3                                │
│  👨‍👩‍👧‍👦 Parental Freedom     ← NEW      │
│  📱 Appearance                          │
│  ⬇️  Downloads                          │
│  ...                                    │
└─────────────────────────────────────────┘
```

**Location in brave-core:**
- iOS: `Sources/Brave/Frontend/Settings/Features/ParentalFreedom/`
- Desktop: `browser/ui/webui/settings/brave_parental_freedom_handler.cc`
- Android: `browser/settings/parental_freedom/`

### Pairing Methods

**IMPORTANT: Device NEVER handles parent credentials. Parent authenticates on their own device (Allow2 app) using passkey/biometrics.**

```
┌─────────────────────────────────────────────────────────────────┐
│                     Connect to Allow2                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐           ┌──────────────┐                    │
│  │   Show QR    │           │   Show PIN   │                    │
│  │    Code      │           │    Code      │                    │
│  └──────────────┘           └──────────────┘                    │
│                                                                  │
│  Ask your parent to scan this QR code or enter the PIN          │
│  in their Allow2 app to pair this device                        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

#### Method 1: QR Code Pairing (Recommended)

```
Device Flow:
1. Device generates deviceToken (UUID)
2. POST /api/pair/qr/init { uuid, name, platform, deviceToken }
3. Server returns { sessionId, qrCodeUrl, expiresIn }
4. Device displays QR code containing sessionId
5. Device polls GET /api/pair/qr/status/:sessionId every 2s

Parent Flow (in Allow2 app):
1. Parent opens Allow2 app, taps "Pair Device"
2. Parent scans QR code with phone camera
3. App calls GET /api/pair/qr/options/:sessionId
4. Parent authenticates with passkey (Face ID / fingerprint)
5. App calls POST /api/pair/qr/complete { sessionId, credential }

Completion:
- Device's next poll returns { status: "completed", credentials: {...} }
- Device stores credentials securely
```

#### Method 2: PIN Code Pairing (Fallback)

```
Device Flow:
1. Device generates deviceToken (UUID)
2. POST /api/pair/pin/init { uuid, name, platform, deviceToken }
3. Server returns { sessionId, pin, expiresIn }
4. Device displays 6-digit PIN: "123456"
5. Device polls GET /api/pair/qr/status/:sessionId every 2s

Parent Flow (in Allow2 app):
1. Parent opens Allow2 app, taps "Enter Code"
2. Parent types the 6-digit PIN shown on device
3. App calls POST /api/pair/pin/options to get passkey challenge
4. Parent authenticates with passkey (Face ID / fingerprint)
5. App calls POST /api/pair/pin/verify { pin, challengeId, credential }

Completion:
- Device's next poll returns { status: "completed", credentials: {...} }
- Device stores credentials securely
```

#### Why NOT Username/Password?

| Approach | Security Risk |
|----------|---------------|
| Parent enters password on child's device | Password could be intercepted, logged, or shoulder-surfed |
| QR/PIN + passkey on parent's device | Passkey never leaves parent's device, biometric-protected |

**The device only displays a code. Authentication happens on the parent's trusted device.**

### Pairing Data Storage (Cross-Platform)

Brave is a multi-platform project (iOS, Android, Windows, macOS, Linux). Storage must use Chromium's cross-platform abstractions:

#### Desktop (C++) - Using PrefService + OSCrypt

```cpp
// File: components/allow2/common/pref_names.h

namespace allow2 {
namespace prefs {

// Non-sensitive settings (stored in profile prefs)
inline constexpr char kAllow2Enabled[] = "brave.allow2.enabled";
inline constexpr char kAllow2ChildId[] = "brave.allow2.child_id";  // empty = shared device
inline constexpr char kAllow2LastCheckTime[] = "brave.allow2.last_check";
inline constexpr char kAllow2CachedChildren[] = "brave.allow2.children";  // JSON array

// Sensitive credentials (encrypted via OSCrypt, stored in local_state)
inline constexpr char kAllow2Credentials[] = "brave.allow2.credentials";
// Structure: { "userId": "...", "pairId": "...", "pairToken": "..." }

}  // namespace prefs
}  // namespace allow2


// File: components/allow2/browser/allow2_credential_manager.cc

#include "components/os_crypt/sync/os_crypt.h"

class Allow2CredentialManager {
 public:
  // Store encrypted credentials
  bool StoreCredentials(const std::string& user_id,
                        const std::string& pair_id,
                        const std::string& pair_token) {
    base::Value::Dict creds;
    creds.Set("userId", user_id);
    creds.Set("pairId", pair_id);
    creds.Set("pairToken", pair_token);

    std::string json = base::WriteJson(creds).value_or("");
    std::string encrypted;

    // OSCrypt uses platform-specific encryption:
    // - macOS: Keychain
    // - Windows: DPAPI
    // - Linux: libsecret/GNOME Keyring/KWallet
    if (!OSCrypt::EncryptString(json, &encrypted)) {
      return false;
    }

    local_state_->SetString(prefs::kAllow2Credentials,
                            base::Base64Encode(encrypted));
    return true;
  }

  // Retrieve and decrypt credentials
  std::optional<Credentials> GetCredentials() {
    std::string encrypted_b64 =
        local_state_->GetString(prefs::kAllow2Credentials);
    if (encrypted_b64.empty()) return std::nullopt;

    std::string encrypted;
    if (!base::Base64Decode(encrypted_b64, &encrypted)) {
      return std::nullopt;
    }

    std::string json;
    if (!OSCrypt::DecryptString(encrypted, &json)) {
      return std::nullopt;
    }

    auto parsed = base::JSONReader::Read(json);
    // ... parse and return
  }
};
```

#### iOS (Swift) - Using Brave's Preferences + Keychain

```swift
// File: Sources/Brave/Frontend/Allow2/Allow2Preferences.swift

import Preferences
import Security

extension Preferences {
    final class Allow2 {
        // Non-sensitive (UserDefaults via Preferences)
        static let isEnabled = Option<Bool>(key: "allow2.enabled", default: true)
        static let childId = Option<String?>(key: "allow2.childId", default: nil)
        static let cachedChildren = Option<Data?>(key: "allow2.children", default: nil)

        // Sensitive credentials - use Keychain directly
        static var credentials: Allow2Credentials? {
            get {
                guard let data = KeychainHelper.load(key: "allow2.credentials") else {
                    return nil
                }
                return try? JSONDecoder().decode(Allow2Credentials.self, from: data)
            }
            set {
                if let newValue = newValue,
                   let data = try? JSONEncoder().encode(newValue) {
                    KeychainHelper.save(key: "allow2.credentials", data: data)
                } else {
                    KeychainHelper.delete(key: "allow2.credentials")
                }
            }
        }
    }
}

struct Allow2Credentials: Codable {
    let userId: String
    let pairId: String
    let pairToken: String
}

// Simple Keychain wrapper
class KeychainHelper {
    static func save(key: String, data: Data) {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "com.brave.allow2",
            kSecAttrAccount as String: key,
            kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlock
        ]
        SecItemDelete(query as CFDictionary)
        SecItemAdd(query as CFDictionary, nil)
    }

    static func load(key: String) -> Data? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "com.brave.allow2",
            kSecAttrAccount as String: key,
            kSecReturnData as String: true
        ]
        var result: AnyObject?
        SecItemCopyMatching(query as CFDictionary, &result)
        return result as? Data
    }

    static func delete(key: String) {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "com.brave.allow2",
            kSecAttrAccount as String: key
        ]
        SecItemDelete(query as CFDictionary)
    }
}
```

#### Android (Kotlin) - Using EncryptedSharedPreferences

```kotlin
// File: browser/allow2/Allow2CredentialManager.kt

import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

class Allow2CredentialManager(context: Context) {

    private val masterKey = MasterKey.Builder(context)
        .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
        .build()

    private val securePrefs = EncryptedSharedPreferences.create(
        context,
        "allow2_secure_prefs",
        masterKey,
        EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
        EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
    )

    fun storeCredentials(userId: String, pairId: String, pairToken: String) {
        securePrefs.edit()
            .putString("userId", userId)
            .putString("pairId", pairId)
            .putString("pairToken", pairToken)
            .apply()
    }

    fun getCredentials(): Credentials? {
        val userId = securePrefs.getString("userId", null) ?: return null
        val pairId = securePrefs.getString("pairId", null) ?: return null
        val pairToken = securePrefs.getString("pairToken", null) ?: return null
        return Credentials(userId, pairId, pairToken)
    }

    fun clearCredentials() {
        securePrefs.edit().clear().apply()
    }
}
```

#### Platform Summary

| Platform | Non-Sensitive | Sensitive Credentials |
|----------|---------------|----------------------|
| **Windows** | PrefService (JSON file) | OSCrypt → DPAPI |
| **macOS** | PrefService (JSON file) | OSCrypt → Keychain |
| **Linux** | PrefService (JSON file) | OSCrypt → libsecret/Keyring |
| **iOS** | Preferences (UserDefaults) | Keychain (kSecClass) |
| **Android** | SharedPreferences | EncryptedSharedPreferences (AES-256) |

### API Endpoints

#### QR Pairing Init (Device → Server)
```
POST https://api.allow2.com/api/pair/qr/init
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

#### QR Status Poll (Device → Server)
```
GET https://api.allow2.com/api/pair/qr/status/:sessionId

Response (waiting):
{
    "status": "pending",
    "scanned": false
}

Response (parent scanned):
{
    "status": "pending",
    "scanned": true
}

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

#### PIN Pairing Init (Device → Server)
```
POST https://api.allow2.com/api/pair/pin/init
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

---

## 2. CHILD SHIELD / SELECTION (Shared Device)

When `childId` is nil (shared device mode), show a shield on app launch or resume:

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│                    🛡️ Who's using Brave?                         │
│                                                                  │
│         ┌─────────┐    ┌─────────┐    ┌─────────┐               │
│         │  Emma   │    │  Jack   │    │  Guest  │               │
│         │  👧     │    │  👦     │    │  👤     │               │
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

### When to Show Shield

**Prerequisites:** Device must be paired (`isPaired == true`) AND not locked to a specific child (`childId == nil`)

| Trigger | Condition | Action |
|---------|-----------|--------|
| App launch (cold start) | Paired + shared device | Show shield |
| Resume from background (>5 min) | Paired + shared device | Show shield |
| Explicit "Switch User" action | Paired (any mode) | Show shield |
| Session timeout (configurable) | Paired + shared device | Show shield |

**Decision Flow:**
```
App Launch
    │
    ├─── Not paired? ──────────────────────► Normal browsing (no Allow2)
    │
    ├─── Paired + childId set? ────────────► Skip shield, start tracking
    │
    └─── Paired + childId nil (shared)? ───► Show child shield
```

### PIN Security

**Problem:** PINs are sensitive credentials that shouldn't be stored or transmitted in plain text.

**Solution:** Server provides PIN hashes; device validates locally by hashing input.

#### API Response (Updated)

```json
{
    "children": [
        {
            "id": 1001,
            "name": "Emma",
            "pinHash": "sha256:a1b2c3d4e5f6...",  // NOT plain PIN
            "pinSalt": "randomsalt123"
        }
    ]
}
```

#### Local Validation

```swift
import CryptoKit

struct Allow2Child: Codable {
    let id: UInt64
    let name: String
    let pinHash: String    // "sha256:..." or "bcrypt:..."
    let pinSalt: String
}

func validateChildPin(child: Allow2Child, enteredPin: String) -> Bool {
    // Hash the entered PIN with the same salt
    let saltedPin = enteredPin + child.pinSalt
    let inputHash = SHA256.hash(data: Data(saltedPin.utf8))
    let inputHashString = "sha256:" + inputHash.compactMap { String(format: "%02x", $0) }.joined()

    // Constant-time comparison to prevent timing attacks
    return inputHashString.constantTimeCompare(to: child.pinHash)
}

extension String {
    func constantTimeCompare(to other: String) -> Bool {
        guard self.count == other.count else { return false }
        var result: UInt8 = 0
        for (a, b) in zip(self.utf8, other.utf8) {
            result |= a ^ b
        }
        return result == 0
    }
}
```

#### Server-Side (Allow2 API Change Required)

```javascript
// When setting/updating child PIN
const crypto = require('crypto');

function hashPin(pin) {
    const salt = crypto.randomBytes(16).toString('hex');
    const hash = crypto.createHash('sha256')
        .update(pin + salt)
        .digest('hex');
    return { pinHash: `sha256:${hash}`, pinSalt: salt };
}

// When returning children in /check or /pairDevice response
children.map(child => ({
    id: child.id,
    name: child.name,
    pinHash: child.pinHash,   // Pre-computed hash
    pinSalt: child.pinSalt    // Salt used for hashing
}));
```

#### Why Not Online Validation?

| Approach | Pros | Cons |
|----------|------|------|
| **Local hash validation** | Works offline, instant, no network latency | Requires API change to send hashes |
| **Online PIN check** | No storage of PIN data on device | Requires network, adds latency, fails offline |

**Recommendation:** Local hash validation with SHA-256. For higher security (bcrypt), online validation may be preferred but impacts UX when offline.

---

## 3. USAGE TRACKING

### Hook Points (Following Screen Time Pattern)

**iOS - BrowserViewController Extensions:**

```swift
// File: BVC+Allow2.swift

extension BrowserViewController {

    /// Called on every URL navigation
    func trackAllow2Usage(url: URL?) {
        guard Allow2.shared.isPaired,
              Preferences.Allow2.isEnabled.value,
              let url = url,
              url.scheme == "http" || url.scheme == "https" else {
            return
        }

        // Don't track private browsing
        guard !(tabManager.selectedTab?.isPrivate ?? true) else {
            return
        }

        // Log browsing activity
        Allow2.shared.check(
            activities: [
                Allow2.Allow2Activity(activity: .Internet, log: true)
            ]
        ) { response in
            self.handleAllow2Response(response)
        }
    }

    /// Called periodically (every 10 seconds) while browsing
    func startAllow2Timer() {
        allow2Timer = Timer.scheduledTimer(
            timeInterval: 10.0,
            target: self,
            selector: #selector(checkAllow2Status),
            userInfo: nil,
            repeats: true
        )
    }
}
```

### Activity Types

| Activity ID | Name | When Logged |
|-------------|------|-------------|
| 1 | Internet | Any web navigation |
| 8 | ScreenTime | Time spent in browser |
| 9 | Social | Social media domains |
| 3 | Gaming | Game-related sites |

### Domain Classification (Optional Enhancement)

```swift
enum DomainCategory {
    case social      // facebook.com, instagram.com, tiktok.com
    case gaming      // roblox.com, minecraft.net
    case education   // khanacademy.org, coursera.org
    case general     // everything else

    var allow2Activity: Allow2.Activity {
        switch self {
        case .social: return .Social
        case .gaming: return .Gaming
        default: return .Internet
        }
    }
}
```

---

## 4. BLOCKING INTERFACE

When `Allow2CheckResult.allowed == false`, show a full-screen block overlay:

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│                         ⏰ Time's Up!                            │
│                                                                  │
│              ┌────────────────────────────────┐                  │
│              │                                │                  │
│              │         🚫 BLOCKED             │                  │
│              │                                │                  │
│              │   Internet time has ended      │                  │
│              │   for today (School Night)     │                  │
│              │                                │                  │
│              └────────────────────────────────┘                  │
│                                                                  │
│                                                                  │
│  ┌──────────────────────┐    ┌──────────────────────┐           │
│  │   Request More Time  │    │   Switch User        │           │
│  └──────────────────────┘    └──────────────────────┘           │
│                                                                  │
│                    Why am I blocked?                             │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Block Reasons from API

```swift
public var explanation: String {
    var reasons: [String] = []

    activities.forEach { (_, activity) in
        if activity["banned"].boolValue {
            reasons.append("You are currently banned from \(activity["name"]).")
        } else if !activity["timeblock"]["allowed"].boolValue {
            reasons.append("You cannot use \(activity["name"]) at this time.")
        } else if activity["remaining"].intValue <= 0 {
            reasons.append("You've used all your \(activity["name"]) time today.")
        }
    }

    return reasons.joined(separator: "\n")
}
```

### Request More Time Flow

```
User taps "Request More Time"
        ↓
┌─────────────────────────────────────────┐
│  Request Additional Time                 │
│                                          │
│  Activity: Internet                      │
│                                          │
│  Request:  ○ 15 minutes                  │
│            ○ 30 minutes                  │
│            ● 1 hour                      │
│            ○ Until bedtime               │
│                                          │
│  Message to parent (optional):           │
│  ┌──────────────────────────────────┐   │
│  │ I need to finish my homework     │   │
│  └──────────────────────────────────┘   │
│                                          │
│         [Send Request]                   │
└─────────────────────────────────────────┘
        ↓
POST https://api.allow2.com/request/createRequest
        ↓
Parent receives notification
        ↓
Parent approves/denies in Allow2 app
        ↓
Next check() call reflects new allowance
```

---

## 5. COUNTDOWN & WARNING NOTIFICATIONS

### Warning Thresholds

| Time Remaining | Notification Type | Visual |
|----------------|-------------------|--------|
| 15 minutes | Gentle reminder | Toast (auto-dismiss) |
| 5 minutes | Warning | Persistent toast |
| 1 minute | Urgent | Banner + sound |
| 0 minutes | Block | Full overlay |

### Implementation

```swift
// File: Allow2WarningManager.swift

class Allow2WarningManager {

    enum WarningLevel {
        case gentle(minutes: Int)    // 15, 10 min
        case warning(minutes: Int)   // 5, 3 min
        case urgent(seconds: Int)    // 60, 30, 10 sec
        case blocked
    }

    private var lastWarningLevel: WarningLevel?
    private var countdownTimer: Timer?
    private var remainingSeconds: Int = 0

    /// Called when Allow2 check returns remaining time
    func updateRemainingTime(_ seconds: Int) {
        remainingSeconds = seconds

        let newLevel = calculateWarningLevel(seconds)

        // Only notify on level changes (avoid spam)
        if shouldNotify(from: lastWarningLevel, to: newLevel) {
            showWarning(level: newLevel)
            lastWarningLevel = newLevel
        }

        // Start countdown timer for final minute
        if seconds <= 60 && seconds > 0 {
            startUrgentCountdown()
        }
    }

    private func calculateWarningLevel(_ seconds: Int) -> WarningLevel {
        switch seconds {
        case ...0:
            return .blocked
        case 1...60:
            return .urgent(seconds: seconds)
        case 61...300:  // 1-5 min
            return .warning(minutes: seconds / 60)
        case 301...900: // 5-15 min
            return .gentle(minutes: seconds / 60)
        default:
            return .gentle(minutes: seconds / 60)
        }
    }
}
```

### Visual Designs

#### 15-Minute Warning (Toast)

```
┌──────────────────────────────────────────────────┐
│ ⏰ 15 minutes of internet time remaining         │
│    [Dismiss]                                     │
└──────────────────────────────────────────────────┘
          ↓ slides up from bottom, auto-dismiss 5s
```

#### 5-Minute Warning (Persistent Toast with Progress)

```
┌──────────────────────────────────────────────────┐
│ ⚠️ Only 5 minutes left!                          │
│ ████████░░░░░░░░░░░░░░░░░░░░░░░  5:00            │
│ [Request More Time]                              │
└──────────────────────────────────────────────────┘
          ↓ stays visible, updates every second
```

#### 1-Minute Countdown (Urgent Banner)

```
┌──────────────────────────────────────────────────────────────────┐
│  🔴 BROWSING ENDS IN: 0:47                                       │
│      ██████████████████████████████████████░░░░░░░░░░░░          │
│  [Request More Time]              [I understand]                 │
└──────────────────────────────────────────────────────────────────┘
              ↓ full-width top banner, countdown animation
```

### Time Block Approaching Warning

For scheduled time blocks (e.g., "No internet after 8pm"):

```
┌──────────────────────────────────────────────────┐
│ 🌙 Bedtime internet block starts in 10 minutes   │
│    Internet will be unavailable 8:00pm - 7:00am  │
│    [OK]                                          │
└──────────────────────────────────────────────────┘
```

### Notification Sound (Optional)

```swift
// Gentle chime for warnings
func playWarningSound(level: WarningLevel) {
    switch level {
    case .gentle:
        AudioServicesPlaySystemSound(1007)  // Gentle tap
    case .warning:
        AudioServicesPlaySystemSound(1016)  // Tweet sound
    case .urgent:
        AudioServicesPlaySystemSound(1005)  // Alarm
    case .blocked:
        AudioServicesPlaySystemSound(1073)  // Lock sound
    }
}
```

---

## 6. DATA FLOW ARCHITECTURE

```
┌─────────────────────────────────────────────────────────────────────┐
│                           BRAVE BROWSER                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────┐    ┌──────────────────┐    ┌─────────────────┐    │
│  │   Settings  │───▶│   Allow2Manager   │◀───│ BrowserViewCtrl │    │
│  │   (Pair)    │    │   (Singleton)     │    │   (Track/Block) │    │
│  └─────────────┘    └────────┬─────────┘    └─────────────────┘    │
│                              │                                       │
│         ┌────────────────────┼────────────────────┐                 │
│         │                    │                    │                 │
│         ▼                    ▼                    ▼                 │
│  ┌─────────────┐    ┌──────────────┐    ┌─────────────────┐        │
│  │   Local     │    │   Warning    │    │   Block         │        │
│  │   Cache     │    │   Manager    │    │   Overlay       │        │
│  └─────────────┘    └──────────────┘    └─────────────────┘        │
│                                                                      │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               │ HTTPS (every 10s or on navigation)
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        ALLOW2 API                                    │
├─────────────────────────────────────────────────────────────────────┤
│  POST /serviceapi/check                                              │
│  {                                                                   │
│      "userId": "67890",                                              │
│      "pairId": "12345",                                              │
│      "childId": "1001",                                              │
│      "activities": [{ "id": 1, "log": true }],                       │
│      "tz": "Australia/Sydney"                                        │
│  }                                                                   │
│                                                                      │
│  Response:                                                           │
│  {                                                                   │
│      "allowed": true,                                                │
│      "activities": {                                                 │
│          "1": {                                                      │
│              "id": 1,                                                │
│              "name": "Internet",                                     │
│              "remaining": 1800,     ◀── seconds remaining            │
│              "expires": 1699999999, ◀── cache expiry                 │
│              "banned": false,                                        │
│              "timeblock": {                                          │
│                  "allowed": true,                                    │
│                  "ends": 1700006400  ◀── when block starts           │
│              }                                                       │
│          }                                                           │
│      },                                                              │
│      "dayTypes": {                                                   │
│          "today": { "id": 2, "name": "School Night" },               │
│          "tomorrow": { "id": 1, "name": "Weekend" }                  │
│      }                                                               │
│  }                                                                   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 7. FILE STRUCTURE FOR BRAVE-CORE

### iOS Structure

```
ios/brave-ios/Sources/Brave/Frontend/
├── ParentalFreedom/
│   ├── Allow2Manager.swift               # Singleton, API calls, caching
│   ├── Allow2CredentialManager.swift     # Keychain storage
│   ├── Allow2CheckResult.swift           # Response model
│   ├── Allow2Child.swift                 # Child model
│   ├── Allow2WarningManager.swift        # Countdown/warnings
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
│       └── BVC+ParentalFreedom.swift     # BVC extension (tracking/blocking)
└── Settings/
    └── Features/
        └── ParentalFreedom/
            └── ParentalFreedomSectionView.swift  # Top-level settings entry
```

### Desktop Structure (C++)

```
components/allow2/
├── common/
│   ├── pref_names.h                      # Preference keys
│   ├── allow2_constants.h                # API URLs, activity IDs
│   └── BUILD.gn
├── browser/
│   ├── allow2_service.h/.cc              # Main service (singleton)
│   ├── allow2_credential_manager.h/.cc   # OSCrypt credential storage
│   ├── allow2_api_client.h/.cc           # HTTP client for API
│   ├── allow2_warning_controller.h/.cc   # Countdown/warning logic
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

### Android Structure (Kotlin)

```
browser/allow2/
├── Allow2Service.kt                      # Main service
├── Allow2CredentialManager.kt            # EncryptedSharedPreferences
├── Allow2ApiClient.kt                    # Retrofit API client
├── Allow2WarningManager.kt               # Countdown/warnings
└── ui/
    ├── ParentalFreedomSettingsFragment.kt
    ├── ChildSelectBottomSheet.kt
    ├── BlockOverlayActivity.kt
    └── WarningSnackbar.kt
```

---

## 8. IMPLEMENTATION PHASES

### Phase 1: Core Integration
- [ ] Add Allow2 SDK/Manager
- [ ] Preferences storage
- [ ] Basic pairing flow
- [ ] Settings UI entry point

### Phase 2: Child Shield
- [ ] Child selection overlay
- [ ] PIN validation
- [ ] Session management
- [ ] Switch user flow

### Phase 3: Usage Tracking
- [ ] URL tracking hooks
- [ ] Periodic check timer
- [ ] Private mode exclusion
- [ ] Activity logging

### Phase 4: Blocking
- [ ] Block overlay UI
- [ ] Explanation display
- [ ] Request more time flow
- [ ] Day type display

### Phase 5: Warnings & Countdown
- [ ] Warning manager
- [ ] Toast notifications
- [ ] Countdown timer
- [ ] Time block alerts
- [ ] Sound effects (optional)

### Phase 6: Polish
- [ ] Animations
- [ ] Localization
- [ ] Accessibility
- [ ] Performance optimization
- [ ] Offline handling

---

## 9. SECURITY CONSIDERATIONS

1. **PIN Security**: PINs never transmitted or stored in plain text. Server sends SHA-256 hash + salt; device validates by hashing input and comparing (constant-time comparison to prevent timing attacks)
2. **API Tokens**: Credentials stored encrypted (OSCrypt on desktop, Keychain on iOS, EncryptedSharedPreferences on Android)
3. **Bypass Prevention**: Block overlay uses platform-specific modal that prevents interaction with underlying content
4. **Private Browsing**: Never tracked, never blocked (parent configures this in Allow2 web portal)
5. **No Device Unpair**: Device cannot unpair itself. Release is ONLY possible from parent's Allow2 portal/app. When released remotely, device receives 401 on next check() and clears local credentials
6. **Transport Security**: All API calls use HTTPS with certificate pinning
7. **Cache Expiry**: Cached check results expire per server-specified TTL; stale cache = re-check required
8. **No Sensitive Logging**: PINs, tokens, and hashes never written to logs or crash reports
9. **Reinstall Protection**: Consider device fingerprinting so reinstalling Brave doesn't bypass pairing (requires Allow2 backend support)

---

## 10. TESTING SCENARIOS

| Scenario | Expected Behavior |
|----------|-------------------|
| **Unpaired States** | |
| First launch, not paired | Settings shows "Parental Freedom" with "Set Up" option |
| Browse while unpaired | Normal browsing, no tracking, no restrictions |
| **Pairing Flow** | |
| Pair via QR code | Scan → confirm children list → paired |
| Pair via manual code | Enter 6-digit code → confirm → paired |
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
| Private browsing tab | No tracking, no blocking (configurable by parent) |
| "Switch User" from settings | Show child shield regardless of current state |

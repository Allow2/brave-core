# Allow2 Profile Identity Binding

**Status:** Planning
**Author:** Allow2 Team
**Last Updated:** 2026-02-21

## Overview

This document describes the capability to bind Allow2 child/parent identities to Brave browser profiles, creating a unified identity layer where selecting a child in the Allow2 picker simultaneously authenticates them AND switches to their bound browser profile.

## Problem Statement

Currently, Allow2 child selection and browser profile management are separate concepts:
- Allow2 tracks which child is using the browser for time limits
- Browser profiles provide isolated browsing environments (bookmarks, history, passwords)
- Users must manage both separately, leading to potential confusion

## Solution: Unified Identity Model

**Core Concept:** The Allow2 child picker becomes the profile switcher. Selecting and authenticating as a child is synonymous with switching to their profile.

```
┌─────────────────────────────────────────────────────────────┐
│                     CURRENT MODEL                            │
│                                                              │
│   Allow2 Child Selection ──┐                                 │
│                            ├──► Browsing Session             │
│   Profile Selection ───────┘    (concepts are separate)      │
│                                                              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                     UNIFIED MODEL                            │
│                                                              │
│   Allow2 Child Selection ════► Profile Switch + Tracking     │
│   (single action, unified identity)                          │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## Binding Options

### Option 1: One-to-One Binding (Simple)

Each Allow2 identity binds to exactly one browser profile.

```
Allow2 Identity          Browser Profile
─────────────────        ───────────────
Emma (child)      ────►  "Emma" Profile
Jack (child)      ────►  "Jack" Profile
Andrew (parent)   ────►  "Default" Profile
```

**Pros:**
- Simple mental model
- Clear ownership
- Easy to implement

**Cons:**
- Less flexible for power users
- Can't have "Emma School" and "Emma Personal" profiles

### Option 2: One-to-Many Binding (Flexible)

Each Allow2 identity can bind to multiple profiles.

```
Allow2 Identity          Browser Profiles
─────────────────        ────────────────────────
Emma (child)      ────►  "Emma School" Profile
                  ────►  "Emma Personal" Profile
Jack (child)      ────►  "Jack" Profile
Andrew (parent)   ────►  "Default" Profile
                  ────►  "Work" Profile
```

**Pros:**
- Flexible for different contexts
- Supports existing multi-profile setups

**Cons:**
- More complex UX (which profile to switch to?)
- Requires sub-selection or "default profile" concept

### Option 3: Profile-First Binding

Profiles are primary; Allow2 identity is attached to profiles.

```
Browser Profile          Allow2 Identity
───────────────          ─────────────────
"Emma" Profile     ────►  Emma (child)
"Jack" Profile     ────►  Jack (child)
"Default" Profile  ────►  Andrew (parent)
"Guest" Profile    ────►  [Unbound - blocked or guest mode]
```

**Pros:**
- Works with existing profile workflows
- Profile can exist without Allow2 binding

**Cons:**
- Unbound profiles create security gap
- Need to handle "orphan" profiles

### Option 4: Hybrid with Strict Mode

Combine flexibility with security controls.

```
Settings:
  ☑ Strict Mode: All profiles must be bound to an Allow2 identity
  ☐ Allow unbound profiles (for parent use only)

Bindings:
  Emma ────► "Emma" Profile (required PIN)
  Jack ────► "Jack" Profile (required PIN)
  Andrew ──► "Default", "Work" Profiles (optional PIN or no PIN)
```

**Recommended approach** - gives parents flexibility while enforcing child restrictions.

## User Experience

### Child Picker Integration

When Allow2 is paired in shared device mode:

```
┌─────────────────────────────────────────────────────────────┐
│                        [Allow2 Logo]                         │
│                                                              │
│                    Who's using Brave?                        │
│                                                              │
│      ┌──────────┐  ┌──────────┐  ┌──────────┐               │
│      │    EJ    │  │    JD    │  │    AD    │               │
│      │   Emma   │  │   Jack   │  │  Andrew  │               │
│      │  (child) │  │  (child) │  │ (parent) │               │
│      └──────────┘  └──────────┘  └──────────┘               │
│                                                              │
│              Choose an account to start browsing.            │
│                                                              │
│         No guest option - everyone must authenticate.        │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

If a child has multiple bound profiles:

```
┌─────────────────────────────────────────────────────────────┐
│                     Welcome back, Emma!                      │
│                                                              │
│                  Which profile today?                        │
│                                                              │
│         ┌─────────────┐      ┌─────────────┐                │
│         │   School    │      │  Personal   │                │
│         │  (default)  │      │             │                │
│         └─────────────┘      └─────────────┘                │
│                                                              │
│                      Enter PIN: ● ● ● ●                      │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Native Profile Switcher Behavior

When Allow2 is paired:
- Chrome/Brave profile menu is **disabled** or shows "Managed by Allow2"
- Profile avatar click shows Allow2 child picker instead
- Keyboard shortcuts for profile switching are intercepted
- Only way to switch is through Allow2 (requires PIN)

### Session Timeout

When session times out (configurable, e.g., 5 minutes idle):
- Profile locks
- Child picker reappears
- Must re-authenticate to continue

## Technical Architecture

### Data Model

#### Allow2 Service (Server-Side)

```sql
-- Profile bindings stored per paired device
CREATE TABLE ProfileBindings (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  PairedDeviceId BIGINT NOT NULL,
  ChildId BIGINT NULL,                    -- NULL = parent/controller
  UserId BIGINT NULL,                     -- For parent binding
  profileGuid VARCHAR(64) NOT NULL,       -- Browser profile GUID
  profileName VARCHAR(255) NOT NULL,      -- Display name
  isDefault BOOLEAN DEFAULT FALSE,        -- Default profile for this identity
  createdAt DATETIME DEFAULT CURRENT_TIMESTAMP,

  FOREIGN KEY (PairedDeviceId) REFERENCES PairedDevices(id),
  FOREIGN KEY (ChildId) REFERENCES Children(id),
  UNIQUE KEY idx_device_profile (PairedDeviceId, profileGuid)
);
```

#### Browser Prefs (Client-Side)

```cpp
// In pref_names.h

// Profile-level pref: Which Allow2 identity this profile is bound to
// Format: {"type": "child"|"parent", "id": 1001, "name": "Emma"}
inline constexpr char kAllow2BoundIdentity[] = "brave.allow2.bound_identity";

// Local state pref: All profile bindings for this device
// Format: [{"profilePath": "...", "childId": 1001}, ...]
inline constexpr char kAllow2ProfileBindings[] = "brave.allow2.profile_bindings";

// Whether strict mode is enabled (all profiles must be bound)
inline constexpr char kAllow2StrictProfileMode[] = "brave.allow2.strict_profile_mode";
```

### Key Components

#### Allow2ProfileManager

Manages the binding between Allow2 identities and browser profiles.

```cpp
// allow2_profile_manager.h

class Allow2ProfileManager {
 public:
  // Binding management
  void BindChildToProfile(uint64_t child_id, Profile* profile);
  void BindParentToProfile(uint64_t user_id, Profile* profile);
  void UnbindProfile(Profile* profile);

  // Query bindings
  std::optional<uint64_t> GetBoundChildId(Profile* profile);
  std::vector<Profile*> GetProfilesForChild(uint64_t child_id);
  Profile* GetDefaultProfileForChild(uint64_t child_id);

  // Profile switching
  void SwitchToChildProfile(uint64_t child_id, const std::string& pin);
  void SwitchToParentProfile(uint64_t user_id);

  // Intercept native profile switching
  bool ShouldBlockProfileSwitch();
  void OnProfileSwitchAttempted(Profile* target_profile);

  // Auto-create profiles for new children
  void CreateProfileForChild(const Child& child);
  void SyncProfilesWithChildList(const std::vector<Child>& children);
};
```

#### Modified Child Picker Flow

```cpp
// In allow2_tab_helper.cc

bool Allow2TabHelper::MaybeShowChildShield() {
  // ... existing checks ...

  Allow2ChildSelectView::Show(
      browser,
      children,
      owner_name,
      // Child selected callback - now includes profile switch
      base::BindOnce(
          [](base::WeakPtr<Allow2Service> weak_service,
             base::WeakPtr<Allow2ProfileManager> weak_profile_mgr,
             uint64_t child_id, const std::string& pin) {
            if (!weak_service || !weak_profile_mgr) return;

            // Validate PIN and select child
            if (weak_service->SelectChild(child_id, pin)) {
              // Switch to bound profile
              weak_profile_mgr->SwitchToChildProfile(child_id, pin);
            }
          },
          service->GetWeakPtr(),
          profile_manager->GetWeakPtr()),
      // Parent callback
      base::BindOnce([](base::WeakPtr<Allow2ProfileManager> weak_profile_mgr,
                        uint64_t parent_user_id) {
        if (weak_profile_mgr) {
          weak_profile_mgr->SwitchToParentProfile(parent_user_id);
        }
      }, profile_manager->GetWeakPtr(), controller_user_id));
}
```

### Profile Switcher Interception

```cpp
// In chromium_src/chrome/browser/ui/views/profiles/profile_menu_view.cc

// Override to intercept profile switching when Allow2 is active
void ProfileMenuView::OnProfileSelected(Profile* profile) {
  if (Allow2Service* service = Allow2ServiceFactory::GetForProfile(profile)) {
    if (service->IsPaired() && service->IsStrictProfileModeEnabled()) {
      // Block native switch, show Allow2 picker instead
      service->ShowChildShield();
      return;
    }
  }

  // Allow native switch if not managed by Allow2
  ProfileMenuViewBase::OnProfileSelected(profile);
}
```

## Implementation Plan

### Phase 1: Foundation (Week 1-2)

1. **Add Profile Binding Prefs**
   - Add `kAllow2BoundIdentity` profile pref
   - Add `kAllow2ProfileBindings` local state pref
   - Register prefs in `allow2_utils.cc`

2. **Create Allow2ProfileManager**
   - Basic binding CRUD operations
   - Query methods for bindings
   - Unit tests

3. **Server Schema**
   - Add `ProfileBindings` table
   - API endpoints for sync

### Phase 2: Binding UI (Week 3-4)

1. **Settings Page**
   - Profile binding configuration UI
   - List profiles with Allow2 identity dropdown
   - "Create Profile for Child" button

2. **First-Run Binding Wizard**
   - After pairing, prompt to bind/create profiles
   - Auto-suggest profile names from child names

### Phase 3: Unified Switching (Week 5-6)

1. **Modify Child Picker**
   - Integrate profile switching into selection callback
   - Handle multi-profile children (sub-selection)

2. **Profile Switcher Interception**
   - Override native profile menu when Allow2 active
   - Intercept keyboard shortcuts
   - Show "Managed by Allow2" indicator

### Phase 4: Security & Polish (Week 7-8)

1. **Strict Mode**
   - Enforce all profiles must be bound
   - Block access to unbound profiles

2. **Session Management**
   - Profile lock on timeout
   - Re-authentication flow

3. **Sync**
   - Sync bindings to server
   - Handle child list changes (new/removed children)

## Security Considerations

| Risk | Mitigation |
|------|------------|
| Child accesses parent profile | Require PIN for all profile switches |
| Unbound profiles bypass tracking | Strict mode: block unbound profiles |
| Profile created outside Allow2 | Detect new profiles, prompt for binding |
| Child knows parent PIN | Separate PINs per identity; push auth for parent |
| Profile deletion breaks binding | Gracefully handle, prompt to rebind |

## Migration Path

For existing paired devices:

1. **Detect existing profiles** on upgrade
2. **Prompt user** to bind profiles to identities
3. **Auto-suggest** based on profile name matching child names
4. **Grace period** before strict mode enforcement

## Design Decisions

### No Guest Mode

On a paired family device, there is **no guest/anonymous browsing option**. Every session must be authenticated:

- **Children:** Tracked with time limits enforced
- **Parent:** Tracked (for transparency) but no limits

This ensures accountability and prevents children from bypassing tracking by using "guest" mode.

### Incognito Handling

**Child profiles:** Incognito mode is **disabled/inaccessible**.
- Menu item hidden or disabled
- Keyboard shortcut (Cmd/Ctrl+Shift+N) blocked
- Prevents hiding local browsing history
- No "private window" option available

**Parent profiles:** Incognito allowed (optional).
- Parent has full browser capabilities
- Still tracked by Allow2 for transparency
- History not saved locally, but Allow2 still records activity

```cpp
// Implementation: Override incognito availability for child profiles
bool Allow2Service::IsIncognitoAllowed(Profile* profile) const {
  auto bound_identity = GetBoundIdentity(profile);
  if (bound_identity && bound_identity->type == IdentityType::kChild) {
    return false;  // Children cannot use incognito
  }
  return true;  // Parent can use incognito
}
```

## Open Questions

1. **Multiple Devices:** Should bindings sync across devices or be device-specific?
2. **Profile Sync:** If Chrome Sync is on, how does that interact with Allow2 binding?

## Related Documents

- [ALLOW2.md](./ALLOW2.md) - Main developer documentation
- [Allow2 Requests System Plan](~/.claude/plans/) - Offline auth implementation
- Style Guide (TODO) - Branding and UI patterns

---

*This document will be updated as the design evolves.*

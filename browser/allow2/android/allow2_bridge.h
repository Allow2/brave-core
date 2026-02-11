/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_ALLOW2_ANDROID_ALLOW2_BRIDGE_H_
#define BRAVE_BROWSER_ALLOW2_ANDROID_ALLOW2_BRIDGE_H_

#include <jni.h>
#include <string>

#include "base/android/jni_weak_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "url/gurl.h"

class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace allow2 {
namespace android {

// Observer interface for Allow2 events.
class Allow2BridgeObserver {
 public:
  virtual ~Allow2BridgeObserver() = default;

  // Called when the blocked state changes.
  virtual void OnBlockedStateChanged(bool is_blocked) = 0;

  // Called when a warning should be shown.
  virtual void OnWarning(int warning_level, int remaining_seconds) = 0;

  // Called when the device is unpaired remotely.
  virtual void OnUnpaired() = 0;

  // Called when child selection is needed.
  virtual void OnNeedChildSelection() = 0;
};

// JNI bridge for Allow2 parental controls integration.
// This class bridges the Kotlin Allow2Service with Chrome's browser process.
class Allow2Bridge {
 public:
  Allow2Bridge(JNIEnv* env, const base::android::JavaRef<jobject>& obj);
  Allow2Bridge(const Allow2Bridge&) = delete;
  Allow2Bridge& operator=(const Allow2Bridge&) = delete;
  ~Allow2Bridge();

  void Destroy(JNIEnv* env);

  // ==================== Pairing State ====================

  // Check if device is paired with Allow2.
  jboolean IsPaired(JNIEnv* env);

  // Check if Allow2 is enabled.
  jboolean IsEnabled(JNIEnv* env);

  // Check if access is currently blocked.
  jboolean IsBlocked(JNIEnv* env);

  // Check if child selection is needed (shared device mode).
  jboolean NeedsChildSelection(JNIEnv* env);

  // ==================== Navigation Hooks ====================

  // Called before navigation to check if URL is allowed.
  // Returns true if navigation should proceed, false if blocked.
  jboolean ShouldAllowNavigation(
      JNIEnv* env,
      const base::android::JavaRef<jstring>& url);

  // Called when navigation completes to log usage.
  void OnNavigationComplete(
      JNIEnv* env,
      const base::android::JavaRef<jstring>& url);

  // ==================== Lifecycle Events ====================

  // Called when the browser becomes foreground.
  void OnBrowserForeground(JNIEnv* env);

  // Called when the browser goes to background.
  void OnBrowserBackground(JNIEnv* env);

  // Called when a new tab is created.
  void OnTabCreated(JNIEnv* env, jlong tab_id);

  // Called when a tab is closed.
  void OnTabClosed(JNIEnv* env, jlong tab_id);

  // ==================== Settings Integration ====================

  // Get the current child's name for display.
  base::android::ScopedJavaLocalRef<jstring> GetCurrentChildName(JNIEnv* env);

  // Get the number of registered children.
  jint GetChildCount(JNIEnv* env);

  // Get remaining time in seconds (-1 if unlimited).
  jint GetRemainingSeconds(JNIEnv* env);

  // ==================== Internal Callbacks ====================

  // Called from Kotlin when blocked state changes.
  void NotifyBlockedStateChanged(JNIEnv* env, jboolean is_blocked);

  // Called from Kotlin when a warning should be shown.
  void NotifyWarning(JNIEnv* env, jint warning_level, jint remaining_seconds);

  // Called from Kotlin when device is unpaired.
  void NotifyUnpaired(JNIEnv* env);

  // Called from Kotlin when child selection is needed.
  void NotifyNeedChildSelection(JNIEnv* env);

  // ==================== Observer Management ====================

  void AddObserver(Allow2BridgeObserver* observer);
  void RemoveObserver(Allow2BridgeObserver* observer);

  // ==================== Static Methods ====================

  // Get the singleton instance for the current profile.
  static Allow2Bridge* GetInstance();

  // Check if Allow2 integration is available.
  static bool IsAvailable();

 private:
  // Check if the URL requires parental control checking.
  bool RequiresParentalCheck(const GURL& url);

  // Get the activity type for a URL.
  int GetActivityTypeForUrl(const GURL& url);

  // Notify Java side of events.
  void NotifyJavaOnNavigate(const std::string& url);

  JavaObjectWeakGlobalRef weak_java_allow2_bridge_;
  raw_ptr<Profile> profile_ = nullptr;

  // Current state cache.
  bool is_blocked_ = false;
  int remaining_seconds_ = -1;
  std::string current_child_name_;

  base::ObserverList<Allow2BridgeObserver> observers_;

  base::WeakPtrFactory<Allow2Bridge> weak_ptr_factory_{this};
};

}  // namespace android
}  // namespace allow2

#endif  // BRAVE_BROWSER_ALLOW2_ANDROID_ALLOW2_BRIDGE_H_

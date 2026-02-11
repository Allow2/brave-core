/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/allow2/android/allow2_bridge.h"

#include <utility>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "chrome/android/chrome_jni_headers/Allow2Bridge_jni.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using content::BrowserThread;

namespace {

// Activity type constants matching Allow2Constants.kt
constexpr int kActivityInternet = 1;
constexpr int kActivityGaming = 3;
constexpr int kActivityScreenTime = 8;
constexpr int kActivitySocial = 9;

// Social media domains for activity detection.
const char* const kSocialDomains[] = {
    "facebook.com", "fb.com",        "twitter.com",   "x.com",
    "instagram.com", "tiktok.com",   "snapchat.com",  "reddit.com",
    "discord.com",   "whatsapp.com", "telegram.org",  "linkedin.com",
    "pinterest.com", "tumblr.com",   "twitch.tv",
};

// Gaming domains for activity detection.
const char* const kGamingDomains[] = {
    "steampowered.com", "epicgames.com",      "roblox.com",
    "minecraft.net",    "xbox.com",           "playstation.com",
    "nintendo.com",     "ea.com",             "ubisoft.com",
    "blizzard.com",     "battle.net",         "leagueoflegends.com",
    "fortnite.com",     "itch.io",            "gog.com",
    "kongregate.com",   "miniclip.com",       "poki.com",
    "crazygames.com",
};

// Singleton instance.
allow2::android::Allow2Bridge* g_instance = nullptr;

}  // namespace

namespace allow2 {
namespace android {

Allow2Bridge::Allow2Bridge(JNIEnv* env,
                           const base::android::JavaRef<jobject>& obj)
    : weak_java_allow2_bridge_(env, obj) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  Java_Allow2Bridge_setNativePtr(env, obj, reinterpret_cast<intptr_t>(this));

  profile_ = ProfileManager::GetLastUsedProfile();
  DCHECK(profile_);

  g_instance = this;

  VLOG(1) << "Allow2Bridge: Initialized";
}

Allow2Bridge::~Allow2Bridge() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (g_instance == this) {
    g_instance = nullptr;
  }

  VLOG(1) << "Allow2Bridge: Destroyed";
}

void Allow2Bridge::Destroy(JNIEnv* env) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  delete this;
}

// ==================== Static Methods ====================

// static
Allow2Bridge* Allow2Bridge::GetInstance() {
  return g_instance;
}

// static
bool Allow2Bridge::IsAvailable() {
  return g_instance != nullptr;
}

// ==================== Pairing State ====================

jboolean Allow2Bridge::IsPaired(JNIEnv* env) {
  JNIEnv* jni_env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(jni_env);
  if (java_obj.is_null()) {
    return JNI_FALSE;
  }

  return Java_Allow2Bridge_isPaired(jni_env, java_obj);
}

jboolean Allow2Bridge::IsEnabled(JNIEnv* env) {
  JNIEnv* jni_env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(jni_env);
  if (java_obj.is_null()) {
    return JNI_FALSE;
  }

  return Java_Allow2Bridge_isEnabled(jni_env, java_obj);
}

jboolean Allow2Bridge::IsBlocked(JNIEnv* env) {
  return is_blocked_ ? JNI_TRUE : JNI_FALSE;
}

jboolean Allow2Bridge::NeedsChildSelection(JNIEnv* env) {
  JNIEnv* jni_env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(jni_env);
  if (java_obj.is_null()) {
    return JNI_FALSE;
  }

  return Java_Allow2Bridge_needsChildSelection(jni_env, java_obj);
}

// ==================== Navigation Hooks ====================

jboolean Allow2Bridge::ShouldAllowNavigation(
    JNIEnv* env,
    const base::android::JavaRef<jstring>& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::string url_str = ConvertJavaStringToUTF8(env, url);
  GURL gurl(url_str);

  if (!gurl.is_valid()) {
    return JNI_TRUE;  // Allow invalid URLs to be handled by browser
  }

  // Always allow chrome:// and brave:// URLs
  if (gurl.SchemeIs("chrome") || gurl.SchemeIs("brave") ||
      gurl.SchemeIs("about")) {
    return JNI_TRUE;
  }

  // Check if blocked
  if (is_blocked_) {
    VLOG(2) << "Allow2Bridge: Navigation blocked - " << gurl.host();
    return JNI_FALSE;
  }

  // Notify Kotlin side for activity tracking
  NotifyJavaOnNavigate(url_str);

  return JNI_TRUE;
}

void Allow2Bridge::OnNavigationComplete(
    JNIEnv* env,
    const base::android::JavaRef<jstring>& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::string url_str = ConvertJavaStringToUTF8(env, url);
  GURL gurl(url_str);

  if (!gurl.is_valid()) {
    return;
  }

  // Only track HTTP(S) URLs
  if (!gurl.SchemeIsHTTPOrHTTPS()) {
    return;
  }

  // Notify Kotlin side for usage logging
  JNIEnv* jni_env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(jni_env);
  if (!java_obj.is_null()) {
    int activity_type = GetActivityTypeForUrl(gurl);
    Java_Allow2Bridge_onNavigationComplete(
        jni_env, java_obj, ConvertUTF8ToJavaString(jni_env, url_str),
        activity_type);
  }
}

// ==================== Lifecycle Events ====================

void Allow2Bridge::OnBrowserForeground(JNIEnv* env) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  JNIEnv* jni_env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(jni_env);
  if (!java_obj.is_null()) {
    Java_Allow2Bridge_onBrowserForeground(jni_env, java_obj);
  }
}

void Allow2Bridge::OnBrowserBackground(JNIEnv* env) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  JNIEnv* jni_env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(jni_env);
  if (!java_obj.is_null()) {
    Java_Allow2Bridge_onBrowserBackground(jni_env, java_obj);
  }
}

void Allow2Bridge::OnTabCreated(JNIEnv* env, jlong tab_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Reserved for future tab-specific tracking
}

void Allow2Bridge::OnTabClosed(JNIEnv* env, jlong tab_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Reserved for future tab-specific tracking
}

// ==================== Settings Integration ====================

ScopedJavaLocalRef<jstring> Allow2Bridge::GetCurrentChildName(JNIEnv* env) {
  JNIEnv* jni_env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(jni_env);
  if (java_obj.is_null()) {
    return ConvertUTF8ToJavaString(jni_env, "");
  }

  return Java_Allow2Bridge_getCurrentChildName(jni_env, java_obj);
}

jint Allow2Bridge::GetChildCount(JNIEnv* env) {
  JNIEnv* jni_env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(jni_env);
  if (java_obj.is_null()) {
    return 0;
  }

  return Java_Allow2Bridge_getChildCount(jni_env, java_obj);
}

jint Allow2Bridge::GetRemainingSeconds(JNIEnv* env) {
  return remaining_seconds_;
}

// ==================== Internal Callbacks ====================

void Allow2Bridge::NotifyBlockedStateChanged(JNIEnv* env, jboolean is_blocked) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  bool blocked = (is_blocked == JNI_TRUE);
  if (is_blocked_ == blocked) {
    return;
  }

  is_blocked_ = blocked;
  VLOG(1) << "Allow2Bridge: Blocked state changed to " << is_blocked_;

  for (Allow2BridgeObserver& observer : observers_) {
    observer.OnBlockedStateChanged(is_blocked_);
  }
}

void Allow2Bridge::NotifyWarning(JNIEnv* env,
                                  jint warning_level,
                                  jint remaining_seconds) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  remaining_seconds_ = remaining_seconds;
  VLOG(2) << "Allow2Bridge: Warning level " << warning_level << ", "
          << remaining_seconds << "s remaining";

  for (Allow2BridgeObserver& observer : observers_) {
    observer.OnWarning(warning_level, remaining_seconds);
  }
}

void Allow2Bridge::NotifyUnpaired(JNIEnv* env) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  is_blocked_ = false;
  remaining_seconds_ = -1;
  current_child_name_.clear();

  VLOG(1) << "Allow2Bridge: Device unpaired";

  for (Allow2BridgeObserver& observer : observers_) {
    observer.OnUnpaired();
  }
}

void Allow2Bridge::NotifyNeedChildSelection(JNIEnv* env) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  VLOG(1) << "Allow2Bridge: Child selection needed";

  for (Allow2BridgeObserver& observer : observers_) {
    observer.OnNeedChildSelection();
  }
}

// ==================== Observer Management ====================

void Allow2Bridge::AddObserver(Allow2BridgeObserver* observer) {
  observers_.AddObserver(observer);
}

void Allow2Bridge::RemoveObserver(Allow2BridgeObserver* observer) {
  observers_.RemoveObserver(observer);
}

// ==================== Private Methods ====================

bool Allow2Bridge::RequiresParentalCheck(const GURL& url) {
  // Only check HTTP(S) URLs
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }

  // Skip internal pages
  if (url.host_piece() == "localhost" || url.host_piece() == "127.0.0.1") {
    return false;
  }

  return true;
}

int Allow2Bridge::GetActivityTypeForUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return kActivityInternet;
  }

  std::string host = url.host();
  std::string domain = net::registry_controlled_domains::GetDomainAndRegistry(
      host, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);

  if (domain.empty()) {
    domain = host;
  }

  // Check social media
  for (const char* social_domain : kSocialDomains) {
    if (domain == social_domain ||
        base::EndsWith(host, std::string(".") + social_domain,
                       base::CompareCase::INSENSITIVE_ASCII)) {
      return kActivitySocial;
    }
  }

  // Check gaming
  for (const char* gaming_domain : kGamingDomains) {
    if (domain == gaming_domain ||
        base::EndsWith(host, std::string(".") + gaming_domain,
                       base::CompareCase::INSENSITIVE_ASCII)) {
      return kActivityGaming;
    }
  }

  return kActivityInternet;
}

void Allow2Bridge::NotifyJavaOnNavigate(const std::string& url) {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_obj =
      weak_java_allow2_bridge_.get(env);
  if (!java_obj.is_null()) {
    Java_Allow2Bridge_onNavigate(env, java_obj,
                                  ConvertUTF8ToJavaString(env, url));
  }
}

// ==================== JNI Entry Points ====================

static void JNI_Allow2Bridge_Init(
    JNIEnv* env,
    const JavaParamRef<jobject>& jcaller) {
  new Allow2Bridge(env, jcaller);
}

static jboolean JNI_Allow2Bridge_IsAvailable(JNIEnv* env) {
  return Allow2Bridge::IsAvailable() ? JNI_TRUE : JNI_FALSE;
}

}  // namespace android
}  // namespace allow2

DEFINE_JNI(Allow2Bridge)

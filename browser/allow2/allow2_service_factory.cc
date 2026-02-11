/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/allow2/allow2_service_factory.h"

#include <memory>

#include "base/no_destructor.h"
#include "base/time/time.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "brave/components/allow2/common/pref_names.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace allow2 {

// static
Allow2ServiceFactory* Allow2ServiceFactory::GetInstance() {
  static base::NoDestructor<Allow2ServiceFactory> instance;
  return instance.get();
}

// static
Allow2Service* Allow2ServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<Allow2Service*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
Allow2Service* Allow2ServiceFactory::GetForProfileIfExists(Profile* profile) {
  return static_cast<Allow2Service*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

Allow2ServiceFactory::Allow2ServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "Allow2Service",
          BrowserContextDependencyManager::GetInstance()) {}

Allow2ServiceFactory::~Allow2ServiceFactory() = default;

std::unique_ptr<KeyedService>
Allow2ServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);

  // Don't create service for incognito profiles.
  if (profile->IsOffTheRecord()) {
    return nullptr;
  }

  // Get the URL loader factory for API requests.
  auto url_loader_factory =
      profile->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();

  // Get local state for encrypted credential storage.
  PrefService* local_state = g_browser_process->local_state();

  // Get profile prefs for non-sensitive settings.
  PrefService* profile_prefs = profile->GetPrefs();

  return std::make_unique<Allow2Service>(
      url_loader_factory, local_state, profile_prefs);
}

content::BrowserContext* Allow2ServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  // Use the original profile, not incognito.
  // Allow2 tracking applies to the main profile only.
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

void Allow2ServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  // Non-sensitive profile preferences.
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

bool Allow2ServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  // Create service when browser context is created so tracking can start
  // immediately if device is paired.
  return true;
}

bool Allow2ServiceFactory::ServiceIsNULLWhileTesting() const {
  // Allow null service in tests for easier mocking.
  return true;
}

}  // namespace allow2

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_ALLOW2_ALLOW2_SERVICE_FACTORY_H_
#define BRAVE_BROWSER_ALLOW2_ALLOW2_SERVICE_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

class Profile;

namespace allow2 {

class Allow2Service;

// Factory for creating Allow2Service instances per browser context.
// Allow2Service provides parental controls functionality including:
// - Device pairing with parent's Allow2 account
// - Usage tracking and time limit enforcement
// - Blocking overlay management
// - "Request more time" functionality
class Allow2ServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the singleton factory instance.
  static Allow2ServiceFactory* GetInstance();

  // Returns the Allow2Service for the given profile, creating one if needed.
  // Returns nullptr for incognito profiles.
  static Allow2Service* GetForProfile(Profile* profile);

  // Returns the Allow2Service for the given profile if it exists.
  // Does not create a new service if one doesn't exist.
  static Allow2Service* GetForProfileIfExists(Profile* profile);

  Allow2ServiceFactory(const Allow2ServiceFactory&) = delete;
  Allow2ServiceFactory& operator=(const Allow2ServiceFactory&) = delete;

 private:
  friend base::NoDestructor<Allow2ServiceFactory>;

  Allow2ServiceFactory();
  ~Allow2ServiceFactory() override;

  // BrowserContextKeyedServiceFactory overrides:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
  bool ServiceIsCreatedWithBrowserContext() const override;
  bool ServiceIsNULLWhileTesting() const override;
};

}  // namespace allow2

#endif  // BRAVE_BROWSER_ALLOW2_ALLOW2_SERVICE_FACTORY_H_

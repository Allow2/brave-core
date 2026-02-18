/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_deficit_tracker.h"

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "brave/components/allow2/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace allow2 {

Allow2DeficitTracker::Allow2DeficitTracker(PrefService* prefs)
    : prefs_(prefs) {
  DCHECK(prefs_);
}

Allow2DeficitTracker::~Allow2DeficitTracker() = default;

int Allow2DeficitTracker::GetDeficitSeconds(uint64_t child_id) const {
  std::string json = prefs_->GetString(prefs::kAllow2DeficitPool);
  if (json.empty()) {
    return 0;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    return 0;
  }

  std::string key = std::to_string(child_id);
  return parsed->GetDict().FindInt(key).value_or(0);
}

void Allow2DeficitTracker::AddDeficit(uint64_t child_id, int seconds) {
  std::string json = prefs_->GetString(prefs::kAllow2DeficitPool);
  base::Value::Dict dict;

  if (!json.empty()) {
    auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
    if (parsed && parsed->is_dict()) {
      dict = std::move(parsed->GetDict());
    }
  }

  std::string key = std::to_string(child_id);
  int current = dict.FindInt(key).value_or(0);
  dict.Set(key, std::min(current + seconds, kMaxDeficitSeconds));

  std::string updated;
  base::JSONWriter::Write(dict, &updated);
  prefs_->SetString(prefs::kAllow2DeficitPool, updated);
}

void Allow2DeficitTracker::ClearDeficit(uint64_t child_id) {
  std::string json = prefs_->GetString(prefs::kAllow2DeficitPool);
  if (json.empty()) {
    return;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    return;
  }

  base::Value::Dict dict = std::move(parsed->GetDict());
  std::string key = std::to_string(child_id);
  dict.Remove(key);

  std::string updated;
  base::JSONWriter::Write(dict, &updated);
  prefs_->SetString(prefs::kAllow2DeficitPool, updated);
}

int Allow2DeficitTracker::ApplyDeficit(uint64_t child_id,
                                        int remaining_seconds) const {
  int deficit = GetDeficitSeconds(child_id);
  return std::max(0, remaining_seconds - deficit);
}

int Allow2DeficitTracker::GetTotalDeficit(uint64_t child_id) const {
  return GetDeficitSeconds(child_id);
}

bool Allow2DeficitTracker::IsDeficitExceeded(uint64_t child_id) const {
  return GetDeficitSeconds(child_id) >= kMaxDeficitSeconds;
}

}  // namespace allow2

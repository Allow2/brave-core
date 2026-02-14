/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_api_client.h"

#include <cstdlib>
#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "brave/components/allow2/browser/allow2_credential_manager.h"
#include "brave/components/allow2/browser/allow2_qr_code_generator.h"
#include "brave/components/allow2/common/allow2_constants.h"
#include "brave/components/allow2/common/allow2_utils.h"
#include "build/build_config.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace allow2 {

// InitPairingResponse struct out-of-line definitions
InitPairingResponse::InitPairingResponse() = default;
InitPairingResponse::~InitPairingResponse() = default;
InitPairingResponse::InitPairingResponse(const InitPairingResponse&) = default;
InitPairingResponse& InitPairingResponse::operator=(const InitPairingResponse&) = default;
InitPairingResponse::InitPairingResponse(InitPairingResponse&&) = default;
InitPairingResponse& InitPairingResponse::operator=(InitPairingResponse&&) = default;

// PairingStatusResponse struct out-of-line definitions
PairingStatusResponse::PairingStatusResponse() = default;
PairingStatusResponse::~PairingStatusResponse() = default;
PairingStatusResponse::PairingStatusResponse(const PairingStatusResponse&) = default;
PairingStatusResponse& PairingStatusResponse::operator=(const PairingStatusResponse&) = default;
PairingStatusResponse::PairingStatusResponse(PairingStatusResponse&&) = default;
PairingStatusResponse& PairingStatusResponse::operator=(PairingStatusResponse&&) = default;

namespace {

// Returns the platform identifier for Allow2 API requests.
const char* GetPlatformIdentifier() {
#if BUILDFLAG(IS_WIN)
  return kAllow2PlatformWindows;
#elif BUILDFLAG(IS_MAC)
  return kAllow2PlatformMacOS;
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  return kAllow2PlatformLinux;
#elif BUILDFLAG(IS_IOS)
  return kAllow2PlatformiOS;
#elif BUILDFLAG(IS_ANDROID)
  return kAllow2PlatformAndroid;
#else
  return "unknown";
#endif
}

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("allow2_api", R"(
        semantics {
          sender: "Allow2 Parental Controls"
          description:
            "Communication with Allow2 API for parental control features "
            "including device pairing, usage tracking, and time limit "
            "enforcement."
          trigger:
            "User pairs device with Allow2 account, or periodic usage checks "
            "during browsing."
          data:
            "Device pairing credentials, child ID, and browsing activity "
            "categorization (not URLs)."
          destination: OTHER
          destination_other: "Allow2 API servers"
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature can be disabled in Brave settings under "
            "Parental Freedom."
          policy_exception_justification:
            "Parental control features are opt-in and require explicit "
            "device pairing."
        })");

constexpr size_t kMaxResponseSize = 1024 * 1024;  // 1MB

std::unique_ptr<network::SimpleURLLoader> CreateLoader(
    const GURL& url,
    const std::string& method,
    const std::string& body) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = method;
  request->load_flags = net::LOAD_DO_NOT_SAVE_COOKIES |
                        net::LOAD_BYPASS_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);

  if (!body.empty()) {
    loader->AttachStringForUpload(body, "application/json");
  }

  return loader;
}

}  // namespace

Allow2ApiClient::Allow2ApiClient(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {
  DCHECK(url_loader_factory_);
}

Allow2ApiClient::~Allow2ApiClient() = default;

std::string Allow2ApiClient::GetBaseUrl() const {
  // Check for environment variable override (staging/development)
  const char* override_url = std::getenv(kAllow2BaseUrlEnvVar);
  if (override_url && override_url[0] != '\0') {
    LOG(INFO) << "Allow2: Using override API URL: " << override_url;
    return std::string(override_url);
  }
  LOG(INFO) << "Allow2: Using default API URL: " << kAllow2DefaultApiBaseUrl;
  return std::string(kAllow2DefaultApiBaseUrl);
}

std::string Allow2ApiClient::GetServiceBaseUrl() const {
  // Check for environment variable override (staging/development)
  const char* override_url = std::getenv(kAllow2BaseUrlEnvVar);
  if (override_url && override_url[0] != '\0') {
    return std::string(override_url);
  }
  return std::string(kAllow2DefaultServiceBaseUrl);
}

std::string Allow2ApiClient::BuildUrl(const std::string& endpoint) const {
  return GetBaseUrl() + endpoint;
}

std::string Allow2ApiClient::BuildServiceUrl(const std::string& endpoint) const {
  return GetServiceBaseUrl() + endpoint;
}

void Allow2ApiClient::Check(const Credentials& credentials,
                            uint64_t child_id,
                            const std::vector<ActivityId>& activities,
                            bool log_usage,
                            CheckCallback callback) {
  base::Value::Dict body;
  body.Set(kCheckUserIdKey, credentials.user_id);
  body.Set(kCheckPairIdKey, credentials.pair_id);
  body.Set(kCheckChildIdKey, static_cast<int>(child_id));
  body.Set(kCheckTimezoneKey, GetCurrentTimezone());
  body.Set("platform", GetPlatformIdentifier());
  body.Set("vid", static_cast<int>(kAllow2VersionId));

  base::Value::List activities_list;
  for (const auto& activity : activities) {
    base::Value::Dict activity_dict;
    activity_dict.Set(kActivityIdKey, static_cast<int>(activity));
    activity_dict.Set(kActivityLogKey, log_usage);
    activities_list.Append(std::move(activity_dict));
  }
  body.Set(kCheckActivitiesKey, std::move(activities_list));

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  // Check endpoint uses service.allow2.com for direct access
  GURL url(BuildServiceUrl(kAllow2CheckEndpoint));
  auto loader = CreateLoader(url, "POST", body_json);

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&Allow2ApiClient::OnCheckComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(loader),
                     std::move(callback)),
      kMaxResponseSize);
}

void Allow2ApiClient::RequestTime(const Credentials& credentials,
                                   uint64_t child_id,
                                   ActivityId activity_id,
                                   int minutes,
                                   const std::string& message,
                                   RequestTimeCallback callback) {
  base::Value::Dict body;
  body.Set(kCheckUserIdKey, credentials.user_id);
  body.Set(kCheckPairIdKey, credentials.pair_id);
  body.Set(kCheckChildIdKey, static_cast<int>(child_id));
  body.Set("activityId", static_cast<int>(activity_id));
  body.Set("minutes", minutes);
  body.Set("platform", GetPlatformIdentifier());
  body.Set("vid", static_cast<int>(kAllow2VersionId));
  if (!message.empty()) {
    body.Set("message", message);
  }

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  GURL url(BuildUrl(kAllow2RequestTimeEndpoint));
  auto loader = CreateLoader(url, "POST", body_json);

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&Allow2ApiClient::OnRequestTimeComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(loader),
                     std::move(callback)),
      kMaxResponseSize);
}

void Allow2ApiClient::OnCheckComplete(
    std::unique_ptr<network::SimpleURLLoader> loader,
    CheckCallback callback,
    std::optional<std::string> response_body) {
  int http_status = 0;
  if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    http_status = loader->ResponseInfo()->headers->response_code();
  }

  if (!response_body.has_value() || loader->NetError() != net::OK) {
    CheckResponse response;
    response.success = false;
    response.http_status_code = http_status;
    response.error = base::StringPrintf("Network error: %d", loader->NetError());
    std::move(callback).Run(response);
    return;
  }

  std::move(callback).Run(ParseCheckResponse(response_body.value(), http_status));
}

void Allow2ApiClient::OnRequestTimeComplete(
    std::unique_ptr<network::SimpleURLLoader> loader,
    RequestTimeCallback callback,
    std::optional<std::string> response_body) {
  if (!response_body.has_value() || loader->NetError() != net::OK) {
    RequestTimeResponse response;
    response.success = false;
    response.error = base::StringPrintf("Network error: %d", loader->NetError());
    std::move(callback).Run(response);
    return;
  }

  std::move(callback).Run(ParseRequestTimeResponse(response_body.value()));
}

CheckResponse Allow2ApiClient::ParseCheckResponse(const std::string& json,
                                                   int http_status) {
  CheckResponse response;
  response.http_status_code = http_status;

  if (http_status == 401) {
    response.error = "Unauthorized - credentials invalidated";
    return response;
  }

  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    response.error = "Invalid JSON response";
    return response;
  }

  const base::Value::Dict& dict = parsed->GetDict();

  response.success = true;
  response.result.allowed = dict.FindBool(kCheckAllowedKey).value_or(true);

  // Parse activities for remaining time
  const base::Value::Dict* activities = dict.FindDict(kCheckActivitiesKey);
  if (activities) {
    // Get internet activity (ID 1)
    const base::Value::Dict* internet =
        activities->FindDict(std::to_string(static_cast<int>(ActivityId::kInternet)));
    if (internet) {
      response.result.remaining_seconds =
          internet->FindInt(kCheckRemainingKey).value_or(0);
      response.result.expires =
          static_cast<int64_t>(internet->FindDouble(kCheckExpiresKey).value_or(0));
      response.result.banned = internet->FindBool(kCheckBannedKey).value_or(false);

      // Parse timeblock
      const base::Value::Dict* timeblock = internet->FindDict(kCheckTimeblockKey);
      if (timeblock && !timeblock->FindBool(kCheckAllowedKey).value_or(true)) {
        response.result.allowed = false;
        response.result.block_reason = "Time block active";
      }
    }
  }

  // Parse day types
  const base::Value::Dict* day_types = dict.FindDict(kCheckDayTypesKey);
  if (day_types) {
    const base::Value::Dict* today = day_types->FindDict("today");
    if (today) {
      const std::string* day_name = today->FindString("name");
      if (day_name) {
        response.result.day_type = *day_name;
      }
    }
  }

  // Build block reason if not allowed
  if (!response.result.allowed && response.result.block_reason.empty()) {
    if (response.result.banned) {
      response.result.block_reason = "Activity banned";
    } else if (response.result.remaining_seconds <= 0) {
      response.result.block_reason = "Time limit reached";
    }
  }

  return response;
}

RequestTimeResponse Allow2ApiClient::ParseRequestTimeResponse(
    const std::string& json) {
  RequestTimeResponse response;

  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    response.error = "Invalid JSON response";
    return response;
  }

  const base::Value::Dict& dict = parsed->GetDict();

  const std::string* status = dict.FindString(kPairStatusKey);
  if (!status || *status != kStatusSuccess) {
    response.error = status ? *status : "Unknown error";
    return response;
  }

  response.success = true;

  const std::string* request_id = dict.FindString("requestId");
  if (request_id) {
    response.request_id = *request_id;
  }

  return response;
}

// ============================================================================
// QR/PIN Pairing Implementation
// ============================================================================

void Allow2ApiClient::InitQRPairing(const std::string& device_token,
                                     const std::string& device_name,
                                     InitPairingCallback callback) {
  base::Value::Dict body;
  // uuid is the unique device instance identifier (BRAVE_DESKTOP_xxx)
  body.Set("uuid", device_token);
  body.Set("name", device_name);
  body.Set("platform", GetPlatformIdentifier());
  // deviceToken must be the registered Allow2 version token, not the device UUID
  body.Set(kPairDeviceTokenKey, kAllow2VersionToken);
  body.Set("vid", static_cast<int>(kAllow2VersionId));

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  // QR pairing uses api.allow2.com
  GURL url(BuildUrl("/api/pair/qr/init"));
  auto loader = CreateLoader(url, "POST", body_json);

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&Allow2ApiClient::OnInitPairingComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(loader),
                     true,  // is_qr_pairing
                     std::move(callback)),
      kMaxResponseSize);
}

void Allow2ApiClient::InitPINPairing(const std::string& device_token,
                                      const std::string& device_name,
                                      InitPairingCallback callback) {
  base::Value::Dict body;
  // uuid is the unique device instance identifier (BRAVE_DESKTOP_xxx)
  body.Set("uuid", device_token);
  body.Set("name", device_name);
  body.Set("platform", GetPlatformIdentifier());
  // deviceToken must be the registered Allow2 version token, not the device UUID
  body.Set(kPairDeviceTokenKey, kAllow2VersionToken);
  body.Set("vid", static_cast<int>(kAllow2VersionId));

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  // PIN pairing uses api.allow2.com
  GURL url(BuildUrl("/api/pair/pin/init"));
  auto loader = CreateLoader(url, "POST", body_json);

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&Allow2ApiClient::OnInitPairingComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(loader),
                     false,  // is_qr_pairing
                     std::move(callback)),
      kMaxResponseSize);
}

void Allow2ApiClient::CheckPairingStatus(const std::string& session_id,
                                          PairingStatusCallback callback) {
  // Status endpoint uses GET with session ID in path
  GURL url(BuildUrl("/api/pair/qr/status/" + session_id));
  auto loader = CreateLoader(url, "GET", "");

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&Allow2ApiClient::OnPairingStatusComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(loader),
                     std::move(callback)),
      kMaxResponseSize);
}

void Allow2ApiClient::CancelPairing(const std::string& session_id) {
  // Cancel is fire-and-forget
  base::Value::Dict body;
  body.Set("sessionId", session_id);

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  GURL url(BuildUrl("/api/pair/cancel"));
  auto loader = CreateLoader(url, "POST", body_json);

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce([](std::optional<std::string>) {}),  // Ignore result
      kMaxResponseSize);
}

void Allow2ApiClient::OnInitPairingComplete(
    std::unique_ptr<network::SimpleURLLoader> loader,
    bool is_qr_pairing,
    InitPairingCallback callback,
    std::optional<std::string> response_body) {
  if (!response_body.has_value() || loader->NetError() != net::OK) {
    InitPairingResponse response;
    response.success = false;
    response.error = base::StringPrintf("Network error: %d", loader->NetError());
    std::move(callback).Run(response);
    return;
  }

  std::move(callback).Run(ParseInitPairingResponse(response_body.value(), is_qr_pairing));
}

void Allow2ApiClient::OnPairingStatusComplete(
    std::unique_ptr<network::SimpleURLLoader> loader,
    PairingStatusCallback callback,
    std::optional<std::string> response_body) {
  if (!response_body.has_value() || loader->NetError() != net::OK) {
    PairingStatusResponse response;
    response.completed = false;
    response.error = base::StringPrintf("Network error: %d", loader->NetError());
    std::move(callback).Run(response);
    return;
  }

  std::move(callback).Run(ParsePairingStatusResponse(response_body.value()));
}

InitPairingResponse Allow2ApiClient::ParseInitPairingResponse(
    const std::string& json,
    bool is_qr_pairing) {
  InitPairingResponse response;

  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    response.error = "Invalid JSON response";
    return response;
  }

  const base::Value::Dict& dict = parsed->GetDict();

  const std::string* status = dict.FindString(kPairStatusKey);
  if (!status || *status != kStatusSuccess) {
    // Check for error message in "message" or "error" field
    const std::string* message = dict.FindString("message");
    if (!message) {
      message = dict.FindString("error");
    }
    if (!message) {
      const std::string* code = dict.FindString("code");
      message = code;
    }
    response.error = message ? *message : (status ? *status : "Unknown error");
    return response;
  }

  response.success = true;

  const std::string* session_id = dict.FindString("sessionId");
  if (session_id) {
    response.session_id = *session_id;
  }

  if (is_qr_pairing) {
    // Server returns qrData: { url, deepLink, encodedData }
    const base::Value::Dict* qr_data = dict.FindDict("qrData");
    if (qr_data) {
      const std::string* qr_url = qr_data->FindString("url");
      if (qr_url) {
        response.web_pairing_url = *qr_url;
        // Generate QR code image from the URL
        auto qr_image = GenerateQRCodeDataUrl(*qr_url);
        if (qr_image.has_value()) {
          response.qr_code_url = qr_image.value();
        } else {
          LOG(ERROR) << "Allow2: Failed to generate QR code image";
        }
      }
    } else {
      // Fallback: check for legacy qrCodeUrl field
      const std::string* qr_url = dict.FindString("qrCodeUrl");
      if (qr_url) {
        response.web_pairing_url = *qr_url;
        auto qr_image = GenerateQRCodeDataUrl(*qr_url);
        if (qr_image.has_value()) {
          response.qr_code_url = qr_image.value();
        }
      }
    }
  } else {
    const std::string* pin = dict.FindString("pin");
    if (pin) {
      response.pin_code = *pin;
    }
  }

  response.expires_in = dict.FindInt("expiresIn").value_or(300);

  return response;
}

PairingStatusResponse Allow2ApiClient::ParsePairingStatusResponse(
    const std::string& json) {
  PairingStatusResponse response;

  auto parsed = base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    response.error = "Invalid JSON response";
    return response;
  }

  const base::Value::Dict& dict = parsed->GetDict();

  const std::string* status = dict.FindString(kPairStatusKey);
  if (!status) {
    response.error = "Missing status field";
    return response;
  }

  if (*status == "pending") {
    response.completed = false;
    response.scanned = dict.FindBool("scanned").value_or(false);
    return response;
  }

  if (*status == "completed") {
    response.completed = true;
    response.success = true;

    // Extract credentials
    const base::Value::Dict* credentials = dict.FindDict("credentials");
    if (credentials) {
      const std::string* user_id = credentials->FindString("userId");
      const std::string* pair_id = credentials->FindString("pairId");
      const std::string* pair_token = credentials->FindString("pairToken");

      if (user_id) response.user_id = *user_id;
      if (pair_id) response.pair_id = *pair_id;
      if (pair_token) response.pair_token = *pair_token;
    }

    // Parse children list
    const base::Value::List* children = dict.FindList(kPairChildrenKey);
    if (children) {
      for (const auto& child_value : *children) {
        if (!child_value.is_dict()) {
          continue;
        }

        const base::Value::Dict& child_dict = child_value.GetDict();
        Child child;
        child.id = child_dict.FindInt(kChildIdKey).value_or(0);

        const std::string* name = child_dict.FindString(kChildNameKey);
        if (name) {
          child.name = *name;
        }

        const std::string* pin_hash = child_dict.FindString(kChildPinHashKey);
        if (pin_hash) {
          child.pin_hash = *pin_hash;
        }

        const std::string* pin_salt = child_dict.FindString(kChildPinSaltKey);
        if (pin_salt) {
          child.pin_salt = *pin_salt;
        }

        response.children.push_back(child);
      }
    }

    return response;
  }

  if (*status == "expired") {
    response.completed = true;
    response.success = false;
    response.error = "Pairing session expired";
    return response;
  }

  if (*status == "declined") {
    response.completed = true;
    response.success = false;
    response.error = "Pairing was declined by parent";
    return response;
  }

  response.error = "Unknown status: " + *status;
  return response;
}

}  // namespace allow2

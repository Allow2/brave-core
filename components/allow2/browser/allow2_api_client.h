/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_API_CLIENT_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_API_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/allow2/browser/allow2_service.h"
#include "brave/components/allow2/common/allow2_constants.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace allow2 {

struct Credentials;

// Response from pairing API.
struct PairResponse {
  bool success = false;
  std::string error;
  std::string user_id;
  std::string pair_id;
  std::string pair_token;
  std::vector<Child> children;
};

// Response from check API.
struct CheckResponse {
  bool success = false;
  std::string error;
  int http_status_code = 0;
  CheckResult result;
};

// Response from request time API.
struct RequestTimeResponse {
  bool success = false;
  std::string error;
  std::string request_id;
};

// Response from QR/PIN pairing initialization.
struct InitPairingResponse {
  bool success = false;
  std::string error;
  std::string session_id;
  std::string qr_code_url;  // For QR pairing
  std::string pin_code;      // For PIN pairing
  int expires_in = 0;        // Session expiry in seconds
};

// Response from pairing status check.
struct PairingStatusResponse {
  bool completed = false;
  bool success = false;
  bool scanned = false;  // True if QR code was scanned
  std::string error;
  std::string user_id;
  std::string pair_id;
  std::string pair_token;
  std::vector<Child> children;
};

// HTTP client for Allow2 API.
// Handles all communication with the Allow2 backend.
class Allow2ApiClient {
 public:
  using PairCallback = base::OnceCallback<void(const PairResponse& response)>;
  using CheckCallback = base::OnceCallback<void(const CheckResponse& response)>;
  using RequestTimeCallback =
      base::OnceCallback<void(const RequestTimeResponse& response)>;
  using InitPairingCallback =
      base::OnceCallback<void(const InitPairingResponse& response)>;
  using PairingStatusCallback =
      base::OnceCallback<void(const PairingStatusResponse& response)>;

  explicit Allow2ApiClient(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~Allow2ApiClient();

  Allow2ApiClient(const Allow2ApiClient&) = delete;
  Allow2ApiClient& operator=(const Allow2ApiClient&) = delete;

  // Pair device with parent account using email/password.
  void PairDevice(const std::string& email,
                  const std::string& password,
                  const std::string& device_token,
                  const std::string& device_name,
                  PairCallback callback);

  // Pair device using pairing code.
  void PairDeviceWithCode(const std::string& pairing_code,
                          const std::string& device_token,
                          const std::string& device_name,
                          PairCallback callback);

  // Check current allowances.
  void Check(const Credentials& credentials,
             uint64_t child_id,
             const std::vector<ActivityId>& activities,
             bool log_usage,
             CheckCallback callback);

  // Request more time from parent.
  void RequestTime(const Credentials& credentials,
                   uint64_t child_id,
                   ActivityId activity_id,
                   int minutes,
                   const std::string& message,
                   RequestTimeCallback callback);

  // ============================================================================
  // QR/PIN Pairing (Device never handles parent credentials)
  // ============================================================================

  // Initialize QR code pairing session.
  // Device displays QR code, parent scans with their Allow2 app.
  // Parent authenticates with passkey/biometrics on their device.
  void InitQRPairing(const std::string& device_token,
                     const std::string& device_name,
                     InitPairingCallback callback);

  // Initialize PIN code pairing session.
  // Device displays 6-digit PIN, parent enters in their Allow2 app.
  void InitPINPairing(const std::string& device_token,
                      const std::string& device_name,
                      InitPairingCallback callback);

  // Check status of pairing session (poll until completed).
  void CheckPairingStatus(const std::string& session_id,
                          PairingStatusCallback callback);

  // Cancel an active pairing session.
  void CancelPairing(const std::string& session_id);

 private:
  // URL loader completion handlers.
  void OnPairComplete(std::unique_ptr<network::SimpleURLLoader> loader,
                      PairCallback callback,
                      std::unique_ptr<std::string> response_body);

  void OnCheckComplete(std::unique_ptr<network::SimpleURLLoader> loader,
                       CheckCallback callback,
                       std::unique_ptr<std::string> response_body);

  void OnRequestTimeComplete(std::unique_ptr<network::SimpleURLLoader> loader,
                             RequestTimeCallback callback,
                             std::unique_ptr<std::string> response_body);

  void OnInitPairingComplete(std::unique_ptr<network::SimpleURLLoader> loader,
                             bool is_qr_pairing,
                             InitPairingCallback callback,
                             std::unique_ptr<std::string> response_body);

  void OnPairingStatusComplete(std::unique_ptr<network::SimpleURLLoader> loader,
                               PairingStatusCallback callback,
                               std::unique_ptr<std::string> response_body);

  // Parse API responses.
  PairResponse ParsePairResponse(const std::string& json);
  CheckResponse ParseCheckResponse(const std::string& json, int http_status);
  RequestTimeResponse ParseRequestTimeResponse(const std::string& json);
  InitPairingResponse ParseInitPairingResponse(const std::string& json,
                                               bool is_qr_pairing);
  PairingStatusResponse ParsePairingStatusResponse(const std::string& json);

  // Get base URLs (checks for env override).
  std::string GetBaseUrl() const;
  std::string GetServiceBaseUrl() const;

  // Build request URLs.
  std::string BuildUrl(const std::string& endpoint) const;
  std::string BuildServiceUrl(const std::string& endpoint) const;

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  base::WeakPtrFactory<Allow2ApiClient> weak_ptr_factory_{this};
};

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_API_CLIENT_H_

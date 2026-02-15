/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_QR_CODE_GENERATOR_H_
#define BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_QR_CODE_GENERATOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace allow2 {

// Generates a QR code image from a URL and returns it as a base64-encoded
// PNG data URL suitable for use in an HTML <img> src attribute.
//
// Example output: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAA..."
//
// Returns std::nullopt if QR code generation fails.
std::optional<std::string> GenerateQRCodeDataUrl(const std::string& data);

// Generates a QR code image from a URL and returns the raw PNG bytes.
// Returns std::nullopt if QR code generation fails.
std::optional<std::vector<uint8_t>> GenerateQRCodePNG(const std::string& data);

}  // namespace allow2

#endif  // BRAVE_COMPONENTS_ALLOW2_BROWSER_ALLOW2_QR_CODE_GENERATOR_H_

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/allow2/browser/allow2_qr_code_generator.h"

#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/logging.h"
#include "components/qr_code_generator/bitmap_generator.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"

namespace allow2 {

std::optional<std::vector<uint8_t>> GenerateQRCodePNG(const std::string& data) {
  if (data.empty()) {
    LOG(ERROR) << "Allow2: Cannot generate QR code for empty data";
    return std::nullopt;
  }

  // Convert string to bytes
  base::span<const uint8_t> input_data(
      reinterpret_cast<const uint8_t*>(data.data()), data.size());

  // Generate QR code bitmap using Chromium's built-in generator
  auto result = qr_code_generator::GenerateBitmap(
      input_data,
      qr_code_generator::ModuleStyle::kSquares,
      qr_code_generator::LocatorStyle::kSquare,
      qr_code_generator::CenterImage::kNoCenterImage,
      qr_code_generator::QuietZone::kIncluded);

  if (!result.has_value()) {
    LOG(ERROR) << "Allow2: QR code generation failed";
    return std::nullopt;
  }

  SkBitmap bitmap = std::move(result.value());

  // Encode bitmap to PNG
  auto png_data = gfx::PNGCodec::EncodeBGRASkBitmap(
      bitmap, /*discard_transparency=*/false);

  if (!png_data.has_value()) {
    LOG(ERROR) << "Allow2: PNG encoding failed";
    return std::nullopt;
  }

  return png_data;
}

std::optional<std::string> GenerateQRCodeDataUrl(const std::string& data) {
  auto png_data = GenerateQRCodePNG(data);
  if (!png_data.has_value()) {
    return std::nullopt;
  }

  // Encode PNG bytes to base64
  std::string base64_encoded = base::Base64Encode(png_data.value());

  // Return as data URL
  return "data:image/png;base64," + base64_encoded;
}

}  // namespace allow2

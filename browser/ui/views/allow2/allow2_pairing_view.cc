/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/allow2/allow2_pairing_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace allow2 {

namespace {

// Layout constants.
constexpr int kDialogWidth = 400;
constexpr int kDialogPadding = 32;
constexpr int kElementSpacing = 16;
constexpr int kQRCodeSize = 180;
constexpr int kTitleFontSize = 20;
constexpr int kPinFontSize = 24;

// Colors.
constexpr SkColor kBackgroundColor = SK_ColorWHITE;
constexpr SkColor kTitleColor = SkColorSetRGB(0x33, 0x33, 0x33);
constexpr SkColor kTextColor = SkColorSetRGB(0x66, 0x66, 0x66);
constexpr SkColor kPinColor = gfx::kGoogleBlue600;
constexpr SkColor kQRCodeColor = SK_ColorBLACK;
constexpr SkColor kQRCodeBackground = SK_ColorWHITE;
constexpr SkColor kSuccessColor = gfx::kGoogleGreen600;

// Polling interval in milliseconds.
constexpr int kPollIntervalMs = 2000;

// Singleton tracking for IsShowing().
static bool g_is_showing = false;

}  // namespace

// static
void Allow2PairingView::Show(
    Browser* browser,
    const std::string& qr_code_url,
    const std::string& pin_code,
    const std::string& session_id,
    base::RepeatingCallback<void(const std::string&)> poll_callback,
    PairingCompleteCallback complete_callback,
    PairingCancelledCallback cancelled_callback) {
  if (!browser) {
    return;
  }

  auto* view = new Allow2PairingView(
      browser, qr_code_url, pin_code, session_id,
      std::move(poll_callback), std::move(complete_callback),
      std::move(cancelled_callback));

  constrained_window::CreateBrowserModalDialogViews(
      view, browser->window()->GetNativeWindow())
      ->Show();

  g_is_showing = true;
}

// static
bool Allow2PairingView::IsShowing() {
  return g_is_showing;
}

Allow2PairingView::Allow2PairingView(
    Browser* browser,
    const std::string& qr_code_url,
    const std::string& pin_code,
    const std::string& session_id,
    base::RepeatingCallback<void(const std::string&)> poll_callback,
    PairingCompleteCallback complete_callback,
    PairingCancelledCallback cancelled_callback)
    : browser_(browser),
      session_id_(session_id),
      poll_callback_(std::move(poll_callback)),
      complete_callback_(std::move(complete_callback)),
      cancelled_callback_(std::move(cancelled_callback)) {
  // Configure dialog.
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  SetCancelCallback(base::BindOnce(&Allow2PairingView::OnDialogCancelled,
                                   base::Unretained(this)));
  SetBackground(views::CreateSolidBackground(kBackgroundColor));

  // Main layout.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kDialogPadding, kDialogPadding), kElementSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Title: "Connect to Allow2"
  title_label_ =
      AddChildView(std::make_unique<views::Label>(u"Connect to Allow2"));
  title_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kTitleFontSize - gfx::FontList().GetFontSize()));
  title_label_->SetFontList(
      title_label_->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  title_label_->SetEnabledColor(kTitleColor);

  // QR code image.
  qr_code_view_ = AddChildView(std::make_unique<views::ImageView>());
  qr_code_view_->SetPreferredSize(gfx::Size(kQRCodeSize, kQRCodeSize));
  qr_code_view_->SetBackground(
      views::CreateSolidBackground(kQRCodeBackground));

  // Generate and set QR code.
  gfx::ImageSkia qr_image = GenerateQRCode(qr_code_url, kQRCodeSize);
  qr_code_view_->SetImage(ui::ImageModel::FromImageSkia(qr_image));

  // Instruction.
  instruction_label_ = AddChildView(std::make_unique<views::Label>(
      u"Ask your parent to scan this in their Allow2 app"));
  instruction_label_->SetEnabledColor(kTextColor);
  instruction_label_->SetMultiLine(true);
  instruction_label_->SetMaximumWidth(kDialogWidth - 2 * kDialogPadding);
  instruction_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  // Divider with "or".
  auto* divider_container = AddChildView(std::make_unique<views::View>());
  auto* divider_layout =
      divider_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 16));
  divider_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  divider_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* left_line = divider_container->AddChildView(std::make_unique<views::View>());
  left_line->SetPreferredSize(gfx::Size(60, 1));
  left_line->SetBackground(views::CreateSolidBackground(kTextColor));

  or_label_ =
      divider_container->AddChildView(std::make_unique<views::Label>(u"or"));
  or_label_->SetEnabledColor(kTextColor);

  auto* right_line = divider_container->AddChildView(std::make_unique<views::View>());
  right_line->SetPreferredSize(gfx::Size(60, 1));
  right_line->SetBackground(views::CreateSolidBackground(kTextColor));

  // PIN code display.
  auto* pin_container = AddChildView(std::make_unique<views::View>());
  auto* pin_layout =
      pin_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  pin_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  auto* show_code_label =
      pin_container->AddChildView(std::make_unique<views::Label>(u"show code:"));
  show_code_label->SetEnabledColor(kTextColor);

  // Format PIN with spaces between digits.
  std::u16string formatted_pin;
  for (size_t i = 0; i < pin_code.length(); ++i) {
    if (i > 0) {
      formatted_pin += u" ";
    }
    formatted_pin += base::UTF8ToUTF16(std::string(1, pin_code[i]));
  }

  pin_label_ = pin_container->AddChildView(
      std::make_unique<views::Label>(formatted_pin));
  pin_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kPinFontSize - gfx::FontList().GetFontSize()));
  pin_label_->SetFontList(
      pin_label_->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  pin_label_->SetEnabledColor(kPinColor);

  // Status label (for "Parent scanned" message).
  status_label_ = AddChildView(std::make_unique<views::Label>(u""));
  status_label_->SetEnabledColor(kSuccessColor);
  status_label_->SetVisible(false);

  // Progress bar.
  progress_bar_ = AddChildView(std::make_unique<views::ProgressBar>());
  progress_bar_->SetPreferredHeight(4);
  progress_bar_->SetPreferredSize(
      gfx::Size(kDialogWidth - 2 * kDialogPadding, 4));
  progress_bar_->SetValue(-1);  // Indeterminate.
  progress_bar_->SetVisible(false);

  // Set preferred size.
  SetPreferredSize(gfx::Size(kDialogWidth, 450));

  // Start polling for pairing status.
  StartPolling();
}

Allow2PairingView::~Allow2PairingView() {
  StopPolling();
  g_is_showing = false;
}

ui::mojom::ModalType Allow2PairingView::GetModalType() const {
  return ui::mojom::ModalType::kWindow;
}

bool Allow2PairingView::ShouldShowCloseButton() const {
  return false;
}

bool Allow2PairingView::ShouldShowWindowTitle() const {
  return false;
}

void Allow2PairingView::OnDialogCancelled() {
  StopPolling();
  if (cancelled_callback_) {
    std::move(cancelled_callback_).Run();
  }
}

gfx::ImageSkia Allow2PairingView::GenerateQRCode(const std::string& url,
                                                  int size) {
  // Simple placeholder QR code visualization.
  // In a full implementation, use a QR code library like libqrencode.
  // For now, create a simple pattern to indicate where the QR code will be.

  SkBitmap bitmap;
  bitmap.allocN32Pixels(size, size);
  SkCanvas canvas(bitmap);
  canvas.drawColor(kQRCodeBackground);

  // Draw a simple pattern to represent QR code.
  SkPaint paint;
  paint.setColor(kQRCodeColor);
  paint.setAntiAlias(true);

  // Module size for fake QR pattern.
  const int module_size = size / 25;
  const int quiet_zone = module_size * 2;

  // Draw corner position patterns.
  auto draw_position_pattern = [&](int x, int y) {
    // Outer square.
    canvas.drawRect(SkRect::MakeXYWH(x, y, module_size * 7, module_size * 7),
                    paint);
    // Inner white.
    paint.setColor(kQRCodeBackground);
    canvas.drawRect(
        SkRect::MakeXYWH(x + module_size, y + module_size, module_size * 5,
                         module_size * 5),
        paint);
    // Center.
    paint.setColor(kQRCodeColor);
    canvas.drawRect(
        SkRect::MakeXYWH(x + module_size * 2, y + module_size * 2,
                         module_size * 3, module_size * 3),
        paint);
  };

  draw_position_pattern(quiet_zone, quiet_zone);
  draw_position_pattern(size - quiet_zone - module_size * 7, quiet_zone);
  draw_position_pattern(quiet_zone, size - quiet_zone - module_size * 7);

  // Add some random-looking data modules.
  paint.setColor(kQRCodeColor);
  for (int row = 0; row < 25; ++row) {
    for (int col = 0; col < 25; ++col) {
      // Skip position patterns.
      if ((row < 9 && col < 9) || (row < 9 && col > 15) ||
          (row > 15 && col < 9)) {
        continue;
      }

      // Simple hash-based pattern.
      size_t hash = std::hash<std::string>{}(url);
      if ((hash + row * 31 + col * 17) % 3 == 0) {
        int x = quiet_zone + col * module_size;
        int y = quiet_zone + row * module_size;
        canvas.drawRect(SkRect::MakeXYWH(x, y, module_size, module_size),
                        paint);
      }
    }
  }

  return gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
}

void Allow2PairingView::StartPolling() {
  poll_timer_.Start(FROM_HERE, base::Milliseconds(kPollIntervalMs),
                    base::BindRepeating(&Allow2PairingView::OnPollTimer,
                                        weak_ptr_factory_.GetWeakPtr()));
}

void Allow2PairingView::StopPolling() {
  poll_timer_.Stop();
}

void Allow2PairingView::OnPollTimer() {
  if (poll_callback_) {
    poll_callback_.Run(session_id_);
  }
}

void Allow2PairingView::OnPairingComplete(bool success,
                                          const std::string& error) {
  StopPolling();

  if (complete_callback_) {
    std::move(complete_callback_).Run(success);
  }

  GetWidget()->Close();
}

void Allow2PairingView::OnQRCodeScanned() {
  // Show "Parent scanned" status.
  if (status_label_) {
    status_label_->SetText(u"Parent scanned - waiting for approval...");
    status_label_->SetVisible(true);
  }

  // Show progress bar.
  if (progress_bar_) {
    progress_bar_->SetVisible(true);
  }
}

BEGIN_METADATA(Allow2PairingView)
END_METADATA

}  // namespace allow2

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/allow2/allow2_block_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/widget/widget.h"

namespace allow2 {

namespace {

// Layout constants.
constexpr int kDialogWidth = 500;
constexpr int kDialogPadding = 40;
constexpr int kElementSpacing = 20;
constexpr int kButtonSpacing = 16;
constexpr int kTitleFontSize = 28;
constexpr int kReasonFontSize = 16;
constexpr int kDayTypeFontSize = 14;

// Colors.
constexpr SkColor kBackgroundColor = SkColorSetRGB(0xF5, 0xF5, 0xF5);
constexpr SkColor kTitleColor = SkColorSetRGB(0x33, 0x33, 0x33);
constexpr SkColor kTextColor = SkColorSetRGB(0x66, 0x66, 0x66);
constexpr SkColor kDayTypeColor = SkColorSetRGB(0x99, 0x99, 0x99);

// Singleton tracking for IsShowing().
static bool g_is_showing = false;

}  // namespace

// static
void Allow2BlockView::Show(Browser* browser,
                           const std::string& reason,
                           const std::string& day_type,
                           RequestTimeCallback request_time_callback,
                           SwitchUserCallback switch_user_callback) {
  if (!browser) {
    return;
  }

  // Create and show the modal dialog.
  auto* view = new Allow2BlockView(browser, reason, day_type,
                                   std::move(request_time_callback),
                                   std::move(switch_user_callback));

  constrained_window::CreateBrowserModalDialogViews(
      view, browser->window()->GetNativeWindow())
      ->Show();

  g_is_showing = true;
}

// static
bool Allow2BlockView::IsShowing() {
  return g_is_showing;
}

Allow2BlockView::Allow2BlockView(Browser* browser,
                                 const std::string& reason,
                                 const std::string& day_type,
                                 RequestTimeCallback request_time_callback,
                                 SwitchUserCallback switch_user_callback)
    : browser_(browser),
      request_time_callback_(std::move(request_time_callback)),
      switch_user_callback_(std::move(switch_user_callback)) {
  // Configure dialog.
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetBackground(views::CreateSolidBackground(kBackgroundColor));

  // Set up main layout.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kDialogPadding, kDialogPadding), kElementSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Blocked icon (using text for now, could use an icon resource).
  auto* icon_label = AddChildView(std::make_unique<views::Label>(u"\u26D4"));
  icon_label->SetFontList(gfx::FontList().DeriveWithSizeDelta(40));
  icon_label->SetEnabledColor(SkColorSetRGB(0xE0, 0x4B, 0x4B));

  // Title: "Time's Up!"
  title_label_ =
      AddChildView(std::make_unique<views::Label>(u"Time's Up!"));
  title_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kTitleFontSize - gfx::FontList().GetFontSize()));
  title_label_->SetFontList(
      title_label_->font_list().DeriveWithWeight(gfx::Font::Weight::BOLD));
  title_label_->SetEnabledColor(kTitleColor);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  // Block reason.
  std::u16string reason_text =
      reason.empty() ? u"Internet time has ended" : base::UTF8ToUTF16(reason);
  reason_label_ = AddChildView(std::make_unique<views::Label>(reason_text));
  reason_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kReasonFontSize - gfx::FontList().GetFontSize()));
  reason_label_->SetEnabledColor(kTextColor);
  reason_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  reason_label_->SetMultiLine(true);
  reason_label_->SetMaximumWidth(kDialogWidth - 2 * kDialogPadding);

  // Day type (e.g., "School Night").
  if (!day_type.empty()) {
    std::u16string day_type_text = u"(" + base::UTF8ToUTF16(day_type) + u")";
    day_type_label_ =
        AddChildView(std::make_unique<views::Label>(day_type_text));
    day_type_label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
        kDayTypeFontSize - gfx::FontList().GetFontSize()));
    day_type_label_->SetFontList(
        day_type_label_->font_list().DeriveWithStyle(gfx::Font::ITALIC));
    day_type_label_->SetEnabledColor(kDayTypeColor);
  }

  // Spacer.
  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetPreferredSize(gfx::Size(1, kElementSpacing));

  // Button container.
  auto* button_container = AddChildView(std::make_unique<views::View>());
  auto* button_layout =
      button_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kButtonSpacing));
  button_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  // "Request More Time" button.
  request_time_button_ = button_container->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&Allow2BlockView::OnRequestTimeClicked,
                              base::Unretained(this)),
          u"Request More Time"));
  request_time_button_->SetProminent(true);
  request_time_button_->SetStyle(ui::ButtonStyle::kProminent);

  // "Switch User" button.
  switch_user_button_ = button_container->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&Allow2BlockView::OnSwitchUserClicked,
                              base::Unretained(this)),
          u"Switch User"));

  // "Why am I blocked?" link.
  auto* link_container = AddChildView(std::make_unique<views::View>());
  link_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));

  why_blocked_link_ =
      link_container->AddChildView(std::make_unique<views::StyledLabel>());
  why_blocked_link_->SetText(u"Why am I blocked?");
  views::StyledLabel::RangeStyleInfo link_style;
  link_style.override_color = gfx::kGoogleBlue600;
  link_style.text_style = views::style::STYLE_LINK;
  why_blocked_link_->AddStyleRange(
      gfx::Range(0, why_blocked_link_->GetText().length()), link_style);
  why_blocked_link_->SetTextContext(views::style::CONTEXT_DIALOG_BODY_TEXT);

  // Set preferred size.
  SetPreferredSize(gfx::Size(kDialogWidth, 400));
}

Allow2BlockView::~Allow2BlockView() {
  g_is_showing = false;
}

ui::mojom::ModalType Allow2BlockView::GetModalType() const {
  return ui::mojom::ModalType::kWindow;
}

bool Allow2BlockView::ShouldShowCloseButton() const {
  // No close button - can only dismiss via actions.
  return false;
}

bool Allow2BlockView::ShouldShowWindowTitle() const {
  // No window title - we have our own title label.
  return false;
}

void Allow2BlockView::SetBlockReason(const std::string& reason) {
  if (reason_label_) {
    std::u16string reason_text =
        reason.empty() ? u"Internet time has ended" : base::UTF8ToUTF16(reason);
    reason_label_->SetText(reason_text);
  }
}

void Allow2BlockView::SetDayType(const std::string& day_type) {
  if (day_type_label_) {
    if (day_type.empty()) {
      day_type_label_->SetVisible(false);
    } else {
      std::u16string day_type_text = u"(" + base::UTF8ToUTF16(day_type) + u")";
      day_type_label_->SetText(day_type_text);
      day_type_label_->SetVisible(true);
    }
  }
}

void Allow2BlockView::OnRequestTimeClicked() {
  if (request_time_callback_) {
    // Request 30 minutes with a default message.
    // In a full implementation, this would show a dialog to select duration
    // and enter a message.
    std::move(request_time_callback_).Run(30, "Please can I have more time?");
  }

  // Close the dialog after requesting time.
  // The block will reappear if the request is denied or expires.
  GetWidget()->Close();
}

void Allow2BlockView::OnSwitchUserClicked() {
  if (switch_user_callback_) {
    std::move(switch_user_callback_).Run();
  }

  // Close the dialog - child selection will appear.
  GetWidget()->Close();
}

void Allow2BlockView::OnWhyBlockedClicked() {
  // In a full implementation, this would show an informational dialog
  // explaining the blocking reason and Allow2 parental controls.
  // For now, we do nothing.
}

BEGIN_METADATA(Allow2BlockView)
END_METADATA

}  // namespace allow2

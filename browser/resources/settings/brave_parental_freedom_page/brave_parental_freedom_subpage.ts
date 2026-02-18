// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import {PrefsMixin} from '/shared/settings/prefs/prefs_mixin.js'
import {I18nMixin} from 'chrome://resources/cr_elements/i18n_mixin.js'
import {WebUiListenerMixin} from 'chrome://resources/cr_elements/web_ui_listener_mixin.js'
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'

import {loadTimeData} from '../i18n_setup.js'
import {RouteObserverMixin} from '../router.js'

import {
  BraveParentalFreedomBrowserProxy,
  BraveParentalFreedomBrowserProxyImpl,
  PairingSession
} from './brave_parental_freedom_browser_proxy.js'

import {getTemplate} from './brave_parental_freedom_subpage.html.js'
import type {CrToastElement} from 'chrome://resources/cr_elements/cr_toast/cr_toast.js'

const SettingsBraveParentalFreedomPageBase =
  I18nMixin(RouteObserverMixin(WebUiListenerMixin(PrefsMixin(PolymerElement))))

/**
 * 'settings-brave-parental-freedom-subpage' is the settings page containing
 * Brave's Allow2 parental controls settings.
 */
class SettingsBraveParentalFreedomPage
  extends SettingsBraveParentalFreedomPageBase {
  static get is() {
    return 'settings-brave-parental-freedom-subpage'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      isPaired_: {
        type: Boolean,
        value: false,
      },

      pairingSession_: {
        type: Object,
        value: null,
      },

      pollingInterval_: {
        type: Number,
        value: null,
      },

      isPairingLoading_: {
        type: Boolean,
        value: false,
      },

      toastMessage_: {
        type: String,
        value: '',
      },
    }
  }

  private declare isPaired_: boolean
  private declare pairingSession_: PairingSession | null
  private declare pollingInterval_: number | null
  private declare isPairingLoading_: boolean
  private declare toastMessage_: string
  private pairingTimeout_: number | null = null
  private promoFrameTimeout_: number | null = null
  private promoFrameLoaded_: boolean = false

  private browserProxy_: BraveParentalFreedomBrowserProxy =
    BraveParentalFreedomBrowserProxyImpl.getInstance()

  override ready() {
    super.ready()

    if (loadTimeData.getBoolean('shouldExposeElementsForTesting')) {
      window.testing = window.testing || {}
      window.testing.parentalFreedomSubpage = this.shadowRoot
    }

    // Load initial pairing state
    this.loadPairingStatus_()

    // Listen for state changes from C++
    this.addWebUiListener('pairing-state-changed', () => {
      this.loadPairingStatus_()
    })

    // Set timeout for promo frame - show fallback if it doesn't load in 5 seconds
    this.promoFrameTimeout_ = window.setTimeout(() => {
      if (!this.promoFrameLoaded_) {
        this.showPromoFallback_()
      }
    }, 5000)
  }

  override currentRouteChanged() {
    // Refresh status when navigating to this page
    this.loadPairingStatus_()
  }

  override disconnectedCallback() {
    super.disconnectedCallback()
    this.stopPolling_()
    this.clearPairingTimeout_()
    if (this.promoFrameTimeout_ !== null) {
      window.clearTimeout(this.promoFrameTimeout_)
      this.promoFrameTimeout_ = null
    }
  }

  private async loadPairingStatus_() {
    const status = await this.browserProxy_.getPairingStatus()
    this.isPaired_ = status.isPaired
  }

  private async onStartPairing_() {
    if (this.isPairingLoading_) {
      return
    }

    this.isPairingLoading_ = true
    this.clearPairingTimeout_()

    // Set a timeout to reset state if no response
    this.pairingTimeout_ = window.setTimeout(() => {
      if (this.isPairingLoading_ && !this.pairingSession_) {
        console.error('Pairing request timed out')
        this.isPairingLoading_ = false
        this.showToast_('Connection timed out. Please check your internet connection and try again.')
      }
    }, 30000)

    try {
      const deviceName = 'Brave Browser'
      // Use QR pairing to get a QR code image from the C++ backend
      const result = await this.browserProxy_.initQRPairing(deviceName)

      this.clearPairingTimeout_()

      if (result.success) {
        this.pairingSession_ = result
        this.isPairingLoading_ = false
        this.startPolling_()
        // Wait for DOM to update before setting QR code image
        // Use requestAnimationFrame to ensure the template has rendered
        requestAnimationFrame(() => {
          requestAnimationFrame(() => {
            this.updateQRCode_()
          })
        })
      } else {
        console.error('Failed to start pairing:', result.error)
        this.isPairingLoading_ = false
        this.showToast_(result.error || 'Failed to start pairing. Please try again.')
      }
    } catch (error) {
      console.error('Pairing error:', error)
      this.clearPairingTimeout_()
      this.isPairingLoading_ = false
      this.showToast_('An unexpected error occurred. Please try again.')
    }
  }

  private clearPairingTimeout_() {
    if (this.pairingTimeout_ !== null) {
      window.clearTimeout(this.pairingTimeout_)
      this.pairingTimeout_ = null
    }
  }

  private updateQRCode_() {
    if (!this.pairingSession_) {
      return
    }
    const qrCodeImg = this.shadowRoot?.getElementById('qrCodeImage') as HTMLImageElement
    if (qrCodeImg && this.pairingSession_.qrCodeUrl) {
      // Use the QR code image generated by the C++ backend (base64-encoded PNG)
      qrCodeImg.src = this.pairingSession_.qrCodeUrl
    } else if (this.pairingSession_.qrCodeUrl) {
      // Element not ready yet, retry after a short delay
      setTimeout(() => this.updateQRCode_(), 100)
    }
  }

  private onCancelPairing_() {
    if (this.pairingSession_) {
      this.browserProxy_.cancelPairing(this.pairingSession_.sessionId)
      this.pairingSession_ = null
      this.stopPolling_()
    }
  }

  private startPolling_() {
    this.stopPolling_()
    this.pollingInterval_ = window.setInterval(() => {
      this.checkPairingStatus_()
    }, 3000)
  }

  private stopPolling_() {
    if (this.pollingInterval_ !== null) {
      window.clearInterval(this.pollingInterval_)
      this.pollingInterval_ = null
    }
  }

  private async checkPairingStatus_() {
    if (!this.pairingSession_) {
      return
    }

    const result = await this.browserProxy_.checkPairingStatus(
      this.pairingSession_.sessionId
    )

    if (result.completed) {
      this.stopPolling_()
      this.pairingSession_ = null

      if (result.success) {
        this.loadPairingStatus_()
      } else {
        console.error('Pairing failed:', result.error)
        this.showToast_(result.error || 'Pairing failed. Please try again.')
      }
    }
  }

  private showToast_(message: string) {
    this.toastMessage_ = message
    const toast = this.shadowRoot?.querySelector<CrToastElement>('#errorToast')
    if (toast) {
      toast.show()
    }
  }

  private onPromoFrameError_() {
    this.showPromoFallback_()
  }

  private onPromoFrameLoad_() {
    // Mark as loaded and clear the timeout
    this.promoFrameLoaded_ = true
    if (this.promoFrameTimeout_ !== null) {
      window.clearTimeout(this.promoFrameTimeout_)
      this.promoFrameTimeout_ = null
    }
    // Hide fallback since iframe loaded successfully
    this.hidePromoFallback_()
  }

  private showPromoFallback_() {
    const iframe = this.shadowRoot?.getElementById('allow2PromoFrame')
    const fallback = this.shadowRoot?.getElementById('promoFallback')
    if (iframe) {
      iframe.style.display = 'none'
    }
    if (fallback) {
      fallback.classList.add('visible')
    }
  }

  private hidePromoFallback_() {
    const fallback = this.shadowRoot?.getElementById('promoFallback')
    if (fallback) {
      fallback.classList.remove('visible')
    }
  }

  private getPairButtonText_(isLoading: boolean): string {
    return isLoading ? 'Connecting...' : 'Pair with Allow2'
  }

  private getWebPairingUrl_(session: PairingSession | null): string {
    if (!session) {
      return 'https://app.allow2.com/pair'
    }
    // Use the server-provided URL which includes all pairing parameters
    // This URL is also encoded in the QR code for Universal/App Link deep linking
    if (session.webPairingUrl) {
      return session.webPairingUrl
    }
    // Fallback: generate URL client-side if not provided by server
    const params = new URLSearchParams({
      sessionId: session.sessionId,
      pin: session.pinCode,
      deviceName: 'Brave Browser'
    })
    return `https://app.allow2.com/pair?${params.toString()}`
  }
}

customElements.define(
  SettingsBraveParentalFreedomPage.is,
  SettingsBraveParentalFreedomPage
)

// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import {sendWithPromise} from 'chrome://resources/js/cr.js'

export interface Child {
  id: number
  name: string
}

export interface PairingStatus {
  isPaired: boolean
  children: Child[]
  currentChild: Child | null
}

export interface PairingSession {
  success: boolean
  sessionId: string
  qrCodeUrl: string      // Base64 QR code image data
  webPairingUrl: string  // Universal link URL (encoded in QR code)
  pinCode: string
  error: string
}

export interface BlockingStatus {
  isBlocked: boolean
  reason: string
  remainingSeconds: number
}

export interface BraveParentalFreedomBrowserProxy {
  /**
   * Gets the current pairing status.
   */
  getPairingStatus(): Promise<PairingStatus>

  /**
   * Initializes QR code pairing.
   */
  initQRPairing(deviceName: string): Promise<PairingSession>

  /**
   * Initializes PIN code pairing.
   */
  initPINPairing(deviceName: string): Promise<PairingSession>

  /**
   * Checks the pairing status for a session.
   */
  checkPairingStatus(sessionId: string): Promise<{
    completed: boolean
    success: boolean
    error: string
  }>

  /**
   * Cancels an active pairing session.
   */
  cancelPairing(sessionId: string): void

  /**
   * Selects a child for the current session.
   */
  selectChild(childId: number, pin: string): Promise<{
    success: boolean
    error: string
  }>

  /**
   * Clears the current child selection.
   */
  clearCurrentChild(): void

  /**
   * Gets whether Allow2 is enabled.
   */
  getAllow2Enabled(): Promise<boolean>

  /**
   * Sets whether Allow2 is enabled.
   */
  setAllow2Enabled(enabled: boolean): void

  /**
   * Gets the current blocking status.
   */
  getBlockingStatus(): Promise<BlockingStatus>

  /**
   * Requests more time from parent.
   */
  requestMoreTime(minutes: number, message: string): Promise<{
    success: boolean
    error: string
  }>
}

export class BraveParentalFreedomBrowserProxyImpl
  implements BraveParentalFreedomBrowserProxy {
  getPairingStatus(): Promise<PairingStatus> {
    return sendWithPromise('getPairingStatus')
  }

  initQRPairing(deviceName: string): Promise<PairingSession> {
    return sendWithPromise('initQRPairing', deviceName)
  }

  initPINPairing(deviceName: string): Promise<PairingSession> {
    return sendWithPromise('initPINPairing', deviceName)
  }

  checkPairingStatus(sessionId: string): Promise<{
    completed: boolean
    success: boolean
    error: string
  }> {
    return sendWithPromise('checkPairingStatus', sessionId)
  }

  cancelPairing(sessionId: string): void {
    chrome.send('cancelPairing', [sessionId])
  }

  selectChild(childId: number, pin: string): Promise<{
    success: boolean
    error: string
  }> {
    return sendWithPromise('selectChild', childId, pin)
  }

  clearCurrentChild(): void {
    chrome.send('clearCurrentChild')
  }

  getAllow2Enabled(): Promise<boolean> {
    return sendWithPromise('getAllow2Enabled')
  }

  setAllow2Enabled(enabled: boolean): void {
    chrome.send('setAllow2Enabled', [enabled])
  }

  getBlockingStatus(): Promise<BlockingStatus> {
    return sendWithPromise('getBlockingStatus')
  }

  requestMoreTime(minutes: number, message: string): Promise<{
    success: boolean
    error: string
  }> {
    return sendWithPromise('requestMoreTime', minutes, message)
  }

  static getInstance(): BraveParentalFreedomBrowserProxy {
    return instance || (instance = new BraveParentalFreedomBrowserProxyImpl())
  }

  static setInstance(obj: BraveParentalFreedomBrowserProxy) {
    instance = obj
  }
}

let instance: BraveParentalFreedomBrowserProxy | null = null

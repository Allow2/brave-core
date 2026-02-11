/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.models

/**
 * Sealed class representing the state of the pairing process.
 */
sealed class Allow2PairingState {
    /**
     * Initial state, pairing not started.
     */
    object Idle : Allow2PairingState()

    /**
     * Initializing pairing session with the server.
     */
    object Initializing : Allow2PairingState()

    /**
     * QR code is ready for scanning.
     */
    data class QrCodeReady(
        val sessionId: String,
        val qrCodeUrl: String,
        val pin: String,
        val expiresIn: Long
    ) : Allow2PairingState()

    /**
     * Parent has scanned the QR code, waiting for confirmation.
     */
    data class Scanned(
        val sessionId: String
    ) : Allow2PairingState()

    /**
     * Pairing completed successfully.
     */
    data class Completed(
        val credentials: Allow2Credentials,
        val children: List<Allow2Child>
    ) : Allow2PairingState()

    /**
     * Pairing failed with an error.
     */
    data class Failed(
        val error: String,
        val retryable: Boolean = true
    ) : Allow2PairingState()

    /**
     * Pairing session expired.
     */
    object Expired : Allow2PairingState()

    /**
     * Pairing was cancelled by the user.
     */
    object Cancelled : Allow2PairingState()
}

/**
 * Extension to check if pairing is in progress.
 */
fun Allow2PairingState.isInProgress(): Boolean {
    return when (this) {
        is Allow2PairingState.Initializing,
        is Allow2PairingState.QrCodeReady,
        is Allow2PairingState.Scanned -> true
        else -> false
    }
}

/**
 * Extension to check if pairing is complete.
 */
fun Allow2PairingState.isComplete(): Boolean {
    return this is Allow2PairingState.Completed
}

/**
 * Extension to check if pairing has failed.
 */
fun Allow2PairingState.isFailed(): Boolean {
    return this is Allow2PairingState.Failed || this is Allow2PairingState.Expired
}

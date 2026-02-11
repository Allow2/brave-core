/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.util

import org.chromium.chrome.browser.allow2.models.Allow2Child
import java.security.MessageDigest

/**
 * Utility class for verifying PINs using constant-time comparison.
 * This prevents timing attacks that could leak PIN information.
 */
object Allow2PinVerifier {

    /**
     * Verify a PIN against a child's stored hash and salt.
     *
     * @param pin The PIN entered by the user
     * @param child The child whose PIN is being verified
     * @return true if the PIN is correct, false otherwise
     */
    fun verify(pin: String, child: Allow2Child): Boolean {
        if (!child.hasPIN()) {
            // No PIN set means access granted
            return true
        }

        val computedHash = computeHash(pin, child.pinSalt)
        val storedHash = child.pinHash

        // Handle "sha256:" prefix if present
        val normalizedStored = if (storedHash.startsWith("sha256:")) {
            storedHash.substring(7)
        } else {
            storedHash
        }

        return constantTimeCompare(computedHash, normalizedStored)
    }

    /**
     * Compute SHA-256 hash of PIN + salt.
     *
     * @param pin The PIN to hash
     * @param salt The salt to combine with the PIN
     * @return Hexadecimal string of the hash
     */
    private fun computeHash(pin: String, salt: String): String {
        val combined = pin + salt
        val digest = MessageDigest.getInstance("SHA-256")
        val hashBytes = digest.digest(combined.toByteArray(Charsets.UTF_8))
        return hashBytes.joinToString("") { "%02x".format(it) }
    }

    /**
     * Constant-time string comparison to prevent timing attacks.
     * This ensures the comparison takes the same amount of time regardless
     * of where the strings differ.
     *
     * @param a First string to compare
     * @param b Second string to compare
     * @return true if strings are equal, false otherwise
     */
    private fun constantTimeCompare(a: String, b: String): Boolean {
        if (a.length != b.length) {
            // Still do comparison to maintain constant timing characteristics
            // even when lengths differ
            var result: Int = a.length xor b.length
            val minLen = minOf(a.length, b.length)
            for (i in 0 until minLen) {
                result = result or (a[i].code xor b[i].code)
            }
            return false
        }

        var result = 0
        for (i in a.indices) {
            result = result or (a[i].code xor b[i].code)
        }
        return result == 0
    }

    /**
     * Validate PIN format (4-6 digits).
     *
     * @param pin The PIN to validate
     * @return true if PIN format is valid
     */
    fun isValidPinFormat(pin: String): Boolean {
        return pin.length in 4..6 && pin.all { it.isDigit() }
    }

    /**
     * Mask a PIN for display (show asterisks).
     *
     * @param length Number of characters to show
     * @return String of asterisks
     */
    fun maskPin(length: Int): String {
        return "*".repeat(length)
    }
}

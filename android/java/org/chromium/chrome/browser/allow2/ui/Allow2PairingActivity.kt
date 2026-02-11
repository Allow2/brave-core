/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.ui

import android.graphics.Bitmap
import android.os.Bundle
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.google.zxing.BarcodeFormat
import com.google.zxing.MultiFormatWriter
import com.google.zxing.common.BitMatrix
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.chromium.chrome.browser.allow2.R
import org.chromium.chrome.browser.allow2.models.Allow2PairingState
import org.chromium.chrome.browser.allow2.models.isComplete
import org.chromium.chrome.browser.allow2.models.isFailed
import org.chromium.chrome.browser.allow2.services.Allow2Service

/**
 * Activity for pairing the device with Allow2.
 * Shows QR code for parent to scan or PIN code for manual entry.
 */
class Allow2PairingActivity : AppCompatActivity() {

    private val allow2Service by lazy { Allow2Service.getInstance(this) }

    // Views
    private lateinit var progressBar: ProgressBar
    private lateinit var qrCodeImage: ImageView
    private lateinit var pinCodeText: TextView
    private lateinit var statusText: TextView
    private lateinit var instructionText: TextView
    private lateinit var retryButton: Button
    private lateinit var cancelButton: Button
    private lateinit var qrContainer: LinearLayout
    private lateinit var pinContainer: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_allow2_pairing)

        initViews()
        setupListeners()
        observePairingState()

        // Start pairing
        allow2Service.startQrPairing()
    }

    private fun initViews() {
        progressBar = findViewById(R.id.progress_bar)
        qrCodeImage = findViewById(R.id.qr_code_image)
        pinCodeText = findViewById(R.id.pin_code_text)
        statusText = findViewById(R.id.status_text)
        instructionText = findViewById(R.id.instruction_text)
        retryButton = findViewById(R.id.retry_button)
        cancelButton = findViewById(R.id.cancel_button)
        qrContainer = findViewById(R.id.qr_container)
        pinContainer = findViewById(R.id.pin_container)
    }

    private fun setupListeners() {
        retryButton.setOnClickListener {
            allow2Service.resetPairingState()
            allow2Service.startQrPairing()
        }

        cancelButton.setOnClickListener {
            allow2Service.cancelPairing()
            finish()
        }
    }

    private fun observePairingState() {
        lifecycleScope.launch {
            allow2Service.pairingState.collectLatest { state ->
                updateUI(state)
            }
        }
    }

    private fun updateUI(state: Allow2PairingState) {
        when (state) {
            is Allow2PairingState.Idle -> {
                showLoading(false)
                qrContainer.visibility = View.GONE
                pinContainer.visibility = View.GONE
                retryButton.visibility = View.GONE
            }

            is Allow2PairingState.Initializing -> {
                showLoading(true)
                statusText.text = getString(R.string.allow2_pairing_initializing)
                qrContainer.visibility = View.GONE
                pinContainer.visibility = View.GONE
                retryButton.visibility = View.GONE
            }

            is Allow2PairingState.QrCodeReady -> {
                showLoading(false)
                statusText.text = getString(R.string.allow2_pairing_scan_qr)
                instructionText.text = getString(R.string.allow2_pairing_instruction)

                // Show QR code
                if (state.qrCodeUrl.isNotBlank()) {
                    qrContainer.visibility = View.VISIBLE
                    generateQrCode(state.qrCodeUrl)?.let { bitmap ->
                        qrCodeImage.setImageBitmap(bitmap)
                    }
                } else {
                    qrContainer.visibility = View.GONE
                }

                // Show PIN code
                if (state.pin.isNotBlank()) {
                    pinContainer.visibility = View.VISIBLE
                    pinCodeText.text = formatPinForDisplay(state.pin)
                } else {
                    pinContainer.visibility = View.GONE
                }

                retryButton.visibility = View.GONE
            }

            is Allow2PairingState.Scanned -> {
                showLoading(true)
                statusText.text = getString(R.string.allow2_pairing_scanned)
                instructionText.text = getString(R.string.allow2_pairing_waiting_confirm)
                retryButton.visibility = View.GONE
            }

            is Allow2PairingState.Completed -> {
                showLoading(false)
                statusText.text = getString(R.string.allow2_pairing_success)

                val childCount = state.children.size
                instructionText.text = resources.getQuantityString(
                    R.plurals.allow2_pairing_children_found,
                    childCount,
                    childCount
                )

                // Return to previous screen after short delay
                qrCodeImage.postDelayed({
                    setResult(RESULT_OK)
                    finish()
                }, 1500)
            }

            is Allow2PairingState.Failed -> {
                showLoading(false)
                statusText.text = getString(R.string.allow2_pairing_failed)
                instructionText.text = state.error
                qrContainer.visibility = View.GONE
                pinContainer.visibility = View.GONE
                retryButton.visibility = if (state.retryable) View.VISIBLE else View.GONE
            }

            is Allow2PairingState.Expired -> {
                showLoading(false)
                statusText.text = getString(R.string.allow2_pairing_expired)
                instructionText.text = getString(R.string.allow2_pairing_expired_message)
                qrContainer.visibility = View.GONE
                pinContainer.visibility = View.GONE
                retryButton.visibility = View.VISIBLE
            }

            is Allow2PairingState.Cancelled -> {
                finish()
            }
        }
    }

    private fun showLoading(loading: Boolean) {
        progressBar.visibility = if (loading) View.VISIBLE else View.GONE
    }

    private fun formatPinForDisplay(pin: String): String {
        // Format: "1 2 3 4 5 6" with spacing
        return pin.toCharArray().joinToString(" ")
    }

    private fun generateQrCode(content: String): Bitmap? {
        return try {
            val size = 512
            val bitMatrix: BitMatrix = MultiFormatWriter().encode(
                content,
                BarcodeFormat.QR_CODE,
                size,
                size
            )

            val width = bitMatrix.width
            val height = bitMatrix.height
            val pixels = IntArray(width * height)

            for (y in 0 until height) {
                val offset = y * width
                for (x in 0 until width) {
                    pixels[offset + x] = if (bitMatrix.get(x, y)) {
                        android.graphics.Color.BLACK
                    } else {
                        android.graphics.Color.WHITE
                    }
                }
            }

            Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888).apply {
                setPixels(pixels, 0, width, 0, 0, width, height)
            }
        } catch (e: Exception) {
            null
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (!allow2Service.pairingState.value.isComplete()) {
            allow2Service.cancelPairing()
        }
    }

    companion object {
        const val REQUEST_CODE = 1001
    }
}

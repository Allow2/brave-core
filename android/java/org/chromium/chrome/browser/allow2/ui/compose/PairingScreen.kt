/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.ui.compose

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.QrCode2
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.chromium.chrome.browser.allow2.R
import org.chromium.chrome.browser.allow2.models.Allow2PairingState

/**
 * Composable screen for device pairing.
 */
@Composable
fun PairingScreen(
    pairingState: Allow2PairingState,
    qrCodeBitmap: Bitmap?,
    onRetry: () -> Unit,
    onCancel: () -> Unit
) {
    Allow2Theme {
        Surface(
            modifier = Modifier.fillMaxSize(),
            color = MaterialTheme.colorScheme.background
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Spacer(modifier = Modifier.height(32.dp))

                // Title
                Text(
                    text = stringResource(R.string.allow2_pairing_title),
                    style = MaterialTheme.typography.headlineMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onBackground
                )

                Spacer(modifier = Modifier.height(32.dp))

                // Content based on state
                when (pairingState) {
                    is Allow2PairingState.Idle,
                    is Allow2PairingState.Initializing -> {
                        LoadingState()
                    }

                    is Allow2PairingState.QrCodeReady -> {
                        QrCodeReadyState(
                            qrCodeBitmap = qrCodeBitmap,
                            pin = pairingState.pin
                        )
                    }

                    is Allow2PairingState.Scanned -> {
                        ScannedState()
                    }

                    is Allow2PairingState.Completed -> {
                        CompletedState(
                            childrenCount = pairingState.children.size
                        )
                    }

                    is Allow2PairingState.Failed -> {
                        FailedState(
                            error = pairingState.error,
                            retryable = pairingState.retryable,
                            onRetry = onRetry
                        )
                    }

                    is Allow2PairingState.Expired -> {
                        ExpiredState(onRetry = onRetry)
                    }

                    is Allow2PairingState.Cancelled -> {
                        // Will be dismissed
                    }
                }

                Spacer(modifier = Modifier.weight(1f))

                // Cancel button (except for completed state)
                if (pairingState !is Allow2PairingState.Completed) {
                    OutlinedButton(
                        onClick = onCancel,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(android.R.string.cancel))
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))
            }
        }
    }
}

@Composable
private fun LoadingState() {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        CircularProgressIndicator(
            modifier = Modifier.size(64.dp),
            color = Allow2Colors.Primary
        )

        Spacer(modifier = Modifier.height(24.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_initializing),
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f)
        )
    }
}

@Composable
private fun QrCodeReadyState(
    qrCodeBitmap: Bitmap?,
    pin: String
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // QR Code
        Box(
            modifier = Modifier
                .size(250.dp)
                .clip(RoundedCornerShape(16.dp))
                .background(Color.White)
                .border(2.dp, Allow2Colors.Primary, RoundedCornerShape(16.dp))
                .padding(16.dp),
            contentAlignment = Alignment.Center
        ) {
            if (qrCodeBitmap != null) {
                Image(
                    bitmap = qrCodeBitmap.asImageBitmap(),
                    contentDescription = "QR Code",
                    modifier = Modifier.fillMaxSize()
                )
            } else {
                Icon(
                    imageVector = Icons.Default.QrCode2,
                    contentDescription = null,
                    modifier = Modifier.size(100.dp),
                    tint = Allow2Colors.Primary.copy(alpha = 0.3f)
                )
            }
        }

        Spacer(modifier = Modifier.height(24.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_scan_qr),
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Medium,
            color = MaterialTheme.colorScheme.onBackground
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_instruction),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f),
            textAlign = TextAlign.Center
        )

        // PIN code section
        if (pin.isNotBlank()) {
            Spacer(modifier = Modifier.height(32.dp))

            Divider(modifier = Modifier.padding(horizontal = 48.dp))

            Spacer(modifier = Modifier.height(24.dp))

            Text(
                text = stringResource(R.string.allow2_pairing_or_code),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.5f)
            )

            Spacer(modifier = Modifier.height(12.dp))

            // PIN display
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                pin.forEach { digit ->
                    Box(
                        modifier = Modifier
                            .size(48.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(MaterialTheme.colorScheme.surface)
                            .border(
                                1.dp,
                                MaterialTheme.colorScheme.outline.copy(alpha = 0.3f),
                                RoundedCornerShape(8.dp)
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = digit.toString(),
                            fontSize = 24.sp,
                            fontWeight = FontWeight.Bold,
                            color = Allow2Colors.Primary
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ScannedState() {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        CircularProgressIndicator(
            modifier = Modifier.size(64.dp),
            color = Allow2Colors.Primary
        )

        Spacer(modifier = Modifier.height(24.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_scanned),
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Medium,
            color = MaterialTheme.colorScheme.onBackground
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_waiting_confirm),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f)
        )
    }
}

@Composable
private fun CompletedState(childrenCount: Int) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Icon(
            imageVector = Icons.Default.CheckCircle,
            contentDescription = null,
            modifier = Modifier.size(80.dp),
            tint = Allow2Colors.Allowed
        )

        Spacer(modifier = Modifier.height(24.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_success),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold,
            color = Allow2Colors.Allowed
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "$childrenCount child${if (childrenCount != 1) "ren" else ""} found",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f)
        )
    }
}

@Composable
private fun FailedState(
    error: String,
    retryable: Boolean,
    onRetry: () -> Unit
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Icon(
            imageVector = Icons.Default.Error,
            contentDescription = null,
            modifier = Modifier.size(80.dp),
            tint = Allow2Colors.Blocked
        )

        Spacer(modifier = Modifier.height(24.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_failed),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold,
            color = Allow2Colors.Blocked
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = error,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f),
            textAlign = TextAlign.Center
        )

        if (retryable) {
            Spacer(modifier = Modifier.height(24.dp))

            Button(onClick = onRetry) {
                Text(stringResource(R.string.allow2_retry))
            }
        }
    }
}

@Composable
private fun ExpiredState(onRetry: () -> Unit) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Icon(
            imageVector = Icons.Default.Error,
            contentDescription = null,
            modifier = Modifier.size(80.dp),
            tint = Allow2Colors.Warning
        )

        Spacer(modifier = Modifier.height(24.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_expired),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold,
            color = Allow2Colors.Warning
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = stringResource(R.string.allow2_pairing_expired_message),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f),
            textAlign = TextAlign.Center
        )

        Spacer(modifier = Modifier.height(24.dp))

        Button(onClick = onRetry) {
            Text(stringResource(R.string.allow2_retry))
        }
    }
}

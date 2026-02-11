/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.ui.compose

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.chromium.chrome.browser.allow2.R
import org.chromium.chrome.browser.allow2.models.Allow2Child

/**
 * Composable screen for child selection (the "shield").
 */
@Composable
fun ChildSelectScreen(
    children: List<Allow2Child>,
    onChildSelected: (Allow2Child, String) -> Boolean,
    modifier: Modifier = Modifier
) {
    var selectedChild by remember { mutableStateOf<Allow2Child?>(null) }
    var pin by remember { mutableStateOf("") }
    var pinError by remember { mutableStateOf<String?>(null) }
    var showPinInput by remember { mutableStateOf(false) }

    Allow2Theme {
        Surface(
            modifier = modifier.fillMaxSize(),
            color = MaterialTheme.colorScheme.background
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Spacer(modifier = Modifier.height(48.dp))

                // Title
                Text(
                    text = stringResource(R.string.allow2_child_select_title),
                    style = MaterialTheme.typography.headlineMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onBackground
                )

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    text = stringResource(R.string.allow2_child_select_subtitle),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f)
                )

                Spacer(modifier = Modifier.height(32.dp))

                // Child grid
                LazyVerticalGrid(
                    columns = GridCells.Fixed(if (children.size <= 2) children.size else 3),
                    horizontalArrangement = Arrangement.spacedBy(16.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                    modifier = Modifier.weight(1f, fill = false)
                ) {
                    items(children) { child ->
                        ChildCard(
                            child = child,
                            isSelected = selectedChild == child,
                            onClick = {
                                selectedChild = child
                                pin = ""
                                pinError = null
                                showPinInput = child.hasPIN()

                                if (!child.hasPIN()) {
                                    onChildSelected(child, "")
                                }
                            }
                        )
                    }
                }

                // PIN input (shown when child has PIN)
                if (showPinInput && selectedChild != null) {
                    Spacer(modifier = Modifier.height(32.dp))

                    PinInputSection(
                        childName = selectedChild?.name ?: "",
                        pin = pin,
                        onPinChange = {
                            pin = it
                            pinError = null
                        },
                        error = pinError,
                        onConfirm = {
                            selectedChild?.let { child ->
                                val success = onChildSelected(child, pin)
                                if (!success) {
                                    pinError = "Incorrect PIN"
                                    pin = ""
                                }
                            }
                        }
                    )
                }

                Spacer(modifier = Modifier.height(24.dp))
            }
        }
    }
}

@Composable
private fun ChildCard(
    child: Allow2Child,
    isSelected: Boolean,
    onClick: () -> Unit
) {
    val borderColor = if (isSelected) {
        Allow2Colors.Primary
    } else {
        Color.Transparent
    }

    val backgroundColor = if (isSelected) {
        Allow2Colors.Primary.copy(alpha = 0.1f)
    } else {
        MaterialTheme.colorScheme.surface
    }

    Column(
        modifier = Modifier
            .width(100.dp)
            .clip(RoundedCornerShape(16.dp))
            .border(2.dp, borderColor, RoundedCornerShape(16.dp))
            .background(backgroundColor)
            .clickable(onClick = onClick)
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Avatar
        Box(
            modifier = Modifier
                .size(56.dp)
                .clip(CircleShape)
                .background(Allow2Colors.Primary.copy(alpha = 0.2f)),
            contentAlignment = Alignment.Center
        ) {
            if (child.id == -1L) {
                Icon(
                    imageVector = Icons.Default.Person,
                    contentDescription = null,
                    modifier = Modifier.size(32.dp),
                    tint = Allow2Colors.Primary
                )
            } else {
                Text(
                    text = child.name.firstOrNull()?.uppercase() ?: "?",
                    fontSize = 24.sp,
                    fontWeight = FontWeight.Bold,
                    color = Allow2Colors.Primary
                )
            }
        }

        Spacer(modifier = Modifier.height(12.dp))

        // Name
        Text(
            text = child.name,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Medium,
            textAlign = TextAlign.Center,
            maxLines = 1,
            color = MaterialTheme.colorScheme.onSurface
        )

        // Lock icon for PIN-protected
        if (child.hasPIN()) {
            Spacer(modifier = Modifier.height(4.dp))
            Icon(
                imageVector = Icons.Default.Lock,
                contentDescription = "PIN protected",
                modifier = Modifier.size(16.dp),
                tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f)
            )
        }
    }
}

@Composable
private fun PinInputSection(
    childName: String,
    pin: String,
    onPinChange: (String) -> Unit,
    error: String?,
    onConfirm: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 32.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Divider(modifier = Modifier.padding(vertical = 16.dp))

        Text(
            text = stringResource(R.string.allow2_enter_pin_for, childName),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f)
        )

        Spacer(modifier = Modifier.height(16.dp))

        OutlinedTextField(
            value = pin,
            onValueChange = { if (it.length <= 6) onPinChange(it) },
            label = { Text(stringResource(R.string.allow2_pin_label)) },
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
            isError = error != null,
            supportingText = error?.let { { Text(it, color = MaterialTheme.colorScheme.error) } },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(16.dp))

        Button(
            onClick = onConfirm,
            enabled = pin.length >= 4,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(stringResource(R.string.allow2_confirm))
        }
    }
}

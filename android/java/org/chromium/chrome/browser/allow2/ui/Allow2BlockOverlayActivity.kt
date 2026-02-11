/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.ui

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowManager
import android.widget.*
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.chromium.chrome.browser.allow2.Allow2Constants
import org.chromium.chrome.browser.allow2.R
import org.chromium.chrome.browser.allow2.services.Allow2Service
import org.chromium.chrome.browser.allow2.util.Allow2TimeFormatter

/**
 * Full-screen overlay shown when access is blocked.
 * Prevents interaction with the browser until time is available or request approved.
 */
class Allow2BlockOverlayActivity : AppCompatActivity() {

    private val allow2Service by lazy { Allow2Service.getInstance(this) }
    private val handler = Handler(Looper.getMainLooper())

    private lateinit var titleText: TextView
    private lateinit var reasonText: TextView
    private lateinit var dayTypeText: TextView
    private lateinit var countdownText: TextView
    private lateinit var requestButton: Button
    private lateinit var switchUserButton: Button
    private lateinit var whyBlockedButton: Button
    private lateinit var requestPendingContainer: LinearLayout
    private lateinit var iconView: ImageView

    private var blockReason: String = ""
    private var dayType: String = ""
    private var isRequestPending = false

    private val refreshRunnable = object : Runnable {
        override fun run() {
            checkIfStillBlocked()
            handler.postDelayed(this, 5000) // Check every 5 seconds
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Make this a secure overlay that cannot be bypassed
        window.addFlags(
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
            WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        )

        setContentView(R.layout.activity_allow2_block_overlay)

        // Read intent extras
        blockReason = intent.getStringExtra(Allow2Constants.EXTRA_BLOCK_REASON) ?: ""
        dayType = intent.getStringExtra(Allow2Constants.EXTRA_DAY_TYPE) ?: ""

        initViews()
        setupListeners()
        updateUI()
        observeBlockState()

        // Start periodic check
        handler.post(refreshRunnable)
    }

    private fun initViews() {
        titleText = findViewById(R.id.title_text)
        reasonText = findViewById(R.id.reason_text)
        dayTypeText = findViewById(R.id.day_type_text)
        countdownText = findViewById(R.id.countdown_text)
        requestButton = findViewById(R.id.request_button)
        switchUserButton = findViewById(R.id.switch_user_button)
        whyBlockedButton = findViewById(R.id.why_blocked_button)
        requestPendingContainer = findViewById(R.id.request_pending_container)
        iconView = findViewById(R.id.icon_view)
    }

    private fun setupListeners() {
        requestButton.setOnClickListener {
            showRequestDialog()
        }

        switchUserButton.setOnClickListener {
            showSwitchUserDialog()
        }

        whyBlockedButton.setOnClickListener {
            showWhyBlockedDialog()
        }
    }

    private fun updateUI() {
        titleText.text = getString(R.string.allow2_blocked_title)
        reasonText.text = blockReason.ifEmpty { getString(R.string.allow2_blocked_default_reason) }

        if (dayType.isNotBlank()) {
            dayTypeText.visibility = View.VISIBLE
            dayTypeText.text = getString(R.string.allow2_blocked_day_type, dayType)
        } else {
            dayTypeText.visibility = View.GONE
        }

        // Update countdown if there's a scheduled unblock time
        updateCountdown()

        // Show/hide request pending state
        requestPendingContainer.visibility = if (isRequestPending) View.VISIBLE else View.GONE
        requestButton.visibility = if (isRequestPending) View.GONE else View.VISIBLE

        // Show switch user only in shared device mode
        switchUserButton.visibility = if (allow2Service.isSharedDeviceMode()) {
            View.VISIBLE
        } else {
            View.GONE
        }
    }

    private fun updateCountdown() {
        val result = allow2Service.checkResult.value
        val activity = result?.activities?.get(1) // Internet activity
        val timeblock = activity?.timeblock

        if (timeblock != null && !timeblock.allowed && timeblock.ends > 0) {
            val secondsUntilEnd = (timeblock.ends - System.currentTimeMillis() / 1000).toInt()
            if (secondsUntilEnd > 0) {
                countdownText.visibility = View.VISIBLE
                countdownText.text = getString(
                    R.string.allow2_blocked_resumes_at,
                    Allow2TimeFormatter.formatTimestamp(timeblock.ends)
                )
            } else {
                countdownText.visibility = View.GONE
            }
        } else {
            countdownText.visibility = View.GONE
        }
    }

    private fun observeBlockState() {
        lifecycleScope.launch {
            allow2Service.isBlocked.collectLatest { blocked ->
                if (!blocked) {
                    // No longer blocked, dismiss overlay
                    finish()
                }
            }
        }
    }

    private fun checkIfStillBlocked() {
        allow2Service.onResume()
    }

    private fun showRequestDialog() {
        val input = EditText(this).apply {
            hint = getString(R.string.allow2_request_hint)
            maxLines = 3
            setPadding(48, 32, 48, 32)
        }

        AlertDialog.Builder(this)
            .setTitle(R.string.allow2_request_title)
            .setMessage(R.string.allow2_request_message)
            .setView(input)
            .setPositiveButton(R.string.allow2_request_send) { _, _ ->
                sendRequest(input.text.toString())
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun sendRequest(message: String) {
        lifecycleScope.launch {
            requestButton.isEnabled = false
            requestButton.text = getString(R.string.allow2_request_sending)

            val result = allow2Service.requestMoreTime(
                minutes = 30,
                message = message.ifEmpty { "Please allow more time" }
            )

            result.fold(
                onSuccess = { msg ->
                    isRequestPending = true
                    updateUI()
                    Toast.makeText(
                        this@Allow2BlockOverlayActivity,
                        msg,
                        Toast.LENGTH_SHORT
                    ).show()
                },
                onFailure = { error ->
                    requestButton.isEnabled = true
                    requestButton.text = getString(R.string.allow2_request_more_time)
                    Toast.makeText(
                        this@Allow2BlockOverlayActivity,
                        error.message,
                        Toast.LENGTH_LONG
                    ).show()
                }
            )
        }
    }

    private fun showSwitchUserDialog() {
        val childSelectDialog = Allow2ChildSelectDialog.newInstance()
        childSelectDialog.setListener(object : Allow2ChildSelectDialog.ChildSelectListener {
            override fun onChildSelected(child: org.chromium.chrome.browser.allow2.models.Allow2Child) {
                // Child switched, recheck status
                checkIfStillBlocked()
            }
        })
        childSelectDialog.show(supportFragmentManager, Allow2ChildSelectDialog.TAG)
    }

    private fun showWhyBlockedDialog() {
        val result = allow2Service.checkResult.value
        val activity = result?.activities?.get(1)
        val dayTypeInfo = result?.todayDayType

        val message = buildString {
            appendLine(getString(R.string.allow2_why_blocked_intro))
            appendLine()

            if (activity?.banned == true) {
                appendLine(getString(R.string.allow2_why_blocked_banned))
            } else if (activity?.timeblock != null && !activity.timeblock.allowed) {
                appendLine(getString(R.string.allow2_why_blocked_timeblock))
            } else if ((activity?.remaining ?: 0) <= 0) {
                appendLine(getString(R.string.allow2_why_blocked_time_used))
            }

            dayTypeInfo?.let {
                appendLine()
                appendLine(getString(R.string.allow2_why_blocked_day_type, it.name))
            }
        }

        AlertDialog.Builder(this)
            .setTitle(R.string.allow2_why_blocked_title)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }

    override fun onBackPressed() {
        // Prevent back button from dismissing the overlay
        // User must switch user or wait for unblock
        Toast.makeText(
            this,
            R.string.allow2_blocked_cannot_dismiss,
            Toast.LENGTH_SHORT
        ).show()
    }

    override fun onDestroy() {
        super.onDestroy()
        handler.removeCallbacks(refreshRunnable)
    }
}

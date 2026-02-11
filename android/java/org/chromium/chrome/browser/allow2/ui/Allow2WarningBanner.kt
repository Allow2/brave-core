/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.ui

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ValueAnimator
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import android.view.LayoutInflater
import android.view.View
import android.view.animation.AccelerateDecelerateInterpolator
import android.widget.FrameLayout
import android.widget.ProgressBar
import android.widget.TextView
import org.chromium.chrome.browser.allow2.R
import org.chromium.chrome.browser.allow2.services.Allow2UsageTracker
import org.chromium.chrome.browser.allow2.util.Allow2TimeFormatter

/**
 * Banner view that shows time remaining warnings.
 * Can be added to the browser UI to show countdown.
 */
class Allow2WarningBanner @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : FrameLayout(context, attrs, defStyleAttr) {

    private val handler = Handler(Looper.getMainLooper())

    private var messageText: TextView
    private var countdownText: TextView
    private var progressBar: ProgressBar
    private var dismissButton: View

    private var currentRemainingSeconds: Int = 0
    private var warningType: Allow2UsageTracker.WarningType? = null
    private var dismissRunnable: Runnable? = null
    private var countdownRunnable: Runnable? = null

    init {
        LayoutInflater.from(context).inflate(R.layout.view_allow2_warning_banner, this, true)

        messageText = findViewById(R.id.message_text)
        countdownText = findViewById(R.id.countdown_text)
        progressBar = findViewById(R.id.progress_bar)
        dismissButton = findViewById(R.id.dismiss_button)

        visibility = View.GONE

        dismissButton.setOnClickListener {
            dismiss()
        }
    }

    /**
     * Show a warning with the given type and remaining time.
     */
    fun showWarning(type: Allow2UsageTracker.WarningType, remainingSeconds: Int) {
        warningType = type
        currentRemainingSeconds = remainingSeconds

        // Cancel any pending dismiss
        dismissRunnable?.let { handler.removeCallbacks(it) }
        countdownRunnable?.let { handler.removeCallbacks(it) }

        // Configure appearance based on warning type
        when (type) {
            Allow2UsageTracker.WarningType.LOW -> {
                setBackgroundResource(R.drawable.bg_warning_low)
                messageText.text = context.getString(R.string.allow2_warning_15min)
                countdownText.visibility = View.GONE
                progressBar.visibility = View.GONE
                dismissButton.visibility = View.VISIBLE

                // Auto-dismiss after 5 seconds
                scheduleAutoDismiss(5000)
            }

            Allow2UsageTracker.WarningType.MEDIUM,
            Allow2UsageTracker.WarningType.HIGH -> {
                setBackgroundResource(R.drawable.bg_warning_medium)
                messageText.text = context.getString(R.string.allow2_warning_5min)
                countdownText.visibility = View.VISIBLE
                progressBar.visibility = View.VISIBLE
                dismissButton.visibility = View.VISIBLE

                // Start countdown
                startCountdown()

                // Auto-dismiss after 10 seconds
                scheduleAutoDismiss(10000)
            }

            Allow2UsageTracker.WarningType.CRITICAL -> {
                setBackgroundResource(R.drawable.bg_warning_critical)
                messageText.text = context.getString(R.string.allow2_warning_1min)
                countdownText.visibility = View.VISIBLE
                progressBar.visibility = View.VISIBLE
                dismissButton.visibility = View.GONE // Cannot dismiss critical warning

                // Start countdown - stays visible until block
                startCountdown()
            }
        }

        updateCountdownDisplay()

        // Animate in if not visible
        if (visibility != View.VISIBLE) {
            visibility = View.VISIBLE
            translationY = -height.toFloat()
            animate()
                .translationY(0f)
                .setDuration(300)
                .setInterpolator(AccelerateDecelerateInterpolator())
                .start()
        }
    }

    private fun startCountdown() {
        countdownRunnable?.let { handler.removeCallbacks(it) }

        countdownRunnable = object : Runnable {
            override fun run() {
                if (currentRemainingSeconds > 0) {
                    currentRemainingSeconds--
                    updateCountdownDisplay()
                    handler.postDelayed(this, 1000)
                }
            }
        }

        handler.postDelayed(countdownRunnable!!, 1000)
    }

    private fun updateCountdownDisplay() {
        countdownText.text = Allow2TimeFormatter.formatCountdown(currentRemainingSeconds)

        // Update progress bar (max 5 minutes = 300 seconds for HIGH/MEDIUM)
        val maxSeconds = when (warningType) {
            Allow2UsageTracker.WarningType.CRITICAL -> 60
            Allow2UsageTracker.WarningType.HIGH,
            Allow2UsageTracker.WarningType.MEDIUM -> 300
            else -> 900
        }
        progressBar.max = maxSeconds
        progressBar.progress = maxSeconds - currentRemainingSeconds.coerceAtMost(maxSeconds)
    }

    private fun scheduleAutoDismiss(delayMs: Long) {
        dismissRunnable = Runnable { dismiss() }
        handler.postDelayed(dismissRunnable!!, delayMs)
    }

    /**
     * Dismiss the banner with animation.
     */
    fun dismiss() {
        if (visibility != View.VISIBLE) return

        // Stop countdown
        countdownRunnable?.let { handler.removeCallbacks(it) }
        dismissRunnable?.let { handler.removeCallbacks(it) }

        // Animate out
        animate()
            .translationY(-height.toFloat())
            .setDuration(200)
            .setInterpolator(AccelerateDecelerateInterpolator())
            .setListener(object : AnimatorListenerAdapter() {
                override fun onAnimationEnd(animation: Animator) {
                    visibility = View.GONE
                    warningType = null
                }
            })
            .start()
    }

    /**
     * Check if the banner is currently showing.
     */
    fun isShowing(): Boolean = visibility == View.VISIBLE

    /**
     * Get the current warning type.
     */
    fun getCurrentWarningType(): Allow2UsageTracker.WarningType? = warningType

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        countdownRunnable?.let { handler.removeCallbacks(it) }
        dismissRunnable?.let { handler.removeCallbacks(it) }
    }
}

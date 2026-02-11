/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.ui

import android.content.Intent
import android.os.Bundle
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import org.chromium.chrome.browser.allow2.R
import org.chromium.chrome.browser.allow2.services.Allow2CredentialManager
import org.chromium.chrome.browser.allow2.services.Allow2Service

/**
 * Settings fragment for Parental Freedom (Allow2) configuration.
 * Appears as a top-level settings section.
 */
class Allow2SettingsFragment : PreferenceFragmentCompat() {

    private val allow2Service by lazy { Allow2Service.getInstance(requireContext()) }
    private val credentialManager by lazy { Allow2CredentialManager.getInstance(requireContext()) }

    private var enabledPref: SwitchPreferenceCompat? = null
    private var pairingPref: Preference? = null
    private var childPref: Preference? = null
    private var sharedDevicePref: SwitchPreferenceCompat? = null
    private var statusPref: Preference? = null
    private var switchUserPref: Preference? = null

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.allow2_preferences, rootKey)

        // Find preferences
        enabledPref = findPreference(PREF_ENABLED)
        pairingPref = findPreference(PREF_PAIRING)
        childPref = findPreference(PREF_CHILD)
        sharedDevicePref = findPreference(PREF_SHARED_DEVICE)
        statusPref = findPreference(PREF_STATUS)
        switchUserPref = findPreference(PREF_SWITCH_USER)

        setupPreferences()
        updateUI()
    }

    override fun onResume() {
        super.onResume()
        updateUI()
    }

    private fun setupPreferences() {
        // Enable/disable toggle
        enabledPref?.setOnPreferenceChangeListener { _, newValue ->
            val enabled = newValue as Boolean
            credentialManager.setEnabled(enabled)
            if (enabled) {
                allow2Service.startTracking()
            } else {
                allow2Service.stopTracking()
            }
            updateUI()
            true
        }

        // Pairing action
        pairingPref?.setOnPreferenceClickListener {
            startPairing()
            true
        }

        // Shared device mode
        sharedDevicePref?.setOnPreferenceChangeListener { _, newValue ->
            credentialManager.setSharedDeviceMode(newValue as Boolean)
            true
        }

        // Switch user
        switchUserPref?.setOnPreferenceClickListener {
            showChildSelectDialog()
            true
        }
    }

    private fun updateUI() {
        val isPaired = allow2Service.isPaired()
        val isEnabled = allow2Service.isEnabled()
        val currentChild = allow2Service.getCurrentChild()
        val children = allow2Service.getChildren()

        // Enable toggle
        enabledPref?.isChecked = isEnabled
        enabledPref?.isEnabled = isPaired

        // Pairing preference
        if (isPaired) {
            pairingPref?.title = getString(R.string.allow2_settings_paired_title)
            pairingPref?.summary = getString(
                R.string.allow2_settings_paired_summary,
                children.size
            )
        } else {
            pairingPref?.title = getString(R.string.allow2_settings_pair_title)
            pairingPref?.summary = getString(R.string.allow2_settings_pair_summary)
        }

        // Current child
        childPref?.isVisible = isPaired && children.isNotEmpty()
        if (currentChild != null) {
            childPref?.title = getString(R.string.allow2_settings_current_child)
            childPref?.summary = currentChild.name
        } else {
            childPref?.summary = getString(R.string.allow2_settings_no_child_selected)
        }

        // Shared device mode
        sharedDevicePref?.isVisible = isPaired
        sharedDevicePref?.isChecked = credentialManager.isSharedDeviceMode()

        // Status
        statusPref?.isVisible = isPaired && isEnabled
        updateStatusPreference()

        // Switch user
        switchUserPref?.isVisible = isPaired && children.size > 1
    }

    private fun updateStatusPreference() {
        val result = allow2Service.checkResult.value
        if (result != null) {
            if (result.allowed) {
                val remaining = result.getRemainingSeconds()
                if (remaining > 0) {
                    statusPref?.summary = getString(
                        R.string.allow2_settings_status_remaining,
                        formatTime(remaining)
                    )
                } else {
                    statusPref?.summary = getString(R.string.allow2_settings_status_allowed)
                }
            } else {
                statusPref?.summary = getString(
                    R.string.allow2_settings_status_blocked,
                    result.getBlockReason()
                )
            }
        } else {
            statusPref?.summary = getString(R.string.allow2_settings_status_checking)
        }
    }

    private fun formatTime(seconds: Int): String {
        val hours = seconds / 3600
        val minutes = (seconds % 3600) / 60

        return when {
            hours > 0 -> "$hours h $minutes min"
            minutes > 0 -> "$minutes min"
            else -> "< 1 min"
        }
    }

    private fun startPairing() {
        val intent = Intent(requireContext(), Allow2PairingActivity::class.java)
        startActivityForResult(intent, Allow2PairingActivity.REQUEST_CODE)
    }

    private fun showChildSelectDialog() {
        val dialog = Allow2ChildSelectDialog.newInstance()
        dialog.setListener(object : Allow2ChildSelectDialog.ChildSelectListener {
            override fun onChildSelected(child: org.chromium.chrome.browser.allow2.models.Allow2Child) {
                updateUI()
            }
        })
        dialog.show(parentFragmentManager, Allow2ChildSelectDialog.TAG)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == Allow2PairingActivity.REQUEST_CODE) {
            updateUI()
        }
    }

    companion object {
        private const val PREF_ENABLED = "allow2_enabled"
        private const val PREF_PAIRING = "allow2_pairing"
        private const val PREF_CHILD = "allow2_child"
        private const val PREF_SHARED_DEVICE = "allow2_shared_device"
        private const val PREF_STATUS = "allow2_status"
        private const val PREF_SWITCH_USER = "allow2_switch_user"

        fun newInstance(): Allow2SettingsFragment {
            return Allow2SettingsFragment()
        }
    }
}

/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.allow2.ui

import android.app.Dialog
import android.content.Context
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.*
import androidx.fragment.app.DialogFragment
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import org.chromium.chrome.browser.allow2.R
import org.chromium.chrome.browser.allow2.models.Allow2Child
import org.chromium.chrome.browser.allow2.services.Allow2Service
import org.chromium.chrome.browser.allow2.util.Allow2PinVerifier

/**
 * Dialog for selecting which child is using the device.
 * Shows on shared devices and requires PIN verification.
 */
class Allow2ChildSelectDialog : DialogFragment() {

    private val allow2Service by lazy { Allow2Service.getInstance(requireContext()) }

    private lateinit var childRecyclerView: RecyclerView
    private lateinit var pinContainer: LinearLayout
    private lateinit var pinEditText: EditText
    private lateinit var pinErrorText: TextView
    private lateinit var confirmButton: Button
    private lateinit var titleText: TextView

    private var selectedChild: Allow2Child? = null
    private var listener: ChildSelectListener? = null

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        return super.onCreateDialog(savedInstanceState).apply {
            setCancelable(false)
            setCanceledOnTouchOutside(false)
            window?.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View? {
        return inflater.inflate(R.layout.dialog_allow2_child_select, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        initViews(view)
        setupChildList()
        setupPinInput()
    }

    private fun initViews(view: View) {
        titleText = view.findViewById(R.id.title_text)
        childRecyclerView = view.findViewById(R.id.child_recycler_view)
        pinContainer = view.findViewById(R.id.pin_container)
        pinEditText = view.findViewById(R.id.pin_edit_text)
        pinErrorText = view.findViewById(R.id.pin_error_text)
        confirmButton = view.findViewById(R.id.confirm_button)

        titleText.text = getString(R.string.allow2_child_select_title)
        pinContainer.visibility = View.GONE
    }

    private fun setupChildList() {
        val children = allow2Service.getChildren()
        val adapter = ChildAdapter(children) { child ->
            onChildSelected(child)
        }

        childRecyclerView.layoutManager = LinearLayoutManager(requireContext())
        childRecyclerView.adapter = adapter
    }

    private fun setupPinInput() {
        pinEditText.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                pinErrorText.visibility = View.GONE
                confirmButton.isEnabled = (s?.length ?: 0) >= 4
            }
        })

        confirmButton.setOnClickListener {
            verifyPinAndConfirm()
        }
    }

    private fun onChildSelected(child: Allow2Child) {
        selectedChild = child

        if (child.hasPIN()) {
            // Show PIN input
            pinContainer.visibility = View.VISIBLE
            pinEditText.text.clear()
            pinEditText.requestFocus()
            confirmButton.isEnabled = false
        } else {
            // No PIN required, select immediately
            confirmSelection(child, "")
        }
    }

    private fun verifyPinAndConfirm() {
        val child = selectedChild ?: return
        val pin = pinEditText.text.toString()

        if (!Allow2PinVerifier.isValidPinFormat(pin)) {
            pinErrorText.text = getString(R.string.allow2_pin_invalid_format)
            pinErrorText.visibility = View.VISIBLE
            return
        }

        if (Allow2PinVerifier.verify(pin, child)) {
            confirmSelection(child, pin)
        } else {
            pinErrorText.text = getString(R.string.allow2_pin_incorrect)
            pinErrorText.visibility = View.VISIBLE
            pinEditText.text.clear()
        }
    }

    private fun confirmSelection(child: Allow2Child, pin: String) {
        if (allow2Service.selectChild(child, pin)) {
            listener?.onChildSelected(child)
            dismiss()
        } else {
            pinErrorText.text = getString(R.string.allow2_pin_verification_failed)
            pinErrorText.visibility = View.VISIBLE
        }
    }

    fun setListener(listener: ChildSelectListener) {
        this.listener = listener
    }

    interface ChildSelectListener {
        fun onChildSelected(child: Allow2Child)
    }

    // ==================== Child Adapter ====================

    private inner class ChildAdapter(
        private val children: List<Allow2Child>,
        private val onChildClick: (Allow2Child) -> Unit
    ) : RecyclerView.Adapter<ChildAdapter.ChildViewHolder>() {

        private var selectedPosition = -1

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ChildViewHolder {
            val view = LayoutInflater.from(parent.context)
                .inflate(R.layout.item_allow2_child, parent, false)
            return ChildViewHolder(view)
        }

        override fun onBindViewHolder(holder: ChildViewHolder, position: Int) {
            val child = children[position]
            holder.bind(child, position == selectedPosition)
        }

        override fun getItemCount(): Int = children.size

        inner class ChildViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
            private val avatarView: TextView = itemView.findViewById(R.id.child_avatar)
            private val nameView: TextView = itemView.findViewById(R.id.child_name)
            private val lockIcon: ImageView = itemView.findViewById(R.id.lock_icon)
            private val container: View = itemView.findViewById(R.id.child_container)

            fun bind(child: Allow2Child, isSelected: Boolean) {
                // Set avatar (first letter or emoji)
                avatarView.text = getAvatarText(child)
                nameView.text = child.name
                lockIcon.visibility = if (child.hasPIN()) View.VISIBLE else View.GONE

                // Highlight selected
                container.isSelected = isSelected

                itemView.setOnClickListener {
                    val previousSelected = selectedPosition
                    selectedPosition = adapterPosition
                    notifyItemChanged(previousSelected)
                    notifyItemChanged(selectedPosition)
                    onChildClick(child)
                }
            }

            private fun getAvatarText(child: Allow2Child): String {
                // Return first character or default emoji
                return when {
                    child.id == -1L -> "\uD83D\uDC64" // Guest icon
                    child.name.isNotEmpty() -> child.name.first().uppercaseChar().toString()
                    else -> "?"
                }
            }
        }
    }

    companion object {
        const val TAG = "Allow2ChildSelectDialog"

        fun newInstance(): Allow2ChildSelectDialog {
            return Allow2ChildSelectDialog()
        }
    }
}

// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import SwiftUI

// MARK: - Child Select View

/// Shield overlay for selecting which child is using the device (shared device mode)
public struct ChildSelectView: View {

  let children: [Allow2Child]
  let onChildSelected: (Allow2Child, String) -> Bool
  let onDismiss: () -> Void

  @State private var selectedChild: Allow2Child?
  @State private var pin: String = ""
  @State private var showPinError = false
  @State private var isValidating = false

  public init(
    children: [Allow2Child],
    onChildSelected: @escaping (Allow2Child, String) -> Bool,
    onDismiss: @escaping () -> Void
  ) {
    self.children = children
    self.onChildSelected = onChildSelected
    self.onDismiss = onDismiss
  }

  public var body: some View {
    VStack(spacing: 0) {
      // Header
      headerView

      Divider()

      // Content
      if let child = selectedChild {
        pinEntryView(for: child)
      } else {
        childSelectionView
      }
    }
    .background(Color(.systemGroupedBackground))
    .navigationTitle("Who's using Brave?")
    .navigationBarTitleDisplayMode(.inline)
    .interactiveDismissDisabled()
  }

  // MARK: - Header View

  private var headerView: some View {
    VStack(spacing: 16) {
      Image(systemName: "shield.lefthalf.filled.badge.checkmark")
        .font(.system(size: 50))
        .foregroundColor(.blue)
        .padding(.top, 24)

      Text("Who's using Brave?")
        .font(.title2)
        .fontWeight(.semibold)

      Text("Select your profile to continue browsing")
        .font(.subheadline)
        .foregroundColor(.secondary)
        .padding(.bottom, 16)
    }
    .frame(maxWidth: .infinity)
    .background(Color(.systemBackground))
  }

  // MARK: - Child Selection View

  private var childSelectionView: some View {
    ScrollView {
      LazyVGrid(
        columns: [
          GridItem(.flexible(), spacing: 16),
          GridItem(.flexible(), spacing: 16),
          GridItem(.flexible(), spacing: 16),
        ],
        spacing: 16
      ) {
        ForEach(children) { child in
          ChildButton(child: child) {
            withAnimation(.easeInOut(duration: 0.2)) {
              selectedChild = child
            }
          }
        }

        // Guest option
        GuestButton {
          // Guest mode - no tracking
          onDismiss()
        }
      }
      .padding(24)
    }
  }

  // MARK: - PIN Entry View

  private func pinEntryView(for child: Allow2Child) -> some View {
    VStack(spacing: 24) {
      Spacer()

      // Child avatar
      VStack(spacing: 12) {
        Image(systemName: "person.circle.fill")
          .font(.system(size: 80))
          .foregroundColor(.blue)

        Text(child.name)
          .font(.title2)
          .fontWeight(.semibold)
      }

      // PIN entry
      VStack(spacing: 16) {
        Text("Enter PIN")
          .font(.headline)
          .foregroundColor(.secondary)

        PINEntryField(pin: $pin, isError: showPinError)
          .frame(height: 60)
          .onChange(of: pin) { newValue in
            showPinError = false
            if newValue.count == 4 {
              validatePin(for: child)
            }
          }

        if showPinError {
          Text("Incorrect PIN. Please try again.")
            .font(.caption)
            .foregroundColor(.red)
        }
      }
      .padding(.horizontal, 32)

      Spacer()

      // Back button
      Button {
        withAnimation(.easeInOut(duration: 0.2)) {
          selectedChild = nil
          pin = ""
          showPinError = false
        }
      } label: {
        HStack {
          Image(systemName: "chevron.left")
          Text("Back to selection")
        }
        .foregroundColor(.secondary)
      }
      .padding(.bottom, 24)
    }
  }

  // MARK: - PIN Validation

  private func validatePin(for child: Allow2Child) {
    isValidating = true

    // Small delay for visual feedback
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
      let success = onChildSelected(child, pin)

      if success {
        onDismiss()
      } else {
        showPinError = true
        pin = ""
      }

      isValidating = false
    }
  }
}

// MARK: - Child Button

private struct ChildButton: View {
  let child: Allow2Child
  let action: () -> Void

  var body: some View {
    Button(action: action) {
      VStack(spacing: 8) {
        Image(systemName: "person.circle.fill")
          .font(.system(size: 50))
          .foregroundColor(.blue)

        Text(child.name)
          .font(.subheadline)
          .fontWeight(.medium)
          .foregroundColor(.primary)
          .lineLimit(1)
      }
      .frame(maxWidth: .infinity)
      .padding(.vertical, 16)
      .background(Color(.secondarySystemGroupedBackground))
      .cornerRadius(12)
    }
    .buttonStyle(.plain)
  }
}

// MARK: - Guest Button

private struct GuestButton: View {
  let action: () -> Void

  var body: some View {
    Button(action: action) {
      VStack(spacing: 8) {
        Image(systemName: "person.circle")
          .font(.system(size: 50))
          .foregroundColor(.secondary)

        Text("Guest")
          .font(.subheadline)
          .fontWeight(.medium)
          .foregroundColor(.secondary)
      }
      .frame(maxWidth: .infinity)
      .padding(.vertical, 16)
      .background(Color(.secondarySystemGroupedBackground))
      .cornerRadius(12)
    }
    .buttonStyle(.plain)
  }
}

// MARK: - PIN Entry Field

private struct PINEntryField: View {
  @Binding var pin: String
  var isError: Bool

  @FocusState private var isFocused: Bool

  var body: some View {
    ZStack {
      // Hidden text field for input
      TextField("", text: $pin)
        .keyboardType(.numberPad)
        .textContentType(.oneTimeCode)
        .focused($isFocused)
        .opacity(0)
        .onChange(of: pin) { newValue in
          // Limit to 4 digits
          if newValue.count > 4 {
            pin = String(newValue.prefix(4))
          }
          // Only allow digits
          pin = pin.filter { $0.isNumber }
        }

      // Visual PIN dots
      HStack(spacing: 20) {
        ForEach(0..<4, id: \.self) { index in
          PINDot(
            isFilled: index < pin.count,
            isError: isError
          )
        }
      }
    }
    .onAppear {
      isFocused = true
    }
    .onTapGesture {
      isFocused = true
    }
  }
}

// MARK: - PIN Dot

private struct PINDot: View {
  let isFilled: Bool
  let isError: Bool

  var body: some View {
    Circle()
      .fill(dotColor)
      .frame(width: 16, height: 16)
      .overlay(
        Circle()
          .stroke(borderColor, lineWidth: 2)
      )
      .animation(.easeInOut(duration: 0.1), value: isFilled)
  }

  private var dotColor: Color {
    if isError {
      return .red
    }
    return isFilled ? .blue : .clear
  }

  private var borderColor: Color {
    if isError {
      return .red
    }
    return .secondary.opacity(0.5)
  }
}

// MARK: - Full Screen Child Shield

/// Full-screen modal version of the child selection shield
public struct ChildShieldOverlay: View {

  let children: [Allow2Child]
  let onChildSelected: (Allow2Child, String) -> Bool
  let onGuest: () -> Void

  @State private var isPresented = true

  public init(
    children: [Allow2Child],
    onChildSelected: @escaping (Allow2Child, String) -> Bool,
    onGuest: @escaping () -> Void
  ) {
    self.children = children
    self.onChildSelected = onChildSelected
    self.onGuest = onGuest
  }

  public var body: some View {
    Color.clear
      .fullScreenCover(isPresented: $isPresented) {
        NavigationStack {
          ChildSelectView(
            children: children,
            onChildSelected: { child, pin in
              let success = onChildSelected(child, pin)
              if success {
                isPresented = false
              }
              return success
            },
            onDismiss: {
              onGuest()
              isPresented = false
            }
          )
        }
        .interactiveDismissDisabled()
      }
  }
}

// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import SwiftUI

// MARK: - Block View

/// Full-screen overlay shown when access is blocked
public struct BlockView: View {

  let checkResult: Allow2CheckResult
  let dayType: String?
  let onRequestMoreTime: () -> Void
  let onSwitchUser: () -> Void
  let onWhyBlocked: () -> Void

  public init(
    checkResult: Allow2CheckResult,
    dayType: String? = nil,
    onRequestMoreTime: @escaping () -> Void,
    onSwitchUser: @escaping () -> Void,
    onWhyBlocked: @escaping () -> Void
  ) {
    self.checkResult = checkResult
    self.dayType = dayType
    self.onRequestMoreTime = onRequestMoreTime
    self.onSwitchUser = onSwitchUser
    self.onWhyBlocked = onWhyBlocked
  }

  public var body: some View {
    VStack(spacing: 0) {
      Spacer()

      // Block icon and message
      blockContent

      Spacer()

      // Action buttons
      actionButtons
        .padding(.bottom, 32)
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .background(backgroundGradient)
  }

  // MARK: - Block Content

  private var blockContent: some View {
    VStack(spacing: 24) {
      // Icon
      ZStack {
        Circle()
          .fill(Color.white.opacity(0.15))
          .frame(width: 120, height: 120)

        Image(systemName: "hand.raised.fill")
          .font(.system(size: 50))
          .foregroundColor(.white)
      }

      // Title
      Text(blockTitle)
        .font(.system(size: 32, weight: .bold))
        .foregroundColor(.white)

      // Message card
      VStack(spacing: 16) {
        Image(systemName: "xmark.circle.fill")
          .font(.system(size: 40))
          .foregroundColor(.red)

        Text("BLOCKED")
          .font(.headline)
          .foregroundColor(.red)

        Text(checkResult.explanation)
          .font(.subheadline)
          .foregroundColor(.secondary)
          .multilineTextAlignment(.center)
          .padding(.horizontal)

        if let dayType = dayType {
          Text("(\(dayType))")
            .font(.caption)
            .foregroundColor(.secondary)
        }
      }
      .padding(24)
      .background(Color(.systemBackground))
      .cornerRadius(16)
      .shadow(color: .black.opacity(0.2), radius: 10, y: 5)
      .padding(.horizontal, 32)
    }
  }

  private var blockTitle: String {
    // Determine the block type based on the check result
    if let remaining = checkResult.minimumRemainingSeconds, remaining <= 0 {
      return "Time's Up!"
    }

    // Check for time block
    for (_, activity) in checkResult.activities {
      if let timeblock = activity.timeblock, !timeblock.allowed {
        return "Not Now"
      }
      if activity.banned {
        return "Blocked"
      }
    }

    return "Access Blocked"
  }

  // MARK: - Action Buttons

  private var actionButtons: some View {
    VStack(spacing: 12) {
      // Request More Time button
      Button(action: onRequestMoreTime) {
        HStack {
          Image(systemName: "clock.badge.questionmark")
          Text("Request More Time")
        }
        .font(.headline)
        .foregroundColor(.white)
        .frame(maxWidth: .infinity)
        .padding(.vertical, 16)
        .background(Color.blue)
        .cornerRadius(12)
      }
      .padding(.horizontal, 32)

      // Switch User button
      Button(action: onSwitchUser) {
        HStack {
          Image(systemName: "person.2")
          Text("Switch User")
        }
        .font(.headline)
        .foregroundColor(.blue)
        .frame(maxWidth: .infinity)
        .padding(.vertical, 16)
        .background(Color.white)
        .cornerRadius(12)
      }
      .padding(.horizontal, 32)

      // Why am I blocked link
      Button(action: onWhyBlocked) {
        Text("Why am I blocked?")
          .font(.subheadline)
          .foregroundColor(.white.opacity(0.8))
          .underline()
      }
      .padding(.top, 8)
    }
  }

  // MARK: - Background

  private var backgroundGradient: some View {
    LinearGradient(
      colors: [
        Color(red: 0.2, green: 0.2, blue: 0.3),
        Color(red: 0.1, green: 0.1, blue: 0.2),
      ],
      startPoint: .top,
      endPoint: .bottom
    )
    .ignoresSafeArea()
  }
}

// MARK: - Request More Time View

public struct RequestMoreTimeView: View {

  let activity: Allow2Activity
  let onRequest: (MoreTimeDuration, String?) async throws -> Void
  let onCancel: () -> Void

  @State private var selectedDuration: MoreTimeDuration = .thirtyMinutes
  @State private var message: String = ""
  @State private var isRequesting = false
  @State private var errorMessage: String?
  @State private var requestSent = false

  @Environment(\.dismiss) private var dismiss

  public init(
    activity: Allow2Activity,
    onRequest: @escaping (MoreTimeDuration, String?) async throws -> Void,
    onCancel: @escaping () -> Void
  ) {
    self.activity = activity
    self.onRequest = onRequest
    self.onCancel = onCancel
  }

  public var body: some View {
    NavigationStack {
      if requestSent {
        requestSentView
      } else {
        requestFormView
      }
    }
  }

  // MARK: - Request Form View

  private var requestFormView: some View {
    Form {
      Section {
        HStack {
          Text("Activity")
          Spacer()
          Text(activityName)
            .foregroundColor(.secondary)
        }
      }

      Section {
        ForEach(MoreTimeDuration.allCases, id: \.rawValue) { duration in
          Button {
            selectedDuration = duration
          } label: {
            HStack {
              Text(duration.displayString)
                .foregroundColor(.primary)
              Spacer()
              if selectedDuration == duration {
                Image(systemName: "checkmark")
                  .foregroundColor(.blue)
              }
            }
          }
        }
      } header: {
        Text("Request")
      }

      Section {
        TextField("Message to parent (optional)", text: $message, axis: .vertical)
          .lineLimit(3...6)
      } header: {
        Text("Message")
      } footer: {
        Text("Let your parent know why you need more time.")
      }

      if let error = errorMessage {
        Section {
          Text(error)
            .foregroundColor(.red)
            .font(.subheadline)
        }
      }
    }
    .navigationTitle("Request Additional Time")
    .navigationBarTitleDisplayMode(.inline)
    .toolbar {
      ToolbarItem(placement: .cancellationAction) {
        Button("Cancel") {
          onCancel()
          dismiss()
        }
      }

      ToolbarItem(placement: .confirmationAction) {
        Button("Send") {
          Task {
            await sendRequest()
          }
        }
        .disabled(isRequesting)
      }
    }
    .disabled(isRequesting)
    .overlay {
      if isRequesting {
        Color.black.opacity(0.3)
          .ignoresSafeArea()
          .overlay {
            ProgressView()
              .scaleEffect(1.5)
              .tint(.white)
          }
      }
    }
  }

  // MARK: - Request Sent View

  private var requestSentView: some View {
    VStack(spacing: 24) {
      Spacer()

      Image(systemName: "paperplane.circle.fill")
        .font(.system(size: 80))
        .foregroundColor(.green)

      Text("Request Sent!")
        .font(.title)
        .fontWeight(.bold)

      Text("Your parent will receive a notification and can approve or deny your request.")
        .font(.subheadline)
        .foregroundColor(.secondary)
        .multilineTextAlignment(.center)
        .padding(.horizontal, 32)

      Spacer()

      Button("Done") {
        onCancel()
        dismiss()
      }
      .font(.headline)
      .foregroundColor(.white)
      .frame(maxWidth: .infinity)
      .padding(.vertical, 16)
      .background(Color.blue)
      .cornerRadius(12)
      .padding(.horizontal, 32)
      .padding(.bottom, 32)
    }
    .navigationBarHidden(true)
  }

  // MARK: - Helpers

  private var activityName: String {
    switch activity {
    case .internet: return "Internet"
    case .gaming: return "Gaming"
    case .screenTime: return "Screen Time"
    case .social: return "Social Media"
    }
  }

  private func sendRequest() async {
    isRequesting = true
    errorMessage = nil

    do {
      try await onRequest(selectedDuration, message.isEmpty ? nil : message)
      await MainActor.run {
        requestSent = true
        isRequesting = false
      }
    } catch {
      await MainActor.run {
        errorMessage = error.localizedDescription
        isRequesting = false
      }
    }
  }
}

// MARK: - Why Blocked View

public struct WhyBlockedView: View {

  let checkResult: Allow2CheckResult

  @Environment(\.dismiss) private var dismiss

  public var body: some View {
    NavigationStack {
      List {
        Section {
          ForEach(Array(checkResult.activities.keys.sorted()), id: \.self) { key in
            if let activity = checkResult.activities[key] {
              activityRow(activity: activity)
            }
          }
        } header: {
          Text("Activity Status")
        }

        Section {
          Text(checkResult.explanation)
            .font(.subheadline)
            .foregroundColor(.secondary)
        } header: {
          Text("Explanation")
        }

        if let dayTypes = checkResult.dayTypes {
          Section {
            if let today = dayTypes.today {
              HStack {
                Text("Today")
                Spacer()
                Text(today.name)
                  .foregroundColor(.secondary)
              }
            }
            if let tomorrow = dayTypes.tomorrow {
              HStack {
                Text("Tomorrow")
                Spacer()
                Text(tomorrow.name)
                  .foregroundColor(.secondary)
              }
            }
          } header: {
            Text("Schedule Type")
          }
        }

        Section {
          Link(destination: URL(string: "https://allow2.com/help/blocked")!) {
            HStack {
              Text("Learn more about blocking")
              Spacer()
              Image(systemName: "arrow.up.right")
                .font(.caption)
            }
          }
        }
      }
      .navigationTitle("Why Am I Blocked?")
      .navigationBarTitleDisplayMode(.inline)
      .toolbar {
        ToolbarItem(placement: .confirmationAction) {
          Button("Done") {
            dismiss()
          }
        }
      }
    }
  }

  @ViewBuilder
  private func activityRow(activity: ActivityResult) -> some View {
    HStack {
      VStack(alignment: .leading, spacing: 4) {
        Text(activity.name)
          .font(.headline)

        if activity.banned {
          Text("Banned")
            .font(.caption)
            .foregroundColor(.red)
        } else if let timeblock = activity.timeblock, !timeblock.allowed {
          if let ends = timeblock.formattedEndsTime {
            Text("Blocked until \(ends)")
              .font(.caption)
              .foregroundColor(.orange)
          } else {
            Text("Time block active")
              .font(.caption)
              .foregroundColor(.orange)
          }
        } else if let remaining = activity.remaining, remaining <= 0 {
          Text("Daily limit reached")
            .font(.caption)
            .foregroundColor(.red)
        } else if let remaining = activity.formattedRemainingTime {
          Text("\(remaining) remaining")
            .font(.caption)
            .foregroundColor(.green)
        }
      }

      Spacer()

      Image(
        systemName: activity.isAllowed ? "checkmark.circle.fill" : "xmark.circle.fill"
      )
      .foregroundColor(activity.isAllowed ? .green : .red)
    }
  }
}

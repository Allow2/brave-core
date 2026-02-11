// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import BraveUI
import Preferences
import SwiftUI

// MARK: - Parental Freedom Settings View

/// Main settings view for Allow2 Parental Freedom integration
public struct ParentalFreedomSettingsView: View {

  @StateObject private var viewModel = ParentalFreedomSettingsViewModel()

  @State private var showPairingSheet = false
  @State private var showChildSelectSheet = false
  @State private var showUnpairAlert = false

  @Environment(\.dismiss) private var dismiss

  public init() {}

  public var body: some View {
    List {
      if viewModel.isPaired {
        pairedContent
      } else {
        unpairedContent
      }
    }
    .listStyle(.insetGrouped)
    .navigationTitle("Parental Freedom")
    .sheet(isPresented: $showPairingSheet) {
      NavigationStack {
        PairingView(viewModel: viewModel)
      }
    }
    .sheet(isPresented: $showChildSelectSheet) {
      NavigationStack {
        ChildSelectView(
          children: viewModel.children,
          onChildSelected: { child, pin in
            viewModel.selectChild(child, pin: pin)
          },
          onDismiss: {
            showChildSelectSheet = false
          }
        )
      }
    }
    .alert("Cannot Unpair", isPresented: $showUnpairAlert) {
      Button("OK", role: .cancel) {}
    } message: {
      Text(
        "This device can only be unpaired by a parent using the Allow2 app or web portal. This protects your child's digital wellbeing settings."
      )
    }
  }

  // MARK: - Paired Content

  private var pairedContent: some View {
    Group {
      // Status Section
      Section {
        HStack {
          Image(systemName: "checkmark.shield.fill")
            .foregroundColor(.green)
            .font(.title2)

          VStack(alignment: .leading, spacing: 4) {
            Text("Protected by Allow2")
              .font(.headline)

            if let child = viewModel.currentChild {
              Text("Currently tracking: \(child.name)")
                .font(.subheadline)
                .foregroundColor(.secondary)
            } else {
              Text("Shared device mode")
                .font(.subheadline)
                .foregroundColor(.secondary)
            }
          }

          Spacer()
        }
        .padding(.vertical, 4)
      }

      // Controls Section
      Section {
        Toggle("Enable Parental Controls", isOn: $viewModel.isEnabled)

        if viewModel.isEnabled {
          Button {
            showChildSelectSheet = true
          } label: {
            HStack {
              Text("Switch User")
              Spacer()
              if let child = viewModel.currentChild {
                Text(child.name)
                  .foregroundColor(.secondary)
              } else {
                Text("Select Child")
                  .foregroundColor(.secondary)
              }
              Image(systemName: "chevron.right")
                .font(.caption)
                .foregroundColor(.secondary)
            }
          }
          .foregroundColor(.primary)
        }
      } header: {
        Text("Controls")
      }

      // Status Section
      if viewModel.isEnabled, let result = viewModel.lastCheckResult {
        Section {
          statusRow(result: result)
        } header: {
          Text("Current Status")
        }
      }

      // Day Type Section
      if let dayTypes = viewModel.lastCheckResult?.dayTypes {
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
          Text("Schedule")
        }
      }

      // Children Section
      Section {
        ForEach(viewModel.children) { child in
          HStack {
            Image(systemName: "person.circle.fill")
              .foregroundColor(.blue)
              .font(.title2)

            Text(child.name)

            Spacer()

            if child.id == viewModel.currentChild?.id {
              Image(systemName: "checkmark")
                .foregroundColor(.green)
            }
          }
        }
      } header: {
        Text("Children")
      }

      // Info Section
      Section {
        Button {
          showUnpairAlert = true
        } label: {
          HStack {
            Image(systemName: "info.circle")
            Text("How to Unpair")
          }
        }
        .foregroundColor(.primary)

        Link(destination: URL(string: "https://allow2.com/help")!) {
          HStack {
            Image(systemName: "questionmark.circle")
            Text("Allow2 Help")
            Spacer()
            Image(systemName: "arrow.up.right")
              .font(.caption)
          }
        }
        .foregroundColor(.primary)
      } header: {
        Text("Help")
      } footer: {
        Text(
          "Parental Freedom is powered by Allow2. Parents can manage settings in the Allow2 app."
        )
      }
    }
  }

  // MARK: - Unpaired Content

  private var unpairedContent: some View {
    Group {
      Section {
        VStack(spacing: 16) {
          Image(systemName: "person.2.badge.gearshape")
            .font(.system(size: 60))
            .foregroundColor(.blue)

          Text("Parental Freedom")
            .font(.title2)
            .fontWeight(.semibold)

          Text(
            "Connect to Allow2 to enable parental controls with flexible time limits, schedules, and activity tracking."
          )
          .font(.subheadline)
          .foregroundColor(.secondary)
          .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 24)
      }

      Section {
        Button {
          showPairingSheet = true
        } label: {
          HStack {
            Image(systemName: "qrcode.viewfinder")
            Text("Pair with Allow2")
            Spacer()
            Image(systemName: "chevron.right")
              .font(.caption)
          }
        }
        .foregroundColor(.primary)
      } footer: {
        Text(
          "Show a QR code for your parent to scan with their Allow2 app, or display a PIN for them to enter."
        )
      }

      Section {
        Link(destination: URL(string: "https://allow2.com")!) {
          HStack {
            Image(systemName: "globe")
            Text("Learn More About Allow2")
            Spacer()
            Image(systemName: "arrow.up.right")
              .font(.caption)
          }
        }
        .foregroundColor(.primary)

        Link(destination: URL(string: "https://allow2.com/signup")!) {
          HStack {
            Image(systemName: "person.badge.plus")
            Text("Create Parent Account")
            Spacer()
            Image(systemName: "arrow.up.right")
              .font(.caption)
          }
        }
        .foregroundColor(.primary)
      }
    }
  }

  // MARK: - Status Row

  @ViewBuilder
  private func statusRow(result: Allow2CheckResult) -> some View {
    if result.allowed {
      if let remaining = result.minimumRemainingSeconds {
        HStack {
          Image(systemName: "clock.fill")
            .foregroundColor(.green)

          VStack(alignment: .leading) {
            Text("Time Remaining")
              .font(.subheadline)
            Text(formatTime(seconds: remaining))
              .font(.headline)
              .foregroundColor(.green)
          }

          Spacer()
        }
      } else {
        HStack {
          Image(systemName: "checkmark.circle.fill")
            .foregroundColor(.green)
          Text("Access Allowed")
          Spacer()
        }
      }
    } else {
      HStack {
        Image(systemName: "hand.raised.fill")
          .foregroundColor(.red)

        VStack(alignment: .leading) {
          Text("Access Blocked")
            .font(.subheadline)
          Text(result.explanation)
            .font(.caption)
            .foregroundColor(.secondary)
        }

        Spacer()
      }
    }
  }

  private func formatTime(seconds: Int) -> String {
    let hours = seconds / 3600
    let minutes = (seconds % 3600) / 60
    let secs = seconds % 60

    if hours > 0 {
      return String(format: "%d:%02d:%02d", hours, minutes, secs)
    } else {
      return String(format: "%d:%02d", minutes, secs)
    }
  }
}

// MARK: - Pairing View (QR/PIN only - device NEVER handles parent credentials)

struct PairingView: View {

  @ObservedObject var viewModel: ParentalFreedomSettingsViewModel

  @State private var deviceName = ""
  @State private var pairingState: PairingState = .idle
  @State private var sessionId: String?
  @State private var qrCodeUrl: String?
  @State private var pinCode: String?
  @State private var showPin = false
  @State private var errorMessage: String?
  @State private var pollTimer: Timer?

  @Environment(\.dismiss) private var dismiss

  enum PairingState {
    case idle
    case initiating
    case waitingForParent
    case completed
    case failed
  }

  var body: some View {
    VStack(spacing: 24) {
      switch pairingState {
      case .idle:
        idleContent

      case .initiating:
        initiatingContent

      case .waitingForParent:
        waitingContent

      case .completed:
        completedContent

      case .failed:
        failedContent
      }
    }
    .padding()
    .navigationTitle("Connect to Allow2")
    .navigationBarTitleDisplayMode(.inline)
    .toolbar {
      ToolbarItem(placement: .cancellationAction) {
        Button("Cancel") {
          cancelPairing()
          dismiss()
        }
      }
    }
    .onAppear {
      deviceName = UIDevice.current.name
    }
    .onDisappear {
      pollTimer?.invalidate()
    }
  }

  // MARK: - Idle Content (Initial State)

  private var idleContent: some View {
    VStack(spacing: 24) {
      Image(systemName: "qrcode.viewfinder")
        .font(.system(size: 60))
        .foregroundColor(.blue)

      Text("Connect to Allow2")
        .font(.title2)
        .fontWeight(.semibold)

      Text(
        "Your parent will scan the QR code or enter the PIN using their Allow2 app. You don't need to enter any credentials."
      )
      .font(.subheadline)
      .foregroundColor(.secondary)
      .multilineTextAlignment(.center)

      TextField("Device Name", text: $deviceName)
        .textFieldStyle(.roundedBorder)
        .frame(maxWidth: 280)

      Button {
        Task {
          await initiatePairing()
        }
      } label: {
        Text("Show Pairing Code")
          .fontWeight(.semibold)
          .frame(maxWidth: .infinity)
          .padding()
          .background(Color.blue)
          .foregroundColor(.white)
          .cornerRadius(10)
      }
      .frame(maxWidth: 280)
      .disabled(deviceName.isEmpty)

      Spacer()
    }
  }

  // MARK: - Initiating Content

  private var initiatingContent: some View {
    VStack(spacing: 24) {
      ProgressView()
        .scaleEffect(1.5)

      Text("Generating pairing code...")
        .font(.subheadline)
        .foregroundColor(.secondary)
    }
  }

  // MARK: - Waiting Content (QR/PIN Display)

  private var waitingContent: some View {
    VStack(spacing: 24) {
      if showPin, let pin = pinCode {
        // PIN Display Mode
        VStack(spacing: 16) {
          Text("Enter this code in the Allow2 app")
            .font(.headline)

          HStack(spacing: 12) {
            ForEach(Array(pin), id: \.self) { char in
              Text(String(char))
                .font(.system(size: 36, weight: .bold, design: .monospaced))
                .frame(width: 44, height: 56)
                .background(Color(.systemGray5))
                .cornerRadius(8)
            }
          }

          Button {
            showPin = false
          } label: {
            Text("Show QR Code Instead")
              .font(.subheadline)
          }
        }
      } else if let qrUrl = qrCodeUrl {
        // QR Code Display Mode
        VStack(spacing: 16) {
          Text("Scan with Allow2 app")
            .font(.headline)

          // QR Code placeholder - actual QR generation would use CoreImage
          ZStack {
            RoundedRectangle(cornerRadius: 12)
              .fill(Color(.systemGray6))
              .frame(width: 200, height: 200)

            if let qrImage = generateQRCode(from: qrUrl) {
              Image(uiImage: qrImage)
                .interpolation(.none)
                .resizable()
                .scaledToFit()
                .frame(width: 180, height: 180)
            } else {
              VStack {
                Image(systemName: "qrcode")
                  .font(.system(size: 80))
                  .foregroundColor(.gray)
                Text(qrUrl)
                  .font(.caption2)
                  .foregroundColor(.secondary)
              }
            }
          }

          if pinCode != nil {
            Button {
              showPin = true
            } label: {
              Text("Show PIN Code Instead")
                .font(.subheadline)
            }
          }
        }
      }

      Text("Waiting for parent to approve...")
        .font(.subheadline)
        .foregroundColor(.secondary)

      ProgressView()

      Text("Ask your parent to scan this code or enter the PIN in their Allow2 app")
        .font(.caption)
        .foregroundColor(.secondary)
        .multilineTextAlignment(.center)
        .padding(.horizontal)
    }
  }

  // MARK: - Completed Content

  private var completedContent: some View {
    VStack(spacing: 24) {
      Image(systemName: "checkmark.circle.fill")
        .font(.system(size: 60))
        .foregroundColor(.green)

      Text("Paired Successfully!")
        .font(.title2)
        .fontWeight(.semibold)

      Text("This device is now protected by Allow2")
        .font(.subheadline)
        .foregroundColor(.secondary)

      Button {
        dismiss()
      } label: {
        Text("Done")
          .fontWeight(.semibold)
          .frame(maxWidth: .infinity)
          .padding()
          .background(Color.blue)
          .foregroundColor(.white)
          .cornerRadius(10)
      }
      .frame(maxWidth: 280)
    }
  }

  // MARK: - Failed Content

  private var failedContent: some View {
    VStack(spacing: 24) {
      Image(systemName: "xmark.circle.fill")
        .font(.system(size: 60))
        .foregroundColor(.red)

      Text("Pairing Failed")
        .font(.title2)
        .fontWeight(.semibold)

      if let error = errorMessage {
        Text(error)
          .font(.subheadline)
          .foregroundColor(.secondary)
          .multilineTextAlignment(.center)
      }

      Button {
        pairingState = .idle
        errorMessage = nil
      } label: {
        Text("Try Again")
          .fontWeight(.semibold)
          .frame(maxWidth: .infinity)
          .padding()
          .background(Color.blue)
          .foregroundColor(.white)
          .cornerRadius(10)
      }
      .frame(maxWidth: 280)
    }
  }

  // MARK: - Actions

  private func initiatePairing() async {
    pairingState = .initiating

    do {
      let session = try await viewModel.initQRPairing(deviceName: deviceName)
      await MainActor.run {
        sessionId = session.sessionId
        qrCodeUrl = session.qrCodeUrl
        pinCode = session.pinCode
        pairingState = .waitingForParent
        startPolling()
      }
    } catch {
      await MainActor.run {
        errorMessage = error.localizedDescription
        pairingState = .failed
      }
    }
  }

  private func startPolling() {
    pollTimer?.invalidate()
    pollTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { _ in
      Task {
        await checkPairingStatus()
      }
    }
  }

  private func checkPairingStatus() async {
    guard let sessionId = sessionId else { return }

    do {
      let status = try await viewModel.checkPairingStatus(sessionId: sessionId)
      await MainActor.run {
        if status.completed {
          pollTimer?.invalidate()
          if status.success {
            pairingState = .completed
          } else {
            errorMessage = status.error ?? "Parent declined or session expired"
            pairingState = .failed
          }
        }
      }
    } catch {
      // Continue polling on error
    }
  }

  private func cancelPairing() {
    pollTimer?.invalidate()
    if let sessionId = sessionId {
      viewModel.cancelPairing(sessionId: sessionId)
    }
  }

  // MARK: - QR Code Generation

  private func generateQRCode(from string: String) -> UIImage? {
    guard let data = string.data(using: .utf8) else { return nil }

    let filter = CIFilter(name: "CIQRCodeGenerator")
    filter?.setValue(data, forKey: "inputMessage")
    filter?.setValue("H", forKey: "inputCorrectionLevel")

    guard let ciImage = filter?.outputImage else { return nil }

    let scaleX = 180 / ciImage.extent.size.width
    let scaleY = 180 / ciImage.extent.size.height
    let transformedImage = ciImage.transformed(by: CGAffineTransform(scaleX: scaleX, y: scaleY))

    let context = CIContext()
    guard let cgImage = context.createCGImage(transformedImage, from: transformedImage.extent) else {
      return nil
    }

    return UIImage(cgImage: cgImage)
  }
}

// MARK: - Pairing Session

struct PairingSession {
  let sessionId: String
  let qrCodeUrl: String
  let pinCode: String?
}

struct PairingStatus {
  let completed: Bool
  let success: Bool
  let error: String?
}

// MARK: - View Model

@MainActor
class ParentalFreedomSettingsViewModel: ObservableObject {

  @Published var isPaired: Bool = false
  @Published var isEnabled: Bool = false {
    didSet {
      Allow2Preferences.isEnabled.value = isEnabled
      if isEnabled {
        Allow2Manager.shared.startTracking()
      } else {
        Allow2Manager.shared.stopTracking()
      }
    }
  }
  @Published var currentChild: Allow2Child?
  @Published var children: [Allow2Child] = []
  @Published var lastCheckResult: Allow2CheckResult?

  private var cancellables: Set<AnyCancellable> = []

  init() {
    loadState()
    observeChanges()
  }

  private func loadState() {
    isPaired = Allow2Manager.shared.isPaired
    isEnabled = Allow2Preferences.isEnabled.value
    currentChild = Allow2Manager.shared.currentChild
    children = Allow2Preferences.cachedChildren
    lastCheckResult = Allow2Manager.shared.lastCheckResult
  }

  private func observeChanges() {
    Allow2Manager.shared.stateDidChange
      .receive(on: DispatchQueue.main)
      .sink { [weak self] state in
        self?.handleStateChange(state)
      }
      .store(in: &cancellables)
  }

  private func handleStateChange(_ state: Allow2State) {
    switch state {
    case .unpaired:
      isPaired = false
      isEnabled = false
      currentChild = nil
      children = []

    case .paired:
      isPaired = true
      children = Allow2Preferences.cachedChildren

    case .childSelected(let child):
      currentChild = child

    case .childCleared:
      currentChild = nil

    default:
      break
    }
  }

  // Initialize QR pairing session - device displays QR, parent scans with their app
  // Parent authenticates with passkey/biometrics on their device
  // Device NEVER sees parent credentials
  func initQRPairing(deviceName: String) async throws -> PairingSession {
    return try await Allow2Manager.shared.initQRPairing(deviceName: deviceName)
  }

  // Initialize PIN pairing session - device displays 6-digit PIN
  // Parent enters in their app and authenticates with passkey
  func initPINPairing(deviceName: String) async throws -> PairingSession {
    return try await Allow2Manager.shared.initPINPairing(deviceName: deviceName)
  }

  // Check if parent has completed the pairing
  func checkPairingStatus(sessionId: String) async throws -> PairingStatus {
    let status = try await Allow2Manager.shared.checkPairingStatus(sessionId: sessionId)
    if status.completed && status.success {
      isPaired = true
      children = Allow2Preferences.cachedChildren
    }
    return status
  }

  // Cancel an active pairing session
  func cancelPairing(sessionId: String) {
    Allow2Manager.shared.cancelPairing(sessionId: sessionId)
  }

  func selectChild(_ child: Allow2Child, pin: String) -> Bool {
    let success = Allow2Manager.shared.selectChild(child, pin: pin)
    if success {
      currentChild = child
    }
    return success
  }
}

// MARK: - Combine import

import Combine

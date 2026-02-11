// Copyright 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import AudioToolbox
import SnapKit
import UIKit

// MARK: - Warning Toast UX Constants

enum WarningToastUX {
  static let toastHeight: CGFloat = 60
  static let toastPadding: CGFloat = 16
  static let cornerRadius: CGFloat = 12
  static let animationDuration: TimeInterval = 0.3
  static let gentleDismissDelay: TimeInterval = 5.0
}

// MARK: - Warning Toast

/// Toast notification for countdown warnings
public class WarningToast: UIView {

  // MARK: - Properties

  private let level: WarningLevel
  private var onRequestMoreTime: (() -> Void)?
  private var onDismiss: (() -> Void)?
  private var countdownTimer: Timer?
  private var remainingSeconds: Int

  weak var viewController: UIViewController?
  var animationConstraint: Constraint?

  // MARK: - UI Components

  private lazy var containerView: UIView = {
    let view = UIView()
    view.backgroundColor = backgroundColor(for: level)
    view.layer.cornerRadius = WarningToastUX.cornerRadius
    view.clipsToBounds = true
    return view
  }()

  private lazy var iconImageView: UIImageView = {
    let imageView = UIImageView()
    imageView.contentMode = .scaleAspectFit
    imageView.tintColor = .white
    imageView.image = icon(for: level)
    return imageView
  }()

  private lazy var titleLabel: UILabel = {
    let label = UILabel()
    label.textColor = .white
    label.font = .systemFont(ofSize: 14, weight: .semibold)
    return label
  }()

  private lazy var timeLabel: UILabel = {
    let label = UILabel()
    label.textColor = .white
    label.font = .monospacedDigitSystemFont(ofSize: 16, weight: .bold)
    return label
  }()

  private lazy var progressView: UIProgressView = {
    let progress = UIProgressView(progressViewStyle: .default)
    progress.progressTintColor = .white
    progress.trackTintColor = .white.withAlphaComponent(0.3)
    return progress
  }()

  private lazy var requestButton: UIButton = {
    let button = UIButton(type: .system)
    button.setTitle("Request More Time", for: .normal)
    button.setTitleColor(.white, for: .normal)
    button.titleLabel?.font = .systemFont(ofSize: 12, weight: .semibold)
    button.backgroundColor = .white.withAlphaComponent(0.2)
    button.layer.cornerRadius = 4
    button.contentEdgeInsets = UIEdgeInsets(top: 4, left: 8, bottom: 4, right: 8)
    button.addTarget(self, action: #selector(requestMoreTimeTapped), for: .touchUpInside)
    return button
  }()

  private lazy var dismissButton: UIButton = {
    let button = UIButton(type: .system)
    button.setImage(UIImage(systemName: "xmark"), for: .normal)
    button.tintColor = .white.withAlphaComponent(0.8)
    button.addTarget(self, action: #selector(dismissTapped), for: .touchUpInside)
    return button
  }()

  // MARK: - Initialization

  public init(
    level: WarningLevel,
    onRequestMoreTime: (() -> Void)? = nil,
    onDismiss: (() -> Void)? = nil
  ) {
    self.level = level
    self.onRequestMoreTime = onRequestMoreTime
    self.onDismiss = onDismiss

    switch level {
    case .gentle(let minutes):
      self.remainingSeconds = minutes * 60
    case .warning(let minutes):
      self.remainingSeconds = minutes * 60
    case .urgent(let seconds):
      self.remainingSeconds = seconds
    case .blocked, .none:
      self.remainingSeconds = 0
    }

    super.init(frame: .zero)
    setupUI()
    updateUI()
  }

  required init?(coder: NSCoder) {
    fatalError("init(coder:) has not been implemented")
  }

  deinit {
    countdownTimer?.invalidate()
  }

  // MARK: - Setup

  private func setupUI() {
    addSubview(containerView)

    containerView.addSubview(iconImageView)
    containerView.addSubview(titleLabel)
    containerView.addSubview(timeLabel)
    containerView.addSubview(dismissButton)

    // Show progress bar for warning and urgent levels
    switch level {
    case .warning, .urgent:
      containerView.addSubview(progressView)
      containerView.addSubview(requestButton)
    default:
      break
    }

    containerView.snp.makeConstraints { make in
      make.edges.equalToSuperview()
    }

    iconImageView.snp.makeConstraints { make in
      make.leading.equalToSuperview().offset(WarningToastUX.toastPadding)
      make.centerY.equalToSuperview()
      make.width.height.equalTo(24)
    }

    dismissButton.snp.makeConstraints { make in
      make.trailing.equalToSuperview().offset(-WarningToastUX.toastPadding)
      make.centerY.equalToSuperview()
      make.width.height.equalTo(24)
    }

    switch level {
    case .warning, .urgent:
      titleLabel.snp.makeConstraints { make in
        make.leading.equalTo(iconImageView.snp.trailing).offset(12)
        make.top.equalToSuperview().offset(8)
      }

      timeLabel.snp.makeConstraints { make in
        make.trailing.equalTo(dismissButton.snp.leading).offset(-12)
        make.top.equalToSuperview().offset(8)
      }

      progressView.snp.makeConstraints { make in
        make.leading.equalTo(iconImageView.snp.trailing).offset(12)
        make.trailing.equalTo(dismissButton.snp.leading).offset(-12)
        make.top.equalTo(titleLabel.snp.bottom).offset(6)
        make.height.equalTo(4)
      }

      requestButton.snp.makeConstraints { make in
        make.leading.equalTo(iconImageView.snp.trailing).offset(12)
        make.top.equalTo(progressView.snp.bottom).offset(6)
      }

    default:
      titleLabel.snp.makeConstraints { make in
        make.leading.equalTo(iconImageView.snp.trailing).offset(12)
        make.centerY.equalToSuperview()
      }

      timeLabel.snp.makeConstraints { make in
        make.trailing.equalTo(dismissButton.snp.leading).offset(-12)
        make.centerY.equalToSuperview()
      }
    }
  }

  private func updateUI() {
    titleLabel.text = title(for: level)
    timeLabel.text = formatTime(remainingSeconds)

    switch level {
    case .warning, .urgent:
      let totalSeconds = initialSeconds(for: level)
      progressView.progress = Float(remainingSeconds) / Float(totalSeconds)
    default:
      break
    }
  }

  // MARK: - Show/Dismiss

  public func show(
    in viewController: UIViewController,
    delay: TimeInterval = 0,
    autoDismiss: Bool = true
  ) {
    self.viewController = viewController

    viewController.view.addSubview(self)

    snp.makeConstraints { make in
      make.leading.trailing.equalToSuperview().inset(WarningToastUX.toastPadding)
      animationConstraint = make.bottom.equalTo(viewController.view.safeAreaLayoutGuide.snp.bottom)
        .offset(100).constraint
    }

    layoutIfNeeded()

    DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
      guard let self = self else { return }

      UIView.animate(withDuration: WarningToastUX.animationDuration) {
        self.animationConstraint?.update(offset: -WarningToastUX.toastPadding)
        self.layoutIfNeeded()
      } completion: { _ in
        self.startCountdownIfNeeded()
        self.playSound()

        if autoDismiss, case .gentle = self.level {
          DispatchQueue.main.asyncAfter(deadline: .now() + WarningToastUX.gentleDismissDelay) {
            [weak self] in
            self?.dismiss()
          }
        }
      }
    }
  }

  public func dismiss(animated: Bool = true) {
    countdownTimer?.invalidate()

    let duration = animated ? WarningToastUX.animationDuration : 0

    UIView.animate(withDuration: duration) {
      self.animationConstraint?.update(offset: 100)
      self.layoutIfNeeded()
    } completion: { _ in
      self.removeFromSuperview()
      self.onDismiss?()
    }
  }

  // MARK: - Countdown

  private func startCountdownIfNeeded() {
    switch level {
    case .warning, .urgent:
      countdownTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) {
        [weak self] _ in
        self?.updateCountdown()
      }
    default:
      break
    }
  }

  private func updateCountdown() {
    remainingSeconds -= 1

    if remainingSeconds <= 0 {
      countdownTimer?.invalidate()
      dismiss()
      return
    }

    updateUI()
  }

  // MARK: - Actions

  @objc private func requestMoreTimeTapped() {
    onRequestMoreTime?()
    dismiss()
  }

  @objc private func dismissTapped() {
    dismiss()
  }

  // MARK: - Helpers

  private func backgroundColor(for level: WarningLevel) -> UIColor {
    switch level {
    case .gentle:
      return UIColor(red: 0.2, green: 0.5, blue: 0.9, alpha: 0.95)
    case .warning:
      return UIColor(red: 0.9, green: 0.6, blue: 0.1, alpha: 0.95)
    case .urgent:
      return UIColor(red: 0.9, green: 0.2, blue: 0.2, alpha: 0.95)
    case .blocked:
      return UIColor(red: 0.6, green: 0.1, blue: 0.1, alpha: 0.95)
    case .none:
      return .clear
    }
  }

  private func icon(for level: WarningLevel) -> UIImage? {
    switch level {
    case .gentle:
      return UIImage(systemName: "clock")
    case .warning:
      return UIImage(systemName: "exclamationmark.triangle")
    case .urgent:
      return UIImage(systemName: "exclamationmark.circle.fill")
    case .blocked:
      return UIImage(systemName: "hand.raised.fill")
    case .none:
      return nil
    }
  }

  private func title(for level: WarningLevel) -> String {
    switch level {
    case .gentle(let minutes):
      return "\(minutes) minutes of internet time remaining"
    case .warning:
      return "Only a few minutes left!"
    case .urgent:
      return "BROWSING ENDS IN:"
    case .blocked:
      return "Access Blocked"
    case .none:
      return ""
    }
  }

  private func initialSeconds(for level: WarningLevel) -> Int {
    switch level {
    case .gentle(let minutes):
      return minutes * 60
    case .warning(let minutes):
      return minutes * 60
    case .urgent(let seconds):
      return seconds
    default:
      return 60
    }
  }

  private func formatTime(_ seconds: Int) -> String {
    let mins = seconds / 60
    let secs = seconds % 60
    return String(format: "%d:%02d", mins, secs)
  }

  private func playSound() {
    switch level {
    case .gentle:
      AudioServicesPlaySystemSound(1007)  // Gentle tap
    case .warning:
      AudioServicesPlaySystemSound(1016)  // Tweet sound
    case .urgent:
      AudioServicesPlaySystemSound(1005)  // Alarm
    case .blocked:
      AudioServicesPlaySystemSound(1073)  // Lock sound
    case .none:
      break
    }
  }
}

// MARK: - Urgent Banner View (Full Width)

/// Full-width top banner for urgent countdown (final minute)
public class UrgentCountdownBanner: UIView {

  private var remainingSeconds: Int
  private var countdownTimer: Timer?
  private var onTimeUp: (() -> Void)?
  private var onRequestMoreTime: (() -> Void)?

  weak var viewController: UIViewController?
  var animationConstraint: Constraint?

  // MARK: - UI Components

  private lazy var backgroundView: UIView = {
    let view = UIView()
    view.backgroundColor = UIColor(red: 0.9, green: 0.1, blue: 0.1, alpha: 0.95)
    return view
  }()

  private lazy var warningIcon: UIImageView = {
    let imageView = UIImageView(image: UIImage(systemName: "exclamationmark.circle.fill"))
    imageView.tintColor = .white
    imageView.contentMode = .scaleAspectFit
    return imageView
  }()

  private lazy var titleLabel: UILabel = {
    let label = UILabel()
    label.text = "BROWSING ENDS IN:"
    label.font = .systemFont(ofSize: 14, weight: .bold)
    label.textColor = .white
    return label
  }()

  private lazy var countdownLabel: UILabel = {
    let label = UILabel()
    label.font = .monospacedDigitSystemFont(ofSize: 24, weight: .bold)
    label.textColor = .white
    return label
  }()

  private lazy var progressBar: UIProgressView = {
    let progress = UIProgressView(progressViewStyle: .default)
    progress.progressTintColor = .white
    progress.trackTintColor = .white.withAlphaComponent(0.3)
    return progress
  }()

  private lazy var requestButton: UIButton = {
    let button = UIButton(type: .system)
    button.setTitle("Request More Time", for: .normal)
    button.setTitleColor(UIColor(red: 0.9, green: 0.1, blue: 0.1, alpha: 1), for: .normal)
    button.titleLabel?.font = .systemFont(ofSize: 12, weight: .semibold)
    button.backgroundColor = .white
    button.layer.cornerRadius = 4
    button.contentEdgeInsets = UIEdgeInsets(top: 6, left: 12, bottom: 6, right: 12)
    button.addTarget(self, action: #selector(requestTapped), for: .touchUpInside)
    return button
  }()

  private lazy var understandButton: UIButton = {
    let button = UIButton(type: .system)
    button.setTitle("I understand", for: .normal)
    button.setTitleColor(.white.withAlphaComponent(0.8), for: .normal)
    button.titleLabel?.font = .systemFont(ofSize: 12)
    button.addTarget(self, action: #selector(understandTapped), for: .touchUpInside)
    return button
  }()

  // MARK: - Initialization

  public init(
    seconds: Int,
    onTimeUp: (() -> Void)?,
    onRequestMoreTime: (() -> Void)?
  ) {
    self.remainingSeconds = seconds
    self.onTimeUp = onTimeUp
    self.onRequestMoreTime = onRequestMoreTime
    super.init(frame: .zero)
    setupUI()
    updateUI()
  }

  required init?(coder: NSCoder) {
    fatalError("init(coder:) has not been implemented")
  }

  deinit {
    countdownTimer?.invalidate()
  }

  // MARK: - Setup

  private func setupUI() {
    addSubview(backgroundView)
    backgroundView.addSubview(warningIcon)
    backgroundView.addSubview(titleLabel)
    backgroundView.addSubview(countdownLabel)
    backgroundView.addSubview(progressBar)
    backgroundView.addSubview(requestButton)
    backgroundView.addSubview(understandButton)

    backgroundView.snp.makeConstraints { make in
      make.edges.equalToSuperview()
    }

    warningIcon.snp.makeConstraints { make in
      make.leading.equalToSuperview().offset(16)
      make.top.equalTo(safeAreaLayoutGuide).offset(8)
      make.width.height.equalTo(20)
    }

    titleLabel.snp.makeConstraints { make in
      make.leading.equalTo(warningIcon.snp.trailing).offset(8)
      make.centerY.equalTo(warningIcon)
    }

    countdownLabel.snp.makeConstraints { make in
      make.trailing.equalToSuperview().offset(-16)
      make.centerY.equalTo(warningIcon)
    }

    progressBar.snp.makeConstraints { make in
      make.leading.trailing.equalToSuperview().inset(16)
      make.top.equalTo(titleLabel.snp.bottom).offset(8)
      make.height.equalTo(6)
    }

    requestButton.snp.makeConstraints { make in
      make.leading.equalToSuperview().offset(16)
      make.top.equalTo(progressBar.snp.bottom).offset(8)
      make.bottom.equalTo(safeAreaLayoutGuide).offset(-8)
    }

    understandButton.snp.makeConstraints { make in
      make.trailing.equalToSuperview().offset(-16)
      make.centerY.equalTo(requestButton)
    }
  }

  private func updateUI() {
    countdownLabel.text = formatTime(remainingSeconds)
    progressBar.progress = Float(remainingSeconds) / 60.0
  }

  // MARK: - Show/Dismiss

  public func show(in viewController: UIViewController) {
    self.viewController = viewController

    viewController.view.addSubview(self)

    snp.makeConstraints { make in
      make.leading.trailing.equalToSuperview()
      animationConstraint = make.top.equalToSuperview().offset(-200).constraint
    }

    layoutIfNeeded()

    UIView.animate(withDuration: 0.3) {
      self.animationConstraint?.update(offset: 0)
      self.layoutIfNeeded()
    } completion: { _ in
      self.startCountdown()
      AudioServicesPlaySystemSound(1005)  // Alarm
    }
  }

  public func dismiss(animated: Bool = true) {
    countdownTimer?.invalidate()

    UIView.animate(withDuration: animated ? 0.3 : 0) {
      self.animationConstraint?.update(offset: -200)
      self.layoutIfNeeded()
    } completion: { _ in
      self.removeFromSuperview()
    }
  }

  // MARK: - Countdown

  private func startCountdown() {
    countdownTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
      self?.tick()
    }
  }

  private func tick() {
    remainingSeconds -= 1

    if remainingSeconds <= 0 {
      countdownTimer?.invalidate()
      onTimeUp?()
      dismiss()
      return
    }

    updateUI()

    // Play tick sound for last 10 seconds
    if remainingSeconds <= 10 {
      AudioServicesPlaySystemSound(1104)  // Tick
    }
  }

  // MARK: - Actions

  @objc private func requestTapped() {
    onRequestMoreTime?()
  }

  @objc private func understandTapped() {
    dismiss()
  }

  // MARK: - Helpers

  private func formatTime(_ seconds: Int) -> String {
    let mins = seconds / 60
    let secs = seconds % 60
    return String(format: "%d:%02d", mins, secs)
  }
}

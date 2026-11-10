import AVFAudio
import SwiftUI

/// Owns the audio session and engine. Main actor. The real-time work all
/// happens in C++ inside the source node's render block.
final class AudioController {
  var mode: SoundMode {
    didSet {
      guard mode != oldValue else { return }
      applyMode()
    }
  }

  /// Muting fades out and releases the session like leaving the app does,
  /// so nothing plays silence in the background.
  var isMuted = false {
    didSet {
      guard isMuted != oldValue else { return }
      updateRunState()
    }
  }

  private let core: DHCore
  private var engine = AVAudioEngine()
  private var source: AVAudioSourceNode?
  private let format: AVAudioFormat
  private var isForeground = true
  private var pauseGeneration = 0
  private var observers: [any NSObjectProtocol] = []

  init(core: DHCore, mode: SoundMode) {
    self.core = core
    self.mode = mode
    // Fixed at the core's native 48 kHz. If the hardware runs at another
    // rate, the main mixer converts.
    guard let format = AVAudioFormat(standardFormatWithSampleRate: DHCore.sampleRate, channels: 2)
    else { fatalError("Could not create the 48 kHz stereo format") }
    self.format = format
    buildGraph()
    observeSystem()
  }

  func scenePhaseChanged(_ phase: ScenePhase) {
    switch phase {
    case .active:
      isForeground = true
      updateRunState()
    case .background:
      isForeground = false
      updateRunState()
    default:
      break
    }
  }

  // MARK: - Run state

  private var shouldRun: Bool {
    !isMuted && (isForeground || mode == .alwaysIncludingBackground)
  }

  private func updateRunState() {
    if shouldRun { startIfNeeded() } else { fadeOutAndPause() }
  }

  private func applyMode() {
    // Switching categories while running can reroute audio; restart cleanly.
    do { try configureSession() } catch { log("session config failed", error) }
    updateRunState()
  }

  private func buildGraph() {
    let node = core.makeSourceNode(format: format)
    engine.attach(node)
    do {
      try engine.connectNode(node, to: engine.mainMixerNode, format: format)
    } catch {
      log("connect failed", error)
    }
    source = node
  }

  private func configureSession() throws {
    let session = AVAudioSession.sharedInstance()
    let category: AVAudioSession.Category = mode == .respectSilentMode ? .ambient : .playback
    try session.setCategory(category, mode: .default, options: [])
    try session.setPreferredSampleRate(DHCore.sampleRate)
    try session.setPreferredIOBufferDuration(0.005)
  }

  private func startIfNeeded() {
    pauseGeneration += 1  // cancels a pending pause
    do {
      try configureSession()
      try AVAudioSession.sharedInstance().setActive(true)
      if !engine.isRunning {
        engine.prepare()
        try engine.start()
      }
      core.setGainTarget(1)
    } catch {
      log("start failed", error)
    }
  }

  private func fadeOutAndPause() {
    core.setGainTarget(0)
    pauseGeneration += 1
    let generation = pauseGeneration
    Task { [weak self] in
      try? await Task.sleep(for: .milliseconds(150))
      guard let self, generation == self.pauseGeneration, !self.shouldRun else { return }
      self.engine.pause()
      try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }
  }

  // MARK: - System events

  private func observeSystem() {
    let center = NotificationCenter.default
    observers.append(
      center.addObserver(
        forName: AVAudioSession.resumptionRecommendationNotification, object: nil, queue: .main
      ) { [weak self] note in
        let context = note.userInfo?[AVAudioSession.resumptionContextKey]
          as? AVAudioSession.ResumptionContext
        let recommendation = context?.recommendation
        MainActor.assumeIsolated {
          self?.handleResumption(recommendation)
        }
      })
    observers.append(
      center.addObserver(
        forName: .AVAudioEngineConfigurationChange, object: nil, queue: .main
      ) { [weak self] _ in
        MainActor.assumeIsolated { self?.handleConfigurationChange() }
      })
    observers.append(
      center.addObserver(
        forName: AVAudioSession.mediaServicesWereResetNotification, object: nil, queue: .main
      ) { [weak self] _ in
        MainActor.assumeIsolated { self?.handleMediaServicesReset() }
      })
  }

  private func handleResumption(_ recommendation: AVAudioSession.ResumptionRecommendation?) {
    // Posted after an interruption ends. startIfNeeded() reactivates the session.
    if recommendation == .shouldResume, shouldRun { startIfNeeded() }
  }

  private func handleConfigurationChange() {
    // The engine stops itself when the hardware format or route changes.
    log("engine configuration changed, running: \(engine.isRunning)")
    guard shouldRun, !engine.isRunning else { return }
    if let source {
      do {
        try engine.connectNode(source, to: engine.mainMixerNode, format: format)
      } catch {
        log("reconnect failed", error)
      }
    }
    startIfNeeded()
  }

  private func handleMediaServicesReset() {
    // Everything audio is invalid now: rebuild from scratch.
    log("media services were reset")
    engine = AVAudioEngine()
    buildGraph()
    if shouldRun { startIfNeeded() }
  }

  private func log(_ message: String, _ error: Error) {
    log("\(message): \(error)")
  }

  private func log(_ message: String) {
    print("Dihedra audio: \(message)")
  }
}

import QuartzCore
import SwiftUI

enum HingeAvailability {
  case unknown, present, absent
}

/// App-wide state. Main actor (the target's default isolation).
@Observable
final class AppModel {
  let core: DHCore
  let audio: AudioController
  let camera: CameraFeed

  private(set) var hingeAvailability: HingeAvailability = .unknown

  /// Stand-in hinge for iPhones without one (and for debugging on a Duo).
  /// Same scale as the real hinge, from DHCore.steepDegrees (the most
  /// folded) to DHCore.flatDegrees. Starts at k = 4.
  var virtualAngle: Double = 146 {
    didSet { if usesVirtualHinge { pushVirtualAngle() } }
  }

  var soundMode: SoundMode {
    didSet {
      soundMode.store()
      audio.mode = soundMode
    }
  }

  var pictureSource: PictureSource {
    didSet {
      pictureSource.store()
      camera.isWanted = pictureSource == .camera
    }
  }

  var cameraFacing: CameraFacing {
    didSet {
      cameraFacing.store()
      camera.facing = cameraFacing
    }
  }

  /// The committed symmetry, rounded. The renderer reports it when it changes.
  var symmetry = 1

  /// Freeze the picture and the sound so the phone can be put down or
  /// regripped without the pattern changing.
  var isHeld = false {
    didSet { core.isHeld = isHeld }
  }

  var isMuted = false {
    didSet { audio.isMuted = isMuted }
  }

  /// Debug: drive the app from the on-screen fold control even on a Duo.
  var simulateHinge = false {
    didSet { if simulateHinge { pushVirtualAngle() } }
  }

  /// Debug: compare detents with a fully continuous fold.
  var detents = true {
    didSet { core.detentsEnabled = detents }
  }

  var usesVirtualHinge: Bool { hingeAvailability == .absent || simulateHinge }

  @ObservationIgnored private var started = false

  init() {
    let core = DHCore()
    let mode = SoundMode.stored
    self.core = core
    self.soundMode = mode
    self.audio = AudioController(core: core, mode: mode)

    let source = PictureSource.stored
    let facing = CameraFacing.stored
    let camera = CameraFeed()
    camera.facing = facing
    camera.isWanted = source == .camera
    self.camera = camera
    self.pictureSource = source
    self.cameraFacing = facing
  }

  func start() {
    guard !started else { return }
    started = true
    // onHingeChange may not fire until the hinge moves, and never fires on
    // phones without one. If nothing arrives, fall back to the fold control.
    Task {
      try? await Task.sleep(for: .seconds(1.5))
      if hingeAvailability == .unknown {
        hingeAvailability = .absent
        pushVirtualAngle()
      }
    }
  }

  func hingeChanged(_ context: DeviceHingeContext) {
    guard let hinge = context.hinge else {
      hingeAvailability = .absent
      pushVirtualAngle()
      return
    }
    hingeAvailability = .present
    guard !simulateHinge else { return }
    core.hingeChanged(
      degrees: hinge.angle.degrees,
      status: DHHingeStatus(hinge.status),
      time: CACurrentMediaTime())
  }

  func scenePhaseChanged(_ phase: ScenePhase) {
    audio.scenePhaseChanged(phase)
    camera.isSceneActive = phase == .active
  }

  private func pushVirtualAngle() {
    core.hingeChanged(degrees: virtualAngle, status: .partiallyOpen, time: CACurrentMediaTime())
  }
}

extension DHHingeStatus {
  init(_ status: DeviceHinge.Status) {
    if status == .closed {
      self = .closed
    } else if status == .fullyOpen {
      self = .fullyOpen
    } else {
      self = .partiallyOpen
    }
  }
}

import AVFoundation
import AVKit
import Metal
import QuartzCore
import Synchronization
import UIKit

/// Whether Dihedra may use the camera.
enum CameraAccess {
  case undetermined, authorized, denied
}

/// Streams a camera into Metal textures for the kaleidoscope. Main actor;
/// the capture session itself runs on CameraCapture's queues.
///
/// The camera is chosen by the way it faces, which an
/// AVCaptureDeviceDirectionCoordinator reports, not by its position: opening
/// or closing iPhone Duo moves the app to the other display, and a camera
/// that faced the scene then faces the person.
@Observable
final class CameraFeed {
  /// Whether the camera is the picture source.
  var isWanted = false {
    didSet { update() }
  }

  var facing: CameraFacing = .away {
    didSet { selectCamera() }
  }

  /// Apps can't use the camera in the background, and the picture pauses
  /// there anyway.
  var isSceneActive = false {
    didSet {
      // Permission may have changed in Settings meanwhile.
      if isSceneActive { access = Self.currentAccess() }
      update()
    }
  }

  private(set) var access = CameraFeed.currentAccess()

  /// False when no camera faces the chosen way in the current posture.
  private(set) var hasCamera = true

  /// Radians: how far to turn the camera image clockwise to level it on screen.
  @ObservationIgnored private(set) var levelAngle: Float = 0

  var isActive: Bool { isWanted && isSceneActive && access == .authorized }

  @ObservationIgnored private let capture = CameraCapture()
  @ObservationIgnored private weak var view: UIView?
  @ObservationIgnored private var directions: AVCaptureDeviceDirectionCoordinator?
  @ObservationIgnored private var directionMap: AVCaptureDeviceDirectionMap?
  @ObservationIgnored private var activeCameraID: String?
  @ObservationIgnored private var rotation: AVCaptureDevice.RotationCoordinator?
  @ObservationIgnored private var rotationObservation: NSKeyValueObservation?
  @ObservationIgnored private var isRequestingAccess = false

  /// Every built-in camera Dihedra may use, most preferred first. The two
  /// physical front cameras stand in for the virtual front camera, which a
  /// direction coordinator leaves out.
  private static let deviceTypes: [AVCaptureDevice.DeviceType] = [
    .builtInWideAngleCamera, .builtInInnerUltraWideCamera, .builtInOuterUltraWideCamera,
  ]

  /// The view that shows the picture. It's the frame of reference for which
  /// way a camera faces.
  func attach(_ view: UIView) {
    guard view !== self.view else { return }
    self.view = view
    // Both coordinators work relative to the view, so start them afresh.
    directions = nil
    directionMap = nil
    activeCameraID = nil
    observeRotation(of: nil)
    update()
  }

  /// The newest camera image, or nil before the first. Any thread.
  nonisolated func latestFrame() -> CameraFrame? { capture.latestFrame() }

  private func update() {
    if isWanted, access == .undetermined { requestAccess() }
    guard isActive, let view else {
      directions = nil
      capture.setRunning(false)
      return
    }
    if directions == nil {
      // Calls back soon with the current directions, then on every change.
      directions = AVCaptureDeviceDirectionCoordinator(view: view, deviceTypes: Self.deviceTypes) {
        [weak self] map in
        self?.directionMap = map
        self?.selectCamera()
      }
    }
    capture.setRunning(true)
  }

  private func selectCamera() {
    guard isActive, let map = directionMap else { return }
    let candidates =
      facing == .away ? map.backwardFacingDeviceDescriptors : map.forwardFacingDeviceDescriptors
    // Keep the current camera while it still faces the right way.
    if let activeCameraID, candidates.contains(where: { $0.uniqueID == activeCameraID }) { return }
    let choice = candidates.min { Self.rank($0.deviceType) < Self.rank($1.deviceType) }
    print("Dihedra camera: \(facing.rawValue) is \(choice?.localizedName ?? "no camera")")
    hasCamera = choice != nil
    activeCameraID = choice?.uniqueID
    capture.select(cameraID: choice?.uniqueID)
    observeRotation(of: choice?.uniqueID)
  }

  private static func rank(_ type: AVCaptureDevice.DeviceType) -> Int {
    deviceTypes.firstIndex(of: type) ?? deviceTypes.count
  }

  /// The capture system reports how far to turn this camera's image to keep
  /// it level in the view, including after the app moves between displays.
  private func observeRotation(of cameraID: String?) {
    rotationObservation = nil
    rotation = nil
    guard let cameraID, let view, let device = AVCaptureDevice(uniqueID: cameraID) else { return }
    let coordinator = AVCaptureDevice.RotationCoordinator(device: device, previewLayer: view.layer)
    // The coordinator notifies on the main queue.
    rotationObservation = coordinator.observe(
      \.videoRotationAngleForHorizonLevelPreview, options: [.initial, .new]
    ) { [weak self] coordinator, _ in
      let degrees = coordinator.videoRotationAngleForHorizonLevelPreview
      MainActor.assumeIsolated {
        print("Dihedra camera: level by \(degrees) degrees")
        self?.levelAngle = Float(degrees) * .pi / 180
      }
    }
    rotation = coordinator
  }

  private func requestAccess() {
    guard !isRequestingAccess else { return }
    isRequestingAccess = true
    Task {
      let granted = await AVCaptureDevice.requestAccess(for: .video)
      isRequestingAccess = false
      access = granted ? .authorized : .denied
      update()
    }
  }

  private static func currentAccess() -> CameraAccess {
    switch AVCaptureDevice.authorizationStatus(for: .video) {
    case .authorized: .authorized
    case .notDetermined: .undetermined
    default: .denied
    }
  }
}

/// One camera image as a Metal texture. Holding it keeps the texture valid.
nonisolated struct CameraFrame: @unchecked Sendable {
  let texture: any MTLTexture
  let backing: CVMetalTexture
  /// CACurrentMediaTime() when it arrived.
  let time: CFTimeInterval
}

/// Owns the capture session. Session work runs on one queue and frames
/// arrive on another, so starting the camera never blocks the main thread.
nonisolated final class CameraCapture: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate,
  @unchecked Sendable
{
  private let sessionQueue = DispatchQueue(label: "Dihedra camera session")
  private let frameQueue = DispatchQueue(label: "Dihedra camera frames")
  private let session = AVCaptureSession()
  private let output = AVCaptureVideoDataOutput()
  private let textureCache: CVMetalTextureCache?
  private let latest = Mutex<CameraFrame?>(nil)
  // Session queue only.
  private var input: AVCaptureDeviceInput?
  private var wantsRunning = false

  override init() {
    var cache: CVMetalTextureCache?
    if let device = MTLCreateSystemDefaultDevice() {
      CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, device, nil, &cache)
    }
    textureCache = cache
    super.init()
    sessionQueue.async { self.configure() }
  }

  func latestFrame() -> CameraFrame? { latest.withLock { $0 } }

  func setRunning(_ running: Bool) {
    sessionQueue.async {
      self.wantsRunning = running
      self.applyRunning()
    }
  }

  /// Switches to the camera with this unique ID, or to none.
  func select(cameraID: String?) {
    sessionQueue.async {
      self.replaceInput(with: cameraID)
      self.applyRunning()
    }
  }

  private func configure() {
    session.beginConfiguration()
    defer { session.commitConfiguration() }
    // Plenty for a picture made of mirrors, and light on power.
    if session.canSetSessionPreset(.hd1920x1080) { session.sessionPreset = .hd1920x1080 }
    output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
    output.alwaysDiscardsLateVideoFrames = true
    // The frames are only ever shown, never recorded, so screen-sized ones
    // are enough. They let focus and exposure settle faster, and they allow
    // preview stabilization on cameras without the low latency kind.
    output.automaticallyConfiguresOutputBufferDimensions = false
    output.deliversPreviewSizedOutputBuffers = true
    output.setSampleBufferDelegate(self, queue: frameQueue)
    if session.canAddOutput(output) { session.addOutput(output) }
  }

  private func replaceInput(with cameraID: String?) {
    guard cameraID != input?.device.uniqueID else { return }
    session.beginConfiguration()
    defer { session.commitConfiguration() }
    if let input {
      session.removeInput(input)
      self.input = nil
    }
    // The cameras can change while this waited in the queue.
    guard let cameraID, let device = AVCaptureDevice(uniqueID: cameraID),
      let newInput = try? AVCaptureDeviceInput(device: device)
    else { return }
    // A camera without the 1080p preset still gets its best default.
    if !session.canAddInput(newInput) { session.sessionPreset = .high }
    guard session.canAddInput(newInput) else { return }
    session.addInput(newInput)
    input = newInput
    guard let connection = output.connection(with: .video) else { return }
    // The shader levels the image for free, so the connection mustn't also
    // rotate every buffer.
    if connection.isVideoRotationAngleSupported(0) { connection.videoRotationAngle = 0 }
    if connection.isVideoStabilizationSupported {
      connection.preferredVideoStabilizationMode = Self.stabilization(for: device.activeFormat)
    }
  }

  /// The mirrors multiply every tremor of the hand, so the image is always
  /// steadied. The first two modes add almost no delay; standard, the
  /// fallback, adds a little.
  private static func stabilization(for format: AVCaptureDevice.Format) -> AVCaptureVideoStabilizationMode {
    [.lowLatency, .previewOptimized, .standard].first(where: format.isVideoStabilizationModeSupported)
      ?? .off
  }

  private func applyRunning() {
    let run = wantsRunning && input != nil
    if run, !session.isRunning {
      session.startRunning()
    } else if !run, session.isRunning {
      session.stopRunning()
    }
  }

  func captureOutput(
    _ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection
  ) {
    guard let textureCache, let pixels = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
    var backing: CVMetalTexture?
    let status = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, textureCache, pixels, nil, .bgra8Unorm,
      CVPixelBufferGetWidth(pixels), CVPixelBufferGetHeight(pixels), 0, &backing)
    guard status == kCVReturnSuccess, let backing, let texture = CVMetalTextureGetTexture(backing)
    else { return }
    let frame = CameraFrame(texture: texture, backing: backing, time: CACurrentMediaTime())
    latest.withLock { $0 = frame }
  }
}

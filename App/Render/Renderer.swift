import MetalKit
import QuartzCore

/// Draws the kaleidoscope. Runs on the main thread (MTKView's default), which
/// is also where hinge updates arrive, so the core's mapper stays single-threaded.
final class Renderer: NSObject {
  /// Seconds to fade between the glass and the camera.
  private static let sourceFade: Float = 0.5
  /// A camera image older than this counts as gone (stopped or interrupted).
  private static let staleFrameAge: CFTimeInterval = 0.5

  private let core: DHCore
  private let camera: CameraFeed
  private let queue: MTLCommandQueue
  private let glassPipeline: MTLRenderPipelineState
  private let cameraPipeline: MTLRenderPipelineState
  private let startTime = CACurrentMediaTime()
  private let onSymmetryChange: (Int) -> Void
  private var symmetry = 0
  private var lastDrawTime = CACurrentMediaTime()
  /// 0 shows the glass, 1 the camera.
  private var cameraMix: Float = 0
  /// The camera image being shown, kept through a fade out.
  private var cameraFrame: CameraFrame?

  init?(view: MTKView, core: DHCore, camera: CameraFeed, onSymmetryChange: @escaping (Int) -> Void) {
    guard let device = view.device,
      let queue = device.makeCommandQueue(),
      let library = device.makeDefaultLibrary()
    else { return nil }

    do {
      glassPipeline = try Self.makePipeline(device: device, library: library, view: view, camera: false)
      cameraPipeline = try Self.makePipeline(device: device, library: library, view: view, camera: true)
    } catch {
      print("Dihedra: pipeline creation failed: \(error)")
      return nil
    }
    self.queue = queue
    self.core = core
    self.camera = camera
    self.onSymmetryChange = onSymmetryChange
    super.init()
  }

  private static func makePipeline(
    device: MTLDevice, library: MTLLibrary, view: MTKView, camera: Bool
  ) throws -> MTLRenderPipelineState {
    let constants = MTLFunctionConstantValues()
    var usesCamera = camera
    constants.setConstantValue(&usesCamera, type: .bool, index: 0)

    let descriptor = MTLRenderPipelineDescriptor()
    descriptor.label = camera ? "Dihedra kaleidoscope, camera" : "Dihedra kaleidoscope, glass"
    descriptor.vertexFunction = library.makeFunction(name: "dh_vertex")
    descriptor.fragmentFunction = try library.makeFunction(name: "dh_fragment", constantValues: constants)
    descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
    return try device.makeRenderPipelineState(descriptor: descriptor)
  }

  /// Fades toward the camera while it delivers fresh images, and back to the
  /// glass when it stops, is switched off or is interrupted.
  private func updateCameraMix(now: CFTimeInterval) {
    let dt = Float(min(now - lastDrawTime, 0.1))
    lastDrawTime = now
    var target: Float = 0
    if camera.isActive, let frame = camera.latestFrame(), now - frame.time < Self.staleFrameAge {
      cameraFrame = frame
      target = 1
    }
    let step = dt / Self.sourceFade
    cameraMix = target > cameraMix ? min(target, cameraMix + step) : max(target, cameraMix - step)
    if cameraMix == 0 { cameraFrame = nil }
  }
}

extension Renderer: MTKViewDelegate {
  func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

  func draw(in view: MTKView) {
    let now = CACurrentMediaTime()
    // Tick the core every frame, even if this frame can't draw: the detent
    // dwell timer and the hinge filter need time to pass.
    let frame = core.frame(at: now)
    let committed = Int(frame.kTarget.rounded())
    if committed != symmetry {
      symmetry = committed
      onSymmetryChange(committed)
    }
    updateCameraMix(now: now)

    guard let pass = view.currentRenderPassDescriptor,
      let drawable = view.currentDrawable,
      let commands = queue.makeCommandBuffer(),
      let encoder = commands.makeRenderCommandEncoder(descriptor: pass)
    else { return }

    var uniforms = DHUniforms(
      resolutionX: Float(view.drawableSize.width),
      resolutionY: Float(view.drawableSize.height),
      time: Float(now - startTime),
      k: frame.k,
      fine: frame.fine,
      energy: 0,
      sourceMix: cameraMix,
      sourceAngle: camera.levelAngle)

    if let cameraFrame {
      encoder.setRenderPipelineState(cameraPipeline)
      encoder.setFragmentTexture(cameraFrame.texture, index: 0)
      // The texture is only valid while its frame is held, so hold it until
      // the GPU is done with it.
      commands.addCompletedHandler { @Sendable _ in withExtendedLifetime(cameraFrame) {} }
    } else {
      encoder.setRenderPipelineState(glassPipeline)
    }
    encoder.setFragmentBytes(&uniforms, length: MemoryLayout<DHUniforms>.stride, index: 0)
    encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
    encoder.endEncoding()
    commands.present(drawable)
    commands.commit()
  }
}

import MetalKit
import QuartzCore

/// Draws the kaleidoscope. Runs on the main thread (MTKView's default), which
/// is also where hinge updates arrive, so the core's mapper stays single-threaded.
final class Renderer: NSObject {
  private let core: DHCore
  private let queue: MTLCommandQueue
  private let pipeline: MTLRenderPipelineState
  private let startTime = CACurrentMediaTime()
  private let onSymmetryChange: (Int) -> Void
  private var symmetry = 0

  init?(view: MTKView, core: DHCore, onSymmetryChange: @escaping (Int) -> Void) {
    guard let device = view.device,
      let queue = device.makeCommandQueue(),
      let library = device.makeDefaultLibrary()
    else { return nil }

    let descriptor = MTLRenderPipelineDescriptor()
    descriptor.label = "Dihedra kaleidoscope"
    descriptor.vertexFunction = library.makeFunction(name: "dh_vertex")
    descriptor.fragmentFunction = library.makeFunction(name: "dh_fragment")
    descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat

    do {
      pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
    } catch {
      print("Dihedra: pipeline creation failed: \(error)")
      return nil
    }
    self.queue = queue
    self.core = core
    self.onSymmetryChange = onSymmetryChange
    super.init()
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
      energy: 0)

    encoder.setRenderPipelineState(pipeline)
    encoder.setFragmentBytes(&uniforms, length: MemoryLayout<DHUniforms>.stride, index: 0)
    encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
    encoder.endEncoding()
    commands.present(drawable)
    commands.commit()
  }
}

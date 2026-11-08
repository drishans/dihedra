import MetalKit
import SwiftUI

/// SwiftUI wrapper for the Metal view.
struct KaleidoView: UIViewRepresentable {
  let core: DHCore
  /// Pause drawing whenever the scene isn't active. Apps can't submit GPU
  /// work from the background, and audio doesn't depend on frames.
  let paused: Bool

  final class Coordinator {
    var renderer: Renderer?
  }

  func makeCoordinator() -> Coordinator { Coordinator() }

  func makeUIView(context: Context) -> MTKView {
    let view = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
    view.colorPixelFormat = .bgra8Unorm
    view.framebufferOnly = true
    view.isOpaque = true
    view.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
    // 120 Hz needs CADisableMinimumFrameDurationOnPhone in Info.plist.
    view.preferredFramesPerSecond = 120
    view.autoResizeDrawable = true

    let renderer = Renderer(view: view, core: core)
    context.coordinator.renderer = renderer  // MTKView.delegate is weak
    view.delegate = renderer
    view.isPaused = paused
    return view
  }

  func updateUIView(_ view: MTKView, context: Context) {
    view.isPaused = paused
  }
}

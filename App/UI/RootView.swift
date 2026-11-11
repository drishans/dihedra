import SwiftUI

/// The kaleidoscope plus its controls. Lives inside a NavigationStack so the
/// toolbar can join the system's vertical bar on iPhone Duo, under the status
/// bar, and fall back to a horizontal bar elsewhere.
struct RootView: View {
  @Environment(AppModel.self) private var model
  @Environment(\.scenePhase) private var scenePhase
  /// Set when toolbar items can sit in a vertical bar, nil when bars are horizontal.
  @Environment(\.toolbarVerticalEdge) private var verticalBarEdge
  @Environment(\.accessibilityVoiceOverEnabled) private var voiceOverEnabled
  @State private var showSettings = false
  @State private var chromeVisible = true
  @State private var chromeTask: Task<Void, Never>?

  var body: some View {
    @Bindable var model = model

    ZStack {
      KaleidoView(core: model.core, camera: model.camera, paused: scenePhase != .active) {
        model.symmetry = $0
      }
        .ignoresSafeArea()
        .contentShape(Rectangle())
        .onTapGesture { revealChrome() }
        .accessibilityLabel("Kaleidoscope")
        .accessibilityHint("Fold the phone to change the symmetry and the harmony.")

      if model.usesVirtualHinge {
        FoldFallbackControl(angle: $model.virtualAngle)
          .frame(maxHeight: .infinity, alignment: .bottom)
          .transition(.opacity)
      }
    }
    .toolbar {
      ToolbarItemGroup(placement: .topBarTrailing) {
        Toggle(isOn: $model.isHeld) {
          Label("Hold", systemImage: "\(model.symmetry).square")
        }
        .accessibilityValue("Symmetry \(model.symmetry)")
        .accessibilityHint("Keeps the picture and the sound as they are while you fold.")

        Toggle(isOn: $model.isMuted) {
          Label("Mute", systemImage: model.isMuted ? "speaker.slash" : "speaker.wave.2")
        }
      }
      // Settings sits at the bottom of a vertical bar, below a system gap.
      // In a horizontal bar it joins the others rather than opening a
      // bottom bar of its own.
      ToolbarItem(placement: verticalBarEdge == nil ? .topBarTrailing : .bottomBar) {
        Button {
          showSettings = true
        } label: {
          Label("Settings", systemImage: "gearshape")
        }
      }
    }
    .toggleStyle(.button)
    .toolbarVisibility(
      chromeVisible ? .visible : .hidden, for: .navigationBar, .bottomBar, .statusBar)
    .animation(.easeInOut(duration: 0.4), value: chromeVisible)
    .animation(.easeInOut(duration: 0.4), value: model.usesVirtualHinge)
    // Hinge data drives the effect; layout is left to the system.
    .onHingeChange { _, context in
      model.hingeChanged(context)
    }
    .onChange(of: scenePhase, initial: true) { _, phase in
      model.scenePhaseChanged(phase)
    }
    .onChange(of: model.isHeld) { revealChrome() }
    .onChange(of: model.isMuted) { revealChrome() }
    .onChange(of: voiceOverEnabled) { revealChrome() }
    .sheet(isPresented: $showSettings, onDismiss: revealChrome) {
      SettingsView()
        .environment(model)
    }
    .persistentSystemOverlays(.hidden)
    .task {
      model.start()
      revealChrome()
    }
  }

  /// Show the controls for a few seconds, then let the kaleidoscope have the
  /// screen. With VoiceOver on they stay, since there's nothing to see to tap.
  private func revealChrome() {
    chromeVisible = true
    chromeTask?.cancel()
    guard !voiceOverEnabled else { return }
    chromeTask = Task {
      try? await Task.sleep(for: .seconds(3))
      guard !Task.isCancelled, !showSettings else { return }
      chromeVisible = false
    }
  }
}

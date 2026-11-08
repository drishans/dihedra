import SwiftUI

struct RootView: View {
  @Environment(AppModel.self) private var model
  @Environment(\.scenePhase) private var scenePhase
  @State private var showSettings = false
  @State private var chromeVisible = true
  @State private var chromeTask: Task<Void, Never>?

  var body: some View {
    @Bindable var model = model

    ZStack {
      KaleidoView(core: model.core, paused: scenePhase != .active)
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

      if chromeVisible {
        Button {
          showSettings = true
        } label: {
          Image(systemName: "gearshape")
            .font(.title3)
            .padding(14)
            .contentShape(Circle())
        }
        .buttonStyle(.plain)
        .foregroundStyle(.white.opacity(0.75))
        .accessibilityLabel("Settings")
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
        .padding(8)
        .transition(.opacity)
      }
    }
    .animation(.easeInOut(duration: 0.4), value: chromeVisible)
    .animation(.easeInOut(duration: 0.4), value: model.usesVirtualHinge)
    // Hinge data drives the effect; layout is left to the system.
    .onHingeChange { _, context in
      model.hingeChanged(context)
    }
    .onChange(of: scenePhase, initial: true) { _, phase in
      model.scenePhaseChanged(phase)
    }
    .sheet(isPresented: $showSettings, onDismiss: revealChrome) {
      SettingsView()
        .environment(model)
    }
    .statusBarHidden()
    .persistentSystemOverlays(.hidden)
    .task {
      model.start()
      revealChrome()
    }
  }

  /// Show the gear for a few seconds, then let the kaleidoscope have the screen.
  private func revealChrome() {
    chromeVisible = true
    chromeTask?.cancel()
    chromeTask = Task {
      try? await Task.sleep(for: .seconds(3))
      guard !Task.isCancelled, !showSettings else { return }
      chromeVisible = false
    }
  }
}

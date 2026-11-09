import SwiftUI

@main
struct DihedraApp: App {
  @State private var model = AppModel()

  var body: some Scene {
    WindowGroup {
      NavigationStack {
        RootView()
      }
      .environment(model)
      .preferredColorScheme(.dark)
    }
  }
}

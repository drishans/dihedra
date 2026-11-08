import SwiftUI

struct SettingsView: View {
  @Environment(AppModel.self) private var model
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    @Bindable var model = model

    NavigationStack {
      Form {
        Section {
          Picker("Sound", selection: $model.soundMode) {
            ForEach(SoundMode.allCases) { mode in
              Text(mode.title).tag(mode)
            }
          }
          .pickerStyle(.inline)
          .labelsHidden()
        } header: {
          Text("Sound")
        } footer: {
          Text(model.soundMode.detail)
        }

        #if DEBUG
          Section("Debug") {
            Toggle("Simulate Hinge", isOn: $model.simulateHinge)
            Toggle("Detents", isOn: $model.detents)
          }
        #endif
      }
      .navigationTitle("Dihedra")
      .navigationBarTitleDisplayMode(.inline)
      .toolbar {
        ToolbarItem(placement: .confirmationAction) {
          Button("Done") { dismiss() }
        }
      }
    }
    .presentationDetents([.medium, .large])
  }
}

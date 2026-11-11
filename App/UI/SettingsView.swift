import SwiftUI

struct SettingsView: View {
  @Environment(AppModel.self) private var model
  @Environment(\.dismiss) private var dismiss
  @Environment(\.openURL) private var openURL

  var body: some View {
    @Bindable var model = model

    NavigationStack {
      Form {
        Section {
          Picker("Picture", selection: $model.pictureSource) {
            ForEach(PictureSource.allCases) { source in
              Text(source.title).tag(source)
            }
          }
          .pickerStyle(.inline)
          .labelsHidden()

          if model.pictureSource == .camera {
            Picker("Camera", selection: $model.cameraFacing) {
              ForEach(CameraFacing.allCases) { facing in
                Text(facing.title).tag(facing)
              }
            }
            .pickerStyle(.segmented)

            if model.camera.access == .denied {
              Button("Open Settings") {
                if let url = URL(string: UIApplication.openSettingsURLString) { openURL(url) }
              }
            }
          }
        } header: {
          Text("Picture")
        } footer: {
          Text(pictureFooter)
        }

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

        Section {
          NavigationLink("Acknowledgements") { AcknowledgementsView() }
        }
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

  private var pictureFooter: String {
    guard model.pictureSource == .camera else {
      return "Stained glass drifts behind the mirrors."
    }
    if model.camera.access == .denied {
      return "Camera access is off. Turn it on in Settings to see the camera through the mirrors."
    }
    if !model.camera.hasCamera {
      return "No camera faces that way right now."
    }
    return "What the camera sees, folded through the mirrors. It stays on your phone and is never recorded."
  }
}

import Foundation

/// What sits behind the mirrors.
enum PictureSource: String, CaseIterable, Identifiable {
  case glass
  case camera

  var id: String { rawValue }

  var title: String {
    switch self {
    case .glass: "Glass"
    case .camera: "Camera"
    }
  }

  private static let defaultsKey = "pictureSource"

  static var stored: PictureSource {
    UserDefaults.standard.string(forKey: defaultsKey).flatMap(PictureSource.init(rawValue:)) ?? .glass
  }

  func store() {
    UserDefaults.standard.set(rawValue, forKey: Self.defaultsKey)
  }
}

/// Which way the camera looks, judged from the screen in use. On iPhone Duo
/// the same camera can face you in one posture and the scene in another.
enum CameraFacing: String, CaseIterable, Identifiable {
  case away
  case toward

  var id: String { rawValue }

  var title: String {
    switch self {
    case .away: "Faces Away"
    case .toward: "Faces You"
    }
  }

  private static let defaultsKey = "cameraFacing"

  static var stored: CameraFacing {
    UserDefaults.standard.string(forKey: defaultsKey).flatMap(CameraFacing.init(rawValue:)) ?? .away
  }

  func store() {
    UserDefaults.standard.set(rawValue, forKey: Self.defaultsKey)
  }
}

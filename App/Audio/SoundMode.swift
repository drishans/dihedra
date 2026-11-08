import Foundation

/// How Dihedra treats the Silent switch and the background.
///
/// iOS can't do "respect the Silent switch" and "keep playing when locked"
/// at once: the ambient category obeys the switch but is also silenced by
/// screen lock, and background playback needs the playback category, which
/// ignores the switch. There's no public API to read the switch, so the
/// setting is one choice of three.
enum SoundMode: String, CaseIterable, Identifiable {
  case respectSilentMode
  case alwaysWhileOpen
  case alwaysIncludingBackground

  var id: String { rawValue }

  var title: String {
    switch self {
    case .respectSilentMode: "Respect Silent Mode"
    case .alwaysWhileOpen: "Always Play While Open"
    case .alwaysIncludingBackground: "Keep Playing When Locked"
    }
  }

  var detail: String {
    switch self {
    case .respectSilentMode:
      "Quiet when Silent Mode is on. Mixes with other audio and stops when you lock the phone."
    case .alwaysWhileOpen:
      "Plays even in Silent Mode, and stops when you leave the app or lock the phone."
    case .alwaysIncludingBackground:
      "Plays even in Silent Mode, and keeps playing in the background and when locked."
    }
  }

  private static let defaultsKey = "soundMode"

  static var stored: SoundMode {
    UserDefaults.standard.string(forKey: defaultsKey).flatMap(SoundMode.init(rawValue:))
      ?? .respectSilentMode
  }

  func store() {
    UserDefaults.standard.set(rawValue, forKey: Self.defaultsKey)
  }
}

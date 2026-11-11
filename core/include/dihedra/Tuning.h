// Dihedra: tuning constants shared by the audio and visual paths.
#pragma once

namespace dihedra::tuning {

// Hinge range. 180 degrees is flat. From about 85 degrees iOS reports the
// phone closed and moves the app to the cover screen (it flickers between
// the screens there), so the symmetry range is stretched across
// [kSteep, kFlat], which keeps every k on the inner screen with a margin.
inline constexpr float kFlatDegrees = 180.0f;
inline constexpr float kSteepDegrees = 95.0f;
// Below this the phone counts as closed even if the status lags. iOS has
// reported closed as late as 66 degrees.
inline constexpr float kClosedDegrees = 60.0f;

// Symmetry k = 180 / mirror angle. k = 1 is a single mirror.
inline constexpr int kMinK = 1;
inline constexpr int kMaxK = 8;

// Detents. One unit is one band, about 11 degrees of hinge travel.
inline constexpr float kHysteresis = 0.2f;     // about 2 degrees
inline constexpr double kDwellSeconds = 0.2;   // hold this long before committing
inline constexpr float kRestSpeed = 12.0f;     // deg/s; slower than this counts as resting

// One Euro filter on the raw hinge angle (degrees).
inline constexpr float kFilterMinCutoff = 1.0f;  // Hz
inline constexpr float kFilterBeta = 0.02f;
inline constexpr float kFilterDCutoff = 1.0f;    // Hz

// Critically damped springs (rad/s). Audio and visuals use the same values
// so the picture and the sound glide together.
inline constexpr float kSymmetryOmega = 7.0f;  // about 0.7 s between detents
inline constexpr float kFineOmega = 14.0f;

// Output gain envelope.
inline constexpr float kGainAttackSeconds = 0.8f;
inline constexpr float kGainReleaseSeconds = 0.03f;

// Output high-pass. At k near 1 the FM puts a sideband at (k - 1) times the
// root, which is DC at k = 1 (it thumped on mute) and sweeps down through the
// bass while gliding there. 20 Hz removes the DC and the part below hearing
// and keeps the audible sweep.
inline constexpr float kHighPassHz = 20.0f;

// Drone.
inline constexpr float kRootHz = 146.83f;   // D3
inline constexpr float kDetune = 0.0012f;   // about 2 cents each side
inline constexpr float kOutputLevel = 0.22f;
inline constexpr float kBreathHz = 0.07f;
// FM feedback, from -1 (rough: the output bends the modulator's frequency)
// through 0 (none) to 1 (brighter: it bends the modulator's phase).
inline constexpr float kFeedback = 0.0f;
// The voice an octave below the carrier, relative to it.
inline constexpr float kSubLevel = 0.5f;

}  // namespace dihedra::tuning

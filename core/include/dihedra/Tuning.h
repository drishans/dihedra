// Dihedra: tuning constants shared by the audio and visual paths.
#pragma once

namespace dihedra::tuning {

// Hinge range. 180 degrees is flat; below kSteepDegrees the inner screen is
// hard to see, so the symmetry range is stretched across [kSteep, kFlat].
inline constexpr float kFlatDegrees = 180.0f;
inline constexpr float kSteepDegrees = 60.0f;
// Below this the phone counts as closed even if the status lags.
inline constexpr float kClosedDegrees = 25.0f;

// Symmetry k = 180 / mirror angle. k = 1 is a single mirror.
inline constexpr int kMinK = 1;
inline constexpr int kMaxK = 8;

// Detents. One unit is one band, about 16 degrees of hinge travel.
inline constexpr float kHysteresis = 0.2f;     // about 3 degrees
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

// Output high-pass. At k near 1 the FM puts a sideband at (1 - k) times the
// root, which is DC at k = 1 and a sub-bass sweep while gliding there. Phone
// speakers crackle trying to play it. 60 Hz is well under the 147 Hz root.
inline constexpr float kHighPassHz = 60.0f;

// Phase 1 drone.
inline constexpr float kRootHz = 146.83f;   // D3
inline constexpr float kDetune = 0.0012f;   // about 2 cents each side
inline constexpr float kOutputLevel = 0.22f;
inline constexpr float kBreathHz = 0.07f;

}  // namespace dihedra::tuning

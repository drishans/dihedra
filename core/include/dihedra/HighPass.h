// Dihedra: fourth-order Butterworth high-pass.
#pragma once

#include <cmath>

namespace dihedra {

// Two state-variable sections (the trapezoidal form from Andrew Simper's
// "Linear Trapezoidal Integrated SVF"), which stays accurate in float at
// cutoffs far below the sample rate, where a direct-form biquad doesn't.
// init() calls tan, so do it off the audio thread; process() is plain math.
class HighPass {
 public:
  void init(float cutoffHz, float sampleRate) {
    // Q of each section for a fourth-order Butterworth response.
    constexpr float kQ[2] = {0.54119610f, 1.30656296f};
    const float g = std::tan(3.14159265f * cutoffHz / sampleRate);
    for (int i = 0; i < 2; ++i) {
      Section& s = sections_[i];
      s.k = 1.0f / kQ[i];
      s.a1 = 1.0f / (1.0f + g * (g + s.k));
      s.a2 = g * s.a1;
      s.a3 = g * s.a2;
      s.ic1 = s.ic2 = 0.0f;
    }
  }

  float process(float x) {
    for (Section& s : sections_) x = s.process(x);
    return x;
  }

 private:
  struct Section {
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;

    float process(float v0) {
      const float v3 = v0 - ic2;
      const float v1 = a1 * ic1 + a2 * v3;
      const float v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = 2.0f * v1 - ic1;
      ic2 = 2.0f * v2 - ic2;
      return v0 - k * v1 - v2;
    }
  };

  Section sections_[2];
};

}  // namespace dihedra

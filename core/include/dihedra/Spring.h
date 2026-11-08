// Dihedra: critically damped spring with an exact discrete step.
#pragma once

#include <cmath>

namespace dihedra {

// x(t) = T + (y0 + (v0 + w*y0) t) e^(-w t), with y0 = x0 - T.
// The step is the closed-form solution, so it is stable for any dt and
// one step of 2*dt equals two steps of dt. That keeps the audio path
// (12-sample blocks) and the render path (one step per frame) in agreement.
struct Spring {
  float value = 0.0f;
  float velocity = 0.0f;
  float omega = 8.0f;

  void reset(float v) {
    value = v;
    velocity = 0.0f;
  }

  float step(float target, float dt) {
    const float y = value - target;
    const float e = std::exp(-omega * dt);
    const float c = velocity + omega * y;
    value = target + (y + c * dt) * e;
    velocity = (velocity - omega * c * dt) * e;
    return value;
  }
};

}  // namespace dihedra

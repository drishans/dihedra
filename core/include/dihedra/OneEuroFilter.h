// Dihedra: One Euro filter (Casiez, Roussel, Vogel, CHI 2012).
// Low jitter at rest, low lag when moving, and it uses real timestamps,
// which matters because hinge updates arrive at no guaranteed rate.
#pragma once

#include <cmath>

namespace dihedra {

struct OneEuroConfig {
  float minCutoff = 1.0f;  // Hz
  float beta = 0.02f;
  float dCutoff = 1.0f;    // Hz
};

class OneEuroFilter {
 public:
  explicit OneEuroFilter(OneEuroConfig config = {}) : config_(config) {}

  void reset() { initialized_ = false; }
  bool initialized() const { return initialized_; }
  float value() const { return x_; }
  float derivative() const { return dx_; }  // units per second

  float filter(float x, double t) {
    if (!initialized_) {
      initialized_ = true;
      x_ = x;
      dx_ = 0.0f;
      t_ = t;
      return x_;
    }
    const double dtd = t - t_;
    if (!(dtd > 0.0)) return x_;  // duplicate or out-of-order timestamp
    t_ = t;
    const float dt = static_cast<float>(dtd);
    const float rawDx = (x - x_) / dt;
    dx_ += alpha(config_.dCutoff, dt) * (rawDx - dx_);
    const float cutoff = config_.minCutoff + config_.beta * std::fabs(dx_);
    x_ += alpha(cutoff, dt) * (x - x_);
    return x_;
  }

 private:
  static float alpha(float cutoff, float dt) {
    constexpr float kTwoPi = 6.28318530718f;
    const float tau = 1.0f / (kTwoPi * cutoff);
    return 1.0f / (1.0f + tau / dt);
  }

  OneEuroConfig config_;
  bool initialized_ = false;
  float x_ = 0.0f;
  float dx_ = 0.0f;
  double t_ = 0.0;
};

}  // namespace dihedra

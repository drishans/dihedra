// Dihedra: real-time audio engine.
//
// render() runs on the audio thread. It never allocates, locks, or calls
// into Objective-C or Swift. Control values arrive through relaxed atomics
// written on the main thread.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

#include "dihedra/HighPass.h"
#include "dihedra/Spring.h"
#include "dihedra/Tuning.h"
#include "dihedra/eurorack/FMEngine.h"

namespace dihedra {

struct SharedControls {
  std::atomic<float> kTarget{1.0f};
  std::atomic<float> fineTarget{0.0f};
  std::atomic<float> gainTarget{0.0f};
};
static_assert(std::atomic<float>::is_always_lock_free, "atomic<float> must be lock-free");

// The voice: a stereo pair of the eurorack two-operator FM engine, detuned
// slightly for width, each with its voice an octave down mixed in. The
// modulator runs at k times the carrier, so the spectrum is harmonic exactly
// when k is a whole number, the same condition under which the kaleidoscope
// closes without a seam.
class FMDrone {
 public:
  // The most samples one render() call takes, as in Plaits.
  static constexpr size_t kMaxFrames = 24;

  void init(float sampleRate) {
    sampleRate_ = sampleRate;
    for (eurorack::FMEngine& voice : voices_) voice.init();
    breathPhase_ = 0.0f;
  }

  // Renders n samples (at most kMaxFrames). The voices glide to k and fine
  // across the block, and the gain eases toward its target sample by sample.
  void render(float* left, float* right, size_t n, float k, float fine, float* gain,
              float gainTarget, float gainAttack, float gainRelease) {
    constexpr float kTwoPi = 6.28318530718f;
    const float detune[2] = {1.0f - tuning::kDetune, 1.0f + tuning::kDetune};
    const float coeff = gainTarget > *gain ? gainAttack : gainRelease;

    breathPhase_ += tuning::kBreathHz * static_cast<float>(n) / sampleRate_;
    if (breathPhase_ >= 1.0f) breathPhase_ -= 1.0f;
    const float breath = 1.0f + 0.2f * std::sin(kTwoPi * breathPhase_);

    // FM depth in turns, 1.8 radians at k = 1. A lower depth for higher
    // ratios keeps the brightness roughly even.
    const float amount =
        (0.2865f / std::sqrt(std::max(k, 1.0f))) * (1.0f + 0.45f * fine) * breath;

    // Frequencies in turns per oversampled step.
    const float step = 1.0f / (static_cast<float>(eurorack::kOversampling) * sampleRate_);
    float carrier[2][kMaxFrames];
    float sub[2][kMaxFrames];
    for (int ch = 0; ch < 2; ++ch) {
      const float fc = tuning::kRootHz * detune[ch] * step;
      voices_[ch].render(fc, fc * k, amount, tuning::kFeedback, carrier[ch], sub[ch], n);
    }

    for (size_t i = 0; i < n; ++i) {
      *gain += coeff * (gainTarget - *gain);
      const float level = *gain * tuning::kOutputLevel;
      float out[2];
      for (int ch = 0; ch < 2; ++ch) {
        out[ch] = (carrier[ch][i] + tuning::kSubLevel * sub[ch][i]) * level;
      }
      if (right) {
        left[i] = out[0];
        right[i] = out[1];
      } else {
        left[i] = 0.5f * (out[0] + out[1]);
      }
    }
  }

 private:
  // Upstream lowers the FM depth for modulators above about 2 kHz to limit
  // aliasing. Dihedra's stay below that, so the taming is left out.
  static_assert(tuning::kRootHz * (1.0f + tuning::kDetune) * tuning::kMaxK < 2000.0f);

  float sampleRate_ = 48000.0f;
  eurorack::FMEngine voices_[2];
  float breathPhase_ = 0.0f;
};

class Engine {
 public:
  // Like Plaits: 48 kHz, in 12-sample blocks.
  static constexpr float kSampleRate = 48000.0f;
  static constexpr size_t kBlockSize = 12;
  static_assert(kBlockSize <= FMDrone::kMaxFrames);

  Engine() {
    kSpring_.omega = tuning::kSymmetryOmega;
    fineSpring_.omega = tuning::kFineOmega;
    kSpring_.reset(1.0f);
    fineSpring_.reset(0.0f);
    drone_.init(kSampleRate);
    for (HighPass& hp : highPass_) hp.init(tuning::kHighPassHz, kSampleRate);
    gainAttack_ = 1.0f - std::exp(-1.0f / (tuning::kGainAttackSeconds * kSampleRate));
    gainRelease_ = 1.0f - std::exp(-1.0f / (tuning::kGainReleaseSeconds * kSampleRate));
  }

  SharedControls& controls() { return controls_; }

  // Audio thread. right may be null for a mono buffer.
  void render(float* left, float* right, size_t frames) noexcept {
    size_t done = 0;
    while (done < frames) {
      const size_t n = std::min(kBlockSize, frames - done);
      const float dt = static_cast<float>(n) / kSampleRate;
      const float k = kSpring_.step(controls_.kTarget.load(std::memory_order_relaxed), dt);
      const float fine = fineSpring_.step(controls_.fineTarget.load(std::memory_order_relaxed), dt);
      const float gainTarget = controls_.gainTarget.load(std::memory_order_relaxed);
      float* l = left + done;
      float* r = right ? right + done : nullptr;
      drone_.render(l, r, n, k, fine, &gain_, gainTarget, gainAttack_, gainRelease_);
      for (size_t i = 0; i < n; ++i) l[i] = highPass_[0].process(l[i]);
      if (r) {
        for (size_t i = 0; i < n; ++i) r[i] = highPass_[1].process(r[i]);
      }
      done += n;
    }
  }

  // For tests.
  float currentK() const { return kSpring_.value; }
  float currentGain() const { return gain_; }

 private:
  SharedControls controls_;
  Spring kSpring_;
  Spring fineSpring_;
  FMDrone drone_;
  HighPass highPass_[2];
  float gain_ = 0.0f;
  float gainAttack_ = 0.0f;
  float gainRelease_ = 0.0f;
};

}  // namespace dihedra

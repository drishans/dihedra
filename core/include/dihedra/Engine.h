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

namespace dihedra {

struct SharedControls {
  std::atomic<float> kTarget{1.0f};
  std::atomic<float> fineTarget{0.0f};
  std::atomic<float> gainTarget{0.0f};
};
static_assert(std::atomic<float>::is_always_lock_free, "atomic<float> must be lock-free");

// Phase 1 voice: a stereo pair of two-operator phase-modulation voices,
// detuned slightly for width. The modulator runs at k times the carrier,
// so the spectrum is harmonic exactly when k is a whole number, the same
// condition under which the kaleidoscope closes without a seam.
class FMDrone {
 public:
  void init(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int ch = 0; ch < 2; ++ch) {
      carrierPhase_[ch] = 0.0f;
      modPhase_[ch] = 0.25f * static_cast<float>(ch);
    }
    breathPhase_ = 0.0f;
  }

  // Renders n samples, interpolating k, fine and gain linearly across the block.
  void render(float* left, float* right, size_t n, float k0, float k1, float fine0,
              float fine1, float* gain, float gainTarget, float gainAttack,
              float gainRelease) {
    constexpr float kTwoPi = 6.28318530718f;
    const float invN = 1.0f / static_cast<float>(n);
    const float invSr = 1.0f / sampleRate_;
    const float detune[2] = {1.0f - tuning::kDetune, 1.0f + tuning::kDetune};
    const float coeff = gainTarget > *gain ? gainAttack : gainRelease;

    for (size_t i = 0; i < n; ++i) {
      const float a = static_cast<float>(i + 1) * invN;
      const float k = k0 + (k1 - k0) * a;
      const float fine = fine0 + (fine1 - fine0) * a;

      breathPhase_ += tuning::kBreathHz * invSr;
      if (breathPhase_ >= 1.0f) breathPhase_ -= 1.0f;
      const float breath = 1.0f + 0.2f * std::sin(kTwoPi * breathPhase_);

      // Lower index for higher ratios keeps the brightness roughly even.
      const float index = (1.8f / std::sqrt(std::max(k, 1.0f))) * (1.0f + 0.45f * fine) * breath;

      *gain += coeff * (gainTarget - *gain);
      const float level = *gain * tuning::kOutputLevel;

      float out[2];
      for (int ch = 0; ch < 2; ++ch) {
        const float fc = tuning::kRootHz * detune[ch];
        carrierPhase_[ch] += fc * invSr;
        modPhase_[ch] += fc * k * invSr;
        carrierPhase_[ch] -= std::floor(carrierPhase_[ch]);
        modPhase_[ch] -= std::floor(modPhase_[ch]);
        const float mod = std::sin(kTwoPi * modPhase_[ch]);
        out[ch] = std::sin(kTwoPi * carrierPhase_[ch] + index * mod) * level;
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
  float sampleRate_ = 48000.0f;
  float carrierPhase_[2] = {0.0f, 0.0f};
  float modPhase_[2] = {0.0f, 0.0f};
  float breathPhase_ = 0.0f;
};

class Engine {
 public:
  // Plaits and Rings both run at 48 kHz, and Plaits renders 12-sample blocks.
  // Phase 1 uses the same so the eurorack engines drop in later.
  static constexpr float kSampleRate = 48000.0f;
  static constexpr size_t kBlockSize = 12;

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
      const float k0 = kSpring_.value;
      const float f0 = fineSpring_.value;
      const float k1 = kSpring_.step(controls_.kTarget.load(std::memory_order_relaxed), dt);
      const float f1 = fineSpring_.step(controls_.fineTarget.load(std::memory_order_relaxed), dt);
      const float gainTarget = controls_.gainTarget.load(std::memory_order_relaxed);
      float* l = left + done;
      float* r = right ? right + done : nullptr;
      drone_.render(l, r, n, k0, k1, f0, f1, &gain_, gainTarget, gainAttack_, gainRelease_);
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

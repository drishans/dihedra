// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Classic 2-op FM found in Braids, Rings and Elements.
//
// Ported to Dihedra from pichenettes/eurorack at f4cfa76 (stmlib at e3bd7c9):
// FMEngine::Render from plaits/dsp/engine/fm_engine.cc, with SinePM from
// plaits/dsp/oscillator/sine_oscillator.h, Downsampler from
// plaits/dsp/downsampler/4x_downsampler.h, ParameterInterpolator from
// stmlib/dsp/parameter_interpolator.h, and the sine and FIR tables from
// plaits/resources/lookup_tables.py.
//
// The synthesis is unchanged. Upstream derives the frequencies, FM amount and
// feedback from note, harmonics, timbre and morph, and quantizes the ratio;
// here render() takes them directly, so the ratio can sit between whole
// numbers. Header-only, without stmlib. Changes are under the same license.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace dihedra::eurorack {

inline constexpr size_t kOversampling = 4;

inline constexpr uint32_t kSineLUTBits = 9;
inline constexpr size_t kSineLUTSize = size_t{1} << kSineLUTBits;

// sin(2 pi t) for t from 0 to 1 in steps of 1/512, plus the point past the
// end that interpolation reads. Upstream stores a further quarter turn for
// cosines, which nothing here needs. Built once at startup rather than
// copied from upstream's printed table, so 4 of the 513 values differ from
// it in the last bit; with that table the port renders bit for bit the same.
inline const std::array<float, kSineLUTSize + 1> kSineTable = [] {
  std::array<float, kSineLUTSize + 1> table{};
  for (size_t i = 0; i < table.size(); ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(kSineLUTSize);
    table[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * t));
  }
  return table;
}();

inline constexpr float kDownsamplerFIR[4] = {0.02442415f, 0.09297315f, 0.16712938f,
                                             0.21547332f};

// Sine of a phase (a full turn is 2^32) advanced by pm turns, for pm within
// plus or minus 32.
inline float sinePM(uint32_t phase, float pm) {
  constexpr float kMaxUint32 = 4294967296.0f;
  constexpr uint32_t kMaxIndex = 32;
  constexpr float kOffset = static_cast<float>(kMaxIndex);
  constexpr float kScale = kMaxUint32 / static_cast<float>(kMaxIndex * 2);

  phase += static_cast<uint32_t>((pm + kOffset) * kScale) * (kMaxIndex * 2);

  const uint32_t integral = phase >> (32 - kSineLUTBits);
  const float fractional = static_cast<float>(phase << kSineLUTBits) / kMaxUint32;
  const float a = kSineTable[integral];
  const float b = kSineTable[integral + 1];
  return a + (b - a) * fractional;
}

// Ramps from *state to a new value across a block, and stores where it got to.
class ParameterInterpolator {
 public:
  ParameterInterpolator(float* state, float newValue, size_t size)
      : state_(state),
        value_(*state),
        increment_((newValue - *state) / static_cast<float>(size)) {}
  ~ParameterInterpolator() { *state_ = value_; }
  ParameterInterpolator(const ParameterInterpolator&) = delete;
  ParameterInterpolator& operator=(const ParameterInterpolator&) = delete;

  float next() {
    value_ += increment_;
    return value_;
  }

 private:
  float* state_;
  float value_;
  float increment_;
};

// Low-pass filters kOversampling steps down to one sample. The FIR spans two
// output samples, so one float of state carries over between blocks.
class Downsampler {
 public:
  explicit Downsampler(float* state) : head_(*state), state_(state) {}
  ~Downsampler() { *state_ = head_; }
  Downsampler(const Downsampler&) = delete;
  Downsampler& operator=(const Downsampler&) = delete;

  void accumulate(size_t i, float sample) {
    head_ += sample * kDownsamplerFIR[3 - (i & 3)];
    tail_ += sample * kDownsamplerFIR[i & 3];
  }

  float read() {
    const float value = head_;
    head_ = tail_;
    tail_ = 0.0f;
    return value;
  }

 private:
  float head_;
  float tail_ = 0.0f;
  float* state_;
};

class FMEngine {
 public:
  void init() { *this = FMEngine{}; }

  // Renders size samples of the carrier into out and of a voice an octave
  // below it into aux. Frequencies are in turns per oversampled step, that
  // is hertz / (kOversampling * sample rate). amount is the carrier's phase
  // modulation depth in turns. feedback runs from -1 to 1: below 0 the output
  // bends the modulator's frequency, above 0 its phase. All four glide there
  // from the previous block's values.
  void render(float carrierFrequency, float modulatorFrequency, float amount, float feedback,
              float* out, float* aux, size_t size) {
    constexpr float kMaxUint32 = 4294967296.0f;

    ParameterInterpolator carrierFrequencyRamp(&previousCarrierFrequency_, carrierFrequency,
                                               size);
    ParameterInterpolator modulatorFrequencyRamp(&previousModulatorFrequency_,
                                                 std::clamp(modulatorFrequency, 0.0f, 0.5f),
                                                 size);
    ParameterInterpolator amountRamp(&previousAmount_, amount, size);
    ParameterInterpolator feedbackRamp(&previousFeedback_, feedback, size);

    Downsampler carrierDownsampler(&carrierFir_);
    Downsampler subDownsampler(&subFir_);

    while (size--) {
      const float depth = amountRamp.next();
      const float fb = feedbackRamp.next();
      const float phaseFeedback = fb < 0.0f ? 0.5f * fb * fb : 0.0f;
      const uint32_t carrierIncrement =
          static_cast<uint32_t>(kMaxUint32 * carrierFrequencyRamp.next());
      const float modulatorStep = modulatorFrequencyRamp.next();

      for (size_t j = 0; j < kOversampling; ++j) {
        modulatorPhase_ += static_cast<uint32_t>(
            kMaxUint32 * modulatorStep * (1.0f + previousSample_ * phaseFeedback));
        carrierPhase_ += carrierIncrement;
        subPhase_ += carrierIncrement >> 1;
        const float modulatorFb = fb > 0.0f ? 0.25f * fb * fb : 0.0f;
        const float modulator = sinePM(modulatorPhase_, modulatorFb * previousSample_);
        const float carrier = sinePM(carrierPhase_, depth * modulator);
        const float sub = sinePM(subPhase_, depth * carrier * 0.25f);
        previousSample_ += 0.05f * (carrier - previousSample_);
        carrierDownsampler.accumulate(j, carrier);
        subDownsampler.accumulate(j, sub);
      }

      *out++ = carrierDownsampler.read();
      *aux++ = subDownsampler.read();
    }
  }

 private:
  uint32_t carrierPhase_ = 0;
  uint32_t modulatorPhase_ = 0;
  uint32_t subPhase_ = 0;

  float previousCarrierFrequency_ = 0.0f;
  float previousModulatorFrequency_ = 0.0f;
  float previousAmount_ = 0.0f;
  float previousFeedback_ = 0.0f;
  float previousSample_ = 0.0f;

  float subFir_ = 0.0f;
  float carrierFir_ = 0.0f;
};

}  // namespace dihedra::eurorack

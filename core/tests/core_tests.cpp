// Dihedra core tests. Build and run with `make -C core test` (macOS or Linux).
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "dihedra/Engine.h"
#include "dihedra/HighPass.h"
#include "dihedra/HingeMapper.h"
#include "dihedra/OneEuroFilter.h"
#include "dihedra/Spring.h"

// Count heap allocations so we can prove render() never allocates.
// GCC inlines the delete below and reports free() on memory from operator
// new, not seeing that this file's operator new allocates with malloc.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
static std::atomic<long> gAllocations{0};
void* operator new(std::size_t size) {
  gAllocations.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static int gFailures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++gFailures;                                                           \
    }                                                                        \
  } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

using namespace dihedra;

// Drive the mapper like the app does: hinge events plus a tick per frame.
struct Rig {
  HingeMapper mapper;
  double t = 0.0;
  MapperOutput out{1.0f, 0.0f};
  explicit Rig(MapperConfig c = {}) : mapper(c) {}
  void hold(float degrees, double seconds, HingeStatus s = HingeStatus::PartiallyOpen) {
    const double dt = 1.0 / 120.0;
    for (double end = t + seconds; t < end; t += dt) {
      mapper.onHinge(degrees, s, t);
      out = mapper.tick(t);
    }
  }
  void sweep(float from, float to, double seconds) {
    const double dt = 1.0 / 120.0;
    const int steps = static_cast<int>(seconds / dt);
    for (int i = 0; i <= steps; ++i, t += dt) {
      const float a = static_cast<float>(i) / static_cast<float>(steps);
      mapper.onHinge(from + (to - from) * a, HingeStatus::PartiallyOpen, t);
      out = mapper.tick(t);
    }
  }
};

// One detent band in degrees, and the hinge angle at the center of band k.
static constexpr float kBand = (tuning::kFlatDegrees - tuning::kSteepDegrees) /
                               (static_cast<float>(tuning::kMaxK - tuning::kMinK) + 0.5f);
static float bandCenter(int k) { return tuning::kFlatDegrees - kBand * static_cast<float>(k - 1); }

static void testSpringExactness() {
  Spring a, b;
  a.omega = b.omega = 7.0f;
  a.reset(1.0f);
  b.reset(1.0f);
  a.step(5.0f, 0.1f);
  for (int i = 0; i < 10; ++i) b.step(5.0f, 0.01f);
  CHECK_NEAR(a.value, b.value, 1e-4f);
  CHECK_NEAR(a.velocity, b.velocity, 1e-3f);
  for (int i = 0; i < 200; ++i) a.step(5.0f, 0.01f);
  CHECK_NEAR(a.value, 5.0f, 1e-3f);
  // Critically damped: never overshoots a step.
  Spring c;
  c.omega = 7.0f;
  c.reset(1.0f);
  float peak = 1.0f;
  for (int i = 0; i < 1000; ++i) peak = std::max(peak, c.step(2.0f, 0.005f));
  CHECK(peak <= 2.0f + 1e-5f);
}

static void testOneEuro() {
  OneEuroFilter f;
  for (int i = 0; i < 100; ++i) CHECK_NEAR(f.filter(90.0f, i / 120.0), 90.0f, 1e-5f);
  // Step input converges.
  double t = 1.0;
  float y = 0.0f;
  for (int i = 0; i < 240; ++i, t += 1.0 / 120.0) y = f.filter(120.0f, t);
  CHECK_NEAR(y, 120.0f, 0.5f);
  // Duplicate timestamp returns the previous value.
  CHECK_NEAR(f.filter(0.0f, t - 1.0 / 120.0), y, 1e-6f);
}

static void testPosition() {
  HingeMapper m;
  CHECK_NEAR(m.position(180.0f), 1.0f, 1e-5f);
  CHECK_NEAR(m.position(tuning::kSteepDegrees), 8.5f, 1e-5f);
  CHECK_NEAR(m.position(10.0f), 8.5f, 1e-5f);   // clamps
  CHECK_NEAR(m.position(200.0f), 0.5f, 1e-5f);  // clamps
  for (int k = 1; k <= 8; ++k) CHECK_NEAR(m.position(bandCenter(k)), static_cast<float>(k), 1e-4f);
}

static void testDetentCommitAfterDwell() {
  Rig r;
  r.hold(bandCenter(1), 0.5);
  CHECK_NEAR(r.out.kTarget, 1.0f, 1e-6f);
  // Jump to band 3: after 0.1 s not committed yet (filter lag plus dwell).
  r.hold(bandCenter(3), 0.1);
  CHECK_NEAR(r.out.kTarget, 1.0f, 1e-6f);
  r.hold(bandCenter(3), 0.6);
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
  CHECK(std::fabs(r.out.fine) < 0.1f);
}

static void testHysteresis() {
  Rig r;
  r.hold(bandCenter(3), 1.0);
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
  // Sit right on the 3/4 boundary with +-1 degree jitter: stays at 3.
  const float boundary = tuning::kFlatDegrees - kBand * 2.5f;
  const double dt = 1.0 / 120.0;
  for (int i = 0; i < 600; ++i, r.t += dt) {
    r.mapper.onHinge(boundary + ((i % 2) ? 1.0f : -1.0f), HingeStatus::PartiallyOpen, r.t);
    r.out = r.mapper.tick(r.t);
    CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
  }
  // Fine reports where we are inside the band.
  CHECK(r.out.fine > 0.5f);
}

static void testQuickSweepCommitsNothing() {
  Rig r;
  r.hold(bandCenter(2), 1.0);
  r.sweep(bandCenter(2), bandCenter(7), 0.3);  // about 190 deg/s
  r.hold(bandCenter(7), 0.05);
  CHECK_NEAR(r.out.kTarget, 2.0f, 1e-6f);  // passed bands 3..6 without committing
  r.hold(bandCenter(7), 0.8);
  CHECK_NEAR(r.out.kTarget, 7.0f, 1e-6f);
}

static void testClosingLatchesRestedState() {
  Rig r;
  r.hold(bandCenter(3), 1.5);
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
  r.sweep(bandCenter(3), 8.0f, 0.4);  // fast close; status still says partiallyOpen
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);  // under kClosedDegrees counts as closed
  r.hold(5.0f, 2.0, HingeStatus::Closed);
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
  // Reopen to band 5 and settle.
  r.hold(bandCenter(5), 1.0);
  CHECK_NEAR(r.out.kTarget, 5.0f, 1e-6f);
}

static void testLaggingStatusCountsAsClosed() {
  // Closing that slows to a stop before the status says closed. iOS has
  // reported closed by 66 degrees at the latest, so 55 counts as closed
  // anyway, and the end of closing never becomes the remembered state.
  Rig r;
  r.hold(bandCenter(3), 1.5);
  const float nearlyShut = 55.0f;
  r.sweep(bandCenter(3), nearlyShut, 0.4);
  r.hold(nearlyShut, 1.0);  // status still partiallyOpen
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
  r.hold(5.0f, 1.0, HingeStatus::Closed);
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
}

static void testTopBandBeforeCoverScreen() {
  // iOS reports closed, and moves the app to the cover screen, from about
  // 85 degrees. k = 8 must be reached above that, and closing keeps it.
  Rig r;
  r.hold(bandCenter(1), 1.0);
  r.hold(90.0f, 1.0);
  CHECK_NEAR(r.out.kTarget, 8.0f, 1e-6f);
  r.hold(84.0f, 1.0, HingeStatus::Closed);
  CHECK_NEAR(r.out.kTarget, 8.0f, 1e-6f);
}

static void testFullyOpenIsSingleMirror() {
  Rig r;
  r.hold(bandCenter(4), 1.0);
  r.hold(180.0f, 1.0, HingeStatus::FullyOpen);
  CHECK_NEAR(r.out.kTarget, 1.0f, 1e-6f);
}

static void testContinuousMode() {
  MapperConfig c;
  c.detents = false;
  Rig r(c);
  r.hold(tuning::kFlatDegrees - kBand * 2.3f, 2.0);  // x = 3.3
  CHECK_NEAR(r.out.kTarget, 3.3f, 0.02f);
  CHECK_NEAR(r.out.fine, 0.0f, 1e-6f);
}

static void testHoldFreezesThenFollows() {
  Rig r;
  r.hold(bandCenter(3), 1.0);
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
  const float fine = r.out.fine;
  r.mapper.setHeld(true);
  CHECK(r.mapper.held());
  // Folding while held changes nothing, not even the fine offset.
  r.sweep(bandCenter(3), bandCenter(6), 0.5);
  r.hold(bandCenter(6), 1.0);
  CHECK_NEAR(r.out.kTarget, 3.0f, 1e-6f);
  CHECK_NEAR(r.out.fine, fine, 1e-6f);
  // The mapper tracked the hinge underneath, so release lands on band 6
  // at once instead of waiting out another dwell.
  r.mapper.setHeld(false);
  r.hold(bandCenter(6), 1.0 / 120.0);
  CHECK_NEAR(r.out.kTarget, 6.0f, 1e-6f);

  // Closing while held keeps the held state, and it stays latched after
  // release until the phone opens again.
  r.hold(bandCenter(6), 1.0);
  r.mapper.setHeld(true);
  r.hold(bandCenter(2), 1.0);
  r.hold(5.0f, 1.0, HingeStatus::Closed);
  CHECK_NEAR(r.out.kTarget, 6.0f, 1e-6f);
  r.mapper.setHeld(false);
  r.hold(5.0f, 1.0, HingeStatus::Closed);
  CHECK_NEAR(r.out.kTarget, 6.0f, 1e-6f);
  r.hold(bandCenter(4), 1.0);
  CHECK_NEAR(r.out.kTarget, 4.0f, 1e-6f);
}

static void testEngine() {
  Engine e;
  std::vector<float> left(8192), right(8192);

  // Silent until the gain target is raised.
  e.render(left.data(), right.data(), 512);
  float peak = 0.0f;
  for (int i = 0; i < 512; ++i) peak = std::max(peak, std::fabs(left[i]));
  CHECK(peak == 0.0f);

  e.controls().gainTarget.store(1.0f);
  e.controls().kTarget.store(3.0f);

  // Odd buffer sizes exercise the partial-block path. No allocations allowed.
  const size_t sizes[] = {1, 7, 12, 13, 256, 511, 1024, 4096};
  const long before = gAllocations.load();
  bool finite = true;
  float maxAbs = 0.0f;
  for (int rep = 0; rep < 50; ++rep) {
    for (size_t n : sizes) {
      e.render(left.data(), right.data(), n);
      for (size_t i = 0; i < n; ++i) {
        finite = finite && std::isfinite(left[i]) && std::isfinite(right[i]);
        maxAbs = std::max(maxAbs, std::max(std::fabs(left[i]), std::fabs(right[i])));
      }
    }
  }
  CHECK(gAllocations.load() == before);
  CHECK(finite);
  CHECK(maxAbs > 0.1f);
  // The high-pass shifts each partial's phase, so the peak rises above the
  // FM voice's flat envelope, but it stays far below full scale.
  CHECK(maxAbs <= 2.0f * tuning::kOutputLevel);
  CHECK_NEAR(e.currentK(), 3.0f, 1e-3f);
  CHECK(e.currentGain() > 0.99f);

  // Mono buffer path.
  e.render(left.data(), nullptr, 100);
  CHECK(std::isfinite(left[99]));

  // Release is fast: 150 ms after the target drops, the level is near silent.
  e.controls().gainTarget.store(0.0f);
  e.render(left.data(), right.data(), 7200);
  CHECK(e.currentGain() < 0.01f);
}

static void testEurorackFMMatchesUpstream() {
  // Samples rendered by the upstream engine (plaits/dsp/engine/fm_engine.cc
  // at f4cfa76) from these frequencies, depth and feedback, with its ramps
  // starting at zero. The port's own sine table differs from upstream's in
  // the last bit of 4 values, which moves the output by about 3e-5.
  struct Case {
    float carrier, modulator, amount, feedback;
    float out[8], aux[8];  // samples 59, 119, ... 479
  };
  const Case cases[] = {
      {0.00153358269f, 0.00283042877f, 0.720000029f, 0.0f,
       {-0.6806858f, 0.5981702f, -0.5234749f, -0.7511883f, 0.3455241f, 0.9097989f, -0.6815007f,
        0.2881171f},
       {0.2600222f, 0.2718256f, 0.3741468f, -0.4887782f, -0.2313416f, 0.9972638f, 0.7937097f,
        -0.02845994f}},
      {0.000766791345f, 0.00265594199f, 1.28000009f, 0.700000048f,
       {0.04804746f, -0.01446873f, -0.2436055f, -0.6484803f, 0.4451418f, -0.7192175f, -0.2283236f,
        -0.3256709f},
       {0.3315334f, 0.8284703f, 0.9242604f, 0.7657452f, -0.3898287f, 0.9243024f, -0.3749707f,
        -0.5108317f}},
      {0.00306716538f, 0.00372652407f, 0.5f, -0.600000024f,
       {0.8990485f, -0.6020702f, -0.9744546f, -0.9590811f, -0.02433852f, -0.8897436f, 0.9594715f,
        -0.969029f},
       {0.3600428f, -0.6922646f, -0.348891f, 0.9220534f, -0.9544218f, 0.356387f, -0.834824f,
        -0.9768583f}},
  };
  for (const Case& c : cases) {
    eurorack::FMEngine fm;
    fm.init();
    float out[480], aux[480];
    for (int b = 0; b < 40; ++b) {
      fm.render(c.carrier, c.modulator, c.amount, c.feedback, out + 12 * b, aux + 12 * b, 12);
    }
    for (int i = 0; i < 8; ++i) {
      CHECK_NEAR(out[59 + 60 * i], c.out[i], 1e-4f);
      CHECK_NEAR(aux[59 + 60 * i], c.aux[i], 1e-4f);
    }
  }
}

// Power at hz in one second of x, from a Hann-windowed DFT.
static double power(const std::vector<float>& x, double hz) {
  double re = 0, im = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (x.size() - 1));
    const double ph = 2.0 * M_PI * hz * i / 48000.0;
    re += w * x[i] * std::cos(ph);
    im += w * x[i] * std::sin(ph);
  }
  return re * re + im * im;
}

static void testHarmonicAtIntegerK() {
  // With the voice an octave down, a whole number k puts every partial on a
  // multiple of half the root. Between whole numbers they leave that grid.
  auto settle = [](float k) {
    Engine e;
    e.controls().gainTarget.store(1.0f);
    e.controls().kTarget.store(k);
    std::vector<float> left(48000), right(48000);
    e.render(left.data(), right.data(), 48000);  // settle
    e.render(left.data(), right.data(), 48000);
    return left;
  };
  // Powers relative to the carrier's.
  const double root = tuning::kRootHz;
  const std::vector<float> two = settle(2.0f);
  const double carrier = power(two, root);
  CHECK(power(two, root * 3.0) > 0.1 * carrier);   // carrier + modulator
  CHECK(power(two, root * 0.5) > 0.1 * carrier);   // the sub voice
  CHECK(power(two, root * 2.25) < 1e-4 * carrier);  // nothing off the grid

  const std::vector<float> between = settle(2.3f);
  CHECK(power(between, root * 3.3) > 0.1 * power(between, root));
}

// Share of a signal's energy below hz, from a Hann-windowed DFT with 1 Hz
// bins (the signal is one second long).
static double lowShare(const std::vector<float>& x, int hz) {
  const size_t n = x.size();
  std::vector<double> w(n);
  double total = 0.0;
  for (size_t i = 0; i < n; ++i) {
    w[i] = (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1))) * x[i];
    total += w[i] * w[i];
  }
  double low = 0.0;
  for (int f = 0; f <= hz; ++f) {
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const double ph = 2.0 * M_PI * f * static_cast<double>(i) / static_cast<double>(n);
      re += w[i] * std::cos(ph);
      im += w[i] * std::sin(ph);
    }
    low += (f == 0 ? 1.0 : 2.0) * (re * re + im * im);
  }
  return low / (static_cast<double>(n) * total);
}

static void testHighPass() {
  const float sr = Engine::kSampleRate;
  auto gainAt = [&](float hz) {
    HighPass hp;
    hp.init(tuning::kHighPassHz, sr);
    double in = 0.0, out = 0.0;
    for (int i = 0; i < 2 * 48000; ++i) {
      const float x = hz == 0.0f ? 1.0f : std::sin(2.0f * 3.14159265f * hz * i / sr);
      const float y = hp.process(x);
      if (i >= 48000) {  // skip the settling second
        in += x * x;
        out += y * y;
      }
    }
    return std::sqrt(out / in);
  };
  CHECK_NEAR(gainAt(tuning::kRootHz), 1.0, 0.01);    // the root passes untouched
  CHECK(gainAt(0.5f * tuning::kHighPassHz) < 0.07);  // about -24 dB an octave down
  CHECK(gainAt(0.0f) < 1e-4);                        // no DC
}

static void testNoRumbleNearSingleMirror() {
  // At k = 1 the FM's lower sideband sits at 0 Hz, and while k glides to 1 it
  // sweeps down below hearing. Both must be filtered out.
  Engine e;
  e.controls().gainTarget.store(1.0f);
  std::vector<float> left(48000), right(48000);
  e.render(left.data(), right.data(), 48000);  // settle at k = 1
  e.render(left.data(), right.data(), 48000);
  double meanL = 0.0, meanR = 0.0;
  for (size_t i = 0; i < left.size(); ++i) {
    meanL += left[i];
    meanR += right[i];
  }
  CHECK(std::fabs(meanL / 48000.0) < 1e-3);
  CHECK(std::fabs(meanR / 48000.0) < 1e-3);

  e.controls().kTarget.store(2.0f);
  e.render(left.data(), right.data(), 48000);
  e.controls().kTarget.store(1.0f);
  e.render(left.data(), right.data(), 48000);  // the glide back to 1
  const int subsonic = static_cast<int>(0.5f * tuning::kHighPassHz);
  CHECK(lowShare(left, subsonic) < 1e-3);
  CHECK(lowShare(right, subsonic) < 1e-3);
}

int main() {
  testSpringExactness();
  testOneEuro();
  testPosition();
  testDetentCommitAfterDwell();
  testHysteresis();
  testQuickSweepCommitsNothing();
  testClosingLatchesRestedState();
  testLaggingStatusCountsAsClosed();
  testTopBandBeforeCoverScreen();
  testFullyOpenIsSingleMirror();
  testContinuousMode();
  testHoldFreezesThenFollows();
  testEngine();
  testEurorackFMMatchesUpstream();
  testHarmonicAtIntegerK();
  testHighPass();
  testNoRumbleNearSingleMirror();
  if (gFailures == 0) {
    std::printf("All core tests passed.\n");
    return 0;
  }
  std::printf("%d check(s) failed.\n", gFailures);
  return 1;
}

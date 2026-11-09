// Dihedra core tests. Build and run with `make -C core test` (macOS or Linux).
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "dihedra/Engine.h"
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

static float bandCenter(int k) { return 180.0f - 16.0f * static_cast<float>(k - 1); }

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
  CHECK_NEAR(m.position(60.0f), 8.5f, 1e-5f);
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
  const float boundary = 180.0f - 16.0f * 2.5f;
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
  r.sweep(bandCenter(2), bandCenter(7), 0.3);  // about 270 deg/s
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
  r.hold(180.0f - 16.0f * 2.3f, 2.0);  // x = 3.3
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
  CHECK(maxAbs <= tuning::kOutputLevel + 1e-4f);
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

static void testHarmonicAtIntegerK() {
  // With integer k the carrier period repeats exactly: compare one carrier
  // period apart using a root-aligned sample rate is awkward, so instead check
  // the spectrum has energy only near multiples of the root via a coarse DFT.
  Engine e;
  e.controls().gainTarget.store(1.0f);
  e.controls().kTarget.store(2.0f);
  std::vector<float> left(48000), right(48000);
  e.render(left.data(), right.data(), 48000);  // settle
  e.render(left.data(), right.data(), 48000);
  auto power = [&](double hz) {
    double re = 0, im = 0;
    for (size_t i = 0; i < left.size(); ++i) {
      const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (left.size() - 1));
      const double ph = 2.0 * M_PI * hz * i / 48000.0;
      re += w * left[i] * std::cos(ph);
      im += w * left[i] * std::sin(ph);
    }
    return re * re + im * im;
  };
  const double root = tuning::kRootHz;
  const double onHarmonic = power(root * 3.0);
  const double between = power(root * 2.5);
  CHECK(onHarmonic > 100.0 * between);
}

int main() {
  testSpringExactness();
  testOneEuro();
  testPosition();
  testDetentCommitAfterDwell();
  testHysteresis();
  testQuickSweepCommitsNothing();
  testClosingLatchesRestedState();
  testFullyOpenIsSingleMirror();
  testContinuousMode();
  testHoldFreezesThenFollows();
  testEngine();
  testHarmonicAtIntegerK();
  if (gFailures == 0) {
    std::printf("All core tests passed.\n");
    return 0;
  }
  std::printf("%d check(s) failed.\n", gFailures);
  return 1;
}

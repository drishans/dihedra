// Dihedra: hinge angle -> symmetry target.
//
// Main thread only. Feed it hinge updates with onHinge() and call tick()
// regularly (every frame). tick() matters because the system sends no
// updates while the hinge is still, and both the filter and the detent
// dwell timer need time to pass.
#pragma once

#include <algorithm>
#include <cmath>

#include "dihedra/OneEuroFilter.h"
#include "dihedra/Tuning.h"

namespace dihedra {

enum class HingeStatus : int { Closed = 0, PartiallyOpen = 1, FullyOpen = 2 };

struct MapperConfig {
  float flatDegrees = tuning::kFlatDegrees;
  float steepDegrees = tuning::kSteepDegrees;
  float closedDegrees = tuning::kClosedDegrees;
  int minK = tuning::kMinK;
  int maxK = tuning::kMaxK;
  float hysteresis = tuning::kHysteresis;
  double dwellSeconds = tuning::kDwellSeconds;
  float restSpeed = tuning::kRestSpeed;
  bool detents = true;
};

struct MapperOutput {
  float kTarget;  // symmetry target; an integer when detents are on
  float fine;     // offset inside the current detent, -1..1
};

class HingeMapper {
 public:
  explicit HingeMapper(MapperConfig config = {})
      : config_(config),
        filter_({tuning::kFilterMinCutoff, tuning::kFilterBeta, tuning::kFilterDCutoff}) {
    committed_ = candidate_ = config_.minK;
    restK_ = static_cast<float>(config_.minK);
  }

  const MapperConfig& config() const { return config_; }

  void setDetents(bool on) {
    if (on == config_.detents) return;
    config_.detents = on;
    committed_ = candidate_ = clampK(static_cast<int>(std::lround(lastX_)));
  }

  // Continuous position in detent units. Flat maps to minK (the center of
  // the first band); steepDegrees maps to the outer edge of the last band.
  // Linear in angle so every band is the same width (about 16 degrees,
  // roughly three times Apple's stated 5 degree accuracy).
  float position(float degrees) const {
    const float span = config_.flatDegrees - config_.steepDegrees;
    const float u = (config_.flatDegrees - degrees) / span;
    const float bands = static_cast<float>(config_.maxK - config_.minK) + 0.5f;
    const float x = static_cast<float>(config_.minK) + u * bands;
    return std::clamp(x, static_cast<float>(config_.minK) - 0.5f,
                      static_cast<float>(config_.maxK) + 0.5f);
  }

  void onHinge(float degrees, HingeStatus status, double t) {
    const bool wasClosed = isClosed_;
    status_ = status;
    lastDegrees_ = degrees;
    hasSample_ = true;
    isClosed_ = status == HingeStatus::Closed || degrees < config_.closedDegrees;
    if (wasClosed && !isClosed_) {
      // Reopened: start the filter from the new angle instead of gliding
      // up from the closed angle.
      filter_.reset();
    }
    (void)t;
  }

  // Hold: freeze the output where it is and ignore the hinge until released.
  // The mapper keeps tracking underneath, so on release it picks up the
  // current angle and the springs glide there.
  void setHeld(bool on) {
    if (on == held_) return;
    held_ = on;
    if (on) heldOutput_ = lastOutput_;
  }

  bool held() const { return held_; }

  MapperOutput tick(double t) {
    lastOutput_ = track(t);
    if (!held_) return lastOutput_;
    // Closing while held keeps the held state, the same way closing keeps
    // what the phone rested on.
    restK_ = heldOutput_.kTarget;
    restFine_ = heldOutput_.fine;
    return heldOutput_;
  }

  int committed() const { return committed_; }

 private:
  int clampK(int k) const { return std::clamp(k, config_.minK, config_.maxK); }

  MapperOutput track(double t) {
    if (!hasSample_) return {static_cast<float>(committed_), 0.0f};
    if (isClosed_) {
      // Closed: hold whatever the phone last rested on, so closing it keeps
      // the chosen symmetry instead of sweeping to the most-folded state.
      return {restK_, restFine_};
    }

    const float degrees = filter_.filter(lastDegrees_, t);
    const float speed = std::fabs(filter_.derivative());
    const float x = position(degrees);
    lastX_ = x;

    MapperOutput out{};
    if (!config_.detents) {
      out.kTarget = std::clamp(x, static_cast<float>(config_.minK),
                               static_cast<float>(config_.maxK));
      out.fine = 0.0f;
    } else {
      updateDetent(x, t);
      const float reach = 0.5f + config_.hysteresis;
      out.kTarget = static_cast<float>(committed_);
      out.fine = std::clamp((x - static_cast<float>(committed_)) / reach, -1.0f, 1.0f);
    }

    // Remember the resting state, but only inside the usable range, so the
    // last moments of closing never become the remembered state.
    if (speed < config_.restSpeed && degrees >= config_.steepDegrees) {
      restK_ = out.kTarget;
      restFine_ = out.fine;
    }
    return out;
  }

  // Schmitt trigger plus a dwell timer: a new detent must be passed by the
  // hysteresis margin and then held for dwellSeconds before it commits. A
  // quick sweep (opening or closing) commits nothing along the way.
  void updateDetent(float x, double t) {
    const float c = static_cast<float>(committed_);
    const float edge = 0.5f + config_.hysteresis;
    int proposal = committed_;
    if (x > c + edge || x < c - edge) proposal = clampK(static_cast<int>(std::lround(x)));

    if (proposal == committed_) {
      candidate_ = committed_;
      return;
    }
    if (proposal != candidate_) {
      candidate_ = proposal;
      candidateSince_ = t;
      return;
    }
    if (t - candidateSince_ >= config_.dwellSeconds) committed_ = candidate_;
  }

  MapperConfig config_;
  OneEuroFilter filter_;
  HingeStatus status_ = HingeStatus::PartiallyOpen;
  bool hasSample_ = false;
  bool isClosed_ = false;
  float lastDegrees_ = tuning::kFlatDegrees;
  float lastX_ = 1.0f;
  int committed_ = 1;
  int candidate_ = 1;
  double candidateSince_ = 0.0;
  float restK_ = 1.0f;
  float restFine_ = 0.0f;
  bool held_ = false;
  MapperOutput lastOutput_{1.0f, 0.0f};
  MapperOutput heldOutput_{1.0f, 0.0f};
};

}  // namespace dihedra

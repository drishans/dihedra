#import "DHCore.h"

#include <algorithm>
#include <atomic>
#include <memory>

#include "dihedra/Engine.h"
#include "dihedra/HingeMapper.h"
#include "dihedra/Spring.h"
#include "dihedra/Tuning.h"

namespace {

dihedra::HingeStatus ToCore(DHHingeStatus status) {
  switch (status) {
    case DHHingeStatusClosed:
      return dihedra::HingeStatus::Closed;
    case DHHingeStatusFullyOpen:
      return dihedra::HingeStatus::FullyOpen;
    case DHHingeStatusPartiallyOpen:
    default:
      return dihedra::HingeStatus::PartiallyOpen;
  }
}

}  // namespace

@implementation DHCore {
  std::unique_ptr<dihedra::Engine> _engine;
  dihedra::HingeMapper _mapper;
  dihedra::Spring _visualK;
  dihedra::Spring _visualFine;
  dihedra::MapperOutput _lastOutput;
  double _lastFrameTime;
}

+ (double)sampleRate {
  return dihedra::Engine::kSampleRate;
}

- (instancetype)init {
  if ((self = [super init])) {
    _engine = std::make_unique<dihedra::Engine>();
    _visualK.omega = dihedra::tuning::kSymmetryOmega;
    _visualFine.omega = dihedra::tuning::kFineOmega;
    _visualK.reset(1.0f);
    _visualFine.reset(0.0f);
    _lastOutput = {1.0f, 0.0f};
    _lastFrameTime = -1.0;
  }
  return self;
}

- (BOOL)detentsEnabled {
  return _mapper.config().detents;
}

- (void)setDetentsEnabled:(BOOL)detentsEnabled {
  _mapper.setDetents(detentsEnabled);
}

- (BOOL)isHeld {
  return _mapper.held();
}

- (void)setHeld:(BOOL)held {
  _mapper.setHeld(held);
}

- (AVAudioSourceNode *)makeSourceNodeWithFormat:(AVAudioFormat *)format {
  NSAssert(format.sampleRate == dihedra::Engine::kSampleRate,
           @"Dihedra renders at %.0f Hz", dihedra::Engine::kSampleRate);
  // Raw pointer: the block must not retain self or touch Objective-C objects.
  // DHCore owns the engine and outlives the source node (AudioController
  // holds both and stops the engine before letting either go).
  dihedra::Engine *engine = _engine.get();
  return [[AVAudioSourceNode alloc]
      initWithFormat:format
         renderBlock:^OSStatus(BOOL *isSilence, const AudioTimeStamp *timestamp,
                               AVAudioFrameCount frameCount, AudioBufferList *outputData) {
           (void)timestamp;
           AudioBuffer *buffers = outputData->mBuffers;
           float *left = static_cast<float *>(buffers[0].mData);
           float *right =
               outputData->mNumberBuffers > 1 ? static_cast<float *>(buffers[1].mData) : nullptr;
           engine->render(left, right, frameCount);
           *isSilence = NO;
           return noErr;
         }];
}

- (void)publish:(dihedra::MapperOutput)output {
  auto &controls = _engine->controls();
  controls.kTarget.store(output.kTarget, std::memory_order_relaxed);
  controls.fineTarget.store(output.fine, std::memory_order_relaxed);
  _lastOutput = output;
}

- (void)hingeChangedToDegrees:(double)degrees status:(DHHingeStatus)status time:(double)time {
  _mapper.onHinge(static_cast<float>(degrees), ToCore(status), time);
  // Publish right away too, so a close is latched even if no frame renders
  // (for example when the screen locks with background audio on).
  [self publish:_mapper.tick(time)];
}

- (DHFrame)frameAtTime:(double)time {
  const dihedra::MapperOutput output = _mapper.tick(time);
  [self publish:output];

  float dt = 0.0f;
  if (_lastFrameTime >= 0.0) dt = static_cast<float>(std::clamp(time - _lastFrameTime, 0.0, 0.1));
  _lastFrameTime = time;

  DHFrame frame;
  frame.k = _visualK.step(output.kTarget, dt);
  frame.fine = _visualFine.step(output.fine, dt);
  frame.kTarget = output.kTarget;
  return frame;
}

- (void)setGainTarget:(float)gain {
  _engine->controls().gainTarget.store(gain, std::memory_order_relaxed);
}

@end

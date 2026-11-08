// Dihedra: Objective-C face of the C++ core. Pure Objective-C so Swift can
// import it through the bridging header without C++ interop mode.
#import <Foundation/Foundation.h>
#import <AVFAudio/AVFAudio.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, DHHingeStatus) {
  DHHingeStatusClosed = 0,
  DHHingeStatusPartiallyOpen = 1,
  DHHingeStatusFullyOpen = 2,
};

/// What the renderer needs each frame.
typedef struct {
  float k;        // smoothed symmetry (glides between detents)
  float fine;     // smoothed offset inside the current detent, -1..1
  float kTarget;  // committed detent
} DHFrame;

@interface DHCore : NSObject

/// The core renders at this rate (48 kHz, the eurorack engines' native rate).
@property (class, nonatomic, readonly) double sampleRate;

/// Whether the hinge snaps to whole-number symmetries. Main thread.
@property (nonatomic) BOOL detentsEnabled;

/// Builds the source node. Its render block calls straight into C++ and
/// captures only a raw pointer: no Objective-C, Swift, ARC or allocation
/// on the audio thread. The format's sample rate must equal `sampleRate`.
- (AVAudioSourceNode *)makeSourceNodeWithFormat:(AVAudioFormat *)format
    NS_SWIFT_NAME(makeSourceNode(format:));

/// Main thread. Call for every hinge update; time is CACurrentMediaTime().
- (void)hingeChangedToDegrees:(double)degrees
                       status:(DHHingeStatus)status
                         time:(double)time
    NS_SWIFT_NAME(hingeChanged(degrees:status:time:));

/// Main thread, once per rendered frame. Advances the mapper (filter and
/// detent timers), publishes targets to the audio thread, and returns the
/// smoothed values for the shader.
- (DHFrame)frameAtTime:(double)time NS_SWIFT_NAME(frame(at:));

/// 0 fades out quickly, 1 swells in. Any thread.
- (void)setGainTarget:(float)gain;

@end

NS_ASSUME_NONNULL_END

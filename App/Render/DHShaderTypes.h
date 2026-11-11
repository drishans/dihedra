// Dihedra: uniforms shared by Swift (through the bridging header) and Metal.
// Plain floats only, so the layout is identical in C, Swift and MSL.
#ifndef DH_SHADER_TYPES_H
#define DH_SHADER_TYPES_H

typedef struct {
  float resolutionX;  // drawable size in pixels
  float resolutionY;
  float time;         // seconds since launch
  float k;            // smoothed symmetry; seamless when it is a whole number
  float fine;         // offset inside the current detent, -1..1
  float energy;       // reserved for the phase 4 FFT bands
  float sourceMix;    // camera pipeline only: 0 shows the glass, 1 the camera
  float sourceAngle;  // camera pipeline only: clockwise turn, in radians, that levels the camera image
} DHUniforms;

#endif

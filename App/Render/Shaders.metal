// Dihedra: the kaleidoscope.
//
// Two mirrors meeting at angle pi/k produce a dihedral pattern with 2k
// images. When k is not a whole number the last wedge is cut short and a
// seam appears at 6 o'clock, which is the visual twin of the inharmonic FM
// sound between detents.
//
// Behind the mirrors sits drifting stained glass or the camera. The renderer
// builds one pipeline for each with the kCamera function constant, and the
// camera one fades from the glass with sourceMix.
#include <metal_stdlib>
#include "DHShaderTypes.h"

using namespace metal;

constant bool kCamera [[function_constant(0)]];

// Camera units per half of its shorter side. At k = 1 this fills the screen's
// width with the camera's.
constant float kCameraScale = 0.8;

struct VertexOut {
  float4 position [[position]];
};

// One triangle that covers the screen; no vertex buffer needed.
vertex VertexOut dh_vertex(uint vid [[vertex_id]]) {
  const float2 p = float2(float((vid << 1) & 2), float(vid & 2));
  VertexOut out;
  out.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
  return out;
}

static float2 dihedral_fold(float2 p, float k) {
  const float r = length(p);
  // Angle from straight up, clockwise, in (0, 2pi]. The wrap sits at 6 o'clock.
  const float a = atan2(p.x, p.y) + M_PI_F;
  const float w = M_PI_F / k;
  float m = fmod(a, 2.0 * w);
  if (m > w) m = 2.0 * w - m;
  return r * float2(cos(m), sin(m));
}

static float2 rotate(float2 q, float angle) {
  const float2x2 rot = float2x2(float2(cos(angle), sin(angle)), float2(-sin(angle), cos(angle)));
  return rot * q;
}

static float3 palette(float t) {
  const float3 a = float3(0.5);
  const float3 b = float3(0.45);
  const float3 c = float3(1.0);
  const float3 d = float3(0.00, 0.33, 0.67);
  return a + b * cos(6.28318530718 * (c * t + d));
}

// Drifting stained glass at folded point f.
static float3 glass(float2 f, constant DHUniforms& u) {
  const float time = u.time;

  // Small hinge nudges inside a detent turn and zoom the glass.
  const float2 q = rotate(f, 0.03 * time + 0.5 * u.fine) * (1.25 + 0.2 * u.fine);

  // Flowing base layer: domain-warped interference that fills the screen.
  const float wx = q.x + 0.35 * sin(q.y * 2.1 + time * 0.11);
  const float wy = q.y + 0.35 * sin(q.x * 1.7 - time * 0.09);
  const float v = sin(wx * 3.1 + time * 0.07) + sin(wy * 2.7 - time * 0.05) +
                  sin((wx + wy) * 2.2 + time * 0.04);

  // Stained glass: bright panes with dark leading. Color steps hide under the leading.
  const float bands = v * 1.4;
  const float cell = abs(fract(bands) - 0.5) * 2.0;
  const float leading = smoothstep(0.08, 0.33, cell);
  const float pane = floor(bands + 0.5);

  // Drifting gems.
  float glow = 0.0;
  for (int i = 0; i < 5; ++i) {
    const float fi = float(i);
    const float2 c = float2(sin(time * 0.071 * (fi + 1.3) + fi * 1.7),
                            cos(time * 0.053 * (fi + 0.7) + fi * 2.3)) * (0.3 + 0.15 * fi);
    const float2 dq = q - c;
    glow += 0.006 / (0.003 + dot(dq, dq));
  }

  const float r = length(q);
  // Each detent gets its own color mood through the 0.09 * k term.
  const float t = 0.11 * pane + 0.05 * v + 0.15 * r + 0.015 * time + 0.09 * u.k + 0.08 * u.fine;
  const float3 col = palette(t);
  const float light = (0.25 + 0.75 * leading) * (0.75 + 0.25 * cos(cell * 3.0));
  return col * light + (1.0 - exp(-0.5 * glow)) * float3(1.0, 0.95, 0.85);
}

// The camera at folded point f. Hinge nudges turn and zoom it like the
// glass, but it doesn't drift, so the scene holds still until the phone moves.
static float3 camera_view(float2 f, constant DHUniforms& u, texture2d<float> camera) {
  // The fold's wedge spans angles 0 to pi/k. Turn it to face the camera's
  // right, so at k = 1 the screen's right half shows the camera's right half
  // as it is and the left half mirrors it.
  const float k = max(u.k, 1.0);
  float2 q = rotate(f, 0.5 * u.fine - 0.5 * M_PI_F / k) * (1.25 + 0.2 * u.fine);

  // Level the image: turning it clockwise by sourceAngle on screen means
  // sampling counterclockwise.
  q = rotate(q, u.sourceAngle) * kCameraScale;

  const float2 size = float2(camera.get_width(), camera.get_height());
  const float2 uv = 0.5 + 0.5 * float2(q.x, -q.y) * min(size.x, size.y) / size;
  // Past the image's edges, further mirrors.
  constexpr sampler s(filter::linear, address::mirrored_repeat);
  return camera.sample(s, uv).rgb;
}

fragment float4 dh_fragment(VertexOut in [[stage_in]],
                            constant DHUniforms& u [[buffer(0)]],
                            texture2d<float> camera [[texture(0), function_constant(kCamera)]]) {
  const float2 res = float2(u.resolutionX, u.resolutionY);
  const float s = 0.5 * min(res.x, res.y);
  float2 p = (in.position.xy - 0.5 * res) / s;
  p.y = -p.y;

  const float2 f = dihedral_fold(p, max(u.k, 1.0));

  float3 col = float3(0.0);
  if (!kCamera || u.sourceMix < 1.0) col = glass(f, u);
  if (kCamera) col = mix(col, camera_view(f, u, camera), smoothstep(0.0, 1.0, u.sourceMix));

  col *= 1.0 - 0.35 * clamp(length(p) - 0.6, 0.0, 1.0);
  return float4(saturate(col), 1.0);
}

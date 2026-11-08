// Dihedra: phase 1 kaleidoscope.
//
// Two mirrors meeting at angle pi/k produce a dihedral pattern with 2k
// images. When k is not a whole number the last wedge is cut short and a
// seam appears at 6 o'clock, which is the visual twin of the inharmonic FM
// sound between detents.
#include <metal_stdlib>
#include "DHShaderTypes.h"

using namespace metal;

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

static float3 palette(float t) {
  const float3 a = float3(0.5);
  const float3 b = float3(0.45);
  const float3 c = float3(1.0);
  const float3 d = float3(0.00, 0.33, 0.67);
  return a + b * cos(6.28318530718 * (c * t + d));
}

fragment float4 dh_fragment(VertexOut in [[stage_in]],
                            constant DHUniforms& u [[buffer(0)]]) {
  const float2 res = float2(u.resolutionX, u.resolutionY);
  const float s = 0.5 * min(res.x, res.y);
  float2 p = (in.position.xy - 0.5 * res) / s;
  p.y = -p.y;

  const float time = u.time;
  float2 q = dihedral_fold(p, max(u.k, 1.0));

  // Small hinge nudges inside a detent turn and zoom the glass.
  const float ang = 0.03 * time + 0.5 * u.fine;
  const float2x2 rot = float2x2(float2(cos(ang), sin(ang)), float2(-sin(ang), cos(ang)));
  q = (rot * q) * (1.25 + 0.2 * u.fine);

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
  float3 col = palette(t);
  const float light = (0.25 + 0.75 * leading) * (0.75 + 0.25 * cos(cell * 3.0));
  col = col * light + (1.0 - exp(-0.5 * glow)) * float3(1.0, 0.95, 0.85);
  col *= 1.0 - 0.35 * clamp(length(p) - 0.6, 0.0, 1.0);
  return float4(saturate(col), 1.0);
}

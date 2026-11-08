"""Numpy port of App/Render/Shaders.metal for quick look checks. Keep in sync by hand.

Usage: python3 tools/shader_preview.py out.png
"""
import sys
import numpy as np
from PIL import Image

TAU = 6.28318530718


def fold(px, py, k):
    r = np.hypot(px, py)
    # Angle measured from straight up, clockwise, in (0, 2pi]; the wrap
    # (and the seam when k is fractional) sits at 6 o'clock.
    a = np.arctan2(px, py) + np.pi
    w = np.pi / k
    m = np.mod(a, 2 * w)
    m = np.where(m > w, 2 * w - m, m)
    return r * np.cos(m), r * np.sin(m)


def palette(t):
    a = np.array([0.5, 0.5, 0.5])
    b = np.array([0.45, 0.45, 0.45])
    c = np.array([1.0, 1.0, 1.0])
    d = np.array([0.00, 0.33, 0.67])
    return a + b * np.cos(TAU * (c * t[..., None] + d))


def shade(W, H, time, k, fine):
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5
    s = 0.5 * min(W, H)
    px = (xs - 0.5 * W) / s
    py = -(ys - 0.5 * H) / s
    qx, qy = fold(px, py, max(k, 1.0))

    ang = 0.03 * time + 0.5 * fine
    ca, sa = np.cos(ang), np.sin(ang)
    zoom = 1.25 + 0.2 * fine
    qx, qy = (ca * qx - sa * qy) * zoom, (sa * qx + ca * qy) * zoom

    # Flowing base layer: domain-warped interference that fills the screen.
    wx = qx + 0.35 * np.sin(qy * 2.1 + time * 0.11)
    wy = qy + 0.35 * np.sin(qx * 1.7 - time * 0.09)
    v = (np.sin(wx * 3.1 + time * 0.07) + np.sin(wy * 2.7 - time * 0.05)
         + np.sin((wx + wy) * 2.2 + time * 0.04))
    # Glass-like cells: bright panes with dark leading between them.
    bands = v * 1.4
    cell = np.abs(np.mod(bands, 1.0) - 0.5) * 2.0          # 0 at edges, 1 at centers
    leading = np.clip((cell - 0.08) / 0.25, 0.0, 1.0)
    leading = leading * leading * (3 - 2 * leading)
    pane = np.floor(bands + 0.5)  # color steps hide under the leading

    # Drifting gems on top.
    glow = np.zeros_like(qx)
    for i in range(5):
        fi = float(i)
        cx = np.sin(time * 0.071 * (fi + 1.3) + fi * 1.7) * (0.3 + 0.15 * fi)
        cy = np.cos(time * 0.053 * (fi + 0.7) + fi * 2.3) * (0.3 + 0.15 * fi)
        d2 = (qx - cx) ** 2 + (qy - cy) ** 2
        glow += 0.006 / (0.003 + d2)

    r = np.hypot(qx, qy)
    t = 0.11 * pane + 0.05 * v + 0.15 * r + 0.015 * time + 0.09 * k + 0.08 * fine
    col = palette(t)
    light = (0.25 + 0.75 * leading) * (0.75 + 0.25 * np.cos(cell * 3.0))
    col = col * light[..., None] + (1.0 - np.exp(-0.5 * glow))[..., None] * np.array([1.0, 0.95, 0.85])
    # Gentle vignette.
    rr = np.hypot(px, py)
    col *= (1.0 - 0.35 * np.clip(rr - 0.6, 0.0, 1.0))[..., None]
    return np.clip(col, 0.0, 1.0)


if __name__ == "__main__":
    out = sys.argv[1]
    tiles = []
    for k, fine in [(1, 0.0), (3, 0.0), (3.5, 0.0), (5, 0.5), (8, -0.5)]:
        img = shade(360, 480, 40.0, k, fine)
        tiles.append((img * 255).astype(np.uint8))
    sheet = np.concatenate(tiles, axis=1)
    Image.fromarray(sheet).save(out)

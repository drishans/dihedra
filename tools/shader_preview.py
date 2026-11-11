"""Numpy port of App/Render/Shaders.metal for quick look checks. Keep in sync by hand.

Usage: python3 tools/shader_preview.py out.png [camera.png]

The top row is the glass and the bottom row the camera, at the same k. The
camera row uses camera.png, or a built-in test card stored sideways the way a
camera delivers it in portrait, so it only looks upright if the shader levels
it. A fractional k shows the seam at 6 o'clock.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw

TAU = 6.28318530718
CAMERA_SCALE = 0.8


def fold(px, py, k):
    r = np.hypot(px, py)
    # Angle measured from straight up, clockwise, in (0, 2pi]; the wrap
    # (and the seam when k is fractional) sits at 6 o'clock.
    a = np.arctan2(px, py) + np.pi
    w = np.pi / k
    m = np.mod(a, 2 * w)
    m = np.where(m > w, 2 * w - m, m)
    return r * np.cos(m), r * np.sin(m)


def rotate(x, y, angle):
    c, s = np.cos(angle), np.sin(angle)
    return c * x - s * y, s * x + c * y


def palette(t):
    a = np.array([0.5, 0.5, 0.5])
    b = np.array([0.45, 0.45, 0.45])
    c = np.array([1.0, 1.0, 1.0])
    d = np.array([0.00, 0.33, 0.67])
    return a + b * np.cos(TAU * (c * t[..., None] + d))


def glass(fx, fy, time, k, fine):
    zoom = 1.25 + 0.2 * fine
    qx, qy = rotate(fx, fy, 0.03 * time + 0.5 * fine)
    qx, qy = qx * zoom, qy * zoom

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
    return col * light[..., None] + (1.0 - np.exp(-0.5 * glow))[..., None] * np.array([1.0, 0.95, 0.85])


def mirrored(t):
    # Metal's mirrored_repeat address mode on normalized coordinates.
    return 1.0 - np.abs(np.mod(t, 2.0) - 1.0)


def sample_linear(image, u, v):
    h, w = image.shape[:2]
    x = mirrored(u) * w - 0.5
    y = mirrored(v) * h - 0.5
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    ax = (x - x0)[..., None]
    ay = (y - y0)[..., None]
    x0c, x1c = np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1)
    y0c, y1c = np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
    top = image[y0c, x0c] * (1 - ax) + image[y0c, x1c] * ax
    bottom = image[y1c, x0c] * (1 - ax) + image[y1c, x1c] * ax
    return top * (1 - ay) + bottom * ay


def camera_view(fx, fy, k, fine, image, angle):
    # Turn the fold's wedge to face the camera's right, so at k = 1 the
    # screen's right half shows the camera's right half as it is.
    zoom = 1.25 + 0.2 * fine
    qx, qy = rotate(fx, fy, 0.5 * fine - 0.5 * np.pi / max(k, 1.0))
    qx, qy = qx * zoom, qy * zoom
    # Level the image: a clockwise turn on screen is a counterclockwise sample.
    qx, qy = rotate(qx, qy, angle)
    qx, qy = qx * CAMERA_SCALE, qy * CAMERA_SCALE
    h, w = image.shape[:2]
    short = min(w, h)
    u = 0.5 + 0.5 * qx * short / w
    v = 0.5 - 0.5 * qy * short / h
    return sample_linear(image, u, v)


def shade(W, H, time, k, fine, camera=None, mix=1.0, angle=0.0):
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5
    s = 0.5 * min(W, H)
    px = (xs - 0.5 * W) / s
    py = -(ys - 0.5 * H) / s
    fx, fy = fold(px, py, max(k, 1.0))

    col = np.zeros((H, W, 3))
    if camera is None or mix < 1.0:
        col = glass(fx, fy, time, k, fine)
    if camera is not None:
        m = mix * mix * (3 - 2 * mix)
        col = col * (1 - m) + camera_view(fx, fy, k, fine, camera, angle) * m

    # Gentle vignette.
    rr = np.hypot(px, py)
    col *= (1.0 - 0.35 * np.clip(rr - 0.6, 0.0, 1.0))[..., None]
    return np.clip(col, 0.0, 1.0)


def test_card():
    """An upright portrait scene, returned sideways as a portrait camera buffer is."""
    w, h = 360, 640
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h // 2):  # sky
        g = y / (h / 2)
        d.line([(0, y), (w, y)], fill=(int(40 + 60 * g), int(90 + 80 * g), int(200 - 20 * g)))
    d.rectangle([0, h // 2, w, h], fill=(60, 120, 50))  # ground
    d.ellipse([w - 120, 40, w - 40, 120], fill=(250, 210, 60))  # sun, top right
    d.polygon([(w // 2, 170), (w // 2 - 50, 260), (w // 2 + 50, 260)], fill=(240, 240, 240))  # arrow up
    d.rectangle([w // 2 - 18, 260, w // 2 + 18, 330], fill=(240, 240, 240))
    d.rectangle([20, h - 120, 130, h - 40], fill=(200, 60, 60))  # red box, bottom left
    # A camera in portrait delivers this a quarter turn counterclockwise,
    # and the capture system asks for a 90 degree clockwise turn.
    return np.asarray(img.rotate(90, expand=True), dtype=np.float32) / 255.0, np.pi / 2


if __name__ == "__main__":
    out = sys.argv[1]
    if len(sys.argv) > 2:
        camera, angle = np.asarray(Image.open(sys.argv[2]).convert("RGB"), dtype=np.float32) / 255.0, 0.0
    else:
        camera, angle = test_card()
    rows = []
    for source in (None, camera):
        tiles = []
        for k, fine in [(1, 0.0), (3, 0.0), (3.5, 0.0), (5, 0.5), (8, -0.5)]:
            img = shade(360, 480, 40.0, k, fine, camera=source, angle=angle)
            tiles.append((img * 255).astype(np.uint8))
        rows.append(np.concatenate(tiles, axis=1))
    Image.fromarray(np.concatenate(rows, axis=0)).save(out)

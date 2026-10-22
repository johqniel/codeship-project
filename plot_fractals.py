import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import hsv_to_rgb

CURVATURE = -0.472135955          # pentagonal tiling of the hyperbolic plane
N, RADIUS, PHASE = 5, 1.0, np.pi * 1.25
RES = 1200
MAX_PERIOD = 200                  # iterations tracked for return time


def regular_ngon(n, radius, phase):
    t = phase + np.arange(n) * 2 * np.pi / n
    return np.stack([radius * np.cos(t), radius * np.sin(t)], axis=1)


def tangent_vertex(verts, px, py):
    best = np.zeros(px.shape, dtype=np.int64)
    for i in range(1, len(verts)):
        ax, ay = verts[best, 0] - px, verts[best, 1] - py
        bx, by = verts[i, 0] - px, verts[i, 1] - py
        turn = ax * by - ay * bx
        take = (turn < 0) | ((turn == 0) & (bx * bx + by * by > ax * ax + ay * ay))
        best = np.where(take, i, best)
    return best


def reflect(pivot, px, py, c):
    vx, vy = pivot[:, 0], pivot[:, 1]
    nv = 1.0 + c * (vx * vx + vy * vy)
    a = 2.0 * (c * (px * vx + py * vy) + 1.0) / nv
    d = a - 1.0
    d = np.where(np.abs(d) < 1e-9, 1e-9, d)
    return (vx * a - px) / d, (vy * a - py) / d


def closeness(ax, ay, bx, by, a_norm, c):
    flat = (ax - bx) ** 2 + (ay - by) ** 2
    nb = 1.0 + c * (bx * bx + by * by)
    cross = ax * by - ay * bx
    inner = 1.0 + c * (ax * bx + ay * by)
    geo = np.sqrt(np.maximum(a_norm * nb, 1e-30))
    return (flat + c * cross ** 2) / (2.0 * geo * (inner + geo))


def in_convex_polygon(verts, px, py):
    inside = np.ones(px.shape, dtype=bool)
    for i in range(len(verts)):
        a, b = verts[i], verts[(i + 1) % len(verts)]
        inside &= (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]) >= 0
    return inside


def period_map(verts, c, res, extent, max_period):
    xs = np.linspace(-extent, extent, res)
    X, Y = np.meshgrid(xs, -xs)                       # Poincare disk coordinates
    disk = 1.0 + 0.25 * c * (X * X + Y * Y) > 0
    k = 1.0 - 0.25 * c * (X * X + Y * Y)
    sx, sy = (X / k)[disk], (Y / k)[disk]             # to Klein, where hops are straight
    table = in_convex_polygon(verts, sx, sy)

    s_norm = 1.0 + c * (sx * sx + sy * sy)
    px, py = sx.copy(), sy.copy()
    best = np.full(sx.shape, np.inf)
    hop = np.zeros(sx.shape, dtype=np.int64)
    for step in range(1, max_period + 1):
        piv = verts[tangent_vertex(verts, px, py)]
        px, py = reflect(piv, px, py, c)
        back = closeness(sx, sy, px, py, s_norm, c)
        better = back < best
        best = np.where(better, back, best)
        hop = np.where(better, step, hop)

    out = np.zeros(X.shape + (3,))
    hue = (hop * 0.03) % 1.0
    rgb = hsv_to_rgb(np.stack([hue, np.full_like(hue, 0.85), np.full_like(hue, 0.95)], axis=1))
    rgb[table] = (0x1A / 255, 0x8B / 255, 0x3A / 255)
    out[disk] = rgb
    return out


if __name__ == "__main__":
    verts = regular_ngon(N, RADIUS, PHASE)
    extent = 2.0 / np.sqrt(-CURVATURE) * 1.02          # just outside the Poincare disk
    img = period_map(verts, CURVATURE, RES, extent, MAX_PERIOD)
    plt.figure(figsize=(8, 8))
    plt.imshow(img, extent=[-extent, extent, -extent, extent])
    plt.axis("off")
    plt.savefig("hyperbolic_billiards.png", dpi=200, bbox_inches="tight", facecolor="black")
    plt.show()
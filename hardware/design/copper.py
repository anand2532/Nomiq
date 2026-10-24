"""Copper geometry for the Nomiq PCB tools: pads, tracks, vias, holes as exact shapes + distance queries.

Shapes:  ("poly", [(x, y), ...])  convex polygon
         ("cap", x1, y1, x2, y2, r) capsule (track / oval pad)
         ("circ", x, y, r)
Used by gen_pcb.py (rasterising for the router) and pcb_check.py (DRC / connectivity on the written file).
"""
from __future__ import annotations

import math

CU = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]


class Item:
    __slots__ = ("kind", "layers", "net", "shape", "owner", "bbox", "ref")

    def __init__(self, kind, layers, net, shape, owner=""):
        self.kind, self.layers, self.net, self.shape, self.owner = kind, frozenset(layers), net, shape, owner
        self.bbox = shape_bbox(shape)


def shape_bbox(s):
    if s[0] == "poly":
        xs, ys = [p[0] for p in s[1]], [p[1] for p in s[1]]
        return (min(xs), min(ys), max(xs), max(ys))
    if s[0] == "cap":
        _, x1, y1, x2, y2, r = s
        return (min(x1, x2) - r, min(y1, y2) - r, max(x1, x2) + r, max(y1, y2) + r)
    _, x, y, r = s
    return (x - r, y - r, x + r, y + r)


def rot(x, y, a):
    r = math.radians(a)
    return (x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r))


def pad_shape(shape, x, y, ang, w, h):
    """world shape of a pad centred at (x, y) with total rotation ang"""
    if shape == "circle":
        return ("circ", x, y, w / 2)
    if shape == "oval":
        if abs(w - h) < 1e-9:
            return ("circ", x, y, w / 2)
        if w > h:
            dx, dy = rot((w - h) / 2, 0, ang)
            return ("cap", x - dx, y - dy, x + dx, y + dy, h / 2)
        dx, dy = rot(0, (h - w) / 2, ang)
        return ("cap", x - dx, y - dy, x + dx, y + dy, w / 2)
    pts = []
    for px, py in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
        dx, dy = rot(px, py, ang)
        pts.append((x + dx, y + dy))
    return ("poly", pts)


# ---------------------------------------------------------------- distances
def _seg_pt(ax, ay, bx, by, px, py):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    qx, qy = ax + t * dx, ay + t * dy
    return math.hypot(px - qx, py - qy)


def _seg_seg(a, b, c, d):
    if _intersect(a, b, c, d):
        return 0.0
    return min(_seg_pt(*a, *b, *c), _seg_pt(*a, *b, *d), _seg_pt(*c, *d, *a), _seg_pt(*c, *d, *b))


def _orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _intersect(a, b, c, d):
    d1, d2, d3, d4 = _orient(c, d, a), _orient(c, d, b), _orient(a, b, c), _orient(a, b, d)
    return ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)) and d1 != 0 and d2 != 0 and d3 != 0 and d4 != 0


def _in_poly(pts, p):
    s = None
    n = len(pts)
    for i in range(n):
        o = _orient(pts[i], pts[(i + 1) % n], p)
        if abs(o) < 1e-12:
            continue
        if s is None:
            s = o > 0
        elif (o > 0) != s:
            return False
    return True


def _poly_seg(pts, a, b):
    if _in_poly(pts, a) or _in_poly(pts, b):
        return 0.0
    n = len(pts)
    return min(_seg_seg(pts[i], pts[(i + 1) % n], a, b) for i in range(n))


def _poly_poly(p, q):
    if _in_poly(p, q[0]) or _in_poly(q, p[0]):
        return 0.0
    n = len(q)
    return min(_poly_seg(p, q[i], q[(i + 1) % n]) for i in range(n))


def _core(s):
    """shape -> (kind, geometry, radius) with capsule/circle reduced to segment/point + radius"""
    if s[0] == "poly":
        return "poly", s[1], 0.0
    if s[0] == "cap":
        return "seg", ((s[1], s[2]), (s[3], s[4])), s[5]
    return "seg", ((s[1], s[2]), (s[1], s[2])), s[3]


def distance(s1, s2) -> float:
    """edge-to-edge distance between two shapes (0 if touching/overlapping)"""
    k1, g1, r1 = _core(s1)
    k2, g2, r2 = _core(s2)
    if k1 == "poly" and k2 == "poly":
        d = _poly_poly(g1, g2)
    elif k1 == "poly":
        d = _poly_seg(g1, *g2)
    elif k2 == "poly":
        d = _poly_seg(g2, *g1)
    else:
        d = _seg_seg(g1[0], g1[1], g2[0], g2[1])
    return max(0.0, d - r1 - r2)


class Grid:
    """bucket index for candidate pairs"""

    def __init__(self, cell=2.0):
        self.cell, self.b = cell, {}

    def add(self, i, bb):
        c = self.cell
        for gx in range(int(math.floor(bb[0] / c)), int(math.floor(bb[2] / c)) + 1):
            for gy in range(int(math.floor(bb[1] / c)), int(math.floor(bb[3] / c)) + 1):
                self.b.setdefault((gx, gy), []).append(i)

    def query(self, bb, m=0.0):
        c, out = self.cell, set()
        for gx in range(int(math.floor((bb[0] - m) / c)), int(math.floor((bb[2] + m) / c)) + 1):
            for gy in range(int(math.floor((bb[1] - m) / c)), int(math.floor((bb[3] + m) / c)) + 1):
                out.update(self.b.get((gx, gy), ()))
        return out

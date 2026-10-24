#!/usr/bin/env python3
"""Generate the Nomiq A0 PCB (KiCad 8 .kicad_pcb) from nomiq_hw.py + fplib.py + placement.py.

    python3 hardware/design/gen_pcb.py            # place, fan out, route, write hardware/kicad/nomiq/nomiq.kicad_pcb
    python3 hardware/design/pcb_check.py          # independent DRC + connectivity check of the written file

Flow
  1. placement.build() + legalize()  (F side = electronics/back, B side = UI/front; see board.py)
  2. pads -> exact copper shapes; NPTH, keep-outs, board edge
  3. RF nets routed first on F.Cu only (no vias), then plane fan-out: every GND pad gets a via to
     In1.Cu (GND plane), every V3V3 pad a via to In2.Cu (3V3 plane); EP pads get via-in-pad arrays
  4. all other nets: grid A* (0.05 mm grid, F.Cu + B.Cu, vias), multi-terminal by tree growth,
     any-angle string pulling, then power/supply tracks widened to their net-class width where clearance allows
  5. zones: In1 GND, In2 3V3, GND pours on F/B (filled by KiCad: `make -C hardware fill` / CI)
The router keeps ROUTER_CLEARANCE (0.15 mm) + raster margin; the board rule is 0.127 mm.
"""
from __future__ import annotations

import ctypes
import json
import math
import os
import subprocess
import sys
import time
import uuid as _uuid

import numpy as np
import cv2

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import board as B  # noqa: E402
import copper as CP  # noqa: E402
import fplib  # noqa: E402
import nomiq_hw as HW  # noqa: E402
import placement as PM  # noqa: E402

OUT = os.path.join(B.ROOT, "hardware", "kicad", "nomiq")
NS = _uuid.UUID("2d4c1f0e-5b7a-4e21-8c3d-6e6f6d697133")
RES = 0.05
MARGIN = 0.10                     # raster + path discretisation allowance
X0, Y0 = -B.HX - 0.25, -B.HY - 0.25
W = int(math.ceil((2 * B.HX + 0.5) / RES))
H = int(math.ceil((2 * B.HY + 0.5) / RES))
OBST = 65535
LAYER_IDX = {"F.Cu": 0, "B.Cu": 1}
VIA_R = B.VIA_D / 2


def uid(*a):
    return str(_uuid.uuid5(NS, "/".join(map(str, a))))


# ------------------------------------------------------------------ nets
NETS = sorted(HW.NETS)
NETNUM = {n: i + 1 for i, n in enumerate(NETS)}
PIN_NET = {m: n for n, mem in HW.NETS.items() for m in mem}


def pad_net(ref, padnum):
    p = HW.PARTS.get(ref)
    if p is None:
        return ""
    for pin, pads in p.pads.items():
        if padnum in pads:
            return PIN_NET.get(f"{ref}.{pin}", "")
    return ""


# ------------------------------------------------------------------ raster helpers
def px(x, y):
    return ((x - X0) / RES - 0.5, (y - Y0) / RES - 0.5)


def cell_xy(ix, iy):
    return (X0 + (ix + 0.5) * RES, Y0 + (iy + 0.5) * RES)


def draw(img, shape, val, grow=0.0):
    S = 16
    if shape[0] == "poly":
        pts = shape[1]
        if grow:
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            # grow polygon by drawing it plus capsules along its edges
            for i in range(len(pts)):
                a, b = pts[i], pts[(i + 1) % len(pts)]
                draw(img, ("cap", a[0], a[1], b[0], b[1], grow), val)
        arr = np.array([[int(round(px(*p)[0] * S)), int(round(px(*p)[1] * S))] for p in pts], np.int32)
        cv2.fillPoly(img, [arr], int(val), lineType=cv2.LINE_8, shift=4)
    elif shape[0] == "circ":
        _, x, y, r = shape
        cx, cy = px(x, y)
        cv2.circle(img, (int(round(cx * S)), int(round(cy * S))), max(1, int(round((r + grow) / RES * S))), int(val), -1,
                   lineType=cv2.LINE_8, shift=4)
    else:
        _, x1, y1, x2, y2, r = shape
        r += grow
        draw(img, ("circ", x1, y1, r), val)
        draw(img, ("circ", x2, y2, r), val)
        L = math.hypot(x2 - x1, y2 - y1)
        if L > 1e-9:
            nx, ny = -(y2 - y1) / L * r, (x2 - x1) / L * r
            draw(img, ("poly", [(x1 + nx, y1 + ny), (x2 + nx, y2 + ny), (x2 - nx, y2 - ny), (x1 - nx, y1 - ny)]), val)


def edge_distance():
    """signed distance (mm) of every cell centre to the board outline (positive inside)"""
    xs = X0 + (np.arange(W) + 0.5) * RES
    ys = Y0 + (np.arange(H) + 0.5) * RES
    X, Y = np.meshgrid(xs, ys)
    r = B.BOARD_R
    qx = np.abs(X) - (B.HX - r)
    qy = np.abs(Y) - (B.HY - r)
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r
    return -outside


# ------------------------------------------------------------------ router core
_lib = None


def lib():
    global _lib
    if _lib is None:
        src = os.path.join(HERE, "router", "astar.c")
        so = os.path.join(HERE, "router", "_astar.so")
        if not os.path.exists(so) or os.path.getmtime(so) < os.path.getmtime(src):
            subprocess.check_call(["cc", "-O2", "-shared", "-fPIC", "-o", so, src])
        _lib = ctypes.CDLL(so)
        P8 = np.ctypeslib.ndpointer(np.uint8, flags="C")
        P32 = np.ctypeslib.ndpointer(np.int32, flags="C")
        P16 = np.ctypeslib.ndpointer(np.uint16, flags="C")
        _lib.astar.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, P8, P8, P8, P8, P32, ctypes.c_int32, P16, P32,
                               ctypes.c_int, ctypes.c_int64]
        _lib.astar.restype = ctypes.c_int
    return _lib


class Board:
    def __init__(self):
        self.pl = PM.build()
        self.log = []
        PM.legalize(self.pl, self.log)
        errs = PM.check(self.pl)
        if errs:
            raise SystemExit("placement errors:\n  " + "\n  ".join(errs))
        self.items: list[CP.Item] = []      # all copper (pads, tracks, vias) + holes
        self.tracks = []                    # (x1, y1, x2, y2, w, layer, net)
        self.vias = []                      # (x, y, net)
        self.lab = np.zeros((2, H, W), np.uint16)
        self.lab2 = np.zeros((2, H, W), np.uint16)   # wide-clearance halo copy
        self.keep = np.zeros((2, H, W), bool)        # track keep-outs
        self.viakeep = np.zeros((H, W), bool)
        self.smd = np.zeros((H, W), np.uint8)        # any SMD pad (no vias in pads)
        self.edge = edge_distance()
        self.holes = np.zeros((H, W), np.uint8)      # NPTH (tracks + vias keep 0.25 mm)
        self.drills = np.zeros((H, W), np.uint8)     # PTH drills (vias keep 0.25 mm hole-to-hole)
        self.pads = {}                               # (ref, padnum) -> list of items
        self.failed = []
        self.hist = np.zeros((2, H, W), np.uint16)
        self.net_copper = {}                         # net -> [items] added by routing (rip-up)
        self._pads()
        self.lab_pads, self.lab2_pads = self.lab.copy(), self.lab2.copy()
        # static cost: keep tracks off the front of other pads (fine-pitch escape lanes)
        padm = (self.lab_pads != 0).astype(np.uint8)
        for l in (0, 1):
            d = cv2.distanceTransform((1 - padm[l]).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE) * RES
            self.hist[l] = np.where(d < 0.6, 6, 0).astype(np.uint16)
        self.hist_base = self.hist.copy()

    # -------------------------------------------------------------- pads, holes, keep-outs
    def _pads(self):
        for ref, pl in self.pl.items():
            for i, p in enumerate(pl.fp.pads):
                num, kind, shape, *_ = p
                x, y, ang = pl.pad_world(p)
                layers = fplib.lay(p[9])
                w, h = p[6], p[7]
                if kind == "np_thru_hole":
                    d = float(p[8].split()[-1]) if p[8] else w
                    it = CP.Item("hole", CP.CU, "", ("circ", x, y, d / 2), owner=ref)
                    self.items.append(it)
                    draw(self.holes, it.shape, 1)
                    for l in (0, 1):
                        draw(self.lab[l], ("circ", x, y, d / 2 + 0.1), OBST)
                    continue
                if "Cu" not in layers:
                    continue
                if kind in ("thru_hole",):
                    lays = CP.CU
                    dd = [t for t in p[8].split() if t != "oval"]
                    draw(self.drills, ("circ", x, y, max(float(v) for v in dd) / 2), 1)
                else:
                    lays = ["F.Cu" if pl.side == "F" else "B.Cu"]
                net = pad_net(ref, num) if ref in HW.PARTS else ""
                if shape == "custom":       # mic GND ring: conservative solid disc
                    shp = ("circ", *pl.world(0.6625 - 0.6625, 0.71), 0.6625 + 0.15)
                    cx, cy = pl.world(0, 0.71)
                    shp = ("circ", cx, cy, 0.8125)
                else:
                    shp = CP.pad_shape(shape, x, y, ang, w, h)
                it = CP.Item("pad", lays, net, shp, owner=f"{ref}.{num}")
                self.items.append(it)
                self.pads.setdefault((ref, num), []).append(it)
                self._raster(it)
                if kind == "smd":
                    draw(self.smd, shp, 1)
            for name, pts, layers in pl.fp.keepouts:
                wpts = [pl.world(x, y) for x, y in pts]
                ls = (0, 1) if layers == "all" else ((0,) if (layers == "F.Cu") == (pl.side == "F") else (1,))
                for l in ls:
                    m = np.zeros((H, W), np.uint8)
                    draw(m, ("poly", wpts), 1)
                    self.keep[l] |= m.astype(bool)
                m = np.zeros((H, W), np.uint8)
                draw(m, ("poly", wpts), 1)
                self.viakeep |= m.astype(bool)

    def _raster(self, it: CP.Item):
        val = NETNUM.get(it.net, OBST) if it.net else OBST
        for l, name in ((0, "F.Cu"), (1, "B.Cu")):
            if name in it.layers:
                draw(self.lab[l], it.shape, val)
                cls, _, clr = B.netclass(it.net) if it.net else ("", 0, B.ROUTER_CLEARANCE)
                if clr > B.ROUTER_CLEARANCE:
                    draw(self.lab2[l], it.shape, val, grow=clr - B.ROUTER_CLEARANCE)

    # -------------------------------------------------------------- masks
    def dist_foreign(self, k):
        out = []
        for l in (0, 1):
            f = ((self.lab[l] != 0) & (self.lab[l] != k)) | ((self.lab2[l] != 0) & (self.lab2[l] != k))
            d = cv2.distanceTransform((~f).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE) * RES
            out.append(d)
        return out

    def _static(self):
        if not hasattr(self, "_hole_d"):
            dt = lambda m: cv2.distanceTransform((1 - m).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE) * RES
            self._hole_d = dt(self.holes)
            self._smd_d = dt(self.smd)
            self._drill_d = dt(self.drills)
            fine = np.zeros((H, W), np.uint8)
            for its in self.pads.values():
                for it in its:
                    if self.fine(it):
                        draw(fine, it.shape, 1)
            self._fine_d = dt(fine)        # keep vias out of fine-pitch escape lanes
        return self._hole_d, self._smd_d

    def via_drill_d(self):
        """distance to the nearest PTH drill or via drill (recomputed as vias are added)"""
        m = self.drills.copy()
        for x, y, _ in self.vias:
            draw(m, ("circ", x, y, B.VIA_DRILL / 2), 1)
        return cv2.distanceTransform((1 - m).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE) * RES

    def masks(self, net, w, dist=None, clr_override=None, in_pad=False):
        k = NETNUM[net]
        _, _, clr = B.netclass(net)
        clr = max(clr, B.ROUTER_CLEARANCE) if clr_override is None else max(clr, clr_override)
        dist = dist or self.dist_foreign(k)
        hole_d, smd_d = self._static()
        blocked = np.zeros((2, H, W), np.uint8)
        for l in (0, 1):
            blocked[l] = ((dist[l] < clr + w / 2 + MARGIN) | self.keep[l] | (self.edge < B.EDGE_CLEARANCE + w / 2 + 0.05)
                          | (hole_d < 0.25 + w / 2 + MARGIN)).astype(np.uint8)
        dd = self.via_drill_d()
        viaok = ((dist[0] >= VIA_R + clr + MARGIN) & (dist[1] >= VIA_R + clr + MARGIN) & ~self.viakeep
                 & (self.edge >= B.EDGE_CLEARANCE + VIA_R + 0.05) & (hole_d >= VIA_R + 0.25 + MARGIN)
                 & (dd >= B.VIA_DRILL / 2 + 0.25 + MARGIN))
        if not in_pad:
            viaok &= smd_d >= VIA_R + 0.05
            viaok &= self._fine_d >= 0.9
        return blocked, viaok.astype(np.uint8), dist

    # -------------------------------------------------------------- geometry output
    def add_track(self, x1, y1, x2, y2, w, layer, net):
        t = [x1, y1, x2, y2, w, layer, net]
        self.tracks.append(t)
        it = CP.Item("track", [layer], net, ("cap", x1, y1, x2, y2, w / 2), owner=f"track:{net}")
        it.ref = t
        self.items.append(it)
        self.net_copper.setdefault(net, []).append(it)
        self._raster(it)
        return it

    def add_via(self, x, y, net):
        v = (x, y, net)
        self.vias.append(v)
        it = CP.Item("via", CP.CU, net, ("circ", x, y, VIA_R), owner=f"via:{net}")
        it.ref = v
        self.items.append(it)
        self.net_copper.setdefault(net, []).append(it)
        self._raster(it)
        return it

    def rip(self, net):
        """remove all routed copper of a net (except hand-drawn copper) and rebuild the rasters"""
        locked = getattr(self, "locked", set())
        allc = self.net_copper.pop(net, [])
        keep = [i for i in allc if id(i) in locked]
        gone = [i for i in allc if id(i) not in locked]
        if keep:
            self.net_copper[net] = keep
        if not gone:
            return
        ids = {id(i) for i in gone}
        self.items = [i for i in self.items if id(i) not in ids]
        tids = {id(i.ref) for i in gone if i.kind == "track"}
        self.tracks = [t for t in self.tracks if id(t) not in tids]
        vset = {id(i.ref) for i in gone if i.kind == "via"}
        self.vias = [v for v in self.vias if id(v) not in vset]
        self.rebuild()

    def rebuild(self):
        self.lab, self.lab2 = self.lab_pads.copy(), self.lab2_pads.copy()
        for its in self.net_copper.values():
            for it in its:
                self._raster(it)

    def blockers_of(self, net, src_items, dst_items, w):
        """route against pads only; return the nets whose routed copper lies on that path"""
        k = NETNUM[net]
        save = (self.lab, self.lab2)
        self.lab, self.lab2 = self.lab_pads, self.lab2_pads
        try:
            res_cells = self.route(net, src_items, dst_items, w, dry=True)
        finally:
            self.lab, self.lab2 = save
        if res_cells is None:
            return None
        l, ix, iy = res_cells
        m = np.zeros((2, H, W), np.uint8)
        m[l, iy, ix] = 1
        r = int(math.ceil((B.ROUTER_CLEARANCE + w / 2 + MARGIN + 0.3) / RES))
        ker = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * r + 1, 2 * r + 1))
        nets = set()
        for li in (0, 1):
            d = cv2.dilate(m[li], ker).astype(bool)
            labs = np.unique(self.lab[li][d])
            for v in labs:
                if v and v != k and v != OBST and NETS[v - 1] in self.net_copper:
                    nets.add(NETS[v - 1])
            self.hist[li][d] = np.minimum(self.hist[li][d].astype(np.int32) + 4, 400).astype(np.uint16)
        return nets

    # -------------------------------------------------------------- terminals
    def pad_cells(self, items, blocked):
        src = np.zeros((2, H, W), np.uint8)
        for it in items:
            for l, name in ((0, "F.Cu"), (1, "B.Cu")):
                if name in it.layers:
                    draw(src[l], it.shape, 1)
        return src & (1 - blocked)

    # -------------------------------------------------------------- one connection
    def route(self, net, src_items, dst_items, w, vias=True, lcost=(14, 10), via_cost=300, max_expand=6_000_000,
              layers=(0, 1), clr_override=None, dry=False):
        blocked, viaok, _ = self.masks(net, w, clr_override=clr_override)
        for l in (0, 1):
            if l not in layers:
                blocked[l] = 1
        if not vias:
            viaok[:] = 0
        src = self.pad_cells(src_items, blocked)
        dst = self.pad_cells(dst_items, blocked)
        if not src.any() or not dst.any():
            return None
        ov = src & dst
        if ov.any():
            if any(self._connected(src_items, [d]) for d in dst_items):
                return []
            l, iy, ix = [int(v[0]) for v in np.nonzero(ov)]
            x, y = cell_xy(ix, iy)
            d = dst_items[0]
            tx, ty = (d.bbox[0] + d.bbox[2]) / 2, (d.bbox[1] + d.bbox[3]) / 2
            return [self.add_track(x, y, tx, ty, min(w, 0.15), "F.Cu" if l == 0 else "B.Cu", net)]
        out = np.zeros(400000, np.int32)
        n = lib().astar(W, H, 2, np.ascontiguousarray(blocked.reshape(-1)), np.ascontiguousarray(viaok.reshape(-1)),
                        np.ascontiguousarray(src.reshape(-1)), np.ascontiguousarray(dst.reshape(-1)),
                        np.array(lcost, np.int32), via_cost, np.ascontiguousarray(self.hist.reshape(-1)), out, out.size,
                        max_expand)
        if n <= 0:
            return None
        cells = out[:n][::-1]
        l = cells // (W * H)
        rem = cells % (W * H)
        iy, ix = rem // W, rem % W
        if dry:
            return (l, ix, iy)
        return self._emit(net, w, l, ix, iy, blocked, src_items, dst_items)

    def _visible(self, blk, a, b):
        (x1, y1), (x2, y2) = a, b
        n = int(max(abs(x2 - x1), abs(y2 - y1)) * 2) + 2
        xs = np.rint(np.linspace(x1, x2, n)).astype(int)
        ys = np.rint(np.linspace(y1, y2, n)).astype(int)
        if (xs < 0).any() or (ys < 0).any() or (xs >= W).any() or (ys >= H).any():
            return False
        return not blk[ys, xs].any()

    def _emit(self, net, w, l, ix, iy, blocked, src_items, dst_items):
        new = []
        runs = []
        s = 0
        for i in range(1, len(l) + 1):
            if i == len(l) or l[i] != l[s]:
                runs.append((int(l[s]), list(zip(ix[s:i].tolist(), iy[s:i].tolist()))))
                s = i
        for ri, (layer, pts) in enumerate(runs):
            blk = blocked[layer]
            # string pulling
            keep = [pts[0]]
            i = 0
            while i < len(pts) - 1:
                j = i + 1
                while j + 1 < len(pts) and self._visible(blk, pts[i], pts[j + 1]):
                    j += 1
                keep.append(pts[j])
                i = j
            mm = [cell_xy(*p) for p in keep]
            lname = "F.Cu" if layer == 0 else "B.Cu"
            if len(mm) == 1:
                mm = mm * 2
            for a, b in zip(mm[:-1], mm[1:]):
                if a != b:
                    new.append(self.add_track(a[0], a[1], b[0], b[1], w, lname, net))
            if ri < len(runs) - 1:
                x, y = cell_xy(*pts[-1])
                new.append(self.add_via(x, y, net))
        return new

    # -------------------------------------------------------------- nets
    def net_pads(self, net):
        """one terminal per copper pad item (duplicate pad numbers are connected explicitly)"""
        out = []
        for (ref, num), its in self.pads.items():
            if its[0].net != net:
                continue
            groups = []
            for it in its:          # pads of one number that overlap (composite pads) form one terminal
                for g in groups:
                    if any(CP.distance(it.shape, o.shape) <= 1e-6 and it.layers & o.layers for o in g):
                        g.append(it)
                        break
                else:
                    groups.append([it])
            for g in groups:
                out.append((ref, num, g))
        return out

    def route_net(self, net, w, **kw):
        self.cur_fail = []
        pads = self.net_pads(net)
        if len(pads) < 2:
            return True
        # tree growth from the pad nearest the centroid of the net
        cx = sum(i[2][0].bbox[0] + i[2][0].bbox[2] for i in pads) / (2 * len(pads))
        cy = sum(i[2][0].bbox[1] + i[2][0].bbox[3] for i in pads) / (2 * len(pads))
        pads.sort(key=lambda t: math.hypot((t[2][0].bbox[0] + t[2][0].bbox[2]) / 2 - cx, (t[2][0].bbox[1] + t[2][0].bbox[3]) / 2 - cy))
        existing = list(self.net_copper.get(net, []))
        if existing:
            # grow the connected component of existing copper and the pads touching it
            tree = existing[:]
            changed = True
            todo = pads[:]
            while changed:
                changed = False
                for t in todo[:]:
                    if self._connected(tree, t[2]):
                        tree += t[2]
                        todo.remove(t)
                        changed = True
            if not any(i.kind == "pad" for i in tree):
                tree = list(todo[0][2]) + existing
                todo = todo[1:]
        else:
            tree = list(pads[0][2])
            todo = pads[1:]
        ok = True
        while todo:
            # nearest remaining pad to the tree
            def dist(t):
                b = t[2][0].bbox
                return min(math.hypot((b[0] + b[2]) / 2 - (it.bbox[0] + it.bbox[2]) / 2, (b[1] + b[3]) / 2 - (it.bbox[1] + it.bbox[3]) / 2) for it in tree)
            todo.sort(key=dist)
            ref, num, its = todo.pop(0)
            if self._connected(tree, its):
                tree += its
                continue
            res = self.route(net, tree, its, w, **kw)
            if res is None and w > B.DEFAULT_TRACK:
                res = self.route(net, tree, its, B.DEFAULT_TRACK, **kw)
            if res is None:
                res = self.route(net, tree, its, B.DEFAULT_TRACK, via_cost=120, max_expand=20_000_000,
                                 **{k: v for k, v in kw.items() if k not in ("via_cost", "max_expand")})
            if res is None:
                res = self.route(net, tree, its, 0.127, via_cost=120, max_expand=30_000_000, clr_override=B.CLEARANCE,
                                 **{k: v for k, v in kw.items() if k not in ("via_cost", "max_expand")})
            if res is None:
                blk, _, _ = self.masks(net, 0.127, clr_override=B.CLEARANCE)
                why = "pad enclosed" if not self.pad_cells(its, blk).any() else (
                    "tree enclosed" if not self.pad_cells(tree, blk).any() else "no path")
                self.failed.append(f"{net}: {ref}.{num} ({why})")
                self.cur_fail.append((tree[:], its))
                ok = False
                tree += its     # keep going with the rest
                continue
            tree += res + its
        return ok

    def _connected(self, tree, its):
        for a in its:
            for b in tree:
                if a.layers & b.layers and CP.distance(a.shape, b.shape) <= 1e-6:
                    return True
        return False

    # -------------------------------------------------------------- plane fan-out
    def _same_net_ep(self, ref, net):
        part = HW.PARTS.get(ref)
        if not part or "EP" not in part.pads and ref != "U1":
            return None
        for num in part.pads.get("EP", []):
            its = self.pads.get((ref, num))
            if its and its[0].net == net:
                return max(its, key=lambda i: (i.bbox[2] - i.bbox[0]) * (i.bbox[3] - i.bbox[1]))
        return None

    def plane_set(self, net):
        """ids of copper items (pads, tracks, vias) of `net` connected to one of its vias"""
        items = [i for i in self.net_copper.get(net, [])] + [i for v in self.pads.values() for i in v if i.net == net]
        grid = CP.Grid(1.0)
        for k, it in enumerate(items):
            grid.add(k, it.bbox)
        parent = list(range(len(items)))

        def f(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x
        for k, a in enumerate(items):
            for j in grid.query(a.bbox, 0.01):
                if j > k and a.layers & items[j].layers and CP.distance(a.shape, items[j].shape) <= 1e-6:
                    parent[f(k)] = f(j)
        roots = {f(k) for k, it in enumerate(items) if it.kind == "via" or "In1.Cu" in it.layers}
        return {id(it) for k, it in enumerate(items) if f(k) in roots}

    def _pad_dir(self, ref, it):
        pl = self.pl[ref]
        cx, cy = (it.bbox[0] + it.bbox[2]) / 2, (it.bbox[1] + it.bbox[3]) / 2
        dx, dy = cx - pl.x, cy - pl.y
        L = math.hypot(dx, dy)
        return (dx / L, dy / L) if L > 1e-6 else (0.0, 0.0)

    @staticmethod
    def fine(it):
        return min(it.bbox[2] - it.bbox[0], it.bbox[3] - it.bbox[1]) < 0.5

    def fanout(self, net, which="coarse"):
        """GND/V3V3 pads -> plane via.  which: 'coarse' (before signal routing) or 'fine' (after)."""
        w = 0.2
        n_ok = n_inpad = 0
        dirty = True
        on_plane = self.plane_set(net)
        for ref, num, its in self.net_pads(net):
            it = max(its, key=lambda i: (i.bbox[2] - i.bbox[0]) * (i.bbox[3] - i.bbox[1]))
            if "In1.Cu" in it.layers:      # THT: already on the plane
                continue
            if (which == "fine") != self.fine(it):
                continue
            if any(id(i) in on_plane for i in its):
                continue                     # already reaches a via / the plane (hand-drawn or earlier pass)
            bx0, by0, bx1, by1 = it.bbox
            pcx, pcy = (bx0 + bx1) / 2, (by0 + by1) / 2
            part = HW.PARTS.get(ref)
            pin = next((pn for pn, pads in part.pads.items() if num in pads), "") if part else ""
            is_ep = (pin == "EP" or (ref, num) == ("U1", "41")) and (bx1 - bx0) >= 0.85 and (by1 - by0) >= 0.85
            placed_ep = False
            if is_ep:
                _, viaok_p, _ = self.masks(net, w, in_pad=True)
                nx = max(1, int((bx1 - bx0 - 2 * VIA_R - 0.2) // 1.0) + 1)
                ny = max(1, int((by1 - by0 - 2 * VIA_R - 0.2) // 1.0) + 1)
                for i in range(nx):
                    for j in range(ny):
                        x = pcx + (i - (nx - 1) / 2) * 1.0
                        y = pcy + (j - (ny - 1) / 2) * 1.0
                        ix, iy = int((x - X0) / RES), int((y - Y0) / RES)
                        if viaok_p[iy, ix]:
                            self.add_via(x, y, net)
                            _, viaok_p, _ = self.masks(net, w, in_pad=True)
                            n_inpad += 1
                            placed_ep = True
                dirty = True
                if placed_ep:
                    continue
            if dirty:
                blocked, viaok, _ = self.masks(net, w)
                dirty = False
            # fine-pitch ground/supply pin next to an exposed pad of the same net: tie it inward to the EP
            ep = self._same_net_ep(ref, net)
            if ep is not None and self.fine(it):
                # straight in, perpendicular to the pin row, to just inside the EP edge
                ex = min(max(pcx, ep.bbox[0] + 0.15), ep.bbox[2] - 0.15)
                ey = min(max(pcy, ep.bbox[1] + 0.15), ep.bbox[3] - 0.15)
                layer = 0 if "F.Cu" in it.layers else 1
                if self._visible(blocked[layer], px(pcx, pcy), px(ex, ey)):
                    self.add_track(pcx, pcy, ex, ey, w, "F.Cu" if layer == 0 else "B.Cu", net)
                    n_ok += 1
                    dirty = True
                    continue
            ux, uy = self._pad_dir(ref, it)
            r0 = max(bx1 - bx0, by1 - by0) / 2 + VIA_R + 0.05
            cands = []
            for rad in np.arange(r0, r0 + 2.5, 0.1):
                for ang in range(0, 360, 10):
                    cx_, cy_ = math.cos(math.radians(ang)), math.sin(math.radians(ang))
                    # prefer outward (away from the part body), then distance
                    cost = rad + 0.8 * (1 - (cx_ * ux + cy_ * uy))
                    cands.append((cost, pcx + rad * cx_, pcy + rad * cy_))
            cands.sort()
            layer = 0 if "F.Cu" in it.layers else 1
            placed = False
            for _, x, y in cands:
                ix, iy = int((x - X0) / RES), int((y - Y0) / RES)
                if not (0 <= ix < W and 0 <= iy < H) or not viaok[iy, ix]:
                    continue
                if not self._visible(blocked[layer], px(pcx, pcy), px(*cell_xy(ix, iy))):
                    continue
                vx, vy = cell_xy(ix, iy)
                self.add_track(pcx, pcy, vx, vy, w, "F.Cu" if layer == 0 else "B.Cu", net)
                self.add_via(vx, vy, net)
                placed = True
                break
            if not placed:
                placed = self.fanout_astar(net, its, w)
                if not placed:
                    anchors = [i for i in self.items if i.net == net and (i.kind in ("via", "track") or "In1.Cu" in i.layers)]
                    res = self.route(net, its, anchors, 0.127, clr_override=B.CLEARANCE) if anchors else None
                    if res is None:
                        self.failed.append(f"{net} fanout: {ref}.{num}")
                        continue
            n_ok += 1
            dirty = True
        self.log.append(f"fanout {net} ({which}): {n_ok} pads, {n_inpad} vias in EP")

    def fanout_astar(self, net, its, w):
        """bent stub: A* from the pad to any cell where a via of this net fits, then drop the via there"""
        for width, clr in ((w, None), (0.127, B.CLEARANCE)):
            blocked, viaok, _ = self.masks(net, width, clr_override=clr)
            src = self.pad_cells(its, blocked)
            if not src.any():
                continue
            layer = 0 if src[0].any() else 1
            dst = np.zeros((2, H, W), np.uint8)
            dst[layer] = viaok & (1 - blocked[layer])
            # not right on top of the pad itself
            dst[layer] &= (1 - src[layer])
            out = np.zeros(200000, np.int32)
            noviaok = np.zeros((H, W), np.uint8)
            n = lib().astar(W, H, 2, np.ascontiguousarray(blocked.reshape(-1)), noviaok.reshape(-1).copy(),
                            np.ascontiguousarray(src.reshape(-1)), np.ascontiguousarray(dst.reshape(-1)),
                            np.array((10, 10), np.int32), 100000, np.ascontiguousarray(self.hist_base.reshape(-1)), out,
                            out.size, 3_000_000)
            if n <= 0:
                continue
            cells = out[:n][::-1]
            l = cells // (W * H)
            rem = cells % (W * H)
            iy, ix = rem // W, rem % W
            self._emit(net, width, l, ix, iy, blocked, its, [])
            vx, vy = cell_xy(int(ix[-1]), int(iy[-1]))
            self.add_via(vx, vy, net)
            return True
        return False

    # -------------------------------------------------------------- widen power tracks
    def widen(self):
        n = 0
        grid = CP.Grid(2.0)
        for i, it in enumerate(self.items):
            grid.add(i, it.bbox)
        for ti, t in enumerate(self.tracks):
            x1, y1, x2, y2, w, layer, net = t
            cls, cw, clr = B.netclass(net)
            if cw <= w + 1e-9:
                continue
            shp = ("cap", x1, y1, x2, y2, cw / 2)
            bb = CP.shape_bbox(shp)
            ok = True
            for j in grid.query(bb, 0.5):
                o = self.items[j]
                if o.net == net or layer not in o.layers:
                    continue
                c = max(clr, B.netclass(o.net)[2] if o.net else B.CLEARANCE) + 0.02
                if o.kind == "hole":
                    c = 0.25
                if CP.distance(shp, o.shape) < c:
                    ok = False
                    break
            if ok and min(B.HX - max(abs(x1), abs(x2)), B.HY - max(abs(y1), abs(y2))) < B.EDGE_CLEARANCE + cw / 2:
                ok = False
            if ok:
                t[4] = cw
                n += 1
                for it in self.items:
                    if it.kind == "track" and it.shape[1:5] == (x1, y1, x2, y2) and it.net == net:
                        it.shape = shp
                        it.bbox = bb
                        break
        self.log.append(f"widened {n} power/supply segments to net-class width")


# ------------------------------------------------------------------ hand-drawn RF section
HAND_TRACKS = [  # (net, width, [points]) board frame; pin tips of U2's top row are at y = -47.35
    ("SX_VR_PA", 0.2, [(-20.75, -47.1), (-20.75, -47.35), (-21.3, -47.9), (-22.3, -48.2)]),
    ("SX_VR_PA", 0.25, [(-22.3, -48.2), (-22.92, -47.2)]),
    ("SX_RFO", 0.2, [(-20.25, -47.1), (-20.25, -48.4), (-21.0, -49.15), (-22.3, -49.15)]),
    ("SX_RFO", 0.3, [(-22.3, -49.15), (-22.3, -50.1)]),
    ("SX_RFO", 0.3, [(-22.3, -49.65), (-23.22, -49.65)]),
    ("TX_N1", 0.3, [(-22.3, -51.1), (-22.3, -52.1)]),
    ("TX_N1", 0.3, [(-22.3, -51.6), (-23.22, -51.6)]),
    ("TX_N2", 0.3, [(-22.3, -53.1), (-22.3, -54.1)]),
    ("TX_N2", 0.3, [(-22.3, -53.6), (-23.22, -53.6)]),
    ("RF_TX", 0.3, [(-22.3, -55.08), (-21.65, -55.5), (-21.65, -55.9)]),
    ("SX_RFI_N", 0.2, [(-19.75, -47.1), (-19.75, -49.4), (-20.3, -49.95)]),
    ("SX_RFI_N", 0.25, [(-20.3, -50.0), (-21.3, -50.82)]),
    ("SX_RFI_P", 0.2, [(-19.25, -47.1), (-19.25, -49.4), (-18.9, -49.75), (-18.9, -50.0)]),
    ("SX_RFI_P", 0.25, [(-18.9, -49.75), (-17.935, -49.9)]),
    ("RX_IN", 0.3, [(-20.3, -51.0), (-18.9, -51.0)]),
    ("RX_IN", 0.3, [(-19.6, -51.0), (-19.6, -51.92)]),
    ("RF_RX", 0.3, [(-19.6, -52.88), (-19.6, -55.0), (-20.35, -55.65), (-20.35, -55.9)]),
    ("RF_ANT", 0.3, [(-21.0, -58.1), (-21.0, -59.12)]),
    ("RF_ANT", 0.3, [(-21.0, -59.0), (-21.92, -59.0)]),
    ("RF_ANT_CONN", 0.3, [(-21.0, -60.08), (-18.725, -59.5)]),
    # TCXO -> XTA, DC-DC loop, VREG (U2 left/bottom rows: tips at x = -21.85 / y = -42.65)
    ("TCXO_OUT", 0.2, [(-25.05, -43.8), (-24.4, -43.8), (-23.81, -44.4), (-23.81, -45.25)]),
    ("SX_XTA", 0.2, [(-22.79, -45.25), (-21.6, -45.25)]),
    ("SX_XTA", 0.2, [(-22.6, -45.25), (-22.6, -44.28)]),
    ("SX_DCC_SW", 0.25, [(-19.75, -42.9), (-19.75, -41.4), (-19.3, -40.95)]),
    ("SX_VREG", 0.25, [(-20.75, -42.9), (-20.75, -41.8), (-22.52, -41.8)]),
    ("SX_VREG", 0.25, [(-21.31, -41.8), (-21.31, -40.75)]),
    ("V3V3", 0.2, [(-21.6, -46.25), (-22.82, -46.23)]),
    ("V3V3", 0.2, [(-19.25, -42.9), (-19.25, -42.35), (-18.9, -41.75)]),
    ("V3V3", 0.2, [(-18.75, -42.9), (-18.75, -42.35), (-18.9, -41.75)]),
    # amplifier strap resistors (U6 left row tips at x = 8.15)
    ("AMP_GAIN", 0.2, [(7.27, 35.75), (8.4, 35.75)]),
    ("AMP_SD_N", 0.2, [(7.27, 36.75), (8.4, 36.75)]),
]


HAND_VIAS = [("V3V3", -18.9, -41.75)]


def rf_manual(bd):
    for net, x, y in HAND_VIAS:
        bd.add_via(x, y, net)
    bd.locked = set()
    for net, w, pts in HAND_TRACKS:
        for a, b in zip(pts[:-1], pts[1:]):
            bd.locked.add(id(bd.add_track(a[0], a[1], b[0], b[1], w, "F.Cu", net)))
    bd.locked |= {id(i) for i in bd.items if i.kind == "via"}
    bd.log.append(f"hand-drawn: {sum(len(p) - 1 for _, _, p in HAND_TRACKS)} segments (RF, TCXO, DC-DC, amp straps)")


# ------------------------------------------------------------------ main flow
def net_kw(net, rf):
    return dict(vias=False, layers=(0,)) if net in rf else {}


def snapshot(bd):
    return ({k: list(v) for k, v in bd.net_copper.items()}, list(bd.items), list(bd.tracks), list(bd.vias), list(bd.failed))


def restore(bd, snap):
    nc, items, tracks, vias, failed = snap
    bd.net_copper = {k: list(v) for k, v in nc.items()}
    bd.items, bd.tracks, bd.vias, bd.failed = list(items), list(tracks), list(vias), list(failed)
    bd.rebuild()


def ripup(bd, rf, rounds=12):
    """rip-up and reroute with congestion history; keeps the best state seen"""
    nfail = lambda: len([f for f in bd.failed if "fanout" not in f])  # noqa: E731
    best = (nfail(), snapshot(bd))
    for rnd in range(rounds):
        bad = sorted({f.split(":")[0] for f in bd.failed if "fanout" not in f})
        if not bad:
            break
        bd.failed = [f for f in bd.failed if "fanout" in f]
        for net in bad:
            if any(f.startswith(net + ":") for f in bd.failed):
                continue
            w = B.netclass(net)[1]
            bd.rip(net)
            kw = net_kw(net, rf)
            if rnd >= 5 and net not in rf:
                kw.update(via_cost=100, lcost=(10, 10))
            bd.route_net(net, w, **kw)
            if not bd.cur_fail:
                continue
            victims = set()
            for tree, its in bd.cur_fail:
                v = bd.blockers_of(net, tree, its, min(w, B.DEFAULT_TRACK))
                if v:
                    victims |= v
            victims -= {"GND", "V3V3"}
            bd.failed = [f for f in bd.failed if not f.startswith(net + ":")]
            bd.rip(net)
            for v in victims:
                bd.rip(v)
            bd.route_net(net, w, **kw)
            for v in sorted(victims):
                bd.route_net(v, B.netclass(v)[1], **net_kw(v, rf))
        bd.log.append(f"rip-up round {rnd + 1}: {nfail()} failed")
        print(bd.log[-1], flush=True)
        if nfail() < best[0]:
            best = (nfail(), snapshot(bd))
    if nfail() > best[0]:
        restore(bd, best[1])
        bd.log.append(f"restored best rip-up state: {best[0]} failed")
    local_ripup(bd, rf)


def local_ripup(bd, rf):
    """for each remaining failure: rip up every routed net near the stuck pad, route the stuck net first"""
    for f in [f for f in bd.failed if "fanout" not in f]:
        net, pad = f.split(":")[0], f.split(":")[1].split()[0]
        ref, num = pad.split(".")
        its = bd.pads.get((ref, num))
        if not its:
            continue
        cx, cy = (its[0].bbox[0] + its[0].bbox[2]) / 2, (its[0].bbox[1] + its[0].bbox[3]) / 2
        for radius in (2.5, 4.0, 6.0, 9.0):
            snap = snapshot(bd)
            victims = set()
            for n, cop in bd.net_copper.items():
                if n in ("GND", "V3V3") or n == net or n in rf:
                    continue
                for it in cop:
                    b = it.bbox
                    if b[0] - radius < cx < b[2] + radius and b[1] - radius < cy < b[3] + radius:
                        victims.add(n)
                        break
            before = [x for x in bd.failed if "fanout" not in x]
            bd.failed = [x for x in bd.failed if not x.startswith(net + ":")]
            for v in victims | {net}:
                bd.rip(v)
            bd.route_net(net, B.netclass(net)[1], **net_kw(net, rf))
            for v in sorted(victims):
                bd.route_net(v, B.netclass(v)[1], **net_kw(v, rf))
            after = [x for x in bd.failed if "fanout" not in x]
            if len(after) < len(before):
                bd.log.append(f"local rip-up r={radius} fixed {net} ({len(victims)} nets rerouted)")
                break
            restore(bd, snap)
    print("local rip-up:", [f for f in bd.failed if "fanout" not in f], flush=True)



def run():
    t0 = time.time()
    bd = Board()
    rf = [n for n in NETS if B.netclass(n)[0] == "RF"]
    rf_manual(bd)
    for n in rf:
        bd.route_net(n, B.netclass(n)[1], vias=False, layers=(0,))
    bd.log.append(f"RF routed {time.time() - t0:.0f}s")
    power = [n for n in NETS if B.netclass(n)[0] == "Power"]
    bd.fanout("GND", "coarse")                # ground stubs first: power tracks must not wall in decoupling caps
    for n in power:
        bd.route_net(n, B.netclass(n)[1])
    order = []
    for n in NETS:
        if n in ("GND", "V3V3") or n in rf or n in power:
            continue
        pads = bd.net_pads(n)
        if len(pads) < 2:
            continue
        xs = [(i[2][0].bbox[0] + i[2][0].bbox[2]) / 2 for i in pads]
        ys = [(i[2][0].bbox[1] + i[2][0].bbox[3]) / 2 for i in pads]
        hp = (max(xs) - min(xs)) + (max(ys) - min(ys))
        cls = B.netclass(n)[0]
        fine = any(bd.fine(i[2][0]) for i in pads)
        pri = {"USB": 0, "Power": 1, "Supply": 2}.get(cls, 4 if not fine else 3)
        order.append((pri, hp, n))
    order.sort()
    for n in ("GND", "V3V3"):
        bd.fanout(n, "coarse")
        bd.fanout(n, "fine")
    fan_fail = [f for f in bd.failed if "fanout" in f]
    bd.failed = [f for f in bd.failed if "fanout" not in f]
    for _, _, n in order:
        bd.route_net(n, B.netclass(n)[1])
    bd.log.append(f"first pass: {len(bd.failed)} failed connections")
    ripup(bd, rf)
    if fan_fail:          # second chance for plane pads after the signals settled
        for n in ("GND", "V3V3"):
            bd.fanout(n, "coarse")
            bd.fanout(n, "fine")
    if any("fanout" in f for f in bd.failed):     # last resort: allow vias closer to fine-pitch pins
        bd._static()
        saved = bd._fine_d
        bd._fine_d = np.full_like(saved, 99.0)
        bd.failed = [f for f in bd.failed if "fanout" not in f]
        for n in ("GND", "V3V3"):
            bd.fanout(n, "coarse")
            bd.fanout(n, "fine")
        bd._fine_d = saved
    bd.log.append(f"routing done {time.time() - t0:.0f}s, {len(bd.tracks)} segments, {len(bd.vias)} vias, "
                  f"{len(bd.failed)} failed")
    bd.widen()
    return bd


# ------------------------------------------------------------------ KiCad writer
def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def f(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def A(x, y):
    """board -> page coords"""
    return f"{f(x + B.ORIGIN[0])} {f(y + B.ORIGIN[1])}"


def footprint_sexpr(pl: B.Placed) -> str:
    fp = pl.fp
    side = pl.side
    ly = "F.Cu" if side == "F" else "B.Cu"
    p = pl.part
    ref = pl.ref
    mirror = " (justify mirror)" if side == "B" else ""
    o = [f'(footprint {q("nomiq:" + fp.name)} (layer {q(ly)}) (uuid {q(uid("fp", ref))}) (at {A(pl.x, pl.y)} {f(pl.a)})']
    fab = "F.Fab" if side == "F" else "B.Fab"
    ty = fp.crt[1] - 0.6
    tx, tyw = 0.0, (-ty if side == "B" else ty)
    o.append(f'(property "Reference" {q(ref)} (at {f(tx)} {f(tyw)} {f(pl.a)}) (layer {q(fab)}) (uuid {q(uid("ref", ref))}) '
             f'(effects (font (size 0.5 0.5) (thickness 0.08)){mirror}))')
    o.append(f'(property "Value" {q(p.value if p else "MountingHole")} (at 0 {f(-(fp.crt[3] + 0.6) if side == "B" else fp.crt[3] + 0.6)} {f(pl.a)}) '
             f'(layer {q(fab)}) (hide yes) (uuid {q(uid("val", ref))}) (effects (font (size 0.5 0.5) (thickness 0.08)){mirror}))')
    o.append(f'(property "Footprint" {q("nomiq:" + fp.name)} (at 0 0 0) (layer {q(fab)}) (hide yes) (uuid {q(uid("fpp", ref))}) '
             f'(effects (font (size 1 1)){mirror}))')
    if p:
        import gen_kicad as GK
        o.append(f'(property "MPN" {q(p.mpn)} (at 0 0 0) (layer {q(fab)}) (hide yes) (uuid {q(uid("mpn", ref))}) (effects (font (size 1 1)){mirror}))')
        o.append(f'(path {q("/" + GK.uid("sheet", p.sheet) + "/" + GK.uid(p.sheet, ref))})')
        o.append(f"(sheetname {q(p.sheet)}) (sheetfile {q(p.sheet + '.kicad_sch')})")
    attr = {"smd": "(attr smd)", "through_hole": "(attr through_hole)",
            "exclude_from_pos_files": "(attr exclude_from_pos_files exclude_from_bom)"}[fp.attr]
    if p and p.dnp:
        attr = attr[:-1] + " dnp)"
    o.append(attr)
    o.append(fplib.rect_lines(fp.crt, "F.CrtYd", 0.05, side))
    o.append(fplib.rect_lines(fp.fab, "F.Fab", 0.1, side))
    m = fplib.pin1_marker(fp, side)
    if m:
        o.append(m)
    for i, pd in enumerate(fp.pads):
        net = pad_net(ref, pd[0]) if (pd[0] and p) else ""
        netspec = (NETNUM[net], net) if net else None
        pinfn = None
        if p and pd[0]:
            for pin, pads in p.pads.items():
                if pd[0] in pads:
                    pinfn = pin
        o.append(fplib.pad_sexpr(fp, i, pd, side=side, rot0=pl.a, net=netspec, uid=uid("pad", ref, i), pinfn=pinfn))
    o += fplib.keepout_sexpr(fp, side)
    if fp.model:
        flip = "(rotate (xyz 0 180 180))" if side == "B" else "(rotate (xyz 0 0 0))"
        o.append(f"(model {q(fp.model)} (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) {flip})")
    o.append(")")
    return "\n    ".join(o)


def outline():
    r, hx, hy = B.BOARD_R, B.HX, B.HY
    s = []
    lines = [((-hx + r, -hy), (hx - r, -hy)), ((hx, -hy + r), (hx, hy - r)), ((hx - r, hy), (-hx + r, hy)),
             ((-hx, hy - r), (-hx, -hy + r))]
    for i, (a, b) in enumerate(lines):
        s.append(f'(gr_line (start {A(*a)}) (end {A(*b)}) (stroke (width 0.05) (type default)) (layer "Edge.Cuts") (uuid {q(uid("edge", i))}))')
    k = 1 - 1 / math.sqrt(2)
    for i, (cx, cy, sx, sy) in enumerate([(hx - r, -hy + r, 1, -1), (hx - r, hy - r, 1, 1), (-hx + r, hy - r, -1, 1),
                                          (-hx + r, -hy + r, -1, -1)]):
        start = (cx, cy + sy * r) if i % 2 == 0 else (cx + sx * r, cy)
        end = (cx + sx * r, cy) if i % 2 == 0 else (cx, cy + sy * r)
        mid = (cx + sx * r / math.sqrt(2), cy + sy * r / math.sqrt(2))
        s.append(f'(gr_arc (start {A(*start)}) (mid {A(*mid)}) (end {A(*end)}) (stroke (width 0.05) (type default)) '
                 f'(layer "Edge.Cuts") (uuid {q(uid("arc", i))}))')
    return s


def board_poly(inset=0.0, n=8):
    r, hx, hy = B.BOARD_R - inset, B.HX - inset, B.HY - inset
    pts = []
    for cx, cy, a0 in [(hx - B.BOARD_R, -hy + r - inset + inset, 90), (hx - B.BOARD_R, hy - r, 0),
                       (-hx + B.BOARD_R, hy - r, 270), (-hx + B.BOARD_R, -hy + r, 180)]:
        pass
    # simple: rounded rectangle polygon
    corners = [(B.HX - B.BOARD_R, -B.HY + B.BOARD_R, -90, 0), (B.HX - B.BOARD_R, B.HY - B.BOARD_R, 0, 90),
               (-B.HX + B.BOARD_R, B.HY - B.BOARD_R, 90, 180), (-B.HX + B.BOARD_R, -B.HY + B.BOARD_R, 180, 270)]
    for cx, cy, a0, a1 in corners:
        for k in range(n + 1):
            a = math.radians(a0 + (a1 - a0) * k / n)
            pts.append((cx + (B.BOARD_R - inset) * math.cos(a), cy + (B.BOARD_R - inset) * math.sin(a)))
    return pts


def zone(net, layer, name, prio=0, clearance=0.2, thermal=True):
    pts = " ".join(f"(xy {A(x, y)})" for x, y in board_poly(0.0))
    cp = "(connect_pads (clearance {}))".format(clearance) if thermal else f"(connect_pads yes (clearance {clearance}))"
    return (f'(zone (net {NETNUM[net]}) (net_name {q(net)}) (layer {q(layer)}) (uuid {q(uid("zone", net, layer))}) (name {q(name)}) '
            f'(hatch edge 0.5) (priority {prio}) {cp} (min_thickness 0.2) (filled_areas_thickness no) '
            f'(fill yes (thermal_gap 0.25) (thermal_bridge_width 0.3) (island_removal_mode 1) (island_area_min 1)) '
            f'(polygon (pts {pts})))')


def write(bd: Board, path):
    o = ['(kicad_pcb (version 20240108) (generator "nomiq_gen_pcb") (generator_version "8.0")',
         '(general (thickness 1.6) (legacy_teardrops no))', '(paper "A4" portrait)',
         '(title_block (title "Nomiq A0 main board") (rev "A0") (company "Nomiq") '
         '(comment 1 "Generated by hardware/design/gen_pcb.py - regenerate, do not hand-edit routing without updating the generator"))',
         '(layers (0 "F.Cu" signal) (1 "In1.Cu" power "In1.GND") (2 "In2.Cu" power "In2.3V3") (31 "B.Cu" signal) '
         '(32 "B.Adhes" user "B.Adhesive") (33 "F.Adhes" user "F.Adhesive") (34 "B.Paste" user) (35 "F.Paste" user) '
         '(36 "B.SilkS" user "B.Silkscreen") (37 "F.SilkS" user "F.Silkscreen") (38 "B.Mask" user) (39 "F.Mask" user) '
         '(40 "Dwgs.User" user "User.Drawings") (41 "Cmts.User" user "User.Comments") (42 "Eco1.User" user "User.Eco1") '
         '(43 "Eco2.User" user "User.Eco2") (44 "Edge.Cuts" user) (45 "Margin" user) (46 "B.CrtYd" user "B.Courtyard") '
         '(47 "F.CrtYd" user "F.Courtyard") (48 "B.Fab" user) (49 "F.Fab" user) (50 "User.1" user) (51 "User.2" user))']
    st = ['(layer "F.SilkS" (type "Top Silk Screen"))', '(layer "F.Paste" (type "Top Solder Paste"))',
          '(layer "F.Mask" (type "Top Solder Mask") (thickness 0.01))']
    for name, kind, t in B.STACKUP:
        if kind == "copper":
            st.append(f'(layer {q(name)} (type "copper") (thickness {t}))')
        else:
            typ = "prepreg" if "prepreg" in kind else "core"
            st.append(f'(layer {q(name)} (type {q(typ)}) (thickness {t}) (material {q("7628" if typ == "prepreg" else "FR4")}) '
                      f'(epsilon_r {4.4 if typ == "prepreg" else 4.6}) (loss_tangent 0.02))')
    st += ['(layer "B.Mask" (type "Bottom Solder Mask") (thickness 0.01))', '(layer "B.Paste" (type "Bottom Solder Paste"))',
           '(layer "B.SilkS" (type "Bottom Silk Screen"))', '(copper_finish "ENIG")', '(dielectric_constraints no)']
    o.append('(setup (stackup ' + " ".join(st) + ') (pad_to_mask_clearance 0) (allow_soldermask_bridges_in_footprints no) '
             f'(aux_axis_origin {A(-B.HX, B.HY)}) (grid_origin {A(0, 0)}))')
    o.append('(net 0 "")')
    for n in NETS:
        o.append(f"(net {NETNUM[n]} {q(n)})")
    for ref in sorted(bd.pl, key=lambda r: (r.rstrip("0123456789"), int("0" + "".join(c for c in r if c.isdigit())))):
        o.append(footprint_sexpr(bd.pl[ref]))
    o += outline()
    o.append(f'(gr_text "NOMIQ A0  LoRa 865-867 MHz" (at {A(-2.0, 22.5)} 0) (layer "F.SilkS") (uuid {q(uid("t1"))}) '
             '(effects (font (size 1 1) (thickness 0.15))))')
    o.append(f'(gr_text "github.com/anand2532/Nomiq" (at {A(-2.0, 24.5)} 0) (layer "F.SilkS") (uuid {q(uid("t2"))}) '
             '(effects (font (size 0.8 0.8) (thickness 0.12))))')
    for i, t in enumerate(bd.tracks):
        x1, y1, x2, y2, w, layer, net = t
        o.append(f'(segment (start {A(x1, y1)}) (end {A(x2, y2)}) (width {f(w)}) (layer {q(layer)}) (net {NETNUM[net]}) '
                 f'(uuid {q(uid("seg", i, net))}))')
    for i, (x, y, net) in enumerate(bd.vias):
        o.append(f'(via (at {A(x, y)}) (size {B.VIA_D}) (drill {B.VIA_DRILL}) (layers "F.Cu" "B.Cu") (net {NETNUM[net]}) '
                 f'(uuid {q(uid("via", i, net))}))')
    o.append(zone("GND", "In1.Cu", "GND plane", 0, 0.2, thermal=False))
    o.append(zone("V3V3", "In2.Cu", "3V3 plane", 0, 0.2, thermal=False))
    o.append(zone("GND", "F.Cu", "GND pour top", 0, 0.2))
    o.append(zone("GND", "B.Cu", "GND pour bottom", 0, 0.2))
    o.append(")")
    with open(path, "w") as fh:
        fh.write("\n".join(o) + "\n")


def write_project_rules():
    """net classes + design rules live in the .kicad_pro (KiCad 7+)"""
    path = os.path.join(OUT, "nomiq.kicad_pro")
    pro = json.load(open(path))
    classes = [{"name": "Default", "clearance": B.CLEARANCE, "track_width": B.DEFAULT_TRACK, "via_diameter": B.VIA_D,
                "via_drill": B.VIA_DRILL, "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_gap": 0.25,
                "diff_pair_width": 0.2, "diff_pair_via_gap": 0.25, "wire_width": 6, "bus_width": 12,
                "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)",
                "priority": 2147483647}]
    patterns = []
    for i, (name, (w, c, nets)) in enumerate(B.NETCLASS.items()):
        cl = dict(classes[0])
        cl.update({"name": name, "clearance": c, "track_width": w, "priority": i})
        classes.append(cl)
        patterns += [{"netclass": name, "pattern": n} for n in sorted(nets)]
    pro.setdefault("net_settings", {})
    pro["net_settings"].update({"classes": classes, "meta": {"version": 3}, "netclass_patterns": patterns,
                                "net_colors": None, "netclass_assignments": None})
    ds = pro.setdefault("board", {}).setdefault("design_settings", {})
    ds["rules"] = {"min_clearance": B.CLEARANCE, "min_track_width": 0.1, "min_via_diameter": 0.45, "min_via_annular_width": 0.1,
                   "min_through_hole_diameter": 0.2, "min_hole_clearance": 0.25, "min_hole_to_hole": 0.25,
                   "min_copper_edge_clearance": B.EDGE_CLEARANCE - 0.05, "min_silk_clearance": 0.0,
                   "min_text_height": 0.5, "min_text_thickness": 0.08, "solder_mask_to_copper_clearance": 0.0,
                   "use_height_for_length_calcs": True, "max_error": 0.005, "min_microvia_diameter": 0.2,
                   "min_microvia_drill": 0.1, "min_resolved_spokes": 1}
    sev = ds.setdefault("rule_severities", {})
    sev.update({"silk_overlap": "ignore", "silk_over_copper": "ignore", "silk_edge_clearance": "ignore",
                "lib_footprint_issues": "ignore", "lib_footprint_mismatch": "ignore", "text_height": "ignore",
                "text_thickness": "ignore", "courtyards_overlap": "error", "footprint_type_mismatch": "ignore",
                "npth_inside_courtyard": "ignore", "starved_thermal": "warning"})
    ds.setdefault("defaults", {})
    json.dump(pro, open(path, "w"), indent=2)


def main():
    bd = run()
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "nomiq.kicad_pcb")
    write(bd, path)
    write_project_rules()
    rep = os.path.join(B.ROOT, "hardware", "design", "out", "pcb_route_report.txt")
    with open(rep, "w") as fh:
        fh.write("\n".join(bd.log) + "\n")
        fh.write("FAILED:\n" + "\n".join(bd.failed) + "\n")
    print("\n".join(bd.log))
    print(f"failed: {len(bd.failed)}")
    for x in bd.failed[:50]:
        print("  ", x)
    print(f"written {os.path.relpath(path, B.ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

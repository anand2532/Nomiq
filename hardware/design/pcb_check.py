#!/usr/bin/env python3
"""Independent DRC + connectivity check of hardware/kicad/nomiq/nomiq.kicad_pcb (reads only the file).

    python3 hardware/design/pcb_check.py [--png out.png]

Checks
  * copper clearance on every copper layer between items of different nets (net-class clearance,
    board minimum 0.127 mm), NPTH hole clearance 0.25 mm, via drill-to-drill 0.25 mm
  * copper to board edge >= min_copper_edge_clearance
  * courtyard overlaps per side
  * connectivity: tracks/vias/pads that touch on a common layer are joined; vias and THT pads that
    reach an inner layer covered by a full-board zone of their net join that plane
Not checked here (KiCad DRC does it after zone fill in CI): zone fill itself, thermal spokes, silk.
"""
from __future__ import annotations

import argparse
import collections
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import copper as CP  # noqa: E402
from gen_kicad import parse, find  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PCB = os.path.join(ROOT, "hardware", "kicad", "nomiq", "nomiq.kicad_pcb")
PRO = os.path.join(ROOT, "hardware", "kicad", "nomiq", "nomiq.kicad_pro")


def one(n, k):
    r = find(n, k)
    return r[0] if r else None


def load(path=PCB):
    root = parse(open(path).read())
    nets = {int(n[1]): n[2] for n in find(root, "net")}
    items, courts = [], []
    for fp in find(root, "footprint"):
        at = one(fp, "at")
        fx, fy, fa = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0
        ref = [p[2] for p in find(fp, "property") if p[1] == "Reference"][0]
        side = one(fp, "layer")[1][0]
        for pad in find(fp, "pad"):
            num, kind, shape = pad[1], pad[2], pad[3]
            pa = one(pad, "at")
            lx, ly = float(pa[1]), float(pa[2])
            ang = float(pa[3]) if len(pa) > 3 else 0.0
            dx, dy = CP.rot(lx, ly, fa)
            x, y = fx + dx, fy + dy
            sz = one(pad, "size")
            w, h = float(sz[1]), float(sz[2])
            layers = one(pad, "layers")[1:]
            cu = []
            for l in layers:
                if l == "*.Cu":
                    cu = list(CP.CU)
                elif l.endswith(".Cu"):
                    cu.append(l)
            netn = one(pad, "net")
            net = netn[2] if netn else ""
            dr = one(pad, "drill")
            if kind == "np_thru_hole":
                d = float(dr[-1]) if dr else w
                items.append(CP.Item("hole", CP.CU, "", ("circ", x, y, d / 2), owner=ref))
                continue
            if not cu:
                continue
            if shape == "custom":
                shp = ("circ", x, y, 0.15)
                prim = one(pad, "primitives")
                gc = one(prim, "gr_circle") if prim else None
                if gc:
                    c, e, wd = one(gc, "center"), one(gc, "end"), float(one(gc, "width")[1])
                    cx, cy = CP.rot(float(c[1]), float(c[2]), ang)
                    r = math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2]))
                    shp = ("circ", x + cx, y + cy, r + wd / 2)     # ring treated as a filled disc (conservative)
            else:
                shp = CP.pad_shape(shape, x, y, ang, w, h)
            it = CP.Item("pad", cu, net, shp, owner=f"{ref}.{num}")
            items.append(it)
            if kind == "thru_hole" and dr:
                dd = [t for t in dr[1:] if t != "oval"]
                items.append(CP.Item("drill", CP.CU, net, ("circ", x, y, float(dd[0]) / 2), owner=f"{ref}.{num}"))
        for r in find(fp, "fp_rect"):
            lay = one(r, "layer")[1]
            if lay.endswith("CrtYd"):
                s, e = one(r, "start"), one(r, "end")
                pts = [CP.rot(float(a), float(b), fa) for a, b in ((s[1], s[2]), (e[1], s[2]), (e[1], e[2]), (s[1], e[2]))]
                pts = [(fx + a, fy + b) for a, b in pts]
                courts.append((ref, lay[0], pts))
    for s in find(root, "segment"):
        a, b = one(s, "start"), one(s, "end")
        w = float(one(s, "width")[1])
        net = nets[int(one(s, "net")[1])]
        items.append(CP.Item("track", [one(s, "layer")[1]], net, ("cap", float(a[1]), float(a[2]), float(b[1]), float(b[2]), w / 2),
                             owner=f"track:{net}"))
    for v in find(root, "via"):
        at = one(v, "at")
        d = float(one(v, "size")[1])
        dr = float(one(v, "drill")[1])
        net = nets[int(one(v, "net")[1])]
        x, y = float(at[1]), float(at[2])
        items.append(CP.Item("via", CP.CU, net, ("circ", x, y, d / 2), owner=f"via:{net}"))
        items.append(CP.Item("drill", CP.CU, net, ("circ", x, y, dr / 2), owner=f"via:{net}"))
    planes = collections.defaultdict(set)
    for z in find(root, "zone"):
        nn = one(z, "net_name")
        lay = one(z, "layer")
        if nn and nn[1] and lay and lay[1] in ("In1.Cu", "In2.Cu"):
            planes[nn[1]].add(lay[1])
    edges = []
    for g in find(root, "gr_line") + find(root, "gr_arc"):
        if one(g, "layer")[1] != "Edge.Cuts":
            continue
        s, e = one(g, "start"), one(g, "end")
        m = one(g, "mid")
        P = [(float(s[1]), float(s[2]))]
        if m:
            P.append((float(m[1]), float(m[2])))
        P.append((float(e[1]), float(e[2])))
        for a, b in zip(P[:-1], P[1:]):
            edges.append((a, b))
    return root, nets, items, courts, planes, edges


def netclasses():
    pro = json.load(open(PRO))
    ns = pro.get("net_settings", {})
    cls = {c["name"]: c for c in ns.get("classes", [])}
    pat = {p["pattern"]: p["netclass"] for p in ns.get("netclass_patterns", [])}
    rules = pro.get("board", {}).get("design_settings", {}).get("rules", {})
    return cls, pat, rules


def check(path=PCB, png=None):
    root, nets, items, courts, planes, edges = load(path)
    cls, pat, rules = netclasses()
    base = rules.get("min_clearance", 0.127)
    edge_clr = rules.get("min_copper_edge_clearance", 0.25)

    def clr(net):
        c = cls.get(pat.get(net, "Default"), cls.get("Default", {}))
        return max(base, c.get("clearance", base))

    errs = []
    grid = CP.Grid(2.0)
    for i, it in enumerate(items):
        grid.add(i, it.bbox)
    seen = set()
    for i, a in enumerate(items):
        for j in grid.query(a.bbox, 0.6):
            if j <= i or (i, j) in seen:
                continue
            seen.add((i, j))
            b = items[j]
            if a.kind == "drill" or b.kind == "drill":
                if a.kind == "drill" and b.kind in ("drill", "hole") or b.kind == "drill" and a.kind == "hole":
                    if a.owner != b.owner and CP.distance(a.shape, b.shape) < rules.get("min_hole_to_hole", 0.25) - 1e-6:
                        errs.append(f"hole-to-hole {a.owner} / {b.owner}")
                continue
            if not (a.layers & b.layers):
                continue
            if a.kind == "hole" or b.kind == "hole":
                h, o = (a, b) if a.kind == "hole" else (b, a)
                if o.kind == "hole" or o.owner.split(".")[0] == h.owner:
                    continue
                if CP.distance(h.shape, o.shape) < rules.get("min_hole_clearance", 0.25) - 1e-6:
                    errs.append(f"hole clearance {h.owner} / {o.owner}")
                continue
            if a.net and a.net == b.net:
                continue
            if a.kind == "pad" and b.kind == "pad" and a.owner.split(".")[0] == b.owner.split(".")[0]:
                continue           # library pad spacing inside one footprint
            need = max(clr(a.net) if a.net else base, clr(b.net) if b.net else base)
            d = CP.distance(a.shape, b.shape)
            if d < need - 1e-4:
                lay = sorted(a.layers & b.layers)[0]
                errs.append(f"clearance {d:.3f} < {need:.3f} on {lay}: {a.owner} ({a.net}) / {b.owner} ({b.net})")
    # board edge
    for it in items:
        if it.kind in ("drill", "hole"):
            continue
        for e in edges:
            d = CP.distance(it.shape, ("cap", e[0][0], e[0][1], e[1][0], e[1][1], 0.0))
            if d < edge_clr - 1e-4:
                errs.append(f"edge clearance {d:.3f}: {it.owner}")
                break
    # courtyards
    for i in range(len(courts)):
        for j in range(i + 1, len(courts)):
            (r1, s1, p1), (r2, s2, p2) = courts[i], courts[j]
            if s1 == s2 and CP.distance(("poly", p1), ("poly", p2)) == 0.0:
                # touching edges are fine; require positive overlap area
                if _overlap_area(p1, p2) > 1e-3:
                    errs.append(f"courtyard overlap {r1} / {r2}")
    # connectivity
    parent = list(range(len(items)))

    def fnd(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def uni(a, b):
        ra, rb = fnd(a), fnd(b)
        if ra != rb:
            parent[ra] = rb

    cu_items = [i for i, it in enumerate(items) if it.kind in ("pad", "track", "via") and it.net]
    for i in cu_items:
        a = items[i]
        for j in grid.query(a.bbox, 0.01):
            b = items[j]
            if j <= i or b.net != a.net or b.kind not in ("pad", "track", "via"):
                continue
            if a.layers & b.layers and CP.distance(a.shape, b.shape) <= 1e-4:
                uni(i, j)
    plane_anchor = {}
    for i in cu_items:
        a = items[i]
        for lay in planes.get(a.net, ()):
            if lay in a.layers:
                if a.net in plane_anchor:
                    uni(i, plane_anchor[a.net])
                else:
                    plane_anchor[a.net] = i
    groups = collections.defaultdict(lambda: collections.defaultdict(list))
    for i in cu_items:
        it = items[i]
        if it.kind == "pad":
            groups[it.net][fnd(i)].append(it.owner)
    unconnected = {n: list(g.values()) for n, g in groups.items() if len(g) > 1}
    stats = collections.Counter(it.kind for it in items)
    if png:
        render(items, edges, png)
    return errs, unconnected, stats


def _overlap_area(p, q):
    xs = sorted(set([a for a, _ in p] + [a for a, _ in q]))
    ax0, ax1 = min(a for a, _ in p), max(a for a, _ in p)
    ay0, ay1 = min(b for _, b in p), max(b for _, b in p)
    bx0, bx1 = min(a for a, _ in q), max(a for a, _ in q)
    by0, by1 = min(b for _, b in q), max(b for _, b in q)
    return max(0, min(ax1, bx1) - max(ax0, bx0)) * max(0, min(ay1, by1) - max(ay0, by0))


def render(items, edges, png, window=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Polygon, Circle
    fig, ax = plt.subplots(figsize=(10, 18))
    col = {"F.Cu": "#d62728", "B.Cu": "#1f77b4"}
    for it in items:
        if it.kind == "hole":
            ax.add_patch(Circle(it.shape[1:3], it.shape[3], color="k"))
            continue
        if it.kind == "drill":
            continue
        lay = "F.Cu" if "F.Cu" in it.layers else ("B.Cu" if "B.Cu" in it.layers else None)
        if it.kind == "via":
            ax.add_patch(Circle(it.shape[1:3], it.shape[3], color="#2ca02c", alpha=0.9))
            continue
        c = col.get(lay, "gray")
        alpha = 0.55 if it.kind == "pad" else 0.8
        s = it.shape
        if s[0] == "poly":
            ax.add_patch(Polygon(s[1], color=c, alpha=alpha, lw=0))
        elif s[0] == "circ":
            ax.add_patch(Circle(s[1:3], s[3], color=c, alpha=alpha))
        else:
            ax.plot([s[1], s[3]], [s[2], s[4]], color=c, lw=s[5] * 2 * 72 / 25.4 * 10 / 1.0 * 0.35, alpha=alpha,
                    solid_capstyle="round")
    for a, b in edges:
        ax.plot([a[0], b[0]], [a[1], b[1]], "k", lw=0.5)
    ax.set_aspect("equal")
    ax.invert_yaxis()
    if window:
        ax.set_xlim(window[0], window[2]); ax.set_ylim(window[3], window[1])
    plt.savefig(png, dpi=120, bbox_inches="tight")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png")
    ap.add_argument("pcb", nargs="?", default=PCB)
    a = ap.parse_args()
    errs, unc, stats = check(a.pcb, a.png)
    print(f"{stats['pad']} pads, {stats['track']} track segments, {stats['via']} vias, {stats['hole']} NPTH")
    print(f"DRC: {len(errs)} violations; connectivity: {len(unc)} nets with unconnected pads")
    for e in errs[:60]:
        print("  DRC", e)
    for n, g in sorted(unc.items()):
        print(f"  OPEN {n}: " + " | ".join(",".join(x) for x in g))
    return 1 if errs or unc else 0


if __name__ == "__main__":
    sys.exit(main())

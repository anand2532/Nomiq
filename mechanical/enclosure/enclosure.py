#!/usr/bin/env python3
"""Nomiq parametric enclosure (build123d).

Usage:
    pip install build123d
    python3 mechanical/enclosure/enclosure.py --out mechanical/exports

Produces front/back shells as STEP (for CAD / fit checks against the KiCad PCB STEP)
and STL (for 3D printing). All dimensions come from params.py.
"""
from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import params as P  # noqa: E402

from build123d import (  # noqa: E402
    Axis, Box, Cylinder, Keep, Plane, Pos, Rot, Solid, export_step, export_stl, fillet, split,
)


def rounded_block(w: float, l: float, h: float, r_plan: float, r_edge: float = 0.0) -> Solid:
    b = Box(w, l, h)
    b = fillet(b.edges().filter_by(Axis.Z), radius=r_plan)
    if r_edge > 0:
        b = fillet(b.faces().sort_by(Axis.Z)[-1].edges() + b.faces().sort_by(Axis.Z)[0].edges(), radius=r_edge)
    return b


def shell() -> Solid:
    outer = rounded_block(P.WIDTH, P.LENGTH, P.HEIGHT, P.CORNER_R, P.EDGE_R)
    inner = rounded_block(P.WIDTH - 2 * P.WALL, P.LENGTH - 2 * P.WALL, P.HEIGHT - 2 * P.WALL,
                          P.CORNER_R - P.WALL)
    return outer - inner


def boss_positions():
    x = P.WIDTH / 2 - P.WALL - P.BOSS_INSET
    y = P.LENGTH / 2 - P.WALL - P.BOSS_INSET
    return [(sx * x, sy * y) for sx in (-1, 1) for sy in (-1, 1)]


def cutters():
    """Openings through the walls, applied before splitting."""
    t = P.WALL * 3
    zf = P.HEIGHT / 2           # front face
    zb = -P.HEIGHT / 2          # back face
    c = []
    # display window
    win = Box(P.DISPLAY_WIN_W, P.DISPLAY_WIN_H, t)
    win = fillet(win.edges().filter_by(Axis.Z), radius=P.DISPLAY_R)
    c.append(Pos(0, P.DISPLAY_Y, zf) * win)
    # front buttons (cross layout)
    for dx, dy in [(0, P.BTN_PITCH), (0, -P.BTN_PITCH), (0, 0), (P.BTN_PITCH * 1.6, 0)]:
        c.append(Pos(dx, P.BTN_CENTER_Y + dy, zf) * Cylinder(P.BTN_D / 2, t))
    # speaker grille: hex-ish grid clipped to a circle
    n = int(P.SPK_FIELD_D / P.SPK_PITCH)
    for i in range(-n, n + 1):
        for j in range(-n, n + 1):
            x = i * P.SPK_PITCH + (P.SPK_PITCH / 2 if j % 2 else 0)
            y = j * P.SPK_PITCH * 0.866
            if x * x + y * y <= (P.SPK_FIELD_D / 2) ** 2:
                c.append(Pos(x, P.SPK_Y + y, zf) * Cylinder(P.SPK_HOLE_D / 2, t))
    # side buttons
    c.append(Pos(-P.WIDTH / 2, P.REC_BTN_Y, P.SIDE_BTN_Z) * Box(t, P.REC_BTN_LEN, P.SIDE_BTN_H))
    c.append(Pos(P.WIDTH / 2, P.CAM_BTN_Y, P.SIDE_BTN_Z) * Box(t, P.SIDE_BTN_LEN, P.SIDE_BTN_H))
    # bottom end: USB-C + mic
    usb = Box(P.USBC_W, t, P.USBC_H)
    usb = fillet(usb.edges().filter_by(Axis.Y), radius=P.USBC_H / 2 - 0.01)
    c.append(Pos(0, -P.LENGTH / 2, P.USBC_Z) * usb)
    c.append(Pos(P.MIC_X, -P.LENGTH / 2, P.USBC_Z) * Rot(90, 0, 0) * Cylinder(P.MIC_D / 2, t))
    # top end: SMA bulkhead + power button
    c.append(Pos(P.SMA_X, P.LENGTH / 2, P.SMA_Z) * Rot(90, 0, 0) * Cylinder(P.SMA_D / 2, t))
    c.append(Pos(P.PWR_BTN_X, P.LENGTH / 2, P.SIDE_BTN_Z) * Box(P.PWR_BTN_W, t, P.SIDE_BTN_H))
    # rear camera
    c.append(Pos(P.CAM_X, P.CAM_Y, zb) * Cylinder(P.CAM_D / 2, t))
    return c


def build():
    body = shell()
    for cut in cutters():
        body = body - cut

    split_z = P.HEIGHT / 2 - P.FRONT_DEPTH
    front = split(body, bisect_by=Plane.XY.offset(split_z), keep=Keep.TOP)
    back = split(body, bisect_by=Plane.XY.offset(split_z), keep=Keep.BOTTOM)

    inner_top = P.HEIGHT / 2 - P.WALL
    inner_bot = -P.HEIGHT / 2 + P.WALL
    for x, y in boss_positions():
        # front: boss from inner ceiling down to split plane, blind insert hole
        h_f = inner_top - split_z
        front = front + Pos(x, y, split_z + h_f / 2) * Cylinder(P.BOSS_OD / 2, h_f)
        front = front - Pos(x, y, split_z + P.INSERT_DEPTH / 2) * Cylinder(P.INSERT_D / 2, P.INSERT_DEPTH)
        # back: boss from floor up to split plane, through clearance + head counterbore
        h_b = split_z - inner_bot
        back = back + Pos(x, y, inner_bot + h_b / 2) * Cylinder(P.BOSS_OD / 2, h_b)
        back = back - Pos(x, y, 0) * Cylinder(P.SCREW_CLEAR_D / 2, P.HEIGHT * 2)
        back = back - Pos(x, y, -P.HEIGHT / 2 + P.SCREW_HEAD_DEPTH / 2) * Cylinder(
            P.SCREW_HEAD_D / 2, P.SCREW_HEAD_DEPTH)

    # battery retention ribs on the back floor
    z_rib = inner_bot + P.BATT_RIB_H / 2
    bw, bl = P.BATT_W + 2 * P.BATT_RIB, P.BATT_L + 2 * P.BATT_RIB
    for dx in (-1, 1):
        back = back + Pos(dx * (bw / 2 - P.BATT_RIB / 2), P.BATT_Y, z_rib) * Box(P.BATT_RIB, bl * 0.6, P.BATT_RIB_H)
    for dy in (-1, 1):
        back = back + Pos(0, P.BATT_Y + dy * (bl / 2 - P.BATT_RIB / 2), z_rib) * Box(bw * 0.6, P.BATT_RIB, P.BATT_RIB_H)

    return front, back


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "exports"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    front, back = build()
    for name, part in (("front_shell", front), ("back_shell", back)):
        bb = part.bounding_box()
        print(f"{name}: valid={part.is_valid()} volume={part.volume / 1000:.1f} cm3 "
              f"bbox={bb.size.X:.1f} x {bb.size.Y:.1f} x {bb.size.Z:.1f} mm")
        export_step(part, os.path.join(a.out, f"{name}.step"))
        export_stl(part, os.path.join(a.out, f"{name}.stl"))
    print(f"exported to {os.path.abspath(a.out)}")


if __name__ == "__main__":
    main()

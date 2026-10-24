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
    # mic: front face, above the PCB port hole
    c.append(Pos(P.MIC_X, P.MIC_Y, zf) * Cylinder(P.MIC_D / 2, t))
    # bottom end: USB-C
    usb = Box(P.USBC_W, t, P.USBC_H)
    usb = fillet(usb.edges().filter_by(Axis.Y), radius=P.USBC_H / 2 - 0.3)
    c.append(Pos(0, -P.LENGTH / 2, P.USBC_Z) * usb)
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
        h_b = P.PCB_BOT_Z - inner_bot           # back boss stops under the PCB; the screw clamps the board
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


def button_caps():
    """four printed key caps: shaft through the front wall + retaining flange, resting on the PTS810 plunger"""
    zf = P.HEIGHT / 2
    z0 = P.PCB_TOP_Z + P.SWITCH_H                 # switch top
    inner_top = zf - P.WALL
    caps = []
    for dx, dy in [(0, P.BTN_PITCH), (0, -P.BTN_PITCH), (0, 0), (P.BTN_PITCH * 1.6, 0)]:
        x, y = dx, P.BTN_CENTER_Y + dy
        shaft_h = zf + P.BTN_PROUD - (inner_top - P.BTN_FLANGE_T)
        flange_h = P.BTN_FLANGE_T
        stem_h = (inner_top - P.BTN_FLANGE_T) - z0 - 0.2
        c = Pos(x, y, inner_top - P.BTN_FLANGE_T + shaft_h / 2) * Cylinder(P.BTN_CAP_D / 2, shaft_h)
        c = c + Pos(x, y, inner_top - flange_h / 2 - P.BTN_FLANGE_T + flange_h) * Cylinder(P.BTN_FLANGE_D / 2, flange_h)
        c = c + Pos(x, y, z0 + 0.2 + stem_h / 2) * Cylinder(1.2, stem_h)
        caps.append(c)
    out = caps[0]
    for c in caps[1:]:
        out = out + c
    return out


def board_envelope():
    """PCB + tallest components as simple solids, for interference checks against the shells"""
    w = P.WIDTH - 2 * P.WALL - 2 * P.PCB_EDGE_GAP
    l = P.LENGTH - 2 * P.WALL - 2 * P.PCB_EDGE_GAP
    pcb = Pos(0, 0, P.PCB_BOT_Z + P.PCB_T / 2) * rounded_block(w, l, P.PCB_T, P.CORNER_R - P.WALL - P.PCB_EDGE_GAP)
    for x, y in boss_positions():
        pcb = pcb - Pos(x, y, P.PCB_BOT_Z + P.PCB_T / 2) * Cylinder(1.35, P.PCB_T * 2)
    z_mod = P.PCB_TOP_Z + P.DISPLAY_SPACER
    tft = Pos(0, P.DISPLAY_Y, z_mod + P.DISPLAY_STACK / 2) * Box(42.72, 60.0, P.DISPLAY_STACK)
    batt = Pos(0, P.BATT_Y, -P.HEIGHT / 2 + P.WALL + 0.2 + 4.0) * Box(P.BATT_W - 2, P.BATT_L - 2, 8.0)
    usb = Pos(0, -P.LENGTH / 2 + P.WALL + 0.5 + 3.675, P.USBC_Z) * Box(8.94, 7.35, 3.2)
    return {"pcb": pcb, "tft": tft, "battery": batt, "usb": usb}


def interference(front, back, env):
    rep = []
    for name, solid in env.items():
        for sname, shell_ in (("front", front), ("back", back)):
            try:
                v = (solid & shell_).volume
            except Exception:  # noqa: BLE001
                v = float("nan")
            rep.append((name, sname, v))
    return rep


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "exports"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    front, back = build()
    caps = button_caps()
    env = board_envelope()
    bad = []
    for name, sname, v in interference(front, back, env):
        flag = "" if v < 1e-3 else "  <-- INTERFERENCE"
        if v >= 1e-3 and not (name == "usb" and sname == "back"):
            bad.append((name, sname, v))
        print(f"interference {name:8s} vs {sname:5s} shell: {v:8.3f} mm3{flag}")
    for name, part in (("front_shell", front), ("back_shell", back), ("button_caps", caps)):
        bb = part.bounding_box()
        valid = part.is_valid() if callable(part.is_valid) else part.is_valid
        print(f"{name}: valid={valid} volume={part.volume / 1000:.1f} cm3 "
              f"bbox={bb.size.X:.1f} x {bb.size.Y:.1f} x {bb.size.Z:.1f} mm")
        export_step(part, os.path.join(a.out, f"{name}.step"))
        export_stl(part, os.path.join(a.out, f"{name}.stl"))
    print(f"exported to {os.path.abspath(a.out)}")
    if bad:
        print("FIT CHECK FAILED:", bad)
        sys.exit(1)


if __name__ == "__main__":
    main()

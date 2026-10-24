"""Nomiq A0 board geometry: outline, stack-up, rules, mechanical interface and component placement.

Coordinates are KiCad board coordinates relative to the board centre, mm, viewed from the
F (top) side.  **F side faces the BACK of the device** (battery / camera side, all electronics);
**B side faces the FRONT** (display module, buttons, speaker connector).  So, for a point seen
from the front of the device at (u, v) (u right, v up, enclosure params.py frame):
    x = -u,   y = -v
Placement helpers accept either frame.  Everything mechanical comes from mechanical/enclosure/params.py.
"""
from __future__ import annotations

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "mechanical", "enclosure"))
sys.path.insert(0, HERE)
import params as P  # noqa: E402
import fplib  # noqa: E402
import nomiq_hw as HW  # noqa: E402

# ------------------------------------------------------------------ board / stack-up / rules
EDGE_GAP = P.PCB_EDGE_GAP                        # PCB to inner wall
BOARD_W = P.WIDTH - 2 * P.WALL - 2 * EDGE_GAP    # x extent
BOARD_L = P.LENGTH - 2 * P.WALL - 2 * EDGE_GAP   # y extent
BOARD_R = max(1.0, P.CORNER_R - P.WALL - EDGE_GAP)
HX, HY = BOARD_W / 2, BOARD_L / 2
ORIGIN = (150.0, 105.0)                           # board centre on the KiCad page

LAYERS = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]
PLANE = {"In1.Cu": "GND", "In2.Cu": "V3V3"}       # solid planes
ROUTE_LAYERS = ["F.Cu", "B.Cu"]
STACKUP = [  # JLC04161H-7628 (JLCPCB 4-layer 1.6 mm)
    ("F.Cu", "copper", 0.035), ("dielectric 1", "prepreg 7628", 0.2104),
    ("In1.Cu", "copper", 0.0152), ("dielectric 2", "core", 1.065),
    ("In2.Cu", "copper", 0.0152), ("dielectric 3", "prepreg 7628", 0.2104), ("B.Cu", "copper", 0.035)]
PCB_T = P.PCB_T

CLEARANCE = 0.127          # board rule (JLC 4-layer min 0.09)
ROUTER_CLEARANCE = 0.15    # router keeps a margin over the rule
EDGE_CLEARANCE = 0.3
VIA_D, VIA_DRILL = 0.6, 0.3
NETCLASS = {               # name: (track width, clearance, nets)
    "Power": (0.5, 0.15, {"VBUS", "VSYS", "VBAT", "VSTT", "BB_L1", "BB_L2", "SPK_P", "SPK_N"}),
    "Supply": (0.3, 0.15, {"V3V3", "VCAM_3V3", "VCAM_2V8", "VCAM_1V2", "TCXO_VCC", "SX_VREG", "SX_DCC_SW", "SX_VR_PA",
                           "LCD_LED", "LCD_LED_SW", "GND"}),
    "RF": (0.35, 0.15, {"SX_RFO", "TX_N1", "TX_N2", "RF_TX", "RF_RX", "RX_IN", "SX_RFI_P", "SX_RFI_N", "RF_ANT", "RF_ANT_CONN"}),
    "USB": (0.2, 0.15, {"USB_DP", "USB_DN", "USB_DP_MCU", "USB_DN_MCU", "ESP_USB_DP", "ESP_USB_DN"}),
}
DEFAULT_TRACK = 0.15


def netclass(net: str) -> tuple[str, float, float]:
    for name, (w, c, nets) in NETCLASS.items():
        if net in nets:
            return name, w, c
    return "Default", DEFAULT_TRACK, CLEARANCE


# ------------------------------------------------------------------ mechanical interface (from params.py)
PCB_TOP_Z = P.PCB_TOP_Z                           # B (front-facing) face of the PCB sits on the split plane
PCB_BOT_Z = P.PCB_BOT_Z                           # F (back-facing) face


def fv(u, v):
    """front-view (params) -> board coords"""
    return (-u, -v)


BOSS_X = P.WIDTH / 2 - P.WALL - P.BOSS_INSET
BOSS_Y = P.LENGTH / 2 - P.WALL - P.BOSS_INSET
HOLES = [(sx * BOSS_X, sy * BOSS_Y) for sx in (-1, 1) for sy in (-1, 1)]
HOLE_KEEPOUT_R = P.BOSS_OD / 2 + 0.3              # boss face on both sides: no parts


# ------------------------------------------------------------------ transforms
def rot(x, y, a):
    r = math.radians(a)
    return (x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r))


class Placed:
    def __init__(self, ref, x, y, a, side):
        self.ref, self.x, self.y, self.a, self.side = ref, x, y, a % 360, side
        self.part = HW.PARTS.get(ref)
        name = fplib.lib_name(self.part.footprint) if self.part else "MountingHole_2.7mm_M2.5"
        self.fp = fplib.FP[name]

    def world(self, lx, ly):
        if self.side == "B":
            ly = -ly
        dx, dy = rot(lx, ly, self.a)
        return (self.x + dx, self.y + dy)

    def pad_world(self, p):
        x, y = self.world(p[3], p[4])
        ang = ((-p[5] if self.side == "B" else p[5]) + self.a) % 360
        return x, y, ang

    def rect_world(self, b):
        pts = [self.world(px, py) for px, py in ((b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3]))]
        return pts

    def bbox(self, b):
        pts = self.rect_world(b)
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        return (min(xs), min(ys), max(xs), max(ys))


PL: dict[str, Placed] = {}


def place(ref, x, y, a=0, side="F"):
    assert ref not in PL, ref
    PL[ref] = Placed(ref, x, y, a, side)
    return PL[ref]


def place_fv(ref, u, v, phi=0, side="B"):
    """place a front-side part by its front-view position/rotation"""
    x, y = fv(u, v)
    return place(ref, x, y, (180 - phi) if side == "B" else -phi, side)


def pad_pos(ref, pin_or_pad):
    pl = PL[ref]
    pads = pl.part.pads.get(pin_or_pad, [pin_or_pad]) if pl.part else [pin_or_pad]
    for p in pl.fp.pads:
        if p[0] == pads[0]:
            return pl.pad_world(p)[:2]
    raise KeyError(f"{ref}.{pin_or_pad}")


def near(ref, ic, pin, dx, dy, a=0, side="F"):
    """place `ref` at offset (dx, dy) from pad `pin` of `ic` (board frame)"""
    px, py = pad_pos(ic, pin)
    return place(ref, px + dx, py + dy, a, side)

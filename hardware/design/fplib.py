"""Nomiq footprint library (project-local copy of every footprint the A0 board uses).

Geometry is taken from the official KiCad footprint library (gitlab.com/kicad/libraries/kicad-footprints,
master, CC-BY-SA 4.0 with the KiCad library exception) and from Espressif's KiCad library for the
ESP32-S3-WROOM-1; the TFT module footprint is project-specific. Pad numbers are the library's, which
follow the manufacturers' datasheets; nomiq_hw.py maps datasheet pin names onto these numbers.

    python3 hardware/design/fplib.py     # writes hardware/kicad/nomiq/lib/nomiq.pretty/*.kicad_mod

Each footprint is a Footprint(pads, crt, fab, attr, model, keepouts). Pad tuple:
    (number, kind, shape, x, y, rot, w, h, drill, layers, rratio)
kind: smd | thru_hole | np_thru_hole | connect ; layers: cmp|cm|p|th|np or explicit KiCad names.
"""
from __future__ import annotations

import os
import uuid as _uuid

LAYERS = {"cmp": "F.Cu F.Mask F.Paste", "cm": "F.Cu F.Mask", "p": "F.Paste", "th": "*.Cu *.Mask", "np": "*.Cu *.Mask",
          "thp": "*.Cu *.Mask F.Paste"}
M3D = "${KICAD8_3DMODEL_DIR}/"
NS = _uuid.UUID("0b6a3c3e-61a4-4b0e-9a1f-6e6f6d697132")


class Footprint:
    def __init__(self, name, pads, crt, fab=None, attr="smd", model="", keepouts=(), descr="", custom=None):
        self.name, self.pads, self.crt, self.fab, self.attr = name, pads, crt, fab or crt, attr
        self.model, self.keepouts, self.descr = model, list(keepouts), descr
        self.custom = custom or {}       # pad index -> primitives s-expr (custom pads)

    def numbered(self):
        return [p for p in self.pads if p[0] != "" and p[1] != "np_thru_hole" and "Cu" in lay(p[9])]


def lay(code: str) -> str:
    return LAYERS.get(code, code)


FP: dict[str, Footprint] = {}


def add(fp: Footprint):
    FP[fp.name] = fp
    return fp


def chip(name, dx, w, h, crt, fab, rr=0.25, model="", paste_split=None):
    pads = [("1", "smd", "roundrect", -dx, 0, 0, w, h, "", "cmp", rr), ("2", "smd", "roundrect", dx, 0, 0, w, h, "", "cmp", rr)]
    if paste_split:   # tiny parts: copper+mask pads, separate paste
        pw, ph, px = paste_split
        pads = [("1", "smd", "roundrect", -dx, 0, 0, w, h, "", "cm", rr), ("2", "smd", "roundrect", dx, 0, 0, w, h, "", "cm", rr),
                ("", "smd", "roundrect", -px, 0, 0, pw, ph, "", "p", rr), ("", "smd", "roundrect", px, 0, 0, pw, ph, "", "p", rr)]
    return add(Footprint(name, pads, crt, fab, model=model))


def quad(name, per_side, pitch, edge, pl, pw, ep, crt, fab, paste, model="", ep_num=None):
    """QFN/DFN-style: pins counter-clockwise from pin 1 at top of left side. ep=(w,h) exposed pad."""
    pads = []
    n = 1
    span = (per_side - 1) * pitch / 2
    for i in range(per_side):                    # left, top->bottom
        pads.append((str(n), "smd", "roundrect", -edge, -span + i * pitch, 0, pl, pw, "", "cmp", 0.25)); n += 1
    for i in range(per_side):                    # bottom, left->right
        pads.append((str(n), "smd", "roundrect", -span + i * pitch, edge, 0, pw, pl, "", "cmp", 0.25)); n += 1
    for i in range(per_side):                    # right, bottom->top
        pads.append((str(n), "smd", "roundrect", edge, span - i * pitch, 0, pl, pw, "", "cmp", 0.25)); n += 1
    for i in range(per_side):                    # top, right->left
        pads.append((str(n), "smd", "roundrect", span - i * pitch, -edge, 0, pw, pl, "", "cmp", 0.25)); n += 1
    pads.append((str(ep_num or n), "smd", "rect", 0, 0, 0, ep[0], ep[1], "", "cm", 0))
    for (x, y, s) in paste:
        pads.append(("", "smd", "roundrect", x, y, 0, s, s, "", "p", 0.25))
    return add(Footprint(name, pads, crt, fab, model=model))


def dual(name, per_side, pitch, edge, pl, pw, crt, fab, rr=0.25, model="", ep=None, paste=()):
    """SOIC/TSSOP/DFN two rows: pins 1..n down the left, n+1.. up the right."""
    pads = []
    span = (per_side - 1) * pitch / 2
    for i in range(per_side):
        pads.append((str(i + 1), "smd", "roundrect", -edge, -span + i * pitch, 0, pl, pw, "", "cmp", rr))
    for i in range(per_side):
        pads.append((str(per_side + i + 1), "smd", "roundrect", edge, span - i * pitch, 0, pl, pw, "", "cmp", rr))
    if ep:
        pads.append((str(2 * per_side + 1), "smd", "rect", 0, 0, 0, ep[0], ep[1], "", "cm", 0))
        for (x, y, w, h) in paste:
            pads.append(("", "smd", "roundrect", x, y, 0, w, h, "", "p", 0.25))
    return add(Footprint(name, pads, crt, fab, model=model))


def sot(name, pins, pl, pw, crt, fab, model):
    pads = [(str(n), "smd", "roundrect", x, y, 0, pl, pw, "", "cmp", 0.25) for n, x, y in pins]
    return add(Footprint(name, pads, crt, fab, model=model))


# ------------------------------------------------------------------ passives / discretes
chip("R_0402_1005Metric", 0.51, 0.54, 0.64, (-0.93, -0.47, 0.93, 0.47), (-0.525, -0.27, 0.525, 0.27),
     model=M3D + "Resistor_SMD.3dshapes/R_0402_1005Metric.step")
chip("C_0402_1005Metric", 0.48, 0.56, 0.62, (-0.91, -0.46, 0.91, 0.46), (-0.5, -0.25, 0.5, 0.25),
     model=M3D + "Capacitor_SMD.3dshapes/C_0402_1005Metric.step")
chip("C_0603_1608Metric", 0.775, 0.9, 0.95, (-1.48, -0.73, 1.48, 0.73), (-0.8, -0.4, 0.8, 0.4),
     model=M3D + "Capacitor_SMD.3dshapes/C_0603_1608Metric.step")
chip("C_0805_2012Metric", 0.95, 1.0, 1.45, (-1.7, -0.98, 1.7, 0.98), (-1.0, -0.625, 1.0, 0.625),
     model=M3D + "Capacitor_SMD.3dshapes/C_0805_2012Metric.step")
chip("L_0402_1005Metric", 0.485, 0.59, 0.64, (-0.93, -0.47, 0.93, 0.47), (-0.5, -0.25, 0.5, 0.25),
     model=M3D + "Inductor_SMD.3dshapes/L_0402_1005Metric.step")
chip("L_0805_2012Metric", 1.0625, 0.875, 1.2, (-1.75, -0.85, 1.75, 0.85), (-1.0, -0.45, 1.0, 0.45),
     model=M3D + "Inductor_SMD.3dshapes/L_0805_2012Metric.step")
add(Footprint("L_Taiyo-Yuden_NR-40xx", [("1", "smd", "rect", -1.4, 0, 0, 1.2, 3.7, "", "cmp", 0),
                                         ("2", "smd", "rect", 1.4, 0, 0, 1.2, 3.7, "", "cmp", 0)],
              (-2.25, -2.25, 2.25, 2.25), (-2.0, -2.0, 2.0, 2.0), model=M3D + "Inductor_SMD.3dshapes/L_Taiyo-Yuden_NR-40xx.step"))
chip("D_0201_0603Metric", 0.32, 0.46, 0.4, (-0.7, -0.35, 0.7, 0.35), (-0.3, -0.15, 0.3, 0.15), rr=0.25,
     model=M3D + "Diode_SMD.3dshapes/D_0201_0603Metric.step", paste_split=(0.318, 0.36, 0.345))
chip("D_SOD-882", 0.35, 0.4, 0.7, (-0.8, -0.6, 0.8, 0.6), (-0.5, -0.3, 0.5, 0.3), rr=0.125,
     model=M3D + "Diode_SMD.3dshapes/D_SOD-882.step", paste_split=(0.3, 0.6, 0.35))
chip("D_SOD-123F", 1.4, 1.1, 1.1, (-2.2, -1.15, 2.2, 1.15), (-1.4, -0.9, 1.4, 0.9), rr=0.227,
     model=M3D + "Diode_SMD.3dshapes/D_SOD-123F.step")
add(Footprint("TestPoint_Pad_D1.0mm", [("1", "smd", "circle", 0, 0, 0, 1.0, 1.0, "", "cm", 0)], (-1, -1, 1, 1),
              (-0.5, -0.5, 0.5, 0.5), attr="exclude_from_pos_files"))
add(Footprint("MountingHole_2.7mm_M2.5", [("", "np_thru_hole", "circle", 0, 0, 0, 2.7, 2.7, "2.7", "np", 0)],
              (-2.95, -2.95, 2.95, 2.95), (-1.35, -1.35, 1.35, 1.35), attr="exclude_from_pos_files"))

# ------------------------------------------------------------------ SOT packages
sot("SOT-23", [("1", -0.9375, -0.95), ("2", -0.9375, 0.95), ("3", 0.9375, 0)], 1.475, 0.6,
    (-1.93, -1.7, 1.93, 1.7), (-0.65, -1.45, 0.65, 1.45), M3D + "Package_TO_SOT_SMD.3dshapes/SOT-23.step")
sot("SOT-23-5", [("1", -1.1375, -0.95), ("2", -1.1375, 0), ("3", -1.1375, 0.95), ("4", 1.1375, 0.95), ("5", 1.1375, -0.95)],
    1.325, 0.6, (-2.05, -1.7, 2.05, 1.7), (-0.8, -1.45, 0.8, 1.45), M3D + "Package_TO_SOT_SMD.3dshapes/SOT-23-5.step")
sot("SOT-23-6", [("1", -1.1375, -0.95), ("2", -1.1375, 0), ("3", -1.1375, 0.95), ("4", 1.1375, 0.95), ("5", 1.1375, 0),
                 ("6", 1.1375, -0.95)], 1.325, 0.6, (-2.05, -1.7, 2.05, 1.7), (-0.8, -1.45, 0.8, 1.45),
    M3D + "Package_TO_SOT_SMD.3dshapes/SOT-23-6.step")
sot("SOT-363_SC-70-6", [("1", -0.8375, -0.65), ("2", -0.8375, 0), ("3", -0.8375, 0.65), ("4", 0.8375, 0.65),
                        ("5", 0.8375, 0), ("6", 0.8375, -0.65)], 1.025, 0.35, (-1.45, -1.1, 1.45, 1.1),
    (-0.625, -1.0, 0.625, 1.0), M3D + "Package_TO_SOT_SMD.3dshapes/SOT-363_SC-70-6.step")

# ------------------------------------------------------------------ ICs
quad("QFN-24-1EP_4x4mm_P0.5mm_EP2.6x2.6mm", 6, 0.5, 1.9375, 0.825, 0.25, (2.6, 2.6), (-2.6, -2.6, 2.6, 2.6),
     (-2, -2, 2, 2), [(-0.65, -0.65, 1.05), (-0.65, 0.65, 1.05), (0.65, -0.65, 1.05), (0.65, 0.65, 1.05)],
     model=M3D + "Package_DFN_QFN.3dshapes/QFN-24-1EP_4x4mm_P0.5mm_EP2.6x2.6mm.step")
quad("TQFN-16-1EP_3x3mm_P0.5mm_EP1.23x1.23mm", 4, 0.5, 1.4375, 0.825, 0.25, (1.23, 1.23), (-2.1, -2.1, 2.1, 2.1),
     (-1.5, -1.5, 1.5, 1.5), [(-0.31, -0.31, 0.5), (-0.31, 0.31, 0.5), (0.31, -0.31, 0.5), (0.31, 0.31, 0.5)],
     model=M3D + "Package_DFN_QFN.3dshapes/TQFN-16-1EP_3x3mm_P0.5mm_EP1.23x1.23mm.step")
quad("VQFN-16-1EP_3x3mm_P0.5mm_EP1.6x1.6mm", 4, 0.5, 1.4375, 0.875, 0.25, (1.6, 1.6), (-2.13, -2.13, 2.13, 2.13),
     (-1.5, -1.5, 1.5, 1.5), [(-0.4, -0.4, 0.64), (-0.4, 0.4, 0.64), (0.4, -0.4, 0.64), (0.4, 0.4, 0.64)],
     model=M3D + "Package_DFN_QFN.3dshapes/VQFN-16-1EP_3x3mm_P0.5mm_EP1.6x1.6mm.step")
dual("TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm", 4, 0.5, 0.9875, 0.775, 0.25, (-1.63, -1.25, 1.63, 1.25), (-1, -1, 1, 1),
     ep=(0.8, 1.2), paste=[(-0.2, -0.3, 0.32, 0.48), (-0.2, 0.3, 0.32, 0.48), (0.2, -0.3, 0.32, 0.48), (0.2, 0.3, 0.32, 0.48)],
     model=M3D + "Package_DFN_QFN.3dshapes/TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm.step")
dual("TSSOP-24_4.4x7.8mm_P0.65mm", 12, 0.65, 2.8625, 1.475, 0.4, (-3.85, -4.15, 3.85, 4.15), (-2.2, -3.9, 2.2, 3.9),
     model=M3D + "Package_SO.3dshapes/TSSOP-24_4.4x7.8mm_P0.65mm.step")
# TI DRC0010J (TPS63001): pads 1-5 left (top->bottom), 6-10 right (bottom->top), PowerPAD 11 incl. corner tabs
_son = []
for i in range(5):
    y = -1 + i * 0.5
    _son += [(str(i + 1), "smd", "rect", -1.76, y, 0, 0.28, 0.28, "", "cmp", 0),
             (str(i + 1), "smd", "oval", -1.475, y, 0, 0.85, 0.28, "", "cmp", 0)]
for i in range(5):
    y = 1 - i * 0.5
    _son += [(str(6 + i), "smd", "oval", 1.475, y, 0, 0.85, 0.28, "", "cmp", 0),
             (str(6 + i), "smd", "rect", 1.76, y, 0, 0.28, 0.28, "", "cmp", 0)]
_son += [("11", "smd", "rect", 0, 0, 0, 1.65, 2.4, "", "cm", 0)]
_son += [("11", "smd", "rect", x, y, 0, 0.68, 1.05, "", "cmp", 0) for x in (-0.45, 0.45) for y in (-0.635, 0.635)]
_son += [("11", "smd", "rect", x, y, 0, 0.28, 0.7, "", "cm", 0) for x in (-0.25, 0.25) for y in (-1.55, 1.55)]
_son += [("11", "smd", "rect", x, y, 0, 0.26, 0.5, "", "cmp", 0) for x in (-0.25, 0.25) for y in (-1.63, 1.63)]
add(Footprint("Texas_S-PVSON-N10", _son, (-2.15, -2.15, 2.15, 2.15), (-1.5, -1.5, 1.5, 1.5),
              model=M3D + "Package_SON.3dshapes/Texas_S-PVSON-N10.step"))
add(Footprint("Crystal_SMD_2016-4Pin_2.0x1.6mm",
              [("1", "smd", "roundrect", -0.7, 0.55, 0, 0.9, 0.8, "", "cmp", 0.25), ("2", "smd", "roundrect", 0.7, 0.55, 0, 0.9, 0.8, "", "cmp", 0.25),
               ("3", "smd", "roundrect", 0.7, -0.55, 0, 0.9, 0.8, "", "cmp", 0.25), ("4", "smd", "roundrect", -0.7, -0.55, 0, 0.9, 0.8, "", "cmp", 0.25)],
              (-1.5, -1.3, 1.5, 1.3), (-1, -0.8, 1, 0.8), model=M3D + "Crystal.3dshapes/Crystal_SMD_2016-4Pin_2.0x1.6mm.step"))
add(Footprint("InvenSense_ICS-43434-6_3.5x2.65mm",
              [("", "np_thru_hole", "circle", 0, 0.71, 0, 0.5, 0.5, "0.5", "*.Mask", 0),
               ("1", "smd", "rect", -0.9, -1.364, 0, 0.6, 0.522, "", "cm", 0), ("2", "smd", "rect", -0.9, -0.542, 0, 0.6, 0.522, "", "cm", 0),
               ("3", "smd", "custom", 0.6625, 0.71, 90, 0.3, 0.3, "", "cm", 0),
               ("4", "smd", "rect", 0.9, -0.542, 0, 0.6, 0.522, "", "cm", 0), ("5", "smd", "rect", 0.9, -1.364, 0, 0.6, 0.522, "", "cm", 0),
               ("6", "smd", "rect", 0, -1.364, 0, 0.6, 0.522, "", "cm", 0)],
              (-1.58, -2.0, 1.58, 2.0), (-1.325, -1.75, 1.325, 1.75),
              model=M3D + "Sensor_Audio.3dshapes/InvenSense_ICS-43434-6_3.5x2.65mm.step",
              custom={3: "(options (clearance outline) (anchor circle)) (primitives (gr_circle (center 0 -0.6625) (end 0.6625 -0.6625) (width 0.3) (fill none)))"},
              descr="bottom-port MEMS mic; acoustic port hole is the NPTH (mask only) - drill it as 0.5 mm"))

# ESP32-S3-WROOM-1 (Espressif KiCad library): 1-14 left, 15-26 bottom, 27-40 right, 41 = 9-pad GND array
_esp = [(str(i + 1), "smd", "rect", -8.75, -8.26 + i * 1.27, 0, 1.5, 0.9, "", "cmp", 0) for i in range(14)]
_esp += [(str(15 + i), "smd", "rect", -6.985 + i * 1.27, 9.5, 90, 1.5, 0.9, "", "cmp", 0) for i in range(12)]
_esp += [(str(27 + i), "smd", "rect", 8.75, 8.25 - i * 1.27, 180, 1.5, 0.9, "", "cmp", 0) for i in range(14)]
_esp += [("41", "smd", "rect", x, y, 0, 0.9, 0.9, "", "cmp", 0) for x in (-2.9, -1.5, -0.1) for y in (-1.94, -0.54, 0.86)]
add(Footprint("ESP32-S3-WROOM-1", _esp, (-9.8, -16.05, 9.8, 10.55), (-9.0, -15.75, 9.0, 9.75),
              model=M3D + "RF_Module.3dshapes/ESP32-S3-WROOM-1.step",
              keepouts=[("antenna keepout", [(9, -9.8), (-9, -9.8), (-9, -15.75), (9, -15.75)], "all")]))

# ------------------------------------------------------------------ connectors / switches
_usb = [("", "np_thru_hole", "circle", -2.89, -2.605, 0, 0.65, 0.65, "0.65", "np", 0),
        ("", "np_thru_hole", "circle", 2.89, -2.605, 0, 0.65, 0.65, "0.65", "np", 0)]
for n, x, w in [("A1", -3.2, 0.6), ("A4", -2.4, 0.6), ("A5", -1.25, 0.3), ("A6", -0.25, 0.3), ("A7", 0.25, 0.3), ("A8", 1.25, 0.3),
                ("A9", 2.4, 0.6), ("A12", 3.2, 0.6), ("B1", 3.2, 0.6), ("B4", 2.4, 0.6), ("B5", 1.75, 0.3), ("B6", 0.75, 0.3),
                ("B7", -0.75, 0.3), ("B8", -1.75, 0.3), ("B9", -2.4, 0.6), ("B12", -3.2, 0.6)]:
    _usb.append((n, "smd", "roundrect", x, -3.68, 0, w, 1.15, "", "cmp", 0.25))
_usb += [("SH", "thru_hole", "oval", x, y, 0, 1.0, h, d, "thp", 0)
         for x in (-4.32, 4.32) for (y, h, d) in ((-3.105, 2.1, "oval 0.6 1.7"), (1.075, 1.8, "oval 0.6 1.4"))]
add(Footprint("USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal", _usb, (-5.32, -4.76, 5.32, 4.18),
              (-4.47, -3.675, 4.47, 3.675), model=M3D + "Connector_USB.3dshapes/USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal.step",
              descr="receptacle opening at +Y (board edge on the +Y side of the footprint)"))
_sd = [(str(i + 1), "smd", "rect", x, -5.45, 0, 0.85 if i < 7 else 0.75, 1.1, "", "cmp", 0)
       for i, x in enumerate([-3.105, -2.005, -0.905, 0.195, 1.295, 2.395, 3.495, 4.545])]
_sd += [("9", "smd", "rect", -5.74, 0.7, 0, 1.2, 1.0, "", "cmp", 0), ("10", "smd", "rect", -5.74, 4.4, 0, 1.2, 1.0, "", "cmp", 0),
        ("SH", "smd", "rect", -5.565, -5.325, 0, 1.55, 1.35, "", "cmp", 0), ("SH", "smd", "rect", -2.24, 5.375, 0, 1.9, 1.35, "", "cmp", 0),
        ("SH", "smd", "rect", 3.73, 5.375, 0, 1.9, 1.35, "", "cmp", 0), ("SH", "smd", "rect", 5.755, -5.1, 0, 1.17, 1.8, "", "cmp", 0)]
add(Footprint("microSD_HC_Molex_104031-0811", _sd, (-6.84, -6.5, 6.84, 6.55), (-5.955, -9.7, 5.995, 5.7),
              model=M3D + "Connector_Card.3dshapes/microSD_HC_Molex_104031-0811.step", descr="card inserted from -Y"))
_fh = [(str(i + 1), "smd", "rect", -5.75 + i * 0.5, -1.85, 0, 0.3, 1.3, "", "cmp", 0) for i in range(24)]
_fh += [("MP", "smd", "rect", x, 1.4, 0, 1.8, 2.2, "", "cmp", 0) for x in (-7.65, 7.65)]
add(Footprint("Hirose_FH12-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal", _fh, (-9.05, -3.0, 9.05, 4.9), (-7.55, -1.2, 7.55, 4.4),
              model=M3D + "Connector_FFC-FPC.3dshapes/Hirose_FH12-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal.step",
              descr="flex enters from +Y"))
add(Footprint("JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal",
              [(str(i + 1), "smd", "roundrect", -2 + 2 * i, -2.85, 0, 1.0, 3.5, "", "cmp", 0.25) for i in range(3)] +
              [("MP", "smd", "roundrect", x, 2.9, 0, 1.5, 3.4, "", "cmp", 0.1667) for x in (-4.35, 4.35)],
              (-5.6, -5.1, 5.6, 5.1), (-4.95, -3.2, 4.95, 4.4),
              model=M3D + "Connector_JST.3dshapes/JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal.step", descr="plug enters from +Y"))
add(Footprint("JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal",
              [("1", "smd", "roundrect", -0.5, -2.0, 0, 0.6, 1.55, "", "cmp", 0.25), ("2", "smd", "roundrect", 0.5, -2.0, 0, 0.6, 1.55, "", "cmp", 0.25)] +
              [("MP", "smd", "roundrect", x, 1.875, 0, 1.2, 1.8, "", "cmp", 0.2083) for x in (-1.8, 1.8)],
              (-2.9, -3.28, 2.9, 3.28), (-2.0, -1.675, 2.0, 2.575),
              model=M3D + "Connector_JST.3dshapes/JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal.step"))
add(Footprint("U.FL_Hirose_U.FL-R-SMT-1_Vertical",
              [("1", "smd", "roundrect", -1.525, 0, 0, 1.05, 1.0, "", "cmp", 0.25),
               ("2", "smd", "roundrect", 0, -1.475, 0, 2.2, 1.05, "", "cmp", 0.238), ("2", "smd", "roundrect", 0, 1.475, 0, 2.2, 1.05, "", "cmp", 0.238)],
              (-2.55, -2.5, 1.8, 2.5), (-1.55, -1.5, 1.3, 1.5), model=M3D + "Connector_Coaxial.3dshapes/U.FL_Hirose_U.FL-R-SMT-1_Vertical.step",
              keepouts=[("top_keepout", [(1.1, -0.95), (-0.99, -0.95), (-0.99, 0.95), (1.1, 0.95)], "F.Cu")]))
add(Footprint("Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical",
              [("", "np_thru_hole", "circle", -2.54, 0, 0, 0.9906, 0.9906, "0.9906", "np", 0),
               ("", "np_thru_hole", "circle", 2.54, -1.016, 0, 0.9906, 0.9906, "0.9906", "np", 0),
               ("", "np_thru_hole", "circle", 2.54, 1.016, 0, 0.9906, 0.9906, "0.9906", "np", 0)] +
              [(str(n), "connect", "circle", x, y, 0, 0.7874, 0.7874, "", "cm", 0)
               for n, x, y in [(1, -1.27, 0.635), (2, -1.27, -0.635), (3, 0, 0.635), (4, 0, -0.635), (5, 1.27, 0.635), (6, 1.27, -0.635)]],
              (-3.5, -2.0, 3.5, 2.0), (-3.0, -1.5, 3.0, 1.5), attr="exclude_from_pos_files"))
add(Footprint("SW_SPST_PTS810",
              [(n, "smd", "rect", x, y, 0, 1.05, 0.65, "", "cmp", 0) for n, y in (("1", -1.075), ("2", 1.075)) for x in (-2.075, 2.075)],
              (-2.85, -1.85, 2.85, 1.85), (-2.1, -1.6, 2.1, 1.6), model=M3D + "Button_Switch_SMD.3dshapes/SW_SPST_PTS810.step"))
add(Footprint("SW_SPST_EVQP7A",
              [(n, "smd", "roundrect", x, y, 0, 1.4, 1.05, "", "cmp", 0.238) for n, y in (("1", -0.72), ("2", 0.72)) for x in (-1.8, 1.8)],
              (-2.75, -1.7, 2.75, 2.35), (-1.75, -1.45, 1.75, 2.1), model=M3D + "Button_Switch_SMD.3dshapes/SW_SPST_EVQP7A.step",
              descr="actuator points to +Y; place with the actuator over the board edge"))
add(Footprint("PinHeader_2x05_P1.27mm_Vertical_SMD",
              [(str(2 * i + 1 + c), "smd", "rect", (-1.95, 1.95)[c], -2.54 + i * 1.27, 0, 2.4, 0.74, "", "cmp", 0)
               for i in range(5) for c in (0, 1)],
              (-4.29, -3.68, 4.29, 3.68), (-2.75, -3.175, 2.75, 3.175),
              model=M3D + "Connector_PinHeader_1.27mm.3dshapes/PinHeader_2x05_P1.27mm_Vertical_SMD.step"))

# 2.4" ILI9341 SPI module: 14 pins soldered through the board, module body on the front side.
# Module outline and pin-1 position are parameters (mechanical/enclosure/params.py TFT_*) - verify on the purchased module.
TFT_W, TFT_H, TFT_PIN1_X, TFT_PIN1_Y = 42.72, 60.0, -16.51, -27.5   # pin row along the module's top edge
_tft = [(str(i + 1), "thru_hole", "rect" if i == 0 else "circle", TFT_PIN1_X + i * 2.54, TFT_PIN1_Y, 0, 1.7, 1.7, "1", "th", 0)
        for i in range(14)]
add(Footprint("TFT_2.4in_ILI9341_1x14", _tft, (-TFT_W / 2 - 0.25, -TFT_H / 2 - 0.25, TFT_W / 2 + 0.25, TFT_H / 2 + 0.25),
              (-TFT_W / 2, -TFT_H / 2, TFT_W / 2, TFT_H / 2), attr="through_hole",
              descr="footprint origin = module centre; module sits on a 2.5 mm header spacer above the front side"))


# ------------------------------------------------------------------ writer
def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def f(v):
    return f"{v:.4f}".rstrip("0").rstrip(".") if isinstance(v, float) else str(v)


def pad_sexpr(fp: Footprint, i: int, p, side="F", rot0=0.0, net=None, uid=None, pinfn=None):
    num, kind, shape, x, y, rot, w, h, drill, layers, rr = p
    layers = lay(layers)
    if side == "B":
        y, rot = -y, -rot
        layers = " ".join(l.replace("F.", "B.") for l in layers.split())
    ls = " ".join(q(l) for l in layers.split())
    ang = (rot + rot0) % 360
    s = f"(pad {q(num)} {kind} {shape} (at {f(x)} {f(y)}{(' ' + f(ang)) if ang else ''}) (size {f(w)} {f(h)})"
    if drill:
        s += f" (drill {drill})"
    s += f" (layers {ls})"
    if shape == "roundrect":
        s += f" (roundrect_rratio {rr})"
    if kind == "np_thru_hole":
        pass
    if net is not None:
        s += f" (net {net[0]} {q(net[1])})"
        if pinfn:
            s += f" (pinfunction {q(pinfn)}) (pintype \"passive\")"
    if i in fp.custom:
        prim = fp.custom[i]
        if side == "B":
            prim = prim.replace("(center 0 -0.6625) (end 0.6625 -0.6625)", "(center 0 0.6625) (end 0.6625 0.6625)")
        s += " " + prim
    s += f" (uuid {q(uid or _uuid.uuid5(NS, fp.name + str(i)))}))"
    return s


def rect_lines(b, layer, width, side="F"):
    x0, y0, x1, y1 = b
    if side == "B":
        y0, y1 = -y1, -y0
        layer = layer.replace("F.", "B.")
    return (f'(fp_rect (start {f(x0)} {f(y0)}) (end {f(x1)} {f(y1)}) (stroke (width {width}) (type solid)) (fill none) '
            f'(layer {q(layer)}) (uuid {q(_uuid.uuid5(NS, layer + str(b)))}))')


def pin1_marker(fp: Footprint, side="F"):
    nums = [p for p in fp.numbered() if p[0] in ("1", "A1")]
    if not nums or len(fp.numbered()) <= 2 and fp.name.startswith(("R_", "C_", "L_")):
        return ""
    p = nums[0]
    px, py, w = p[3], p[4], (p[6] if p[5] % 180 == 0 else p[7])
    cx = px - w / 2 - 0.3 if px <= 0 else px + w / 2 + 0.3
    cy = py
    if side == "B":
        cy = -cy
    layer = "F.SilkS" if side == "F" else "B.SilkS"
    return (f'(fp_circle (center {f(cx)} {f(cy)}) (end {f(cx + 0.12)} {f(cy)}) (stroke (width 0.12) (type solid)) (fill solid) '
            f'(layer {q(layer)}) (uuid {q(_uuid.uuid5(NS, fp.name + "pin1" + side))}))')


def keepout_sexpr(fp: Footprint, side="F", origin=(0.0, 0.0), rot=0.0, world=False):
    out = []
    for name, pts, layers in fp.keepouts:
        lay_s = '(layers "*.Cu")' if layers == "all" else f'(layer {q(layers if side == "F" else layers.replace("F.", "B."))})'
        P = [(x, -y if side == "B" else y) for x, y in pts]
        if world:
            P = [xform(origin, rot, x, y) for x, y in P]
        pts_s = " ".join(f"(xy {f(round(x, 4))} {f(round(y, 4))})" for x, y in P)
        out.append(f'(zone (net 0) (net_name "") {lay_s} (uuid {q(_uuid.uuid5(NS, fp.name + name + side + str(origin)))}) '
                   f'(name {q(name)}) (hatch edge 0.5) (connect_pads (clearance 0)) (min_thickness 0.25) '
                   f'(keepout (tracks not_allowed) (vias not_allowed) (pads not_allowed) (copperpour not_allowed) (footprints allowed)) '
                   f'(fill (thermal_gap 0.5) (thermal_bridge_width 0.5)) (polygon (pts {pts_s})))')
    return out


def xform(origin, rot, x, y):
    import math
    a = math.radians(rot)
    return (origin[0] + x * math.cos(a) + y * math.sin(a), origin[1] - x * math.sin(a) + y * math.cos(a))


def footprint_lib_sexpr(fp: Footprint) -> str:
    out = [f'(footprint {q(fp.name)} (version 20240108) (generator "nomiq_fplib") (generator_version "8.0") (layer "F.Cu")']
    if fp.descr:
        out.append(f"(descr {q(fp.descr)})")
    out.append(f'(property "Reference" "REF**" (at 0 {f(fp.crt[1] - 0.6)} 0) (layer "F.Fab") (uuid {q(_uuid.uuid5(NS, fp.name + "ref"))}) '
               f'(effects (font (size 0.5 0.5) (thickness 0.08))))')
    out.append(f'(property "Value" {q(fp.name)} (at 0 {f(fp.crt[3] + 0.6)} 0) (layer "F.Fab") (hide yes) (uuid {q(_uuid.uuid5(NS, fp.name + "val"))}) '
               f'(effects (font (size 0.5 0.5) (thickness 0.08))))')
    attr = {"smd": "(attr smd)", "through_hole": "(attr through_hole)", "exclude_from_pos_files": "(attr exclude_from_pos_files exclude_from_bom)"}[fp.attr]
    out.append(attr)
    out.append(rect_lines(fp.crt, "F.CrtYd", 0.05))
    out.append(rect_lines(fp.fab, "F.Fab", 0.1))
    m = pin1_marker(fp)
    if m:
        out.append(m)
    for i, p in enumerate(fp.pads):
        out.append(pad_sexpr(fp, i, p))
    out += keepout_sexpr(fp)
    if fp.model:
        out.append(f"(model {q(fp.model)} (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))")
    out.append(")")
    return "\n  ".join(out) + "\n"


def write_lib(dirpath: str) -> int:
    os.makedirs(dirpath, exist_ok=True)
    for name, fp in FP.items():
        with open(os.path.join(dirpath, name + ".kicad_mod"), "w") as fh:
            fh.write(footprint_lib_sexpr(fp))
    return len(FP)


def lib_name(footprint_field: str) -> str:
    """'Resistor_SMD:R_0402_1005Metric' -> 'R_0402_1005Metric'"""
    return footprint_field.split(":", 1)[-1]


def check(dirpath: str) -> list[str]:
    """Library on disk == generated; every footprint has numbered copper pads and a courtyard enclosing them."""
    errs = []
    for name, fp in FP.items():
        path = os.path.join(dirpath, name + ".kicad_mod")
        if not os.path.exists(path) or open(path).read() != footprint_lib_sexpr(fp):
            errs.append(f"{name}: lib/nomiq.pretty is stale - run python3 hardware/design/fplib.py")
        x0, y0, x1, y1 = fp.crt
        for p in fp.pads:
            hw, hh = (p[6] / 2, p[7] / 2) if p[5] % 180 == 0 else (p[7] / 2, p[6] / 2)
            if p[3] - hw < x0 - 1e-6 or p[3] + hw > x1 + 1e-6 or p[4] - hh < y0 - 1e-6 or p[4] + hh > y1 + 1e-6:
                errs.append(f"{name}: pad {p[0]!r} outside courtyard")
    return errs


if __name__ == "__main__":
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    d = os.path.join(here, "..", "kicad", "nomiq", "lib", "nomiq.pretty")
    if "--check" in sys.argv:
        e = check(d)
        print(f"{len(FP)} footprints, {'OK' if not e else 'ERRORS'}")
        for x in e:
            print("ERROR:", x)
        sys.exit(1 if e else 0)
    n = write_lib(d)
    print(f"{n} footprints written")

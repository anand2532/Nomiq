#!/usr/bin/env python3
"""Generate the KiCad 8 schematic of the Nomiq A0 board from nomiq_hw.py.

    python3 hardware/design/gen_kicad.py            # writes hardware/kicad/nomiq/*.kicad_sch + lib/nomiq.kicad_sym
    python3 hardware/design/gen_kicad.py --check    # regenerate in memory and verify connectivity only

Design style: every pin ends in a short wire stub with a *global label* carrying the net
name, so connectivity is explicit and identical to nomiq_hw.py (verified after writing by
re-parsing the files and rebuilding the netlist). One hierarchical sheet per design sheet.
Symbols are generated into the project library `nomiq` with datasheet pin names; pad
numbers are real for U1 (ESP32-S3-WROOM-1) and U2 (SX1262) and sequential placeholders
elsewhere (field "PadMap" = "unverified") — fix them when assigning final footprints.

Open nomiq.kicad_pro in KiCad 8/9, run ERC (`/hw-check`), then "Update PCB from schematic".
"""
from __future__ import annotations

import argparse
import collections
import os
import re
import sys
import uuid as _uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nomiq_hw as HW  # noqa: E402

OUT = os.path.abspath(os.path.join(HERE, "..", "kicad", "nomiq"))
G = 2.54  # grid
NS = _uuid.UUID("6f1b8c1e-3a7e-4c55-9d0e-6e6f6d697131")


def uid(*parts) -> str:
    """Deterministic UUIDs so regenerating gives stable diffs."""
    return str(_uuid.uuid5(NS, "/".join(str(p) for p in parts)))


# --------------------------------------------------------------------------- pad maps
ESP32_S3_WROOM_1 = {
    "GND": ["1", "40", "41"], "3V3": ["2"], "EN": ["3"], "IO4": ["4"], "IO5": ["5"], "IO6": ["6"], "IO7": ["7"],
    "IO15": ["8"], "IO16": ["9"], "IO17": ["10"], "IO18": ["11"], "IO8": ["12"], "IO19": ["13"], "IO20": ["14"],
    "IO3": ["15"], "IO46": ["16"], "IO9": ["17"], "IO10": ["18"], "IO11": ["19"], "IO12": ["20"], "IO13": ["21"],
    "IO14": ["22"], "IO21": ["23"], "IO47": ["24"], "IO48": ["25"], "IO45": ["26"], "IO0": ["27"], "IO35": ["28"],
    "IO36": ["29"], "IO37": ["30"], "IO38": ["31"], "IO39": ["32"], "IO40": ["33"], "IO41": ["34"], "IO42": ["35"],
    "RXD0": ["36"], "TXD0": ["37"], "IO2": ["38"], "IO1": ["39"],
}
SX1262_QFN24 = {
    "VDD_IN": ["1"], "GND": ["2", "5", "8", "20"], "XTA": ["3"], "XTB": ["4"], "DIO3": ["6"], "VREG": ["7"],
    "DCC_SW": ["9"], "VBAT": ["10"], "VBAT_IO": ["11"], "DIO2": ["12"], "DIO1": ["13"], "BUSY": ["14"],
    "NRESET": ["15"], "MISO": ["16"], "MOSI": ["17"], "SCK": ["18"], "NSS": ["19"], "RFI_P": ["21"],
    "RFI_N": ["22"], "RFO": ["23"], "VR_PA": ["24"], "EP": ["25"],
}
PADMAPS = {"U1": ESP32_S3_WROOM_1, "U2": SX1262_QFN24}


def pads_for(p: HW.Part) -> tuple[list[tuple[str, str]], bool]:
    """[(pin_name, pad_number)], verified?"""
    if p.ref in PADMAPS:
        pm = PADMAPS[p.ref]
        out = []
        for name in p.pins:
            for pad in pm[name]:
                out.append((name, pad))
        return out, True
    if p.pins == ["1", "2"] or p.pins == ["1"] or all(x.isdigit() for x in p.pins):
        return [(n, n) for n in p.pins], True
    return [(n, str(i + 1)) for i, n in enumerate(p.pins)], False


# --------------------------------------------------------------------------- s-expr helpers
def q(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def font(hide=False, justify=None):
    j = f" (justify {justify})" if justify else ""
    h = " (hide yes)" if hide else ""
    return f"(effects (font (size 1.27 1.27)){j}{h})"


def prop(name, val, x, y, hide=False, angle=0, justify=None):
    return f'(property {q(name)} {q(val)} (at {x:.2f} {y:.2f} {angle}) {font(hide, justify)})'


# --------------------------------------------------------------------------- symbol geometry
class Sym:
    """Box symbol: pins split left/right; 2-pin parts are drawn vertical."""

    def __init__(self, p: HW.Part):
        self.p = p
        self.lib_name = f"{p.ref}_{re.sub(r'[^A-Za-z0-9_]', '_', p.value)[:24]}"
        self.pads, self.verified = pads_for(p)
        self.two_pin = len(self.pads) <= 2 and set(n for n, _ in self.pads) <= {"1", "2"}
        self.pins = []   # (name, pad, lx, ly, angle)  lx/ly = connection point in library coords (y up)
        if self.two_pin:
            self.w, self.h = G * 2, G * 4
            for name, pad in self.pads:
                if name == "1":
                    self.pins.append((name, pad, 0.0, self.h / 2 + G, 270))
                else:
                    self.pins.append((name, pad, 0.0, -self.h / 2 - G, 90))
            return
        n = len(self.pads)
        left = self.pads[: (n + 1) // 2]
        right = self.pads[(n + 1) // 2:]
        rows = max(len(left), len(right))
        longest = max(len(nm) for nm, _ in self.pads)
        self.w = max(6, (longest * 2 + 4)) * G / 2 * 1.0
        self.w = round(self.w / G) * G
        self.h = (rows + 1) * G
        top = (rows - 1) * G / 2
        for i, (nm, pad) in enumerate(left):
            self.pins.append((nm, pad, -self.w / 2 - G, top - i * G, 0))
        for i, (nm, pad) in enumerate(right):
            self.pins.append((nm, pad, self.w / 2 + G, top - i * G, 180))

    def lib_sexpr(self) -> str:
        p = self.p
        name = self.lib_name
        s = [f'(symbol {q(name)} (pin_names (offset 0.508)) (exclude_from_sim no) (in_bom yes) (on_board yes)']
        s.append(prop("Reference", re.sub(r"\d+$", "", p.ref) or "U", 0, self.h / 2 + 2.54))
        s.append(prop("Value", p.value, 0, -self.h / 2 - 2.54))
        s.append(prop("Footprint", p.footprint, 0, 0, hide=True))
        s.append(prop("Datasheet", "", 0, 0, hide=True))
        s.append(prop("Description", p.note, 0, 0, hide=True))
        s.append(f'(symbol {q(name + "_0_1")} (rectangle (start {-self.w/2:.2f} {self.h/2:.2f}) (end {self.w/2:.2f} {-self.h/2:.2f}) '
                 f'(stroke (width 0.254) (type default)) (fill (type background))))')
        pins = []
        for nm, pad, x, y, ang in self.pins:
            pins.append(f'(pin passive line (at {x:.2f} {y:.2f} {ang}) (length {G}) (name {q(nm)} {font()}) (number {q(pad)} {font()}))')
        s.append(f'(symbol {q(name + "_1_1")} ' + " ".join(pins) + ")")
        s.append(")")
        return " ".join(s)


# --------------------------------------------------------------------------- sheet layout
def layout(parts):
    """Place parts left->right in rows; return {ref: (x, y)} in schematic coords (y down)."""
    pos = {}
    x, y, row_h = 40.0, 40.0, 0.0
    max_w = 360.0
    for p in parts:
        s = SYMS[p.ref]
        label_room = 32.0 if not s.two_pin else 12.0
        w = s.w + 2 * label_room
        h = s.h + (12.0 if s.two_pin else 16.0)
        if x + w > max_w:
            x, y, row_h = 40.0, y + row_h + 10.0, 0.0
        cx = round((x + w / 2) / G) * G
        cy = round((y + h / 2) / G) * G
        pos[p.ref] = (cx, cy)
        x += w + 6.0
        row_h = max(row_h, h)
    return pos, y + row_h + 30


def net_of_pin():
    m = {}
    for net, mem in HW.NETS.items():
        for k in mem:
            m[k] = net
    return m


def sheet_file(sheet: str) -> str:
    return f"{sheet}.kicad_sch"


def build_sheet(sheet: str, root_uuid: str, sheet_uuid: str, page: int) -> str:
    parts = sorted((p for p in HW.PARTS.values() if p.sheet == sheet),
                   key=lambda p: (0 if p.ref[0] in "UJY" else 1, p.ref[0], int(re.sub(r"\D", "", p.ref) or 0)))
    pos, height = layout(parts)
    pin_net = net_of_pin()
    syms = {SYMS[p.ref].lib_name: SYMS[p.ref] for p in parts}
    out = [f'(kicad_sch (version 20231120) (generator "nomiq_gen") (generator_version "8.0") (uuid {q(sheet_uuid)}) (paper "A2")',
           f'(title_block (title {q("Nomiq A0 — " + sheet)}) (rev "A0") (comment 1 "Generated from hardware/design/nomiq_hw.py - edit the spec, then regenerate"))']
    out.append("(lib_symbols " + " ".join(s.lib_sexpr().replace(f'(symbol "{n}"', f'(symbol "nomiq:{n}"', 1)
                                           for n, s in syms.items()) + ")")
    for p in parts:
        s = SYMS[p.ref]
        cx, cy = pos[p.ref]
        su = uid(sheet, p.ref)
        fields = [prop("Reference", p.ref, cx, cy - s.h / 2 - 2.54),
                  prop("Value", p.value, cx, cy + s.h / 2 + 2.54),
                  prop("Footprint", p.footprint, cx, cy, hide=True),
                  prop("Datasheet", "", cx, cy, hide=True),
                  prop("MPN", p.mpn, cx, cy, hide=True),
                  prop("Verify", "yes" if p.verify else "", cx, cy, hide=True),
                  prop("PadMap", "datasheet" if s.verified else "unverified", cx, cy, hide=True)]
        pins = " ".join(f'(pin {q(pad)} (uuid {q(uid(sheet, p.ref, "pin", pad))}))' for _, pad, *_ in s.pins)
        out.append(f'(symbol (lib_id {q("nomiq:" + s.lib_name)}) (at {cx:.2f} {cy:.2f} 0) (unit 1) (exclude_from_sim no) '
                   f'(in_bom yes) (on_board yes) (dnp no) (uuid {q(su)}) ' + " ".join(fields) + " " + pins +
                   f' (instances (project "nomiq" (path {q("/" + root_uuid + "/" + sheet_uuid)} (reference {q(p.ref)}) (unit 1)))))')
        # stubs + labels / no-connects
        for nm, pad, lx, ly, ang in s.pins:
            px, py = cx + lx, cy - ly           # library y-up -> schematic y-down
            net = pin_net.get(f"{p.ref}.{nm}")
            if net is None:
                out.append(f'(no_connect (at {px:.2f} {py:.2f}) (uuid {q(uid(sheet, p.ref, pad, "nc"))}))')
                continue
            dx, dy = {0: (-G, 0), 180: (G, 0), 270: (0, -G), 90: (0, G)}[ang]
            ex, ey = px + dx, py + dy
            out.append(f'(wire (pts (xy {px:.2f} {py:.2f}) (xy {ex:.2f} {ey:.2f})) (stroke (width 0) (type default)) (uuid {q(uid(sheet, p.ref, pad, "w"))}))')
            lab_ang, just = {0: (180, "right"), 180: (0, "left"), 270: (90, "left"), 90: (270, "right")}[ang]
            out.append(f'(global_label {q(net)} (shape bidirectional) (at {ex:.2f} {ey:.2f} {lab_ang}) (fields_autoplaced yes) '
                       f'{font(justify=just)} (uuid {q(uid(sheet, p.ref, pad, "l"))}) '
                       f'(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {ex:.2f} {ey:.2f} 0) {font(hide=True)}))')
    out.append(")")
    return "\n".join(out) + "\n"


def build_root(root_uuid: str, sheets: list[str], sheet_uuids: dict) -> str:
    out = [f'(kicad_sch (version 20231120) (generator "nomiq_gen") (generator_version "8.0") (uuid {q(root_uuid)}) (paper "A3")',
           '(title_block (title "Nomiq A0 — main board") (rev "A0") (comment 1 "Hierarchical sheets generated from hardware/design/nomiq_hw.py"))',
           "(lib_symbols)"]
    for i, s in enumerate(sheets):
        x = 30 + (i % 4) * 90
        y = 40 + (i // 4) * 60
        su = sheet_uuids[s]
        out.append(f'(sheet (at {x} {y}) (size 70 35) (fields_autoplaced yes) (stroke (width 0.1524) (type solid)) '
                   f'(fill (color 0 0 0 0.0000)) (uuid {q(su)}) '
                   f'{prop("Sheetname", s, x, y - 1, justify="left bottom")} '
                   f'{prop("Sheetfile", sheet_file(s), x, y + 36, justify="left top")} '
                   f'(instances (project "nomiq" (path {q("/" + root_uuid)} (page {q(str(i + 2))})))))')
    out.append('(sheet_instances (path "/" (page "1")))')
    out.append(")")
    return "\n".join(out) + "\n"


def build_lib() -> str:
    return "(kicad_symbol_lib (version 20231120) (generator \"nomiq_gen\") (generator_version \"8.0\")\n" + \
        "\n".join(s.lib_sexpr() for s in SYMS.values()) + "\n)\n"


# --------------------------------------------------------------------------- verification
TOK = re.compile(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+')


def parse(text: str):
    stack, cur = [], []
    for t in TOK.findall(text):
        if t == "(":
            stack.append(cur); cur = []
        elif t == ")":
            done = cur; cur = stack.pop(); cur.append(done)
        else:
            cur.append(t[1:-1].replace('\\"', '"') if t.startswith('"') else t)
    if stack:
        raise ValueError("unbalanced parentheses")
    return cur[0]


def find(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def verify(files: dict[str, str]) -> list[str]:
    """Rebuild connectivity from the generated sheets: pin endpoint -> wire -> global label."""
    errs = []
    got = collections.defaultdict(set)
    for name, text in files.items():
        if name == "nomiq.kicad_sch" or not name.endswith(".kicad_sch"):
            continue
        root = parse(text)
        libs = {s[1]: s for s in find(find(root, "lib_symbols")[0], "symbol")}
        endpoints = {}
        for sym in find(root, "symbol"):
            lib = libs[find(sym, "lib_id")[0][1]]
            at = find(sym, "at")[0]
            cx, cy = float(at[1]), float(at[2])
            ref = [p[2] for p in find(sym, "property") if p[1] == "Reference"][0]
            for unit in find(lib, "symbol"):
                for pin in find(unit, "pin"):
                    pa = find(pin, "at")[0]
                    nm = find(pin, "name")[0][1]
                    endpoints[(round(cx + float(pa[1]), 2), round(cy - float(pa[2]), 2))] = f"{ref}.{nm}"
        wires = {}
        for w in find(root, "wire"):
            pts = [(round(float(p[1]), 2), round(float(p[2]), 2)) for p in find(find(w, "pts")[0], "xy")]
            wires[pts[0]] = pts[1]; wires[pts[1]] = pts[0]
        labels = {(round(float(find(l, "at")[0][1]), 2), round(float(find(l, "at")[0][2]), 2)): l[1]
                  for l in find(root, "global_label")}
        for pt, pin in endpoints.items():
            far = wires.get(pt)
            if far is not None and far in labels:
                got[labels[far]].add(pin)
    want = {k: set(v) for k, v in HW.NETS.items()}
    for net in sorted(set(want) | set(got)):
        if want.get(net, set()) != got.get(net, set()):
            errs.append(f"net {net}: spec {sorted(want.get(net, []))} vs schematic {sorted(got.get(net, []))}")
    return errs


SYMS: dict[str, Sym] = {}


def generate() -> dict[str, str]:
    SYMS.clear()
    for p in HW.PARTS.values():
        SYMS[p.ref] = Sym(p)
    sheets = sorted({p.sheet for p in HW.PARTS.values()})
    root_uuid = uid("root")
    sheet_uuids = {s: uid("sheet", s) for s in sheets}
    files = {"nomiq.kicad_sch": build_root(root_uuid, sheets, sheet_uuids), "lib/nomiq.kicad_sym": build_lib()}
    for i, s in enumerate(sheets):
        files[sheet_file(s)] = build_sheet(s, root_uuid, sheet_uuids[s], i + 2)
    return files


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if HW.check():
        print("nomiq_hw.py checks fail - fix the spec first")
        return 1
    files = generate()
    for name, text in files.items():
        parse(text)   # syntax: balanced, tokenizable
    errs = verify(files)
    unverified = sorted(r for r, s in SYMS.items() if not s.verified)
    print(f"{len(SYMS)} symbols, {len(files) - 2} sheets, connectivity {'OK' if not errs else 'MISMATCH'}; "
          f"{len(unverified)} parts with placeholder pad numbers")
    for e in errs[:20]:
        print("ERROR:", e)
    if a.check:
        return 1 if errs else 0
    os.makedirs(os.path.join(OUT, "lib"), exist_ok=True)
    for name, text in files.items():
        with open(os.path.join(OUT, name), "w") as f:
            f.write(text)
    print(f"written to {os.path.relpath(OUT, HW.ROOT)}/")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())

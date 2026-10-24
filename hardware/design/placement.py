"""Component placement for the Nomiq A0 board (see board.py for the coordinate frame).

F side (KiCad top) = electronics, faces the battery/back cover.  B side = user interface, faces the front.
Floorplan (board frame, y = -62 is the top/antenna end):
    top end        RF: SX1262 + TCXO + PA filter + balun + switch + u.FL (x < -10), power key, camera FPC (x > 8)
    upper middle   ESP32-S3 module, antenna (unused radio) keep-out on the board edge at -x
    middle         I/O expander, microSD, camera LDOs, debug, reset/boot
    lower middle   audio amp, battery connector, fuel gauge
    bottom end     USB-C, ESD, charger, buck-boost, load switch, STT header, mic (port to the front)
    B side         TFT module (soldered through), 4 front keys, 2 side keys, power key, speaker connector
"""
from __future__ import annotations

import board as B
from board import near, place, place_fv, pad_pos  # noqa: F401

P = B.P


def build():
    B.PL.clear()
    # ---------------------------------------------------------------- mounting holes (boss faces)
    for i, (x, y) in enumerate(B.HOLES):
        place(f"H{i + 1}", x, y)

    # ---------------------------------------------------------------- 03_radio (top-left in board frame)
    place("U2", -19.5, -45.0, 0)
    # supply / regulator
    place("C32", -23.3, -46.23, 180)                       # VDD_IN decoupling, pad1 next to pin 1
    place("C33", -16.8, -40.4, 270)                        # VBAT/VBAT_IO bulk (plane-connected)
    place("R30", -14.0, -27.5, 90)                         # NSS pull-up, on the way to the ESP32 (keeps U2's SPI side free)
    # TCXO + DC-DC: hand-placed and hand-routed (HAND_TRACKS in gen_pcb.py)
    place("Y1", -25.6, -44.5, 270)                         # OUT (pad 3) faces R31, VCC (pad 4) above it
    place("C35", -25.0, -47.0, 90)
    place("R31", -23.3, -45.25, 0)                         # TCXO_OUT -> XTA (pin 3)
    place("C34", -22.6, -43.8, 270)
    place("L2", -20.25, -40.4, 180)                        # pad1 DCC_SW under pin 9, pad2 VREG under pin 7
    place("C30", -23.0, -41.8, 180)
    # RF chain: hand-placed (see RF_TRACKS in gen_pcb.py); TX column x=-22.3 left, RX balun right, both up to U3
    place("L3", -22.3, -48.685, 90)                         # VR_PA choke: pad1 VR_PA (bottom), pad2 RFO (top)
    place("C31", -23.4, -47.2, 180)                        # VR_PA decoupling
    place("L4", -22.3, -50.6, 90)
    place("C36", -23.7, -49.65, 180)                        # PA match shunt at the RFO node
    place("L5", -22.3, -52.6, 90)
    place("C37", -23.7, -51.6, 180)
    place("C39", -22.3, -54.6, 90)                          # DC block -> RF1
    place("C38", -23.7, -53.6, 180)
    place("L6", -20.3, -50.5, 270)                          # balun series L: pad2 RFI_N (bottom)
    place("C44", -18.9, -50.5, 270)                         # balun series C: pad2 RFI_P (bottom)
    place("L7", -17.45, -49.9, 0)                           # RFI_P shunt
    place("C45", -21.3, -51.3, 90)                          # RFI_N shunt
    place("C43", -19.6, -52.4, 270)                         # DC block RF2 <- RX_IN
    place("U3", -21.0, -57.0, 90)
    place("C46", -22.4, -59.0, 180)                         # antenna pi shunt 1 (DNP)
    place("C47", -21.0, -59.6, 90)                          # series DC block
    place("J4", -17.2, -59.5, 0)
    place("D3", -21.4, -61.3, 0)
    place("C48", -23.2, -60.5, 0)                           # antenna pi shunt 2 (DNP)
    place("TP11", -11.5, -27.5)
    place("TP12", -9.0, -27.5)

    # ---------------------------------------------------------------- 02_mcu
    place("U1", -16.35, -12.0, 90)                         # antenna end flush with the -x edge
    near("C22", "U1", "3V3", 0.0, 1.8, 0)
    near("C21", "U1", "3V3", -2.6, 1.9, 0)
    near("R20", "U1", "EN", 1.2, 1.8, 90)
    near("C20", "U1", "EN", 2.4, 1.8, 90)
    place("SW1", -27.0, 3.5, 0)                            # RESET (developer, inside case)
    place("SW2", -27.0, 8.8, 0)                            # BOOT
    place("R21", -22.6, 8.8, 90)
    place("J3", -26.5, 14.6, 0)                            # Tag-Connect
    near("R22", "U1", "IO20", 0.0, 2.0, 90)
    near("R23", "U1", "IO19", 0.0, 2.0, 90)
    place("TP10", -21.6, 4.0)
    # I/O expander + I2C pull-ups
    place("U4", 4.0, -14.0, 0)
    near("C23", "U4", "VCC", 2.0, -0.6, 0)
    place("R24", 9.0, -19.6, 0)
    place("R25", 9.0, -18.4, 0)
    place("TP8", 11.4, -21.0)
    place("TP9", 11.4, -18.6)

    # ---------------------------------------------------------------- 05_camera (top-right)
    place("J6", 19.0, -38.0, 180)                          # flex leaves towards the camera (-y)
    place("U15", 12.0, -31.0, 0)
    near("C50", "U15", "CT", 1.6, 0.4, 90)
    near("R50", "U15", "ON", -1.8, 0.6, 90)
    place("U12", 18.0, -31.0, 0)
    place("U13", 24.0, -31.0, 0)
    near("C51", "U15", "VOUT", 1.7, -0.8, 90)
    near("C52", "U12", "OUT", 1.7, -0.3, 90)
    near("C53", "U13", "OUT", 1.7, -0.3, 90)
    place("R51", 7.0, -26.5, 0)

    # ---------------------------------------------------------------- 06_display_storage
    place("J8", 20.5, 12.5, 0)                             # microSD
    near("C61", "J8", "VDD", 0.0, -2.0, 0)
    near("C62", "J8", "VDD", 1.6, -2.0, 0)
    place("R63", 24.6, 4.0, 90)
    place("R64", 25.8, 4.0, 90)
    place("R65", 27.0, 4.0, 90)
    place("Q2", 4.0, -2.0, 0)                              # backlight switch
    place("Q1", 8.0, -2.0, 0)
    place("R61", 1.4, -2.0, 90)
    place("R66", 6.0, -4.4, 0)
    place("R60", 10.6, -2.0, 90)
    place("R62", 1.4, -5.6, 90)
    place("C60", -3.0, -5.0, 90)

    # ---------------------------------------------------------------- 04_audio
    place("U6", 10.0, 36.0, 0)
    near("C42", "U6", "VDD", 1.2, 1.9, 90)
    near("C41", "U6", "VDD", 2.6, 1.9, 90)
    place("R41", 6.76, 35.75, 180)                         # pad1 AMP_GAIN next to pin 2
    place("R43", 6.76, 36.75, 180)                         # pad1 AMP_SD_N next to pin 4
    place("U5", 19.0, 56.0, 0)                             # mic: port hole -> front-face mic hole (MIC_X/MIC_Y)
    near("C40", "U5", "VDD", 1.7, 0.0, 90)
    place("R40", 20.9, 53.2, 90)                           # I2S_DIN pull-down, right of the mic (keeps WS exit free)

    # ---------------------------------------------------------------- 01_power (bottom end)
    place("J1", 0.0, 58.9, 0)                              # USB-C, opening at +y (bottom end)
    place("R1", -3.4, 53.6, 90)
    place("R2", 3.4, 53.6, 90)
    place("D1", -6.5, 52.4, 90)
    place("D2", 6.8, 53.4, 90)
    place("C1", 9.2, 53.4, 90)
    place("U8", -12.0, 48.0, 0)                            # charger: ISET/ITERM/TMR on top, ILIM top-right
    near("R3", "U8", "ISET", -1.2, -2.2, 90)               # GND ends point away (fan-out above)
    near("R5", "U8", "ITERM", 0.0, -2.2, 90)
    near("R6", "U8", "TMR", 1.2, -2.2, 90)
    near("R4", "U8", "ILIM", 2.0, -1.0, 90)
    near("C2", "U8", "IN", 2.6, -2.4, 90)
    near("C3", "U8", "OUT", 2.4, 0.9, 90)
    near("C4", "U8", "BAT", -2.4, 0.2, 90)
    place("R7", -8.0, 52.0, 90)
    place("R8", -9.4, 52.0, 90)
    place("TP1", -16.6, 50.0)
    place("J2", -20.0, 37.0, 0)                            # battery connector (plug from +y)
    place("U9", -10.0, 38.0, 0)                            # fuel gauge
    near("C5", "U9", "VDD", -1.9, 0.0, 90)
    place("R9", -10.0, 35.0, 0)
    place("U10", 14.0, 47.0, 0)                            # buck-boost: L1/L2/VIN/VOUT pins on the left
    place("L1", 9.5, 47.0, 90)                             # pad1 BB_L1 (pin 4, lower), pad2 BB_L2 (pin 2, upper)
    place("C6", 13.2, 50.2, 0)                             # VIN, next to pin 5
    place("C7", 13.4, 43.6, 0)                             # VOUT, next to pin 1
    place("C8", 13.4, 41.8, 0)
    place("U14", 25.0, 41.0, 0)                            # STT load switch
    near("C9", "U14", "CT", 1.7, 0.0, 90)
    near("C10", "U14", "VIN", -2.3, 0.0, 90)
    near("R13", "U14", "ON", -2.3, 0.4, 90)
    place("C11", 25.0, 37.6, 0)
    place("J9", 25.0, 30.0, 0)                             # STT SoM header
    for i, x in enumerate((-4.5, -2.0, 2.5, 5.0, 7.5)):
        place(f"TP{3 + i}", x, 50.5)

    # ---------------------------------------------------------------- 07_ui / 06 display (B side, front view u,v)
    place_fv("J7", 0.0, P.DISPLAY_Y, 0)                    # TFT module, soldered through
    btn = {"SW3": (0.0, P.BTN_CENTER_Y + P.BTN_PITCH), "SW4": (0.0, P.BTN_CENTER_Y - P.BTN_PITCH),
           "SW5": (0.0, P.BTN_CENTER_Y), "SW6": (P.BTN_PITCH * 1.6, P.BTN_CENTER_Y)}
    for ref, (u, v) in btn.items():
        place_fv(ref, u, v, 0)
    # side keys: EVQ-P7A actuator (+Y local) over the board edge
    place("SW7", B.HX - 1.6, -P.REC_BTN_Y, 270, "B")       # left side (front view) = +x
    place("SW8", -B.HX + 1.6, -P.CAM_BTN_Y, 90, "B")       # right side = -x
    place("SW9", -P.PWR_BTN_X, -B.HY + 1.6, 0, "B")        # top end
    place_fv("J5", -16.0, -38.0, 0)                        # speaker connector, beside the speaker
    # button pull-ups + ESD on F side behind each key
    keys = ["SW3", "SW4", "SW5", "SW6", "SW7", "SW8"]
    for i, k in enumerate(keys):
        kx, ky = B.PL[k].x, B.PL[k].y
        if k == "SW7":
            kx -= 4.0
        if k == "SW8":
            kx += 4.0
        place(f"R7{i}", kx - 1.0, ky + 3.0, 90)
        place(f"D7{i}", kx + 1.0, ky + 3.0, 90)
    return B.PL


# ------------------------------------------------------------------ legalisation
FIXED_PREFIX = ("U", "J", "SW", "H", "Y")
FIXED = {"C32", "Y1", "C35", "R31", "C34", "L2", "C30", "R41", "R43", "L3", "C31", "L4", "C36", "L5", "C37", "C39", "C38", "L6", "C44", "L7", "C45", "C43", "C46", "C47", "D3", "C48"}
EDGE_OK = {"J1", "SW7", "SW8", "SW9", "U1"}


def _ov(a, b, m=0.0):
    if len(b) == 5:      # (x, y, r, 0, "circ") hole keep-out
        a, b = b, a
    if len(a) == 5:
        x, y, r = a[0], a[1], a[2]
        dx = max(b[0] - x, 0, x - b[2])
        dy = max(b[1] - y, 0, y - b[3])
        return dx * dx + dy * dy < (r + m) ** 2
    return a[0] < b[2] + m and b[0] < a[2] + m and a[1] < b[3] + m and b[1] < a[3] + m


def blockers(pl):
    """axis-aligned obstacles per side: (bbox, owner)"""
    out = {"F": [], "B": []}
    for r, p in pl.items():
        bb = p.bbox(p.fp.crt)
        if r.startswith("H"):
            for s in out:
                out[s].append(((p.x, p.y, B.HOLE_KEEPOUT_R, 0, "circ"), r))
            continue
        if r == "J7":     # module body on B, pins through both sides
            out["B"].append((bb, r))
            for pd in p.fp.pads:
                x, y, _ = p.pad_world(pd)
                out["F"].append(((x - 1.1, y - 1.1, x + 1.1, y + 1.1), r))
            continue
        out[p.side].append((bb, r))
        for name, pts, layers in p.fp.keepouts:
            if layers == "all":
                w = [p.world(x, y) for x, y in pts]
                kb = (min(q[0] for q in w), min(q[1] for q in w), max(q[0] for q in w), max(q[1] for q in w))
                for s in out:
                    if s != p.side:
                        out[s].append((kb, r + ":keepout"))
                out[p.side].append((kb, r + ":keepout"))
    return out


def inside(bb, ref):
    if ref in EDGE_OK:
        return True
    return bb[0] >= -B.HX + 0.2 and bb[2] <= B.HX - 0.2 and bb[1] >= -B.HY + 0.2 and bb[3] <= B.HY - 0.2


def legalize(pl, log=None):
    movable = [r for r in pl if not r.startswith(FIXED_PREFIX) and r not in FIXED]
    fixed = {r: p for r, p in pl.items() if r not in movable}
    settled = blockers(fixed)
    moved = []
    for r in movable:
        p = pl[r]
        x0, y0, a0 = p.x, p.y, p.a
        best = None
        for rad in [0.0] + [0.1 * k for k in range(1, 60)]:
            cands = [(x0, y0)] if rad == 0 else []
            if rad:
                n = max(8, int(2 * 3.1416 * rad / 0.1))
                import math
                cands = [(x0 + rad * math.cos(2 * math.pi * i / n), y0 + rad * math.sin(2 * math.pi * i / n)) for i in range(n)]
            for (x, y) in cands:
                for a in (a0, a0 + 90):
                    p.x, p.y, p.a = x, y, a % 360
                    bb = p.bbox(p.fp.crt)
                    if not inside(bb, r):
                        continue
                    if any(_ov(bb, ob, 0.05) for ob, _ in settled[p.side]):
                        continue
                    best = (x, y, a % 360)
                    break
                if best:
                    break
            if best:
                break
        if best is None:
            p.x, p.y, p.a = x0, y0, a0
            if log is not None:
                log.append(f"could not legalise {r}")
        else:
            p.x, p.y, p.a = best
            if (abs(best[0] - x0) > 1e-6 or abs(best[1] - y0) > 1e-6) and log is not None:
                moved.append(r)
        settled[p.side].append((p.bbox(p.fp.crt), r))
    if log is not None:
        log.append(f"legalised {len(moved)} parts: {' '.join(moved)}")
    return pl


def check(pl):
    errs = []
    allb = blockers({r: p for r, p in pl.items()})
    items = list(pl.items())
    for side in ("F", "B"):
        obs = allb[side]
        for i in range(len(obs)):
            for j in range(i + 1, len(obs)):
                (b1, r1), (b2, r2) = obs[i], obs[j]
                if r1.split(":")[0] == r2.split(":")[0]:
                    continue
                if _ov(b1, b2):
                    errs.append(f"{side}: {r1} overlaps {r2}")
    for r, p in items:
        if r.startswith("H"):
            continue
        if not inside(p.bbox(p.fp.crt), r):
            errs.append(f"{r} outside the board")
    return sorted(set(errs))

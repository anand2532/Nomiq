# Mechanical

* `enclosure/params.py` — every dimension (single source of truth)
* `enclosure/enclosure.py` — build123d model → `exports/front_shell.{step,stl}`, `exports/back_shell.{step,stl}`,
  `exports/button_caps.{step,stl}`; prints an interference check of the PCB, display module, battery and
  USB-C envelopes against both shells and exits non-zero on a collision
* `exports/` — generated (CI artefact). Put the KiCad board STEP here as `pcb.step` for fit checks.

```bash
pip install build123d
python3 mechanical/enclosure/enclosure.py --out mechanical/exports
```

## v0.2 — fitted to PCB A0 (130 × 70 × 30 mm, portrait)

| Feature | Location |
|---|---|
| 2.4" display window 38 × 50 | front, upper half |
| Nav cross (UP/DOWN/SELECT) + BACK, printed caps on PTS810 switches | front, below display |
| Speaker grille Ø22 | front, bottom |
| Record (PTT, 14 mm long) | left side |
| Camera button | right side |
| USB-C (on the back-facing PCB side, Z = PCB_BOT_Z − 1.6) | bottom end |
| Mic port Ø1.2 over the bottom-port mic's PCB hole (gasket) | front, bottom-left |
| SMA bulkhead + power button | top end |
| Camera lens Ø9 | back, top-left |
| Battery pocket 52 × 82 (ribs) | back shell |
| 4 × M2.5 screws from back into heat-set inserts | corners |

Split: front shell 11 mm deep (display + keys + speaker), back shell 19 mm (battery + camera + STT module).
The PCB (64.6 × 124.6 × 1.6) sits on the split plane: front bosses press on its front face, back bosses stop
under it (Z = PCB_BOT_Z) and the four M2.5 screws clamp the board between them. Electronics face the back
cover; the display module is soldered through the board on a 2.5 mm spacer.

Print: PETG or ASA, 0.2 mm layers, 5 perimeters, front shell face-down on textured sheet; back shell face-down. Inserts: M2.5 × 4 mm heat-set.

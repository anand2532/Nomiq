# Mechanical

* `enclosure/params.py` — every dimension (single source of truth)
* `enclosure/enclosure.py` — build123d model → `exports/front_shell.{step,stl}`, `exports/back_shell.{step,stl}`
* `exports/` — generated (CI artefact). Put the KiCad board STEP here as `pcb.step` for fit checks.

```bash
pip install build123d
python3 mechanical/enclosure/enclosure.py --out mechanical/exports
```

## v0.1 concept (130 × 70 × 30 mm, portrait)

| Feature | Location |
|---|---|
| 2.4" display window 38 × 50 | front, upper half |
| Nav cross (UP/DOWN/SELECT) + BACK | front, below display |
| Speaker grille Ø22 | front, bottom |
| Record (PTT, 14 mm long) | left side |
| Camera button | right side |
| USB-C + mic port | bottom end |
| SMA bulkhead + power button | top end |
| Camera lens Ø9 | back, top-left |
| Battery pocket 52 × 82 (ribs) | back shell |
| 4 × M2.5 screws from back into heat-set inserts | corners |

Split: front shell 11 mm deep (display + PCB), back shell 19 mm (battery + STT module).

Print: PETG or ASA, 0.2 mm layers, 5 perimeters, front shell face-down on textured sheet; back shell face-down. Inserts: M2.5 × 4 mm heat-set.

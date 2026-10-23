---
description: Rebuild the parametric enclosure and report geometry checks
---
Run `python3 mechanical/enclosure/enclosure.py --out mechanical/exports` and report validity, volume and bounding boxes. If `mechanical/exports/pcb.step` exists, check clearances against it using the build123d MCP server. $ARGUMENTS

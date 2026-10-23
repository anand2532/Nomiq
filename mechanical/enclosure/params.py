"""Single source of truth for enclosure dimensions (mm).

Coordinate system (device held in portrait, display facing you):
  X = width (left -> right), Y = length (bottom/USB end -> top/antenna end),
  Z = thickness (back -> front). Origin = centre of the outer bounding box.

Every opening references a component in hardware/bom/bom-candidates.csv. When a
part is finalised, update the matching block here and re-run enclosure.py.
"""

# ---- Envelope (brief §9: ~130 x 70 x 30, preliminary) ----
LENGTH = 130.0          # Y
WIDTH = 70.0            # X
HEIGHT = 30.0           # Z
WALL = 2.2              # FDM: 2.2 mm = 5 perimeters @ 0.45 mm
CORNER_R = 8.0          # plan-view corner radius
EDGE_R = 2.0            # front/back edge rounding
FRONT_DEPTH = 11.0      # front shell depth (display + PCB); back shell takes the rest

# ---- Fasteners: M2.5 x 16 screws from the back into heat-set inserts in the front ----
BOSS_INSET = 5.5        # boss centre inset from inner wall corner (x and y)
BOSS_OD = 7.0
INSERT_D = 3.6          # M2.5 heat-set insert pilot
INSERT_DEPTH = 5.0
SCREW_CLEAR_D = 2.8
SCREW_HEAD_D = 5.2
SCREW_HEAD_DEPTH = 2.0

# ---- Display: 2.4" ILI9341 (module 42.7 x 60, active ~36.7 x 48.9) ----
DISPLAY_WIN_W = 38.0    # X (portrait)
DISPLAY_WIN_H = 50.0    # Y
DISPLAY_Y = 22.0        # window centre Y
DISPLAY_R = 1.5

# ---- Front buttons: D-pad style (UP/DOWN/SELECT/BACK) ----
BTN_D = 7.0
BTN_CENTER_Y = -18.0
BTN_PITCH = 11.0

# ---- Speaker grille (28 mm speaker) ----
SPK_Y = -45.0
SPK_HOLE_D = 1.6
SPK_PITCH = 3.0
SPK_FIELD_D = 22.0

# ---- Side buttons ----
REC_BTN_Y = 10.0        # left side (-X), thumb-operated PTT
REC_BTN_LEN = 14.0      # long, easy to find
CAM_BTN_Y = 30.0        # right side (+X)
SIDE_BTN_LEN = 8.0
SIDE_BTN_H = 4.0
SIDE_BTN_Z = 4.5        # centre Z (in front shell, PCB-mounted side switches)

# ---- End faces ----
USBC_W = 9.6
USBC_H = 3.8
USBC_Z = 3.0            # bottom end (-Y)
MIC_D = 1.2             # bottom end, next to USB-C
MIC_X = 12.0
SMA_D = 6.6             # top end (+Y), bulkhead SMA
SMA_X = 22.0
SMA_Z = -4.0
PWR_BTN_X = -18.0       # top end
PWR_BTN_W = 8.0

# ---- Rear camera (OV2640 lens holder M12/8 mm) ----
CAM_X = -20.0
CAM_Y = 48.0
CAM_D = 9.0

# ---- Battery pocket in back shell (placeholder: 1S pouch 50 x 80 x 8, 3000 mAh class) ----
BATT_W = 52.0
BATT_L = 82.0
BATT_Y = -8.0
BATT_RIB = 1.2
BATT_RIB_H = 6.0

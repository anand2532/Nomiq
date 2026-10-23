/* Power: fuel gauge, charger status, rail gating, sleep policy. Phase 6. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum { RAIL_CAMERA, RAIL_STT, RAIL_AMP, RAIL_LCD_BL } power_rail_t;
esp_err_t power_init(void);
esp_err_t power_rail(power_rail_t r, bool on);   /* enforces interlocks (no CAM+STT+AMP peak) */
int       power_battery_pct(void);
int       power_battery_mv(void);
bool      power_is_charging(void);
void      power_idle_tick(void);                 /* display timeout, light-sleep entry */

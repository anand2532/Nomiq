/* Power: fuel gauge, charger status, rail gating with interlocks (power_policy.h). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "power_policy.h"

esp_err_t       power_init(void);
power_verdict_t power_rail(power_rail_t r, bool on);
/* Poll the gauge (call every ~10 s). Returns the battery level classification. */
battery_level_t power_update(void);
int             power_battery_pct(void);
int             power_battery_mv(void);
bool            power_is_charging(void);

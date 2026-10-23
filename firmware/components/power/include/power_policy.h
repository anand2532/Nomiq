/* Power policy: rail interlocks and battery thresholds (docs/specs/power-budget.md). Pure C. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { RAIL_CAMERA = 0, RAIL_STT, RAIL_AMP, RAIL_LCD_BL, RAIL__N } power_rail_t;
typedef enum { PWR_OK = 0, PWR_DENIED_INTERLOCK, PWR_DENIED_BATTERY } power_verdict_t;
typedef enum { BAT_NORMAL = 0, BAT_LOW, BAT_CRITICAL, BAT_SHUTDOWN } battery_level_t;

#define PWR_LOW_PCT        15
#define PWR_CRITICAL_PCT   5     /* no camera / STT below this */
#define PWR_SHUTDOWN_MV    3350  /* graceful power-off (data flushed) */

typedef struct {
    bool on[RAIL__N];
    uint8_t soc_pct;
    uint16_t vbat_mv;
    bool charging;
} power_state_t;

void            power_policy_init(power_state_t *s);
power_verdict_t power_policy_request(power_state_t *s, power_rail_t r, bool on);
battery_level_t power_policy_battery(power_state_t *s, uint8_t soc_pct, uint16_t vbat_mv, bool charging);

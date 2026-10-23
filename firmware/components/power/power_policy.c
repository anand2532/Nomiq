#include "power_policy.h"

#include <string.h>

void power_policy_init(power_state_t *s) { memset(s, 0, sizeof *s); s->soc_pct = 100; s->vbat_mv = 4000; }

power_verdict_t power_policy_request(power_state_t *s, power_rail_t r, bool on)
{
    if (r >= RAIL__N) return PWR_DENIED_INTERLOCK;
    if (!on) { s->on[r] = false; return PWR_OK; }
    /* camera (~100 mA + LDOs) and the STT SoM (boot spike up to ~0.6 A) never run together;
     * this keeps the VSYS peak inside the 2 A budget even with the amplifier playing. */
    if ((r == RAIL_CAMERA && s->on[RAIL_STT]) || (r == RAIL_STT && s->on[RAIL_CAMERA])) return PWR_DENIED_INTERLOCK;
    if ((r == RAIL_CAMERA || r == RAIL_STT) && !s->charging && s->soc_pct < PWR_CRITICAL_PCT) return PWR_DENIED_BATTERY;
    s->on[r] = true;
    return PWR_OK;
}

battery_level_t power_policy_battery(power_state_t *s, uint8_t soc, uint16_t mv, bool charging)
{
    s->soc_pct = soc; s->vbat_mv = mv; s->charging = charging;
    if (!charging && mv && mv < PWR_SHUTDOWN_MV) return BAT_SHUTDOWN;
    if (charging) return BAT_NORMAL;
    if (soc < PWR_CRITICAL_PCT) {
        s->on[RAIL_CAMERA] = false;       /* shed heavy loads */
        s->on[RAIL_STT] = false;
        return BAT_CRITICAL;
    }
    return soc < PWR_LOW_PCT ? BAT_LOW : BAT_NORMAL;
}

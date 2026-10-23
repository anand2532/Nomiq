#include <stdio.h>
#include <string.h>

#include "fuel_gauge.h"
#include "power_policy.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint16_t REG[256];
static int fail_bus;
static int rd(uint8_t r, uint16_t *v, void *c) { (void)c; if (fail_bus) return -1; *v = REG[r]; return 0; }
static int wr(uint8_t r, uint16_t v, void *c) { (void)c; if (fail_bus) return -1; REG[r] = v; return 0; }

int main(void)
{
    fg_bus_t b = { rd, wr, NULL };
    REG[FG_REG_VERSION] = 0x0012; REG[FG_REG_CONFIG] = 0x971C | 0x0020;   /* default RCOMP 0x97, ALRT set */
    CHECK(fg_init(&b, 10) == 0);
    CHECK(REG[FG_REG_CONFIG] == 0x9716);                                   /* ATHD = 22 -> 10 %, ALRT cleared */
    REG[FG_REG_VCELL] = 49920;  uint16_t mv; CHECK(fg_voltage_mv(&b, &mv) == 0 && mv == 3900);  /* 49920*78.125uV */
    REG[FG_REG_SOC] = (57 << 8) | 200; uint8_t p; uint16_t x; CHECK(fg_soc(&b, &p, &x) == 0 && p == 58 && x == ((57 << 8) | 200));
    REG[FG_REG_SOC] = (101 << 8); CHECK(fg_soc(&b, &p, NULL) == 0 && p == 100);
    REG[FG_REG_CRATE] = (uint16_t)(int16_t)-50; int16_t cr; CHECK(fg_charge_rate(&b, &cr) == 0 && cr == -1040);
    REG[FG_REG_CONFIG] |= 0x20; bool al; CHECK(fg_alert_active(&b, &al) == 0 && al);
    CHECK(fg_alert_clear(&b) == 0 && fg_alert_active(&b, &al) == 0 && !al);
    REG[FG_REG_VERSION] = 0x0040; CHECK(fg_init(&b, 10) == -2);          /* wrong chip */
    fail_bus = 1; CHECK(fg_voltage_mv(&b, &mv) == -1); fail_bus = 0;

    power_state_t s; power_policy_init(&s);
    CHECK(power_policy_request(&s, RAIL_CAMERA, true) == PWR_OK);
    CHECK(power_policy_request(&s, RAIL_STT, true) == PWR_DENIED_INTERLOCK);
    CHECK(power_policy_request(&s, RAIL_AMP, true) == PWR_OK);
    CHECK(power_policy_request(&s, RAIL_CAMERA, false) == PWR_OK);
    CHECK(power_policy_request(&s, RAIL_STT, true) == PWR_OK);
    CHECK(power_policy_request(&s, RAIL_CAMERA, true) == PWR_DENIED_INTERLOCK);
    CHECK(power_policy_battery(&s, 50, 3800, false) == BAT_NORMAL);
    CHECK(power_policy_battery(&s, 12, 3650, false) == BAT_LOW);
    CHECK(power_policy_battery(&s, 4, 3500, false) == BAT_CRITICAL && !s.on[RAIL_STT]);   /* load shed */
    CHECK(power_policy_request(&s, RAIL_STT, true) == PWR_DENIED_BATTERY);
    CHECK(power_policy_battery(&s, 4, 3500, true) == BAT_NORMAL);                        /* charging */
    CHECK(power_policy_request(&s, RAIL_STT, true) == PWR_OK);
    CHECK(power_policy_battery(&s, 2, 3300, false) == BAT_SHUTDOWN);
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

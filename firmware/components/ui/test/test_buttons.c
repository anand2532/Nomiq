#include <stdio.h>

#include "buttons.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static btn_state_t S;
static btn_event_t E[16];
static int count(uint32_t from, uint32_t to, uint8_t raw_pressed_mask, uint8_t btn, btn_ev_t ev)
{
    int c = 0;
    for (uint32_t t = from; t < to; t += 10) {
        int n = btn_sample(&S, (uint8_t)~raw_pressed_mask, t, E, 16);
        for (int i = 0; i < n; i++) if (E[i].button == btn && E[i].ev == ev) c++;
    }
    return c;
}

int main(void)
{
    btn_init(&S);
    CHECK(count(0, 100, 0, 0, BTN_EV_PRESS) == 0);
    /* short press of SELECT (bit 2) */
    CHECK(count(100, 300, 1u << 2, 2, BTN_EV_PRESS) == 1);
    CHECK(count(300, 400, 0, 2, BTN_EV_SHORT) == 1);
    /* bounce: 10 ms glitches are ignored */
    int ev = 0;
    for (uint32_t t = 400; t < 600; t += 10) ev += btn_sample(&S, (uint8_t)~((t / 10) % 2 ? 1u << 3 : 0), t, E, 16);
    CHECK(ev == 0);
    count(600, 700, 0, 3, BTN_EV_PRESS);
    /* long press of RECORD (bit 4): PRESS, LONG once, RELEASE, no SHORT */
    CHECK(count(700, 1000, 1u << 4, 4, BTN_EV_PRESS) == 1);
    CHECK(count(1000, 2000, 1u << 4, 4, BTN_EV_LONG) == 1);
    btn_init(&S);
    int rel = 0, sh = 0;
    for (uint32_t t = 0; t < 2000; t += 10) {
        uint8_t m = t < 1200 ? (1u << 4) : 0;
        int n = btn_sample(&S, (uint8_t)~m, t, E, 16);
        for (int i = 0; i < n; i++) { if (E[i].ev == BTN_EV_RELEASE) rel++; if (E[i].ev == BTN_EV_SHORT) sh++; }
    }
    CHECK(rel == 1 && sh == 0);
    /* two buttons at once */
    btn_init(&S);
    CHECK(count(0, 200, (1u << 0) | (1u << 1), 0, BTN_EV_PRESS) == 1);
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

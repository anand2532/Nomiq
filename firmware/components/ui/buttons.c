#include "buttons.h"

#include <string.h>

void btn_init(btn_state_t *s) { memset(s, 0, sizeof *s); }

int btn_sample(btn_state_t *s, uint8_t raw_al, uint32_t now, btn_event_t *out, int max)
{
    int n = 0;
    uint8_t raw = (uint8_t)~raw_al & ((1u << BTN_COUNT) - 1u);
    for (uint8_t b = 0; b < BTN_COUNT; b++) {
        uint8_t m = (uint8_t)(1u << b);
        if ((raw & m) != (s->last_raw & m)) s->raw_since[b] = now;           /* edge: restart debounce */
        bool settled = now - s->raw_since[b] >= BTN_DEBOUNCE_MS;
        bool is = (raw & m) != 0, was = (s->stable & m) != 0;
        if (settled && is != was) {
            if (is) {
                s->stable |= m; s->pressed_at[b] = now; s->long_sent[b] = false;
                if (n < max) out[n++] = (btn_event_t){ b, BTN_EV_PRESS };
            } else {
                s->stable &= (uint8_t)~m;
                if (n < max) out[n++] = (btn_event_t){ b, BTN_EV_RELEASE };
                if (!s->long_sent[b] && n < max) out[n++] = (btn_event_t){ b, BTN_EV_SHORT };
            }
        } else if (was && !s->long_sent[b] && now - s->pressed_at[b] >= BTN_LONG_MS) {
            s->long_sent[b] = true;
            if (n < max) out[n++] = (btn_event_t){ b, BTN_EV_LONG };
        }
    }
    s->last_raw = raw;
    return n;
}

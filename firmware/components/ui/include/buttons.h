/* Button decoder: debounce + short/long press from periodic raw samples (active-low bits).
 * Pure C, host-tested. Sample every 10-20 ms from the IO-expander input register. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BTN_COUNT        6       /* UP, DOWN, SELECT, BACK, RECORD, CAMERA = expander bits 0..5 */
#define BTN_DEBOUNCE_MS  30
#define BTN_LONG_MS      800

typedef enum { BTN_EV_NONE = 0, BTN_EV_PRESS, BTN_EV_RELEASE, BTN_EV_SHORT, BTN_EV_LONG } btn_ev_t;

typedef struct {
    uint8_t  stable;            /* debounced state, bit = pressed */
    uint8_t  last_raw;
    uint32_t raw_since[BTN_COUNT];
    uint32_t pressed_at[BTN_COUNT];
    bool     long_sent[BTN_COUNT];
} btn_state_t;

typedef struct { uint8_t button; btn_ev_t ev; } btn_event_t;

void btn_init(btn_state_t *s);
/* raw_active_low: expander port0 bits (0 = pressed). Returns number of events written. */
int  btn_sample(btn_state_t *s, uint8_t raw_active_low, uint32_t now_ms, btn_event_t *out, int max);

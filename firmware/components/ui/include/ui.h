/* UI: LVGL on SPI TFT, button input via IO expander. Phase 6. */
#pragma once
#include "esp_err.h"
typedef enum { UI_EV_UP, UI_EV_DOWN, UI_EV_SELECT, UI_EV_BACK, UI_EV_REC_DOWN, UI_EV_REC_UP,
               UI_EV_CAMERA, UI_EV_POWER } ui_event_t;
esp_err_t ui_init(void);
void ui_show_status(int rssi, int snr, int batt_pct, const char *peer_name);
void ui_show_progress(const char *title, int pct, int eta_s, int retries);
void ui_toast(const char *msg);

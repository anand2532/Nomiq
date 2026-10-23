/* LCD renderer for ui_view_t (LVGL). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ui_view.h"

esp_err_t display_init(void);
void      display_render(const ui_view_t *v);
void      display_power(bool on);
void      display_backlight(uint8_t pct);

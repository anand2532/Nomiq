/* ui_view — turns the UI model into plain text rows for any renderer (LVGL, serial console, tests). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ui_model.h"

#define UI_VIEW_ROWS 6
#define UI_VIEW_COLS 40

typedef struct {
    char status[UI_VIEW_COLS + 1];          /* top bar: battery, signal, unread */
    char title[UI_VIEW_COLS + 1];
    char rows[UI_VIEW_ROWS][UI_VIEW_COLS + 1];
    uint8_t n_rows;
    int8_t  highlight;                      /* row index or -1 */
    char body[UI_TEXT_MAX + 1];             /* multi-line text area */
    int8_t  progress;                       /* 0..100, -1 = hidden */
    char footer[UI_VIEW_COLS + 1];          /* toast / hint */
    bool show_image;                        /* photo preview / viewer: renderer draws the image */
} ui_view_t;

void ui_view_build(const ui_model_t *m, ui_view_t *v);

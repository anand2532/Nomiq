/* display_lvgl.c — ILI9341 240x320 over the shared SPI bus, LVGL 9 via esp_lvgl_port.
 * Draws a ui_view_t (status bar, title, list, body, progress, footer). */
#include "display.h"

#include <string.h>
#include "board.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "display";
#define HRES 240
#define VRES 320

static esp_lcd_panel_handle_t s_panel;
static lv_obj_t *s_status, *s_title, *s_rows[UI_VIEW_ROWS], *s_body, *s_bar, *s_footer;

static void backlight_init(void)
{
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                              .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    ledc_channel_config_t c = { .gpio_num = PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
                                .timer_sel = LEDC_TIMER_0, .duty = 0, .hpoint = 0 };
    ledc_channel_config(&c);
}

void display_backlight(uint8_t pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)pct * 1023u / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static lv_obj_t *label(lv_obj_t *parent, int y, int h, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, 4, y);
    lv_obj_set_size(l, HRES - 8, h);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, "");
    return l;
}

esp_err_t display_init(void)
{
    iox_set(IOX_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    iox_set(IOX_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t ioc = {
        .dc_gpio_num = PIN_LCD_DC, .cs_gpio_num = PIN_LCD_CS, .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_SPI_HOST, &ioc, &io), TAG, "io");
    esp_lcd_panel_dev_config_t pc = { .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, .bits_per_pixel = 16 };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &pc, &s_panel), TAG, "panel");
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_disp_on_off(s_panel, true);

    lvgl_port_cfg_t lc = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lc), TAG, "lvgl");
    lvgl_port_display_cfg_t dc = {
        .io_handle = io, .panel_handle = s_panel, .buffer_size = HRES * 40, .double_buffer = true,
        .hres = HRES, .vres = VRES, .monochrome = false,
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    if (!lvgl_port_add_disp(&dc)) return ESP_FAIL;

    lvgl_port_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    s_status = label(scr, 2, 16, &lv_font_montserrat_12);
    s_title = label(scr, 22, 22, &lv_font_montserrat_16);
    for (int i = 0; i < UI_VIEW_ROWS; i++) {
        s_rows[i] = label(scr, 50 + i * 26, 24, &lv_font_montserrat_16);
        lv_obj_set_style_bg_color(s_rows[i], lv_palette_main(LV_PALETTE_BLUE), 0);
    }
    s_body = label(scr, 50, 150, &lv_font_montserrat_16);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    s_bar = lv_bar_create(scr);
    lv_obj_set_pos(s_bar, 10, 210);
    lv_obj_set_size(s_bar, HRES - 20, 16);
    s_footer = label(scr, VRES - 40, 36, &lv_font_montserrat_12);
    lv_label_set_long_mode(s_footer, LV_LABEL_LONG_WRAP);
    lvgl_port_unlock();

    backlight_init();
    display_backlight(80);
    return ESP_OK;
}

void display_render(const ui_view_t *v)
{
    if (!lvgl_port_lock(100)) return;
    lv_label_set_text(s_status, v->status);
    lv_label_set_text(s_title, v->title);
    int body_y = 50 + v->n_rows * 26 + (v->n_rows ? 6 : 0);
    for (int i = 0; i < UI_VIEW_ROWS; i++) {
        if (i < v->n_rows) {
            lv_label_set_text(s_rows[i], v->rows[i]);
            lv_obj_remove_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(s_rows[i], i == v->highlight ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        } else {
            lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_set_y(s_body, body_y);
    lv_label_set_text(s_body, v->show_image && !v->body[0] ? "[photo]" : v->body);
    if (v->progress >= 0) {
        lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_bar, v->progress, LV_ANIM_ON);
    } else {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_footer, v->footer);
    lvgl_port_unlock();
}

void display_power(bool on)
{
    esp_lcd_panel_disp_on_off(s_panel, on);
    display_backlight(on ? 80 : 0);
}

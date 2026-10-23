/* power_esp.c — MAX17048 over the shared I2C bus, rails via the IO expander. */
#include "power.h"

#include "board.h"
#include "board_pins.h"
#include "esp_log.h"
#include "fuel_gauge.h"

static const char *TAG = "power";
static i2c_master_dev_handle_t s_fg;
static fg_bus_t s_bus;
static power_state_t s_st;
static bool s_fg_ok;

static int rd16(uint8_t reg, uint16_t *v, void *ctx)
{
    (void)ctx;
    uint8_t b[2];
    if (i2c_master_transmit_receive(s_fg, &reg, 1, b, 2, 50) != ESP_OK) return -1;
    *v = (uint16_t)((b[0] << 8) | b[1]);
    return 0;
}

static int wr16(uint8_t reg, uint16_t v, void *ctx)
{
    (void)ctx;
    uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
    return i2c_master_transmit(s_fg, b, 3, 50) == ESP_OK ? 0 : -1;
}

esp_err_t power_init(void)
{
    power_policy_init(&s_st);
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = FG_ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(board_i2c_bus(), &dc, &s_fg) != ESP_OK) return ESP_FAIL;
    s_bus = (fg_bus_t){ rd16, wr16, NULL };
    int rc = fg_init(&s_bus, PWR_LOW_PCT);
    s_fg_ok = rc == 0;
    if (!s_fg_ok) ESP_LOGW(TAG, "fuel gauge not found (%d) - battery level unavailable", rc);
    power_update();
    return ESP_OK;
}

power_verdict_t power_rail(power_rail_t r, bool on)
{
    power_verdict_t v = power_policy_request(&s_st, r, on);
    if (v != PWR_OK) { ESP_LOGW(TAG, "rail %d request denied (%d)", r, v); return v; }
    switch (r) {
    case RAIL_CAMERA: iox_set(IOX_CAM_PWR_EN, on); break;
    case RAIL_STT:    iox_set(IOX_STT_EN, on); break;
    case RAIL_AMP:    iox_set(IOX_AMP_SD, on); break;
    default: break;   /* LCD backlight is PWM on GPIO45, handled by the display driver */
    }
    return PWR_OK;
}

battery_level_t power_update(void)
{
    uint8_t pct = s_st.soc_pct; uint16_t mv = s_st.vbat_mv;
    if (s_fg_ok) { fg_soc(&s_bus, &pct, NULL); fg_voltage_mv(&s_bus, &mv); }
    uint16_t in = 0xFFFF;
    iox_read(&in);
    bool charging = !(in & (1u << IOX_CHG_STAT));       /* CHG is active-low open drain */
    bool cam = s_st.on[RAIL_CAMERA], stt = s_st.on[RAIL_STT];
    battery_level_t lvl = power_policy_battery(&s_st, pct, mv, charging);
    if (cam && !s_st.on[RAIL_CAMERA]) iox_set(IOX_CAM_PWR_EN, false);   /* policy shed loads */
    if (stt && !s_st.on[RAIL_STT]) iox_set(IOX_STT_EN, false);
    return lvl;
}

int  power_battery_pct(void) { return s_st.soc_pct; }
int  power_battery_mv(void) { return s_st.vbat_mv; }
bool power_is_charging(void) { return s_st.charging; }

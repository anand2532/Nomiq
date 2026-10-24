/*
 * messenger.c — the complete Nomiq device application: glues app_core (messaging), ui_model
 * (behaviour), display, buttons, audio, camera, STT co-processor, power and the radio.
 *
 *   radio IRQ task --frames--> [app task, 20 ms tick] <--buttons (IO expander)
 *                                 |  app_core events -> ui events
 *                                 |  ui actions      -> subsystems (audio/camera/stt via worker task)
 *                                 v
 *                              display_render(ui_view)
 */
#include "messenger.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app_core.h"
#include "audio.h"
#include "board.h"
#include "board_pins.h"
#include "buttons.h"
#include "camera.h"
#include "display.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "image_fit.h"
#include "nvs.h"
#include "power.h"
#include "radio.h"
#include "sdkconfig.h"
#include "stt_link.h"
#include "ui_model.h"
#include "ui_view.h"

static const char *TAG = "messenger";

#define ROOT        "/sd/nomiq"
#define REC_PCM     ROOT "/pcm/rec.pcm"
#define REC_C2      ROOT "/pcm/rec.c2"
#define PHOTO_JPG   ROOT "/pcm/photo.jpg"

typedef struct { uint8_t len; int16_t rssi; int8_t snr; uint8_t buf[255]; } rx_item_t;
typedef enum { JOB_STT, JOB_CAPTURE, JOB_SEND_VOICE } job_t;

static QueueHandle_t s_rxq, s_uiq, s_jobq;
static app_t *s_app;
static ui_model_t s_m;
static btn_state_t s_btn;
static bool s_dirty = true;
static uint32_t s_self;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---------------- platform hooks for app_core ---------------- */

static uint32_t radio_send_hook(const uint8_t *f, size_t n, void *ctx)
{
    (void)ctx;
    uint32_t toa = radio_time_on_air_ms(n);
    if (radio_send(f, n, 0) != ESP_OK) ESP_LOGW(TAG, "radio_send failed");
    return toa;
}

static int space_hook(const char *root, uint64_t *fr, uint64_t *tot)
{
    (void)root;
    return esp_vfs_fat_info("/sd", tot, fr) == ESP_OK ? 0 : -1;
}

static int secret_save(const char *name, const void *d, size_t n, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open("nomiq_sec", NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t e = nvs_set_blob(h, name, d, n);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}

static int secret_load(const char *name, void *d, size_t n, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open("nomiq_sec", NVS_READONLY, &h) != ESP_OK) return -1;
    size_t len = n;
    esp_err_t e = nvs_get_blob(h, name, d, &len);
    nvs_close(h);
    return e == ESP_OK && len == n ? 0 : -1;
}

static void post_ui(ui_event_t e) { e.now_ms = now_ms(); xQueueSend(s_uiq, &e, 0); }

static void app_event_hook(const app_event_t *e, void *ctx)
{
    (void)ctx;
    ui_event_t u = { .seq = e->seq, .peer = e->peer, .arg = e->arg };
    switch (e->type) {
    case APP_EV_PROGRESS:    u.type = EV_PROGRESS; break;
    case APP_EV_DELIVERED:   u.type = EV_DELIVERED; break;
    case APP_EV_RETRY_LATER: u.type = EV_RETRY_LATER; break;
    case APP_EV_REJECTED:    u.type = EV_REJECTED; break;
    case APP_EV_RECEIVED:    u.type = EV_RECEIVED; u.arg = e->kind; break;
    case APP_EV_PAIR_CODE:   u.type = EV_PAIR_CODE; break;
    case APP_EV_PAIRED:      u.type = EV_PAIRED; break;
    default: return;
    }
    post_ui(u);
}

static void on_radio_rx(const uint8_t *frame, size_t len, radio_link_t link, void *ctx)
{
    (void)ctx;
    rx_item_t it = { .len = (uint8_t)len, .rssi = link.rssi_dbm, .snr = link.snr_db };
    memcpy(it.buf, frame, len);
    xQueueSend(s_rxq, &it, 0);
}

/* ---------------- worker: slow jobs off the UI/app task ---------------- */

static char s_stt_text[UI_TEXT_MAX + 1];

static void worker_task(void *arg)
{
    (void)arg;
    job_t j;
    for (;;) {
        if (xQueueReceive(s_jobq, &j, portMAX_DELAY) != pdTRUE) continue;
        if (j == JOB_STT) {
            stt_result_t r = {0};
            esp_err_t e = stt_power_on(15000);
            if (e == ESP_OK) e = stt_transcribe_file(REC_PCM, "en", &r);
            stt_power_off();
            snprintf(s_stt_text, sizeof s_stt_text, "%.*s", UI_TEXT_MAX, r.text);
            ui_event_t u = { .type = e == ESP_OK ? EV_STT_FINAL : EV_STT_ERROR, .text = s_stt_text, .confidence = r.confidence };
            post_ui(u);
        } else if (j == JOB_CAPTURE) {
            img_fit_cfg_t c; img_fit_default_cfg(&c);
            img_fit_result_t r;
            esp_err_t e = camera_capture(&c, PHOTO_JPG, &r);
            uint32_t eta = xfer_eta_ms(r.bytes, radio_time_on_air_ms(255), radio_time_on_air_ms(42), 10,
                                       radio_duty_budget_ms() == UINT32_MAX ? 0 : 25);
            ui_event_t u = { .type = e == ESP_OK ? EV_PHOTO_READY : EV_PHOTO_FAILED, .arg = r.bytes, .arg2 = eta };
            post_ui(u);
        }
    }
}

/* ---------------- action dispatch ---------------- */

static void load_contacts(void)
{
    uint32_t ids[UI_MAX_CONTACTS]; char names[UI_MAX_CONTACTS][16];
    int n = app_peers(s_app, ids, names, UI_MAX_CONTACTS);
    ui_contact_t c[UI_MAX_CONTACTS];
    for (int i = 0; i < n; i++) { c[i].id = ids[i]; snprintf(c[i].name, sizeof c[i].name, "%s", names[i]); }
    ui_model_set_contacts(&s_m, c, (uint8_t)n);
}

static void open_message(uint32_t seq)
{
    const msg_rec_t *r = msg_store_get(app_store(s_app), seq);
    char p[160];
    s_m.text[0] = 0;
    if (!r || msg_store_path(app_store(s_app), r, p, sizeof p) != 0) return;
    if (r->kind == MSG_VOICE) { audio_play(p); snprintf(s_m.text, sizeof s_m.text, "Playing voice (%u B)", (unsigned)r->size); }
    else if (r->kind == MSG_TEXT) {
        FILE *f = fopen(p, "rb");
        if (f) { size_t n = fread(s_m.text, 1, UI_TEXT_MAX, f); s_m.text[n] = 0; fclose(f); }
    } else snprintf(s_m.text, sizeof s_m.text, "Photo, %u bytes", (unsigned)r->size);
    if (r->state == MS_RECEIVED) msg_store_set_state(app_store(s_app), seq, MS_READ);
}

static void do_action(const ui_action_t *a)
{
    uint32_t seq = 0, t = now_ms();
    job_t j;
    switch (a->type) {
    case ACT_DISPLAY_ON:  display_power(true); break;
    case ACT_DISPLAY_OFF: display_power(false); break;
    case ACT_REC_START:   audio_stop(); audio_record_start(REC_PCM, s_m.rec_max_ms); break;
    case ACT_REC_STOP:    audio_record_stop(NULL); break;
    case ACT_PLAY_RECORDING: audio_play(REC_PCM); break;
    case ACT_STT_START:   j = JOB_STT; xQueueSend(s_jobq, &j, 0); break;
    case ACT_STT_CANCEL:  break;   /* result is ignored by the model if it is no longer waiting */
    case ACT_CAMERA_ON:   power_rail(RAIL_CAMERA, true); break;
    case ACT_CAMERA_OFF:  camera_power(false); power_rail(RAIL_CAMERA, false); break;
    case ACT_CAPTURE:     j = JOB_CAPTURE; xQueueSend(s_jobq, &j, 0); break;
    case ACT_SEND_TEXT:
        if (app_send_text(s_app, a->peer, a->text, t, &seq) == 0) ui_model_sending_started(&s_m, seq);
        break;
    case ACT_SEND_VOICE:
        if (audio_encode(REC_PCM, REC_C2, VC_MODE_1200) == ESP_OK &&
            app_send_file(s_app, a->peer, MSG_VOICE, REC_C2, t, &seq) == 0) ui_model_sending_started(&s_m, seq);
        break;
    case ACT_SEND_PHOTO:
        if (app_send_file(s_app, a->peer, MSG_PHOTO, PHOTO_JPG, t, &seq) == 0) ui_model_sending_started(&s_m, seq);
        break;
    case ACT_CANCEL_SEND:  app_cancel(s_app, a->seq, t); break;
    case ACT_OPEN_MESSAGE: open_message(a->seq); break;
    case ACT_PAIR_BEGIN:   app_pair_begin(s_app, t); break;
    case ACT_PAIR_CONFIRM: {
        char name[16]; snprintf(name, sizeof name, "Dev %04x", (unsigned)(a->peer & 0xFFFF));
        app_pair_confirm(s_app, a->peer, name);
        load_contacts();
        break;
    }
    case ACT_POWER_OFF:
        ESP_LOGI(TAG, "power off");
        display_power(false);
        radio_sleep();
        esp_deep_sleep_start();
        break;
    default: break;
    }
}

static void handle_ui(const ui_event_t *e)
{
    ui_action_t acts[8];
    int n = ui_model_handle(&s_m, e, acts, 8);
    for (int i = 0; i < n; i++) do_action(&acts[i]);
    if (e->type != EV_TICK || n) s_dirty = true;
}

static const ui_event_type_t BTN_SHORT[BTN_COUNT] = { EV_UP, EV_DOWN, EV_SELECT, EV_BACK, 0, EV_CAMERA };
static const ui_event_type_t BTN_LONG[BTN_COUNT]  = { EV_UP, EV_DOWN, EV_SELECT_LONG, EV_BACK_LONG, 0, 0 };

static void poll_buttons(uint32_t t)
{
    uint16_t in = 0xFFFF;
    if (iox_read(&in) != ESP_OK) return;
    btn_event_t ev[12];
    int n = btn_sample(&s_btn, (uint8_t)in, t, ev, 12);
    for (int i = 0; i < n; i++) {
        ui_event_t u = { .now_ms = t };
        uint8_t b = ev[i].button;
        if (b == IOX_BTN_RECORD) {
            if (ev[i].ev == BTN_EV_PRESS) u.type = EV_REC_DOWN;
            else if (ev[i].ev == BTN_EV_RELEASE) u.type = EV_REC_UP;
        } else if (ev[i].ev == BTN_EV_SHORT) u.type = BTN_SHORT[b];
        else if (ev[i].ev == BTN_EV_LONG) u.type = BTN_LONG[b];
        if (u.type) handle_ui(&u);
    }
}

/* ---------------- main loop ---------------- */

static void app_task(void *arg)
{
    (void)arg;
    rx_item_t it;
    ui_event_t ue;
    ui_view_t view;
    uint32_t next_power = 0, next_tick = 0;
    for (;;) {
        uint32_t t = now_ms();
        while (xQueueReceive(s_rxq, &it, 0) == pdTRUE) {
            s_m.rssi = it.rssi; s_m.snr = it.snr;
            app_on_frame(s_app, it.buf, it.len, it.snr, it.rssi, t);
        }
        app_poll(s_app, t);
        while (xQueueReceive(s_uiq, &ue, 0) == pdTRUE) handle_ui(&ue);
        poll_buttons(t);
        if ((int32_t)(t - next_tick) >= 0) {
            ui_event_t tick = { .type = EV_TICK, .now_ms = t };
            handle_ui(&tick);
            next_tick = t + 250;
        }
        if ((int32_t)(t - next_power) >= 0) {
            battery_level_t lvl = power_update();
            s_m.battery_pct = (uint8_t)power_battery_pct();
            s_m.charging = power_is_charging();
            if (lvl == BAT_SHUTDOWN) { ui_action_t off = { .type = ACT_POWER_OFF }; do_action(&off); }
            s_dirty = true;
            next_power = t + 10000;
        }
        if (s_dirty && s_m.display_on) {
            ui_view_build(&s_m, &view);
            display_render(&view);
            s_dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t mount_sd(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SPI_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = BOARD_SPI_HOST;
    esp_vfs_fat_mount_config_t mc = { .format_if_mount_failed = false, .max_files = 8, .allocation_unit_size = 16 * 1024 };
    sdmmc_card_t *card;
    esp_err_t e = esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mc, &card);
    if (e == ESP_OK) { mkdir(ROOT, 0775); mkdir(ROOT "/pcm", 0775); }
    return e;
}

esp_err_t messenger_start(void)
{
    s_self = board_device_id();
    ESP_ERROR_CHECK(power_init());
    if (mount_sd() != ESP_OK) ESP_LOGE(TAG, "microSD not mounted - messages cannot be stored");
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(audio_init());

    s_rxq = xQueueCreate(16, sizeof(rx_item_t));
    s_uiq = xQueueCreate(16, sizeof(ui_event_t));
    s_jobq = xQueueCreate(4, sizeof(job_t));

    app_cfg_t cfg = {
        .self_id = s_self, .root = ROOT, .space = space_hook, .radio_send = radio_send_hook,
        .on_event = app_event_hook, .secret_save = secret_save, .secret_load = secret_load,
    };
    s_app = app_create(&cfg, now_ms());
    if (!s_app) { ESP_LOGE(TAG, "app_create failed"); return ESP_FAIL; }

    ui_model_init(&s_m, 30000);
    btn_init(&s_btn);
    load_contacts();

    radio_cfg_t rc = {
        .freq_hz = CONFIG_NOMIQ_LORA_FREQ_HZ, .sf = CONFIG_NOMIQ_LORA_SF, .bw_khz = LORA_BW_KHZ, .cr = 1,
        .tx_dbm = CONFIG_NOMIQ_TX_DBM,
#if CONFIG_NOMIQ_REG_PROFILE_SRD2021
        .reg_profile = REG_PROFILE_IN865_SRD2021,
#else
        .reg_profile = REG_PROFILE_IN865_2008,
#endif
    };
    ESP_ERROR_CHECK(radio_init(&rc, on_radio_rx, NULL));
    xTaskCreatePinnedToCore(worker_task, "worker", 8192, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(app_task, "app", 12288, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "Nomiq messenger running, device id %08lx", (unsigned long)s_self);
    return ESP_OK;
}

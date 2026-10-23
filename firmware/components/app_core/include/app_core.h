/*
 * app_core — the Nomiq messenger service: pairing, encrypted link, message queue,
 * reliable transfers and local history. Glues nomiq_proto (frames, transfer engine,
 * crypto) to msg_store. Platform-independent (radio + clock injected) so two complete
 * devices can be simulated in one host test.
 *
 * UI / tasks call: app_send_*, app_cancel, app_pair_*; the radio task feeds app_on_frame;
 * a 10–50 ms tick calls app_poll. Events are reported through cfg.on_event.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "msg_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MAX_PEERS     16
#define APP_MAX_ACTIVE    4
#define APP_PAIR_WINDOW_MS 60000u
#define APP_RETRY_MS      60000u    /* failed transfers stay queued and retry */

typedef enum {
    APP_EV_PROGRESS = 1,  /* arg = percent */
    APP_EV_DELIVERED,
    APP_EV_RETRY_LATER,   /* transfer failed; stays queued (REQ-STO-003) */
    APP_EV_REJECTED,      /* arg = nomiq_reject_t */
    APP_EV_RECEIVED,      /* seq = new local record */
    APP_EV_PAIR_CODE,     /* peer + arg = 6-digit code to compare */
    APP_EV_PAIRED,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    uint32_t seq;
    uint32_t peer;
    uint8_t  kind;
    uint32_t arg;
} app_event_t;

typedef struct {
    uint32_t self_id;
    const char *root;                         /* storage root, e.g. "/sd/nomiq" */
    msg_store_space_fn space;
    uint32_t (*radio_send)(const uint8_t *frame, size_t len, void *ctx);   /* returns ToA ms */
    bool (*radio_busy)(uint32_t now_ms, void *ctx);                         /* may be NULL */
    void (*on_event)(const app_event_t *e, void *ctx);                      /* may be NULL */
    void *ctx;
    bool open_mode;                           /* accept/send plaintext with unpaired peers (demo) */
    /* Secrets (identity key, link keys, frame counter). On the device: encrypted NVS.
     * If NULL they fall back to store blobs (host tests only — SD card is not secret). */
    int (*secret_save)(const char *name, const void *data, size_t len, void *ctx);
    int (*secret_load)(const char *name, void *data, size_t len, void *ctx);
} app_cfg_t;

typedef struct {
    uint32_t rx_frames, rx_auth_fail, rx_replay, rx_unpaired, rx_crc;
    uint32_t tx_frames;
} app_stats_t;

typedef struct app app_t;

app_t *app_create(const app_cfg_t *cfg, uint32_t now_ms);
void   app_destroy(app_t *a);                 /* simulates power-off: RAM state is lost */
msg_store_t *app_store(app_t *a);
const app_stats_t *app_stats(app_t *a);

int  app_send_text(app_t *a, uint32_t peer, const char *text, uint32_t now_ms, uint32_t *seq);
int  app_send_file(app_t *a, uint32_t peer, uint8_t kind, const char *src_path, uint32_t now_ms, uint32_t *seq);
int  app_cancel(app_t *a, uint32_t seq, uint32_t now_ms);

void app_on_frame(app_t *a, const uint8_t *frame, size_t len, int8_t snr, int16_t rssi, uint32_t now_ms);
void app_poll(app_t *a, uint32_t now_ms);

/* Pairing: both users start pairing, compare the code shown on both screens, confirm. */
int  app_pair_begin(app_t *a, uint32_t now_ms);
int  app_pair_confirm(app_t *a, uint32_t peer, const char *name);
bool app_is_paired(app_t *a, uint32_t peer);

#ifdef __cplusplus
}
#endif

/*
 * ui_model — the Nomiq user-interface state machine (docs/specs/ui-and-storage.md §2).
 *
 * Pure logic, no graphics: it consumes button / system events and produces
 *   (1) a view state the LVGL renderer draws, and
 *   (2) actions for the controller (record, send, capture, pair, play, display off ...).
 * Host-tested by driving complete user flows (Modes 1-4).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_TEXT_MAX      220
#define UI_MAX_CONTACTS  16
#define UI_MAX_INBOX     8
#define UI_DISPLAY_TIMEOUT_MS 30000u

typedef enum {
    SCR_HOME = 0, SCR_CONTACTS, SCR_COMPOSE, SCR_VOICE_REC, SCR_VOICE_PREVIEW, SCR_STT_LISTEN, SCR_STT_WAIT,
    SCR_TEXT_PREVIEW, SCR_EDITOR, SCR_CAMERA, SCR_PHOTO_PREVIEW, SCR_SENDING, SCR_INBOX, SCR_VIEWER,
    SCR_SETTINGS, SCR_PAIRING, SCR__COUNT
} ui_screen_t;

typedef enum {
    EV_UP = 1, EV_DOWN, EV_SELECT, EV_SELECT_LONG, EV_BACK, EV_BACK_LONG, EV_REC_DOWN, EV_REC_UP, EV_CAMERA,
    EV_POWER,
    /* system */
    EV_TICK, EV_STT_FINAL, EV_STT_ERROR, EV_PHOTO_READY, EV_PHOTO_FAILED, EV_PROGRESS, EV_DELIVERED,
    EV_RETRY_LATER, EV_REJECTED, EV_RECEIVED, EV_PAIR_CODE, EV_PAIRED, EV_RECORD_DONE,
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    uint32_t now_ms;
    uint32_t seq, peer, arg, arg2;     /* meaning depends on type (see ui_model.c) */
    const char *text;                  /* EV_STT_FINAL */
    float confidence;
} ui_event_t;

typedef enum {
    ACT_NONE = 0, ACT_DISPLAY_ON, ACT_DISPLAY_OFF, ACT_REC_START, ACT_REC_STOP, ACT_PLAY_RECORDING,
    ACT_STT_START, ACT_STT_CANCEL, ACT_CAMERA_ON, ACT_CAMERA_OFF, ACT_CAPTURE, ACT_SEND_TEXT, ACT_SEND_VOICE,
    ACT_SEND_PHOTO, ACT_CANCEL_SEND, ACT_OPEN_MESSAGE, ACT_PAIR_BEGIN, ACT_PAIR_CONFIRM, ACT_POWER_OFF,
} ui_action_type_t;

typedef struct {
    ui_action_type_t type;
    uint32_t peer;
    uint32_t seq;
    const char *text;                  /* ACT_SEND_TEXT: points into the model's buffer */
} ui_action_t;

typedef struct { uint32_t id; char name[16]; } ui_contact_t;
typedef struct { uint32_t seq, peer; uint8_t kind; bool unread; } ui_inbox_item_t;

typedef struct {
    ui_screen_t screen;
    bool display_on;
    uint8_t sel;                       /* highlighted row on list screens */
    /* status bar */
    uint8_t battery_pct;
    bool charging;
    int16_t rssi; int8_t snr;
    /* data */
    ui_contact_t contacts[UI_MAX_CONTACTS];
    uint8_t n_contacts;
    uint32_t recipient;
    ui_inbox_item_t inbox[UI_MAX_INBOX];
    uint8_t n_inbox;
    uint8_t unread;
    /* compose */
    char text[UI_TEXT_MAX + 1];
    uint16_t text_len;
    float stt_conf;
    bool low_confidence;
    uint32_t rec_started_ms, rec_ms, rec_max_ms;
    uint32_t photo_bytes, photo_eta_ms;
    /* sending */
    uint32_t sending_seq;
    uint8_t progress;
    char status[48];                   /* toast / status line */
    /* pairing */
    uint32_t pair_peer, pair_code;
    /* editor */
    uint8_t ed_char;                   /* index into the editor alphabet */
    uint32_t last_input_ms;
} ui_model_t;

void ui_model_init(ui_model_t *m, uint32_t rec_max_ms);
void ui_model_set_contacts(ui_model_t *m, const ui_contact_t *c, uint8_t n);
/* Process one event; writes up to `max` actions, returns how many. */
int  ui_model_handle(ui_model_t *m, const ui_event_t *e, ui_action_t *out, int max);
const char *ui_screen_name(ui_screen_t s);
/* Controller reports the store seq of the message it just queued (for progress/delivery). */
void ui_model_sending_started(ui_model_t *m, uint32_t seq);
extern const char UI_EDITOR_ALPHABET[];

#ifdef __cplusplus
}
#endif

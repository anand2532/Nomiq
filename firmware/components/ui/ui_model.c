/* ui_model.c — see include/ui_model.h */
#include "ui_model.h"

#include <stdio.h>
#include <string.h>

const char UI_EDITOR_ALPHABET[] = " abcdefghijklmnopqrstuvwxyz0123456789.,?!'-:/@";
#define ALPHA_N ((uint8_t)(sizeof UI_EDITOR_ALPHABET - 1))

static const uint8_t MENU_LEN[SCR__COUNT] = {
    [SCR_HOME] = 4, [SCR_COMPOSE] = 4, [SCR_VOICE_PREVIEW] = 3, [SCR_TEXT_PREVIEW] = 4,
    [SCR_PHOTO_PREVIEW] = 2, [SCR_SETTINGS] = 4,
};

static const char *NAMES[SCR__COUNT] = {
    "HOME", "CONTACTS", "COMPOSE", "VOICE_REC", "VOICE_PREVIEW", "STT_LISTEN", "STT_WAIT", "TEXT_PREVIEW",
    "EDITOR", "CAMERA", "PHOTO_PREVIEW", "SENDING", "INBOX", "VIEWER", "SETTINGS", "PAIRING",
};
const char *ui_screen_name(ui_screen_t s) { return s < SCR__COUNT ? NAMES[s] : "?"; }

typedef struct { ui_action_t *out; int max, n; } acts_t;
static void act(acts_t *a, ui_action_type_t t, uint32_t peer, uint32_t seq, const char *text)
{
    if (a->n < a->max) a->out[a->n++] = (ui_action_t){ t, peer, seq, text };
}

static void go(ui_model_t *m, ui_screen_t s) { m->screen = s; m->sel = 0; }
static void status(ui_model_t *m, const char *s) { snprintf(m->status, sizeof m->status, "%s", s); }

void ui_model_init(ui_model_t *m, uint32_t rec_max_ms)
{
    memset(m, 0, sizeof *m);
    m->display_on = true;
    m->rec_max_ms = rec_max_ms ? rec_max_ms : 30000;
    m->rssi = -127;
}

void ui_model_set_contacts(ui_model_t *m, const ui_contact_t *c, uint8_t n)
{
    if (n > UI_MAX_CONTACTS) n = UI_MAX_CONTACTS;
    memcpy(m->contacts, c, sizeof(ui_contact_t) * n);
    m->n_contacts = n;
}

static uint8_t list_len(const ui_model_t *m)
{
    switch (m->screen) {
    case SCR_CONTACTS: return m->n_contacts;
    case SCR_INBOX:    return m->n_inbox;
    default:           return MENU_LEN[m->screen];
    }
}

static void stop_recording_to_preview(ui_model_t *m, acts_t *a, uint32_t now)
{
    act(a, ACT_REC_STOP, 0, 0, NULL);
    m->rec_ms = now - m->rec_started_ms;
    go(m, SCR_VOICE_PREVIEW);
}

static void inbox_push(ui_model_t *m, uint32_t seq, uint32_t peer, uint8_t kind)
{
    if (m->n_inbox < UI_MAX_INBOX) m->n_inbox++;
    memmove(&m->inbox[1], &m->inbox[0], sizeof(ui_inbox_item_t) * (m->n_inbox - 1));
    m->inbox[0] = (ui_inbox_item_t){ seq, peer, kind, true };
    m->unread++;
}

static const char *contact_name(const ui_model_t *m, uint32_t id, char *buf, size_t n)
{
    for (uint8_t i = 0; i < m->n_contacts; i++) if (m->contacts[i].id == id) return m->contacts[i].name;
    snprintf(buf, n, "%08x", (unsigned)id);
    return buf;
}

/* system events are valid on any screen */
static bool handle_system(ui_model_t *m, const ui_event_t *e, acts_t *a)
{
    char nb[12], s[48];
    switch (e->type) {
    case EV_RECEIVED:
        inbox_push(m, e->seq, e->peer, (uint8_t)e->arg);
        snprintf(s, sizeof s, "New message from %s", contact_name(m, e->peer, nb, sizeof nb));
        status(m, s);
        if (!m->display_on) { m->display_on = true; act(a, ACT_DISPLAY_ON, 0, 0, NULL); }
        m->last_input_ms = e->now_ms;
        return true;
    case EV_PROGRESS:
        if (e->seq == m->sending_seq) m->progress = (uint8_t)e->arg;
        return true;
    case EV_DELIVERED:
        if (e->seq == m->sending_seq) {
            m->progress = 100;
            status(m, "Delivered");
            m->sending_seq = 0;
            if (m->screen == SCR_SENDING) go(m, SCR_HOME);
        }
        return true;
    case EV_RETRY_LATER:
        if (e->seq == m->sending_seq) {
            status(m, "No reply - will retry automatically");
            m->sending_seq = 0;
            if (m->screen == SCR_SENDING) go(m, SCR_HOME);
        }
        return true;
    case EV_REJECTED:
        if (e->seq == m->sending_seq) {
            status(m, e->arg == 1 ? "Rejected: recipient storage full" : "Rejected by recipient");
            m->sending_seq = 0;
            if (m->screen == SCR_SENDING) go(m, SCR_HOME);
        }
        return true;
    case EV_PAIR_CODE:
        if (m->screen == SCR_PAIRING) {
            m->pair_peer = e->peer;
            m->pair_code = e->arg;
            snprintf(s, sizeof s, "Code %06u - same on both? SELECT", (unsigned)e->arg);
            status(m, s);
        }
        return true;
    case EV_PAIRED:
        if (m->n_contacts < UI_MAX_CONTACTS) {
            ui_contact_t *c = &m->contacts[m->n_contacts++];
            c->id = e->peer;
            snprintf(c->name, sizeof c->name, "Dev %04x", (unsigned)(e->peer & 0xFFFF));
        }
        status(m, "Paired");
        if (m->screen == SCR_PAIRING) go(m, SCR_HOME);
        return true;
    case EV_TICK: {
        if (m->screen == SCR_VOICE_REC && e->now_ms - m->rec_started_ms >= m->rec_max_ms) {
            stop_recording_to_preview(m, a, e->now_ms);
            status(m, "Maximum length reached");
            m->last_input_ms = e->now_ms;   /* user must see the preview: restart the screen timeout */
        }
        bool busy = m->screen == SCR_VOICE_REC || m->screen == SCR_STT_WAIT || m->screen == SCR_CAMERA;
        if (m->display_on && !busy && e->now_ms - m->last_input_ms >= UI_DISPLAY_TIMEOUT_MS) {
            m->display_on = false;
            act(a, ACT_DISPLAY_OFF, 0, 0, NULL);
        }
        return true;
    }
    default:
        return false;
    }
}

int ui_model_handle(ui_model_t *m, const ui_event_t *e, ui_action_t *out, int max)
{
    acts_t a = { out, max, 0 };
    if (handle_system(m, e, &a)) return a.n;

    bool is_button = e->type <= EV_POWER;
    if (is_button) {
        m->last_input_ms = e->now_ms;
        if (e->type == EV_POWER) {
            if (e->arg) { act(&a, ACT_POWER_OFF, 0, 0, NULL); return a.n; }
            m->display_on = !m->display_on;
            act(&a, m->display_on ? ACT_DISPLAY_ON : ACT_DISPLAY_OFF, 0, 0, NULL);
            return a.n;
        }
        if (!m->display_on) {
            m->display_on = true;
            act(&a, ACT_DISPLAY_ON, 0, 0, NULL);
            /* push-to-talk works straight from a dark screen; other buttons only wake */
            if (e->type != EV_REC_DOWN && e->type != EV_REC_UP) return a.n;
        }
        if (e->type == EV_UP || e->type == EV_DOWN) {
            uint8_t n = list_len(m);
            if (m->screen == SCR_EDITOR) {
                m->ed_char = (uint8_t)((m->ed_char + (e->type == EV_UP ? ALPHA_N - 1 : 1)) % ALPHA_N);
            } else if (n) {
                m->sel = (uint8_t)((m->sel + (e->type == EV_UP ? n - 1 : 1)) % n);
            }
            return a.n;
        }
    }

    const uint32_t now = e->now_ms;
    switch (m->screen) {
    case SCR_HOME:
        if (e->type == EV_SELECT) {
            if (m->sel == 0) {
                if (!m->n_contacts) { status(m, "No paired devices - pair first"); go(m, SCR_PAIRING); act(&a, ACT_PAIR_BEGIN, 0, 0, NULL); }
                else go(m, SCR_CONTACTS);
            } else if (m->sel == 1) go(m, SCR_INBOX);
            else if (m->sel == 2) { go(m, SCR_PAIRING); m->pair_code = 0; act(&a, ACT_PAIR_BEGIN, 0, 0, NULL); status(m, "Pairing - start pairing on the other device too"); }
            else go(m, SCR_SETTINGS);
        } else if (e->type == EV_REC_DOWN || e->type == EV_CAMERA) {
            if (!m->recipient) { status(m, "Select a recipient first"); if (m->n_contacts) go(m, SCR_CONTACTS); }
            else if (e->type == EV_REC_DOWN) { act(&a, ACT_REC_START, 0, 0, NULL); m->rec_started_ms = now; go(m, SCR_VOICE_REC); }
            else { act(&a, ACT_CAMERA_ON, 0, 0, NULL); go(m, SCR_CAMERA); }
        }
        break;

    case SCR_CONTACTS:
        if (e->type == EV_SELECT && m->n_contacts) { m->recipient = m->contacts[m->sel].id; go(m, SCR_COMPOSE); }
        else if (e->type == EV_BACK) go(m, SCR_HOME);
        break;

    case SCR_COMPOSE:
        if (e->type == EV_REC_DOWN) { act(&a, ACT_REC_START, 0, 0, NULL); m->rec_started_ms = now; go(m, SCR_VOICE_REC); }
        else if (e->type == EV_CAMERA) { act(&a, ACT_CAMERA_ON, 0, 0, NULL); go(m, SCR_CAMERA); }
        else if (e->type == EV_SELECT) {
            switch (m->sel) {
            case 0: status(m, "Hold the REC button and speak"); break;
            case 1: go(m, SCR_STT_LISTEN); status(m, "Hold REC and speak"); break;
            case 2: act(&a, ACT_CAMERA_ON, 0, 0, NULL); go(m, SCR_CAMERA); break;
            default: m->text_len = 0; m->text[0] = 0; m->ed_char = 1; go(m, SCR_EDITOR); break;
            }
        } else if (e->type == EV_BACK) go(m, SCR_CONTACTS);
        break;

    case SCR_VOICE_REC:
        if (e->type == EV_REC_UP) stop_recording_to_preview(m, &a, now);
        break;

    case SCR_VOICE_PREVIEW:
        if (e->type == EV_SELECT) {
            if (m->sel == 0) { act(&a, ACT_SEND_VOICE, m->recipient, 0, NULL); m->progress = 0; go(m, SCR_SENDING); }
            else if (m->sel == 1) act(&a, ACT_PLAY_RECORDING, 0, 0, NULL);
            else go(m, SCR_COMPOSE);
        } else if (e->type == EV_BACK) go(m, SCR_COMPOSE);
        else if (e->type == EV_REC_DOWN) { act(&a, ACT_REC_START, 0, 0, NULL); m->rec_started_ms = now; go(m, SCR_VOICE_REC); }
        break;

    case SCR_STT_LISTEN:
        if (e->type == EV_REC_DOWN) { act(&a, ACT_REC_START, 0, 0, NULL); m->rec_started_ms = now; status(m, "Listening..."); }
        else if (e->type == EV_REC_UP) {
            act(&a, ACT_REC_STOP, 0, 0, NULL);
            m->rec_ms = now - m->rec_started_ms;
            act(&a, ACT_STT_START, 0, 0, NULL);
            status(m, "Transcribing (offline)...");
            go(m, SCR_STT_WAIT);
        } else if (e->type == EV_BACK) go(m, SCR_COMPOSE);
        break;

    case SCR_STT_WAIT:
        if (e->type == EV_STT_FINAL || e->type == EV_STT_ERROR) {
            const char *t = (e->type == EV_STT_FINAL && e->text) ? e->text : "";
            snprintf(m->text, sizeof m->text, "%s", t);
            m->text_len = (uint16_t)strlen(m->text);
            m->stt_conf = e->type == EV_STT_FINAL ? e->confidence : 0.0f;
            m->low_confidence = m->text_len == 0 || m->stt_conf < 0.5f;
            status(m, m->low_confidence ? "Didn't catch that clearly - edit, retry or send as voice" : "Check the text, then SEND");
            go(m, SCR_TEXT_PREVIEW);
        } else if (e->type == EV_BACK) { act(&a, ACT_STT_CANCEL, 0, 0, NULL); go(m, SCR_COMPOSE); }
        break;

    case SCR_TEXT_PREVIEW:
        if (e->type == EV_SELECT) {
            switch (m->sel) {
            case 0:
                if (!m->text_len) { status(m, "Nothing to send"); break; }
                act(&a, ACT_SEND_TEXT, m->recipient, 0, m->text); m->progress = 0; go(m, SCR_SENDING); break;
            case 1: m->ed_char = 1; go(m, SCR_EDITOR); break;
            case 2: go(m, SCR_STT_LISTEN); break;
            default: act(&a, ACT_SEND_VOICE, m->recipient, 0, NULL); m->progress = 0; go(m, SCR_SENDING); break;
            }
        } else if (e->type == EV_BACK) go(m, SCR_COMPOSE);
        break;

    case SCR_EDITOR:
        if (e->type == EV_SELECT) {
            if (m->text_len < UI_TEXT_MAX) { m->text[m->text_len++] = UI_EDITOR_ALPHABET[m->ed_char]; m->text[m->text_len] = 0; }
        } else if (e->type == EV_BACK) {
            if (m->text_len) m->text[--m->text_len] = 0;
            else go(m, SCR_TEXT_PREVIEW);
        } else if (e->type == EV_SELECT_LONG || e->type == EV_BACK_LONG) {
            m->low_confidence = false;
            go(m, SCR_TEXT_PREVIEW);
        }
        break;

    case SCR_CAMERA:
        if (e->type == EV_CAMERA || e->type == EV_SELECT) { act(&a, ACT_CAPTURE, 0, 0, NULL); status(m, "Capturing..."); }
        else if (e->type == EV_PHOTO_READY) {
            m->photo_bytes = e->arg; m->photo_eta_ms = e->arg2;
            char s[48]; snprintf(s, sizeof s, "%u kB, about %u s to send", (unsigned)((e->arg + 999) / 1000), (unsigned)((e->arg2 + 999) / 1000));
            status(m, s);
            go(m, SCR_PHOTO_PREVIEW);
        } else if (e->type == EV_PHOTO_FAILED) status(m, "Camera error - try again");
        else if (e->type == EV_BACK) { act(&a, ACT_CAMERA_OFF, 0, 0, NULL); go(m, SCR_COMPOSE); }
        break;

    case SCR_PHOTO_PREVIEW:
        if (e->type == EV_SELECT) {
            if (m->sel == 0) { act(&a, ACT_CAMERA_OFF, 0, 0, NULL); act(&a, ACT_SEND_PHOTO, m->recipient, 0, NULL); m->progress = 0; go(m, SCR_SENDING); }
            else go(m, SCR_CAMERA);
        } else if (e->type == EV_BACK || e->type == EV_CAMERA) go(m, SCR_CAMERA);
        break;

    case SCR_SENDING:
        if (e->type == EV_BACK) { status(m, "Sending in background"); go(m, SCR_HOME); }
        else if (e->type == EV_SELECT_LONG && m->sending_seq) {
            act(&a, ACT_CANCEL_SEND, 0, m->sending_seq, NULL);
            status(m, "Cancelled");
            m->sending_seq = 0;
            go(m, SCR_HOME);
        }
        break;

    case SCR_INBOX:
        if (e->type == EV_SELECT && m->n_inbox) {
            ui_inbox_item_t *it = &m->inbox[m->sel];
            if (it->unread && m->unread) { it->unread = false; m->unread--; }
            act(&a, ACT_OPEN_MESSAGE, it->peer, it->seq, NULL);
            go(m, SCR_VIEWER);
        } else if (e->type == EV_BACK) go(m, SCR_HOME);
        break;

    case SCR_VIEWER:
        if (e->type == EV_BACK) go(m, SCR_INBOX);
        break;

    case SCR_PAIRING:
        if (e->type == EV_SELECT && m->pair_code) act(&a, ACT_PAIR_CONFIRM, m->pair_peer, 0, NULL);
        else if (e->type == EV_BACK) go(m, SCR_HOME);
        break;

    case SCR_SETTINGS:
        if (e->type == EV_BACK) go(m, SCR_HOME);
        break;

    default:
        break;
    }
    return a.n;
}

void ui_model_sending_started(ui_model_t *m, uint32_t seq) { m->sending_seq = seq; m->progress = 0; }

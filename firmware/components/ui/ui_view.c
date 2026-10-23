#include "ui_view.h"

/* rows are fixed-width display lines: truncation is intended */
#pragma GCC diagnostic ignored "-Wformat-truncation"

#include <stdio.h>
#include <string.h>

static void row(ui_view_t *v, const char *fmt, const char *a)
{
    if (v->n_rows < UI_VIEW_ROWS) snprintf(v->rows[v->n_rows++], UI_VIEW_COLS + 1, fmt, a);
}

static const char *name_of(const ui_model_t *m, uint32_t id, char *buf)
{
    for (uint8_t i = 0; i < m->n_contacts; i++) if (m->contacts[i].id == id) return m->contacts[i].name;
    snprintf(buf, 12, "%08x", (unsigned)id);
    return buf;
}

static const char *kind_name(uint8_t k) { return k == 2 ? "Voice" : k == 3 ? "Photo" : "Text"; }

void ui_view_build(const ui_model_t *m, ui_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->highlight = -1;
    v->progress = -1;
    char nb[12];
    int bars = m->rssi > -90 ? 4 : m->rssi > -105 ? 3 : m->rssi > -115 ? 2 : m->rssi > -125 ? 1 : 0;
    snprintf(v->status, sizeof v->status, "%s%3u%%  sig %c%c%c%c  %s%u new",
             m->charging ? "+" : " ", m->battery_pct, bars > 0 ? '|' : '.', bars > 1 ? '|' : '.',
             bars > 2 ? '|' : '.', bars > 3 ? '|' : '.', "", m->unread);
    snprintf(v->footer, sizeof v->footer, "%s", m->status);
    const char *to = m->recipient ? name_of(m, m->recipient, nb) : "-";

    switch (m->screen) {
    case SCR_HOME:
        snprintf(v->title, sizeof v->title, "Nomiq");
        row(v, "%s", "Send message"); 
        { char b[24]; snprintf(b, sizeof b, "Inbox (%u new)", m->unread); row(v, "%s", b); }
        row(v, "%s", "Pair device"); row(v, "%s", "Settings");
        v->highlight = (int8_t)m->sel;
        break;
    case SCR_CONTACTS:
        snprintf(v->title, sizeof v->title, "Send to");
        for (uint8_t i = 0; i < m->n_contacts && v->n_rows < UI_VIEW_ROWS; i++) row(v, "%s", m->contacts[i].name);
        v->highlight = (int8_t)m->sel;
        break;
    case SCR_COMPOSE:
        snprintf(v->title, sizeof v->title, "To: %s", to);
        row(v, "%s", "Voice message (hold REC)"); row(v, "%s", "Voice to text"); row(v, "%s", "Photo"); row(v, "%s", "Type text");
        v->highlight = (int8_t)m->sel;
        break;
    case SCR_VOICE_REC:
        snprintf(v->title, sizeof v->title, "Recording...  To: %s", to);
        snprintf(v->body, sizeof v->body, "Release REC to stop\nmax %u s", (unsigned)(m->rec_max_ms / 1000));
        break;
    case SCR_VOICE_PREVIEW:
        snprintf(v->title, sizeof v->title, "Voice %u.%u s  To: %s", (unsigned)(m->rec_ms / 1000), (unsigned)(m->rec_ms % 1000 / 100), to);
        row(v, "%s", "SEND"); row(v, "%s", "Play"); row(v, "%s", "Discard");
        v->highlight = (int8_t)m->sel;
        break;
    case SCR_STT_LISTEN:
        snprintf(v->title, sizeof v->title, "Voice to text  To: %s", to);
        snprintf(v->body, sizeof v->body, "Hold REC and speak.\nRecognition runs on this device (offline).");
        break;
    case SCR_STT_WAIT:
        snprintf(v->title, sizeof v->title, "Transcribing...");
        snprintf(v->body, sizeof v->body, "Please wait");
        break;
    case SCR_TEXT_PREVIEW:
        snprintf(v->title, sizeof v->title, m->low_confidence ? "Check text (unclear)" : "Check text");
        snprintf(v->body, sizeof v->body, "%s", m->text_len ? m->text : "(nothing recognised)");
        row(v, "%s", "SEND"); row(v, "%s", "Edit"); row(v, "%s", "Retry"); row(v, "%s", "Send as voice");
        v->highlight = (int8_t)m->sel;
        break;
    case SCR_EDITOR:
        snprintf(v->title, sizeof v->title, "Edit  [%c]  hold SELECT = done", UI_EDITOR_ALPHABET[m->ed_char] == ' ' ? '_' : UI_EDITOR_ALPHABET[m->ed_char]);
        snprintf(v->body, sizeof v->body, "%s_", m->text);
        break;
    case SCR_CAMERA:
        snprintf(v->title, sizeof v->title, "Camera  To: %s", to);
        snprintf(v->body, sizeof v->body, "Press CAMERA to capture");
        v->show_image = true;
        break;
    case SCR_PHOTO_PREVIEW:
        snprintf(v->title, sizeof v->title, "Photo %u kB  ~%u s", (unsigned)((m->photo_bytes + 999) / 1000), (unsigned)((m->photo_eta_ms + 999) / 1000));
        row(v, "%s", "SEND"); row(v, "%s", "Retake");
        v->highlight = (int8_t)m->sel;
        v->show_image = true;
        break;
    case SCR_SENDING:
        snprintf(v->title, sizeof v->title, "Sending to %s", to);
        v->progress = (int8_t)m->progress;
        snprintf(v->body, sizeof v->body, "BACK = continue in background\nhold SELECT = cancel");
        break;
    case SCR_INBOX:
        snprintf(v->title, sizeof v->title, "Inbox");
        for (uint8_t i = 0; i < m->n_inbox && v->n_rows < UI_VIEW_ROWS; i++) {
            char b[UI_VIEW_COLS + 1];
            snprintf(b, sizeof b, "%s %s %s", m->inbox[i].unread ? "*" : " ", kind_name(m->inbox[i].kind), name_of(m, m->inbox[i].peer, nb));
            row(v, "%s", b);
        }
        if (!m->n_inbox) snprintf(v->body, sizeof v->body, "No messages yet");
        v->highlight = m->n_inbox ? (int8_t)m->sel : -1;
        break;
    case SCR_VIEWER:
        snprintf(v->title, sizeof v->title, "Message");
        snprintf(v->body, sizeof v->body, "%s", m->text);   /* controller loads text / "Playing voice" */
        v->show_image = m->n_inbox && m->inbox[m->sel].kind == 3;
        break;
    case SCR_PAIRING:
        snprintf(v->title, sizeof v->title, "Pairing");
        if (m->pair_code) snprintf(v->body, sizeof v->body, "Code %06u\nSame on both screens?\nSELECT = confirm, BACK = cancel", (unsigned)m->pair_code);
        else snprintf(v->body, sizeof v->body, "Start pairing on the other device.\nWaiting...");
        break;
    case SCR_SETTINGS:
        snprintf(v->title, sizeof v->title, "Settings");
        row(v, "%s", "Radio"); row(v, "%s", "Voice quality"); row(v, "%s", "Photo size"); row(v, "%s", "About");
        v->highlight = (int8_t)m->sel;
        break;
    default:
        break;
    }
}

/* Drives complete user flows through the UI state machine (Modes 1-4 of the product brief). */
#include <stdio.h>
#include <string.h>

#include "ui_model.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static ui_model_t M;
static ui_action_t A[8];
static int NA;
static uint32_t T;

static int ev(ui_event_type_t t) { ui_event_t e = { .type = t, .now_ms = T += 100 }; return NA = ui_model_handle(&M, &e, A, 8); }
static int evx(ui_event_t e) { e.now_ms = T += 100; return NA = ui_model_handle(&M, &e, A, 8); }
static int has(ui_action_type_t t) { for (int i = 0; i < NA; i++) if (A[i].type == t) return i + 1; return 0; }
#define SCR(s) CHECK(M.screen == (s))

int main(void)
{
    ui_model_init(&M, 30000);
    ui_contact_t c[2] = { { 0xB0B0, "Bravo" }, { 0xC0C0, "Charlie" } };
    ui_model_set_contacts(&M, c, 2);

    /* pick Charlie */
    SCR(SCR_HOME);
    ev(EV_SELECT); SCR(SCR_CONTACTS);
    ev(EV_DOWN); ev(EV_SELECT); SCR(SCR_COMPOSE); CHECK(M.recipient == 0xC0C0);

    /* MODE 1: voice message (hold, release, preview, send, delivered) */
    ev(EV_REC_DOWN); SCR(SCR_VOICE_REC); CHECK(has(ACT_REC_START));
    T += 4000;
    ev(EV_REC_UP); SCR(SCR_VOICE_PREVIEW); CHECK(has(ACT_REC_STOP)); CHECK(M.rec_ms >= 4000 && M.rec_ms < 4300);
    ev(EV_DOWN); ev(EV_SELECT); CHECK(has(ACT_PLAY_RECORDING)); SCR(SCR_VOICE_PREVIEW);
    ev(EV_UP); ev(EV_SELECT); SCR(SCR_SENDING);
    { int i = has(ACT_SEND_VOICE); CHECK(i && A[i - 1].peer == 0xC0C0); }
    ui_model_sending_started(&M, 41);
    evx((ui_event_t){ .type = EV_PROGRESS, .seq = 41, .arg = 50 }); CHECK(M.progress == 50);
    evx((ui_event_t){ .type = EV_PROGRESS, .seq = 99, .arg = 10 }); CHECK(M.progress == 50);   /* other message */
    evx((ui_event_t){ .type = EV_DELIVERED, .seq = 41 }); SCR(SCR_HOME); CHECK(strcmp(M.status, "Delivered") == 0);

    /* max recording length auto-stops */
    ev(EV_REC_DOWN); SCR(SCR_VOICE_REC);
    T += 31000; ev(EV_TICK); SCR(SCR_VOICE_PREVIEW); CHECK(has(ACT_REC_STOP));
    ev(EV_BACK); SCR(SCR_COMPOSE);

    /* MODE 2: voice -> text, clear speech */
    ev(EV_DOWN); ev(EV_SELECT); SCR(SCR_STT_LISTEN);
    ev(EV_REC_DOWN); CHECK(has(ACT_REC_START));
    T += 2500; ev(EV_REC_UP); SCR(SCR_STT_WAIT); CHECK(has(ACT_REC_STOP) && has(ACT_STT_START));
    evx((ui_event_t){ .type = EV_STT_FINAL, .text = "meet at the gate", .confidence = 0.9f });
    SCR(SCR_TEXT_PREVIEW); CHECK(!M.low_confidence && strcmp(M.text, "meet at the gate") == 0);
    /* edit: append "!" */
    ev(EV_DOWN); ev(EV_SELECT); SCR(SCR_EDITOR);
    for (int g = 0; g < 100 && M.screen == SCR_EDITOR && UI_EDITOR_ALPHABET[M.ed_char] != '!'; g++) ev(EV_DOWN);
    ev(EV_SELECT); CHECK(strcmp(M.text, "meet at the gate!") == 0);
    ev(EV_BACK); CHECK(strcmp(M.text, "meet at the gate") == 0);   /* delete last */
    ev(EV_SELECT); ev(EV_SELECT_LONG); SCR(SCR_TEXT_PREVIEW);
    CHECK(strcmp(M.text, "meet at the gate!") == 0);
    ev(EV_SELECT); SCR(SCR_SENDING);
    { int i = has(ACT_SEND_TEXT); CHECK(i && strcmp(A[i - 1].text, "meet at the gate!") == 0 && A[i - 1].peer == 0xC0C0); }
    ui_model_sending_started(&M, 42);
    evx((ui_event_t){ .type = EV_RETRY_LATER, .seq = 42 }); SCR(SCR_HOME);

    /* MODE 2: unclear speech -> low confidence -> send as voice instead */
    ev(EV_SELECT); ev(EV_SELECT); SCR(SCR_COMPOSE);  /* contacts: sel reset to Bravo */
    CHECK(M.recipient == 0xB0B0);
    ev(EV_DOWN); ev(EV_SELECT); ev(EV_REC_DOWN); ev(EV_REC_UP); SCR(SCR_STT_WAIT);
    evx((ui_event_t){ .type = EV_STT_ERROR }); SCR(SCR_TEXT_PREVIEW); CHECK(M.low_confidence && M.text_len == 0);
    ev(EV_SELECT); SCR(SCR_TEXT_PREVIEW); CHECK(!has(ACT_SEND_TEXT));                      /* empty text not sendable */
    ev(EV_UP); ev(EV_SELECT); SCR(SCR_SENDING); CHECK(has(ACT_SEND_VOICE));                 /* 4th item: send as voice */
    ev(EV_BACK); SCR(SCR_HOME); CHECK(strcmp(M.status, "Sending in background") == 0);

    /* MODE 3: photo with size + ETA, retake, send */
    ev(EV_CAMERA); SCR(SCR_CAMERA); CHECK(has(ACT_CAMERA_ON));
    ev(EV_CAMERA); CHECK(has(ACT_CAPTURE));
    evx((ui_event_t){ .type = EV_PHOTO_READY, .arg = 11500, .arg2 = 76500 }); SCR(SCR_PHOTO_PREVIEW);
    CHECK(strstr(M.status, "12 kB") && strstr(M.status, "77 s"));
    ev(EV_DOWN); ev(EV_SELECT); SCR(SCR_CAMERA);                                              /* retake */
    ev(EV_SELECT); evx((ui_event_t){ .type = EV_PHOTO_READY, .arg = 9000, .arg2 = 60000 });
    ev(EV_SELECT); SCR(SCR_SENDING); CHECK(has(ACT_CAMERA_OFF) && has(ACT_SEND_PHOTO));
    ui_model_sending_started(&M, 43);
    ev(EV_SELECT_LONG); SCR(SCR_HOME); { int i = has(ACT_CANCEL_SEND); CHECK(i && A[i - 1].seq == 43); }

    /* MODE 4: receive while display is off, then read it */
    T += UI_DISPLAY_TIMEOUT_MS + 1; ev(EV_TICK); CHECK(!M.display_on && has(ACT_DISPLAY_OFF));
    evx((ui_event_t){ .type = EV_RECEIVED, .seq = 7, .peer = 0xB0B0, .arg = 3 });
    CHECK(M.display_on && has(ACT_DISPLAY_ON) && M.unread == 1 && strstr(M.status, "Bravo"));
    evx((ui_event_t){ .type = EV_RECEIVED, .seq = 8, .peer = 0xDEAD, .arg = 1 }); CHECK(strstr(M.status, "0000dead"));
    ev(EV_DOWN); ev(EV_SELECT); SCR(SCR_INBOX);
    CHECK(M.inbox[0].seq == 8 && M.inbox[1].seq == 7);
    ev(EV_DOWN); ev(EV_SELECT); SCR(SCR_VIEWER); { int i = has(ACT_OPEN_MESSAGE); CHECK(i && A[i - 1].seq == 7); }
    CHECK(M.unread == 1);
    ev(EV_BACK); SCR(SCR_INBOX); ev(EV_BACK); SCR(SCR_HOME);

    /* wake: a button on a dark screen only wakes; PTT works directly */
    T += UI_DISPLAY_TIMEOUT_MS + 1; ev(EV_TICK); CHECK(!M.display_on);
    ev(EV_SELECT); CHECK(M.display_on && has(ACT_DISPLAY_ON) && M.screen == SCR_HOME);
    T += UI_DISPLAY_TIMEOUT_MS + 1; ev(EV_TICK);
    ev(EV_REC_DOWN); CHECK(has(ACT_DISPLAY_ON) && has(ACT_REC_START)); SCR(SCR_VOICE_REC);
    ev(EV_REC_UP); ev(EV_BACK);

    /* pairing */
    ev(EV_BACK); SCR(SCR_CONTACTS); ev(EV_BACK); SCR(SCR_HOME);
    ev(EV_DOWN); ev(EV_DOWN); ev(EV_SELECT); SCR(SCR_PAIRING); CHECK(has(ACT_PAIR_BEGIN));
    ev(EV_SELECT); CHECK(!has(ACT_PAIR_CONFIRM));                                            /* no code yet */
    evx((ui_event_t){ .type = EV_PAIR_CODE, .peer = 0xD00D, .arg = 42 }); CHECK(strstr(M.status, "000042"));
    ev(EV_SELECT); { int i = has(ACT_PAIR_CONFIRM); CHECK(i && A[i - 1].peer == 0xD00D); }
    evx((ui_event_t){ .type = EV_PAIRED, .peer = 0xD00D }); SCR(SCR_HOME); CHECK(M.n_contacts == 3);

    /* power button */
    ui_event_t pw = { .type = EV_POWER, .arg = 1 }; evx(pw); CHECK(has(ACT_POWER_OFF));

    /* no contacts: Send goes to pairing */
    ui_model_init(&M, 0);
    ev(EV_SELECT); SCR(SCR_PAIRING); CHECK(has(ACT_PAIR_BEGIN));
    ev(EV_BACK); ev(EV_REC_DOWN); SCR(SCR_HOME); CHECK(!has(ACT_REC_START));                  /* no recipient yet */

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

/* End-to-end simulation: complete Nomiq devices (pairing, encryption, storage, transfers)
 * over a lossy simulated LoRa channel. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_core.h"
#include "nomiq_proto.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define NDEV 3
typedef struct { int dev; uint32_t busy_until; uint32_t pair_code; uint32_t pair_peer; int received, delivered, retry; uint32_t last_rx_seq; } devctx_t;
static devctx_t D[NDEV];
static app_t *A[NDEV];
static app_cfg_t C[NDEV];
static char ROOT[NDEV][128];
static const uint32_t ID[NDEV] = { 0x1A1A1A1A, 0x2B2B2B2B, 0x3C3C3C3C };

typedef struct { uint32_t t; int from; uint8_t b[256]; size_t n; } ev_t;
static ev_t Q[8192];
static int qn, loss = 0;
static uint32_t now;
static unsigned rng = 7;
static int rnd100(void) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % 100u); }
static ev_t captured; static int capture_next;

static uint32_t radio_send(const uint8_t *f, size_t n, void *ctx)
{
    devctx_t *d = ctx;
    uint32_t toa = 40u + (uint32_t)n * 2u;
    uint32_t start = d->busy_until > now ? d->busy_until : now;
    d->busy_until = start + toa;
    if (capture_next && d->dev == 0 && n > 30) { captured.n = n; memcpy(captured.b, f, n); captured.from = 0; capture_next = 0; }
    if (rnd100() >= loss && qn < 8192) { Q[qn].t = start + toa; Q[qn].from = d->dev; memcpy(Q[qn].b, f, n); Q[qn].n = n; qn++; }
    return toa;
}
static bool radio_busy(uint32_t t, void *ctx) { return ((devctx_t *)ctx)->busy_until > t; }
static void on_event(const app_event_t *e, void *ctx)
{
    devctx_t *d = ctx;
    switch (e->type) {
    case APP_EV_PAIR_CODE: d->pair_code = e->arg; d->pair_peer = e->peer; break;
    case APP_EV_RECEIVED: d->received++; d->last_rx_seq = e->seq; break;
    case APP_EV_DELIVERED: d->delivered++; break;
    case APP_EV_RETRY_LATER: d->retry++; break;
    default: break;
    }
}
static int space(const char *r, uint64_t *f, uint64_t *t) { (void)r; *f = 1000; *t = 2000; return 0; }

static void boot(int i)
{
    C[i] = (app_cfg_t){ .self_id = ID[i], .root = ROOT[i], .space = space, .radio_send = radio_send,
                        .radio_busy = radio_busy, .on_event = on_event, .ctx = &D[i] };
    D[i].dev = i;
    A[i] = app_create(&C[i], now);
}

static void step(uint32_t until)
{
    for (; now < until; now += 5) {
        for (int i = 0; i < qn;) {
            if (Q[i].t > now) { i++; continue; }
            ev_t e = Q[i]; Q[i] = Q[--qn];
            for (int d = 0; d < NDEV; d++)       /* broadcast medium: everyone in range hears it */
                if (d != e.from && A[d] && D[d].busy_until <= e.t - 1) app_on_frame(A[d], e.b, e.n, 8, -95, now);
        }
        for (int d = 0; d < NDEV; d++) if (A[d]) app_poll(A[d], now);
    }
}

static int run_until(int (*cond)(void), uint32_t limit)
{
    while (!cond() && now < limit) step(now + 100);
    return cond();
}

static uint32_t want_rx_b, want_del_a;
static int c_b_rx(void) { return D[1].received >= (int)want_rx_b; }
static int c_a_del(void) { return D[0].delivered >= (int)want_del_a; }
static int c_both(void) { return c_b_rx() && c_a_del(); }

static void make_file(const char *p, size_t n, unsigned seed)
{
    FILE *f = fopen(p, "wb");
    for (size_t i = 0; i < n; i++) fputc((int)((i * 37 + seed) ^ (i >> 5)) & 0xFF, f);
    fclose(f);
}

static int same_file(const char *a, const char *b)
{
    FILE *x = fopen(a, "rb"), *y = fopen(b, "rb");
    if (!x || !y) { if (x) fclose(x); if (y) fclose(y); return 0; }
    int ok = 1, c1, c2;
    do { c1 = fgetc(x); c2 = fgetc(y); if (c1 != c2) { ok = 0; break; } } while (c1 != EOF);
    fclose(x); fclose(y);
    return ok;
}

static void rx_path(int dev, uint32_t seq, char *p, size_t n)
{
    const msg_rec_t *r = msg_store_get(app_store(A[dev]), seq);
    if (!r) { p[0] = 0; return; }
    msg_store_path(app_store(A[dev]), r, p, n);
}

int main(void)
{
    char cmd[512] = "rm -rf";
    for (int i = 0; i < NDEV; i++) {
        snprintf(ROOT[i], sizeof ROOT[i], "/tmp/nomiq_app_%d_%c", (int)getpid(), 'A' + i);
        strcat(cmd, " "); strcat(cmd, ROOT[i]);
    }
    system(cmd);
    for (int i = 0; i < NDEV; i++) boot(i);

    /* ---- pairing A <-> B (C is not pairing) ---- */
    app_pair_begin(A[0], now);
    step(now + 2000);
    app_pair_begin(A[1], now);           /* B starts later: A must still receive B's key */
    step(now + 3000);
    CHECK(D[0].pair_peer == ID[1] && D[1].pair_peer == ID[0]);
    CHECK(D[0].pair_code == D[1].pair_code && D[0].pair_code < 1000000);
    printf("  pairing code on both screens: %06u\n", D[0].pair_code);
    CHECK(D[2].pair_code == 0);
    CHECK(app_pair_confirm(A[0], ID[1], "Bravo") == 0 && app_pair_confirm(A[1], ID[0], "Alpha") == 0);
    CHECK(app_is_paired(A[0], ID[1]) && app_is_paired(A[1], ID[0]) && !app_is_paired(A[2], ID[0]));
    { uint32_t ids[4]; char nm[4][16]; CHECK(app_peers(A[0], ids, nm, 4) == 1 && ids[0] == ID[1] && !strcmp(nm[0], "Bravo")); }

    /* ---- encrypted text ---- */
    uint32_t s1;
    CHECK(app_send_text(A[0], ID[1], "Meet at the north gate at 5", now, &s1) == 0);
    want_rx_b = 1; want_del_a = 1;
    CHECK(run_until(c_both, now + 60000));
    char p[256]; rx_path(1, D[1].last_rx_seq, p, sizeof p);
    FILE *f = fopen(p, "rb"); char txt[64] = {0}; if (f) { fread(txt, 1, sizeof txt - 1, f); fclose(f); }
    CHECK(strcmp(txt, "Meet at the north gate at 5") == 0);
    CHECK(msg_store_get(app_store(A[0]), s1)->state == MS_DELIVERED);
    CHECK(app_stats(A[1])->rx_auth_fail == 0);

    /* ---- long text (> 223 B) travels as an object ---- */
    char longtxt[600]; memset(longtxt, 'x', sizeof longtxt - 1); longtxt[599] = 0; memcpy(longtxt, "LONG:", 5);
    CHECK(app_send_text(A[0], ID[1], longtxt, now, NULL) == 0);
    want_rx_b = 2; want_del_a = 2;
    CHECK(run_until(c_both, now + 120000));
    rx_path(1, D[1].last_rx_seq, p, sizeof p);
    { const msg_rec_t *r = msg_store_get(app_store(A[1]), D[1].last_rx_seq); CHECK(r && r->kind == MSG_TEXT && r->size == 599); }

    /* ---- photo over a 20 % loss link ---- */
    loss = 20;
    char photo[256]; snprintf(photo, sizeof photo, "%s/test_photo.jpg", ROOT[0]);
    make_file(photo, 15000, 3);
    uint32_t sp;
    CHECK(app_send_file(A[0], ID[1], MSG_PHOTO, photo, now, &sp) == 0);
    want_rx_b = 3; want_del_a = 3;
    uint32_t t0 = now;
    CHECK(run_until(c_both, now + 900000));
    printf("  15 kB photo @20%% loss: %.1f s (sim)\n", (now - t0) / 1000.0);
    rx_path(1, D[1].last_rx_seq, p, sizeof p);
    CHECK(same_file(photo, p));
    { const msg_rec_t *r = msg_store_get(app_store(A[1]), D[1].last_rx_seq); CHECK(r && r->kind == MSG_PHOTO && r->state == MS_RECEIVED); }

    /* ---- sender power loss mid-transfer, resume after reboot ---- */
    loss = 5;
    char voice[256]; snprintf(voice, sizeof voice, "%s/test_voice.c2", ROOT[0]);
    make_file(voice, 20000, 9);
    uint32_t sv;
    CHECK(app_send_file(A[0], ID[1], MSG_VOICE, voice, now, &sv) == 0);
    for (int i = 0; i < 200 && msg_store_get(app_store(A[0]), sv)->state != MS_SENDING; i++) step(now + 50);
    step(now + 15000);                           /* part of the object is across */
    uint32_t frames_before = app_stats(A[0])->tx_frames;
    app_destroy(A[0]); A[0] = NULL; qn = 0;      /* power cut: RAM lost, in-flight frames lost */
    step(now + 2000);
    boot(0);                                      /* reboot from SD + secrets */
    CHECK(app_is_paired(A[0], ID[1]));
    CHECK(msg_store_get(app_store(A[0]), sv)->state == MS_PENDING);   /* SENDING -> PENDING */
    want_rx_b = 4; D[0].delivered = 0; want_del_a = 1;
    CHECK(run_until(c_both, now + 900000));
    uint32_t frames_after = app_stats(A[0])->tx_frames;
    printf("  resume after sender reboot: %u frames before power cut, %u after (object = %u frags)\n",
           frames_before, frames_after, nomiq_frag_count(20000));
    CHECK(frames_after < nomiq_frag_count(20000));                    /* did not start over */
    rx_path(1, D[1].last_rx_seq, p, sizeof p);
    CHECK(same_file(voice, p));

    /* ---- frame counter never reused across the reboot (nonce safety) ---- */
    capture_next = 1;
    app_send_text(A[0], ID[1], "after reboot", now, NULL);
    want_rx_b = 5;
    CHECK(run_until(c_b_rx, now + 60000));
    CHECK(app_stats(A[1])->rx_replay == 0);    /* new counters were all fresh for B's window */

    /* ---- replayed frame is rejected ---- */
    uint32_t rep = app_stats(A[1])->rx_replay;
    app_on_frame(A[1], captured.b, captured.n, 8, -95, now);
    CHECK(app_stats(A[1])->rx_replay == rep + 1);
    CHECK(D[1].received == 5);

    /* ---- tampered frame is rejected ---- */
    captured.b[30] ^= 0x10;
    uint16_t crc = nomiq_crc16(captured.b, 18, 0xFFFF);
    crc = nomiq_crc16(&captured.b[20], captured.n - 20, crc);
    captured.b[18] = (uint8_t)crc; captured.b[19] = (uint8_t)(crc >> 8);
    uint32_t af = app_stats(A[1])->rx_auth_fail;
    app_on_frame(A[1], captured.b, captured.n, 8, -95, now);
    CHECK(app_stats(A[1])->rx_auth_fail == af + 1);

    /* ---- unpaired device C cannot inject messages; its message stays queued ---- */
    uint32_t sc;
    CHECK(app_send_text(A[2], ID[1], "spoof", now, &sc) == 0);
    step(now + 20000);
    CHECK(D[1].received == 5);
    CHECK(msg_store_get(app_store(A[2]), sc)->state == MS_PENDING);  /* never sent in plaintext */

    /* ---- cancel ---- */
    char big[256]; snprintf(big, sizeof big, "%s/big.jpg", ROOT[0]);
    make_file(big, 60000, 5);
    uint32_t sb;
    app_send_file(A[0], ID[1], MSG_PHOTO, big, now, &sb);
    step(now + 5000);
    CHECK(app_cancel(A[0], sb, now) == 0);
    CHECK(msg_store_get(app_store(A[0]), sb)->state == MS_CANCELLED);

    /* ---- history survives B's reboot ---- */
    app_destroy(A[1]); A[1] = NULL; boot(1);
    msg_rec_t l[16];
    int n = msg_store_list(app_store(A[1]), l, 16, 0, 0, ID[0]);
    CHECK(n == 5);

    for (int i = 0; i < NDEV; i++) app_destroy(A[i]);
    system(cmd);
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

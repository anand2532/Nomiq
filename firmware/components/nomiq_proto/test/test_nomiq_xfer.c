/* Discrete-event simulation tests for the transfer engine (two devices, lossy channel). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomiq_xfer.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define DEV_A 0xA0000001u
#define DEV_B 0xB0000002u
#define MAXQ  4096
#define OBJ_MAX (NOMIQ_MAX_FRAGS * NOMIQ_MAX_PAYLOAD)

/* ---------------- simulated channel ---------------- */
typedef struct { uint32_t s, t; uint8_t buf[NOMIQ_LORA_MAX_FRAME]; size_t len; } ev_t;
/* half-duplex: remember recent TX intervals per device; frames arriving while the
 * receiving device transmits are lost */
static uint32_t txi_s[2][64], txi_e[2][64];
static unsigned txi_n[2];
static int half_duplex_drops;
static ev_t q[MAXQ];
static int qn;
static uint32_t now;
static uint32_t busy_until[2];  /* per device radio */
static int loss_pct;            /* frame loss both directions */
static int sending_dev;         /* which device the current send() belongs to */
static unsigned rng = 1;
static int rnd100(void) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % 100u); }
static uint32_t air_total[2];

static uint32_t sim_send(const uint8_t *f, size_t len, void *ctx)
{
    (void)ctx;
    uint32_t toa = 40u + (uint32_t)len * 2u;   /* ~SF7/125 kHz-ish */
    uint32_t start = busy_until[sending_dev] > now ? busy_until[sending_dev] : now;
    busy_until[sending_dev] = start + toa;
    air_total[sending_dev] += toa;
    txi_s[sending_dev][txi_n[sending_dev] % 64] = start;
    txi_e[sending_dev][txi_n[sending_dev] % 64] = start + toa;
    txi_n[sending_dev]++;
    if (rnd100() >= loss_pct && qn < MAXQ) {
        q[qn].s = start;
        q[qn].t = start + toa;
        memcpy(q[qn].buf, f, len);
        q[qn].len = len;
        qn++;
    }
    return toa;
}

/* ---------------- device A (sender) object source ---------------- */
static uint8_t src_obj[OBJ_MAX];
static int src_read(void *obj_ctx, uint32_t off, uint8_t *buf, uint16_t len)
{
    memcpy(buf, (uint8_t *)obj_ctx + off, len);
    return 0;
}

/* ---------------- device B (receiver) storage ---------------- */
static uint8_t dst_obj[OBJ_MAX];
static bool storage_full;
static int corrupt_once;
static int objects_done, texts_done;
static char last_text[256];
static uint8_t saved_bitmap[NOMIQ_MAX_FRAGS / 8];
static nomiq_rx_obj_t saved_obj;
static bool have_saved;

static void sha8_of(const uint8_t *d, uint32_t n, uint8_t out[8])
{   /* FNV-1a 64 as a stand-in for truncated SHA-256 in host tests */
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; i++) { h ^= d[i]; h *= 1099511628211ull; }
    for (int i = 0; i < 8; i++) out[i] = (uint8_t)(h >> (8 * i));
}
static bool b_can_store(uint32_t src, uint32_t size, void *ctx) { (void)src; (void)size; (void)ctx; return !storage_full; }
static int b_write(const nomiq_rx_obj_t *o, const uint8_t sha8[8], uint32_t off, const uint8_t *d, uint16_t len, void *ctx)
{
    (void)o; (void)sha8; (void)ctx;
    memcpy(&dst_obj[off], d, len);
    if (corrupt_once) { dst_obj[off] ^= 0xFF; corrupt_once = 0; }
    return 0;
}
static bool b_verify(const nomiq_rx_obj_t *o, const uint8_t sha8[8], void *ctx)
{
    (void)ctx; uint8_t h[8]; sha8_of(dst_obj, o->size, h); return memcmp(h, sha8, 8) == 0;
}
static void b_save(const nomiq_rx_obj_t *o, const uint8_t sha8[8], uint8_t t, void *ctx)
{
    (void)sha8; (void)t; (void)ctx; saved_obj = *o; have_saved = true; (void)saved_bitmap;
}
static bool b_load(uint32_t src, const uint8_t sha8[8], nomiq_rx_obj_t *o, void *ctx)
{
    (void)sha8; (void)ctx;
    if (!have_saved || saved_obj.src != src) return false;
    *o = saved_obj; return true;
}
static void b_text(uint32_t src, const uint8_t *t, uint8_t len, void *ctx)
{
    (void)src; (void)ctx; memcpy(last_text, t, len); last_text[len] = 0; texts_done++;
}
static void b_obj(const nomiq_rx_obj_t *o, uint8_t t, const uint8_t sha8[8], void *ctx)
{
    (void)o; (void)t; (void)sha8; (void)ctx; objects_done++;
}

static nomiq_io_t io_a = { .send = sim_send, .read = src_read };
static nomiq_io_t io_b = { .send = sim_send, .can_store = b_can_store, .write = b_write, .verify = b_verify,
                           .save_partial = b_save, .load_partial = b_load, .on_text = b_text, .on_object = b_obj };

static nomiq_rx_t rxB;

static void reset_sim(int loss, unsigned seed)
{
    qn = 0; now = 0; busy_until[0] = busy_until[1] = 0; air_total[0] = air_total[1] = 0;
    loss_pct = loss; rng = seed; txi_n[0] = txi_n[1] = 0; half_duplex_drops = 0; storage_full = false; corrupt_once = 0;
    objects_done = texts_done = 0; have_saved = false;
    memset(dst_obj, 0, sizeof dst_obj);
    nomiq_rx_engine_init(&rxB, DEV_B);
}

/* deliver due frames */
static void deliver(nomiq_tx_t *txs[], int ntx)
{
    for (int i = 0; i < qn;) {
        if (q[i].t > now) { i++; continue; }
        ev_t e = q[i];
        q[i] = q[--qn];
        nomiq_hdr_t h; const uint8_t *pl;
        if (nomiq_frame_decode(e.buf, e.len, &h, &pl) != NOMIQ_OK) continue;
        int rxdev = h.dst == DEV_B ? 1 : 0;
        bool clash = false;
        for (unsigned k = 0; k < 64 && k < txi_n[rxdev]; k++)
            if (txi_s[rxdev][k] < e.t && txi_e[rxdev][k] > e.s) clash = true;
        if (clash) { half_duplex_drops++; continue; }
        if (h.dst == DEV_B) { sending_dev = 1; nomiq_rx_on_frame(&rxB, &h, pl, 8, -90, now, &io_b); }
        else if (h.dst == DEV_A) { for (int k = 0; k < ntx; k++) if (txs[k] && nomiq_tx_on_frame(txs[k], &h, pl, now)) break; }
    }
}

/* run until all txs terminal or timeout; optional hook at each step */
typedef void (*hook_t)(nomiq_tx_t *txs[], int ntx);
static uint32_t run(nomiq_tx_t *txs[], int ntx, uint32_t limit_ms, hook_t hook)
{
    for (; now < limit_ms; now += 5) {
        deliver(txs, ntx);
        if (hook) hook(txs, ntx);
        bool all = true;
        for (int k = 0; k < ntx; k++) if (txs[k] && !nomiq_tx_done(txs[k])) all = false;
        if (all) break;
        bool radio_free = now >= busy_until[0];
        int pick = radio_free ? nomiq_sched_pick(txs, ntx, now) : -1;
        sending_dev = 0;
        for (int k = 0; k < ntx; k++) if (txs[k]) nomiq_tx_poll(txs[k], now, k == pick, &io_a);
        nomiq_rx_poll(&rxB, now);
    }
    return now;
}

static void fill_src(uint32_t n, unsigned seed)
{
    for (uint32_t i = 0; i < n; i++) src_obj[i] = (uint8_t)((i * 131u + seed) ^ (i >> 7));
}

/* ---------------- tests ---------------- */

static void test_text(void)
{
    const char *msg = "Meet at the north gate, 5 pm";
    for (int loss = 0; loss <= 50; loss += 25) {
        reset_sim(loss, 7u + (unsigned)loss);
        nomiq_tx_t t; nomiq_tx_t *txs[1] = { &t };
        nomiq_tx_begin_text(&t, DEV_A, DEV_B, 1, (const uint8_t *)msg, (uint8_t)strlen(msg));
        run(txs, 1, 120000, NULL);
        CHECK(t.state == NOMIQ_TX_DELIVERED);
        CHECK(texts_done == 1);           /* dedup: delivered to app exactly once */
        CHECK(strcmp(last_text, msg) == 0);
    }
}

static void test_object(int loss, uint32_t size, unsigned seed)
{
    reset_sim(loss, seed);
    fill_src(size, seed);
    uint8_t sha[8]; sha8_of(src_obj, size, sha);
    nomiq_tx_t t; nomiq_tx_t *txs[1] = { &t };
    CHECK(nomiq_tx_begin(&t, DEV_A, DEV_B, 100, NOMIQ_OBJ_PHOTO, size, sha, src_obj) == NOMIQ_OK);
    uint32_t end = run(txs, 1, 3600000, NULL);
    CHECK(t.state == NOMIQ_TX_DELIVERED);
    CHECK(objects_done == 1);
    CHECK(memcmp(src_obj, dst_obj, size) == 0);
    printf("  object %5u B loss %2d%%: %3u frags, %4u frames, %6.1f s, A air %.1f s, hd-drops %d\n",
           size, loss, t.frag_n, t.frames_sent, end / 1000.0, air_total[0] / 1000.0, half_duplex_drops);
    if (loss == 0) CHECK(t.frames_sent == (uint32_t)t.frag_n + 1u); /* OFFER + each frag once */
}

static nomiq_tx_t *g_tx;
static int rebooted;
static void hook_rx_reboot(nomiq_tx_t *txs[], int ntx)
{
    (void)txs; (void)ntx;
    if (!rebooted && g_tx->acked_cnt > g_tx->frag_n / 2) {
        nomiq_rx_engine_init(&rxB, DEV_B);  /* RAM state lost; partial persisted via save_partial */
        rebooted = 1;
    }
}

static void test_receiver_reboot(void)
{
    const uint32_t size = 15000;
    reset_sim(10, 99);
    fill_src(size, 3);
    uint8_t sha[8]; sha8_of(src_obj, size, sha);
    nomiq_tx_t t; nomiq_tx_t *txs[1] = { &t }; g_tx = &t; rebooted = 0;
    nomiq_tx_begin(&t, DEV_A, DEV_B, 200, NOMIQ_OBJ_VOICE, size, sha, src_obj);
    run(txs, 1, 3600000, hook_rx_reboot);
    CHECK(rebooted == 1);
    CHECK(t.state == NOMIQ_TX_DELIVERED);
    CHECK(memcmp(src_obj, dst_obj, size) == 0);
}

static void test_sender_reboot_resume(void)
{
    const uint32_t size = 20000;
    reset_sim(0, 5);
    fill_src(size, 9);
    uint8_t sha[8]; sha8_of(src_obj, size, sha);
    nomiq_tx_t t1; nomiq_tx_t *txs1[1] = { &t1 };
    nomiq_tx_begin(&t1, DEV_A, DEV_B, 300, NOMIQ_OBJ_PHOTO, size, sha, src_obj);
    /* run until roughly half the object has been acknowledged, then "power off" A */
    for (; now < 600000 && t1.acked_cnt < t1.frag_n / 2; now += 5) {
        deliver(txs1, 1);
        sending_dev = 0;
        nomiq_tx_poll(&t1, now, now >= busy_until[0] && nomiq_sched_pick(txs1, 1, now) == 0, &io_a);
    }
    uint16_t had = t1.acked_cnt;
    CHECK(had >= t1.frag_n / 2 && had < t1.frag_n);
    qn = 0; /* frames in flight lost */
    nomiq_tx_t t2; nomiq_tx_t *txs2[1] = { &t2 };
    nomiq_tx_begin(&t2, DEV_A, DEV_B, 301 /* new msg_id after reboot */, NOMIQ_OBJ_PHOTO, size, sha, src_obj);
    run(txs2, 1, 3600000, NULL);
    CHECK(t2.state == NOMIQ_TX_DELIVERED);
    CHECK(memcmp(src_obj, dst_obj, size) == 0);
    /* resumed: only the missing part is resent (+ OFFER) */
    CHECK(t2.frames_sent <= (uint32_t)(t2.frag_n - had) + 2u);
    printf("  resume: %u/%u frags held before reboot, %u frames after\n", had, t2.frag_n, t2.frames_sent);
}

static void test_storage_full(void)
{
    reset_sim(0, 1);
    storage_full = true;
    fill_src(5000, 1);
    uint8_t sha[8]; sha8_of(src_obj, 5000, sha);
    nomiq_tx_t t; nomiq_tx_t *txs[1] = { &t };
    nomiq_tx_begin(&t, DEV_A, DEV_B, 400, NOMIQ_OBJ_PHOTO, 5000, sha, src_obj);
    run(txs, 1, 60000, NULL);
    CHECK(t.state == NOMIQ_TX_REJECTED);
    CHECK(t.reject_reason == NOMIQ_REJ_STORAGE_FULL);
}

static void test_corrupt_then_recover(void)
{
    const uint32_t size = 3000;
    reset_sim(0, 2);
    fill_src(size, 4);
    corrupt_once = 1;
    uint8_t sha[8]; sha8_of(src_obj, size, sha);
    nomiq_tx_t t; nomiq_tx_t *txs[1] = { &t };
    nomiq_tx_begin(&t, DEV_A, DEV_B, 500, NOMIQ_OBJ_PHOTO, size, sha, src_obj);
    run(txs, 1, 600000, NULL);
    CHECK(t.state == NOMIQ_TX_DELIVERED);
    CHECK(memcmp(src_obj, dst_obj, size) == 0);
}

static uint32_t text_done_at;
static nomiq_tx_t *g_text;
static void hook_text(nomiq_tx_t *txs[], int ntx)
{
    (void)txs; (void)ntx;
    if (!text_done_at && g_text->state == NOMIQ_TX_DELIVERED) text_done_at = now;
}

static void test_priority(void)
{
    const uint32_t size = 30000;
    reset_sim(0, 11);
    fill_src(size, 6);
    uint8_t sha[8]; sha8_of(src_obj, size, sha);
    nomiq_tx_t photo, text;
    nomiq_tx_begin(&photo, DEV_A, DEV_B, 600, NOMIQ_OBJ_PHOTO, size, sha, src_obj);
    nomiq_tx_t *txs[2] = { &photo, NULL };
    /* start the photo, then queue a text mid-transfer */
    for (; now < 3000; now += 5) {
        deliver(txs, 2); sending_dev = 0;
        nomiq_tx_poll(&photo, now, now >= busy_until[0] && nomiq_sched_pick(txs, 2, now) == 0, &io_a);
    }
    const char *msg = "urgent";
    nomiq_tx_begin_text(&text, DEV_A, DEV_B, 601, (const uint8_t *)msg, 6);
    txs[1] = &text; g_text = &text; text_done_at = 0;
    uint32_t queued_at = now;
    run(txs, 2, 3600000, hook_text);
    CHECK(photo.state == NOMIQ_TX_DELIVERED && text.state == NOMIQ_TX_DELIVERED);
    CHECK(text_done_at > 0 && text_done_at - queued_at < 5000);   /* TP-RF-04: < 5 s */
    printf("  priority: text delivered %.2f s after queueing during a %u-frag photo\n",
           (text_done_at - queued_at) / 1000.0, photo.frag_n);
}

static void test_sched_and_duty(void)
{
    nomiq_tx_t a, b; uint8_t sha[8] = {0};
    nomiq_tx_begin(&a, 1, 2, 1, NOMIQ_OBJ_PHOTO, 1000, sha, src_obj);
    nomiq_tx_begin_text(&b, 1, 2, 2, (const uint8_t *)"x", 1);
    nomiq_tx_t *txs[2] = { &a, &b };
    CHECK(nomiq_sched_pick(txs, 2, 0) == 1);
    b.state = NOMIQ_TX_DELIVERED;
    CHECK(nomiq_sched_pick(txs, 2, 0) == 0);
    a.state = NOMIQ_TX_WAIT_SACK; a.deadline_ms = 1000;
    CHECK(nomiq_sched_pick(txs, 2, 500) == -1);
    CHECK(nomiq_sched_pick(txs, 2, 1000) == 0);

    nomiq_duty_t d; nomiq_duty_init(&d, 25);       /* 2.5 % => 90 s per rolling hour */
    CHECK(nomiq_duty_can_send(&d, 0, 90000));
    nomiq_duty_record(&d, 1000, 60000);
    nomiq_duty_record(&d, 120000, 29000);
    CHECK(nomiq_duty_used_ms(&d, 130000) == 89000);
    CHECK(nomiq_duty_can_send(&d, 130000, 1000));
    CHECK(!nomiq_duty_can_send(&d, 130000, 1001));
    CHECK(nomiq_duty_can_send(&d, 3600000 + 1000, 30000)); /* first minute aged out */
    nomiq_duty_t u; nomiq_duty_init(&u, 0);
    CHECK(nomiq_duty_can_send(&u, 0, 1u << 30));
}

/* Monte-Carlo: many seeds at 25 % loss must all deliver intact */
static void test_monte_carlo(void)
{
    int ok = 0, runs = 60;
    uint32_t worst = 0;
    for (int r = 0; r < runs; r++) {
        uint32_t size = 2000u + (uint32_t)r * 397u;
        reset_sim(25, 1000u + (unsigned)r);
        fill_src(size, (unsigned)r);
        uint8_t sha[8]; sha8_of(src_obj, size, sha);
        nomiq_tx_t t; nomiq_tx_t *txs[1] = { &t };
        nomiq_tx_begin(&t, DEV_A, DEV_B, (uint16_t)(700 + r), NOMIQ_OBJ_VOICE, size, sha, src_obj);
        uint32_t end = run(txs, 1, 3600000, NULL);
        if (t.state == NOMIQ_TX_DELIVERED && !memcmp(src_obj, dst_obj, size)) ok++;
        if (end > worst) worst = end;
    }
    CHECK(ok == runs);
    printf("  monte-carlo 25%% loss: %d/%d delivered, worst %.1f s\n", ok, runs, worst / 1000.0);
}

int main(void)
{
    test_text();
    test_object(0, 12000, 1);
    test_object(30, 12000, 2);
    test_object(40, 40000, 3);
    test_object(0, 1, 4);
    test_object(20, NOMIQ_MAX_PAYLOAD * 3, 5);
    test_receiver_reboot();
    test_sender_reboot_resume();
    test_storage_full();
    test_corrupt_then_recover();
    test_priority();
    test_sched_and_duty();
    test_monte_carlo();
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

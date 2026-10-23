/* app_core.c — see include/app_core.h */
#include "app_core.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomiq_crypto.h"
#include "nomiq_proto.h"
#include "nomiq_xfer.h"

#define CTR_BLOCK 1024u

typedef struct {
    uint32_t id;
    uint8_t  key[16];
    char     name[16];
    uint8_t  used;
} peer_disk_t;

typedef struct {
    peer_disk_t d;
    nomiq_replay_t replay;   /* RAM only */
} peer_t;

typedef struct {
    bool     used;
    uint32_t seq;
    uint8_t  kind;
    FILE    *f;
    uint8_t  text[NOMIQ_MAX_PAYLOAD];
    uint8_t  last_pct;
    nomiq_tx_t tx;
} slot_t;

typedef struct { uint32_t seq, at; } retry_t;

struct app {
    app_cfg_t cfg;
    msg_store_t store;
    nomiq_rx_t rx;
    nomiq_io_t io;
    slot_t slot[APP_MAX_ACTIVE];
    peer_t peer[APP_MAX_PEERS];
    retry_t retry[16];
    app_stats_t st;
    uint32_t ctr, ctr_limit;
    uint8_t id_priv[32], id_pub[32];
    bool have_id;
    bool pairing;
    uint32_t pair_until;
    uint32_t pair_peer;
    uint8_t pair_key[16];
    bool pair_pending;
    uint32_t pair_replied[APP_MAX_PEERS];
    uint32_t now;
};

/* ---------------- helpers ---------------- */

static void emit_ev(app_t *a, app_event_type_t t, uint32_t seq, uint32_t peer, uint8_t kind, uint32_t arg)
{
    if (!a->cfg.on_event) return;
    app_event_t e = { t, seq, peer, kind, arg };
    a->cfg.on_event(&e, a->cfg.ctx);
}

static int sec_save(app_t *a, const char *n, const void *d, size_t l)
{
    return a->cfg.secret_save ? a->cfg.secret_save(n, d, l, a->cfg.ctx) : msg_store_blob_save(&a->store, n, d, l);
}
static int sec_load(app_t *a, const char *n, void *d, size_t l)
{
    return a->cfg.secret_load ? a->cfg.secret_load(n, d, l, a->cfg.ctx) : msg_store_blob_load(&a->store, n, d, l);
}

static peer_t *peer_find(app_t *a, uint32_t id)
{
    for (int i = 0; i < APP_MAX_PEERS; i++) if (a->peer[i].d.used && a->peer[i].d.id == id) return &a->peer[i];
    return NULL;
}

static void peers_save(app_t *a)
{
    peer_disk_t d[APP_MAX_PEERS];
    for (int i = 0; i < APP_MAX_PEERS; i++) d[i] = a->peer[i].d;
    sec_save(a, "contacts", d, sizeof d);
}

static uint32_t next_ctr(app_t *a)
{
    if (a->ctr >= a->ctr_limit) {            /* reserve the next block before using it */
        uint32_t lim = a->ctr_limit + CTR_BLOCK;
        sec_save(a, "ctr", &lim, sizeof lim);
        a->ctr_limit = lim;
    }
    return a->ctr++;
}

static void hex8(const uint8_t *b, char *out) { for (int i = 0; i < 8; i++) sprintf(out + 2 * i, "%02x", b[i]); }

static void part_name(uint32_t src, const uint8_t sha8[8], char *name, size_t n)
{
    char h[17]; hex8(sha8, h);
    snprintf(name, n, "%08x_%s", (unsigned)src, h);
}

static void part_path(app_t *a, uint32_t src, const uint8_t sha8[8], char *p, size_t n)
{
    char name[40]; part_name(src, sha8, name, sizeof name);
    snprintf(p, n, "%s/partial/%s.part", a->store.root, name);
}

static int file_sha8(const char *path, uint32_t size, uint8_t out8[8])
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    nomiq_sha256_t c; uint8_t buf[512], h[32];
    nomiq_sha256_init(&c);
    uint32_t left = size;
    while (left) {
        size_t n = fread(buf, 1, left > sizeof buf ? sizeof buf : left, f);
        if (!n) break;
        nomiq_sha256_update(&c, buf, n);
        left -= (uint32_t)n;
    }
    fclose(f);
    nomiq_sha256_final(&c, h);
    memcpy(out8, h, 8);
    return left ? -1 : 0;
}

static long file_size(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *i = fopen(src, "rb"), *o = i ? fopen(dst, "wb") : NULL;
    if (!i || !o) { if (i) fclose(i); return -1; }
    uint8_t b[1024]; size_t n; int rc = 0;
    while ((n = fread(b, 1, sizeof b, i)) > 0) if (fwrite(b, 1, n, o) != n) { rc = -1; break; }
    fclose(i); fclose(o);
    return rc;
}

/* ---------------- link layer: seal / open ---------------- */

static uint32_t io_send(const uint8_t *frame, size_t len, void *ctx)
{
    app_t *a = ctx;
    nomiq_hdr_t h; const uint8_t *pl;
    if (nomiq_frame_decode(frame, len, &h, &pl) != NOMIQ_OK) return 0;
    peer_t *p = peer_find(a, h.dst);
    uint8_t buf[NOMIQ_LORA_MAX_FRAME];
    int n;
    if (p) {
        n = nomiq_frame_seal(p->d.key, next_ctr(a), &h, pl, buf, sizeof buf);
    } else if (a->cfg.open_mode) {
        memcpy(buf, frame, len); n = (int)len;
    } else {
        return 0;   /* never send plaintext to an unpaired peer */
    }
    if (n <= 0) return 0;
    a->st.tx_frames++;
    return a->cfg.radio_send(buf, (size_t)n, a->cfg.ctx);
}

/* ---------------- receiver hooks ---------------- */

static bool io_can_store(uint32_t src, uint32_t size, void *ctx)
{
    (void)src; (void)size;
    app_t *a = ctx;
    return msg_store_policy(&a->store) != STORE_REJECT_BULK;
}

static int io_write(const nomiq_rx_obj_t *o, const uint8_t sha8[8], uint32_t off, const uint8_t *d, uint16_t len, void *ctx)
{
    app_t *a = ctx;
    char p[192]; part_path(a, o->src, sha8, p, sizeof p);
    FILE *f = fopen(p, "r+b");
    if (!f) f = fopen(p, "w+b");
    if (!f) return -1;
    int rc = (fseek(f, (long)off, SEEK_SET) == 0 && fwrite(d, 1, len, f) == len) ? 0 : -1;
    fclose(f);
    return rc;
}

static bool io_verify(const nomiq_rx_obj_t *o, const uint8_t sha8[8], void *ctx)
{
    app_t *a = ctx;
    char p[192]; part_path(a, o->src, sha8, p, sizeof p);
    uint8_t h[8];
    return file_sha8(p, o->size, h) == 0 && memcmp(h, sha8, 8) == 0;
}

typedef struct { nomiq_rx_obj_t o; uint8_t type; } partial_blob_t;

static void io_save_partial(const nomiq_rx_obj_t *o, const uint8_t sha8[8], uint8_t t, void *ctx)
{
    app_t *a = ctx;
    char name[40]; part_name(o->src, sha8, name, sizeof name);
    partial_blob_t b = { *o, t };
    msg_store_blob_save(&a->store, name, &b, sizeof b);
}

static bool io_load_partial(uint32_t src, const uint8_t sha8[8], nomiq_rx_obj_t *o, void *ctx)
{
    app_t *a = ctx;
    char name[40]; part_name(src, sha8, name, sizeof name);
    partial_blob_t b;
    if (msg_store_blob_load(&a->store, name, &b, sizeof b) != 0) return false;
    *o = b.o;
    return true;
}

static void io_on_text(uint32_t src, const uint8_t *txt, uint8_t len, void *ctx)
{
    app_t *a = ctx;
    msg_rec_t r = { .peer = src, .kind = MSG_TEXT, .state = MS_RECEIVED, .outgoing = 0, .size = len, .ts = a->now / 1000 };
    if (msg_store_put(&a->store, &r) != 0) return;
    char p[192];
    if (msg_store_path(&a->store, &r, p, sizeof p) == 0) {
        FILE *f = fopen(p, "wb");
        if (f) { fwrite(txt, 1, len, f); fclose(f); }
    }
    emit_ev(a, APP_EV_RECEIVED, r.seq, src, MSG_TEXT, len);
}

static void io_on_object(const nomiq_rx_obj_t *o, uint8_t t, const uint8_t sha8[8], void *ctx)
{
    app_t *a = ctx;
    msg_rec_t r = { .peer = o->src, .kind = t, .state = MS_RECEIVED, .outgoing = 0, .size = o->size, .ts = a->now / 1000 };
    memcpy(r.sha8, sha8, 8);
    if (msg_store_put(&a->store, &r) != 0) return;
    char src[192], dst[192], name[40];
    part_path(a, o->src, sha8, src, sizeof src);
    msg_store_path(&a->store, &r, dst, sizeof dst);
    if (rename(src, dst) != 0) copy_file(src, dst);
    part_name(o->src, sha8, name, sizeof name);
    msg_store_blob_delete(&a->store, name);
    emit_ev(a, APP_EV_RECEIVED, r.seq, o->src, t, o->size);
}

/* ---------------- sender side ---------------- */

static int slot_read(void *obj_ctx, uint32_t off, uint8_t *buf, uint16_t len)
{
    slot_t *s = obj_ctx;
    if (!s->f || fseek(s->f, (long)off, SEEK_SET) != 0) return -1;
    return fread(buf, 1, len, s->f) == len ? 0 : -1;
}

static bool is_active(app_t *a, uint32_t seq)
{
    for (int i = 0; i < APP_MAX_ACTIVE; i++) if (a->slot[i].used && a->slot[i].seq == seq) return true;
    return false;
}

static bool retry_due(app_t *a, uint32_t seq)
{
    for (unsigned i = 0; i < 16; i++)
        if (a->retry[i].seq == seq) return (int32_t)(a->now - a->retry[i].at) >= 0;
    return true;
}

static void retry_set(app_t *a, uint32_t seq, uint32_t at)
{
    unsigned k = 0;
    for (unsigned i = 0; i < 16; i++) {
        if (a->retry[i].seq == seq) { k = i; break; }
        if ((int32_t)(a->retry[i].at - a->retry[k].at) < 0) k = i;
    }
    a->retry[k].seq = seq;
    a->retry[k].at = at;
}

static void slot_free(slot_t *s)
{
    if (s->f) fclose(s->f);
    s->f = NULL;
    s->used = false;
}

static bool slot_start(app_t *a, slot_t *s, const msg_rec_t *r)
{
    if (!peer_find(a, r->peer) && !a->cfg.open_mode) return false;
    char p[192];
    if (msg_store_path(&a->store, r, p, sizeof p) != 0) return false;
    memset(s, 0, sizeof *s);
    s->seq = r->seq;
    s->kind = r->kind;
    uint16_t msg_id = (uint16_t)r->seq;
    if (r->kind == MSG_TEXT && r->size <= NOMIQ_MAX_PAYLOAD) {
        FILE *f = fopen(p, "rb");
        if (!f) return false;
        size_t n = fread(s->text, 1, r->size, f);
        fclose(f);
        if (n != r->size) return false;
        nomiq_tx_begin_text(&s->tx, a->cfg.self_id, r->peer, msg_id, s->text, (uint8_t)n);
    } else {
        s->f = fopen(p, "rb");
        if (!s->f) return false;
        if (nomiq_tx_begin(&s->tx, a->cfg.self_id, r->peer, msg_id, r->kind, r->size, r->sha8, s) != NOMIQ_OK) {
            slot_free(s); return false;
        }
    }
    s->used = true;
    msg_store_set_state(&a->store, r->seq, MS_SENDING);
    return true;
}

static void fill_slots(app_t *a)
{
    msg_rec_t pend[16];
    int n = msg_store_pending(&a->store, pend, 16);
    for (int i = 0; i < n; i++) {
        if (pend[i].state == MS_PAUSED || is_active(a, pend[i].seq) || !retry_due(a, pend[i].seq)) continue;
        slot_t *free_s = NULL;
        for (int k = 0; k < APP_MAX_ACTIVE; k++) if (!a->slot[k].used) { free_s = &a->slot[k]; break; }
        if (!free_s) return;
        if (!slot_start(a, free_s, &pend[i])) retry_set(a, pend[i].seq, a->now + APP_RETRY_MS);
    }
}

static void finish_slot(app_t *a, slot_t *s)
{
#ifdef APP_DEBUG
    fprintf(stderr, "[%08x] finish seq %u state %d retries %u acked %u/%u rej %u frames %u\n", a->cfg.self_id, s->seq, s->tx.state, s->tx.retries, s->tx.acked_cnt, s->tx.frag_n, s->tx.reject_reason, s->tx.frames_sent);
#endif
    nomiq_tx_t *t = &s->tx;
    switch (t->state) {
    case NOMIQ_TX_DELIVERED:
        msg_store_set_state(&a->store, s->seq, MS_DELIVERED);
        emit_ev(a, APP_EV_DELIVERED, s->seq, t->peer, s->kind, 100);
        break;
    case NOMIQ_TX_REJECTED:
        msg_store_set_state(&a->store, s->seq, MS_FAILED);
        emit_ev(a, APP_EV_REJECTED, s->seq, t->peer, s->kind, t->reject_reason);
        break;
    case NOMIQ_TX_CANCELLED:
        msg_store_set_state(&a->store, s->seq, MS_CANCELLED);
        break;
    default:   /* FAILED: keep it queued, try again later */
        msg_store_set_state(&a->store, s->seq, MS_PENDING);
        retry_set(a, s->seq, a->now + APP_RETRY_MS);
        emit_ev(a, APP_EV_RETRY_LATER, s->seq, t->peer, s->kind, APP_RETRY_MS);
        break;
    }
    slot_free(s);
}

/* ---------------- pairing ---------------- */

static void send_pair(app_t *a, uint32_t dst)
{
    uint8_t buf[NOMIQ_LORA_MAX_FRAME];
    nomiq_hdr_t h = { .type = NOMIQ_T_PAIR, .flags = NOMIQ_PRIO_CONTROL, .len = 32, .src = a->cfg.self_id,
                      .dst = dst, .msg_id = 0, .frag_ix = 0, .frag_n = 1 };
    int n = nomiq_frame_encode(&h, a->id_pub, buf, sizeof buf);
    if (n > 0) { a->st.tx_frames++; a->cfg.radio_send(buf, (size_t)n, a->cfg.ctx); }
}

static void ensure_identity(app_t *a)
{
    if (a->have_id) return;
    if (sec_load(a, "identity", a->id_priv, 32) != 0) {
        nomiq_random(a->id_priv, 32);
        nomiq_x25519_public(a->id_priv, a->id_pub);   /* clamps id_priv */
        sec_save(a, "identity", a->id_priv, 32);
    }
    nomiq_x25519_public(a->id_priv, a->id_pub);
    a->have_id = true;
}

int app_pair_begin(app_t *a, uint32_t now_ms)
{
    a->now = now_ms;
    ensure_identity(a);
    a->pairing = true;
    a->pair_until = now_ms + APP_PAIR_WINDOW_MS;
    a->pair_pending = false;
    memset(a->pair_replied, 0, sizeof a->pair_replied);
    send_pair(a, NOMIQ_BROADCAST);
    return 0;
}

static void on_pair(app_t *a, const nomiq_hdr_t *h, const uint8_t *pl)
{
    if (!a->pairing || h->len != 32 || h->src == a->cfg.self_id) return;
    uint8_t shared[32]; uint32_t code;
    if (nomiq_x25519_shared(a->id_priv, pl, shared) != 0) return;
    if (nomiq_pair_derive(shared, a->cfg.self_id, h->src, a->pair_key, &code) != 0) return;
    memset(shared, 0, sizeof shared);
    a->pair_peer = h->src;
    a->pair_pending = true;
    bool replied = false;
    for (int i = 0; i < APP_MAX_PEERS; i++) if (a->pair_replied[i] == h->src) replied = true;
    if (!replied) {
        for (int i = 0; i < APP_MAX_PEERS; i++) if (!a->pair_replied[i]) { a->pair_replied[i] = h->src; break; }
        send_pair(a, h->src);   /* make sure the other side has our key even if it started later */
    }
    emit_ev(a, APP_EV_PAIR_CODE, 0, h->src, 0, code);
}

int app_pair_confirm(app_t *a, uint32_t peer, const char *name)
{
    if (!a->pair_pending || a->pair_peer != peer) return -ENOENT;
    peer_t *p = peer_find(a, peer);
    if (!p) for (int i = 0; i < APP_MAX_PEERS; i++) if (!a->peer[i].d.used) { p = &a->peer[i]; break; }
    if (!p) return -ENOSPC;
    memset(p, 0, sizeof *p);
    p->d.used = 1;
    p->d.id = peer;
    memcpy(p->d.key, a->pair_key, 16);
    snprintf(p->d.name, sizeof p->d.name, "%s", name ? name : "");
    peers_save(a);
    a->pair_pending = false;
    a->pairing = false;
    memset(a->pair_key, 0, sizeof a->pair_key);
    emit_ev(a, APP_EV_PAIRED, 0, peer, 0, 0);
    return 0;
}

bool app_is_paired(app_t *a, uint32_t peer) { return peer_find(a, peer) != NULL; }

/* ---------------- lifecycle ---------------- */

app_t *app_create(const app_cfg_t *cfg, uint32_t now_ms)
{
    app_t *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->cfg = *cfg;
    a->now = now_ms;
    if (msg_store_init(&a->store, cfg->root, cfg->space) != 0) { free(a); return NULL; }
    nomiq_rx_engine_init(&a->rx, cfg->self_id);
    a->io = (nomiq_io_t){
        .send = io_send, .read = slot_read, .can_store = io_can_store, .write = io_write,
        .verify = io_verify, .save_partial = io_save_partial, .load_partial = io_load_partial,
        .on_text = io_on_text, .on_object = io_on_object, .ctx = a,
    };
    peer_disk_t d[APP_MAX_PEERS];
    if (sec_load(a, "contacts", d, sizeof d) == 0)
        for (int i = 0; i < APP_MAX_PEERS; i++) a->peer[i].d = d[i];
    uint32_t lim = 0;
    sec_load(a, "ctr", &lim, sizeof lim);
    a->ctr = lim;                 /* everything below `lim` may have been used before a crash */
    a->ctr_limit = lim;           /* next_ctr() reserves a new block before the first use */
    if (sec_load(a, "identity", a->id_priv, 32) == 0) { nomiq_x25519_public(a->id_priv, a->id_pub); a->have_id = true; }
    return a;
}

void app_destroy(app_t *a)
{
    if (!a) return;
    for (int i = 0; i < APP_MAX_ACTIVE; i++) if (a->slot[i].f) fclose(a->slot[i].f);
    msg_store_deinit(&a->store);
    free(a);
}

msg_store_t *app_store(app_t *a) { return &a->store; }
const app_stats_t *app_stats(app_t *a) { return &a->st; }

/* ---------------- API ---------------- */

int app_send_text(app_t *a, uint32_t peer, const char *text, uint32_t now_ms, uint32_t *seq)
{
    a->now = now_ms;
    size_t n = strlen(text);
    msg_rec_t r = { .peer = peer, .kind = MSG_TEXT, .state = MS_PENDING, .outgoing = 1, .size = (uint32_t)n, .ts = now_ms / 1000 };
    nomiq_sha256_t c; uint8_t h[32];
    nomiq_sha256_init(&c); nomiq_sha256_update(&c, (const uint8_t *)text, n); nomiq_sha256_final(&c, h);
    memcpy(r.sha8, h, 8);
    /* write the content first, then the record (a crash leaves an orphan file, never a dangling record) */
    msg_rec_t probe = r; probe.seq = a->store.next_seq;
    char p[192];
    if (msg_store_path(&a->store, &probe, p, sizeof p) != 0) return -ENAMETOOLONG;
    FILE *f = fopen(p, "wb");
    if (!f) return -errno;
    fwrite(text, 1, n, f);
    fclose(f);
    int rc = msg_store_put(&a->store, &r);
    if (rc == 0 && seq) *seq = r.seq;
    return rc;
}

int app_send_file(app_t *a, uint32_t peer, uint8_t kind, const char *src_path, uint32_t now_ms, uint32_t *seq)
{
    a->now = now_ms;
    long sz = file_size(src_path);
    if (sz < 0) return -ENOENT;
    if (nomiq_frag_count((uint32_t)sz) > NOMIQ_MAX_FRAGS) return -EFBIG;
    msg_rec_t r = { .peer = peer, .kind = kind, .state = MS_PENDING, .outgoing = 1, .size = (uint32_t)sz, .ts = now_ms / 1000 };
    if (file_sha8(src_path, (uint32_t)sz, r.sha8) != 0) return -EIO;
    msg_rec_t probe = r; probe.seq = a->store.next_seq;
    char p[192];
    if (msg_store_path(&a->store, &probe, p, sizeof p) != 0) return -ENAMETOOLONG;
    if (copy_file(src_path, p) != 0) return -EIO;
    int rc = msg_store_put(&a->store, &r);
    if (rc == 0 && seq) *seq = r.seq;
    return rc;
}

int app_cancel(app_t *a, uint32_t seq, uint32_t now_ms)
{
    a->now = now_ms;
    for (int i = 0; i < APP_MAX_ACTIVE; i++) {
        slot_t *s = &a->slot[i];
        if (s->used && s->seq == seq) {
            nomiq_tx_cancel(&s->tx, now_ms, &a->io);
            finish_slot(a, s);
            return 0;
        }
    }
    return msg_store_set_state(&a->store, seq, MS_CANCELLED);
}

void app_on_frame(app_t *a, const uint8_t *frame, size_t len, int8_t snr, int16_t rssi, uint32_t now_ms)
{
    a->now = now_ms;
    a->st.rx_frames++;
    if (len > NOMIQ_LORA_MAX_FRAME) return;
    uint8_t buf[NOMIQ_LORA_MAX_FRAME];
    memcpy(buf, frame, len);
    nomiq_hdr_t h; const uint8_t *pl;
    int rc = nomiq_frame_decode(buf, len, &h, &pl);
    if (rc != NOMIQ_OK) { a->st.rx_crc++; return; }
    if (h.dst != a->cfg.self_id && h.dst != NOMIQ_BROADCAST) return;

    if (h.type == NOMIQ_T_PAIR && !(h.flags & NOMIQ_F_ENCRYPTED)) { on_pair(a, &h, pl); return; }

    if (h.flags & NOMIQ_F_ENCRYPTED) {
        peer_t *p = peer_find(a, h.src);
        if (!p) { a->st.rx_unpaired++; return; }
        uint32_t ctr;
        if (nomiq_frame_open(p->d.key, buf, len, &h, &pl, &ctr) != NOMIQ_OK) { a->st.rx_auth_fail++; return; }
        if (!nomiq_replay_accept(&p->replay, ctr)) { a->st.rx_replay++; return; }
    } else if (!a->cfg.open_mode) {
        a->st.rx_unpaired++;
        return;
    }

    for (int i = 0; i < APP_MAX_ACTIVE; i++)
        if (a->slot[i].used && nomiq_tx_on_frame(&a->slot[i].tx, &h, pl, now_ms)) return;
    int8_t r8 = (int8_t)(rssi < -128 ? -128 : (rssi > 127 ? 127 : rssi));
    nomiq_rx_on_frame(&a->rx, &h, pl, snr, r8, now_ms, &a->io);
}

void app_poll(app_t *a, uint32_t now_ms)
{
    a->now = now_ms;
    if (a->pairing && (int32_t)(now_ms - a->pair_until) >= 0) { a->pairing = false; a->pair_pending = false; }

    fill_slots(a);

    nomiq_tx_t *txs[APP_MAX_ACTIVE];
    for (int i = 0; i < APP_MAX_ACTIVE; i++) txs[i] = a->slot[i].used ? &a->slot[i].tx : NULL;
    bool busy = a->cfg.radio_busy && a->cfg.radio_busy(now_ms, a->cfg.ctx);
    int pick = busy ? -1 : nomiq_sched_pick(txs, APP_MAX_ACTIVE, now_ms);

    for (int i = 0; i < APP_MAX_ACTIVE; i++) {
        slot_t *s = &a->slot[i];
        if (!s->used) continue;
        nomiq_tx_poll(&s->tx, now_ms, i == pick, &a->io);
        uint8_t pct = nomiq_tx_progress_pct(&s->tx);
        if (pct >= s->last_pct + 10) { s->last_pct = pct; emit_ev(a, APP_EV_PROGRESS, s->seq, s->tx.peer, s->kind, pct); }
        if (nomiq_tx_done(&s->tx)) finish_slot(a, s);
    }
    nomiq_rx_poll(&a->rx, now_ms);
}

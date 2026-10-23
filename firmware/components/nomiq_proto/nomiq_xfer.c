/* nomiq_xfer.c — see include/nomiq_xfer.h */
#include "nomiq_xfer.h"

#include <string.h>

/* wrap-safe "a is at or after b" for millisecond clocks */
static bool time_reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t emit(const nomiq_io_t *io, uint8_t type, uint8_t flags, uint32_t src, uint32_t dst,
                     uint16_t msg_id, uint16_t frag_ix, uint16_t frag_n, const uint8_t *pl, uint8_t len)
{
    uint8_t buf[NOMIQ_LORA_MAX_FRAME];
    nomiq_hdr_t h = { .type = type, .flags = flags, .len = len, .src = src, .dst = dst,
                      .msg_id = msg_id, .frag_ix = frag_ix, .frag_n = frag_n };
    int n = nomiq_frame_encode(&h, pl, buf, sizeof buf);
    if (n < 0 || !io || !io->send) return 0;
    return io->send(buf, (size_t)n, io->ctx);
}

static uint32_t reply_wait_ms(uint8_t retries, uint32_t toa_ms, uint16_t msg_id)
{
    /* reply has comparable airtime to our frame; add processing guard + small jitter */
    return nomiq_backoff_ms(retries, 2u * toa_ms + 100u, (uint32_t)(msg_id * 37u) % 100u);
}

/* =========================== Sender =========================== */

static bool tx_acked(const nomiq_tx_t *tx, uint16_t ix) { return tx->acked[ix >> 3] & (1u << (ix & 7)); }
static void tx_ack(nomiq_tx_t *tx, uint16_t ix)
{
    if (ix < tx->frag_n && !tx_acked(tx, ix)) {
        tx->acked[ix >> 3] |= (uint8_t)(1u << (ix & 7));
        tx->acked_cnt++;
    }
}
static uint16_t tx_first_unacked(const nomiq_tx_t *tx)
{
    for (uint16_t i = 0; i < tx->frag_n; i++) if (!tx_acked(tx, i)) return i;
    return tx->frag_n;
}

static uint8_t prio_for(uint8_t obj_type)
{
    switch (obj_type) {
    case NOMIQ_OBJ_TEXT:  return NOMIQ_PRIO_TEXT;
    case NOMIQ_OBJ_VOICE: return NOMIQ_PRIO_VOICE;
    default:              return NOMIQ_PRIO_PHOTO;
    }
}

int nomiq_tx_begin(nomiq_tx_t *tx, uint32_t self, uint32_t peer, uint16_t msg_id, uint8_t obj_type,
                   uint32_t size, const uint8_t sha8[8], void *obj_ctx)
{
    if (!tx || !sha8) return NOMIQ_E_ARG;
    uint16_t n = nomiq_frag_count(size);
    if (n > NOMIQ_MAX_FRAGS) return NOMIQ_E_RANGE;
    memset(tx, 0, sizeof(*tx));
    tx->state = NOMIQ_TX_OFFER;
    tx->self = self; tx->peer = peer; tx->msg_id = msg_id;
    tx->obj_type = obj_type; tx->prio = prio_for(obj_type);
    tx->size = size; tx->frag_n = n; tx->obj_ctx = obj_ctx;
    memcpy(tx->sha8, sha8, 8);
    return NOMIQ_OK;
}

int nomiq_tx_begin_text(nomiq_tx_t *tx, uint32_t self, uint32_t peer, uint16_t msg_id,
                        const uint8_t *txt, uint8_t len)
{
    if (!tx || (!txt && len)) return NOMIQ_E_ARG;
    if (len > NOMIQ_MAX_PAYLOAD) return NOMIQ_E_LEN;
    memset(tx, 0, sizeof(*tx));
    tx->state = NOMIQ_TX_OFFER;
    tx->self = self; tx->peer = peer; tx->msg_id = msg_id;
    tx->obj_type = NOMIQ_OBJ_TEXT; tx->prio = NOMIQ_PRIO_TEXT;
    tx->size = len; tx->frag_n = 1; tx->text = txt; tx->inline_text = true;
    return NOMIQ_OK;
}

bool nomiq_tx_done(const nomiq_tx_t *tx)
{
    return tx->state == NOMIQ_TX_DELIVERED || tx->state == NOMIQ_TX_FAILED ||
           tx->state == NOMIQ_TX_REJECTED || tx->state == NOMIQ_TX_CANCELLED || tx->state == NOMIQ_TX_IDLE;
}

bool nomiq_tx_wants_air(const nomiq_tx_t *tx, uint32_t now_ms)
{
    switch (tx->state) {
    case NOMIQ_TX_OFFER:
    case NOMIQ_TX_SEND:
        return true;
    case NOMIQ_TX_WAIT_ACCEPT:
    case NOMIQ_TX_WAIT_SACK:
    case NOMIQ_TX_WAIT_DONE:
        return time_reached(now_ms, tx->deadline_ms);
    default:
        return false;
    }
}

uint8_t nomiq_tx_progress_pct(const nomiq_tx_t *tx)
{
    if (tx->state == NOMIQ_TX_DELIVERED) return 100;
    if (!tx->frag_n) return 0;
    uint32_t p = (uint32_t)tx->acked_cnt * 100u / tx->frag_n;
    return (uint8_t)(p > 99 ? 99 : p);
}

static uint32_t tx_send_data(nomiq_tx_t *tx, uint16_t ix, uint8_t extra_flags, const nomiq_io_t *io)
{
    uint8_t buf[NOMIQ_MAX_PAYLOAD];
    uint32_t off; uint16_t len;
    if (nomiq_frag_span(tx->size, ix, &off, &len) != NOMIQ_OK) return 0;
    if (!io->read || io->read(tx->obj_ctx, off, buf, len) != 0) return 0;
    tx->frames_sent++;
    return emit(io, NOMIQ_T_DATA, (uint8_t)(tx->prio | extra_flags), tx->self, tx->peer,
                tx->msg_id, ix, tx->frag_n, buf, (uint8_t)len);
}

static uint32_t tx_send_offer(nomiq_tx_t *tx, const nomiq_io_t *io)
{
    uint8_t retx = tx->retries ? NOMIQ_F_RETX : 0;
    tx->frames_sent++;
    if (tx->inline_text) {
        return emit(io, NOMIQ_T_TEXT, (uint8_t)(NOMIQ_PRIO_TEXT | NOMIQ_F_ACK_REQ | retx), tx->self, tx->peer,
                    tx->msg_id, 0, 1, tx->text, (uint8_t)tx->size);
    }
    uint8_t pl[NOMIQ_OFFER_LEN];
    pl[0] = tx->obj_type;
    put32(&pl[1], tx->size);
    memcpy(&pl[5], tx->sha8, 8);
    pl[13] = 0; /* format/codec: filled by app layer in v1.1 */
    return emit(io, NOMIQ_T_OFFER, (uint8_t)(NOMIQ_PRIO_CONTROL | NOMIQ_F_ACK_REQ | retx), tx->self, tx->peer,
                tx->msg_id, 0, tx->frag_n, pl, sizeof pl);
}

int nomiq_tx_poll(nomiq_tx_t *tx, uint32_t now_ms, bool may_send, const nomiq_io_t *io)
{
    int sent = 0;
    switch (tx->state) {
    case NOMIQ_TX_OFFER:
        if (!may_send) break;
        tx->last_toa_ms = tx_send_offer(tx, io);
        tx->deadline_ms = now_ms + tx->last_toa_ms + reply_wait_ms(tx->retries, tx->last_toa_ms, tx->msg_id);
        tx->listen_until_ms = now_ms + 2u * tx->last_toa_ms + 100u;
        tx->state = NOMIQ_TX_WAIT_ACCEPT;
        sent = 1;
        break;

    case NOMIQ_TX_SEND: {
        if (!may_send) break;
        if (tx->win_pos >= tx->win_cnt) {           /* build the next window */
            tx->win_cnt = 0; tx->win_pos = 0;
            for (uint16_t ix = tx_first_unacked(tx); ix < tx->frag_n && tx->win_cnt < NOMIQ_WINDOW; ix++) {
                /* the SACK bitmap covers 64 frags from base, keep the window inside it */
                if (tx->win_cnt && ix - tx->win[0] >= 64) break;
                if (!tx_acked(tx, ix)) tx->win[tx->win_cnt++] = ix;
            }
            if (tx->win_cnt == 0) {
                tx->state = NOMIQ_TX_WAIT_DONE;
                tx->deadline_ms = now_ms + reply_wait_ms(tx->retries, tx->last_toa_ms ? tx->last_toa_ms : 400, tx->msg_id);
                break;
            }
        }
        uint16_t ix = tx->win[tx->win_pos++];
        if (tx_acked(tx, ix)) break;                 /* acked meanwhile (late SACK) */
        bool last = tx->win_pos >= tx->win_cnt;
        uint32_t toa = tx_send_data(tx, ix, last ? NOMIQ_F_ACK_REQ : 0, io);
        tx->last_toa_ms = toa;
        sent = 1;
        if (last) {
            tx->state = NOMIQ_TX_WAIT_SACK;
            tx->deadline_ms = now_ms + toa + reply_wait_ms(tx->retries, toa, tx->msg_id);
            tx->listen_until_ms = now_ms + 2u * toa + 100u;
        }
        break;
    }

    case NOMIQ_TX_WAIT_ACCEPT:
    case NOMIQ_TX_WAIT_SACK:
    case NOMIQ_TX_WAIT_DONE:
        if (!time_reached(now_ms, tx->deadline_ms)) break;
        if (tx->retries >= NOMIQ_MAX_RETRIES) { tx->state = NOMIQ_TX_FAILED; break; }
        if (!may_send) break;
        tx->retries++;
        /* First timeout: probe. Repeated timeouts: re-OFFER, which also recovers a receiver
         * that rebooted and lost its slot (it resumes from the persisted bitmap by sha8). */
        if (tx->state == NOMIQ_TX_WAIT_ACCEPT || tx->retries >= 2) {
            tx->state = NOMIQ_TX_OFFER;
            return nomiq_tx_poll(tx, now_ms, may_send, io);
        }
        /* probe: one frame with ACK_REQ to elicit SACK / DONE */
        {
            uint16_t ix = tx_first_unacked(tx);
            if (ix >= tx->frag_n) ix = (uint16_t)(tx->frag_n - 1);
            tx->last_toa_ms = tx_send_data(tx, ix, NOMIQ_F_ACK_REQ | NOMIQ_F_RETX, io);
            tx->deadline_ms = now_ms + tx->last_toa_ms + reply_wait_ms(tx->retries, tx->last_toa_ms, tx->msg_id);
            tx->listen_until_ms = now_ms + 2u * tx->last_toa_ms + 100u;
            sent = 1;
        }
        break;

    default:
        break;
    }
    return sent;
}

bool nomiq_tx_on_frame(nomiq_tx_t *tx, const nomiq_hdr_t *h, const uint8_t *pl, uint32_t now_ms)
{
    if (nomiq_tx_done(tx) || h->src != tx->peer || h->dst != tx->self || h->msg_id != tx->msg_id) return false;
    tx->listen_until_ms = now_ms;          /* reply arrived: radio may transmit again */
    tx->win_pos = tx->win_cnt;             /* any new ack info: rebuild window */

    switch (h->type) {
    case NOMIQ_T_ACK:
        if (tx->inline_text) { tx->state = NOMIQ_TX_DELIVERED; tx->acked_cnt = 1; }
        return true;

    case NOMIQ_T_ACCEPT:
        if (tx->state != NOMIQ_TX_WAIT_ACCEPT && tx->state != NOMIQ_TX_OFFER) return true;
        /* ACCEPT carries the receiver's full bitmap (resume) — authoritative */
        memset(tx->acked, 0, sizeof tx->acked);
        tx->acked_cnt = 0;
        for (uint16_t i = 0; i < tx->frag_n && (size_t)(i >> 3) < h->len; i++) {
            if (pl[i >> 3] & (1u << (i & 7))) tx_ack(tx, i);
        }
        tx->retries = 0;
        tx->state = NOMIQ_TX_SEND;
        return true;

    case NOMIQ_T_SACK: {
        if (h->len < NOMIQ_SACK_LEN) return true;
        if (h->frag_n != tx->frag_n) return true;
        uint64_t bm = (uint64_t)get32(pl) | ((uint64_t)get32(pl + 4) << 32);
        /* SACK is the receiver's authoritative snapshot: everything below base is held,
         * [base, base+64) exactly as the bitmap says (it may have discarded a corrupt object). */
        for (uint16_t i = 0; i < 64 && (uint32_t)h->frag_ix + i < tx->frag_n; i++) {
            uint16_t ix = (uint16_t)(h->frag_ix + i);
            if (bm & ((uint64_t)1 << i)) tx_ack(tx, ix);
            else if (tx_acked(tx, ix)) { tx->acked[ix >> 3] &= (uint8_t)~(1u << (ix & 7)); tx->acked_cnt--; }
        }
        for (uint16_t i = 0; i < h->frag_ix && i < tx->frag_n; i++) tx_ack(tx, i);
        tx->peer_snr = (int8_t)pl[8];
        tx->peer_rssi = (int8_t)pl[9];
        tx->retries = 0;
        if (tx->state == NOMIQ_TX_WAIT_SACK || tx->state == NOMIQ_TX_SEND || tx->state == NOMIQ_TX_WAIT_DONE) {
            tx->state = (tx->acked_cnt >= tx->frag_n) ? NOMIQ_TX_WAIT_DONE : NOMIQ_TX_SEND;
            if (tx->state == NOMIQ_TX_WAIT_DONE)
                tx->deadline_ms = now_ms + reply_wait_ms(0, tx->last_toa_ms, tx->msg_id);
        }
        return true;
    }

    case NOMIQ_T_DONE:
        tx->state = NOMIQ_TX_DELIVERED;
        tx->acked_cnt = tx->frag_n;
        return true;

    case NOMIQ_T_REJECT:
        tx->state = NOMIQ_TX_REJECTED;
        tx->reject_reason = h->len ? pl[0] : 0;
        return true;

    case NOMIQ_T_CANCEL:
        tx->state = NOMIQ_TX_CANCELLED;
        return true;

    default:
        return false;
    }
}

void nomiq_tx_cancel(nomiq_tx_t *tx, uint32_t now_ms, const nomiq_io_t *io)
{
    (void)now_ms;
    if (nomiq_tx_done(tx)) return;
    uint8_t reason = 0;
    emit(io, NOMIQ_T_CANCEL, NOMIQ_PRIO_CONTROL, tx->self, tx->peer, tx->msg_id, 0, tx->frag_n, &reason, 1);
    tx->state = NOMIQ_TX_CANCELLED;
}

/* =========================== Receiver =========================== */

void nomiq_rx_engine_init(nomiq_rx_t *rx, uint32_t self)
{
    memset(rx, 0, sizeof(*rx));
    rx->self = self;
    nomiq_dedup_init(&rx->dedup);
}

static bool done_has(const nomiq_rx_t *rx, uint32_t src, uint16_t msg_id)
{
    for (unsigned i = 0; i < NOMIQ_DONE_MEMORY; i++)
        if (rx->done[i].src == src && rx->done[i].msg_id == msg_id && src) return true;
    return false;
}

static void done_add(nomiq_rx_t *rx, uint32_t src, uint16_t msg_id)
{
    rx->done[rx->done_next].src = src;
    rx->done[rx->done_next].msg_id = msg_id;
    rx->done_next = (uint8_t)((rx->done_next + 1) % NOMIQ_DONE_MEMORY);
}

static nomiq_rx_slot_t *slot_by_msg(nomiq_rx_t *rx, uint32_t src, uint16_t msg_id)
{
    for (unsigned i = 0; i < NOMIQ_RX_SLOTS; i++)
        if (rx->slot[i].used && rx->slot[i].o.src == src && rx->slot[i].o.msg_id == msg_id) return &rx->slot[i];
    return NULL;
}

static nomiq_rx_slot_t *slot_by_sha(nomiq_rx_t *rx, uint32_t src, const uint8_t sha8[8])
{
    for (unsigned i = 0; i < NOMIQ_RX_SLOTS; i++)
        if (rx->slot[i].used && rx->slot[i].o.src == src && !memcmp(rx->slot[i].sha8, sha8, 8)) return &rx->slot[i];
    return NULL;
}

static nomiq_rx_slot_t *slot_free(nomiq_rx_t *rx)
{
    for (unsigned i = 0; i < NOMIQ_RX_SLOTS; i++) if (!rx->slot[i].used) return &rx->slot[i];
    return NULL;
}

static void send_simple(nomiq_rx_t *rx, const nomiq_io_t *io, uint8_t type, uint32_t dst, uint16_t msg_id,
                        uint16_t frag_ix, uint16_t frag_n, const uint8_t *pl, uint8_t len)
{
    emit(io, type, NOMIQ_PRIO_CONTROL, rx->self, dst, msg_id, frag_ix, frag_n, pl, len);
}

static void send_sack(nomiq_rx_t *rx, nomiq_rx_slot_t *s, const nomiq_io_t *io)
{
    uint16_t base = nomiq_rx_first_missing(&s->o);
    if (base >= s->o.frag_n) base = (uint16_t)(s->o.frag_n - 1);
    uint64_t bm = nomiq_rx_sack(&s->o, base);
    uint8_t pl[NOMIQ_SACK_LEN];
    put32(pl, (uint32_t)bm);
    put32(pl + 4, (uint32_t)(bm >> 32));
    pl[8] = (uint8_t)rx->last_snr;
    pl[9] = (uint8_t)rx->last_rssi;
    send_simple(rx, io, NOMIQ_T_SACK, s->o.src, s->o.msg_id, base, s->o.frag_n, pl, sizeof pl);
}

static void finalize(nomiq_rx_t *rx, nomiq_rx_slot_t *s, const nomiq_io_t *io)
{
    if (io->verify && !io->verify(&s->o, s->sha8, io->ctx)) {
        /* corrupt object: restart from scratch */
        nomiq_rx_init(&s->o, s->o.src, s->o.msg_id, s->o.size);
        send_sack(rx, s, io);
        return;
    }
    if (io->on_object) io->on_object(&s->o, s->obj_type, s->sha8, io->ctx);
    done_add(rx, s->o.src, s->o.msg_id);
    send_simple(rx, io, NOMIQ_T_DONE, s->o.src, s->o.msg_id, 0, s->o.frag_n, NULL, 0);
    s->used = false;
}

void nomiq_rx_on_frame(nomiq_rx_t *rx, const nomiq_hdr_t *h, const uint8_t *pl,
                       int8_t snr, int8_t rssi, uint32_t now_ms, const nomiq_io_t *io)
{
    if (h->dst != rx->self) return;
    rx->last_snr = snr;
    rx->last_rssi = rssi;

    switch (h->type) {
    case NOMIQ_T_TEXT:
        if (!nomiq_dedup_seen(&rx->dedup, h) && io->on_text) io->on_text(h->src, pl, h->len, io->ctx);
        send_simple(rx, io, NOMIQ_T_ACK, h->src, h->msg_id, 0, 1, NULL, 0);
        break;

    case NOMIQ_T_OFFER: {
        if (h->len < NOMIQ_OFFER_LEN) return;
        if (done_has(rx, h->src, h->msg_id)) {
            send_simple(rx, io, NOMIQ_T_DONE, h->src, h->msg_id, 0, h->frag_n, NULL, 0);
            return;
        }
        uint8_t obj_type = pl[0];
        uint32_t size = get32(&pl[1]);
        const uint8_t *sha8 = &pl[5];
        nomiq_rx_slot_t *s = slot_by_sha(rx, h->src, sha8);
        if (!s) {
            if (nomiq_frag_count(size) > NOMIQ_MAX_FRAGS || nomiq_frag_count(size) != h->frag_n) {
                uint8_t r = NOMIQ_REJ_TOO_BIG;
                send_simple(rx, io, NOMIQ_T_REJECT, h->src, h->msg_id, 0, h->frag_n, &r, 1);
                return;
            }
            s = slot_free(rx);
            if (!s) {
                uint8_t r = NOMIQ_REJ_BUSY;
                send_simple(rx, io, NOMIQ_T_REJECT, h->src, h->msg_id, 0, h->frag_n, &r, 1);
                return;
            }
            bool resumed = io->load_partial && io->load_partial(h->src, sha8, &s->o, io->ctx) &&
                           s->o.size == size;
            if (!resumed) {
                if (io->can_store && !io->can_store(h->src, size, io->ctx)) {
                    uint8_t r = NOMIQ_REJ_STORAGE_FULL;
                    send_simple(rx, io, NOMIQ_T_REJECT, h->src, h->msg_id, 0, h->frag_n, &r, 1);
                    return;
                }
                nomiq_rx_init(&s->o, h->src, h->msg_id, size);
            }
            s->used = true;
            s->obj_type = obj_type;
            memcpy(s->sha8, sha8, 8);
        }
        s->o.msg_id = h->msg_id;   /* sender may have restarted with a new msg_id */
        s->last_ms = now_ms;
        send_simple(rx, io, NOMIQ_T_ACCEPT, h->src, h->msg_id, 0, s->o.frag_n, s->o.bitmap,
                    (uint8_t)((s->o.frag_n + 7) / 8));
        break;
    }

    case NOMIQ_T_DATA: {
        nomiq_rx_slot_t *s = slot_by_msg(rx, h->src, h->msg_id);
        if (!s) {
            if (done_has(rx, h->src, h->msg_id))
                send_simple(rx, io, NOMIQ_T_DONE, h->src, h->msg_id, 0, h->frag_n, NULL, 0);
            return;
        }
        if (h->frag_n != s->o.frag_n) return;
        s->last_ms = now_ms;
        uint32_t off; uint16_t len;
        if (nomiq_frag_span(s->o.size, h->frag_ix, &off, &len) != NOMIQ_OK || len != h->len) return;
        if (!nomiq_rx_has(&s->o, h->frag_ix)) {
            if (io->write && io->write(&s->o, s->sha8, off, pl, len, io->ctx) != 0) return;
            nomiq_rx_mark(&s->o, h->frag_ix);
        }
        if (nomiq_rx_complete(&s->o)) {
            finalize(rx, s, io);
        } else if (h->flags & NOMIQ_F_ACK_REQ) {
            if (io->save_partial) io->save_partial(&s->o, s->sha8, s->obj_type, io->ctx);
            send_sack(rx, s, io);
        }
        break;
    }

    case NOMIQ_T_CANCEL: {
        nomiq_rx_slot_t *s = slot_by_msg(rx, h->src, h->msg_id);
        if (s) s->used = false;
        break;
    }

    default:
        break;
    }
}

void nomiq_rx_poll(nomiq_rx_t *rx, uint32_t now_ms)
{
    for (unsigned i = 0; i < NOMIQ_RX_SLOTS; i++) {
        nomiq_rx_slot_t *s = &rx->slot[i];
        if (s->used && time_reached(now_ms, s->last_ms + NOMIQ_RX_IDLE_MS)) s->used = false;
    }
}

/* =========================== Scheduler =========================== */

int nomiq_sched_pick(nomiq_tx_t *const txs[], int n, uint32_t now_ms)
{
    int best = -1;
    for (int i = 0; i < n; i++)
        if (txs[i] && !nomiq_tx_done(txs[i]) && !time_reached(now_ms, txs[i]->listen_until_ms)) return -1;
    for (int i = 0; i < n; i++) {
        if (!txs[i] || !nomiq_tx_wants_air(txs[i], now_ms)) continue;
        if (best < 0 || txs[i]->prio < txs[best]->prio) best = i;
    }
    return best;
}

/* =========================== Duty cycle =========================== */

void nomiq_duty_init(nomiq_duty_t *d, uint16_t permille)
{
    memset(d, 0, sizeof(*d));
    d->permille = permille;
    for (unsigned i = 0; i < 60; i++) d->bucket_start[i] = UINT32_MAX;
}

uint32_t nomiq_duty_used_ms(const nomiq_duty_t *d, uint32_t now_ms)
{
    uint32_t minute = now_ms / 60000u, used = 0;
    for (unsigned i = 0; i < 60; i++) {
        if (d->bucket_start[i] != UINT32_MAX && minute - d->bucket_start[i] < 60u) used += d->bucket_ms[i];
    }
    return used;
}

bool nomiq_duty_can_send(const nomiq_duty_t *d, uint32_t now_ms, uint32_t toa_ms)
{
    if (d->permille == 0) return true;
    uint32_t budget = 3600u * d->permille; /* ms per hour: 3 600 000 * permille / 1000 */
    return nomiq_duty_used_ms(d, now_ms) + toa_ms <= budget;
}

void nomiq_duty_record(nomiq_duty_t *d, uint32_t now_ms, uint32_t toa_ms)
{
    uint32_t minute = now_ms / 60000u;
    unsigned b = minute % 60u;
    if (d->bucket_start[b] != minute) { d->bucket_start[b] = minute; d->bucket_ms[b] = 0; }
    d->bucket_ms[b] += toa_ms;
}

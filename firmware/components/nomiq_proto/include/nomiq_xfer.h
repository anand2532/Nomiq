/*
 * nomiq_xfer — reliable object transfer engine (sender + receiver state machines),
 * priority scheduler and regulatory duty-cycle limiter.
 *
 * Pure C99, clock and I/O injected, so the whole engine runs in host tests with a
 * simulated lossy channel. Spec: docs/specs/communication-protocol.md §3, §5.
 */
#ifndef NOMIQ_XFER_H
#define NOMIQ_XFER_H

#include "nomiq_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOMIQ_OFFER_LEN       14u   /* obj_type u8, size u32, sha8[8], format u8 */
#define NOMIQ_SACK_LEN        10u   /* bitmap u64, snr i8, rssi i8 */
#define NOMIQ_MAX_RETRIES     8u
#define NOMIQ_RX_SLOTS        4u
#define NOMIQ_DONE_MEMORY     8u
#define NOMIQ_RX_IDLE_MS      (30u * 60u * 1000u)

typedef enum { NOMIQ_OBJ_TEXT = 1, NOMIQ_OBJ_VOICE = 2, NOMIQ_OBJ_PHOTO = 3 } nomiq_obj_t;

typedef enum {
    NOMIQ_REJ_STORAGE_FULL = 1,
    NOMIQ_REJ_TOO_BIG = 2,
    NOMIQ_REJ_UNKNOWN_SENDER = 3,
    NOMIQ_REJ_BUSY = 4,
} nomiq_reject_t;

/* ---------- I/O hooks ---------- */

typedef struct {
    /* Transmit one encoded frame (header+payload). Returns time-on-air in ms. */
    uint32_t (*send)(const uint8_t *frame, size_t len, void *ctx);
    /* Sender: read object bytes for a fragment. Returns 0 on success. */
    int (*read)(void *obj_ctx, uint32_t off, uint8_t *buf, uint16_t len);
    /* Receiver hooks */
    bool (*can_store)(uint32_t src, uint32_t size, void *ctx);
    int  (*write)(const nomiq_rx_obj_t *o, const uint8_t sha8[8], uint32_t off,
                  const uint8_t *data, uint16_t len, void *ctx);
    bool (*verify)(const nomiq_rx_obj_t *o, const uint8_t sha8[8], void *ctx);
    /* Persistence of partial receives for resume (may be NULL). */
    void (*save_partial)(const nomiq_rx_obj_t *o, const uint8_t sha8[8], uint8_t obj_type, void *ctx);
    bool (*load_partial)(uint32_t src, const uint8_t sha8[8], nomiq_rx_obj_t *o, void *ctx);
    /* Notifications (may be NULL) */
    void (*on_text)(uint32_t src, const uint8_t *txt, uint8_t len, void *ctx);
    void (*on_object)(const nomiq_rx_obj_t *o, uint8_t obj_type, const uint8_t sha8[8], void *ctx);
    void *ctx;
} nomiq_io_t;

/* ---------- Sender ---------- */

typedef enum {
    NOMIQ_TX_IDLE = 0,
    NOMIQ_TX_OFFER,       /* need to (re)send OFFER or TEXT */
    NOMIQ_TX_WAIT_ACCEPT, /* OFFER sent / TEXT sent, waiting */
    NOMIQ_TX_SEND,        /* ready to send next window */
    NOMIQ_TX_WAIT_SACK,
    NOMIQ_TX_WAIT_DONE,
    NOMIQ_TX_DELIVERED,
    NOMIQ_TX_FAILED,
    NOMIQ_TX_REJECTED,
    NOMIQ_TX_CANCELLED,
} nomiq_tx_state_t;

typedef struct {
    nomiq_tx_state_t state;
    uint32_t self, peer;
    uint16_t msg_id;
    uint8_t  obj_type;
    uint8_t  prio;
    uint32_t size;
    uint8_t  sha8[8];
    uint16_t frag_n;
    uint16_t acked_cnt;
    uint8_t  acked[NOMIQ_MAX_FRAGS / 8];
    uint8_t  retries;
    uint32_t deadline_ms;
    uint32_t last_toa_ms;
    uint8_t  reject_reason;
    int8_t   peer_snr;
    int8_t   peer_rssi;
    uint32_t frames_sent;
    uint32_t listen_until_ms;   /* keep the radio in RX for the expected reply */
    uint16_t win[NOMIQ_WINDOW]; /* current window (fragment indices) */
    uint8_t  win_cnt, win_pos;
    void    *obj_ctx;       /* passed to io->read */
    const uint8_t *text;    /* TEXT objects: inline payload */
} nomiq_tx_t;

int  nomiq_tx_begin(nomiq_tx_t *tx, uint32_t self, uint32_t peer, uint16_t msg_id, uint8_t obj_type,
                    uint32_t size, const uint8_t sha8[8], void *obj_ctx);
/* Text <= NOMIQ_MAX_PAYLOAD bytes: single TEXT frame + ACK. `txt` must stay valid. */
int  nomiq_tx_begin_text(nomiq_tx_t *tx, uint32_t self, uint32_t peer, uint16_t msg_id,
                         const uint8_t *txt, uint8_t len);
/* Drive timers / transmit if allowed. Returns number of frames sent. */
int  nomiq_tx_poll(nomiq_tx_t *tx, uint32_t now_ms, bool may_send, const nomiq_io_t *io);
/* Feed a received frame addressed to this sender. Returns true if consumed. */
bool nomiq_tx_on_frame(nomiq_tx_t *tx, const nomiq_hdr_t *h, const uint8_t *pl, uint32_t now_ms);
void nomiq_tx_cancel(nomiq_tx_t *tx, uint32_t now_ms, const nomiq_io_t *io);
bool nomiq_tx_done(const nomiq_tx_t *tx);         /* terminal state */
bool nomiq_tx_wants_air(const nomiq_tx_t *tx, uint32_t now_ms);
uint8_t nomiq_tx_progress_pct(const nomiq_tx_t *tx);

/* ---------- Receiver ---------- */

typedef struct {
    bool     used;
    uint8_t  obj_type;
    uint8_t  sha8[8];
    uint32_t last_ms;
    nomiq_rx_obj_t o;
} nomiq_rx_slot_t;

typedef struct {
    uint32_t src;
    uint16_t msg_id;
} nomiq_done_rec_t;

typedef struct {
    uint32_t self;
    nomiq_rx_slot_t slot[NOMIQ_RX_SLOTS];
    nomiq_done_rec_t done[NOMIQ_DONE_MEMORY];
    uint8_t  done_next;
    nomiq_dedup_t dedup;
    int8_t   last_snr;
    int8_t   last_rssi;
} nomiq_rx_t;

void nomiq_rx_engine_init(nomiq_rx_t *rx, uint32_t self);
/* Feed a received frame addressed to us (or broadcast). Sends replies through io. */
void nomiq_rx_on_frame(nomiq_rx_t *rx, const nomiq_hdr_t *h, const uint8_t *pl,
                       int8_t snr, int8_t rssi, uint32_t now_ms, const nomiq_io_t *io);
/* Expire idle partial transfers (kept persisted for resume). */
void nomiq_rx_poll(nomiq_rx_t *rx, uint32_t now_ms);

/* ---------- Priority scheduler ---------- */

/* Index of the transfer that may use the air next (lowest prio value wins, then lowest
 * index), or -1. Transfers send one frame per poll, so a text can pre-empt a bulk window
 * between frames. Returns -1 while any transfer is listening for an expected reply
 * (half-duplex radio). */
int nomiq_sched_pick(nomiq_tx_t *const txs[], int n, uint32_t now_ms);

/* ---------- Duty-cycle limiter (rolling 1 h, 60 x 1 min buckets) ---------- */

typedef struct {
    uint16_t permille;          /* 0 = unlimited; 25 = 2.5 % */
    uint32_t bucket_ms[60];
    uint32_t bucket_start[60];  /* minute index the bucket belongs to */
} nomiq_duty_t;

void     nomiq_duty_init(nomiq_duty_t *d, uint16_t permille);
uint32_t nomiq_duty_used_ms(const nomiq_duty_t *d, uint32_t now_ms);
bool     nomiq_duty_can_send(const nomiq_duty_t *d, uint32_t now_ms, uint32_t toa_ms);
void     nomiq_duty_record(nomiq_duty_t *d, uint32_t now_ms, uint32_t toa_ms);

#ifdef __cplusplus
}
#endif
#endif /* NOMIQ_XFER_H */

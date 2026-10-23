/* msg_store.c — see include/msg_store.h */
#include "msg_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define REC_MAGIC   0x4E51u   /* "NQ" */
#define REC_VER     1u
#define REC_SIZE    64u
#define OP_PUT      1u
#define OP_STATE    2u


/* ---------- helpers ---------- */

static uint16_t crc16(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

static void mkdirs(const char *path)
{
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0775); *p = '/'; }
    }
    mkdir(tmp, 0775);
}

static void join(msg_store_t *S, char *out, size_t n, const char *a) { snprintf(out, n, "%s/%s", S->root, a); }

/* record layout: magic2 ver1 op1 | seq4 peer4 msg_id2 kind1 state1 out1 sha8 size4 ts4 | pad | crc2 @62 */
static void encode(uint8_t b[REC_SIZE], uint8_t op, const msg_rec_t *r)
{
    memset(b, 0, REC_SIZE);
    put16(b, REC_MAGIC); b[2] = REC_VER; b[3] = op;
    put32(b + 4, r->seq); put32(b + 8, r->peer); put16(b + 12, r->msg_id);
    b[14] = r->kind; b[15] = r->state; b[16] = r->outgoing;
    memcpy(b + 17, r->sha8, 8);
    put32(b + 25, r->size); put32(b + 29, r->ts);
    put16(b + 62, crc16(b, 62));
}

static bool decode(const uint8_t b[REC_SIZE], uint8_t *op, msg_rec_t *r)
{
    if (get16(b) != REC_MAGIC || b[2] != REC_VER || get16(b + 62) != crc16(b, 62)) return false;
    *op = b[3];
    r->seq = get32(b + 4); r->peer = get32(b + 8); r->msg_id = get16(b + 12);
    r->kind = b[14]; r->state = b[15]; r->outgoing = b[16];
    memcpy(r->sha8, b + 17, 8);
    r->size = get32(b + 25); r->ts = get32(b + 29);
    return true;
}

static msg_rec_t *find(msg_store_t *S, uint32_t seq)
{
    for (int i = 0; i < S->n; i++) if (S->recs[i].seq == seq) return &S->recs[i];
    return NULL;
}

static int append(msg_store_t *S, uint8_t op, const msg_rec_t *r)
{
    uint8_t b[REC_SIZE];
    encode(b, op, r);
    if (fwrite(b, 1, REC_SIZE, S->log) != REC_SIZE) return -EIO;
    if (fflush(S->log) != 0) return -EIO;
    fsync(fileno(S->log));
    S->log_records++;
    return 0;
}

static void apply(msg_store_t *S, uint8_t op, const msg_rec_t *r)
{
    msg_rec_t *e = find(S, r->seq);
    if (op == OP_PUT) {
        if (!e) {
            if (S->n >= MSG_STORE_MAX) {          /* evict oldest record from RAM index */
                memmove(&S->recs[0], &S->recs[1], sizeof(msg_rec_t) * (size_t)(S->n - 1));
                S->n--;
            }
            e = &S->recs[S->n++];
        }
        *e = *r;
    } else if (op == OP_STATE && e) {
        e->state = r->state;
    }
    if (r->seq >= S->next_seq) S->next_seq = r->seq + 1;
}

/* ---------- lifecycle ---------- */

int msg_store_init(msg_store_t *S, const char *root, msg_store_space_fn space)
{
    msg_store_deinit(S);
    snprintf(S->root, sizeof S->root, "%s", root);
    S->space = space;
    S->recs = calloc(MSG_STORE_MAX, sizeof(msg_rec_t));
    if (!S->recs) return -ENOMEM;
    S->n = 0; S->next_seq = 1; S->log_records = 0;

    char p[160];
    mkdirs(S->root);
    join(S, p, sizeof p, "in"); mkdirs(p);
    join(S, p, sizeof p, "out"); mkdirs(p);
    join(S, p, sizeof p, "partial"); mkdirs(p);

    join(S, p, sizeof p, "index.log");
    long good_end = 0;
    FILE *f = fopen(p, "rb");
    if (f) {
        uint8_t b[REC_SIZE];
        while (fread(b, 1, REC_SIZE, f) == REC_SIZE) {
            uint8_t op; msg_rec_t r;
            if (!decode(b, &op, &r)) break;           /* torn/corrupt tail: stop here */
            apply(S, op, &r);
            S->log_records++;
            good_end = ftell(f);
        }
        fclose(f);
        if (truncate(p, good_end) != 0) { /* best effort */ }
    }
    S->log = fopen(p, "ab");
    if (!S->log) return -errno;

    /* crash recovery: anything mid-send resumes as PENDING */
    for (int i = 0; i < S->n; i++) {
        if (S->recs[i].state == MS_SENDING) {
            S->recs[i].state = MS_PENDING;
            append(S, OP_STATE, &S->recs[i]);
        }
    }
    if (S->log_records > 4u * (uint32_t)S->n + 64u) msg_store_compact(S);
    return 0;
}

void msg_store_deinit(msg_store_t *S)
{
    if (S->log) fclose(S->log);
    S->log = NULL;
    free(S->recs);
    S->recs = NULL;
    S->n = 0;
}

/* ---------- records ---------- */

int msg_store_put(msg_store_t *S, msg_rec_t *r)
{
    if (!S->log || !r) return -EINVAL;
    r->seq = S->next_seq++;
    int rc = append(S, OP_PUT, r);
    if (rc == 0) apply(S, OP_PUT, r);
    return rc;
}

int msg_store_set_state(msg_store_t *S, uint32_t seq, msg_state_t st)
{
    msg_rec_t *e = find(S, seq);
    if (!e) return -ENOENT;
    msg_rec_t tmp = *e;
    tmp.state = (uint8_t)st;
    int rc = append(S, OP_STATE, &tmp);
    if (rc == 0) e->state = (uint8_t)st;
    if (!S->hold_compact && S->log_records > 4u * (uint32_t)S->n + 64u) msg_store_compact(S);
    return rc;
}

const msg_rec_t *msg_store_get(msg_store_t *S, uint32_t seq) { return find(S, seq); }
int msg_store_count(msg_store_t *S) { return S->n; }

int msg_store_list(msg_store_t *S, msg_rec_t *out, int max, int offset, uint8_t kind, uint32_t peer)
{
    int k = 0, skipped = 0;
    for (int i = S->n - 1; i >= 0 && k < max; i--) {
        const msg_rec_t *r = &S->recs[i];
        if (r->state == MS_DELETED) continue;
        if (kind && r->kind != kind) continue;
        if (peer && r->peer != peer) continue;
        if (skipped++ < offset) continue;
        out[k++] = *r;
    }
    return k;
}

int msg_store_pending(msg_store_t *S, msg_rec_t *out, int max)
{
    int k = 0;
    for (int i = 0; i < S->n && k < max; i++) {
        const msg_rec_t *r = &S->recs[i];
        if (r->outgoing && (r->state == MS_PENDING || r->state == MS_SENDING || r->state == MS_PAUSED))
            out[k++] = *r;
    }
    return k;
}

int msg_store_path(msg_store_t *S, const msg_rec_t *r, char *buf, size_t len)
{
    static const char *ext[] = { "bin", "txt", "c2", "jpg" };
    const char *e = r->kind <= MSG_PHOTO ? ext[r->kind] : ext[0];
    int n = snprintf(buf, len, "%s/%s/%08x/%u.%s", S->root, r->outgoing ? "out" : "in",
                     (unsigned)r->peer, (unsigned)r->seq, e);
    if (n < 0 || (size_t)n >= len) return -ENAMETOOLONG;
    /* ensure the peer directory exists */
    char dir[160];
    snprintf(dir, sizeof dir, "%s/%s/%08x", S->root, r->outgoing ? "out" : "in", (unsigned)r->peer);
    mkdirs(dir);
    return 0;
}

/* ---------- blobs ---------- */

static void blob_path(msg_store_t *S, char *p, size_t n, const char *name) { snprintf(p, n, "%s/partial/%s.bin", S->root, name); }

int msg_store_blob_save(msg_store_t *S, const char *name, const void *data, size_t len)
{
    char p[160], t[168];
    blob_path(S, p, sizeof p, name);
    snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb");
    if (!f) return -errno;
    bool ok = fwrite(data, 1, len, f) == len && fflush(f) == 0;
    fsync(fileno(f));
    fclose(f);
    if (!ok) { remove(t); return -EIO; }
    return rename(t, p) == 0 ? 0 : -errno;   /* atomic replace */
}

int msg_store_blob_load(msg_store_t *S, const char *name, void *data, size_t len)
{
    char p[160];
    blob_path(S, p, sizeof p, name);
    FILE *f = fopen(p, "rb");
    if (!f) return -ENOENT;
    size_t n = fread(data, 1, len, f);
    fclose(f);
    return n == len ? 0 : -EIO;
}

int msg_store_blob_delete(msg_store_t *S, const char *name)
{
    char p[160];
    blob_path(S, p, sizeof p, name);
    return remove(p) == 0 ? 0 : -errno;
}

/* ---------- space policy ---------- */

store_policy_t msg_store_policy(msg_store_t *S)
{
    uint64_t fr = 0, tot = 0;
    if (!S->space || S->space(S->root, &fr, &tot) != 0 || tot == 0) return STORE_OK;
    if (fr * 100u < tot * MSG_STORE_BULK_PCT) return STORE_REJECT_BULK;
    if (fr * 100u < tot * MSG_STORE_WARN_PCT) return STORE_WARN_LOW;
    return STORE_OK;
}

uint64_t msg_store_prune_media(msg_store_t *S, uint64_t need_bytes)
{
    uint64_t freed = 0;
    S->hold_compact = true;
    for (int i = 0; i < S->n && freed < need_bytes; i++) {
        msg_rec_t *r = &S->recs[i];
        if (r->kind == MSG_TEXT || r->state == MS_DELETED) continue;
        bool finished = r->state == MS_DELIVERED || r->state == MS_RECEIVED || r->state == MS_READ ||
                        r->state == MS_CANCELLED || r->state == MS_FAILED;
        if (!finished) continue;
        char p[160];
        if (msg_store_path(S, r, p, sizeof p) == 0) {
            struct stat st;
            if (stat(p, &st) == 0) { freed += (uint64_t)st.st_size; remove(p); }
        }
        msg_store_set_state(S, r->seq, MS_DELETED);
    }
    S->hold_compact = false;
    if (S->log_records > 4u * (uint32_t)S->n + 64u) msg_store_compact(S);
    return freed;
}

/* ---------- compaction ---------- */

int msg_store_compact(msg_store_t *S)
{
    char p[160], t[168];
    join(S, p, sizeof p, "index.log");
    snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb");
    if (!f) return -errno;
    uint32_t written = 0;
    for (int i = 0; i < S->n; i++) {
        uint8_t b[REC_SIZE];
        if (S->recs[i].state == MS_DELETED) continue;
        encode(b, OP_PUT, &S->recs[i]);
        if (fwrite(b, 1, REC_SIZE, f) != REC_SIZE) { fclose(f); remove(t); return -EIO; }
        written++;
    }
    fflush(f); fsync(fileno(f)); fclose(f);
    if (S->log) fclose(S->log);
    if (rename(t, p) != 0) { S->log = fopen(p, "ab"); return -errno; }
    S->log = fopen(p, "ab");
    /* drop deleted from RAM too */
    int k = 0;
    for (int i = 0; i < S->n; i++) if (S->recs[i].state != MS_DELETED) S->recs[k++] = S->recs[i];
    S->n = k;
    S->log_records = written;
    return S->log ? 0 : -EIO;
}

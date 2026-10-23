/* Host tests for msg_store: persistence, crash recovery, torn writes, compaction, space policy. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "msg_store.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static char root[256];
static msg_store_t ST;
static uint64_t fake_free = 50, fake_total = 100;
static int space(const char *r, uint64_t *f, uint64_t *t) { (void)r; *f = fake_free; *t = fake_total; return 0; }

static msg_rec_t mk(uint32_t peer, uint8_t kind, uint8_t st, uint8_t out, uint32_t size)
{
    msg_rec_t r; memset(&r, 0, sizeof r);
    r.peer = peer; r.kind = kind; r.state = st; r.outgoing = out; r.size = size; r.ts = 1000;
    for (int i = 0; i < 8; i++) r.sha8[i] = (uint8_t)(size + i);
    return r;
}

static long file_size(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

int main(void)
{
    snprintf(root, sizeof root, "/tmp/msgstore_test_%d", (int)getpid());
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", root); system(cmd);

    CHECK(msg_store_init(&ST, root, space) == 0);
    msg_rec_t a = mk(0xA, MSG_TEXT, MS_RECEIVED, 0, 20);
    msg_rec_t b = mk(0xB, MSG_PHOTO, MS_SENDING, 1, 12000);
    msg_rec_t c = mk(0xA, MSG_VOICE, MS_PENDING, 1, 4500);
    CHECK(msg_store_put(&ST, &a) == 0 && msg_store_put(&ST, &b) == 0 && msg_store_put(&ST, &c) == 0);
    CHECK(a.seq == 1 && b.seq == 2 && c.seq == 3);
    CHECK(msg_store_set_state(&ST, a.seq, MS_READ) == 0);
    CHECK(msg_store_set_state(&ST, 99, MS_READ) < 0);

    msg_rec_t l[8];
    CHECK(msg_store_list(&ST, l, 8, 0, 0, 0) == 3 && l[0].seq == 3);          /* newest first */
    CHECK(msg_store_list(&ST, l, 8, 0, MSG_PHOTO, 0) == 1 && l[0].seq == 2);
    CHECK(msg_store_list(&ST, l, 8, 0, 0, 0xA) == 2);
    CHECK(msg_store_list(&ST, l, 1, 1, 0, 0) == 1 && l[0].seq == 2);          /* paging */

    /* write a media file for b */
    char p[256];
    CHECK(msg_store_path(&ST, &b, p, sizeof p) == 0);
    CHECK(strstr(p, "/out/0000000b/2.jpg") != NULL);
    FILE *f = fopen(p, "wb"); for (int i = 0; i < 12000; i++) fputc(i, f); fclose(f);

    /* reboot: SENDING -> PENDING, state persisted */
    msg_store_deinit(&ST);
    CHECK(msg_store_init(&ST, root, space) == 0);
    CHECK(msg_store_count(&ST) == 3);
    CHECK(msg_store_get(&ST, 1)->state == MS_READ);
    CHECK(msg_store_get(&ST, 2)->state == MS_PENDING);
    msg_rec_t pend[8];
    CHECK(msg_store_pending(&ST, pend, 8) == 2 && pend[0].seq == 2 && pend[1].seq == 3);
    msg_rec_t d = mk(0xC, MSG_TEXT, MS_PENDING, 1, 5);
    CHECK(msg_store_put(&ST, &d) == 0 && d.seq == 4);                          /* seq continues */

    /* torn write: append half a record, plus garbage — must be ignored and truncated */
    msg_store_deinit(&ST);
    char logp[300]; snprintf(logp, sizeof logp, "%s/index.log", root);
    long before = file_size(logp);
    f = fopen(logp, "ab"); fwrite("NQ\x01\x01garbage-torn-record", 1, 24, f); fclose(f);
    CHECK(msg_store_init(&ST, root, space) == 0);
    CHECK(msg_store_count(&ST) == 4);
    CHECK(file_size(logp) == before);                                     /* tail truncated */
    msg_rec_t e = mk(0xD, MSG_TEXT, MS_RECEIVED, 0, 7);
    CHECK(msg_store_put(&ST, &e) == 0 && e.seq == 5);
    msg_store_deinit(&ST);
    CHECK(msg_store_init(&ST, root, space) == 0 && msg_store_count(&ST) == 5);

    /* corrupt a record in the middle: everything from it on is dropped (log is append-only) */
    /* (not done here: would discard valid later data — covered by the torn-tail case) */

    /* blobs: atomic save/load */
    uint8_t bm[64], bm2[64]; for (int i = 0; i < 64; i++) bm[i] = (uint8_t)(i * 3);
    CHECK(msg_store_blob_save(&ST, "0000000a_0102030405060708", bm, sizeof bm) == 0);
    CHECK(msg_store_blob_load(&ST, "0000000a_0102030405060708", bm2, sizeof bm2) == 0 && !memcmp(bm, bm2, 64));
    CHECK(msg_store_blob_delete(&ST, "0000000a_0102030405060708") == 0);
    CHECK(msg_store_blob_load(&ST, "0000000a_0102030405060708", bm2, sizeof bm2) < 0);

    /* space policy */
    fake_free = 50; CHECK(msg_store_policy(&ST) == STORE_OK);
    fake_free = 4;  CHECK(msg_store_policy(&ST) == STORE_WARN_LOW);
    fake_free = 0;  CHECK(msg_store_policy(&ST) == STORE_REJECT_BULK);

    /* prune: only finished media is deleted (b is PENDING -> kept) */
    CHECK(msg_store_prune_media(&ST, 1) == 0);
    CHECK(msg_store_set_state(&ST, 2, MS_DELIVERED) == 0);
    CHECK(msg_store_prune_media(&ST, 1) == 12000);
    CHECK(msg_store_get(&ST, 2)->state == MS_DELETED);
    CHECK(access(p, F_OK) != 0);
    CHECK(msg_store_list(&ST, l, 8, 0, MSG_PHOTO, 0) == 0);

    /* compaction keeps live data, drops deleted, shrinks the log */
    for (int i = 0; i < 300; i++) msg_store_set_state(&ST, 1, (i & 1) ? MS_READ : MS_RECEIVED);
    long big = file_size(logp);
    CHECK(msg_store_compact(&ST) == 0);
    CHECK(file_size(logp) < big && msg_store_count(&ST) == 4);
    msg_store_deinit(&ST);
    CHECK(msg_store_init(&ST, root, space) == 0 && msg_store_count(&ST) == 4);
    CHECK(msg_store_get(&ST, 2) == NULL && msg_store_get(&ST, 5) != NULL);
    msg_rec_t g = mk(0xE, MSG_TEXT, MS_PENDING, 1, 1);
    CHECK(msg_store_put(&ST, &g) == 0 && g.seq == 6);

    /* capacity: more than MSG_STORE_MAX records keeps newest in RAM */
    for (int i = 0; i < MSG_STORE_MAX + 10; i++) { msg_rec_t x = mk(0xF, MSG_TEXT, MS_READ, 0, 1); msg_store_put(&ST, &x); }
    CHECK(msg_store_count(&ST) == MSG_STORE_MAX);
    CHECK(msg_store_list(&ST, l, 1, 0, 0, 0) == 1 && l[0].seq == (uint32_t)(6 + MSG_STORE_MAX + 10));

    msg_store_deinit(&ST);
    system(cmd);
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

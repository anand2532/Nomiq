/*
 * msg_store — local message history on microSD (REQ-STO-001..004, REQ-SYS-006).
 *
 * Layout under <root>/ (root = "/sd/nomiq" on the device):
 *   index.log                  append-only log of fixed 64-byte CRC-protected records
 *   out/<peer>/<seq>.<ext>     sent media / text      in/<peer>/<seq>.<ext>  received
 *   partial/<name>.bin         resumable receive state (opaque blobs)
 *
 * Crash safety: every state change is one appended record + fflush/fsync before the
 * caller touches the radio. A torn or corrupt tail record (power loss) is ignored on
 * load. On load, SENDING becomes PENDING so transfers resume after reboot.
 *
 * Pure C + stdio: runs on ESP-IDF (FATFS via VFS) and on the host for unit tests.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSG_STORE_MAX       1024   /* live records kept in RAM (PSRAM on device) */
#define MSG_STORE_WARN_PCT  5
#define MSG_STORE_BULK_PCT  1

typedef enum { MSG_TEXT = 1, MSG_VOICE = 2, MSG_PHOTO = 3 } msg_kind_t;
typedef enum {
    MS_PENDING = 0, MS_SENDING, MS_PAUSED, MS_DELIVERED, MS_FAILED, MS_CANCELLED,
    MS_PARTIAL, MS_RECEIVED, MS_READ, MS_DELETED
} msg_state_t;

typedef struct {
    uint32_t seq;        /* local unique id, assigned by msg_store_put */
    uint32_t peer;
    uint16_t msg_id;     /* air-protocol id */
    uint8_t  kind;       /* msg_kind_t */
    uint8_t  state;      /* msg_state_t */
    uint8_t  outgoing;
    uint8_t  sha8[8];
    uint32_t size;
    uint32_t ts;         /* seconds (RTC or uptime-based) */
} msg_rec_t;

typedef enum { STORE_OK = 0, STORE_WARN_LOW = 1, STORE_REJECT_BULK = 2 } store_policy_t;

/* Free/total bytes provider (statvfs / esp_vfs_fat_info). Injected for tests. */
typedef int (*msg_store_space_fn)(const char *root, uint64_t *free_b, uint64_t *total_b);

int  msg_store_init(const char *root, msg_store_space_fn space);
void msg_store_deinit(void);

/* Adds a record (seq assigned and returned in r->seq). Returns 0 or -errno. */
int  msg_store_put(msg_rec_t *r);
int  msg_store_set_state(uint32_t seq, msg_state_t st);
const msg_rec_t *msg_store_get(uint32_t seq);
/* Newest first; skips DELETED. Optional filter: kind 0 = all, peer 0 = all. */
int  msg_store_list(msg_rec_t *out, int max, int offset, uint8_t kind, uint32_t peer);
/* Outgoing records that still need the radio (PENDING/SENDING/PAUSED), oldest first. */
int  msg_store_pending(msg_rec_t *out, int max);
int  msg_store_count(void);

/* Content file path for a record ("<root>/in/0000abcd/42.jpg"). */
int  msg_store_path(const msg_rec_t *r, char *buf, size_t len);

/* Opaque blobs (resume bitmaps etc.) */
int  msg_store_blob_save(const char *name, const void *data, size_t len);
int  msg_store_blob_load(const char *name, void *data, size_t len);
int  msg_store_blob_delete(const char *name);

/* Storage-full policy (REQ-STO-004). */
store_policy_t msg_store_policy(void);
/* Delete oldest finished media (voice/photo) until `need_bytes` freed. Returns bytes freed. */
uint64_t msg_store_prune_media(uint64_t need_bytes);

/* Rewrite index.log with live records only (also called automatically). */
int  msg_store_compact(void);

#ifdef __cplusplus
}
#endif

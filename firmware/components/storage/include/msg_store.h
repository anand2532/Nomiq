/* Message store on microSD: /sd/nomiq/{index.db, out/, in/, partial/}. Phase 6. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef enum { MSG_TEXT = 1, MSG_VOICE = 2, MSG_PHOTO = 3 } msg_kind_t;
typedef enum { MS_PENDING, MS_SENDING, MS_PAUSED, MS_DELIVERED, MS_FAILED, MS_CANCELLED,
               MS_PARTIAL, MS_RECEIVED, MS_READ } msg_state_t;
typedef struct {
    uint32_t peer; uint16_t msg_id; msg_kind_t kind; msg_state_t state;
    uint32_t size; uint32_t ts; bool outgoing; char path[64]; uint8_t sha8[8];
} msg_rec_t;
esp_err_t msg_store_init(void);
esp_err_t msg_store_put(const msg_rec_t *r);
esp_err_t msg_store_update_state(uint32_t peer, uint16_t msg_id, bool outgoing, msg_state_t st);
int       msg_store_list(msg_rec_t *out, int max, int offset);   /* newest first */
esp_err_t msg_store_save_bitmap(uint32_t peer, uint16_t msg_id, const uint8_t *bm, size_t len);
uint64_t  msg_store_free_bytes(void);

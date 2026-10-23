/* Audio: I2S mic capture, I2S amp playback, Codec2 encode/decode. Phase 2/4. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#define AUDIO_SAMPLE_RATE 16000   /* capture; Codec2 path resamples to 8 kHz */
typedef enum { AUDIO_CODEC2_700C, AUDIO_CODEC2_1200, AUDIO_CODEC2_2400, AUDIO_CODEC2_3200 } audio_codec_t;
esp_err_t audio_init(void);
esp_err_t audio_record_start(const char *pcm_path, uint32_t max_ms);   /* PTT pressed */
esp_err_t audio_record_stop(uint32_t *duration_ms);                    /* PTT released */
esp_err_t audio_encode(const char *pcm_path, const char *c2_path, audio_codec_t codec);
esp_err_t audio_play(const char *path);   /* .pcm or .c2 */
esp_err_t audio_stop(void);

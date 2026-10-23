/* Audio: I2S MEMS mic capture + I2S class-D playback (16 kHz mono), Codec2 voice notes. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "voice_codec.h"

#define AUDIO_SAMPLE_RATE 16000

esp_err_t audio_init(void);
/* Start recording raw 16 kHz s16 PCM to `pcm_path` (stops by itself after max_ms). */
esp_err_t audio_record_start(const char *pcm_path, uint32_t max_ms);
/* Stop recording; returns the recorded duration. Blocks until the file is closed. */
esp_err_t audio_record_stop(uint32_t *duration_ms);
bool      audio_is_recording(void);
/* Encode a recording into a Codec2 voice note (.c2). */
esp_err_t audio_encode(const char *pcm_path, const char *c2_path, vc_mode_t mode);
/* Play a .pcm (16 kHz s16) or .c2 file asynchronously. */
esp_err_t audio_play(const char *path);
esp_err_t audio_stop(void);
bool      audio_is_playing(void);

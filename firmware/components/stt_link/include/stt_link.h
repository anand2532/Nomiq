/* Link to the offline STT co-processor (UART RPC + load switch). Phase 3.
 * Wire format (UART, 921600 8N1): [0xA5][type u8][len u32 LE][payload][crc16]
 *   ESP->STT: HELLO, AUDIO_BEGIN{rate,lang}, AUDIO_CHUNK{pcm}, AUDIO_END
 *   STT->ESP: READY{model,ver}, PARTIAL{utf8}, FINAL{utf8,confidence}, ERROR{code}
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { char text[512]; float confidence; uint32_t latency_ms; } stt_result_t;
esp_err_t stt_power_on(uint32_t boot_timeout_ms);
esp_err_t stt_transcribe_file(const char *pcm_path, const char *lang, stt_result_t *out);
esp_err_t stt_power_off(void);

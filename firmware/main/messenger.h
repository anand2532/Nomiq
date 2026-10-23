#pragma once
#include "esp_err.h"
/* Full device application (display, buttons, audio, camera, STT, messaging). */
esp_err_t messenger_start(void);
/* Phase-1 radio link test (no display needed): periodic pings + CSV range log. */
void link_test_start(void);

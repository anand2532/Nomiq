/*
 * Nomiq firmware entry point (skeleton).
 * Task layout and event flow: docs/01-system-architecture.md §4
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "board_pins.h"
#include "nomiq_proto.h"

static const char *TAG = "nomiq";

void app_main(void)
{
    ESP_LOGI(TAG, "Nomiq boot — proto v%u, max payload %u B", NOMIQ_PROTO_VERSION, NOMIQ_MAX_PAYLOAD);

    /* Bring-up order (each step implemented in its phase):
     * 1. power_init()       — fuel gauge, keep STT/CAM rails off
     * 2. msg_store_init()   — mount microSD, recover PENDING/PARTIAL
     * 3. ui_init()          — LCD + LVGL + expander buttons
     * 4. radio_init()       — SX1262, regulatory profile, start RX
     * 5. start tasks: app_core, tx_scheduler, rx_dispatch, ui, audio
     */

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* Nomiq firmware entry point. Mode is chosen in menuconfig: Nomiq application -> App mode. */
#include "board.h"
#include "esp_log.h"
#include "messenger.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(board_init());
#if CONFIG_NOMIQ_APP_LINK_TEST
    link_test_start();
#else
    ESP_ERROR_CHECK(messenger_start());
#endif
}

idf_component_register(SRCS "main.c" "linktest.c" "messenger.c" INCLUDE_DIRS "."
    REQUIRES nomiq_proto board radio_sx1262 app_core storage ui audio camera power stt_link
             nvs_flash esp_timer fatfs sdmmc driver)

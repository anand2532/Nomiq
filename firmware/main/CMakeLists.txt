idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES nomiq_proto board radio_sx1262 nvs_flash esp_timer)

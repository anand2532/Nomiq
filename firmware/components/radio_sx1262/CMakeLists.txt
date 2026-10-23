idf_component_register(
    SRCS "sx126x.c" "radio_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES board nomiq_proto
    PRIV_REQUIRES driver esp_timer esp_rom)

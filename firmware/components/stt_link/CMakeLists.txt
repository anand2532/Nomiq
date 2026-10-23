idf_component_register(SRCS "stt_proto.c" "stt_link_esp.c" INCLUDE_DIRS "include"
    REQUIRES board power PRIV_REQUIRES driver esp_timer freertos)

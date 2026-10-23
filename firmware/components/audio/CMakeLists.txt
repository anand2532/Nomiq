idf_component_register(
    SRCS "voice_codec.c" "audio_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES codec2 board
    PRIV_REQUIRES driver freertos)

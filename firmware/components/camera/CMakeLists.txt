idf_component_register(
    SRCS "image_fit.c" "camera_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES board
    PRIV_REQUIRES esp32-camera freertos)

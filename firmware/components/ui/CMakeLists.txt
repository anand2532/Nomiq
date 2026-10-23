idf_component_register(
    SRCS "ui_model.c" "ui_view.c" "buttons.c" "display_lvgl.c"
    INCLUDE_DIRS "include"
    REQUIRES board
    PRIV_REQUIRES driver esp_lcd freertos)

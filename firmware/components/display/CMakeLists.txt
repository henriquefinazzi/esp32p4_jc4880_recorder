idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES board driver esp_lcd espressif__esp_lvgl_port lvgl__lvgl
    PRIV_REQUIRES espressif__esp_lcd_touch_gt911 esp_hw_support
)

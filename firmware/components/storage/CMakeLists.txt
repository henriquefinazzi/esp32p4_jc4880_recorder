idf_component_register(
    SRCS "storage.c"
    INCLUDE_DIRS "include"
    REQUIRES driver sdmmc fatfs vfs
    PRIV_REQUIRES espressif__esp_tinyusb espressif__tinyusb
)

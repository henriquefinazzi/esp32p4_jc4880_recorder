idf_component_register(
    SRCS "main.c" "ui.c" "web.c" "config.c" "logbuf.c" "logo.c" "updater.c"
    INCLUDE_DIRS "."
    REQUIRES settings_file i18n rec_writer audio_dsp storage audio_in net display fonts i18n driver esp_timer nvs_flash fatfs
             esp_http_server app_update mbedtls espressif__mdns esp_http_client esp_https_ota json fwversion
)

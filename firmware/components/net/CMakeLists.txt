# esp_wifi stays private: esp_hosted exports an "-include" option that PlatformIO
# breaks when reordering the compiler arguments of components depending on it publicly.
idf_component_register(
    SRCS "net.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi nvs_flash esp_netif esp_event lwip espressif__esp_hosted esp_system bootloader_support esp_app_format
    # Wi-Fi chip (ESP32-C6) firmware, so the device can update it (see firmware/c6/README.md).
    EMBED_FILES "../../c6/network_adapter_esp32c6.bin"
)

# esp_wifi fica privado: o esp_hosted exporta uma opção "-include" que o PlatformIO
# quebra ao reordenar os argumentos do compilador de quem depende dele publicamente.
idf_component_register(
    SRCS "net.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi nvs_flash esp_netif esp_event lwip espressif__esp_hosted esp_system bootloader_support esp_app_format
    # Firmware do chip de Wi-Fi (ESP32-C6) para atualizá-lo pelo aparelho (ver firmware/c6/LEIA.md).
    EMBED_FILES "../../c6/network_adapter_esp32c6.bin"
)

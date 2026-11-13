# Firmware do chip de Wi-Fi (ESP32-C6)

`network_adapter_esp32c6.bin` é o firmware oficial do ESP-Hosted (projeto `slave` do
componente espressif/esp_hosted 2.12.13), compilado para ESP32-C6 com a configuração padrão
(transporte SDIO, modo streaming), igual ao lado do P4. Vai embutido no firmware do gravador
e é enviado ao C6 por Ajustes > Sobre > Atualizar chip de Wi-Fi.

SHA-256: b83859e6929e65a9a3696b69df8cf1fcb8b0e9106b335edf082affad6d68a9f3

Como recompilar (ESP-IDF do PlatformIO, numa cópia do componente):

    cp -R managed_components/espressif__esp_hosted /tmp/c6 && cd /tmp/c6/slave
    export ESP_IDF_VERSION=5.5 IDF_PYTHON_CHECK_CONSTRAINTS=no IDF_PATH=~/.platformio/packages/framework-espidf
    export PATH=~/.platformio/penv/.espidf-5.5.5/bin:~/.platformio/packages/toolchain-riscv32-esp/bin:~/.platformio/packages/tool-cmake/bin:~/.platformio/packages/tool-ninja:$PATH
    python $IDF_PATH/tools/idf.py -DIDF_TARGET=esp32c6 build   # saída: build/network_adapter.bin

Ao atualizar o esp_hosted do P4, recompile este arquivo na mesma versão.

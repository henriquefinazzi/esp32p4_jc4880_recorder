# Wi-Fi chip firmware (ESP32-C6)

`network_adapter_esp32c6.bin` is the official ESP-Hosted firmware (the `slave` project of the
espressif/esp_hosted 2.12.13 component), built for the ESP32-C6 with the default configuration
(SDIO transport, streaming mode), matching the P4 side. It is embedded in the recorder firmware
and sent to the C6 from Settings > About > Update Wi-Fi chip.

SHA-256: b83859e6929e65a9a3696b69df8cf1fcb8b0e9106b335edf082affad6d68a9f3

How to rebuild it (with PlatformIO's ESP-IDF, on a copy of the component):

    cp -R managed_components/espressif__esp_hosted /tmp/c6 && cd /tmp/c6/slave
    export ESP_IDF_VERSION=5.5 IDF_PYTHON_CHECK_CONSTRAINTS=no IDF_PATH=~/.platformio/packages/framework-espidf
    export PATH=~/.platformio/penv/.espidf-5.5.5/bin:~/.platformio/packages/toolchain-riscv32-esp/bin:~/.platformio/packages/tool-cmake/bin:~/.platformio/packages/tool-ninja:$PATH
    python $IDF_PATH/tools/idf.py -DIDF_TARGET=esp32c6 build   # output: build/network_adapter.bin

The build output is `build/network_adapter.bin`. When updating esp_hosted on the P4, rebuild this file
at the same version.

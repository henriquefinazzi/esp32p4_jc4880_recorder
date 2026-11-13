# Avisos de terceiros / Third-party notices

O código próprio do projeto está sob a licença MIT ([LICENSE](LICENSE)). Os itens abaixo são de terceiros
e mantêm as licenças deles; a licença MIT não se aplica a eles.

## Incluídos neste repositório

| Componente | Onde | Licença | Texto da licença |
|---|---|---|---|
| Shine (codificador MP3), © autores do Shine (Gabriel Bouvigne, Pete Everett, Romain Beauxis e outros), <https://github.com/toots/shine>, sem alterações | `firmware/components/shine/` | LGPL-2.0-only (GNU Library General Public License v2) | `firmware/components/shine/COPYING` |
| esp_tinyusb 1.7.6~2, © Espressif Systems, <https://github.com/espressif/esp-usb>, **modificado** (ver `LEIA-PATCH.md`) | `firmware/components/espressif__esp_tinyusb/` | Apache-2.0 | `firmware/components/espressif__esp_tinyusb/LICENSE` |
| `tusb_config.h` dentro do esp_tinyusb, © 2019 Ha Thach (tinyusb.org) | idem | MIT | abaixo |
| Fonte Montserrat, © 2011 The Montserrat Project Authors, <https://github.com/JulietaUla/Montserrat> | `firmware/components/fonts/` | SIL OFL 1.1 | `firmware/components/fonts/OFL.txt` |
| Sequência de inicialização do painel ST7701S, do BSP <https://github.com/ultramcu/guition-jc4880p4-bsp>, © 2026 ultramcu | `firmware/components/display/st7701_init.h` | MIT | `licenses/LICENSE-ultramcu-guition-bsp.txt` |
| Firmware do ESP32-C6 (binário), compilado de esp-hosted-mcu 2.12.13 (<https://github.com/espressif/esp-hosted-mcu>) com ESP-IDF 5.5.5 | `firmware/c6/network_adapter_esp32c6.bin` | Apache-2.0 (esp-hosted, ESP-IDF, bibliotecas Wi-Fi/PHY da Espressif); inclui também FreeRTOS (MIT), wpa_supplicant (BSD-3-Clause), mbedTLS (Apache-2.0), protobuf-c (BSD-2-Clause), newlib (licenças BSD) e a biblioteca do controlador BLE da Espressif, derivada do Apache Mynewt NimBLE (Apache-2.0, ver NOTICE) | `licenses/LICENSE-Apache-2.0.txt`, `licenses/NOTICE-esp32c6-bt-lib.txt` e a lista de direitos autorais do ESP-IDF: <https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/COPYRIGHT.html> |

### Licença MIT do TinyUSB (`tusb_config.h`)

```
The MIT License (MIT)

Copyright (c) 2019 Ha Thach (tinyusb.org)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## Baixados na compilação (não estão no repositório)

O PlatformIO e o gerenciador de componentes do ESP-IDF baixam, entre outros:

- **ESP-IDF** e componentes da Espressif (esp_hosted, esp_wifi_remote, esp_lvgl_port, esp_lcd_touch,
  esp_lcd_touch_gt911, esp_codec_dev, mdns, eppp_link, esp_serial_slave_link, wifi_remote_over_eppp):
  Apache-2.0. Dentro do ESP-IDF: lwIP (BSD-3), mbedTLS (Apache-2.0), protobuf-c (BSD-2), FreeRTOS (MIT).
- **LVGL**: MIT. **TinyUSB**: MIT. **cJSON**: MIT.
- **esp_audio_codec** (Espressif), usado no Opus: licença *Espressif Modified MIT*, que permite o uso
  **apenas com produtos da Espressif**. Inclui a libopus (BSD-3).

## Ao distribuir um firmware compilado

O firmware compilado contém o Shine, sob LGPL-2.0-only, ligado estaticamente. Quem distribuir o `firmware.bin`
(por exemplo, como arquivo de uma versão no GitHub ou gravado num aparelho) deve:

1. avisar que o firmware contém o Shine sob a LGPL e incluir o texto da licença (`COPYING`);
2. permitir que o usuário modifique o Shine e gere de novo o firmware. Num *release* do GitHub, basta
   publicar o binário junto à tag/commit exato do código-fonte deste repositório (que inclui o Shine e o
   `dependencies.lock`) e às instruções de compilação (`pio run`) — LGPL §6(d). Num aparelho entregue a
   alguém, entregue junto o código-fonte daquela versão (por exemplo, um .zip da tag) ou uma oferta por
   escrito, válida por pelo menos 3 anos, de fornecê-lo — LGPL §6(a)/(c). Não impeça a modificação nem a
   engenharia reversa para depurar essas modificações;
3. incluir estes avisos e os textos das licenças: MIT (este projeto, ultramcu, TinyUSB, LVGL, cJSON,
   FreeRTOS), Apache-2.0 (ESP-IDF, esp_hosted, esp_tinyusb, mbedTLS, mdns, esp_lcd_touch), BSD-3 (lwIP,
   libopus, wpa_supplicant), BSD-2 (protobuf-c), *Espressif Modified MIT* (esp_audio_codec — arquivo
   `LICENSE` do componente), OFL 1.1 (Montserrat), LGPL-2.0 (Shine) e o NOTICE do NimBLE; a lista completa
   do ESP-IDF está em <https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/COPYRIGHT.html>.
   Na prática: anexe a cada *release* um `licenses.zip` com esses textos (estão em
   `firmware/managed_components/*/LICENSE*` depois da primeira compilação e na página acima).

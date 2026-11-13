# Validação da placa

Testes feitos na Guition JC4880P443C_I_W_Y (ESP32-P4 rev 1.3 + ESP32-C6) para confirmar que ela serve
como gravador. Úteis para quem tiver a mesma placa.

## Portas e botões

- **Duas USB-C.** Uma é a USB Serial/JTAG do P4: grava o firmware e mostra o console. A outra é a USB
  nativa de alta velocidade do P4, usada no modo pendrive.
- **Botões** (olhando a placa por trás, com o microSD para cima): no canto superior esquerdo, **BOOT (SW1)**
  à esquerda e **RST (SW2)** à direita. O BOOT também começa e para a gravação. O terceiro botão (SW3),
  ao lado do conector da bateria, é a tecla do IP5306: liga e desliga a placa quando ela está na bateria.
- O LED em GPIO26 citado em alguns materiais da comunidade não acende nesta placa.

## Periféricos

| Item | Resultado | Observação |
|---|---|---|
| Tela | funciona | ST7701S por MIPI-DSI (2 vias, 500 Mbps), LDO canal 3 em 2,5 V, LVGL 9 |
| Toque | funciona | GT911 em 0x5D, no mesmo I²C do codec (SDA 7, SCL 8) |
| microSD | funciona | SDMMC de 4 bits, slot 0 (GPIO 39–44), alimentado pelo LDO canal 4 |
| Wi-Fi | funciona | ESP32-C6 por SDIO (ESP-Hosted). Veio de fábrica com o firmware 2.3.2; o gravador o atualiza para 2.12.13 pela tela Sobre |
| Modo pendrive | funciona | o cartão aparece no computador; ao ejetar, volta ao aparelho. Desligado durante a gravação |

## Áudio

| Item | Resultado | Observação |
|---|---|---|
| Microfone na placa | sim | o codec ES8311 (I²C 0x18) tem um microfone soldado. Silêncio ≈ −64 dBFS; fala a 30 cm com picos até −4 dBFS com ganho de 30 dB |
| Gravação sem perdas | funciona | 60 s gravados resultam em 60,0 s no arquivo, sem blocos perdidos |
| Queda de energia | funciona | o arquivo é fechado a cada 2 s: 15 s gravados antes de um reinício resultaram em 14 s recuperados |
| Cartão cheio | funciona | a gravação em curso é fechada com cabeçalho válido e o aparelho avisa |
| Fala a 30 cm e a 1 m | funciona | transcrição automática (Whisper, no computador) correta |
| Conversa de 2 min com várias pessoas | funciona | sem saturação; transcrição coerente do início ao fim |
| Chamada em viva-voz | funciona | transcrição equivalente à de uma gravação feita pelo microfone de um notebook |
| Opus e MP3 em tempo real | funciona | o P4 codifica sem perder áudio (Opus pelo `esp_audio_codec` 2.5; MP3 pelo Shine) |

Ganho padrão: 30 dB serviu de 30 cm a 1 m sem saturar.

## Especificação do fabricante

- Consumo típico: cerca de 320 mA a 5 V.
- Bateria: conector MX 1.25 de 2 pinos, carga pelo IP5306. Confira a polaridade antes de ligar uma célula.

## Ainda não medido

- Consumo real com tela acesa e apagada (para escolher a bateria).
- Gravação contínua de 30 minutos ou mais.
- Conversa a 3 m.

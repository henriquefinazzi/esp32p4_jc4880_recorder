# Arquitetura do firmware

Visão geral de como o firmware está organizado. Detalhes de cada parte ficam nos comentários do código.

## Tarefas

| Tarefa | O que faz |
|---|---|
| áudio | lê o microfone (ES8311 por I²S, 16 kHz, mono, 16 bits) em blocos de 100 ms, aplica o filtro passa-altas e põe os blocos num buffer de 8 s na PSRAM |
| gravação | tira os blocos do buffer e escreve no formato escolhido (WAV, Opus ou MP3); fecha o arquivo a cada 2 s; núcleo fixo e pilha grande por causa do Opus |
| interface (LVGL) | telas, teclado, avisos; um temporizador de 200 ms atualiza relógio, estado e transições |
| rede | Wi-Fi pelo ESP32-C6 (ESP-Hosted), reconexão automática às redes salvas, ponto de acesso, hora por NTP |
| configuração | espelha as configurações no `CONFIG.JSON` do cartão e aplica as mudanças feitas nele |
| servidor web | página das gravações, API e atualização de firmware |

O buffer de 8 s separa a leitura do microfone da escrita no cartão: uma pausa do cartão não perde áudio.

## Componentes (`firmware/components`)

- **audio_in / audio_dsp:** captura e filtro.
- **wav_writer / rec_writer:** escrita em WAV, Opus (empacotamento Ogg próprio) e MP3 (Shine).
- **storage:** cartão microSD e modo pendrive (TinyUSB). O cartão tem um dono por vez: o aparelho ou o
  computador. Usa uma cópia local do `esp_tinyusb` com uma correção que impede formatar sozinho um cartão
  que não seja FAT (ver `components/espressif__esp_tinyusb/LEIA-PATCH.md`).
- **display:** tela, toque e LVGL.
- **net:** Wi-Fi, redes salvas, ponto de acesso, NTP e atualização do ESP32-C6.
- **settings_file:** leitura e validação do `CONFIG.JSON` (C puro, testado no computador).
- **i18n:** textos por chave, carregados de arquivos JSON.

## Onde ficam as configurações

A memória interna (NVS) é a fonte; o `CONFIG.JSON` no cartão é um espelho editável. Assim o aparelho liga
configurado mesmo sem cartão. Valores inválidos no arquivo são ignorados.

## Firmware e atualização

A flash tem duas áreas de firmware (`ota_0` e `ota_1`). A atualização grava na área livre, troca e reinicia;
se o firmware novo não chegar ao fim da inicialização, o próximo reinício volta ao anterior.

## Testes

`firmware/test_host` compila as partes em C puro para o computador e confere, entre outras coisas, que
os arquivos Opus e MP3 gerados abrem no `ffmpeg` com a duração certa.

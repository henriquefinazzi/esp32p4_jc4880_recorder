[English](README.md) · **Português**

# esp32p4-jc4880-recorder (P4 Recorder)

Gravador de reuniões portátil com tela de toque, feito sobre a placa **Guition JC4880P443C_I_W_Y**
(ESP32-P4 + ESP32-C6, tela de 4,3"). Grava no cartão microSD em WAV, Opus ou MP3, mostra as
gravações numa página web na rede local e funciona como pendrive pela USB.

## O que ele faz

- **Gravação** com um toque (ou pelo botão BOOT), com nível de áudio, ganho ajustável durante a
  gravação e tela que apaga sozinha para economizar energia.
- **Formatos:** WAV (sem compressão, ~115 MB/h), Opus (~11 MB/h) ou MP3 (~14 MB/h).
- **Filtro de ruído grave** (passa-altas de 100 Hz) contra zumbido de ar-condicionado e batidas na mesa.
- **Proteção contra queda de energia:** o arquivo é fechado a cada 2 s; uma queda perde no máximo isso.
- **Lista de gravações** no aparelho: ver os dados, renomear, apagar uma ou várias.
- **Cartão cheio:** opção de apagar as gravações mais antigas automaticamente.
- **Página web** na rede local (`http://<nome>.local`): ouvir, baixar, renomear e apagar gravações,
  com usuário e senha. Cinco senhas erradas bloqueiam o endereço por 5 minutos.
- **Hotspot (ponto de acesso):** o gravador cria uma rede Wi-Fi para o celular se conectar direto,
  sem roteador, com QR code para entrar.
- **Modo pendrive:** pela USB de alta velocidade, o cartão aparece no computador. Ao ejetar, volta ao aparelho.
- **Configurações no cartão:** um arquivo `CONFIG.JSON` (oculto) espelha todas as configurações e pode
  ser editado no computador; o aparelho aplica as mudanças quando o cartão volta.
- **Atualização de firmware pelo Wi-Fi**, pela página ou por linha de comando, com volta automática à
  versão anterior se a nova não iniciar. O chip de Wi-Fi (ESP32-C6) também é atualizado pelo aparelho.
- **Bloqueio de tela** com código numérico e **tema claro/escuro**.
- **Idiomas:** português e inglês embutidos (o aparelho começa em inglês); outros idiomas por arquivo no
  cartão, sem recompilar.
- **Hora** pela internet (servidor NTP configurável) ou pelo navegador, quando não há internet.
- Cartões em formato não reconhecido (exFAT, NTFS) **nunca são formatados sem confirmação**.

## Hardware

| Item | Detalhe |
|---|---|
| Placa | Guition JC4880P443C_I_W_Y (ESP32-P4 com 32 MB de PSRAM e 16 MB de flash; ESP32-C6 para Wi-Fi) |
| Tela | 4,3", 480 x 800, MIPI-DSI (ST7701S), toque GT911 |
| Áudio | Codec ES8311 com microfone na placa |
| Armazenamento | microSD (FAT32) |
| Energia | USB-C, ou bateria de lítio pelo conector da placa (carga pelo IP5306) |

A placa tem duas USB-C: a de **gravação/console** (USB Serial/JTAG) e a de **alta velocidade**, usada no
modo pendrive.

> O firmware foi desenvolvido e testado numa placa com ESP32-P4 **revisão 1.3**. Por isso o
> `esp_audio_codec` está fixado na versão 2.5.x: a 2.6 em diante exige a revisão 3.0 ou superior.

## Baixar

Cada [versão publicada](https://github.com/henriquefinazzi/esp32p4_jc4880_recorder/releases) traz uma imagem pronta
para gravar. Grave no endereço 0x0 pela USB-C de console:

```bash
esptool --chip esp32p4 write-flash 0x0 p4-recorder-v0.4-full.bin
```

A imagem completa apaga as configurações salvas no aparelho; as gravações no Cartão-SD são mantidas.

## Compilar e gravar

Requisitos: [PlatformIO](https://platformio.org/) (o projeto usa a plataforma
[pioarduino](https://github.com/pioarduino/platform-espressif32) com ESP-IDF 5.5).

```bash
cd firmware
pio run                                   # compila
pio run -t upload --upload-port <porta>   # grava pela USB de console (primeira vez)
```

Para guardar os arquivos de compilação fora da pasta do projeto, defina `PLATFORMIO_WORKSPACE_DIR`.

A primeira gravação precisa ser pela USB, porque ela também grava a tabela de partições com as duas
áreas de firmware. Depois disso, as atualizações podem ser pelo Wi-Fi:

```bash
firmware/tools/ota.sh <nome-na-rede>      # compila e envia; pede a senha do navegador
```

Ou pela página web do gravador, em **Firmware → Atualizar firmware…** (aparece com as funções de
desenvolvedor ligadas, em Ajustes → Modo desenvolvedor).

## Primeiro uso

1. Coloque um microSD em FAT32 (se estiver em outro formato, o aparelho oferece formatar).
2. Em **Ajustes → Wi-Fi**, escolha a rede e digite a senha (só redes de 2,4 GHz).
3. Em **Ajustes → Acesso pelo navegador** aparecem o endereço (de fábrica, `http://p4recorder.local`),
   o usuário (`recorder`) e a senha (sorteada na primeira vez). Abra o endereço num computador ou
   celular na mesma rede. Sem roteador, ligue o **hotspot** (`P4Recorder-XXXX`) e abra
   `http://192.168.4.1`.

## Configurações pelo cartão

O arquivo `CONFIG.JSON`, na raiz do cartão, tem todas as configurações, inclusive senhas de Wi-Fi.
Edite no computador e ejete o cartão: o aparelho aplica o que mudou e mostra um aviso. Valores
inválidos são ignorados e o arquivo é regravado com os valores em uso. Trate o cartão como algo que
contém senhas.

## Idiomas

Os textos ficam em `firmware/lang/pt.json` e `firmware/lang/en.json` (chave → texto). Para outro idioma,
copie um deles, troque `_codigo` e `_nome`, traduza e coloque no cartão como `LANGUAGES/<codigo>.json`.
Ele aparece em **Ajustes → Idioma**. O aparelho começa em inglês; textos que faltarem saem em inglês.

## Testes

Partes em C puro (escrita de WAV, empacotamento Ogg Opus, MP3, filtro de áudio, leitura do
`CONFIG.JSON`) têm testes que rodam no computador. Precisam de CMake, `libopus`, `ffmpeg` e do ESP-IDF
instalado pelo PlatformIO (o cJSON vem de lá; outro caminho pode ser dado com `-DCJSON_DIR=...`):

```bash
cmake -S firmware/test_host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Estrutura

```
firmware/
  src/                 interface (ui.c), núcleo (main.c), servidor web, configurações
  components/          áudio, cartão, tela, rede, formatos de gravação, idiomas…
  lang/                textos em português e inglês
  c6/                  firmware do chip de Wi-Fi (ESP-Hosted) e como recompilá-lo
  test_host/           testes no computador
  tools/ota.sh         envio de firmware pelo Wi-Fi
docs/                  documentação do projeto
```

Mais detalhes (em inglês) em [docs/architecture.md](docs/architecture.md) (como o firmware é organizado),
[docs/board-validation.md](docs/board-validation.md) (testes feitos na placa) e
[docs/TODO.md](docs/TODO.md) (próximos passos).

## Licença

O código deste projeto está sob a licença MIT (arquivo [LICENSE](LICENSE)). Componentes de terceiros
mantêm as licenças deles; veja [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

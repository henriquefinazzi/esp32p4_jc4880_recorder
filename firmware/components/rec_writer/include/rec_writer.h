// Gravação em arquivo nos formatos WAV (PCM), Opus (Ogg Opus) e MP3. Áudio mono 16 bits.
// C puro: o codificador Opus fica atrás de opus_shim.h (ESP-IDF no aparelho, libopus nos
// testes do computador). Retornos int: 0 = ok, negativo = erro (errno preservado).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { REC_FMT_WAV = 0, REC_FMT_OPUS = 1, REC_FMT_MP3 = 2 } rec_format_t;

#define REC_OPUS_BITRATE 24000 // bits/s: voz clara a 16 kHz
#define REC_MP3_KBPS 32        // MPEG-2 camada III a 16 kHz

typedef struct rec_writer rec_writer_t;

// Extensão do arquivo, com ponto: ".WAV", ".OPUS", ".MP3".
const char *rec_format_ext(rec_format_t fmt);

// Formato pela extensão do nome do arquivo; -1 se não for de gravação.
int rec_format_from_name(const char *name);

// Duração aproximada pelo tamanho do arquivo (exata no WAV; taxa constante nos outros).
uint32_t rec_seconds_from_size(rec_format_t fmt, uint32_t sample_rate, uint64_t bytes);

rec_writer_t *rec_writer_open(const char *path, rec_format_t fmt, uint32_t sample_rate);

// Acrescenta amostras. Guarda sobras internamente até completar um quadro do codificador.
int rec_writer_write(rec_writer_t *w, const int16_t *pcm, size_t samples);

// Leva ao cartão tudo o que já está codificado (protege contra queda de energia).
int rec_writer_checkpoint(rec_writer_t *w);

// Codifica a sobra (com silêncio até o fim do quadro), fecha e libera.
int rec_writer_close(rec_writer_t *w);

// Bytes gravados no arquivo até agora.
uint64_t rec_writer_bytes(const rec_writer_t *w);

#ifdef __cplusplus
}
#endif

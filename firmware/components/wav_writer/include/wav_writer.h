// Gravador de WAV PCM. C puro, sem dependência de ESP-IDF, para rodar também no Mac.
// Retornos int: 0 = ok, negativo = erro.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wav_writer wav_writer_t;

// Cria o arquivo e escreve um cabeçalho de 44 bytes com tamanhos zero. NULL se falhar.
wav_writer_t *wav_writer_open(const char *path, uint32_t sample_rate_hz,
                              uint16_t channels, uint16_t bits_per_sample);

// Acrescenta PCM. Em erro, o contador de bytes não avança.
int wav_writer_write(wav_writer_t *w, const void *pcm, size_t bytes);

// Regrava os tamanhos no cabeçalho e força a ida ao disco. Depois disso, uma queda
// de energia preserva tudo o que foi escrito até aqui.
int wav_writer_checkpoint(wav_writer_t *w);

// Checkpoint, fecha e libera. O ponteiro não pode ser usado depois.
int wav_writer_close(wav_writer_t *w);

uint32_t wav_writer_data_bytes(const wav_writer_t *w);

#ifdef __cplusplus
}
#endif

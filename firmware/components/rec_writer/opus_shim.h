// Codificador Opus mínimo usado pelo rec_writer. Duas implementações:
// opus_shim_esp.c (esp_audio_codec, no aparelho) e test_host/opus_shim_host.c (libopus).
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct opus_shim opus_shim_t;

// Mono, 16 bits, quadros de 20 ms, taxa constante. NULL se falhar.
opus_shim_t *opus_shim_open(uint32_t sample_rate, uint32_t bitrate);

// Codifica exatamente um quadro (sample_rate / 50 amostras). Devolve bytes ou negativo.
int opus_shim_encode(opus_shim_t *e, const int16_t *pcm, uint8_t *out, size_t out_max);

void opus_shim_close(opus_shim_t *e);

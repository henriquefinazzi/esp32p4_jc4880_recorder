// Entrada de áudio pelo codec ES8311 da Guition JC4880P443C. Mono, 16 bits.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_IN_DEFAULT_GAIN_DB 30.0f

esp_err_t audio_in_init(uint32_t sample_rate_hz);

esp_err_t audio_in_set_gain_db(float db);

// Bloqueia até ler `n` amostras. Devolve o número lido ou um valor negativo em erro.
int audio_in_read(int16_t *samples, size_t n);

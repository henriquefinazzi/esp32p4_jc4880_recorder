// Filtros do áudio gravado. C puro, testado no computador.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Passa-altas Butterworth de 2ª ordem: corta graves abaixo de `cutoff_hz` (zumbido de
// ar-condicionado, batidas na mesa, vento) e o nível contínuo do microfone.
typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} dsp_highpass_t;

void dsp_highpass_init(dsp_highpass_t *f, float sample_rate, float cutoff_hz);

// Filtra no lugar. Satura em vez de dar a volta no int16.
void dsp_highpass_run(dsp_highpass_t *f, int16_t *pcm, size_t n);

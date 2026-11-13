// Filters for recorded audio. Plain C, tested on the computer.
#pragma once

#include <stddef.h>
#include <stdint.h>

// 2nd-order Butterworth high-pass: cuts lows below `cutoff_hz` (air-conditioner
// hum, table knocks, wind) and the microphone's DC offset.
typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} dsp_highpass_t;

void dsp_highpass_init(dsp_highpass_t *f, float sample_rate, float cutoff_hz);

// Filters in place. Saturates instead of wrapping around int16.
void dsp_highpass_run(dsp_highpass_t *f, int16_t *pcm, size_t n);

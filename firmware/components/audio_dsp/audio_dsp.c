#include "audio_dsp.h"

#include <math.h>

void dsp_highpass_init(dsp_highpass_t *f, float sample_rate, float cutoff_hz)
{
    // Fórmulas do "Audio EQ Cookbook" (R. Bristow-Johnson), Q = 1/sqrt(2).
    const float w0 = 2.0f * (float)M_PI * cutoff_hz / sample_rate;
    const float alpha = sinf(w0) / (2.0f * 0.70710678f);
    const float c = cosf(w0);
    const float a0 = 1.0f + alpha;
    f->b0 = (1.0f + c) / 2.0f / a0;
    f->b1 = -(1.0f + c) / a0;
    f->b2 = f->b0;
    f->a1 = -2.0f * c / a0;
    f->a2 = (1.0f - alpha) / a0;
    f->x1 = f->x2 = f->y1 = f->y2 = 0;
}

void dsp_highpass_run(dsp_highpass_t *f, int16_t *pcm, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        float x = pcm[i];
        float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
        f->x2 = f->x1;
        f->x1 = x;
        f->y2 = f->y1;
        f->y1 = y;
        pcm[i] = y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : (int16_t)lrintf(y);
    }
}

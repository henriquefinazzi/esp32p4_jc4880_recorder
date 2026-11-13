// Implementação do opus_shim com a libopus do computador, só para os testes.
#include <opus/opus.h>
#include <stdlib.h>

#include "opus_shim.h"

struct opus_shim {
    OpusEncoder *enc;
    int frame;
};

opus_shim_t *opus_shim_open(uint32_t sample_rate, uint32_t bitrate)
{
    opus_shim_t *e = calloc(1, sizeof(*e));
    int err = 0;
    e->enc = opus_encoder_create((opus_int32)sample_rate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        free(e);
        return NULL;
    }
    opus_encoder_ctl(e->enc, OPUS_SET_BITRATE((opus_int32)bitrate));
    opus_encoder_ctl(e->enc, OPUS_SET_VBR(0));
    e->frame = (int)(sample_rate / 50);
    return e;
}

int opus_shim_encode(opus_shim_t *e, const int16_t *pcm, uint8_t *out, size_t out_max)
{
    return opus_encode(e->enc, pcm, e->frame, out, (opus_int32)out_max);
}

void opus_shim_close(opus_shim_t *e)
{
    opus_encoder_destroy(e->enc);
    free(e);
}

#include "wav_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WAV_HEADER_BYTES 44u
// WAV size fields are 32 bits; the largest data chunk that fits.
#define WAV_MAX_DATA_BYTES (UINT32_MAX - (WAV_HEADER_BYTES - 8u))

struct wav_writer {
    FILE *file;
    uint32_t data_bytes;
    uint32_t sample_rate_hz;
    uint16_t channels;
    uint16_t bits_per_sample;
};

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static int write_header(wav_writer_t *w)
{
    uint8_t h[WAV_HEADER_BYTES];
    uint16_t block_align = (uint16_t)(w->channels * (w->bits_per_sample / 8));

    memcpy(h, "RIFF", 4);
    put_u32le(h + 4, w->data_bytes + (WAV_HEADER_BYTES - 8u));
    memcpy(h + 8, "WAVEfmt ", 8);
    put_u32le(h + 16, 16);
    put_u16le(h + 20, 1); // PCM
    put_u16le(h + 22, w->channels);
    put_u32le(h + 24, w->sample_rate_hz);
    put_u32le(h + 28, w->sample_rate_hz * block_align);
    put_u16le(h + 32, block_align);
    put_u16le(h + 34, w->bits_per_sample);
    memcpy(h + 36, "data", 4);
    put_u32le(h + 40, w->data_bytes);

    if (fseek(w->file, 0, SEEK_SET) != 0) return -1;
    if (fwrite(h, 1, sizeof(h), w->file) != sizeof(h)) return -1;
    if (fseek(w->file, 0, SEEK_END) != 0) return -1;
    return 0;
}

wav_writer_t *wav_writer_open(const char *path, uint32_t sample_rate_hz,
                              uint16_t channels, uint16_t bits_per_sample)
{
    if (!path || channels == 0 || bits_per_sample == 0 || bits_per_sample % 8 != 0) return NULL;

    wav_writer_t *w = calloc(1, sizeof(*w));
    if (!w) return NULL;

    w->file = fopen(path, "wb");
    if (!w->file) {
        free(w);
        return NULL;
    }
    w->sample_rate_hz = sample_rate_hz;
    w->channels = channels;
    w->bits_per_sample = bits_per_sample;

    if (wav_writer_checkpoint(w) != 0) {
        fclose(w->file);
        free(w);
        return NULL;
    }
    return w;
}

int wav_writer_write(wav_writer_t *w, const void *pcm, size_t bytes)
{
    if (!w || (!pcm && bytes > 0)) return -1;
    if (bytes == 0) return 0;
    if (bytes > WAV_MAX_DATA_BYTES - w->data_bytes) return -2;

    size_t written = fwrite(pcm, 1, bytes, w->file);
    // Counts what was actually accepted, so the header never promises more than there is.
    w->data_bytes += (uint32_t)written;
    return written == bytes ? 0 : -3;
}

int wav_writer_checkpoint(wav_writer_t *w)
{
    if (!w) return -1;
    if (write_header(w) != 0) return -2;
    if (fflush(w->file) != 0) return -3;
    if (fsync(fileno(w->file)) != 0) return -4;
    return 0;
}

int wav_writer_close(wav_writer_t *w)
{
    if (!w) return -1;
    int rc = wav_writer_checkpoint(w);
    if (fclose(w->file) != 0 && rc == 0) rc = -5;
    free(w);
    return rc;
}

uint32_t wav_writer_data_bytes(const wav_writer_t *w)
{
    return w ? w->data_bytes : 0;
}

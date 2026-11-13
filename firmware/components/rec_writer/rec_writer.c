#include "rec_writer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "layer3.h"
#include "ogg_opus.h"
#include "opus_shim.h"
#include "wav_writer.h"

#define MAX_FRAME 1152 // largest frame among the encoders (MP3 MPEG-1); Opus 20 ms at 16 kHz = 320

struct rec_writer {
    rec_format_t fmt;
    uint32_t rate;
    wav_writer_t *wav;
    FILE *f;
    ogg_opus_t *ogg;
    opus_shim_t *opus;
    shine_t mp3;
    int frame;              // samples per encoder frame
    int16_t pending[MAX_FRAME] __attribute__((aligned(16))); // Opus vector routines
    uint8_t pkt[1500] __attribute__((aligned(16)));          // Opus packet (off the task stack)
    int npending;
    uint64_t bytes;
};

const char *rec_format_ext(rec_format_t fmt)
{
    return fmt == REC_FMT_OPUS ? ".OPUS" : fmt == REC_FMT_MP3 ? ".MP3" : ".WAV";
}

int rec_format_from_name(const char *name)
{
    size_t n = name ? strlen(name) : 0;
    if (n > 4 && strcasecmp(name + n - 4, ".WAV") == 0) return REC_FMT_WAV;
    if (n > 5 && strcasecmp(name + n - 5, ".OPUS") == 0) return REC_FMT_OPUS;
    if (n > 4 && strcasecmp(name + n - 4, ".MP3") == 0) return REC_FMT_MP3;
    return -1;
}

uint32_t rec_seconds_from_size(rec_format_t fmt, uint32_t sample_rate, uint64_t bytes)
{
    switch (fmt) {
    case REC_FMT_OPUS: return bytes > 100 ? (uint32_t)((bytes - 100) * 8 / REC_OPUS_BITRATE) : 0;
    case REC_FMT_MP3: return (uint32_t)(bytes * 8 / (REC_MP3_KBPS * 1000));
    default: return bytes > 44 ? (uint32_t)((bytes - 44) / (sample_rate * 2)) : 0;
    }
}

static int put_bytes(rec_writer_t *w, const void *p, size_t n)
{
    if (n && fwrite(p, 1, n, w->f) != n) return -1;
    w->bytes += n;
    return 0;
}

// Encodes one full frame from w->pending.
static int encode_frame(rec_writer_t *w)
{
    if (w->fmt == REC_FMT_OPUS) {
        int n = opus_shim_encode(w->opus, w->pending, w->pkt, sizeof(w->pkt));
        if (n < 0) return -1;
        if (ogg_opus_packet(w->ogg, w->pkt, (size_t)n, (uint32_t)w->frame)) return -1;
        w->bytes = w->ogg->bytes;
        return 0;
    }
    int16_t *ch[1] = {w->pending};
    int written = 0;
    unsigned char *out = shine_encode_buffer(w->mp3, ch, &written);
    return put_bytes(w, out, (size_t)written);
}

rec_writer_t *rec_writer_open(const char *path, rec_format_t fmt, uint32_t sample_rate)
{
    rec_writer_t *w = aligned_alloc(16, (sizeof(*w) + 15) & ~(size_t)15);
    if (w) memset(w, 0, sizeof(*w));
    if (!w) return NULL;
    w->fmt = fmt;
    w->rate = sample_rate;

    if (fmt == REC_FMT_WAV) {
        w->wav = wav_writer_open(path, sample_rate, 1, 16);
        if (w->wav) return w;
        free(w);
        return NULL;
    }

    w->f = fopen(path, "wb");
    if (!w->f) goto fail;
    if (fmt == REC_FMT_OPUS) {
        w->frame = (int)(sample_rate / 50);
        w->opus = opus_shim_open(sample_rate, REC_OPUS_BITRATE);
        w->ogg = malloc(sizeof(*w->ogg));
        if (!w->opus || !w->ogg) goto fail;
        if (ogg_opus_begin(w->ogg, w->f, sample_rate, (uint32_t)rand() | 1u)) goto fail;
        w->bytes = w->ogg->bytes;
    } else {
        shine_config_t cfg;
        shine_set_config_mpeg_defaults(&cfg.mpeg);
        cfg.wave.channels = PCM_MONO;
        cfg.wave.samplerate = (int)sample_rate;
        cfg.mpeg.mode = MONO;
        cfg.mpeg.bitr = REC_MP3_KBPS;
        if (shine_check_config(cfg.wave.samplerate, cfg.mpeg.bitr) < 0) goto fail;
        w->mp3 = shine_initialise(&cfg);
        if (!w->mp3) goto fail;
        w->frame = shine_samples_per_pass(w->mp3);
        if (w->frame > MAX_FRAME) goto fail;
    }
    return w;

fail: {
    int saved = errno;
    if (w->opus) opus_shim_close(w->opus);
    if (w->mp3) shine_close(w->mp3);
    free(w->ogg);
    if (w->f) {
        fclose(w->f);
        unlink(path);
    }
    free(w);
    errno = saved;
    return NULL;
}
}

int rec_writer_write(rec_writer_t *w, const int16_t *pcm, size_t samples)
{
    if (w->fmt == REC_FMT_WAV) return wav_writer_write(w->wav, pcm, samples * sizeof(int16_t));
    while (samples) {
        size_t take = (size_t)(w->frame - w->npending);
        if (take > samples) take = samples;
        memcpy(w->pending + w->npending, pcm, take * sizeof(int16_t));
        w->npending += (int)take;
        pcm += take;
        samples -= take;
        if (w->npending == w->frame) {
            if (encode_frame(w)) return -1;
            w->npending = 0;
        }
    }
    return 0;
}

int rec_writer_checkpoint(rec_writer_t *w)
{
    if (w->fmt == REC_FMT_WAV) return wav_writer_checkpoint(w->wav);
    if (w->fmt == REC_FMT_OPUS) {
        if (ogg_opus_flush(w->ogg, false)) return -1;
        w->bytes = w->ogg->bytes;
    }
    if (fflush(w->f) != 0 || fsync(fileno(w->f)) != 0) return -1;
    return 0;
}

int rec_writer_close(rec_writer_t *w)
{
    if (w->fmt == REC_FMT_WAV) {
        int rc = wav_writer_close(w->wav);
        free(w);
        return rc;
    }
    int rc = 0;
    if (w->npending) {
        memset(w->pending + w->npending, 0, (size_t)(w->frame - w->npending) * sizeof(int16_t));
        w->npending = w->frame;
        if (encode_frame(w)) rc = -1;
    }
    if (w->fmt == REC_FMT_OPUS) {
        if (ogg_opus_flush(w->ogg, true)) rc = -1;
        opus_shim_close(w->opus);
        free(w->ogg);
    } else {
        int written = 0;
        unsigned char *out = shine_flush(w->mp3, &written);
        if (put_bytes(w, out, (size_t)written)) rc = -1;
        shine_close(w->mp3);
    }
    if (fflush(w->f) != 0 || fsync(fileno(w->f)) != 0) rc = -1;
    if (fclose(w->f) != 0) rc = -1;
    free(w);
    return rc;
}

uint64_t rec_writer_bytes(const rec_writer_t *w)
{
    return w->fmt == REC_FMT_WAV ? 44 + wav_writer_data_bytes(w->wav) : w->bytes;
}

// Recording to file in WAV (PCM), Opus (Ogg Opus) and MP3 formats. Mono 16-bit audio.
// Plain C: the Opus encoder sits behind opus_shim.h (ESP-IDF on the device, libopus in the
// computer tests). int returns: 0 = ok, negative = error (errno preserved).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { REC_FMT_WAV = 0, REC_FMT_OPUS = 1, REC_FMT_MP3 = 2 } rec_format_t;

#define REC_OPUS_BITRATE 24000 // bits/s: clear voice at 16 kHz
#define REC_MP3_KBPS 32        // MPEG-2 Layer III at 16 kHz

typedef struct rec_writer rec_writer_t;

// File extension, with dot: ".WAV", ".OPUS", ".MP3".
const char *rec_format_ext(rec_format_t fmt);

// Format from the file name's extension; -1 if not a recording.
int rec_format_from_name(const char *name);

// Approximate duration from the file size (exact for WAV; constant bitrate for the others).
uint32_t rec_seconds_from_size(rec_format_t fmt, uint32_t sample_rate, uint64_t bytes);

rec_writer_t *rec_writer_open(const char *path, rec_format_t fmt, uint32_t sample_rate);

// Appends samples. Keeps leftovers internally until an encoder frame is complete.
int rec_writer_write(rec_writer_t *w, const int16_t *pcm, size_t samples);

// Flushes everything already encoded to the card (protects against power loss).
int rec_writer_checkpoint(rec_writer_t *w);

// Encodes the leftover (padded with silence to the end of the frame), closes and frees.
int rec_writer_close(rec_writer_t *w);

// Bytes written to the file so far.
uint64_t rec_writer_bytes(const rec_writer_t *w);

#ifdef __cplusplus
}
#endif

// PCM WAV writer. Plain C, no ESP-IDF dependency, so it also runs on the Mac.
// int returns: 0 = ok, negative = error.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wav_writer wav_writer_t;

// Creates the file and writes a 44-byte header with zero sizes. NULL on failure.
wav_writer_t *wav_writer_open(const char *path, uint32_t sample_rate_hz,
                              uint16_t channels, uint16_t bits_per_sample);

// Appends PCM. On error, the byte counter does not advance.
int wav_writer_write(wav_writer_t *w, const void *pcm, size_t bytes);

// Rewrites the sizes in the header and forces a flush to disk. After that, a power
// loss keeps everything written so far.
int wav_writer_checkpoint(wav_writer_t *w);

// Checkpoint, close and free. The pointer must not be used afterwards.
int wav_writer_close(wav_writer_t *w);

uint32_t wav_writer_data_bytes(const wav_writer_t *w);

#ifdef __cplusplus
}
#endif

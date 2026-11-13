// Minimal Opus encoder used by rec_writer. Two implementations:
// opus_shim_esp.c (esp_audio_codec, on the device) and test_host/opus_shim_host.c (libopus).
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct opus_shim opus_shim_t;

// Mono, 16-bit, 20 ms frames, constant bitrate. NULL on failure.
opus_shim_t *opus_shim_open(uint32_t sample_rate, uint32_t bitrate);

// Encodes exactly one frame (sample_rate / 50 samples). Returns bytes or negative.
int opus_shim_encode(opus_shim_t *e, const int16_t *pcm, uint8_t *out, size_t out_max);

void opus_shim_close(opus_shim_t *e);

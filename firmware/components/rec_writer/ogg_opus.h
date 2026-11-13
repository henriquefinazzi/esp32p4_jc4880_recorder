// Ogg Opus packaging (RFC 7845): OpusHead/OpusTags headers and pages of packets.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    uint32_t serial;
    uint32_t page_seq;
    uint64_t granule;      // samples at 48 kHz up to the end of the last packet
    uint8_t segs[255];     // segment table of the page being built
    int nsegs;
    uint8_t body[255 * 255];
    size_t body_len;
    uint32_t rate_mult;    // 48000 / input rate
    uint64_t bytes;
} ogg_opus_t;

int ogg_opus_begin(ogg_opus_t *o, FILE *f, uint32_t input_rate, uint32_t serial);
// Appends a packet of `frame_samples` samples (at the input rate). Closes the page
// on its own when full.
int ogg_opus_packet(ogg_opus_t *o, const uint8_t *pkt, size_t len, uint32_t frame_samples);
// Closes the page being built (if any). eos marks the end of the stream.
int ogg_opus_flush(ogg_opus_t *o, bool eos);
uint32_t ogg_crc32(const uint8_t *p, size_t n, uint32_t crc);

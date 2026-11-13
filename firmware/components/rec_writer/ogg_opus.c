#include "ogg_opus.h"

#include <string.h>

#define PRE_SKIP 312 // Opus encoder delay (6.5 ms at 48 kHz)
#define PACKETS_PER_PAGE 50 // ~1 s per page: low overhead and small loss on power failure

static uint32_t s_crc_table[256];

static void crc_init(void)
{
    if (s_crc_table[1]) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t r = i << 24;
        for (int j = 0; j < 8; j++) r = r & 0x80000000u ? (r << 1) ^ 0x04C11DB7u : r << 1;
        s_crc_table[i] = r;
    }
}

uint32_t ogg_crc32(const uint8_t *p, size_t n, uint32_t crc)
{
    crc_init();
    while (n--) crc = (crc << 8) ^ s_crc_table[((crc >> 24) ^ *p++) & 0xFF];
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static int write_page(ogg_opus_t *o, uint8_t flags, uint64_t granule)
{
    uint8_t h[27 + 255];
    memcpy(h, "OggS", 4);
    h[4] = 0;
    h[5] = flags;
    put32(h + 6, (uint32_t)granule);
    put32(h + 10, (uint32_t)(granule >> 32));
    put32(h + 14, o->serial);
    put32(h + 18, o->page_seq++);
    put32(h + 22, 0);
    h[26] = (uint8_t)o->nsegs;
    memcpy(h + 27, o->segs, o->nsegs);
    size_t hl = 27 + o->nsegs;
    uint32_t crc = ogg_crc32(h, hl, 0);
    crc = ogg_crc32(o->body, o->body_len, crc);
    put32(h + 22, crc);
    if (fwrite(h, 1, hl, o->f) != hl) return -1;
    if (o->body_len && fwrite(o->body, 1, o->body_len, o->f) != o->body_len) return -1;
    o->bytes += hl + o->body_len;
    o->nsegs = 0;
    o->body_len = 0;
    return 0;
}

// Puts a whole packet into the page being built (segment table in steps of 255).
static int add_packet(ogg_opus_t *o, const uint8_t *pkt, size_t len)
{
    int need = (int)(len / 255) + 1;
    if (o->nsegs + need > 255) return -1;
    memcpy(o->body + o->body_len, pkt, len);
    o->body_len += len;
    for (size_t left = len;; left -= 255) {
        o->segs[o->nsegs++] = left >= 255 ? 255 : (uint8_t)left;
        if (left < 255) break;
    }
    return 0;
}

int ogg_opus_begin(ogg_opus_t *o, FILE *f, uint32_t input_rate, uint32_t serial)
{
    memset(o, 0, sizeof(*o));
    o->f = f;
    o->serial = serial;
    o->rate_mult = 48000 / input_rate;

    uint8_t head[19];
    memcpy(head, "OpusHead", 8);
    head[8] = 1;  // version
    head[9] = 1;  // channels
    head[10] = PRE_SKIP & 0xFF;
    head[11] = PRE_SKIP >> 8;
    put32(head + 12, input_rate);
    head[16] = 0; // output gain
    head[17] = 0;
    head[18] = 0; // mapping 0: mono/stereo
    if (add_packet(o, head, sizeof(head)) || write_page(o, 0x02, 0)) return -1; // beginning of stream

    static const char vendor[] = "p4recorder";
    uint8_t tags[8 + 4 + sizeof(vendor) - 1 + 4];
    memcpy(tags, "OpusTags", 8);
    put32(tags + 8, sizeof(vendor) - 1);
    memcpy(tags + 12, vendor, sizeof(vendor) - 1);
    put32(tags + 12 + sizeof(vendor) - 1, 0); // no comments
    if (add_packet(o, tags, sizeof(tags)) || write_page(o, 0, 0)) return -1;
    return 0;
}

int ogg_opus_packet(ogg_opus_t *o, const uint8_t *pkt, size_t len, uint32_t frame_samples)
{
    if (o->nsegs + (int)(len / 255) + 1 > 255 && ogg_opus_flush(o, false)) return -1;
    if (add_packet(o, pkt, len)) return -1;
    o->granule += (uint64_t)frame_samples * o->rate_mult;
    if (o->nsegs >= PACKETS_PER_PAGE) return ogg_opus_flush(o, false);
    return 0;
}

int ogg_opus_flush(ogg_opus_t *o, bool eos)
{
    if (o->nsegs == 0 && !eos) return 0;
    return write_page(o, eos ? 0x04 : 0, o->granule);
}

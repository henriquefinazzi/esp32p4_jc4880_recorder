// Empacotamento Ogg Opus (RFC 7845): cabeçalhos OpusHead/OpusTags e páginas com pacotes.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    uint32_t serial;
    uint32_t page_seq;
    uint64_t granule;      // amostras a 48 kHz até o fim do último pacote
    uint8_t segs[255];     // tabela de segmentos da página em montagem
    int nsegs;
    uint8_t body[255 * 255];
    size_t body_len;
    uint32_t rate_mult;    // 48000 / taxa de entrada
    uint64_t bytes;
} ogg_opus_t;

int ogg_opus_begin(ogg_opus_t *o, FILE *f, uint32_t input_rate, uint32_t serial);
// Acrescenta um pacote de `frame_samples` amostras (na taxa de entrada). Fecha a página
// sozinho quando enche.
int ogg_opus_packet(ogg_opus_t *o, const uint8_t *pkt, size_t len, uint32_t frame_samples);
// Fecha a página em montagem (se houver). eos marca o fim do fluxo.
int ogg_opus_flush(ogg_opus_t *o, bool eos);
uint32_t ogg_crc32(const uint8_t *p, size_t n, uint32_t crc);

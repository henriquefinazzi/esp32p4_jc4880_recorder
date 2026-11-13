// Grava 5 s de tom com ruído nos três formatos; check_rec_files.sh confere com ffprobe.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ogg_opus.h"
#include "rec_writer.h"

static int failures;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c);   \
            failures++;                                             \
        }                                                           \
    } while (0)

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    CHECK(ogg_crc32((const uint8_t *)"", 0, 0) == 0);
    CHECK(ogg_crc32((const uint8_t *)"a", 1, 0) != 0);

    const uint32_t rate = 16000;
    int16_t block[256];
    for (int f = 0; f < 3; f++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/teste%s", dir, rec_format_ext((rec_format_t)f));
        rec_writer_t *w = rec_writer_open(path, (rec_format_t)f, rate);
        CHECK(w != NULL);
        if (!w) continue;
        unsigned t = 0;
        for (int b = 0; b < (int)(5 * rate / 256); b++) { // blocos de 256: não casam com os quadros
            for (int i = 0; i < 256; i++, t++) {
                block[i] = (int16_t)(8000 * sin(2 * M_PI * 440 * t / rate) + (rand() % 400 - 200));
            }
            CHECK(rec_writer_write(w, block, 256) == 0);
            if (b % 125 == 0) CHECK(rec_writer_checkpoint(w) == 0);
        }
        uint64_t bytes = rec_writer_bytes(w);
        CHECK(rec_writer_close(w) == 0);
        uint32_t est = rec_seconds_from_size((rec_format_t)f, rate, bytes);
        printf("%s: %llu bytes, %u s estimados\n", path, (unsigned long long)bytes, est);
        CHECK(est >= 4 && est <= 6);
        CHECK(rec_format_from_name(path) == f);
    }
    CHECK(rec_format_from_name("REC_0001 nome.opus") == REC_FMT_OPUS);
    CHECK(rec_format_from_name("REC_0001.txt") == -1);
    if (failures) {
        printf("%d falha(s)\n", failures);
        return 1;
    }
    printf("rec_writer: ok\n");
    return 0;
}

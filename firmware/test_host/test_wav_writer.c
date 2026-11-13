// wav_writer tests running on the Mac (no ESP-IDF).
#include "wav_writer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAILED %s:%d: %s\n", __func__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static char tmp_path[256];

static const char *new_tmp(void)
{
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/wav_writer_test_XXXXXX");
    int fd = mkstemp(tmp_path);
    if (fd >= 0) close(fd);
    return tmp_path;
}

static long read_file(const char *path, uint8_t *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t n = fread(buf, 1, max, f);
    (void)n;
    fclose(f);
    return size;
}

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void test_header_after_close(void)
{
    const char *path = new_tmp();
    static uint8_t zeros[32000];
    uint8_t h[44];

    wav_writer_t *w = wav_writer_open(path, 16000, 1, 16);
    CHECK(w != NULL);
    CHECK(wav_writer_write(w, zeros, sizeof(zeros)) == 0);
    CHECK(wav_writer_close(w) == 0);

    CHECK(read_file(path, h, sizeof(h)) == 32044);
    CHECK(memcmp(h, "RIFF", 4) == 0);
    CHECK(u32le(h + 4) == 32036);
    CHECK(memcmp(h + 8, "WAVEfmt ", 8) == 0);
    CHECK(u32le(h + 16) == 16);
    CHECK(u16le(h + 20) == 1);
    CHECK(u16le(h + 22) == 1);
    CHECK(u32le(h + 24) == 16000);
    CHECK(u32le(h + 28) == 32000);
    CHECK(u16le(h + 32) == 2);
    CHECK(u16le(h + 34) == 16);
    CHECK(memcmp(h + 36, "data", 4) == 0);
    CHECK(u32le(h + 40) == 32000);
    unlink(path);
}

static void test_checkpoint_survives_power_loss(void)
{
    const char *path = new_tmp();
    static uint8_t zeros[16000];
    uint8_t h[44];

    wav_writer_t *w = wav_writer_open(path, 16000, 1, 16);
    CHECK(w != NULL);
    CHECK(wav_writer_write(w, zeros, 16000) == 0);
    CHECK(wav_writer_checkpoint(w) == 0);
    CHECK(wav_writer_write(w, zeros, 8000) == 0);
    // No close: simulates a power loss. The header on disk reflects the checkpoint.
    CHECK(read_file(path, h, sizeof(h)) >= 16044);
    CHECK(u32le(h + 40) == 16000);
    CHECK(u32le(h + 4) == 16036);
    wav_writer_close(w);
    unlink(path);
}

static void test_open_bad_path(void)
{
    CHECK(wav_writer_open("/nao/existe/x.wav", 16000, 1, 16) == NULL);
}

static void test_write_edge_cases(void)
{
    const char *path = new_tmp();
    uint8_t buf[10] = {0};
    wav_writer_t *w = wav_writer_open(path, 16000, 1, 16);
    CHECK(w != NULL);
    CHECK(wav_writer_write(w, buf, 0) == 0);
    CHECK(wav_writer_write(NULL, buf, 10) < 0);
    CHECK(wav_writer_checkpoint(NULL) < 0);
    wav_writer_close(w);
    unlink(path);
}

static void test_close_after_failed_write_keeps_valid_header(void)
{
    const char *path = new_tmp();
    static uint8_t zeros[1000];
    uint8_t h[44];

    wav_writer_t *w = wav_writer_open(path, 16000, 1, 16);
    CHECK(w != NULL);
    CHECK(wav_writer_write(w, zeros, 1000) == 0);
    // Impossible write (would exceed the WAV 4 GiB limit): error, nothing changes.
    CHECK(wav_writer_write(w, zeros, SIZE_MAX) < 0);
    CHECK(wav_writer_data_bytes(w) == 1000);
    CHECK(wav_writer_close(w) == 0);
    CHECK(read_file(path, h, sizeof(h)) == 1044);
    CHECK(u32le(h + 40) == 1000);
    unlink(path);
}

static void test_data_bytes(void)
{
    const char *path = new_tmp();
    uint8_t buf[100] = {0};
    wav_writer_t *w = wav_writer_open(path, 16000, 1, 16);
    CHECK(w != NULL);
    CHECK(wav_writer_write(w, buf, 100) == 0);
    CHECK(wav_writer_write(w, buf, 60) == 0);
    CHECK(wav_writer_data_bytes(w) == 160);
    wav_writer_close(w);
    unlink(path);
}

int main(void)
{
    test_header_after_close();
    test_checkpoint_survives_power_loss();
    test_open_bad_path();
    test_write_edge_cases();
    test_close_after_failed_write_keeps_valid_header();
    test_data_bytes();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}

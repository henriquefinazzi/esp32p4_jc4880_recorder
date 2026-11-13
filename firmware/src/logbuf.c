#include "logbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define LOGBUF_SIZE (16 * 1024)

static char *s_buf;
static size_t s_head;  // next write position
static bool s_wrapped;
static vprintf_like_t s_orig;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static int hook(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    char line[256];
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > (int)sizeof(line) - 1) n = sizeof(line) - 1;
    if (n > 0 && s_buf) {
        portENTER_CRITICAL(&s_mux);
        for (int i = 0; i < n; i++) {
            s_buf[s_head++] = line[i];
            if (s_head == LOGBUF_SIZE) {
                s_head = 0;
                s_wrapped = true;
            }
        }
        portEXIT_CRITICAL(&s_mux);
    }
    return s_orig ? s_orig(fmt, args) : vprintf(fmt, args);
}

void logbuf_init(void)
{
    s_buf = heap_caps_malloc(LOGBUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_buf) return;
    s_orig = esp_log_set_vprintf(hook);
}

size_t logbuf_copy(char *out, size_t len)
{
    if (!s_buf || len == 0) return 0;
    size_t n = 0;
    portENTER_CRITICAL(&s_mux);
    if (s_wrapped) {
        size_t tail = LOGBUF_SIZE - s_head;
        size_t a = tail < len ? tail : len;
        memcpy(out, s_buf + s_head, a);
        n = a;
    }
    size_t b = s_head < len - n ? s_head : len - n;
    memcpy(out + n, s_buf, b);
    n += b;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

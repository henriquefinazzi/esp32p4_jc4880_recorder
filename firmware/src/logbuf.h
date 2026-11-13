// Last log lines (ESP_LOG) kept in memory, to read from the browser
// (/api/log) when the console USB is not connected.
#pragma once

#include <stddef.h>

void logbuf_init(void);

// Copies the log, oldest to newest. Returns how many bytes were written.
size_t logbuf_copy(char *out, size_t len);

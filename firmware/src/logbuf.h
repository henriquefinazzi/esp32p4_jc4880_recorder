// Últimas linhas do registro (ESP_LOG) guardadas na memória, para ler pelo navegador
// (/api/log) quando a USB do console não está ligada.
#pragma once

#include <stddef.h>

void logbuf_init(void);

// Copia o registro, do mais antigo ao mais novo. Devolve quantos bytes escreveu.
size_t logbuf_copy(char *out, size_t len);

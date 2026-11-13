// microSD da Guition JC4880P443C: SDMMC de 4 bits, alimentado pelo LDO interno canal 4.
// O cartão tem um dono por vez: o aparelho (montado em /sdcard) ou o computador,
// quando ligado pela USB High-Speed, que o vê como pendrive.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define STORAGE_MOUNT_POINT "/sdcard"

// Inicializa o cartão, monta em /sdcard e liga o modo pendrive. Nunca formata.
esp_err_t storage_mount(void);

// true quando o aparelho pode ler e escrever (cartão presente e não entregue ao computador).
bool storage_is_mounted(void);

// true quando o cartão está entregue ao computador pela USB.
bool storage_in_use_by_host(void);

// Liga ou desliga a presença na USB. Desligar durante a gravação impede o computador
// de tomar o cartão no meio de um arquivo.
void storage_usb_enable(bool enable);


// Espaço total e livre, em MB. false se o cartão não estiver com o aparelho.
bool storage_space_mb(uint32_t *total_mb, uint32_t *free_mb);

// Cartão presente mas com sistema de arquivos que o aparelho não lê (exFAT, NTFS...).
// Nunca é formatado sozinho: só por storage_format(), depois de o usuário confirmar.
bool storage_unreadable(void);

// Formata o cartão em FAT (apaga tudo) e o monta. Vale para o cartão no aparelho e para o
// cartão não reconhecido.
esp_err_t storage_format(void);

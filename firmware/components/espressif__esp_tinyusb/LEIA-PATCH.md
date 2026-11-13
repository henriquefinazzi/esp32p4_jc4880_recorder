# Cópia local de espressif/esp_tinyusb 1.7.6~2

Copiada de managed_components para corrigir um defeito que apagava dados: ao montar um
cartão cujo sistema de arquivos o FATFS não reconhece (exFAT, NTFS, corrompido), `_mount()`
formatava o cartão sozinho, ignorando `mount_config.format_if_mount_failed`.

Mudança (procure "P4 Recorder:" em tusb_msc_storage.c): o campo é guardado no handle e
`_mount()` só formata se ele for verdadeiro. O firmware do P4 Recorder sempre usa falso.

Ao atualizar o esp_tinyusb, confira se a Espressif já respeita o campo; se não, reaplique.

Segunda mudança: `tinyusb_msc_storage_unformatted()` informa se a última montagem falhou por falta
de sistema de arquivos (ex.: cartão formatado em exFAT no computador e ejetado).

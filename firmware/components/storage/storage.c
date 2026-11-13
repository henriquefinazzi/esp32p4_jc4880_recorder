#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb.h"
#include "tusb_msc_storage.h"

#define SD_LDO_CHANNEL 4

static const char *TAG = "storage";

static sdmmc_card_t *s_card;
static sdmmc_card_t *s_unreadable; // card present, but without a file system FATFS can read
static sd_pwr_ctrl_handle_t s_pwr;
static bool s_usb_ready;

static esp_err_t storage_finish_mount(sdmmc_card_t *card);
static bool card_unformatted(void);

esp_err_t storage_mount(void)
{
    if (s_card) {
        if (card_unformatted()) return tinyusb_msc_storage_mount(STORAGE_MOUNT_POINT) == ESP_OK ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
        return ESP_OK;
    }
    if (s_unreadable) {
        // "Try again": the card may have been formatted on the computer meanwhile.
        if (tinyusb_msc_storage_mount(STORAGE_MOUNT_POINT) != ESP_OK) return ESP_ERR_NOT_SUPPORTED;
        return storage_finish_mount(s_unreadable);
    }
    // On eject from the computer, the USB component remounts at the Kconfig path, not ours.
    if (strcmp(CONFIG_TINYUSB_MSC_MOUNT_PATH, STORAGE_MOUNT_POINT) != 0) {
        ESP_LOGE(TAG, "CONFIG_TINYUSB_MSC_MOUNT_PATH (%s) differs from %s: the card disappears after eject",
                 CONFIG_TINYUSB_MSC_MOUNT_PATH, STORAGE_MOUNT_POINT);
    }

    sd_pwr_ctrl_ldo_config_t ldo_cfg = {.ldo_chan_id = SD_LDO_CHANNEL};
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_pwr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "card LDO: %s", esp_err_to_name(err));
        return err;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0; // fixed pins 39-44; slot 1 belongs to the ESP32-C6
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.pwr_ctrl_handle = s_pwr;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;

    sdmmc_card_t *card = calloc(1, sizeof(*card));
    if (!card) return ESP_ERR_NO_MEM;

    err = host.init();
    if (err == ESP_OK) err = sdmmc_host_init_slot(host.slot, &slot);
    if (err == ESP_OK) err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "card: %s", esp_err_to_name(err));
        free(card);
        sd_pwr_ctrl_del_on_chip_ldo(s_pwr);
        s_pwr = NULL;
        return err;
    }

    // The USB drive component becomes the owner of the mount: it takes the card from the
    // device when the computer connects and gives it back when the computer ejects.
    const tinyusb_msc_sdmmc_config_t msc_cfg = {
        .card = card,
        .mount_config = {
            .format_if_mount_failed = false, // never erase recordings
            .max_files = 4,
            .allocation_unit_size = 16 * 1024,
        },
    };
    err = tinyusb_msc_storage_init_sdmmc(&msc_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb drive: %s", esp_err_to_name(err));
        return err;
    }
    err = tinyusb_msc_storage_mount(STORAGE_MOUNT_POINT);
    if (err != ESP_OK) {
        // E.g. exFAT or NTFS. Nothing is erased: the screen offers to format, with confirmation.
        ESP_LOGW(TAG, "card has no recognized file system (%s)", esp_err_to_name(err));
        s_unreadable = card;
        return ESP_ERR_NOT_SUPPORTED;
    }
    return storage_finish_mount(card);
}

// Card mounted: store it and enable USB drive mode (only once).
static esp_err_t storage_finish_mount(sdmmc_card_t *card)
{
    esp_err_t err;
    s_card = card;
    s_unreadable = NULL;
    if (s_usb_ready) return ESP_OK;

    const tinyusb_config_t usb_cfg = {0}; // default descriptors from Kconfig
    err = tinyusb_driver_install(&usb_cfg);
    if (err == ESP_OK) {
        s_usb_ready = true;
    } else {
        // Without USB the device still records; only USB drive mode is unavailable.
        ESP_LOGW(TAG, "usb drive mode unavailable: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

// Never mounted; not handed to the computer: the remount after eject failed due to the format.
static bool card_unformatted(void)
{
    return s_card != NULL && tinyusb_msc_storage_in_use_by_usb_host() && tinyusb_msc_storage_unformatted();
}

bool storage_is_mounted(void)
{
    return s_card != NULL && !tinyusb_msc_storage_in_use_by_usb_host();
}

bool storage_in_use_by_host(void)
{
    return s_card != NULL && tinyusb_msc_storage_in_use_by_usb_host() && !card_unformatted();
}

void storage_usb_enable(bool enable)
{
    if (!s_usb_ready) return;
    if (enable) {
        tud_connect();
    } else {
        tud_disconnect();
    }
}

bool storage_space_mb(uint32_t *total_mb, uint32_t *free_mb)
{
    uint64_t total = 0, free_bytes = 0;
    if (!storage_is_mounted() || esp_vfs_fat_info(STORAGE_MOUNT_POINT, &total, &free_bytes) != ESP_OK) return false;
    if (total_mb) *total_mb = (uint32_t)(total / (1024 * 1024));
    if (free_mb) *free_mb = (uint32_t)(free_bytes / (1024 * 1024));
    return true;
}

// Creates a new FAT (FAT32 on cards above 2 GB; exFAT is not enabled) on the card,
// which must not be mounted.
static esp_err_t make_fat(sdmmc_card_t *card)
{
    BYTE pdrv = FF_DRV_NOT_USED;
    esp_err_t err = ff_diskio_get_drive(&pdrv);
    if (err != ESP_OK) return err;
    ff_diskio_register_sdmmc(pdrv, card);
    char drv[3] = {(char)('0' + pdrv), ':', 0};
    const size_t work_len = 4096;
    void *work = malloc(work_len);
    if (work) {
        const MKFS_PARM opt = {.fmt = FM_ANY, .au_size = 32 * 1024};
        FRESULT fr = f_mkfs(drv, &opt, work, work_len);
        free(work);
        if (fr != FR_OK) {
            ESP_LOGE(TAG, "f_mkfs: %d", (int)fr);
            err = ESP_FAIL;
        }
    } else {
        err = ESP_ERR_NO_MEM;
    }
    ff_diskio_unregister(pdrv);
    return err;
}

esp_err_t storage_format(void)
{
    if (s_unreadable) {
        // Unrecognized card: format and mount for the first time.
        sdmmc_card_t *card = s_unreadable;
        esp_err_t err = make_fat(card);
        if (err != ESP_OK) return err;
        err = tinyusb_msc_storage_mount(STORAGE_MOUNT_POINT);
        if (err != ESP_OK) return err;
        return storage_finish_mount(card);
    }
    if (card_unformatted()) {
        // Formatted with another file system by the computer and ejected: format and mount again.
        esp_err_t err = make_fat(s_card);
        if (err != ESP_OK) return err;
        return tinyusb_msc_storage_mount(STORAGE_MOUNT_POINT);
    }
    if (!storage_is_mounted()) return ESP_ERR_INVALID_STATE;

    // Takes the card from the device, formats it directly via FATFS and mounts it again.
    esp_err_t err = tinyusb_msc_storage_unmount();
    if (err != ESP_OK) return err;
    err = make_fat(s_card);
    esp_err_t mount_err = tinyusb_msc_storage_mount(STORAGE_MOUNT_POINT);
    return err != ESP_OK ? err : mount_err;
}

bool storage_unreadable(void)
{
    return s_unreadable != NULL || card_unformatted();
}

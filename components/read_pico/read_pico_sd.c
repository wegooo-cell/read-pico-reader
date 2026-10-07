/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDMMC 探测、挂载、格式化。
 *
 * SDMMC probe, mount, and format.
 */

#include "read_pico_sd.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "sdmmc_cmd.h"

#define SD_MOUNT_POINT "/sdcard"
#define SD_PIN_CLK GPIO_NUM_38
#define SD_PIN_CMD GPIO_NUM_42
#define SD_PIN_D0 GPIO_NUM_44

static const char* TAG = "sd_card";
static sdmmc_card_t* card;
static int probe_state;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool media_invalidated;
static read_pico_sd_info_t cached_info;

// 失效只影响快照，消费者释放句柄前不得卸载卡。/ Invalidation changes snapshots only; consumers must close handles before unmount.
static void observe_media_locked(bool present) {
    bool inserted = present && !cached_info.present;
    if (!present && (cached_info.mounted || probe_state == 1)) media_invalidated = true;
    // 首次开机无卡时没有文件句柄；后插卡可重新探测。
    // A card inserted after an empty boot has no stale handles and can be probed.
    if (inserted && probe_state == 2 && !media_invalidated && !cached_info.mounted) {
        probe_state = 0;
        cached_info.error = ESP_ERR_INVALID_STATE;
    }
    cached_info.present = present;
    if (!present || media_invalidated) {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = present;
        cached_info.error = present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }
}

static void publish_info(const read_pico_sd_info_t* info) {
    bool present = read_pico_sd_present();
    portENTER_CRITICAL(&state_lock);
    cached_info = *info;
    observe_media_locked(present);
    probe_state = 2;
    portEXIT_CRITICAL(&state_lock);
}

static void fill_info(read_pico_sd_info_t* info, esp_err_t mount_err) {
    memset(info, 0, sizeof(*info));
    info->present = read_pico_sd_present();
    info->error = mount_err;
    info->needs_format = info->present && !info->mounted
        && mount_err != ESP_OK
        && mount_err != ESP_ERR_TIMEOUT
        && mount_err != ESP_ERR_NOT_FOUND
        && mount_err != ESP_ERR_NOT_FINISHED;
    if (mount_err != ESP_OK || card == NULL) return;

    info->mounted = true;
    info->needs_format = false;
    strlcpy(info->name, card->cid.name, sizeof(info->name));
    info->capacity_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    uint64_t total_bytes = 0;
    info->error = esp_vfs_fat_info(
        SD_MOUNT_POINT, &total_bytes, &info->free_bytes
    );
}

static void ensure_media_dirs(void) {
    if (mkdir("/sdcard/assets", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir assets: %d", errno);
    }
    if (mkdir("/sdcard/assets/fonts", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir assets/fonts: %d", errno);
    }
    if (mkdir("/sdcard/fonts", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir fonts: %d", errno);
    }
    if (mkdir("/sdcard/books", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir books: %d", errno);
    }
    if (mkdir("/sdcard/pictures", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir pictures: %d", errno);
    }
}

static esp_err_t mount_card(bool format_if_failed) {
    if (card != NULL) return ESP_OK;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // 40MHz 在此卡上连续写入会间歇性总线超时（sdmmc_write_blocks 0x101），
    // 首选降到 20MHz 官方默认值换稳定性；挂载失败仍可经下方降速重试兜底。
    // / 40 MHz intermittently times out on this card under sustained writes
    // / (sdmmc_write_blocks 0x101); prefer the 20 MHz default, with the slower
    // / retry loop below as a further fallback.
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_PIN_CLK;
    slot.cmd = SD_PIN_CMD;
    slot.d0 = SD_PIN_D0;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_failed,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    // 高容量卡对高速协商及信号裕量更敏感；失败后降速重试，绝不在探测时格式化。
    // Retry high-capacity cards at slower bus clocks; probing never formats media.
    const int clocks[] = {SDMMC_FREQ_DEFAULT, 10000};
    esp_err_t err = ESP_FAIL;
    for (size_t attempt = 0; attempt < sizeof(clocks) / sizeof(clocks[0]); ++attempt) {
        if (!read_pico_sd_present()) return ESP_ERR_NOT_FOUND;
        host.max_freq_khz = clocks[attempt];
        card = NULL;
        err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &card);
        if (err == ESP_OK) break;
        if (err == ESP_ERR_NO_MEM || err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE) break;
        ESP_LOGW(TAG, "SD probe at %d kHz failed: %s; retry slower", clocks[attempt], esp_err_to_name(err));
        card = NULL;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (err != ESP_OK) {
        card = NULL;
        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(err));
    } else ensure_media_dirs();
    return err;
}

static esp_err_t close_card(void) {
    if (card != NULL) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unmount %s", esp_err_to_name(err));
            return err;
        }
        card = NULL;
    }
    return ESP_OK;
}

static void probe_task(void* arg) {
    (void)arg;
    read_pico_sd_info_t info = { 0 };
    info.present = read_pico_sd_present();
    if (!info.present) {
        info.error = ESP_ERR_NOT_FOUND;
        publish_info(&info);
        ESP_LOGI(TAG, "SD_CD absent, skip mount");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = mount_card(false);
    fill_info(&info, err);
    if (err == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Mounted %s, capacity=%llu MB, free=%llu MB",
            info.name,
            (unsigned long long)(info.capacity_bytes / (1024 * 1024)),
            (unsigned long long)(info.free_bytes / (1024 * 1024))
        );
    }
    publish_info(&info);
    if (info.needs_format) {
        ESP_LOGW(TAG, "SD present but no FAT, ask user to format");
    }
    vTaskDelete(NULL);
}

esp_err_t read_pico_sd_start_probe(void) {
    bool present = read_pico_sd_present();
    portENTER_CRITICAL(&state_lock);
    observe_media_locked(present);
    esp_err_t err = ESP_OK;
    if (probe_state == 1) err = ESP_ERR_NOT_FINISHED;
    else if (media_invalidated) err = cached_info.error;
    else if (probe_state == 2) err = cached_info.mounted ? ESP_OK :
        cached_info.error != ESP_OK ? cached_info.error : ESP_FAIL;
    else if (!present) {
        probe_state = 2;
        err = ESP_ERR_NOT_FOUND;
    } else {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = true;
        cached_info.error = ESP_ERR_NOT_FINISHED;
        probe_state = 1;
    }
    bool start = err == ESP_OK && probe_state == 1;
    portEXIT_CRITICAL(&state_lock);
    if (!start) return err;
    BaseType_t created = xTaskCreate(
        probe_task, "sd_probe", 4096, NULL, 5, NULL
    );
    if (created != pdPASS) {
        portENTER_CRITICAL(&state_lock);
        probe_state = 0;
        cached_info.error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info) {
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    bool present = read_pico_sd_present();
    portENTER_CRITICAL(&state_lock);
    observe_media_locked(present);
    *info = cached_info;
    esp_err_t err = !present || media_invalidated ? info->error :
        probe_state == 0 ? ESP_ERR_INVALID_STATE :
        probe_state == 1 ? ESP_ERR_NOT_FINISHED : info->error;
    portEXIT_CRITICAL(&state_lock);
    return err;
}

// 驱动操作期间占用忙状态，不在临界区执行 I/O。/ Reserve busy state across driver I/O outside the critical section.
static bool begin_operation(void) {
    portENTER_CRITICAL(&state_lock);
    bool ready = probe_state != 1;
    if (ready) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}

esp_err_t read_pico_sd_remount(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err = close_card();
    if (err != ESP_OK) {
        read_pico_sd_info_t info = { .present = read_pico_sd_present(), .error = err };
        publish_info(&info);
        return err;
    }
    portENTER_CRITICAL(&state_lock);
    memset(&cached_info, 0, sizeof(cached_info));
    media_invalidated = false;
    probe_state = 0;
    portEXIT_CRITICAL(&state_lock);
    return read_pico_sd_start_probe();
}

esp_err_t read_pico_sd_sync(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err = close_card();
    if (err != ESP_OK) {
        read_pico_sd_info_t info = { .present = read_pico_sd_present(), .mounted = true, .error = err };
        publish_info(&info);
        return err;
    }
    read_pico_sd_info_t info = { .error = ESP_ERR_INVALID_STATE };
    publish_info(&info);
    return ESP_OK;
}

esp_err_t read_pico_sd_format(void) {
    read_pico_sd_info_t current;
    (void)read_pico_sd_get_info(&current);
    portENTER_CRITICAL(&state_lock);
    bool invalid = media_invalidated;
    bool busy = probe_state == 1;
    if (!invalid && !busy) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    if (invalid) return current.present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    if (busy) return ESP_ERR_NOT_FINISHED;
    if (!read_pico_sd_present()) {
        read_pico_sd_info_t info = { .error = ESP_ERR_NOT_FOUND };
        publish_info(&info);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;
    if (card == NULL) {
        err = mount_card(true);
    } else {
        err = esp_vfs_fat_sdcard_format(SD_MOUNT_POINT, card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "format %s", esp_err_to_name(err));
        }
    }

    read_pico_sd_info_t info = { 0 };
    fill_info(&info, err);
    if (err == ESP_OK) {
        ensure_media_dirs();
        fill_info(&info, ESP_OK);
        ESP_LOGI(TAG, "formatted %s", info.name);
    }
    publish_info(&info);
    return err;
}

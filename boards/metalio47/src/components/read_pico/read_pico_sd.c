/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDMMC 探测、挂载、格式化。
 *
 * SDMMC probe, mount, and format.
 * S31无CD脚：CMD13健康确认后释放消费者，异步恢复；USB/sleep卸载不触发恢复。
 * S31 has no CD pin: confirm CMD13 loss, close consumers, then recover asynchronously; intentional unmounts remain suspended.
 */

#include "read_pico_sd.h"
#include "read_pico_sd_access.h"
#include "read_pico_sd_msc.h"
#include "metalio_hw.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "diskio_sdmmc.h"
#include "esp_attr.h"
#include "esp_system.h"

#define SD_MOUNT_POINT "/sdcard"
#define SD_PIN_CLK GPIO_NUM_24
#define SD_PIN_CMD GPIO_NUM_25
#define SD_PIN_D0 GPIO_NUM_20

static const char* TAG = "sd_card";
static sdmmc_card_t* card;
static void* sd_dma_buffer;
static sdmmc_card_t *usb_card;
static sdmmc_host_t usb_host;
static _Atomic bool usb_owned;
static bool usb_host_ready;

// Follow Metalio's S31 storage reference: keep SD DMA in internal RAM.
// External application data is copied by the SDK through one bounded bounce buffer.
// Reference: CloudZao/Metalio-E-INK4-Plus, SdCardManager::PrepareDmaBuffer.
static bool sd_internal_dma_buffer(int slot, const void* buffer, size_t bytes) {
    return esp_ptr_internal(buffer) && sdmmc_host_check_buffer_alignment(slot, buffer, bytes);
}

static esp_err_t prepare_sd_dma(sdmmc_host_t* host) {
    const uint32_t caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    size_t alignment = 0;
    esp_err_t err = esp_cache_get_alignment(caps, &alignment);
    if (err != ESP_OK) return err;
    if (alignment < 4) alignment = 4;
    sd_dma_buffer = heap_caps_aligned_alloc(alignment, 4096, caps);
    if (!sd_dma_buffer) return ESP_ERR_NO_MEM;
    host->flags &= ~SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    host->check_buffer_alignment = sd_internal_dma_buffer;
    host->dma_aligned_buffer = sd_dma_buffer;
    host->unaligned_multi_block_rw_max_chunk_size = 8;
    return ESP_OK;
}

// SDK通过flag区分全局与按slot的清理回调；不能混用函数签名。
// The SDK flag selects global versus per-slot teardown; preserve the callback ABI.
static esp_err_t deinit_sd_host(const sdmmc_host_t *host) {
    return host->flags&SDMMC_HOST_FLAG_DEINIT_ARG?host->deinit_p(host->slot):host->deinit();
}

static void release_sd_dma(void) {
    heap_caps_free(sd_dma_buffer);
    sd_dma_buffer = NULL;
}
static int probe_state;
static bool filesystem_unreadable;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool media_invalidated;
static read_pico_sd_info_t cached_info = {.error = ESP_ERR_INVALID_STATE};
static bool space_busy, health_busy, suspended, mounted_once;
static bool manual_busy;
static unsigned health_failures;
static int64_t health_due_ms, health_failed_ms;

typedef struct { int khz, width; } sd_profile_t;
static const sd_profile_t sd_profiles[] = {
    {40000,4},{40000,4},{20000,4},{10000,4},
    {5000,4},{1000,4},{5000,1},{1000,1},
};
#define SD_PROFILE_COUNT (sizeof(sd_profiles)/sizeof(sd_profiles[0]))
#define SD_PROFILE_COOKIE UINT32_C(0x53445000)
static RTC_DATA_ATTR uint32_t s_sd_sleep_profile;
static int s_sd_last_profile=-1;
static bool s_sd_profile_loaded;

// 只在本次运行/深睡RTC中记忆成功档位；冷启/异常复位重试原高速，不写用户NVS。
// Keep successful profiles in RAM/deep-sleep RTC only; cold/abnormal resets start high without NVS writes.
static size_t sd_preferred_profile(void) {
    portENTER_CRITICAL(&state_lock);
    if(!s_sd_profile_loaded) {
        unsigned index=s_sd_sleep_profile&255u;
        if(esp_reset_reason()==ESP_RST_DEEPSLEEP &&
            (s_sd_sleep_profile&UINT32_C(0xffffff00))==SD_PROFILE_COOKIE && index<SD_PROFILE_COUNT)
            s_sd_last_profile=(int)index;
        else s_sd_sleep_profile=0;
        s_sd_profile_loaded=true;
    }
    size_t result=s_sd_last_profile<0?0:(size_t)s_sd_last_profile;
    portEXIT_CRITICAL(&state_lock);
    return result;
}
static void sd_remember_profile(size_t index) {
    if(index==1)index=0;
    portENTER_CRITICAL(&state_lock);
    s_sd_last_profile=(int)index;
    s_sd_sleep_profile=SD_PROFILE_COOKIE|(uint32_t)index;
    portEXIT_CRITICAL(&state_lock);
}

// 失效只影响快照，消费者释放句柄前不得卸载卡。/ Invalidation changes snapshots only; consumers must close handles before unmount.
static void observe_media_locked(bool present) {
    if(!present){s_sd_last_profile=-1;s_sd_sleep_profile=0;}
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
    bool present = false;
    esp_err_t detect_err = read_pico_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    cached_info = *info;
    if (info->mounted) {
        mounted_once = true;
        health_failures = 0;
        health_due_ms = esp_timer_get_time() / 1000 + 2000;
    }
    // 已经确认的拔卡失效不会被后续通信错误或探测成功覆盖。
    // A confirmed removal latch survives later detection faults and probe success.
    if (detect_err == ESP_OK || media_invalidated)
        observe_media_locked(detect_err == ESP_OK ? present : cached_info.present);
    probe_state = 2;
    manual_busy = false;
    read_pico_sd_access_set_mounted(cached_info.mounted && !media_invalidated && !suspended);
    portEXIT_CRITICAL(&state_lock);
}

static esp_err_t fill_space(read_pico_sd_info_t *info) {
    uint64_t total = 0, available = 0;
    esp_err_t err = esp_vfs_fat_info(SD_MOUNT_POINT, &total, &available);
    // 出错或损坏的计数不等于「卡已满」，也不能发生无符号减法下溢。
    // Failed/corrupt counters never mean a full card, and must not underflow subtraction.
    if (err == ESP_OK && (!total || available > total || total > info->capacity_bytes)) err = ESP_FAIL;
    info->space_valid = err == ESP_OK;
    info->space_error = err;
    info->filesystem_bytes = err == ESP_OK ? total : 0;
    info->free_bytes = err == ESP_OK ? available : 0;
    return err;
}

// FAT32 的 FSInfo 可能仍是电脑上旧的计数。每次挂载仅一次作废它，首轮统计只读扫描 FAT。
// Invalidate possibly stale FAT32 FSInfo once per mount; the first read-only FAT scan then counts allocations.
static void invalidate_mount_space(void) {
    BYTE drive = ff_diskio_get_pdrv_card(card);
    if (drive == 0xff) return;
    char path[12]; snprintf(path,sizeof(path),"%u:",(unsigned)drive);
    FF_DIR root;
    if (f_opendir(&root,path) != FR_OK) return;
    FATFS *fs = root.obj.fs;
    (void)f_closedir(&root);
    if (!fs) return;
#if FF_FS_REENTRANT
    if (!ff_mutex_take(fs->ldrv)) return;
#endif
    fs->free_clst = UINT32_MAX;
#if FF_FS_REENTRANT
    ff_mutex_give(fs->ldrv);
#endif
}

static void fill_info(read_pico_sd_info_t* info, esp_err_t mount_err) {
    memset(info, 0, sizeof(*info));
    bool present = false;
    esp_err_t detect_err = read_pico_sd_detect(&present);
    // 成功挂载是有卡的证据；检测通信失败不覆盖挂载结果。
    // A successful mount proves presence; failed detection must not override it.
    info->present = detect_err == ESP_OK ? present : mount_err != ESP_ERR_NOT_FOUND;
    info->error = mount_err;
    // 挂载错误本身不证明文件系统问题；仅只读识别到 exFAT 或空白卡才提示。
    // Mount errors alone are inconclusive; only read-only confirmation of exFAT/blank media enables the hint.
    info->needs_format = info->present && filesystem_unreadable;
    if (mount_err != ESP_OK || card == NULL) return;

    info->mounted = true;
    info->needs_format = false;
    strlcpy(info->name, card->cid.name, sizeof(info->name));
    info->capacity_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    // 空间统计失败不覆盖「挂载成功」，仍允许正常读取文件。
    // Allocation-query failure must not turn a mounted filesystem into a mount failure.
    // 挂载就绪不等待整张FAT扫描；文件管理按需异步统计。
    // Publish mount readiness without a full FAT walk; file management accounts asynchronously.
    info->space_error = ESP_ERR_NOT_FINISHED;
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

// 只识别明确不支持的文件系统，不写卡，也不根据通用 ESP_FAIL 猜测。
// Identify known unsupported media without writing or interpreting generic ESP_FAIL as a filesystem error.
static bool unsupported_boot_sector(const uint8_t *sector) {
    if (!memcmp(sector + 3, "EXFAT   ", 8)) return true;
    for (unsigned i = 0; i < 512; ++i) if (sector[i]) return false;
    return true;
}
static bool probe_unsupported_filesystem(sdmmc_host_t host, const sdmmc_slot_config_t *slot) {
    sdmmc_card_t *probe = calloc(1, sizeof(*probe));
    uint8_t *sector = heap_caps_malloc(512, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    bool owned = false, unsupported = false;
    if (!probe || !sector) goto done;
    host.max_freq_khz = 10000;
    if (host.init() != ESP_OK) goto done; // 不接管其他已初始化会话。/ Never take over another initialized session.
    owned = true;
    if (sdmmc_host_init_slot(host.slot, slot) != ESP_OK || sdmmc_card_init(&host, probe) != ESP_OK ||
        probe->csd.sector_size != 512 || sdmmc_read_sectors(probe, sector, 0, 1) != ESP_OK) goto done;
    unsupported = unsupported_boot_sector(sector);
    if (!unsupported && sector[510] == 0x55 && sector[511] == 0xaa) {
        uint32_t starts[4] = {0};
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *entry = sector + 446 + 16 * i;
            if (!entry[4]) continue;
            starts[i] = (uint32_t)entry[8] | (uint32_t)entry[9] << 8 | (uint32_t)entry[10] << 16 | (uint32_t)entry[11] << 24;
        }
        for (unsigned i = 0; i < 4 && !unsupported; ++i) {
            if (!starts[i] || starts[i] >= probe->csd.capacity) continue;
            if (sdmmc_read_sectors(probe, sector, starts[i], 1) != ESP_OK) goto done;
            // 分区必须明确标识 exFAT；普通 FAT 或未知损坏不建议格式化。
            // A partition must explicitly identify exFAT; do not suggest formatting FAT or unknown corruption.
            unsupported = !memcmp(sector + 3, "EXFAT   ", 8);
        }
    }
done:
    if (owned) (void)deinit_sd_host(&host);
    free(probe); heap_caps_free(sector);
    return unsupported;
}

// 成功挂载后，先验证真实扇区和FAT根目录，再允许字体/书籍访问。
// Verify actual sector IO and the FAT root before publishing a mount to font/book consumers.
static esp_err_t verify_mounted_card(void) {
    esp_err_t err=sdmmc_get_status(card);
    if (err != ESP_OK) return err;
    if (card->csd.sector_size != 512) return ESP_ERR_NOT_SUPPORTED;
    err=sdmmc_read_sectors(card,sd_dma_buffer,0,1);
    if (err != ESP_OK) return err;
    BYTE drive=ff_diskio_get_pdrv_card(card);
    if (drive == 0xff) return ESP_ERR_INVALID_STATE;
    char path[16];snprintf(path,sizeof(path),"%u:",(unsigned)drive);
    FF_DIR root;
    if (f_opendir(&root,path) != FR_OK) return ESP_FAIL;
    FILINFO entry;
    int read_error=f_readdir(&root,&entry);
    int close_error=f_closedir(&root);
    return read_error == FR_OK && close_error == FR_OK ? ESP_OK : ESP_FAIL;
}

// 普通挂载和MSC共用引脚、位宽、DMA与成功档；USB时钟上限仍20MHz。
// VFS and MSC share pins, width, DMA and known-good profiles; USB retains a 20MHz ceiling.
static void configure_sd_bus(sdmmc_host_t *host,sdmmc_slot_config_t *slot,sd_profile_t profile,bool usb) {
    *slot=(sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    host->slot=SDMMC_HOST_SLOT_0;
    host->max_freq_khz=usb && profile.khz>20000?20000:profile.khz;
    slot->width=profile.width;
    slot->clk=SD_PIN_CLK;slot->cmd=SD_PIN_CMD;slot->d0=SD_PIN_D0;
    slot->d1=profile.width==4?GPIO_NUM_21:GPIO_NUM_NC;
    slot->d2=profile.width==4?GPIO_NUM_22:GPIO_NUM_NC;
    slot->d3=profile.width==4?GPIO_NUM_23:GPIO_NUM_NC;
    slot->cd=slot->wp=GPIO_NUM_NC;
    slot->flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    if(profile.width==1) {
        host->flags&=~(SDMMC_HOST_FLAG_4BIT|SDMMC_HOST_FLAG_8BIT);
        host->flags|=SDMMC_HOST_FLAG_1BIT;
    }
}

static esp_err_t mount_card_owned(bool format_if_failed) {
    filesystem_unreadable = false;
    if (card != NULL) return verify_mounted_card();

    metalio_sd_release_hold();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    esp_err_t dma_error = prepare_sd_dma(&host);
    if (dma_error != ESP_OK) {
        metalio_usb_serial_reconnect();
        return dma_error;
    }
    // S31原四线高速优先；慢卡复用成功档，失败再降频/改单线。
    // Prefer native four-bit high speed, reusing good profiles before lower clocks/single-line fallback.
    sdmmc_slot_config_t slot;
    configure_sd_bus(&host,&slot,sd_profiles[0],false);

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_failed,
        .max_files = 16,
        .allocation_unit_size = 16 * 1024,
    };

    // 冷启动先等待并重试高速，再降速；CMD6 忙也可能返回 INVALID_STATE，不得直接放弃。
    // Settle/retry high speed before lowering clocks; CMD6 busy may return INVALID_STATE and must retry.
    const size_t first=sd_preferred_profile();
    const int native_flags=host.flags;
    esp_err_t err = ESP_FAIL;
    bool profile_pin_error=false;
    for (size_t attempt = 0; attempt < SD_PROFILE_COUNT; ++attempt) {
        // 记忆档失败后先试更保守档，再有界绕回其余档；换卡也不会困在旧单线档。
        // After the hint fails, try safer profiles then wrap once; changed cards are not trapped in an old mode.
        size_t index=(first+attempt)%SD_PROFILE_COUNT;
        sd_profile_t profile=sd_profiles[index];
        bool present = false;
        esp_err_t detect_err = read_pico_sd_detect(&present);
        if (detect_err == ESP_OK && !present) {
            err = ESP_ERR_NOT_FOUND;
            break;
        }
        if (detect_err != ESP_OK) ESP_LOGW(TAG, "SD detect unavailable: %s; try card I/O", esp_err_to_name(detect_err));
        host.flags=native_flags;
        configure_sd_bus(&host,&slot,profile,false);
        if(profile.width==1) {
            err=metalio_sd_single_line_idle();
            if(err!=ESP_OK){profile_pin_error=true;break;}
        }
        card = NULL;
        err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &card);
        if (err == ESP_OK) {
            err=verify_mounted_card();
            if (err != ESP_OK) {
                // 尚未发布给消费者：安全卸载后继续降频，不记忆未验证的档位。
                // No consumers admitted yet: unmount before fallback, never remember an unverified profile.
                esp_err_t close_error=esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT,card);
                if (close_error != ESP_OK) {
                    metalio_usb_serial_reconnect();
                    return close_error; // 保留card/DMA所有权供恢复。/ Retain card/DMA ownership for recovery.
                }
                card=NULL;
            }
        }
        if (err == ESP_OK) {
            sd_remember_profile(index);
            ESP_LOGI(TAG,"SD mounted profile: %d kHz/%d-bit preferred=%u attempt=%u",
                profile.khz,profile.width,(unsigned)first,(unsigned)attempt+1);
            break;
        }
        if (err == ESP_ERR_NO_MEM || err == ESP_ERR_INVALID_ARG) break;
        ESP_LOGW(TAG, "SD probe %u at %d kHz/%d-bit failed: %s", (unsigned)attempt + 1,
                 profile.khz,profile.width,esp_err_to_name(err));
        card = NULL;
        // VFS mount 在失败时只清理自己建立的 host；不额外 deinit 别的存储会话。
        // Failed VFS mount cleans up its own host; never forcibly deinit another storage session.
        if (attempt + 1 < SD_PROFILE_COUNT) vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (err != ESP_OK) {
        card = NULL;
        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(err));
        if (!format_if_failed && !profile_pin_error && err == ESP_FAIL) filesystem_unreadable = probe_unsupported_filesystem(host, &slot);
        release_sd_dma();
    } else {
        invalidate_mount_space();
        ensure_media_dirs();
    }
    // 参考power_hw.cc：必须在SD/CNNT恢复完成后再软重连USJ，失败探测也恢复。
    // Per reference power_hw.cc, reconnect USJ after SD/CNNT settles, including failed probes.
    metalio_usb_serial_reconnect();
    return err;
}

static esp_err_t mount_card(bool format_if_failed) {
    if(usb_owned || !read_pico_sd_access_transition_begin(1000))return ESP_ERR_NOT_FINISHED;
    esp_err_t err=mount_card_owned(format_if_failed);
    if(err!=ESP_OK)read_pico_sd_access_set_mounted(false);
    read_pico_sd_access_transition_end();
    return err;
}

static esp_err_t close_card_owned(void) {
    if (card != NULL) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unmount %s", esp_err_to_name(err));
            return err;
        }
        card = NULL;
        release_sd_dma();
    }
    return ESP_OK;
}

// 暂时有访问未归还时恢复原快照，不能伪报卡已失效。/ Busy admission preserves the prior snapshot instead of inventing media loss.
static void abort_operation(void) {
    portENTER_CRITICAL(&state_lock);
    probe_state=cached_info.mounted?2:0;manual_busy=false;
    if(cached_info.error==ESP_ERR_NOT_FINISHED)cached_info.error=ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL(&state_lock);
}

static esp_err_t close_card(void) {
    if(usb_owned || !read_pico_sd_access_transition_begin(1000))return ESP_ERR_NOT_FINISHED;
    esp_err_t err=close_card_owned();
    if(err==ESP_OK)read_pico_sd_access_set_mounted(false);
    read_pico_sd_access_transition_end();
    return err;
}

// 原始卡资源始终归驱动；TinyUSB仅借用，销毁完成前不得归还DMA或host。
// Driver owns raw resources; TinyUSB borrows them until teardown completes.
static esp_err_t release_usb_host(void) {
    if(usb_host_ready) {
        esp_err_t err=deinit_sd_host(&usb_host);
        if(err!=ESP_OK)return err;
        usb_host_ready=false;
    }
    free(usb_card);usb_card=NULL;
    return ESP_OK;
}
esp_err_t read_pico_sd_usb_release(void) {
    if(!read_pico_sd_access_transition_begin(1000))return ESP_ERR_NOT_FINISHED;
    esp_err_t err=release_usb_host();
    if(err==ESP_OK && usb_owned){release_sd_dma();usb_owned=false;}
    read_pico_sd_access_transition_end();
    return err;
}
esp_err_t read_pico_sd_usb_acquire(sdmmc_card_t **out) {
    if(!out)return ESP_ERR_INVALID_ARG;
    *out=NULL;
    if(usb_owned)return ESP_ERR_INVALID_STATE;
    esp_err_t err=read_pico_sd_sync();
    if(err!=ESP_OK)return err;
    if(!read_pico_sd_access_transition_begin(1000))return ESP_ERR_NOT_FINISHED;
    usb_owned=true;
    usb_host=(sdmmc_host_t)SDMMC_HOST_DEFAULT();
    err=prepare_sd_dma(&usb_host);
    const size_t first=sd_preferred_profile();
    const int native_flags=usb_host.flags;
    for(size_t attempt=0;err==ESP_OK && attempt<SD_PROFILE_COUNT;attempt++) {
        size_t index=(first+attempt)%SD_PROFILE_COUNT;
        sdmmc_slot_config_t slot;
        usb_host.flags=native_flags;
        configure_sd_bus(&usb_host,&slot,sd_profiles[index],true);
        metalio_sd_release_hold();
        if(slot.width==1)err=metalio_sd_single_line_idle();
        if(err==ESP_OK) {
            err=usb_host.init();
            if(err==ESP_OK)usb_host_ready=true;
            else break; // 未取得host所有权，不能强制deinit。/ Never deinit a host we did not acquire.
        }
        if(err==ESP_OK)err=sdmmc_host_init_slot(usb_host.slot,&slot);
        if(err==ESP_OK){usb_card=calloc(1,sizeof(*usb_card));if(!usb_card)err=ESP_ERR_NO_MEM;}
        if(err==ESP_OK)err=sdmmc_card_init(&usb_host,usb_card);
        if(err==ESP_OK)err=sdmmc_get_status(usb_card);
        if(err==ESP_OK){*out=usb_card;break;}
        ESP_LOGW(TAG,"USB SD probe %u at %d kHz/%d-bit failed: %s",
            (unsigned)attempt+1,usb_host.max_freq_khz,slot.width,esp_err_to_name(err));
        esp_err_t cleanup=release_usb_host();
        if(cleanup!=ESP_OK){err=cleanup;break;}
        if(err==ESP_ERR_NO_MEM || err==ESP_ERR_INVALID_ARG || attempt+1==SD_PROFILE_COUNT)break;
        vTaskDelay(pdMS_TO_TICKS(250));
        err=ESP_OK;
    }
    if(!*out) {
        esp_err_t cleanup=release_usb_host();
        if(cleanup==ESP_OK){release_sd_dma();usb_owned=false;}
        else err=cleanup;
    }
    read_pico_sd_access_transition_end();
    return err;
}

static void probe_task(void* arg) {
    (void)arg;
    read_pico_sd_info_t info = { 0 };
    esp_err_t detect_err = read_pico_sd_detect(&info.present);
    if (detect_err == ESP_OK && !info.present) {
        info.error = ESP_ERR_NOT_FOUND;
        publish_info(&info);
        ESP_LOGI(TAG, "SD_CD absent, skip mount");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = mount_card(false);
    if(err==ESP_ERR_NOT_FINISHED){abort_operation();vTaskDelete(NULL);return;}
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
    vTaskDelete(NULL);
}

esp_err_t read_pico_sd_start_probe(void) {
    bool present = true;
    esp_err_t detect_err = read_pico_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    if (detect_err == ESP_OK) observe_media_locked(present);
    esp_err_t err = ESP_OK;
    if (suspended) err = ESP_ERR_INVALID_STATE;
    else if (probe_state == 1 || space_busy || health_busy) err = ESP_ERR_NOT_FINISHED;
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
    bool present = false;
    esp_err_t detect_err = read_pico_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    if (detect_err == ESP_OK) observe_media_locked(present);
    if(media_invalidated)read_pico_sd_access_set_mounted(false);
    *info = cached_info;
    esp_err_t err = manual_busy ? ESP_ERR_NOT_FINISHED :
        probe_state == 0 && detect_err != ESP_OK && !info->present ? detect_err :
        !info->present || media_invalidated ? info->error :
        probe_state == 0 ? ESP_ERR_INVALID_STATE :
        probe_state == 1 ? ESP_ERR_NOT_FINISHED : info->error;
    portEXIT_CRITICAL(&state_lock);
    return err;
}

// 驱动操作期间占用忙状态，不在临界区执行 I/O。/ Reserve busy state across driver I/O outside the critical section.
static bool begin_operation(void) {
    portENTER_CRITICAL(&state_lock);
    bool ready = !usb_owned && probe_state != 1 && !space_busy && !health_busy;
    if (ready) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}

static bool begin_space_refresh(void) {
    portENTER_CRITICAL(&state_lock);
    bool ready=probe_state!=1&&!space_busy&&!health_busy&&cached_info.mounted&&!media_invalidated;
    if(ready)space_busy=true;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}
static esp_err_t finish_space_refresh(void) {
    read_pico_sd_info_t info;
    esp_err_t err=read_pico_sd_get_info(&info);
    if(err==ESP_OK&&info.mounted){err=fill_space(&info);publish_info(&info);}
    portENTER_CRITICAL(&state_lock);space_busy=false;portEXIT_CRITICAL(&state_lock);
    return err;
}
esp_err_t read_pico_sd_refresh_space(void) {
    read_pico_sd_info_t info;
    esp_err_t err=read_pico_sd_get_info(&info);
    if(err!=ESP_OK||!info.mounted)return err!=ESP_OK?err:ESP_ERR_INVALID_STATE;
    if(!begin_space_refresh())return ESP_ERR_NOT_FINISHED;
    return finish_space_refresh();
}
static void space_task(void *arg) {
    (void)arg;(void)finish_space_refresh();vTaskDelete(NULL);
}
esp_err_t read_pico_sd_start_space_refresh(void) {
    read_pico_sd_info_t info;
    esp_err_t err=read_pico_sd_get_info(&info);
    if(err!=ESP_OK||!info.mounted)return err!=ESP_OK?err:ESP_ERR_INVALID_STATE;
    if(!begin_space_refresh())return ESP_ERR_NOT_FINISHED;
    // 与挂载忙状态分离，统计期间仍报告已挂载；重挂/USB交接须等统计释放卡。
    // Accounting never masquerades as an unmounted card; remount/USB handoff wait for this owner.
    if(xTaskCreate(space_task,"sd_space",4096,NULL,2,NULL)!=pdPASS){
        portENTER_CRITICAL(&state_lock);space_busy=false;portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t read_pico_sd_remount(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err = close_card();
    if(err==ESP_ERR_NOT_FINISHED){abort_operation();return err;}
    if (err != ESP_OK) {
        read_pico_sd_info_t info = { .present = true, .error = err };
        (void)read_pico_sd_detect(&info.present);
        publish_info(&info);
        return err;
    }
    portENTER_CRITICAL(&state_lock);
    memset(&cached_info, 0, sizeof(cached_info));
    media_invalidated = false;
    suspended = false; health_failures = 0;
    probe_state = 0;
    portEXIT_CRITICAL(&state_lock);
    return read_pico_sd_start_probe();
}

// 只读CMD13在后台确认总线掉线，不用整卡容量扫描；生命周期操作等待该任务退出。
// Background read-only CMD13 detects bus loss without a FAT scan; lifecycle operations wait for this owner.
static void health_task(void *arg) {
    (void)arg;
    esp_err_t err = sdmmc_get_status(card);
    int64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&state_lock);
    if (!media_invalidated && cached_info.mounted) {
        if (err == ESP_OK || err == ESP_ERR_NO_MEM || err == ESP_ERR_INVALID_ARG) {
            health_failures = 0; health_due_ms = now + 2000;
        }
        else {
            if (!health_failures++) health_failed_ms = now;
            health_due_ms = now + 500;
            if (health_failures >= 3 && now - health_failed_ms >= 400) {
                media_invalidated = true;
                memset(&cached_info, 0, sizeof(cached_info));
                cached_info.present = true; cached_info.error = err;
            }
        }
    }
    bool invalidated=media_invalidated;
    health_busy = false;
    portEXIT_CRITICAL(&state_lock);
    if(invalidated)read_pico_sd_access_set_mounted(false);
    vTaskDelete(NULL);
}
void read_pico_sd_poll_health(void) {
    int64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&state_lock);
    bool start = !suspended && card && cached_info.mounted && !media_invalidated &&
        probe_state != 1 && !space_busy && !health_busy && now >= health_due_ms;
    if (start) health_busy = true;
    portEXIT_CRITICAL(&state_lock);
    if (start && xTaskCreate(health_task,"sd_health",3072,NULL,1,NULL) != pdPASS) {
        portENTER_CRITICAL(&state_lock);
        health_busy = false; health_due_ms = now + 2000;
        portEXIT_CRITICAL(&state_lock);
    }
}
bool read_pico_sd_needs_recovery(void) {
    portENTER_CRITICAL(&state_lock);
    bool needed = !suspended && probe_state != 1 && !cached_info.mounted &&
        (media_invalidated || mounted_once || cached_info.present);
    portEXIT_CRITICAL(&state_lock);
    return needed;
}
void read_pico_sd_profile(int *khz,int *width) {
    portENTER_CRITICAL(&state_lock);
    bool ready=cached_info.mounted && !media_invalidated && !usb_owned && s_sd_last_profile>=0;
    sd_profile_t profile=ready?sd_profiles[s_sd_last_profile]:(sd_profile_t){0};
    if(khz)*khz=profile.khz;
    if(width)*width=profile.width;
    portEXIT_CRITICAL(&state_lock);
}
bool read_pico_sd_io_ready(void) {
    portENTER_CRITICAL(&state_lock);
    bool ready = !suspended && probe_state != 1 && cached_info.mounted && !media_invalidated;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}
esp_err_t read_pico_sd_invalidate_io(void) {
    portENTER_CRITICAL(&state_lock);
    if (probe_state == 1 || space_busy || health_busy) {
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_NOT_FINISHED;
    }
    // 调用方已停消费者；仅失效快照，异步任务随后按既有所有权规则重挂。
    // Consumers are closed; invalidate only the snapshot, then remount through the owned async task.
    suspended=false;media_invalidated=true;probe_state=2;
    read_pico_sd_access_set_mounted(false);
    memset(&cached_info,0,sizeof(cached_info));
    cached_info.present=true;cached_info.error=ESP_FAIL;
    portEXIT_CRITICAL(&state_lock);
    return ESP_OK;
}
static void recovery_task(void *arg) {
    const bool manual = arg != NULL;
    esp_err_t err = close_card();
    if(err==ESP_ERR_NOT_FINISHED){abort_operation();vTaskDelete(NULL);return;}
    if (err == ESP_OK) {
        portENTER_CRITICAL(&state_lock);
        media_invalidated = false; health_failures = 0;
        if (manual) suspended = false;
        memset(&cached_info,0,sizeof(cached_info));
        cached_info.present = true; cached_info.error = ESP_ERR_NOT_FINISHED;
        portEXIT_CRITICAL(&state_lock);
        err = mount_card(false);
    }
    if(err==ESP_ERR_NOT_FINISHED){abort_operation();vTaskDelete(NULL);return;}
    read_pico_sd_info_t info;
    fill_info(&info,err);
    publish_info(&info);
    ESP_LOGI(TAG,"%s recovery: %s",manual ? "manual" : "automatic",esp_err_to_name(err));
    vTaskDelete(NULL);
}
esp_err_t read_pico_sd_start_recovery(void) {
    if (!read_pico_sd_needs_recovery()) return ESP_ERR_INVALID_STATE;
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    if (xTaskCreate(recovery_task,"sd_recover",4096,NULL,3,NULL) == pdPASS) return ESP_OK;
    portENTER_CRITICAL(&state_lock);
    probe_state = 2;
    portEXIT_CRITICAL(&state_lock);
    return ESP_ERR_NO_MEM;
}

esp_err_t read_pico_sd_start_manual_recovery(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    portENTER_CRITICAL(&state_lock);
    manual_busy = true;
    portEXIT_CRITICAL(&state_lock);
    if (xTaskCreate(recovery_task,"sd_manual",4096,(void*)1,3,NULL) == pdPASS) return ESP_OK;
    portENTER_CRITICAL(&state_lock);
    probe_state = 2;
    manual_busy = false;
    portEXIT_CRITICAL(&state_lock);
    return ESP_ERR_NO_MEM;
}

esp_err_t read_pico_sd_sync(void) {
    read_pico_sd_info_t previous;
    (void)read_pico_sd_get_info(&previous);
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err = close_card();
    if(err==ESP_ERR_NOT_FINISHED){abort_operation();return err;}
    if (err != ESP_OK) {
        read_pico_sd_info_t info = { .present = true, .mounted = true, .error = err };
        (void)read_pico_sd_detect(&info.present);
        publish_info(&info);
        return err;
    }
    // S31没有CD检测脚：卸载不等于拔卡，保留已知存在状态，允许随后重新挂载字体。
    // S31 has no card-detect pin: unmounting is not removal; retain known presence for font remounts.
    read_pico_sd_info_t info = { .present = previous.present, .error = ESP_ERR_INVALID_STATE };
    publish_info(&info);
    portENTER_CRITICAL(&state_lock);
    if(cached_info.present && !media_invalidated) probe_state=0;
    suspended = true;
    portEXIT_CRITICAL(&state_lock);
    return ESP_OK;
}

esp_err_t read_pico_sd_format(void) {
    read_pico_sd_info_t current;
    (void)read_pico_sd_get_info(&current);
    portENTER_CRITICAL(&state_lock);
    bool invalid = media_invalidated;
    bool busy = usb_owned || probe_state == 1 || space_busy || health_busy || suspended;
    if (!invalid && !busy) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    if (invalid) return current.present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    if (busy) return ESP_ERR_NOT_FINISHED;
    bool present = false;
    if (read_pico_sd_detect(&present) == ESP_OK && !present) {
        read_pico_sd_info_t info = { .error = ESP_ERR_NOT_FOUND };
        publish_info(&info);
        return ESP_ERR_NOT_FOUND;
    }

    if(!read_pico_sd_access_transition_begin(1000)) {
        portENTER_CRITICAL(&state_lock);probe_state=2;portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_NOT_FINISHED;
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
    read_pico_sd_access_set_mounted(err==ESP_OK);
    read_pico_sd_access_transition_end();
    publish_info(&info);
    return err;
}

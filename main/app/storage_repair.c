/*
 * 中文：PICO设置中的存储维护，分块备份、源/备份双重校验及显式重建。
 * English: Pico maintenance with chunked backup, source/readback verification and explicit rebuild.
 * 冻结：不自动重建；任何错误或复位均撤销授权；原始备份永久保留，不覆盖旧备份。
 * Frozen: never rebuild automatically; errors/reset revoke authorization; retain originals and never overwrite older backups.
 */
#include "storage_repair.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "psa/crypto.h"
#include "read_pico_sd.h"
#include "read_pico.h"
#include "read_pico_transfer.h"
#include "usb_storage.h"
#include "ota_online.h"
#include "online_font.h"
#include "kiiko_device_sync.h"
#include "book_cover_jobs.h"
#include "book_source.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_library.h"
#include "book_lock_collage.h"
#include "ui_avatar.h"
#include "app_font_context.h"
#include "ttf_font.h"
#include "ui_kit.h"

#ifndef STORAGE_REPAIR_ROOT
#define STORAGE_REPAIR_ROOT "/sdcard/kiikoread-recovery"
#endif
#define REPAIR_CHUNK 8192u
#define REPAIR_ADDRESS 0x410000u
#define REPAIR_SIZE 0x500000u
static storage_repair_status_t s_status;
static const esp_partition_t *s_partition;
static FILE *s_file;
static uint8_t *s_buffer;
static uint8_t s_expected[32];
static psa_hash_operation_t s_hash = PSA_HASH_OPERATION_INIT;
static bool s_backup, s_recheck, s_fonts_closed;
static int64_t s_deadline;
static unsigned s_painted_percent;
static char s_directory[160], s_part_path[208];

bool app_storage_repair_busy(void) {
    return s_status.phase >= STORAGE_REPAIR_PREPARE && s_status.phase <= STORAGE_REPAIR_REBUILD;
}
storage_repair_status_t app_storage_repair_status(void) { return s_status; }
static bool owner_busy(void) {
    read_pico_transfer_status_t transfer = {0}; online_font_status_t font = {0};
    read_pico_transfer_get_status(&transfer); online_font_status(&font);
    return usb_storage_active() || pico_online_busy() || kiiko_sync_active() || font.busy ||
           transfer.state != READ_PICO_TRANSFER_STOPPED;
}
static bool settle_owner(void) {
    online_font_status_t font = {0}; online_font_status(&font);
    if (usb_storage_active() || pico_online_busy() || kiiko_sync_active() || font.busy) return false;
    read_pico_transfer_status_t transfer = {0}; read_pico_transfer_get_status(&transfer);
    // 空闲WiFi/HTTP会话可安全停止并join；上传/文件操作中的会话由原子门禁拒绝。
    // Stop/join an idle WiFi/HTTP session; the atomic admission gate rejects active file operations.
    if (transfer.state != READ_PICO_TRANSFER_STOPPED && !read_pico_transfer_try_stop_if_idle()) return false;
    return !owner_busy();
}
static bool partition_valid(void) {
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                         ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    if (!part || part->address != REPAIR_ADDRESS || part->size != REPAIR_SIZE ||
        part->encrypted || part->readonly) return false;
    if (s_partition && part != s_partition) return false;
    s_partition = part;
    return true;
}
static void release_work(void) {
    if (s_file) { fclose(s_file); s_file = NULL; }
    free(s_buffer); s_buffer = NULL;
    (void)psa_hash_abort(&s_hash);
}
static void restore_font(void) {
    if (!s_fonts_closed) return;
    s_fonts_closed = false;
    if (read_pico_sd_io_ready()) { (void)ttf_font_scan(); (void)app_font_retry_active(); }
}
static bool finish(storage_repair_phase_t phase, esp_err_t error) {
    release_work(); s_status.phase = phase; s_status.error = error;
    if (phase != STORAGE_REPAIR_BACKUP_READY) s_status.backup_verified = false;
    restore_font(); return true;
}
static bool fail(esp_err_t error) { return finish(STORAGE_REPAIR_FAILED, error); }
static bool hash_start(void) {
    (void)psa_hash_abort(&s_hash);
    return psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&s_hash, PSA_ALG_SHA_256) == PSA_SUCCESS;
}
static bool hash_finish(uint8_t digest[32]) {
    size_t size = 0;
    return psa_hash_finish(&s_hash, digest, 32, &size) == PSA_SUCCESS && size == 32;
}
static bool durable_close(void) {
    bool ok = fflush(s_file) == 0;
    if (ok) ok = fsync(fileno(s_file)) == 0;
    if (fclose(s_file)) ok = false;
    s_file = NULL; return ok;
}
static esp_err_t ensure_directory(const char *path) {
    if (!mkdir(path, 0777)) return ESP_OK;
    struct stat st;
    return errno == EEXIST && !stat(path, &st) && S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
}
static esp_err_t prepare_backup(void) {
    uint64_t total = 0, available = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &available) != ESP_OK) return ESP_FAIL;
    if (available < s_partition->size + 65536u) return ESP_ERR_INVALID_SIZE;
    if (ensure_directory(STORAGE_REPAIR_ROOT) != ESP_OK) return ESP_FAIL;
    bool created = false;
    for (int retry = 0; retry < 8; ++retry) {
        snprintf(s_directory, sizeof(s_directory), STORAGE_REPAIR_ROOT "/storage-%08x%08x",
                 (unsigned)esp_random(), (unsigned)esp_random());
        if (!mkdir(s_directory, 0777)) { created = true; break; }
        if (errno != EEXIST) return ESP_FAIL;
    }
    if (!created) return ESP_FAIL;
    snprintf(s_status.backup_path, sizeof(s_status.backup_path), "%s/storage.raw", s_directory);
    snprintf(s_part_path, sizeof(s_part_path), "%s/storage.raw.part", s_directory);
    s_file = fopen(s_part_path, "wb");
    return s_file && hash_start() ? ESP_OK : ESP_FAIL;
}
static bool seal_backup(void) {
    if (rename(s_part_path, s_status.backup_path)) return false;
    for (size_t i = 0; i < sizeof(s_expected); ++i)
        snprintf(s_status.sha256 + i * 2, 3, "%02x", s_expected[i]);
    char path[192]; snprintf(path, sizeof(path), "%s/backup.json", s_directory);
    s_file = fopen(path, "wb");
    if (!s_file) return false;
    bool ok = fprintf(s_file, "{\"format\":\"kiikoread-storage-raw-v1\",\"board\":\"pico\","
                      "\"address\":%u,\"bytes\":%u,\"sha256\":\"%s\",\"file\":\"storage.raw\"}\n",
                      REPAIR_ADDRESS, REPAIR_SIZE, s_status.sha256) > 0;
    return durable_close() && ok;
}
static bool progress_redraw(void) {
    unsigned percent = s_status.total ? (unsigned)(s_status.done * 100 / s_status.total) : 0;
    if (percent / 20 == s_painted_percent / 20) return false;
    s_painted_percent = percent; return true;
}
esp_err_t app_storage_repair_begin(bool backup, int64_t now_ms) {
    if (app_storage_repair_busy()) return ESP_ERR_INVALID_STATE;
    if (!settle_owner()) {
        s_status = (storage_repair_status_t){.phase = STORAGE_REPAIR_FAILED,
            .error = ESP_ERR_INVALID_STATE, .card_error = ESP_ERR_NOT_FINISHED,
            .flash_error = ESP_ERR_NOT_FINISHED, .progress_error = ESP_ERR_NOT_FINISHED};
        return ESP_ERR_INVALID_STATE;
    }
    release_work(); s_partition = NULL; s_backup = backup; s_recheck = false;
    s_status = (storage_repair_status_t){.phase = STORAGE_REPAIR_PREPARE,
        .card_error = ESP_ERR_NOT_FINISHED, .flash_error = ESP_ERR_NOT_FINISHED,
        .progress_error = ESP_ERR_NOT_FINISHED};
    s_deadline = now_ms + 5000; return ESP_OK;
}
esp_err_t app_storage_repair_confirm(int64_t now_ms) {
    if (s_status.phase != STORAGE_REPAIR_BACKUP_READY || !s_status.backup_verified ||
        now_ms > s_deadline || !settle_owner() || !read_pico_sd_io_ready()) {
        if (s_status.phase == STORAGE_REPAIR_BACKUP_READY) {
            s_status.phase = STORAGE_REPAIR_FAILED; s_status.error = ESP_ERR_INVALID_STATE;
            s_status.backup_verified = false;
        }
        return ESP_ERR_INVALID_STATE;
    }
    s_recheck = true; s_status.backup_verified = false;
    s_status.phase = STORAGE_REPAIR_PREPARE; s_deadline = now_ms + 5000;
    return ESP_OK;
}
void app_storage_repair_cancel(void) {
    if (app_storage_repair_busy()) return;
    release_work(); s_status.phase = STORAGE_REPAIR_IDLE; s_status.backup_verified = false;
    s_recheck = s_backup = false; s_partition = NULL; restore_font();
}
bool app_storage_repair_tick(int64_t now_ms) {
    if (!app_storage_repair_busy()) return false;
    if (owner_busy()) return fail(ESP_ERR_INVALID_STATE);
    if (s_status.phase == STORAGE_REPAIR_PREPARE) {
        bool covers = book_cover_jobs_quiesce(0), avatar = ui_avatar_quiesce(0);
        if (!covers || !avatar) return now_ms >= s_deadline ? fail(ESP_ERR_TIMEOUT) : false;
        book_close(); ttf_font_close_embedded(); ttf_font_unload();
        ui_text_set_system_font(false); (void)ttf_font_open_builtin(); s_fonts_closed = true;
        ui_avatar_invalidate(); book_lock_collage_invalidate();
        read_pico_sd_info_t info = {0}; esp_err_t error = read_pico_sd_get_info(&info);
        if (error == ESP_ERR_NOT_FINISHED) return now_ms >= s_deadline ? fail(ESP_ERR_TIMEOUT) : false;
        if (info.present && !info.mounted) {
            error = read_pico_sd_remount();
            if (error != ESP_OK && error != ESP_ERR_NOT_FINISHED) return fail(error);
            s_status.phase = STORAGE_REPAIR_REMOUNT; return true;
        }
        s_status.phase = STORAGE_REPAIR_CHECK; return true;
    }
    if (s_status.phase == STORAGE_REPAIR_REMOUNT) {
        read_pico_sd_info_t info = {0};
        if (read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED) return false;
        s_status.phase = STORAGE_REPAIR_CHECK; return true;
    }
    if (s_status.phase == STORAGE_REPAIR_CHECK) {
        read_pico_sd_info_t info = {0};
        esp_err_t card = read_pico_sd_get_info(&info);
        s_status.card_error = card == ESP_OK && info.mounted ? ESP_OK : card != ESP_OK ? card : ESP_FAIL;
        if (!s_backup) {
            book_store_root_t roots[BOOK_STORE_ROOT_MAX]; int count = 0;
            esp_err_t mounts = book_store_roots(roots, &count);
            unsigned root_errors = book_store_root_errors();
            if (root_errors & BOOK_STORE_ROOT_ERROR_CARD) s_status.card_error = ESP_FAIL;
            s_status.flash_error = book_store_flash_ready() && !(root_errors & BOOK_STORE_ROOT_ERROR_FLASH) ?
                ESP_OK : mounts != ESP_OK ? mounts : ESP_FAIL;
            s_status.progress_error = book_progress_prepare_storage();
            s_status.card_progress = book_progress_using_card_storage();
            book_store_notify_changed(); book_library_invalidate();
            return finish(STORAGE_REPAIR_DONE, ESP_OK);
        }
        // 备份及最终复核期间不尝试重挂机内分区，避免WL元数据写入改变待保留的原始快照。
        // Do not mount internal storage while backing up or rechecking: WL metadata writes could alter the raw snapshot.
        s_status.flash_error = book_store_flash_ready() ? ESP_OK : ESP_FAIL;
        // 重建入口只用于未挂载的机内分区；正常分区无需清空。
        // Rebuild is available only for an unmounted internal volume; a healthy one needs no clearing.
        if (book_store_flash_ready()) return finish(STORAGE_REPAIR_DONE, ESP_OK);
        if (s_status.card_error != ESP_OK || !partition_valid())
            return fail(ESP_ERR_INVALID_STATE);
        s_buffer = malloc(REPAIR_CHUNK * 2);
        if (!s_buffer) return fail(ESP_ERR_NO_MEM);
        s_status.done = 0; s_status.total = s_partition->size; s_painted_percent = 0;
        if (s_recheck) {
            s_file = fopen(s_status.backup_path, "rb");
            if (!s_file || !hash_start()) return fail(ESP_FAIL);
            s_status.phase = STORAGE_REPAIR_RECHECK;
        } else {
            esp_err_t prepared = prepare_backup();
            if (prepared != ESP_OK) return fail(prepared);
            s_status.phase = STORAGE_REPAIR_COPY;
        }
        return true;
    }
    if (s_status.phase == STORAGE_REPAIR_COPY) {
        size_t size = s_status.total - s_status.done;
        if (size > REPAIR_CHUNK) size = REPAIR_CHUNK;
        if (size) {
            if (!read_pico_sd_io_ready() || esp_partition_read(s_partition, s_status.done, s_buffer, size) != ESP_OK ||
                fwrite(s_buffer, 1, size, s_file) != size || psa_hash_update(&s_hash, s_buffer, size) != PSA_SUCCESS)
                return fail(ESP_FAIL);
            s_status.done += size; return progress_redraw();
        }
        if (!hash_finish(s_expected) || !durable_close()) return fail(ESP_FAIL);
        s_file = fopen(s_part_path, "rb");
        if (!s_file || !hash_start()) return fail(ESP_FAIL);
        s_status.done = 0; s_painted_percent = 0; s_status.phase = STORAGE_REPAIR_VERIFY; return true;
    }
    if (s_status.phase == STORAGE_REPAIR_VERIFY || s_status.phase == STORAGE_REPAIR_RECHECK) {
        size_t size = s_status.total - s_status.done;
        if (size > REPAIR_CHUNK) size = REPAIR_CHUNK;
        if (size) {
            if (!read_pico_sd_io_ready() || fread(s_buffer, 1, size, s_file) != size ||
                esp_partition_read(s_partition, s_status.done, s_buffer + REPAIR_CHUNK, size) != ESP_OK ||
                memcmp(s_buffer, s_buffer + REPAIR_CHUNK, size) ||
                psa_hash_update(&s_hash, s_buffer, size) != PSA_SUCCESS) return fail(ESP_FAIL);
            s_status.done += size; return progress_redraw();
        }
        uint8_t digest[32];
        bool ok = fgetc(s_file) == EOF && !ferror(s_file);
        if (fclose(s_file)) ok = false;
        s_file = NULL;
        if (!ok || !hash_finish(digest) || memcmp(digest, s_expected, sizeof(digest))) return fail(ESP_FAIL);
        if (s_recheck) { s_status.phase = STORAGE_REPAIR_REBUILD; return true; }
        if (!seal_backup()) return fail(ESP_FAIL);
        s_status.backup_verified = true; s_deadline = now_ms + 300000;
        return finish(STORAGE_REPAIR_BACKUP_READY, ESP_OK);
    }
    // 只有第二次确认后的逐字节与SHA256再校验能到达这里；不接受复位前留下的确认。
    // Only a fresh second confirmation and source/readback SHA256 verification can reach this; reset never resumes consent.
    bool present = false;
    if (!s_recheck || !partition_valid() || book_store_flash_ready() ||
        read_pico_sd_detect(&present) != ESP_OK || !present || !read_pico_sd_io_ready())
        return fail(ESP_ERR_INVALID_STATE);
    s_status.erase_started = true;
    esp_err_t erased = esp_partition_erase_range(s_partition, 0, s_partition->size);
    if (erased != ESP_OK) return fail(erased);
    book_store_root_t roots[BOOK_STORE_ROOT_MAX]; int count = 0;
    esp_err_t mounted = book_store_roots(roots, &count);
    if (mounted != ESP_OK || !book_store_flash_ready()) return fail(mounted != ESP_OK ? mounted : ESP_FAIL);
    s_status.flash_error = ESP_OK;
    s_status.progress_error = book_progress_prepare_storage();
    s_status.card_progress = book_progress_using_card_storage();
    book_store_notify_changed(); book_library_invalidate();
    return finish(s_status.progress_error == ESP_OK ? STORAGE_REPAIR_DONE : STORAGE_REPAIR_FAILED,
                  s_status.progress_error);
}

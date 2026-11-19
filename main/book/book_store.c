/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 书目录与 FAT 容量查询；调用方负责先显示首次初始化提示。
 * Book directories and FAT capacity queries; callers show initialization feedback first.
 *
 * 冻结：不格式化 SD；内置 storage 分区按需挂载，失败不影响可用 SD。
 * Frozen: never format SD; mount internal storage on demand without blocking usable SD on failure.
 * 冻结：有卡但挂载失效时不把上传静默切换到内置存储；拔卡不复用旧容量。
 * Frozen: present but invalid media must not silently redirect uploads to flash; never reuse removed-card capacity.
 * 冻结：用户确认后仅删已挂载根（TF 或内置 storage）内的普通书籍文件，路径不得穿越、
 * 不得用目录或链接替换；文件与进度部分失败必须返回，不通知 UI 版本。
 * Frozen: after user confirmation, delete only a regular book file under a mounted root (SD or
 * internal storage), never through traversal, a directory or a link; report partial failures
 * without notifying UI revision.
 * 冻结：删除前清除同名普通覆盖备份；仅有备份时先恢复，失败保留可恢复的原书。
 * Frozen: clear a matching regular replacement backup before deletion; restore backup-only books first and preserve recovery on failure.
 * 2026-10-23 变更：书架会列出 books 子目录里的书，也会补入文件管理在卡内任意位置打开过的书，
 * 原先只受理书根第一层，这些书点删除一律失败。现在按已挂载根受理，其余检查不变。
 * Changed 2026-10-23: the shelf lists books in nested books folders and backfills books opened
 * anywhere on the card by the file manager. The old top-level-only rule refused them all; the
 * mounted-root rule below admits them while keeping every other check.
 */
#include "book_store.h"
#include "book_progress.h"
#include "book_added.h"
#include "settings.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "read_pico_sd.h"
#include "wear_levelling.h"
#include "esp_timer.h"

static const char* TAG = "book_store";
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static unsigned s_revision;
static bool s_roots_degraded;
static esp_err_t s_flash_error;
static int64_t s_flash_retry_us;

void book_store_notify_changed(void) { ++s_revision; }
unsigned book_store_revision(void) { return s_revision; }
bool book_store_roots_degraded(void) { return s_roots_degraded; }

static esp_err_t ensure_dir(const char* path) {
    if (mkdir(path, 0777) == 0) return ESP_OK;
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return ESP_OK;
    }
    ESP_LOGW(TAG, "book directory unavailable: %s (%d)", path, errno);
    return ESP_FAIL;
}

static bool sd_ready(void) {
    read_pico_sd_info_t info = {0};
    read_pico_sd_get_info(&info);
    return info.present && info.mounted;
}

// 只在整个逻辑数据区均为擦除态时允许首次建立FAT；磨损均衡元数据不算用户数据。
// Initialize FAT only after checking every logical data byte; WL metadata is outside that area.
static bool flash_data_blank(wl_handle_t handle) {
    uint8_t bytes[512];
    const size_t size = wl_size(handle);
    if (!size) return false;
    for (size_t off = 0; off < size; off += sizeof(bytes)) {
        size_t count = size - off < sizeof(bytes) ? size - off : sizeof(bytes);
        if (wl_read(handle, off, bytes, count) != ESP_OK) return false;
        for (size_t i = 0; i < count; ++i) if (bytes[i] != 0xff) return false;
    }
    return true;
}

static esp_err_t flash_mount(bool force_retry) {
    if (!book_store_flash_ready()) {
        if (!force_retry && esp_timer_get_time() < s_flash_retry_us) return s_flash_error;
        esp_vfs_fat_mount_config_t config = {
            .format_if_mount_failed = false,
            .max_files = 4,
            .allocation_unit_size = 4096,
        };
        wl_handle_t handle = WL_INVALID_HANDLE;
        esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/flash", "storage", &config, &handle);
        if (err != ESP_OK) {
            const bool blank = err == ESP_FAIL && handle != WL_INVALID_HANDLE && flash_data_blank(handle);
            // IDF的FAT失败路径会留下已建立的WL句柄；丢弃它会逐次耗尽实例。
            // A failed FAT mount can retain its WL handle; release it before retrying.
            esp_err_t released = handle == WL_INVALID_HANDLE ? ESP_OK : wl_unmount(handle);
            handle = WL_INVALID_HANDLE;
            if (blank && released == ESP_OK) {
                ESP_LOGI(TAG, "initialize verified empty internal storage");
                config.format_if_mount_failed = true;
                err = esp_vfs_fat_spiflash_mount_rw_wl("/flash", "storage", &config, &handle);
                if (err != ESP_OK && handle != WL_INVALID_HANDLE) (void)wl_unmount(handle);
            }
        }
        if (err != ESP_OK) {
            s_wl = WL_INVALID_HANDLE;
            s_flash_error = err;
            s_flash_retry_us = esp_timer_get_time() + INT64_C(10000000);
            ESP_LOGW(TAG, "flash mount: %s", esp_err_to_name(err));
            return err;
        }
        s_wl = handle;
        s_flash_retry_us = 0;
    }
    return ESP_OK;
}

static esp_err_t flash_root(book_store_root_t* out) {
    esp_err_t err = flash_mount(true);
    if (err == ESP_OK) err = ensure_dir("/flash/books");
    if (err == ESP_OK) *out = (book_store_root_t){.path = "/flash/books", .is_flash = true};
    return err;
}

esp_err_t book_store_prepare_flash(void) {
    return flash_mount(false);
}

esp_err_t book_store_roots(book_store_root_t out[BOOK_STORE_ROOT_MAX], int* n) {
    if (out == NULL || n == NULL) return ESP_ERR_INVALID_ARG;
    *n = 0;
    s_roots_degraded = false;
    memset(out, 0, sizeof(*out) * BOOK_STORE_ROOT_MAX);
    if (sd_ready()) {
        // Also show books already copied to a singular `book` directory.
        struct stat legacy;
        if (!strcmp(app_settings_books_dir(), "/sdcard/books") && stat("/sdcard/book", &legacy) == 0 && S_ISDIR(legacy.st_mode))
            out[(*n)++] = (book_store_root_t){.path = "/sdcard/book", .is_flash = false};
        if (ensure_dir(app_settings_books_dir()) == ESP_OK)
            snprintf(out[(*n)++].path, sizeof(out[0].path), "%s", app_settings_books_dir());
        else s_roots_degraded = true;
    } else {
        read_pico_sd_info_t info = {0};
        read_pico_sd_get_info(&info);
        if (info.present) s_roots_degraded = true;
    }
    esp_err_t err = flash_root(&out[*n]);
    if (err == ESP_OK) ++*n;
    else s_roots_degraded = true;
    return *n > 0 ? ESP_OK : err;
}

esp_err_t book_store_upload_root(book_store_root_t* out) {
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!sd_ready()) {
        read_pico_sd_info_t info = {0};
        read_pico_sd_get_info(&info);
        // 有卡但挂载失效时让用户显式重挂，不能悄悄改写内置目录。
        // Present but invalid media requires an explicit remount, not silent flash redirection.
        if (info.present) return ESP_ERR_INVALID_STATE;
        return flash_root(out);
    }
    esp_err_t err = ensure_dir(app_settings_books_dir());
    if (err == ESP_OK) snprintf(out->path, sizeof(out->path), "%s", app_settings_books_dir());
    return err;
}

size_t book_store_file_limit(const book_store_root_t* root) {
    if (root == NULL) return 0;
    return root->is_flash ? BOOK_STORE_FLASH_FILE_MAX : SIZE_MAX;
}

uint64_t book_store_free_bytes(const book_store_root_t* root) {
    if (root == NULL) return 0;
    if (!root->is_flash && !sd_ready()) return 0;
    uint64_t total = 0, free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(root->is_flash ? "/flash" : "/sdcard", &total, &free_bytes);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FAT capacity: %s", esp_err_to_name(err));
        return 0;
    }
    return free_bytes;
}

bool book_store_flash_ready(void) {
    return s_wl != WL_INVALID_HANDLE;
}

esp_err_t book_store_delete(const char* path, bool* removed) {
    if (!removed) return ESP_ERR_INVALID_ARG;
    *removed = false;
    if (!path || strnlen(path, BOOK_STORE_PATH_MAX) >= BOOK_STORE_PATH_MAX) return ESP_ERR_INVALID_ARG;
    const char* root;
    const char* name;
    size_t selected_len = strlen(app_settings_books_dir());
    if (!strncmp(path, "/flash/books/", 13)) {
        if (!book_store_flash_ready()) return ESP_ERR_INVALID_STATE;
        root = "/flash/books"; name = path + 13;
    } else {
        if (!strncmp(path, app_settings_books_dir(), selected_len) && path[selected_len] == '/') {
            root = app_settings_books_dir(); name = path + selected_len + 1;
        } else if (!strncmp(path, "/sdcard/", 8)) {
            // 书架也会列出 books 子目录里的书，以及文件管理在卡内任意位置打开过的书；删除按已挂载根受理。
            // The shelf also lists books in nested folders and books opened anywhere on the card,
            // so deletion accepts any book file under the mounted SD root (including legacy book/).
            root = "/sdcard"; name = path + 8;
        } else return ESP_ERR_INVALID_ARG;
        if (!sd_ready()) return ESP_ERR_INVALID_STATE;
    }
    size_t len = strlen(name);
    if (!len || name[0] == '/' || name[len - 1] == '/' || strstr(name, "//")) return ESP_ERR_INVALID_ARG;
    // 只拒绝独立的路径穿越段；书名中的省略号“...”是合法字符。
    // Reject traversal components, while keeping literal ellipses in real book names.
    for(const char *component=name;*component;){
        const char *end=strchr(component,'/');size_t n=end?(size_t)(end-component):strlen(component);
        if((n==1&&component[0]=='.')||(n==2&&!memcmp(component,"..",2)))return ESP_ERR_INVALID_ARG;
        if(!end)break;
        component=end+1;
    }
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p) {
        if (*p < 32 || *p == 127 || *p == '\\' || *p == ':') return ESP_ERR_INVALID_ARG;
    }
    const char* base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (strlen(base) > 255) return ESP_ERR_INVALID_ARG;
    const char* ext = strrchr(base, '.');
    if (!ext || (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub"))) return ESP_ERR_INVALID_ARG;
    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return ESP_ERR_INVALID_STATE;
    // FAT 没有符号链接；主机测试用 lstat，确保不会跟随主机链接删其他文件。
    // FAT has no symlinks; host builds use lstat to reject host links rather than follow them.
#ifdef ESP_PLATFORM
    int result = stat(path, &st);
#else
    int result = lstat(path, &st);
#endif
    bool exists = result == 0;
    if (!exists && errno != ENOENT) return ESP_FAIL;
    if (exists && !S_ISREG(st.st_mode)) return ESP_ERR_INVALID_ARG;
    // 显式删除也处理覆盖备份，避免下次传书启动恢复已删图书。
    // Explicit deletion handles replacement backups so transfer startup cannot resurrect deleted books.
    const char suffix[] = ".rename-backup";
    // FAT的255上限按UTF-16单元计数；不存在的超长备份名不能阻止删除正常原书。
    // FAT counts 255 UTF-16 units; an impossible backup name must not block deleting the real book.
    size_t units=sizeof(suffix)-1;
    for(const unsigned char *p=(const unsigned char*)base;*p;++p)
        if((*p&0xc0)!=0x80)units+=*p>=0xf0?2:1;
    char backup[BOOK_STORE_PATH_MAX + sizeof(suffix)];
    snprintf(backup, sizeof(backup), "%s%s", path, suffix);
    result=-1;errno=ENOENT;
    if(units<=255){
#ifdef ESP_PLATFORM
        result = stat(backup, &st);
#else
        result = lstat(backup, &st);
#endif
    }
    if (result != 0) {
        if (errno != ENOENT) return ESP_FAIL;
    } else {
        if (!S_ISREG(st.st_mode)) return ESP_ERR_INVALID_ARG;
        if (exists) {
            if (unlink(backup) != 0 && errno != ENOENT) return ESP_FAIL;
        } else {
            // 未完成覆盖先恢复旧书；后续删除失败时仍保留可见原书。
            // Restore an interrupted replacement first, keeping the old book visible if deletion fails.
            if (rename(backup, path) != 0) return ESP_FAIL;
            exists = true;
        }
    }
    if (exists) {
        if (unlink(path) != 0 && errno != ENOENT) return ESP_FAIL;
    }
    *removed = true;
    book_added_forget(path);
    return book_progress_forget(path);
}

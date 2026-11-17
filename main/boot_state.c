/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界内存与紧凑校验的 NVS 启动记录，兼容旧定长格式。开书中断保留原进度，不把不支持的缓存当图书。
 * English: Bounded-memory compact checked NVS startup state, compatible with the legacy fixed record. Interrupted opening preserves progress and unsupported caches are never treated as books.
 * 冻结：不擦除分区、不动态分配、不逐页写 Flash；恢复入口须先成功消费，资源失败回可操作页面。
 * Frozen: No partition erasure, allocations or per-page Flash writes. Consume resume successfully before using it; failed resources fall back to an operable page.
 */
#include "boot_state.h"
#include "kiiko_board_config.h"
#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "book/book_file_path.h"

#define BOOT_MAGIC UINT32_C(0x50425331)
#define BOOT_PACKED_MAGIC UINT32_C(0x50425332)
typedef struct {
    uint32_t magic;
    uint8_t starting, resume_valid, reserved[2];
    pico_resume_t resume;
    char opening[PICO_BOOT_PATH_MAX], blocked[PICO_BOOT_PATH_MAX];
    uint32_t checksum;
} boot_record_t;
static boot_record_t s_record;
static pico_resume_t s_resume;
static bool s_recovery, s_resume_pending, s_writable, s_interrupted, s_resume_frame;
// 原因仅用于诊断；保留所有既有保护与兼容记录格式。/ Diagnostic causes only; retain every protection and the persisted format.
enum {
    BOOT_RECOVERY_RESET = 1u,
    BOOT_RECOVERY_RECORD = 2u,
    BOOT_RECOVERY_STARTING = 4u,
    BOOT_RECOVERY_OPENING = 8u,
    BOOT_RECOVERY_STORE = 16u,
};
static unsigned s_recovery_reasons;
#if KIIKO_BOARD_METALIO47
static bool s_lock_pending;
bool pico_boot_lock_pending(void) { return s_lock_pending; }
#endif
typedef struct {
    uint32_t magic;
    uint16_t lengths[3];
    uint8_t starting, resume_valid, frame, tab, reader, fullscreen;
    uint32_t checksum;
} boot_packed_header_t;
#define BOOT_PACKED_MAX (sizeof(boot_packed_header_t) + 3u * PICO_BOOT_PATH_MAX)

static uint32_t checksum(const boot_record_t *r) {
    const uint8_t *p = (const uint8_t *)r;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(boot_record_t, checksum); ++i) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}
static bool path_valid(const char *path, bool empty) {
    size_t n = strnlen(path, PICO_BOOT_PATH_MAX);
    if (!n) return empty;
    if (n == PICO_BOOT_PATH_MAX || (strncmp(path, "/sdcard/", 8) && strncmp(path, "/flash/books/", 13))) return false;
    for (const char *p = path + 1; *p;) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (!len || (len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return false;
        for (size_t i = 0; i < len; ++i) if ((unsigned char)p[i] < 32 || p[i] == '\\') return false;
        if (!end) break;
        p = end + 1;
        if (!*p) return false;
    }
    return true;
}
static bool resume_valid(const pico_resume_t *r) {
    return r && r->tab < 4 && r->reader <= 1 && r->fullscreen <= 1 &&
        (!r->reader || r->tab == 1) && path_valid(r->path, !r->reader);
}
static uint32_t packed_checksum(const uint8_t *data,size_t size) {
    uint32_t hash=2166136261u;
    for(size_t i=0;i<size;++i){
        uint8_t value=i>=offsetof(boot_packed_header_t,checksum)&&i<offsetof(boot_packed_header_t,checksum)+sizeof(uint32_t)?0:data[i];
        hash=(hash^value)*16777619u;
    }
    return hash;
}
static size_t pack_record(uint8_t data[BOOT_PACKED_MAX]) {
    boot_packed_header_t header={.magic=BOOT_PACKED_MAGIC,.starting=s_record.starting,
        .resume_valid=s_record.resume_valid};
    if (s_record.resume_valid) {
        header.frame=s_record.reserved[0];header.tab=s_record.resume.tab;
        header.reader=s_record.resume.reader;header.fullscreen=s_record.resume.fullscreen;
    }
    const char *paths[]={s_record.resume_valid ? s_record.resume.path : "",s_record.opening,s_record.blocked};
    size_t size=sizeof(header);
    for(unsigned i=0;i<3;++i){
        size_t length=strnlen(paths[i],PICO_BOOT_PATH_MAX);
        if(length==PICO_BOOT_PATH_MAX)return 0;
        header.lengths[i]=(uint16_t)(length+1);
        memcpy(data+size,paths[i],length+1);size+=length+1;
    }
    memcpy(data,&header,sizeof(header));header.checksum=packed_checksum(data,size);
    memcpy(data,&header,sizeof(header));return size;
}
static bool unpack_record(const uint8_t *data,size_t size,boot_record_t *record) {
    if(size<sizeof(boot_packed_header_t))return false;
    boot_packed_header_t header;memcpy(&header,data,sizeof(header));
    if(header.magic!=BOOT_PACKED_MAGIC||header.checksum!=packed_checksum(data,size)||
        header.starting>1||header.resume_valid>1||header.frame>1||header.reader>1||header.fullscreen>1)return false;
    memset(record,0,sizeof(*record));record->magic=BOOT_MAGIC;
    record->starting=header.starting;record->resume_valid=header.resume_valid;record->reserved[0]=header.frame;
    record->resume.tab=header.tab;record->resume.reader=header.reader;record->resume.fullscreen=header.fullscreen;
    char *paths[]={record->resume.path,record->opening,record->blocked};size_t at=sizeof(header);
    for(unsigned i=0;i<3;++i){
        size_t length=header.lengths[i];
        if(!length||length>PICO_BOOT_PATH_MAX||length>size-at||data[at+length-1]||
            strnlen((const char*)data+at,length)!=length-1)return false;
        memcpy(paths[i],data+at,length);at+=length;
    }
    return at==size&&resume_valid(&record->resume)&&path_valid(record->opening,true)&&path_valid(record->blocked,true);
}
static esp_err_t store(void) {
    s_record.checksum = checksum(&s_record);
    uint8_t packed[BOOT_PACKED_MAX];size_t size=pack_record(packed);
    if(!size)return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t err = nvs_open(KIIKO_BOARD_STARTUP_NAMESPACE, NVS_READWRITE, &h);
    s_writable=err==ESP_OK;
    if (err != ESP_OK) {ESP_LOGW("boot_state","checkpoint open: %s",esp_err_to_name(err));return err;}
    err = nvs_set_blob(h, "state", packed, size);
    if(err==ESP_ERR_NVS_NOT_ENOUGH_SPACE){
        // 只回收本模块旧检查点，不擦设置/书签/进度。断电落在此窗口只回首页，解析尚未获准。
        // Reclaim only this module's old checkpoint, never settings/bookmarks/progress. Power loss in this window returns Home because parsing has not been admitted.
        esp_err_t reclaimed=nvs_erase_key(h,"state");
        if(reclaimed==ESP_OK||reclaimed==ESP_ERR_NVS_NOT_FOUND){
            reclaimed=nvs_commit(h);
            if(reclaimed==ESP_OK)err=nvs_set_blob(h,"state",packed,size);
            else err=reclaimed;
        }else err=reclaimed;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGW("boot_state", "checkpoint: %s", esp_err_to_name(err));
    return err;
}
void pico_boot_init(bool abnormal_reset) {
    memset(&s_record, 0, sizeof(s_record));
    s_resume_pending = false;
    s_resume_frame = false;
    s_interrupted = false;
    s_recovery = abnormal_reset;
    s_recovery_reasons = abnormal_reset ? BOOT_RECOVERY_RESET : 0;
#if KIIKO_BOARD_METALIO47
    s_lock_pending = false;
#endif
    nvs_handle_t h;
    esp_err_t err = nvs_open(KIIKO_BOARD_STARTUP_NAMESPACE, NVS_READWRITE, &h);
    s_writable = err == ESP_OK;
    if (s_writable) {
        uint8_t data[BOOT_PACKED_MAX];size_t size=sizeof(data);
        err = nvs_get_blob(h, "state", data, &size);
        nvs_close(h);
        bool valid=err==ESP_OK&&size<=sizeof(data)&&unpack_record(data,size,&s_record);
        if(!valid&&err==ESP_OK&&size==sizeof(s_record)){
            memcpy(&s_record,data,sizeof(s_record));
            valid=s_record.magic == BOOT_MAGIC &&
            s_record.starting <= 1 && s_record.resume_valid <= 1 &&
            resume_valid(&s_record.resume) && path_valid(s_record.opening, true) &&
            path_valid(s_record.blocked, true) && s_record.checksum == checksum(&s_record);
        }
        if (!valid) {
            if (err != ESP_ERR_NVS_NOT_FOUND) {
                s_recovery = true;
                s_recovery_reasons |= BOOT_RECOVERY_RECORD;
            }
            memset(&s_record, 0, sizeof(s_record));
        }
#if KIIKO_BOARD_METALIO47
        // 真实锁屏记录独立于字体/开书保护；看门狗不能把锁定当作普通首页启动。
        // A validated lock record is independent of font/book recovery; watchdogs cannot turn it into an unlocked Home boot.
        s_lock_pending = valid && s_record.resume_valid;
#endif
    } else {
        s_recovery = true;
        s_recovery_reasons |= BOOT_RECOVERY_RECORD;
    }
    // 其他版本的缓存清单仍留在卡上，只移除不被本地阅读器支持的恢复/隔离入口。
    // Retain other editions' cache files; remove only resume/quarantine entries unsupported by the local reader.
    if(s_record.resume.reader&&!book_file_path_supported(s_record.resume.path)){
        memset(&s_record.resume,0,sizeof(s_record.resume));s_record.resume_valid=0;s_record.reserved[0]=0;
    }
    if(s_record.opening[0]&&!book_file_path_supported(s_record.opening))s_record.opening[0]=0;
    if(s_record.blocked[0]&&!book_file_path_supported(s_record.blocked))s_record.blocked[0]=0;
    if (s_record.starting) s_recovery_reasons |= BOOT_RECOVERY_STARTING;
    if (s_record.opening[0]) s_recovery_reasons |= BOOT_RECOVERY_OPENING;
    if (s_record.starting || s_record.opening[0]) s_recovery = true;
    if (s_record.opening[0]) {
        s_interrupted = true;
        memcpy(s_record.blocked, s_record.opening, sizeof(s_record.blocked));
        s_record.opening[0] = 0;
    }
    const bool blocked_resume = s_record.resume.reader && s_record.blocked[0] &&
        !strcmp(s_record.resume.path, s_record.blocked);
    if (s_record.resume_valid && !s_recovery && !blocked_resume) {
        s_resume = s_record.resume;
        s_resume_pending = true;
        s_resume_frame = s_record.reserved[0] == 1;
    }
    s_record.magic = BOOT_MAGIC;
    s_record.starting = 1;
#if KIIKO_BOARD_METALIO47
    // 门禁前再次复位仍须保留锁定；实际恢复仍由take_resume持久化消费，异常启动不放行开书。
    // Retain the lock across another reset before the gate; take_resume still consumes persistently before book admission.
    if (!s_lock_pending) {
        s_record.resume_valid = 0;
        s_record.reserved[0] = 0;
    }
#else
    s_record.resume_valid = 0;
    s_record.reserved[0] = 0;
#endif
    if (store() != ESP_OK) {
        s_resume_pending = false; s_recovery = true;
        s_recovery_reasons |= BOOT_RECOVERY_STORE;
    }
    ESP_LOGI("boot_state", "startup recovery=%d resume=%d reasons=0x%02x", s_recovery, s_resume_pending, s_recovery_reasons);
}
void pico_boot_ready(void) {
    s_record.starting = 0;
    (void)store();
}
bool pico_boot_recovery(void) { return s_recovery; }
bool pico_boot_asset_allowed(const char *path) {
    return path && !s_recovery && (!s_record.blocked[0] || strcmp(path, s_record.blocked));
}
esp_err_t pico_boot_book_begin(const char *path) {
    if (!path || !path_valid(path, false)||!book_file_path_supported(path)) return ESP_ERR_INVALID_ARG;
    memset(s_record.opening, 0, sizeof(s_record.opening));
    memcpy(s_record.opening, path, strlen(path));
    s_record.resume_valid = 0;
    esp_err_t err = store();
    if (err != ESP_OK) s_record.opening[0] = 0;
    return err;
}
void pico_boot_book_end(bool success) {
    if (success && !strcmp(s_record.blocked, s_record.opening)) s_record.blocked[0] = 0;
    s_record.opening[0] = 0;
    (void)store();
}
esp_err_t pico_boot_save_resume(const pico_resume_t *resume) {
    if (!resume_valid(resume) || (resume->reader && !book_file_path_supported(resume->path))) return ESP_ERR_INVALID_ARG;
    memset(&s_record.resume, 0, sizeof(s_record.resume));
    s_record.resume.tab = resume->tab;
    s_record.resume.reader = resume->reader;
    s_record.resume.fullscreen = resume->fullscreen;
    memcpy(s_record.resume.path, resume->path, strlen(resume->path));
    s_record.resume_valid = 1;
    s_record.reserved[0] = 0;
    return store();
}
void pico_boot_hold_resume(void) {
    if (s_resume_pending) {
        s_record.resume=s_resume;s_record.resume_valid=1;
        s_record.reserved[0]=s_resume_frame;
        (void)store();
    }
}
esp_err_t pico_boot_mark_frame_cached(void) {
    if(!s_record.resume_valid || s_recovery)return ESP_ERR_INVALID_STATE;
    s_record.reserved[0]=1;
    esp_err_t err=store();
    if(err!=ESP_OK)s_record.reserved[0]=0;
    return err;
}
bool pico_boot_resume_frame_cached(void) { return s_resume_frame && s_resume_pending && !s_recovery; }
bool pico_boot_has_resume(void) { return s_resume_pending && !s_recovery; }
bool pico_boot_take_resume(pico_resume_t *resume) {
    if (!resume || !s_resume_pending) return false;
    // 密码等待可能再次持久化；先消费再允许恢复，写入失败不能重试开书。
    // Authentication may have re-persisted it; consume before admission and never retry after a write failure.
    if (s_record.resume_valid) {
        s_record.resume_valid = 0;
        if (store() != ESP_OK) { s_resume_pending = false; return false; }
    }
    *resume = s_resume;
    s_resume_pending = false;
#if KIIKO_BOARD_METALIO47
    s_lock_pending = false;
#endif
    return true;
}
void pico_boot_clear_resume(void) {
#if KIIKO_BOARD_METALIO47
    s_lock_pending = false;
#endif
    s_resume_pending = false;
    if (!s_record.resume_valid) return;
    s_record.resume_valid = 0;
    (void)store();
}
bool pico_boot_interrupted_book(char *path, size_t capacity) {
    if (!s_interrupted || !path || strlen(s_record.blocked) >= capacity) return false;
    memcpy(path, s_record.blocked, strlen(s_record.blocked) + 1);
    return true;
}
void pico_boot_leave_book_recovery(void) {
    if (s_recovery_reasons & BOOT_RECOVERY_OPENING) {
        s_recovery = false;
        s_interrupted = false;
    }
}
void pico_boot_forget_interrupted_book(void) {
    s_interrupted = false;
    s_record.blocked[0] = 0;
    (void)store();
}

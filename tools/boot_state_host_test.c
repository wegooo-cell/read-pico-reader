/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实启动记录跨复位、断电、损坏与 NVS 失败测试。
 * English: Exercise actual startup records across reset, power loss, corruption and NVS failures.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/boot_state.c"

static boot_record_t durable;
static uint8_t durable_raw[BOOT_PACKED_MAX], staged_raw[BOOT_PACKED_MAX];
static size_t durable_size, staged_size, free_bytes=SIZE_MAX;
static bool present, staged_present, fail_open, fail_commit, fail_set_after_erase;
static unsigned commits, erases;
static const unsigned settings_sentinel=0x7351, progress_sentinel=0x19af;
static void decode_durable(void) {
    if(!present){memset(&durable,0,sizeof(durable));return;}
    if(!unpack_record(durable_raw,durable_size,&durable)){
        assert(durable_size==sizeof(durable));memcpy(&durable,durable_raw,sizeof(durable));
    }
}
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    assert(!strcmp(ns, KIIKO_BOARD_STARTUP_NAMESPACE) && mode == NVS_READWRITE);
    *h=1;staged_present=present;staged_size=durable_size;
    memcpy(staged_raw,durable_raw,durable_size);
    return fail_open ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *size) {
    assert(h == 1 && !strcmp(key, "state"));
    if (!present) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size>=durable_size);memcpy(data,durable_raw,durable_size);*size=durable_size;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size) {
    assert(h==1&&!strcmp(key,"state")&&size<=sizeof(staged_raw));
    if(fail_set_after_erase&&erases)return ESP_FAIL;
    if(size>free_bytes)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    memcpy(staged_raw,data,size);staged_size=size;staged_present=true;return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {
    assert(h==1&&!strcmp(key,"state"));++erases;
    staged_present=false;staged_size=0;return present?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h == 1); ++commits;
    if (fail_commit) return ESP_FAIL;
    if(free_bytes!=SIZE_MAX)free_bytes+=durable_size-staged_size;
    durable_size=staged_size;present=staged_present;
    memcpy(durable_raw,staged_raw,durable_size);decode_durable();return ESP_OK;
}
static void seed_legacy(boot_record_t record) {
    record.magic=BOOT_MAGIC;record.checksum=checksum(&record);
    durable_size=sizeof(record);present=true;memcpy(durable_raw,&record,sizeof(record));decode_durable();
}
int main(void) {
    pico_resume_t resume = {.tab=1, .reader=1, .fullscreen=1, .path="/sdcard/books/deep.epub"}, out;
    char interrupted[PICO_BOOT_PATH_MAX];
#if KIIKO_BOARD_METALIO47
    // 解锁前连续重启不能抹掉锁定意图，异常启动仍禁止自动开书/导入字体。
    // Consecutive boots before unlock must retain the lock while abnormal startup still forbids automatic book/font resume.
    pico_boot_init(false);pico_boot_ready();assert(pico_boot_save_resume(&resume)==ESP_OK);
    pico_boot_init(false);assert(pico_boot_lock_pending()&&durable.resume_valid);
    for(unsigned cycle=0;cycle<3;++cycle){
        pico_boot_init(true);
        assert(pico_boot_lock_pending()&&durable.resume_valid&&pico_boot_recovery()&&!pico_boot_has_resume());
    }
    pico_boot_ready();pico_boot_clear_resume();
    assert(!pico_boot_lock_pending()&&!durable.resume_valid);
#endif
    pico_boot_init(false); assert(!pico_boot_recovery() && !pico_boot_take_resume(&out) && !s_recovery_reasons);
    pico_boot_ready();
    assert(pico_boot_save_resume(&resume) == ESP_OK);
    pico_boot_init(false);
    assert(!pico_boot_recovery() && pico_boot_take_resume(&out));
    assert(out.reader && out.fullscreen && !strcmp(out.path, resume.path));
    assert(!pico_boot_take_resume(&out) && !durable.resume_valid);
    // 恢复过程中又复位，入口已消费且启动未完成，回到保护模式。
    // Reset again while resuming: consumption and incomplete startup prevent a loop.
    pico_boot_init(false); assert(pico_boot_recovery() && !pico_boot_take_resume(&out));
    assert(s_recovery_reasons==BOOT_RECOVERY_STARTING);
    pico_boot_ready(); pico_boot_init(false); assert(!pico_boot_recovery()); pico_boot_ready();
    // 密码取消/深睡再次开机不能丢失恢复，但实际开书前必须消费。
    // PIN cancellation and another deep boot retain resume; actual opening still consumes it.
    assert(pico_boot_save_resume(&resume) == ESP_OK);
    assert(pico_boot_mark_frame_cached()==ESP_OK); pico_boot_init(false); pico_boot_ready();
    pico_boot_hold_resume(); assert(durable.resume_valid && durable.reserved[0]);
    pico_boot_init(false); assert(!pico_boot_recovery()); pico_boot_ready(); pico_boot_hold_resume();
    assert(pico_boot_take_resume(&out) && !strcmp(out.path, resume.path) && !durable.resume_valid);
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_init(false); pico_boot_ready(); pico_boot_hold_resume();
    fail_commit=true; assert(!pico_boot_take_resume(&out)); fail_commit=false; pico_boot_clear_resume();
    assert(pico_boot_book_begin("/sdcard/books/large.epub") == ESP_OK);
    pico_boot_init(false); assert(pico_boot_recovery());
    assert(s_recovery_reasons==BOOT_RECOVERY_OPENING);
    assert(pico_boot_interrupted_book(interrupted, sizeof(interrupted)) && !strcmp(interrupted, "/sdcard/books/large.epub"));
    assert(!pico_boot_interrupted_book(interrupted, 8));
    pico_boot_ready();
    pico_boot_leave_book_recovery();
    assert(!pico_boot_recovery() && !pico_boot_interrupted_book(interrupted, sizeof(interrupted)));
    assert(!pico_boot_asset_allowed("/sdcard/books/large.epub") && pico_boot_asset_allowed(resume.path));
    assert(pico_boot_book_begin(resume.path) == ESP_OK);
    pico_boot_book_end(true);
    pico_boot_init(false); pico_boot_ready();
    assert(!pico_boot_recovery() && !pico_boot_asset_allowed("/sdcard/books/large.epub") && pico_boot_asset_allowed(resume.path));
    // 密码取消/深睡再次开机不能丢失恢复，但实际开书前必须消费。
    // PIN cancellation and another deep boot retain resume; actual opening still consumes it.
    assert(pico_boot_save_resume(&resume) == ESP_OK);
    assert(pico_boot_mark_frame_cached()==ESP_OK); pico_boot_init(false); pico_boot_ready();
    pico_boot_hold_resume(); assert(durable.resume_valid && durable.reserved[0]);
    pico_boot_init(false); assert(!pico_boot_recovery()); pico_boot_ready(); pico_boot_hold_resume();
    assert(pico_boot_take_resume(&out) && !strcmp(out.path, resume.path) && !durable.resume_valid);
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_init(false); pico_boot_ready(); pico_boot_hold_resume();
    fail_commit=true; assert(!pico_boot_take_resume(&out)); fail_commit=false; pico_boot_clear_resume();
    assert(pico_boot_book_begin("/sdcard/books/large.epub") == ESP_OK); pico_boot_book_end(true);
    assert(pico_boot_asset_allowed("/sdcard/books/large.epub"));
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_clear_resume();
    pico_boot_init(false); assert(!pico_boot_take_resume(&out)); pico_boot_ready();
    assert(pico_boot_save_resume(&resume) == ESP_OK); pico_boot_init(true);
    assert(pico_boot_recovery() && !pico_boot_take_resume(&out) && !pico_boot_interrupted_book(interrupted, sizeof(interrupted)));
    assert(s_recovery_reasons==BOOT_RECOVERY_RESET);
#if KIIKO_BOARD_METALIO47
    // WDT保护禁用字体/开书，但不能顺便取消锁屏。/ Watchdog recovery blocks fonts/books without clearing the lock gate.
    assert(pico_boot_lock_pending() && !pico_boot_has_resume());
    pico_boot_clear_resume();assert(!pico_boot_lock_pending());
#endif
    pico_boot_ready();
    resume.path[0]=0; assert(pico_boot_save_resume(&resume)==ESP_ERR_INVALID_ARG);
    strcpy(resume.path, "/sdcard/../danger.epub"); assert(pico_boot_save_resume(&resume)==ESP_ERR_INVALID_ARG);
    assert(pico_boot_book_begin("/sdcard/../danger.epub")==ESP_ERR_INVALID_ARG);
    assert(pico_boot_book_begin("/other/book.epub")==ESP_ERR_INVALID_ARG);
    durable_raw[durable_size-1] ^= 1;
    pico_boot_init(false); assert(pico_boot_recovery()); pico_boot_ready();
    assert(s_recovery_reasons==BOOT_RECOVERY_RECORD);
#if KIIKO_BOARD_METALIO47
    assert(!pico_boot_lock_pending());
#endif
    fail_commit=true;
    assert(pico_boot_book_begin("/sdcard/book.epub")==ESP_FAIL);
    pico_boot_init(false); assert(pico_boot_recovery() && !pico_boot_take_resume(&out));
    assert(s_recovery_reasons==BOOT_RECOVERY_STORE);
    fail_commit=false; fail_open=true;
    pico_boot_init(false); assert(pico_boot_recovery() && pico_boot_book_begin("/sdcard/book.epub")!=ESP_OK);
    assert(s_recovery_reasons==(BOOT_RECOVERY_RECORD|BOOT_RECOVERY_STORE));
    fail_open=false;
    assert(commits > 0);
    present=false;memset(&durable,0,sizeof(durable));pico_boot_init(false);pico_boot_ready();
    strcpy(resume.path,"/sdcard/books/deep.epub");
    assert(pico_boot_save_resume(&resume)==ESP_OK&&pico_boot_mark_frame_cached()==ESP_OK);
    pico_boot_init(false);assert(pico_boot_has_resume()&&pico_boot_resume_frame_cached());
#if KIIKO_BOARD_METALIO47
    assert(durable.reserved[0]==1&&durable.resume_valid&&pico_boot_lock_pending());
#else
    assert(durable.reserved[0]==0&&!durable.resume_valid);
#endif
    // 多次等待/深睡都保留真实锁图标记，但新开书前仍消费一次性入口。
    // Repeated locked boots retain the actual-art marker while book admission remains one-shot.
    for(unsigned cycle=0;cycle<4;++cycle){
        pico_boot_ready();pico_boot_hold_resume();
        assert(durable.resume_valid&&durable.reserved[0]==1);
        pico_boot_init(false);assert(pico_boot_has_resume()&&pico_boot_resume_frame_cached());
    }
    assert(pico_boot_take_resume(&out)&&!pico_boot_resume_frame_cached());pico_boot_ready();
    assert(pico_boot_save_resume(&resume)==ESP_OK);fail_commit=true;
    assert(pico_boot_mark_frame_cached()==ESP_FAIL);fail_commit=false;
    pico_boot_init(false);assert(pico_boot_has_resume()&&!pico_boot_resume_frame_cached());
    pico_boot_ready();assert(pico_boot_take_resume(&out));
    // 紧凑记录迁移旧格式，非图书缓存不再隔离；真实 EPUB 中断仍拦截。
    // Migrate legacy records and ignore non-book cache entries while retaining EPUB quarantine.
    boot_record_t legacy={.resume_valid=1,.resume={.tab=1,.reader=1,.fullscreen=1,.path="/sdcard/books/deep.epub"}};
    seed_legacy(legacy);pico_boot_init(false);
    assert(pico_boot_take_resume(&out)&&!strcmp(out.path,legacy.resume.path));
    assert(durable_size<128);pico_boot_ready();
    strcpy(legacy.resume.path,"/sdcard/books/catalog.wread");
    strcpy(legacy.opening,legacy.resume.path);strcpy(legacy.blocked,legacy.resume.path);
    seed_legacy(legacy);pico_boot_init(false);
    assert(!pico_boot_recovery()&&!pico_boot_take_resume(&out)&&!pico_boot_interrupted_book(interrupted,sizeof(interrupted)));
    assert(!durable.opening[0]&&!durable.blocked[0]);pico_boot_ready();
    assert(pico_boot_book_begin(legacy.resume.path)==ESP_ERR_INVALID_ARG);
    assert(pico_boot_save_resume(&legacy.resume)==ESP_ERR_INVALID_ARG);
    // NVS 余量不足时只回收本模块旧键；其他命名空间不会被触及。
    // Low-space recovery erases only this module's old key, never other namespaces.
    legacy=(boot_record_t){0};seed_legacy(legacy);free_bytes=0;erases=0;
    pico_boot_init(false);assert(!pico_boot_recovery()&&erases==1&&durable_size<64);
    pico_boot_ready();assert(pico_boot_book_begin("/sdcard/books/retry.epub")==ESP_OK);
    assert(settings_sentinel==0x7351&&progress_sentinel==0x19af);
    pico_boot_init(false);assert(pico_boot_interrupted_book(interrupted,sizeof(interrupted)));
    assert(!strcmp(interrupted,"/sdcard/books/retry.epub"));pico_boot_ready();
    free_bytes=0;erases=0;fail_set_after_erase=true;
    assert(pico_boot_book_begin("/sdcard/books/failed.epub")!=ESP_OK);
    assert(!present&&erases==1);fail_set_after_erase=false;free_bytes=SIZE_MAX;
    // 回收窗口断电后留在首页，不自动尝试解析；随后正常写入与手动开书可恢复。
    // Power loss in the reclaim window stays Home without parsing; later manual opening can recover.
    pico_boot_init(false);assert(!pico_boot_take_resume(&out));pico_boot_ready();
    fail_open=true;pico_boot_init(false);fail_open=false;
    assert(pico_boot_book_begin("/sdcard/books/recovered.txt")==ESP_OK);pico_boot_book_end(true);
    // A reset during TXT first paint skips automatic resume even with a later lock record.
    strcpy(resume.path, "/sdcard/books/imported.txt");
    assert(pico_boot_book_begin(resume.path) == ESP_OK);
    pico_boot_init(true);
    assert(pico_boot_interrupted_book(interrupted, sizeof(interrupted)) && !strcmp(interrupted, resume.path));
    assert(!pico_boot_take_resume(&out));
    pico_boot_ready();
    assert(pico_boot_save_resume(&resume) == ESP_OK);
    pico_boot_init(false);
    assert(!pico_boot_recovery() && !pico_boot_take_resume(&out) && !pico_boot_asset_allowed(resume.path));
    pico_boot_clear_resume();
    pico_boot_ready();
    assert(pico_boot_book_begin(resume.path) == ESP_OK); // Manual retry is allowed.
    pico_boot_book_end(true);
    assert(pico_boot_asset_allowed(resume.path));
    assert(pico_boot_save_resume(&resume) == ESP_OK);
    pico_boot_init(false);
    assert(pico_boot_take_resume(&out) && !strcmp(out.path, resume.path));
    pico_boot_ready();
    seed_legacy(legacy);free_bytes=0;erases=0;fail_commit=true;
    assert(pico_boot_book_begin("/sdcard/books/unadmitted.epub")==ESP_FAIL);
    assert(present&&durable_size==sizeof(legacy)&&erases==1);
    fail_commit=false;free_bytes=SIZE_MAX;
    puts("PASS: compact/legacy startup records; interrupted EPUB quarantine; unsupported cache ignored; low-space key-only reclaim; interrupted reclaim and failed commits stay safe; retry after transient NVS failure; settings/progress preserved");
    puts("PASS: one-shot reader/fullscreen resume; power-cut book quarantine; explicit retry; no false book blame; absent/corrupt NVS and commit failure; checked paths");
}

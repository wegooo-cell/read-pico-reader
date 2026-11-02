#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Mock driver regression for the real SD lifecycle / 实际 SD 生命周期的模拟驱动回归。"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/sd-media-test'
out.mkdir(parents=True, exist_ok=True)
for name in ('driver/sdmmc_host.h', 'esp_log.h', 'esp_vfs_fat.h',
             'freertos/FreeRTOS.h', 'freertos/task.h', 'read_pico_board.h',
             'esp_timer.h', 'sdmmc_cmd.h', 'esp_err.h', 'esp_heap_caps.h', 'metalio_hw.h', 'esp_attr.h', 'esp_system.h',
             'esp_memory_utils.h','esp_private/esp_cache_private.h'):
    p = out / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('#pragma once\n', encoding='utf-8')
for name in ('ff.h','diskio_sdmmc.h'):
    (out/name).write_text((root/'tools/sd_probe_stubs'/name).read_text())
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_NO_MEM 6
#define ESP_ERR_NOT_SUPPORTED 7
#define GPIO_NUM_38 38
#define GPIO_NUM_20 20
#define GPIO_NUM_21 21
#define GPIO_NUM_22 22
#define GPIO_NUM_23 23
#define GPIO_NUM_24 24
#define GPIO_NUM_25 25
#define SDMMC_HOST_SLOT_0 0
#define GPIO_NUM_42 42
#define GPIO_NUM_44 44
#define GPIO_NUM_NC -1
#define SDMMC_FREQ_HIGHSPEED 40000
#define RTC_DATA_ATTR
#define ESP_RST_DEEPSLEEP 5
static int esp_reset_reason(void){return 0;}
static int metalio_sd_single_line_idle(void){return ESP_OK;}
#define SDMMC_HOST_FLAG_DEINIT_ARG 8
#define SDMMC_HOST_FLAG_1BIT 1
#define SDMMC_HOST_FLAG_4BIT 2
#define SDMMC_HOST_FLAG_8BIT 4
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){.init=mock_host_init,.deinit=mock_host_deinit})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
static esp_err_t mock_host_init(void){return ESP_OK;}
static esp_err_t mock_host_deinit(void){return ESP_OK;}
typedef struct { int max_freq_khz,slot,flags; esp_err_t (*init)(void),(*deinit)(void); esp_err_t (*deinit_p)(int); bool (*check_buffer_alignment)(int,const void*,size_t); void* dma_aligned_buffer; size_t unaligned_multi_block_rw_max_chunk_size; } sdmmc_host_t;
typedef struct { int width,clk,cmd,d0,d1,d2,d3,cd,wp,flags; } sdmmc_slot_config_t;
typedef struct { bool format_if_mount_failed; int max_files,allocation_unit_size; } esp_vfs_fat_sdmmc_mount_config_t;
typedef struct { struct { char name[8]; } cid; struct { uint32_t capacity,sector_size; } csd; } sdmmc_card_t;
static bool access_busy;
bool read_pico_sd_access_transition_begin(uint32_t ms){(void)ms;return !access_busy;}
void read_pico_sd_access_transition_end(void){}
void read_pico_sd_access_set_mounted(bool mounted){(void)mounted;}
#define SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF 64
static bool sdmmc_host_check_buffer_alignment(int slot,const void* p,size_t n){(void)slot;return (uintptr_t)p%4==0&&n%4==0;}
static bool esp_ptr_internal(const void* p){(void)p;return true;}
static esp_err_t esp_cache_get_alignment(unsigned caps,size_t* a){(void)caps;*a=64;return ESP_OK;}
static void* heap_caps_aligned_alloc(size_t a,size_t n,int caps){(void)caps;return aligned_alloc(a,n);}
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
static void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static void heap_caps_free(void *p){free(p);}
static esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config){(void)slot;(void)config;return ESP_OK;}
static esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card){(void)host;(void)card;return ESP_ERR_TIMEOUT;}
static esp_err_t sdmmc_read_sectors(sdmmc_card_t *card,void *buffer,size_t start,size_t count){(void)card;(void)start;assert(count==1);memset(buffer,0,512);return ESP_OK;}
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) assert(pthread_mutex_lock(p)==0)
#define portEXIT_CRITICAL(p) assert(pthread_mutex_unlock(p)==0)
typedef int BaseType_t;
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static bool present=true, drop_during_mount=false, unmount_fail=false, detect_fail=false;
static int mounts, unmounts, formats;
static int64_t test_now_us;
static int status_error;
static int64_t esp_timer_get_time(void){return test_now_us;}
static bool made_books, made_fonts, made_pictures;
static sdmmc_card_t mock_card={.cid={"MOCK"},.csd={2048,512}};
static int sdmmc_get_status(sdmmc_card_t *c){assert(c==&mock_card);return status_error;}
static void (*pending)(void*);
esp_err_t read_pico_sd_detect(bool* detected);
static void vTaskDelay(int n) {(void)n;}
static void vTaskDelete(void* p) {(void)p;}
static int xTaskCreate(void (*f)(void*), const char* n,int z,void* a,int pr,void* h) {
    (void)n;(void)z;(void)a;(void)pr;(void)h;assert(!pending);pending=f;return pdPASS;
}
static int esp_vfs_fat_info(const char* p,uint64_t* total,uint64_t* freeb) {(void)p;*total=1048576;*freeb=524288;return 0;}
static int esp_vfs_fat_sdmmc_mount(const char* p,const sdmmc_host_t* h,const sdmmc_slot_config_t* s,const esp_vfs_fat_sdmmc_mount_config_t* c,sdmmc_card_t** card) {
    (void)p;(void)h;(void)s;assert(!c->format_if_mount_failed);mounts++;*card=&mock_card;if(drop_during_mount)present=false;return 0;
}
static int esp_vfs_fat_sdcard_unmount(const char* p,sdmmc_card_t* c){(void)p;assert(c==&mock_card);unmounts++;return unmount_fail ? ESP_FAIL : ESP_OK;}
static int esp_vfs_fat_sdcard_format(const char* p,sdmmc_card_t* c){(void)p;(void)c;formats++;return 0;}
static void metalio_sd_release_hold(void){}
static void metalio_usb_serial_reconnect(void){}
static int mock_mkdir(const char* p,int mode){
    (void)mode;
    if(!strcmp(p,"/sdcard/books")) made_books=true;
    if(!strcmp(p,"/sdcard/fonts")) made_fonts=true;
    if(!strcmp(p,"/sdcard/pictures")) made_pictures=true;
    return 0;
}
#define mkdir mock_mkdir
#include "../../components/read_pico/read_pico_sd.c"
// 此测试验证拔卡生命周期；空间计数由 sd_probe_host_test 独立覆盖。
// This fixture checks media lifecycle; allocation counting is covered by sd_probe_host_test.
BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *c){(void)c;return 0;}
int f_opendir(FF_DIR *root,const char *path){(void)path;root->obj.fs=NULL;return FR_OK;}
int f_readdir(FF_DIR *root,FILINFO *entry){(void)root;entry->fname[0]=0;return FR_OK;}
int f_closedir(FF_DIR *root){(void)root;return FR_OK;}
int ff_mutex_take(int drive){(void)drive;return 1;}
void ff_mutex_give(int drive){(void)drive;}
// 测试实际板级检测函数；FCA 读错不应写出“无卡”。/ Exercise real board detect; failed FCA reads must not publish absence.
#define IOE_SD_CD (1u << 6)
static void* s_ioe=(void*)1;
static esp_err_t fca9555_read_reg(void* h, uint8_t reg, uint8_t* value) {
    (void)h; assert(reg==0);
    if(detect_fail) return ESP_ERR_TIMEOUT;
    *value=present ? 0 : 0xff;
    return ESP_OK;
}
@BOARD_DETECT@
static void finish_probe(void) {assert(pending);void(*f)(void*)=pending;pending=NULL;f(NULL);}
static void* snapshot_reader(void* arg) {
    (void)arg;
    for(int i=0;i<20000;i++) {
        read_pico_sd_info_t info;
        int err=read_pico_sd_get_info(&info);
        if(err==ESP_OK) assert(info.mounted && info.capacity_bytes==1048576 && !info.space_valid && info.space_error==ESP_ERR_NOT_FINISHED);
    }
    return NULL;
}
int main(void) {
    read_pico_sd_info_t info;
    assert(read_pico_sd_detect(NULL)==ESP_ERR_INVALID_ARG);
    bool detected=true;
    s_ioe=NULL;
    assert(read_pico_sd_detect(&detected)==ESP_ERR_INVALID_STATE && detected);
    assert(!read_pico_sd_present());
    s_ioe=(void*)1;
    detect_fail=true;
    assert(read_pico_sd_detect(&detected)==ESP_ERR_TIMEOUT && detected);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_TIMEOUT && !info.mounted);
    detect_fail=false;
    assert(read_pico_sd_get_info(NULL)==ESP_ERR_INVALID_ARG);
    assert(read_pico_sd_start_probe()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_sync()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && info.capacity_bytes);
    assert(made_books && made_fonts && made_pictures);
    detect_fail=true;
    for(int i=0;i<20;i++) assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && info.capacity_bytes);
    assert(read_pico_sd_start_probe()==ESP_OK && mounts==1 && unmounts==0);
    detect_fail=false;
    present=false;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND);
    assert(!info.mounted && !info.needs_format && !info.capacity_bytes && !info.free_bytes && !info.name[0]);
    assert(unmounts==0 && card==&mock_card);
    present=true;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && info.present && !info.mounted);
    assert(read_pico_sd_start_probe()==ESP_ERR_INVALID_STATE && mounts==1);
    assert(read_pico_sd_format()==ESP_ERR_INVALID_STATE && formats==0);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED && unmounts==1);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && mounts==2);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    present=false;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND);
    present=true;
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.mounted && !info.needs_format);
    // 真拔卡之后的检测读错不能让探测结果绕过失效锁存。
    // A detection fault after real removal must not let a probe bypass the removal latch.
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    present=false;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND);
    present=true;
    detect_fail=true;
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.mounted && !info.capacity_bytes);
    assert(read_pico_sd_format()==ESP_ERR_INVALID_STATE && formats==0);
    detect_fail=false;
    drop_during_mount=true;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND && !info.mounted);
    present=true;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE);
    drop_during_mount=false;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_OK);
    detect_fail=true;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted);
    detect_fail=false;
    assert(read_pico_sd_sync()==ESP_OK);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.mounted && !info.capacity_bytes);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    unmount_fail=true;
    void* retained_dma=sd_dma_buffer;assert(retained_dma);
    assert(read_pico_sd_sync()==ESP_FAIL && card==&mock_card);
    assert(sd_dma_buffer==retained_dma);
    assert(read_pico_sd_get_info(&info)==ESP_FAIL && info.mounted);
    unmount_fail=false;
    assert(read_pico_sd_sync()==ESP_OK && card==NULL);
    assert(sd_dma_buffer==NULL);
    pthread_t reader;
    assert(pthread_create(&reader,NULL,snapshot_reader,NULL)==0);
    for(int i=0;i<100;i++) {
        assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
        finish_probe();
    }
    assert(pthread_join(reader,NULL)==0);
    assert(formats==0);
    access_busy=true;int before=unmounts;
    assert(read_pico_sd_sync()==ESP_ERR_NOT_FINISHED && unmounts==before);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted);
    access_busy=false;
    // S31无CD脚时靠三次CMD13错误确认；忙时不得卸载，USB主动卸载不自动恢复。
    // With no CD pin, confirm three CMD13 failures; never unmount a busy owner or auto-remount USB.
    status_error=ESP_ERR_TIMEOUT;
    for(int i=0;i<3;++i){test_now_us+=i?500000:2001000;read_pico_sd_poll_health();assert(pending);assert(read_pico_sd_sync()==ESP_ERR_NOT_FINISHED);finish_probe();}
    assert(read_pico_sd_needs_recovery()&&!read_pico_sd_io_ready());
    status_error=ESP_OK;assert(read_pico_sd_start_recovery()==ESP_OK);assert(!read_pico_sd_io_ready());finish_probe();
    assert(read_pico_sd_io_ready());status_error=ESP_OK;
    assert(read_pico_sd_sync()==ESP_OK&&!read_pico_sd_needs_recovery());
    test_now_us+=3000000;read_pico_sd_poll_health();assert(!pending);

    puts("PASS: injected media-provider faults on S31 SD lifecycle, unknown mount, removal, stale reinsertion, busy lifecycle, mid-probe removal, explicit recovery, concurrent snapshots, no autoformat");
}
'''
# A fault-injection provider for the shared lifecycle, not the Metalio board's absent CD pin.
detect = r"""
esp_err_t read_pico_sd_detect(bool *detected){
 if(!detected)return ESP_ERR_INVALID_ARG;
 if(!s_ioe)return ESP_ERR_INVALID_STATE;
 uint8_t value;esp_err_t err=fca9555_read_reg(s_ioe,0,&value);
 if(err==ESP_OK)*detected=!(value&IOE_SD_CD);return err;
}
bool read_pico_sd_present(void){bool value=false;return read_pico_sd_detect(&value)==ESP_OK&&value;}
"""
(out / 'test.c').write_text(source.replace('@BOARD_DETECT@', detect), encoding='utf-8')
subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-variable', '-fsanitize=address,undefined', '-g', '-pthread',
                '-I'+str(out), '-I'+str(root/'components/read_pico/include'),
                str(out/'test.c'), '-o', str(out/'test')], check=True)
subprocess.run([str(out/'test')], check=True)

/*
 * 首次无卡启动后插卡应可挂载；拔出已挂载卡仍须显式重挂。
 * A first insertion after empty boot mounts; removal of a mounted card still requires explicit remount.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_vfs_fat.h"
#include "freertos/task.h"

static bool access_busy;
bool read_pico_sd_access_transition_begin(uint32_t ms){(void)ms;return !access_busy;}
void read_pico_sd_access_transition_end(void){}
void read_pico_sd_access_set_mounted(bool mounted){(void)mounted;}
static bool raw_mode,raw_need_single,raw_deinit_fail;
static int raw_clock,raw_width,raw_limit=20000,raw_inits,raw_stops;
static bool test_present, test_exfat, test_raw_error;
static bool detect_failure, detect_unsupported;
static int mount_calls, unmount_calls, test_fail_above_khz;
static esp_err_t test_mount_error = ESP_ERR_TIMEOUT;
static int clock_history[256],width_history[256], delays;
static bool test_need_single;
static int verified_fail_above_khz,active_khz;
static unsigned verified_reads;
int test_reset_reason;
static uint64_t test_total=1024*512,test_free=768*512;
static esp_err_t test_info_error=ESP_OK;
static unsigned space_calls, invalidations;
static bool fat_locked;
static bool defer_manual,fail_manual_task,fail_unmount;
static void (*queued_manual)(void*);
static void *queued_manual_arg;
static unsigned format_calls;

static sdmmc_card_t test_card = {
    .cid.name = "TEST64G", .csd.capacity = 1024, .csd.sector_size = 512,
};

#define mkdir(path, mode) (0)
#include "../components/read_pico/read_pico_sd.c"
#undef mkdir

int64_t esp_timer_get_time(void){return 0;}
esp_err_t sdmmc_get_status(sdmmc_card_t *c){assert(c==&test_card || (raw_mode && c==usb_card));return ESP_OK;}
static FATFS test_fat={.free_clst=1};
BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *c){assert(c==&test_card);return 0;}
int f_opendir(FF_DIR *root,const char *path){assert(!strcmp(path,"0:"));root->obj.fs=&test_fat;return FR_OK;}
int f_readdir(FF_DIR *root,FILINFO *entry){assert(root->obj.fs==&test_fat);entry->fname[0]=0;return FR_OK;}
int f_closedir(FF_DIR *root){assert(root->obj.fs==&test_fat);return FR_OK;}
int ff_mutex_take(int drive){assert(drive==0&&!fat_locked);fat_locked=true;return 1;}
void ff_mutex_give(int drive){assert(drive==0&&fat_locked&&test_fat.free_clst==UINT32_MAX);fat_locked=false;++invalidations;}
esp_err_t test_host_init(void){if(raw_mode)++raw_inits;return ESP_OK;}
esp_err_t test_host_deinit(void){if(raw_mode){++raw_stops;if(raw_deinit_fail)return ESP_FAIL;}return ESP_OK;}
esp_err_t test_host_deinit_slot(int slot){assert(slot==0);return test_host_deinit();}
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config){assert(slot==0);raw_width=config->width;return ESP_OK;}
esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *out){
 *out=test_card;
 if(raw_mode){raw_clock=host->max_freq_khz;assert(raw_clock<=20000);
  assert(host->dma_aligned_buffer==sd_dma_buffer && host->unaligned_multi_block_rw_max_chunk_size==8);
  assert(host->check_buffer_alignment==sd_internal_dma_buffer);
  if(raw_clock>raw_limit || (raw_need_single && raw_width!=1))return ESP_ERR_TIMEOUT;
 }
 return test_raw_error?ESP_ERR_TIMEOUT:ESP_OK;
}
esp_err_t sdmmc_read_sectors(sdmmc_card_t *c,void *buffer,size_t start,size_t count){(void)c;(void)start;assert(count==1);++verified_reads;if(verified_fail_above_khz&&active_khz>verified_fail_above_khz)return ESP_ERR_TIMEOUT;if(test_raw_error)return ESP_ERR_TIMEOUT;memset(buffer,0x5a,512);if(test_exfat)memcpy((char*)buffer+3,"EXFAT   ",8);return ESP_OK;}
bool read_pico_sd_present(void) { return test_present; }
esp_err_t read_pico_sd_detect(bool* present) {
    if (detect_failure) return ESP_ERR_TIMEOUT;
    if (detect_unsupported) return ESP_ERR_NOT_SUPPORTED;
    *present = test_present;
    return ESP_OK;
}
const char* esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
BaseType_t xTaskCreate(void (*fn)(void*), const char* name, int stack,
                       void* arg, int priority, void* handle) {
    (void)name; (void)stack; (void)priority; (void)handle;
    if(!strcmp(name,"sd_manual")){
        if(fail_manual_task)return !pdPASS;
        if(defer_manual){assert(!queued_manual);queued_manual=fn;queued_manual_arg=arg;return pdPASS;}
    }
    fn(arg);
    return pdPASS;
}
void vTaskDelay(int ticks) { assert(ticks == 250); ++delays; }
void vTaskDelete(void* handle) { (void)handle; }
esp_err_t esp_vfs_fat_sdmmc_mount(const char* path, const sdmmc_host_t* host,
                                  const sdmmc_slot_config_t* slot,
                                  const esp_vfs_fat_sdmmc_mount_config_t* config,
                                  sdmmc_card_t** out) {
    assert(!strcmp(path,"/sdcard") && host->slot==0);
    assert((slot->width==4||slot->width==1) && slot->clk==GPIO_NUM_24 && slot->cmd==GPIO_NUM_25 && slot->d0==GPIO_NUM_20);
    if(slot->width==4)assert(slot->d1==GPIO_NUM_21&&slot->d2==GPIO_NUM_22&&slot->d3==GPIO_NUM_23);
    else assert(slot->d1==GPIO_NUM_NC&&slot->d2==GPIO_NUM_NC&&slot->d3==GPIO_NUM_NC &&
        !(host->flags&(SDMMC_HOST_FLAG_4BIT|SDMMC_HOST_FLAG_8BIT))&&(host->flags&SDMMC_HOST_FLAG_1BIT));
    assert(mount_calls < 256);
    clock_history[mount_calls]=host->max_freq_khz;width_history[mount_calls++]=slot->width;
    assert(test_present);
    assert(!config->format_if_mount_failed);
    assert(host->dma_aligned_buffer==test_dma_live&&test_dma_live);
    assert(host->unaligned_multi_block_rw_max_chunk_size==8);
    assert(!(host->flags&SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF));
    assert(host->check_buffer_alignment(0,test_dma_live,4096));
    assert(!host->check_buffer_alignment(0,(char*)test_dma_live+1,4096));
    test_external_dma_ptr=test_dma_live;
    assert(!host->check_buffer_alignment(0,test_dma_live,4096));
    test_external_dma_ptr=NULL;
    if((test_need_single&&slot->width!=1)||(test_fail_above_khz && host->max_freq_khz > test_fail_above_khz)){*out=NULL;return test_mount_error;}
    active_khz=host->max_freq_khz;assert(config->max_files==16);
    *out = &test_card;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_unmount(const char* path, sdmmc_card_t* mounted) {
    (void)path;
    assert(mounted == &test_card);
    ++unmount_calls;
    if(fail_unmount)return ESP_FAIL;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_format(const char* path, sdmmc_card_t* mounted) {
    (void)path; (void)mounted;
    ++format_calls;
    return ESP_OK;
}
esp_err_t esp_vfs_fat_info(const char* path, uint64_t* total, uint64_t* free_bytes) {
    (void)path;
    ++space_calls;
    assert(!fat_locked);
    // 模拟真实 f_getfree：作废 FSInfo 后扫描，后续直接复用随写入变化的计数。
    // Like f_getfree, unknown FSInfo scans once; later counts follow allocation changes.
    if (test_fat.free_clst==UINT32_MAX) test_fat.free_clst=123;
    *total=test_total;*free_bytes=test_free;
    return test_info_error;
}

int main(void) {
    read_pico_sd_info_t info = {0};
    assert(read_pico_sd_start_probe() == ESP_ERR_NOT_FOUND);
    assert(read_pico_sd_get_info(&info) == ESP_ERR_NOT_FOUND);
    assert(!info.present && !info.mounted && mount_calls == 0);

    test_present = true;
    assert(read_pico_sd_get_info(&info) == ESP_ERR_INVALID_STATE);
    assert(info.present && !info.mounted);
    assert(read_pico_sd_start_probe() == ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info) == ESP_OK);
    assert(info.present && info.mounted && mount_calls == 1);

    detect_failure = true;
    assert(read_pico_sd_get_info(&info) == ESP_OK && info.mounted && info.capacity_bytes);
    assert(read_pico_sd_start_probe() == ESP_OK && mount_calls == 1);
    detect_failure = false;

    test_present = false;
    assert(read_pico_sd_get_info(&info) == ESP_ERR_NOT_FOUND);
    test_present = true;
    assert(read_pico_sd_start_probe() == ESP_ERR_INVALID_STATE);
    assert(read_pico_sd_remount() == ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info) == ESP_OK);
    assert(info.mounted && mount_calls == 2 && unmount_calls == 1);
    test_card.csd.capacity = 536870912u; // 256 GiB / 512 byte sectors, exceeds 32-bit byte range.
    test_fail_above_khz=10000;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.mounted);
    assert(info.capacity_bytes==256ull*1024*1024*1024);
    assert(mount_calls==6 && delays==3);
    assert(clock_history[2]==40000 && clock_history[3]==40000 && clock_history[4]==20000 && clock_history[5]==10000);

    // CMD6 忙不能终止降速；默认频率跳过高速切换。/ CMD6 busy must fall back; default speed skips HS negotiation.
    s_sd_last_profile=-1;s_sd_sleep_profile=0;
    test_mount_error=ESP_ERR_INVALID_STATE;
    test_fail_above_khz=20000;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && mount_calls==9);
    assert(clock_history[6]==40000 && clock_history[7]==40000 && clock_history[8]==20000);

    // 资源错误立即失败，初始化失败不建议格式化。/ Resource errors fail immediately; init failures never suggest format.
    test_mount_error=ESP_ERR_NO_MEM;
    test_fail_above_khz=1;
    int before=mount_calls, previous_delays=delays;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NO_MEM && !info.mounted && !info.needs_format);
    assert(mount_calls==before+1 && delays==previous_delays);
    test_mount_error=ESP_ERR_INVALID_STATE;
    before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.needs_format);
    assert(mount_calls==before+8 && delays==previous_delays+7);
    test_mount_error=ESP_FAIL;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_FAIL && !info.needs_format);

    test_exfat=true;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_FAIL && info.needs_format);
    test_raw_error=true;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_FAIL && !info.needs_format);
    test_raw_error=test_exfat=false;

    // CD 通信未知时仍尝试实际挂载，不能假设拔卡。/ Unknown CD still permits an actual mount, not assumed removal.
    detect_failure=true;
    test_fail_above_khz=0;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && info.present);
    detect_failure=false;
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted);
    assert(!info.space_valid && info.space_error==ESP_ERR_NOT_FINISHED && space_calls==0);
    assert(read_pico_sd_refresh_space()==ESP_OK);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.space_valid && info.filesystem_bytes==test_total && info.free_bytes==test_free);
    assert(invalidations>0);
    // 多分区卡只统计挂载分区；256GiB卡不能截断，挂载状态与统计错误独立。
    test_total=128ull*1024*1024*1024; test_free=100ull*1024*1024*1024;
    unsigned before_space=space_calls;
    assert(read_pico_sd_refresh_space()==ESP_OK && space_calls==before_space+1);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.capacity_bytes==256ull*1024*1024*1024 &&
           info.filesystem_bytes==test_total && info.filesystem_bytes-info.free_bytes==28ull*1024*1024*1024);
    test_free-=512*1024;
    assert(read_pico_sd_refresh_space()==ESP_OK);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.free_bytes==test_free);
    test_info_error=ESP_FAIL;
    assert(read_pico_sd_refresh_space()==ESP_FAIL);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.mounted&&!info.space_valid&&info.space_error==ESP_FAIL&&info.free_bytes==0);
    test_info_error=ESP_OK;test_free=test_total+1;
    assert(read_pico_sd_refresh_space()==ESP_FAIL);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&!info.space_valid);
    test_free=0;
    assert(read_pico_sd_refresh_space()==ESP_OK);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.space_valid&&info.free_bytes==0);
    // S31 V1.5 has no physical CD: unsupported detection preserves a mounted card.
    detect_unsupported=true;test_present=false;before_space=space_calls;
    assert(read_pico_sd_refresh_space()==ESP_OK && space_calls==before_space+1);
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && info.present && info.space_valid);
    detect_unsupported=false;
    test_present=false;before_space=space_calls;
    assert(read_pico_sd_refresh_space()==ESP_ERR_NOT_FOUND&&space_calls==before_space);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND&&!info.mounted&&!info.space_valid);
    assert(close_card()==ESP_OK&&!sd_dma_buffer&&!test_dma_live&&test_dma_allocs==test_dma_frees);
    test_present=true;before=mount_calls;
    test_dma_fail_alloc=true;
    assert(mount_card(false)==ESP_ERR_NO_MEM&&mount_calls==before&&!sd_dma_buffer);
    test_dma_fail_alloc=false;test_dma_alignment_error=true;
    assert(mount_card(false)==ESP_FAIL&&mount_calls==before&&!sd_dma_buffer);
    test_dma_alignment_error=false;
    assert(test_dma_allocs==test_dma_frees);
    // 慢四线卡：记忆成功档，浅睡卸载后单次命中；失败再下调，保持NVS不参与。
    // Slow four-line media reuses its profile across a sleep unmount, then lowers it after new faults, without NVS.
    test_mount_error=ESP_ERR_TIMEOUT;test_fail_above_khz=10000;test_need_single=false;
    s_sd_last_profile=-1;s_sd_sleep_profile=0;
    before=mount_calls;assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&mount_calls==before+4&&s_sd_last_profile==3);
    assert(read_pico_sd_sync()==ESP_OK);before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+1&&clock_history[before]==10000);
    test_fail_above_khz=5000;before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+2&&s_sd_last_profile==4);
    assert(clock_history[before]==10000&&clock_history[before+1]==5000);
    // SDK mount success alone is insufficient: failed real reads must cleanly fall back.
    assert(close_card()==ESP_OK);test_fail_above_khz=0;verified_fail_above_khz=10000;
    s_sd_last_profile=-1;s_sd_sleep_profile=0;before=mount_calls;int prior_unmounts=unmount_calls;unsigned prior_reads=verified_reads;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+4);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.mounted&&s_sd_last_profile==3);
    assert(unmount_calls==prior_unmounts+3&&verified_reads==prior_reads+4);
    verified_fail_above_khz=0;test_fail_above_khz=5000;
    // 单线只在四线失败后使用，成功后复用，单线5MHz失败时退到1MHz。
    // Try single-line only after four-line failure; reuse success and lower5MHz to1MHz when needed.
    s_sd_last_profile=-1;s_sd_sleep_profile=0;test_need_single=true;before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+7&&s_sd_last_profile==6);
    for(int i=0;i<6;i++)assert(width_history[before+i]==4);
    assert(width_history[before+6]==1&&clock_history[before+6]==5000);
    before=mount_calls;assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+1&&width_history[before]==1);
    test_fail_above_khz=1000;before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+2&&s_sd_last_profile==7);
    assert(clock_history[before]==5000&&clock_history[before+1]==1000);
    // 深睡只接受有界RTC提示；冷启/异常复位和损坏索引回原档，所有失败最多8次。
    // Deep wake accepts a bounded RTC hint; cold/abnormal reset or corrupt indices start high, with at most8 attempts.
    assert(close_card()==ESP_OK);s_sd_profile_loaded=false;s_sd_last_profile=-1;test_reset_reason=ESP_RST_DEEPSLEEP;
    before=mount_calls;assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+1&&clock_history[before]==1000&&width_history[before]==1);
    assert(close_card()==ESP_OK);s_sd_profile_loaded=false;s_sd_last_profile=-1;s_sd_sleep_profile=SD_PROFILE_COOKIE|255u;
    test_need_single=false;test_fail_above_khz=0;before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+1&&clock_history[before]==40000);
    assert(close_card()==ESP_OK);s_sd_profile_loaded=false;s_sd_last_profile=-1;s_sd_sleep_profile=SD_PROFILE_COOKIE|7u;test_reset_reason=0;
    before=mount_calls;assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+1&&clock_history[before]==40000&&width_history[before]==4);
    test_fail_above_khz=1;s_sd_last_profile=6;s_sd_sleep_profile=SD_PROFILE_COOKIE|6u;before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before+8);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_TIMEOUT&&!info.mounted&&s_sd_last_profile==6&&!sd_dma_buffer);
    assert(width_history[before]==1&&width_history[before+2]==4&&clock_history[before+2]==40000);
    test_single_idle_error=ESP_FAIL;before=mount_calls;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED&&mount_calls==before);
    assert(read_pico_sd_get_info(&info)==ESP_FAIL&&!info.mounted&&!info.needs_format&&!sd_dma_buffer);
    assert(test_single_idle_calls>0);
    assert(test_dma_allocs==test_dma_frees);
    // Manual recovery is asynchronous even from failed boot/paused wake; no formatter is used.
    unsigned old_formats=format_calls;test_single_idle_error=ESP_OK;test_fail_above_khz=0;test_need_single=false;
    defer_manual=true;suspended=true;media_invalidated=true;cached_info.present=false;cached_info.error=ESP_ERR_NOT_FOUND;
    before=mount_calls;assert(read_pico_sd_start_manual_recovery()==ESP_OK&&queued_manual&&manual_busy);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FINISHED&&mount_calls==before);
    assert(read_pico_sd_start_manual_recovery()==ESP_ERR_NOT_FINISHED);
    test_present=false;assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FINISHED&&manual_busy);test_present=true;
    void (*work)(void*)=queued_manual;queued_manual=NULL;work(queued_manual_arg);
    assert(!manual_busy&&!suspended&&read_pico_sd_get_info(&info)==ESP_OK&&info.mounted);
    before=unmount_calls;fail_manual_task=true;
    assert(read_pico_sd_start_manual_recovery()==ESP_ERR_NO_MEM&&!manual_busy&&unmount_calls==before);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.mounted);fail_manual_task=false;
    space_busy=true;assert(read_pico_sd_start_manual_recovery()==ESP_ERR_NOT_FINISHED);space_busy=false;
    health_busy=true;assert(read_pico_sd_start_manual_recovery()==ESP_ERR_NOT_FINISHED);health_busy=false;
    assert(unmount_calls==before&&!queued_manual);fail_unmount=true;
    assert(read_pico_sd_start_manual_recovery()==ESP_OK);work=queued_manual;queued_manual=NULL;work(queued_manual_arg);
    assert(!manual_busy&&read_pico_sd_get_info(&info)==ESP_FAIL&&card);fail_unmount=false;
    assert(read_pico_sd_start_manual_recovery()==ESP_OK);work=queued_manual;queued_manual=NULL;work(queued_manual_arg);
    assert(read_pico_sd_get_info(&info)==ESP_OK&&info.mounted&&format_calls==old_formats);
    // Snapshot invalidation never steals an active health/accounting/mount owner or unmounts live IO.
    before=unmount_calls;health_busy=true;
    assert(read_pico_sd_invalidate_io()==ESP_ERR_NOT_FINISHED&&unmount_calls==before);
    health_busy=false;space_busy=true;
    assert(read_pico_sd_invalidate_io()==ESP_ERR_NOT_FINISHED&&unmount_calls==before);
    space_busy=false;assert(read_pico_sd_invalidate_io()==ESP_OK&&card&&unmount_calls==before&&!read_pico_sd_io_ready());
    assert(read_pico_sd_needs_recovery()&&read_pico_sd_start_recovery()==ESP_OK);
    assert(read_pico_sd_io_ready()&&unmount_calls==before+1&&format_calls==old_formats);
    assert(close_card()==ESP_OK&&!sd_dma_buffer&&test_dma_allocs==test_dma_frees);
    // 真正调用共享驱动的MSC路径：降频/单线、内部DMA和失败所有权。
    // Exercise the actual shared MSC driver: fallback, internal DMA and failed-cleanup ownership.
    defer_manual=false;assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    raw_mode=true;raw_need_single=true;raw_limit=1000;
    sdmmc_card_t *exported=NULL;before=mount_calls;
    assert(read_pico_sd_usb_acquire(&exported)==ESP_OK && exported && usb_owned && !card);
    assert(raw_width==1 && raw_clock==1000 && raw_inits==8 && raw_stops==7);
    assert(sd_dma_buffer && !read_pico_sd_io_ready() && !read_pico_sd_needs_recovery());
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED && mount_calls==before);
    assert(read_pico_sd_start_manual_recovery()==ESP_ERR_NOT_FINISHED);
    void *retained_dma=sd_dma_buffer;raw_deinit_fail=true;
    assert(read_pico_sd_usb_release()==ESP_FAIL && usb_owned && usb_card==exported && sd_dma_buffer==retained_dma);
    raw_deinit_fail=false;assert(read_pico_sd_usb_release()==ESP_OK && !usb_owned && !usb_card && !sd_dma_buffer);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    raw_limit=0;raw_inits=raw_stops=0;exported=NULL;
    assert(read_pico_sd_usb_acquire(&exported)==ESP_ERR_TIMEOUT && !exported && !usb_owned && !sd_dma_buffer);
    assert(raw_inits==8 && raw_stops==8 && !usb_card);
    assert(read_pico_sd_usb_release()==ESP_OK);raw_mode=false;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(close_card()==ESP_OK && test_dma_allocs==test_dma_frees);
    puts("PASS: SD profiles/RTC and DMA, asynchronous manual recovery from failed/paused/removal states, lifecycle owners/OOM/unmount failure/retry/no autoformat/256GiB");
    return 0;
}

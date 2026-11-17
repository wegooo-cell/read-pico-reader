#include "host.h"
#include "read_pico_sd.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "read_pico_search.h"
#include "display.h"
#include "epd_highlevel.h"
#include "esp_app_desc.h"
#include "boot_state.h"
#include "esp_vfs_fat.h"
#include "sc7a20h.h"
#include "sc7a20h_lab.h"
#include "esp_timer.h"
#include "ff.h"
#include "ble_page_turner.h"
#include "ota_online.h"
#include "ota_update.h"
#include "app.h"
#include <string.h>
#include <time.h>
#include <sys/statvfs.h>

const EpdWaveform E0470_WAVEFORM={0},E0470_FOLLOW_WAVEFORM={0},E0470_TEXTTURN_WAVEFORM={0};
static bool wifi;
void sim_wifi_toggle(void){wifi=!wifi;}
bool sim_wifi_on(void){return wifi;}
int read_pico_sd_get_info(read_pico_sd_info_t*i){memset(i,0,sizeof(*i));i->present=i->mounted=true;strcpy(i->name,"Mac");i->capacity_bytes=16ULL*1024*1024*1024;i->free_bytes=8ULL*1024*1024*1024;return 0;}
int read_pico_sd_start_probe(void){return 0;}int read_pico_sd_remount(void){return 0;}int read_pico_sd_sync(void){return 0;}
int read_pico_sd_format(void){return ESP_ERR_NOT_SUPPORTED;}
const pmu_snapshot_t *read_pico_pmu_get(void){static pmu_snapshot_t p;p.present=p.status_ok=p.identity_ok=true;p.flags=PMU_STATUS_BATTERY_VALID;p.soc_permille=780;p.time_synced=true;p.unix_sec=time(NULL);return &p;}
void read_pico_transfer_get_status(read_pico_transfer_status_t*s){memset(s,0,sizeof(*s));s->wifi_enabled=s->network_ready=wifi;s->mode=READ_PICO_TRANSFER_MODE_STA;}
int read_pico_transfer_get_saved_wifi(char*out,bool*configured){if(out)strcpy(out,"Mac 模拟网络");if(configured)*configured=wifi;return 0;}
void epd_hl_set_all_white(EpdiyHighlevelState*h){if(h->front_fb)memset(h->front_fb,255,1216*684/2);}
uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState*h){return h->front_fb;}
void guard_draw_result(EpdiyHighlevelState*h,enum EpdDrawError e){(void)h;(void)e;}
void rails_keepalive(void){}void display_set_bulk_io(bool active){(void)active;}
enum EpdDrawError update_display_mode(EpdiyHighlevelState*h,enum EpdDrawMode m){return 0;}
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState*h,enum EpdDrawMode m){return 0;}
enum EpdDrawError update_display_full(EpdiyHighlevelState*h){return 0;}
enum EpdDrawError update_display_from_white(EpdiyHighlevelState*h){return 0;}
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState*h){return 0;}
enum EpdDrawError update_display_white(EpdiyHighlevelState*h){return 0;}
bool display_take_white_exit(void){return false;}
void display_main_transition_cancel(void){}
void display_main_transition_arm(const void*p,bool a,bool b){}
void display_main_transition_disarm(void){}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m,EpdRect a){return 0;}
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m,EpdRect a){return 0;}
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m,EpdRect a){return 0;}
const esp_app_desc_t *esp_app_get_description(void){static esp_app_desc_t a={.version="0.3.3-rc97",.project_name="Pico",.date="2026-10-12",.idf_ver="Mac simulator"};return &a;}
bool pico_boot_asset_allowed(const char*p){return true;}
bool pico_boot_recovery(void){return false;}
void pico_boot_book_end(bool ok){}
void pico_boot_clear_resume(void){}
void pico_boot_hold_resume(void){}
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
EM_JS(void,sim_random,(void *p,size_t n),{crypto.getRandomValues(HEAPU8.subarray(p,p+n));});
void esp_fill_random(void*p,size_t n){sim_random(p,n);}
#else
void esp_fill_random(void*p,size_t n){arc4random_buf(p,n);}
#endif
int esp_vfs_fat_spiflash_mount_rw_wl(const char*p,const char*label,const esp_vfs_fat_mount_config_t*c,wl_handle_t*w){*w=1;sim_mkdir(p,0700);return 0;}
int esp_vfs_fat_info(const char*p,uint64_t*total,uint64_t*free){*total=16ULL*1024*1024*1024;*free=8ULL*1024*1024*1024;return 0;}
int f_getfree(const char*p,DWORD*n,FATFS**fs){static FATFS f={.csize=8,.n_fatent=2000000};*fs=&f;*n=1000000;return 0;}

const EpdWaveform E0470_FULL_WAVEFORM={0};
const EpdWaveform *display_reader_text_waveform(void){return &E0470_TEXTTURN_WAVEFORM;}
void display_main_transition_shelf_page(void){}
enum EpdDrawError update_display_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m){return 0;}
enum EpdDrawError update_display_water_turn(EpdiyHighlevelState*h,EpdRect a,e0470_turn_dir_t d){return 0;}
static int turn_tick=14000;
void e0470_page_turn_set_tick_us(int n){turn_tick=n;}int e0470_page_turn_tick_us(void){return turn_tick;}void e0470_page_turn_release(void){}
bool app_poll_main_navigation(app_ctx_t*c){return false;}
int ui_key_hit_test(uint16_t x,uint16_t y){return y>=1216?(x<228?0:x<456?1:2):-1;}
int pico_boot_book_begin(const char*p){return 0;}
void pmu_selftest_prepare_powerdown(void){}
int read_pico_pmu_cmd(uint16_t c,const uint8_t*p,uint8_t n){return ESP_ERR_NOT_SUPPORTED;}
void esp_restart(void){}
int read_pico_transfer_sync_time(uint32_t*s){return ESP_ERR_NOT_SUPPORTED;}
int read_pico_transfer_import_wifi_backup(const read_pico_transfer_wifi_backup_t*b){return ESP_ERR_NOT_SUPPORTED;}
int read_pico_transfer_export_wifi_backup(read_pico_transfer_wifi_backup_t*b){memset(b,0,sizeof(*b));return ESP_ERR_NOT_SUPPORTED;}
bool read_pico_transfer_wifi_backup_valid(const read_pico_transfer_wifi_backup_t*b){return false;}
static bool checked;
int pico_online_check(void){checked=true;return ESP_ERR_NOT_SUPPORTED;}int pico_online_download(void){return ESP_ERR_NOT_SUPPORTED;}int pico_online_commit(void){return ESP_ERR_NOT_SUPPORTED;}
bool pico_online_busy(void){return false;}void pico_online_cancel_join(void){}
void pico_online_get_status(pico_update_status_t*s){memset(s,0,sizeof(*s));s->state=checked?PICO_UPDATE_FAILED:PICO_UPDATE_IDLE;snprintf(s->message,sizeof(s->message),"模拟器尚未接入联网升级");}
int pico_ota_inspect(const char*p,pico_ota_info_t*i){memset(i,0,sizeof(*i));snprintf(i->current_version,sizeof(i->current_version),"0.3.3-rc97");snprintf(i->message,sizeof(i->message),"模拟器暂不运行设备BIN");return ESP_ERR_NOT_SUPPORTED;}
int pico_ota_install(const char*p,char*m,size_t n){snprintf(m,n,"请在真实Pico设备上升级");return ESP_ERR_NOT_SUPPORTED;}
void ble_pt_reset_failure(void){}int ble_pt_stop(uint32_t n){return 0;}bool ble_pt_running(void){return false;}
bool ble_pt_take_start_failure(char*out,size_t n){return false;}
void ble_pt_scan_start(uint32_t n){}void ble_pt_scan_stop(void){}bool ble_pt_scanning(void){return false;}uint8_t ble_pt_device_count(void){return 0;}const ble_pt_device_t *ble_pt_device(uint8_t n){return NULL;}
int ble_pt_connect(const char*p){return ESP_ERR_NOT_SUPPORTED;}void ble_pt_disconnect(void){}bool ble_pt_connected(void){return false;}bool ble_pt_connecting(void){return false;}const char *ble_pt_connected_name(void){return "";}
uint8_t ble_pt_bond_count(void){return 0;}const ble_pt_bond_t*ble_pt_bond(uint8_t n){return NULL;}void ble_pt_forget(const char*p){}
bool ble_pt_pop_key(ble_pt_event_t*e){return false;}bool ble_pt_pop_raw(ble_pt_raw_t*r){return false;}
uint32_t ble_pt_raw_code(const ble_pt_raw_t*r){return 0;}
int ble_pt_bind(ble_pt_action_t a,const ble_pt_frame_t *frame){return ESP_ERR_NOT_SUPPORTED;}bool ble_pt_binding(ble_pt_action_t a,ble_pt_frame_t *out){return false;}
ble_pt_action_t ble_pt_action_for_raw(uint32_t code){return BLE_PT_ACTION_NONE;}
ble_pt_action_t ble_pt_action_for_usage(uint8_t usage,uint8_t mods){return BLE_PT_ACTION_NONE;}
#include "ui_nav.h"
#include "ui_kit.h"
static void transfer_render(app_ctx_t*c,uint8_t*f){ui_clear_page(f);ui_nav_status(f);ui_draw_header(f,"文件传输","Mac 模拟器");ui_text(f,342,430,30,"通过模拟器侧栏导入书籍和字体",EPD_DRAW_ALIGN_CENTER,false);ui_text(f,342,510,24,"WiFi / USB 硬件传输尚未接入",EPD_DRAW_ALIGN_CENTER,false);ui_nav_draw(f,2);}
static app_redraw_t transfer_key(app_ctx_t*c,int k){c->request_return=true;return APP_REDRAW_PAGE;}
const app_desc_t app_transfer={.title="文件传输",.render=transfer_render,.on_key=transfer_key};
void app_transfer_request_hotspot_start(void){}void app_transfer_request_usb_start(void){}void app_transfer_request_wifi_setup(void){}void app_transfer_request_wifi_upload(void){}
enum EpdDrawError update_display_image_gray(EpdiyHighlevelState*h){return 0;}

void ble_pt_learn_begin(void){}
ble_pt_action_t ble_pt_action_for_frame(const ble_pt_frame_t*f){return BLE_PT_ACTION_NONE;}
void display_main_transition_gray_bands(const EpdRect*b,unsigned count){}
enum EpdDrawError display_panel_repair(EpdiyHighlevelState*h){return 0;}
int read_pico_sd_refresh_space(void){return 0;}
void rp_boot_mark(const char*n){}
const EpdWaveform *display_main_bw_waveform(void){return &E0470_WAVEFORM;}
const EpdWaveform *display_main_gray_waveform(void){return &E0470_WAVEFORM;}
const EpdWaveform *display_main_normal_waveform(void){return &E0470_WAVEFORM;}

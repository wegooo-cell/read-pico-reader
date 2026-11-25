#!/usr/bin/env python3
"""中文：真实PICO备份/重建与两次确认的故障注入，不访问设备。
English: Fault-inject the real Pico backup/rebuild and confirmation flow without hardware.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
def strip_includes(text):
    return re.sub(r'^\s*#\s*(?:include|pragma).*$', '', text, flags=re.M)

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sha256.h"
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM=2,
      ESP_ERR_NOT_FINISHED=3,ESP_ERR_TIMEOUT=4,ESP_ERR_INVALID_SIZE=5};
enum {ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_FAT=0x81,
      READ_PICO_TRANSFER_STOPPED=0,READ_PICO_TRANSFER_UPLOADING=3};
typedef struct {uint32_t address,size;bool encrypted,readonly;} esp_partition_t;
static esp_partition_t part={0x410000,0x500000,false,false};
typedef struct {int state;} read_pico_transfer_status_t;
typedef struct {bool busy;} online_font_status_t;
typedef struct {bool present,mounted;} read_pico_sd_info_t;
typedef struct {bool is_flash;}book_store_root_t;
#define BOOK_STORE_ROOT_MAX 3
enum{BOOK_STORE_ROOT_ERROR_CARD=1u,BOOK_STORE_ROOT_ERROR_FLASH=2u};
typedef struct {SHA256_CTX context;bool active;} psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {0}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
static int psa_crypto_init(void){return 0;}
static int psa_hash_setup(psa_hash_operation_t*h,int alg){assert(alg==1);sha256_init(&h->context);h->active=true;return 0;}
static int psa_hash_update(psa_hash_operation_t*h,const void*p,size_t n){assert(h->active);sha256_update(&h->context,p,n);return 0;}
static int psa_hash_finish(psa_hash_operation_t*h,uint8_t*out,size_t cap,size_t*n){assert(h->active&&cap>=32);sha256_final(&h->context,out);*n=32;h->active=false;return 0;}
static int psa_hash_abort(psa_hash_operation_t*h){h->active=false;return 0;}
static bool usb,ota,boot,sync_busy,font_busy,covers=true,avatar=true,card=true,present=true,flash_ready;
static bool after_erase,mount_after_erase=true,erase_error,read_error,fsync_error,close_error,alloc_error;
static unsigned erases,reads,closed_books,closed_fonts,remounts,random_serial,format_mounts;
static unsigned writes,owner_transfer,close_failures;
static unsigned root_checks;
static long write_fail_after=-1;
static uint64_t available=6*1024*1024;
static uint8_t device[0x500000],original[0x500000];
static void read_pico_transfer_get_status(read_pico_transfer_status_t*s){s->state=(int)owner_transfer;}
static bool read_pico_transfer_try_stop_if_idle(void){if(owner_transfer==READ_PICO_TRANSFER_UPLOADING)return false;owner_transfer=0;return true;}
static void online_font_status(online_font_status_t*s){s->busy=font_busy;}
static bool usb_storage_active(void){return usb;}
static bool app_download_active(void){return boot;}
static bool pico_online_busy(void){return ota;}
static bool kiiko_sync_active(void){return sync_busy;}
static bool book_cover_jobs_quiesce(unsigned ms){assert(!ms);return covers;}
static bool ui_avatar_quiesce(unsigned ms){assert(!ms);return avatar;}
static void book_close(void){assert(covers&&avatar);++closed_books;}
static void ttf_font_close_embedded(void){}
static void ttf_font_unload(void){++closed_fonts;}
static void ui_text_set_system_font(bool on){assert(!on);}
static int ttf_font_open_builtin(void){return 0;}
static int ttf_font_scan(void){return 2;}
static bool app_font_retry_active(void){return true;}
static void ui_avatar_invalidate(void){}
static void book_lock_collage_invalidate(void){}
static void book_store_notify_changed(void){}
static void book_library_invalidate(void){}
static int read_pico_sd_get_info(read_pico_sd_info_t*i){i->mounted=card;i->present=present;return card?ESP_OK:ESP_FAIL;}
static int read_pico_sd_detect(bool*p){*p=present;return ESP_OK;}
static bool read_pico_sd_io_ready(void){return card;}
static int read_pico_sd_remount(void){assert(closed_fonts);++remounts;card=present;return ESP_OK;}
static int book_store_roots(book_store_root_t*r,int*n){(void)r;++root_checks;if(after_erase&&mount_after_erase){flash_ready=true;after_erase=false;++format_mounts;}*n=(card?1:0)+(flash_ready?1:0);return *n?ESP_OK:ESP_FAIL;}
static bool book_store_flash_ready(void){return flash_ready;}
static unsigned book_store_root_errors(void){return (flash_ready?0:2u)|(card?0:1u);}
static int book_progress_prepare_storage(void){return flash_ready||card?ESP_OK:ESP_FAIL;}
static bool book_progress_using_card_storage(void){return !flash_ready&&card;}
static const esp_partition_t*esp_partition_find_first(int t,int st,const char*label){assert(t==1&&st==0x81&&!strcmp(label,"storage"));return &part;}
static int esp_partition_read(const esp_partition_t*p,size_t at,void*out,size_t n){assert(p==&part&&at+n<=sizeof(device));++reads;if(read_error)return ESP_FAIL;memcpy(out,device+at,n);return ESP_OK;}
static int esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
static unsigned esp_random(void){return ++random_serial;}
static int esp_vfs_fat_info(const char*p,uint64_t*t,uint64_t*f){assert(!strcmp(p,"/sdcard"));*t=64*1024*1024;*f=available;return card?ESP_OK:ESP_FAIL;}
static void*test_malloc(size_t n){return alloc_error?NULL:malloc(n);}
static size_t test_fwrite(const void*p,size_t z,size_t n,FILE*f){++writes;if(write_fail_after==0)return 0;if(write_fail_after>0)--write_fail_after;return fwrite(p,z,n,f);}
static int test_fsync(int fd){return fsync_error?-1:fsync(fd);}
static int test_fclose(FILE*f){int result=fclose(f);if(close_error&&close_failures++==0)return -1;return result;}
'''
unit += strip_includes((ROOT/'main/app/storage_repair.h').read_text())
unit += '\n#define malloc test_malloc\n#define fwrite test_fwrite\n#define fsync test_fsync\n#define fclose test_fclose\n'
unit += strip_includes((ROOT/'main/app/storage_repair.c').read_text())
unit += '\n#undef malloc\n#undef fwrite\n#undef fsync\n#undef fclose\n'
unit += r'''
static int esp_partition_erase_range(const esp_partition_t*p,size_t at,size_t n){
 assert(p==&part&&p->address==0x410000&&at==0&&n==sizeof(device));
 assert(s_status.phase==STORAGE_REPAIR_REBUILD&&s_recheck&&s_status.erase_started);
 assert(!memcmp(device,original,sizeof(device)));FILE*f=fopen(s_status.backup_path,"rb");assert(f);
 uint8_t buf[8192];size_t off=0;
 while(off<sizeof(original)){assert(fread(buf,1,sizeof(buf),f)==sizeof(buf));assert(!memcmp(buf,original+off,sizeof(buf)));off+=sizeof(buf);}
 assert(fgetc(f)==EOF&&!ferror(f)&&fclose(f)==0);++erases;
 if(erase_error)return ESP_FAIL;
 memset(device,255,sizeof(device));after_erase=true;return ESP_OK;
}
static void reset(void){
 release_work();s_status=(storage_repair_status_t){0};s_partition=NULL;s_backup=s_recheck=s_fonts_closed=false;
 part=(esp_partition_t){0x410000,0x500000,false,false};
 usb=ota=boot=sync_busy=font_busy=flash_ready=after_erase=erase_error=read_error=fsync_error=close_error=alloc_error=false;
 covers=avatar=card=present=mount_after_erase=true;owner_transfer=0;available=6*1024*1024;
 erases=reads=closed_books=closed_fonts=remounts=writes=format_mounts=close_failures=root_checks=0;write_fail_after=-1;
 for(size_t i=0;i<sizeof(device);++i)original[i]=(uint8_t)(i*13+i/4096);
 memcpy(device,original,sizeof(device));
}
static void run(void){for(int i=0;i<5000&&app_storage_repair_busy();++i)(void)app_storage_repair_tick(i+20);assert(!app_storage_repair_busy());}
static void backup(void){assert(app_storage_repair_begin(true,0)==ESP_OK);run();assert(s_status.phase==STORAGE_REPAIR_BACKUP_READY&&s_status.backup_verified&&!erases&&!root_checks);}
static void source_retained(void){assert(!erases&&!memcmp(device,original,sizeof(device)));}
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{bool touched;}test_touch_t;
typedef struct{int64_t now_ms;test_touch_t*touch;}app_ctx_t;
typedef struct{int type,x0,y0;}ui_gesture_event_t;
typedef int app_redraw_t;
enum{UI_GESTURE_TAP,UI_GESTURE_MOVE,APP_REDRAW_NONE,APP_REDRAW_PAGE,EPD_DRAW_ALIGN_LEFT};
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static void back_header(uint8_t*f,const char*t){(void)f;(void)t;}
static void settings_navigation(uint8_t*f){(void)f;}
static void ui_clear_page(uint8_t*f){*f=0;}
static bool test_english;
static bool ui_locale_english(void){return test_english;}
static int settings_nav_hit(int x,int y){(void)x;return y>=1096?0:-1;}
static void ui_nav_request(app_ctx_t*ctx,int tab){(void)ctx;(void)tab;}
static void ui_text_fixed(uint8_t*f,int x,int y,int px,const char*t,int a,bool inv){(void)f;(void)x;(void)px;(void)t;(void)a;(void)inv;assert(y<1096);}
static void ui_text_box(uint8_t*f,EpdRect r,int px,const char*t,int a,bool inv){(void)f;(void)px;(void)t;(void)a;(void)inv;assert(r.x>=0&&r.y>=0&&r.x+r.width<=684&&r.y+r.height<=1096);}
static void ui_draw_button(uint8_t*f,EpdRect r,const char*t,bool inv){(void)f;(void)t;(void)inv;assert(r.x>=0&&r.y>=0&&r.x+r.width<=684&&r.y+r.height<=1096);}
'''
unit += r'''
enum{SETTINGS_MAIN,SETTINGS_STORAGE_REPAIR,SETTINGS_FACTORY_RESET,SETTINGS_CONFIG,SETTINGS_BOOT,SETTINGS_UPGRADE,SETTINGS_SYSTEM_REPAIR};
static int s_page;static bool s_reset_confirm,s_reset_pending,s_config_confirm,s_panel_repair_pending,s_dc_recover_pending,s_boot_pending;
static char s_reset_notice[96],s_notice[96];
static void upgrade_open(void){s_page=SETTINGS_UPGRADE;}
'''.replace('\n+','\n')
unit += strip_includes((ROOT/'main/apps/storage_repair_ui.inc').read_text())
from home_cover_cache_host_test import function
settings_ui=ROOT/'boards/pico/src/main/apps/app_device_settings.c'
main_gesture=function('on_gesture',settings_ui)
maintenance=main_gesture[main_gesture.index('    if(y>=SETTINGS_MAINTENANCE_Y'):main_gesture.rindex('    return APP_REDRAW_NONE;')]
unit += '''
#define SETTINGS_MAINTENANCE_Y 2000
#define SETTINGS_MAINTENANCE_ROWS 5
#define SETTINGS_ROW_H 82
static app_redraw_t actual_maintenance(app_ctx_t*ctx,int y){int row;(void)ctx;
'''.replace('\n+','\n') + maintenance + 'return APP_REDRAW_NONE;}\n'

unit += r'''
int main(void){
 reset();assert(app_storage_repair_confirm(0)!=ESP_OK);source_retained();
 assert(app_storage_repair_begin(false,0)==ESP_OK);run();assert(s_status.phase==STORAGE_REPAIR_DONE&&s_status.flash_error!=ESP_OK&&s_status.card_progress);source_retained();
 reset();card=false;assert(app_storage_repair_begin(false,0)==ESP_OK);run();
 assert(remounts==1&&closed_fonts&&s_status.phase==STORAGE_REPAIR_DONE&&s_status.card_error==ESP_OK&&s_status.card_progress);source_retained();
 reset();owner_transfer=1;assert(app_storage_repair_begin(false,0)==ESP_OK&&!owner_transfer);run();source_retained();
 reset();flash_ready=true;assert(app_storage_repair_begin(true,0)==ESP_OK);run();assert(s_status.phase==STORAGE_REPAIR_DONE);source_retained();
 for(int field=0;field<5;++field){reset();if(field==0)usb=true;if(field==1)ota=true;if(field==2)sync_busy=true;if(field==3)font_busy=true;if(field==4)owner_transfer=3;
  assert(app_storage_repair_begin(true,0)!=ESP_OK);source_retained();}
 for(int bad=0;bad<5;++bad){reset();if(bad==0)part.address=0x9000;if(bad==1)part.size=0x400000;if(bad==2)part.encrypted=true;if(bad==3)part.readonly=true;if(bad==4)available=sizeof(device);
  assert(app_storage_repair_begin(true,0)==ESP_OK);run();assert(s_status.phase==STORAGE_REPAIR_FAILED);source_retained();}
 reset();covers=false;assert(app_storage_repair_begin(true,0)==ESP_OK);assert(!app_storage_repair_tick(1));assert(app_storage_repair_tick(5000)&&s_status.phase==STORAGE_REPAIR_FAILED&&!closed_fonts);source_retained();
 for(int fault=0;fault<5;++fault){reset();if(fault==0)alloc_error=true;if(fault==1)read_error=true;if(fault==2)write_fail_after=2;if(fault==3)fsync_error=true;if(fault==4)close_error=true;
  assert(app_storage_repair_begin(true,0)==ESP_OK);run();assert(s_status.phase==STORAGE_REPAIR_FAILED&&!s_status.backup_verified);source_retained();}
 reset();backup();assert(strlen(s_status.sha256)==64);char first[192];strcpy(first,s_status.backup_path);
 app_storage_repair_cancel();assert(app_storage_repair_confirm(0)!=ESP_OK);source_retained();
 backup();assert(strcmp(first,s_status.backup_path)&&access(first,F_OK)==0);
 assert(app_storage_repair_confirm(400000)!=ESP_OK);source_retained();
 for(int fault=0;fault<6;++fault){reset();backup();
  if(fault==0)device[100]^=1;
  if(fault==1){FILE*f=fopen(s_status.backup_path,"r+b");assert(f&&fseek(f,100,SEEK_SET)==0);int c=fgetc(f);assert(c!=EOF&&fseek(f,100,SEEK_SET)==0&&fputc(c^1,f)!=EOF&&fclose(f)==0);}
  if(fault==2){FILE*f=fopen(s_status.backup_path,"ab");assert(f&&fputc(0,f)!=EOF&&fclose(f)==0);}
  if(fault==3)assert(truncate(s_status.backup_path,sizeof(device)-1)==0);
  if(fault==4)card=false;
  if(fault==5)usb=true;
  esp_err_t e=app_storage_repair_confirm(10000);if(e==ESP_OK)run();assert(!erases);if(fault!=0)source_retained();
 }
 reset();backup();memset(&s_status,0,sizeof(s_status));s_recheck=false;
 assert(app_storage_repair_confirm(10000)!=ESP_OK);source_retained();
 reset();backup();assert(app_storage_repair_confirm(10000)==ESP_OK);
 while(s_status.phase!=STORAGE_REPAIR_REBUILD){assert(app_storage_repair_busy());(void)app_storage_repair_tick(11000);}
 present=false;(void)app_storage_repair_tick(11001);assert(s_status.phase==STORAGE_REPAIR_FAILED);source_retained();
 for(int fault=0;fault<2;++fault){reset();backup();char saved[192];strcpy(saved,s_status.backup_path);
  if(fault==0)erase_error=true;else mount_after_erase=false;
  assert(app_storage_repair_confirm(10000)==ESP_OK);run();assert(erases==1&&s_status.phase==STORAGE_REPAIR_FAILED&&access(saved,F_OK)==0);}
 reset();backup();assert(app_storage_repair_confirm(10000)==ESP_OK);run();assert(erases==1&&format_mounts==1&&flash_ready&&s_status.phase==STORAGE_REPAIR_DONE&&s_status.progress_error==ESP_OK);
 puts("PASS: real SHA256 raw backup/source readback, independent second confirmation, exact storage-only erase, successful remount and backup retention");
 puts("PASS: owner conflicts, wrong/encrypted/read-only partitions, insufficient space, OOM, read/write/fsync/close errors, corruption/truncation/trailing bytes, changed source, timeout, removal, cancel/reset and failed rebuild never bypass guards");
 reset();s_storage_rebuild_question=s_storage_wait_release=false;test_touch_t held={true};app_ctx_t ctx={.touch=&held};uint8_t fb=0;
 ui_gesture_event_t e={UI_GESTURE_MOVE,60,950};assert(storage_repair_gesture(&ctx,&e)==APP_REDRAW_NONE);source_retained();
 e.type=UI_GESTURE_TAP;assert(storage_repair_gesture(&ctx,&e)==APP_REDRAW_PAGE&&s_storage_rebuild_question&&!app_storage_repair_busy());
 unsigned before=reads;fb=255;storage_repair_draw(&fb);assert(reads==before&&!erases&&fb==0);
 assert(storage_repair_gesture(&ctx,&e)==APP_REDRAW_PAGE&&app_storage_repair_busy());
 for(int i=0;i<5000&&app_storage_repair_busy();++i)(void)storage_repair_tick(&ctx);
 assert(s_status.phase==STORAGE_REPAIR_BACKUP_READY&&!erases&&s_storage_wait_release);
 assert(storage_repair_gesture(&ctx,&e)==APP_REDRAW_NONE&&!erases);
 held.touched=false;(void)storage_repair_tick(&ctx);assert(!s_storage_wait_release);
 test_english=true;storage_repair_draw(&fb);test_english=false;ctx.now_ms=10000;assert(storage_repair_gesture(&ctx,&e)==APP_REDRAW_PAGE);run();assert(erases==1);
 reset();s_storage_rebuild_question=true;e.y0=851;assert(storage_repair_gesture(&ctx,&e)==APP_REDRAW_PAGE&&!s_storage_rebuild_question);source_retained();
 reset();s_page=SETTINGS_MAIN;s_storage_rebuild_question=true;
 assert(actual_maintenance(&ctx,2000+2*82)==APP_REDRAW_PAGE&&s_page==SETTINGS_SYSTEM_REPAIR&&!erases);
 fb=255;before=reads;system_repair_draw(&fb);assert(!fb&&reads==before&&!erases);
 test_english=true;system_repair_draw(&fb);test_english=false;
 e.x0=60;e.y0=272+2*112;assert(system_repair_gesture(&ctx,&e)==APP_REDRAW_PAGE&&s_page==SETTINGS_STORAGE_REPAIR&&!s_storage_rebuild_question&&!erases);
 backup();s_page=SETTINGS_STORAGE_REPAIR;e.x0=60;e.y0=100;
 assert(storage_repair_gesture(&ctx,&e)==APP_REDRAW_PAGE&&s_page==SETTINGS_SYSTEM_REPAIR&&app_storage_repair_confirm(10000)!=ESP_OK);source_retained();
 release_work();puts("PASS: actual settings entry/header-back, initial warning, verified backup and separate final confirmation; pure drawing and cancellation revoke authorization");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-storage-repair-') as directory:
    p=Path(directory)
    (p/'test.c').write_text(unit)
    vendor=ROOT/'tools/simulator/web/vendor'
    # 副本显式用无符号拼接输入字，避免旧第三方例程的整型提升移位UB；不改vendor原件。
    # Use unsigned input-word assembly in the test copy, retaining the original vendor source.
    sha=(vendor/'sha256.c').read_text()
    sha=re.sub(r'\bdata\[j(?: \+ \d)?\]',lambda m:'((WORD)'+m[0]+')',sha)
    (p/'sha256.c').write_text(sha)
    command=['cc','-std=c11','-D_DEFAULT_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',
             '-fsanitize=address,undefined',f'-DSTORAGE_REPAIR_ROOT="{p}/backups"',
             '-I'+str(vendor),str(p/'test.c'),str(p/'sha256.c'),'-o',str(p/'test')]
    subprocess.run(command,check=True)
    subprocess.run([str(p/'test')],check=True)

loop=(ROOT/'boards/pico/src/main/app/app_loop.c').read_text()
assert loop.index('if (app_storage_repair_busy()) {')<loop.index('if (ctx.now_ms - last_media_poll_ms')
block=loop[loop.index('if (app_storage_repair_busy()) {'):loop.index('if (ctx.now_ms - last_media_poll_ms')]
assert all(token in block for token in ('read_pico_pmu_drain_events','last_input_ms','last_font_retry_ms','last_media_poll_ms','continue;'))
ui=(ROOT/'boards/pico/src/main/apps/app_device_settings.c').read_text()
assert '"系统修复"' in ui and '#define SETTINGS_MAINTENANCE_ROWS 5' in ui
assert '"存储检查与修复"' in (ROOT/'main/apps/storage_repair_ui.inc').read_text()
assert ui.count('app_storage_repair_cancel(); s_storage_rebuild_question=false;')>=2
assert 'return !app_storage_repair_busy() && !s_reset_pending' in ui
print('PASS: Pico loop/exit/lock/navigation maintenance interlocks and settings entry are wired; S31 controller unchanged')

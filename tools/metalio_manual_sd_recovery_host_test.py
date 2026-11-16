#!/usr/bin/env python3
"""S31 manual recovery lifecycle and actual settings entry/hit geometry. No hardware claims.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile
from board_source_view import select_source
from home_cover_cache_host_test import function

ROOT=Path(__file__).resolve().parents[1]
def no_includes(text):
    return re.sub(r'^\s*#\s*(?:include|pragma).*$', '', text, flags=re.M)

unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=1,ESP_ERR_NOT_FINISHED=2,ESP_ERR_NO_MEM=3,ESP_ERR_TIMEOUT=4};
enum {READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_STARTING,READ_PICO_TRANSFER_READY,READ_PICO_TRANSFER_UPLOADING,READ_PICO_TRANSFER_ERROR};
typedef struct {int state;}read_pico_transfer_status_t;
typedef struct {bool busy;}online_font_status_t;
typedef struct {bool mounted;}read_pico_sd_info_t;
static bool usb,boot,ota,online,covers=true,avatar=true,mounted,selected_ready=true;
static int transfer,card_result=ESP_ERR_NOT_FINISHED,start_result=ESP_OK,font_count=128;
static unsigned book_closed,embedded_closed,font_closed,builtin,wake_dropped,starts,scans,font_retry,invalidations;
static void read_pico_transfer_get_status(read_pico_transfer_status_t *s){s->state=transfer;}
static void online_font_status(online_font_status_t *s){s->busy=online;}
static bool usb_storage_active(void){return usb;}
static bool app_download_active(void){return boot;}
static bool pico_online_busy(void){return ota;}
static bool book_cover_jobs_quiesce(unsigned ms){assert(ms==0);return covers;}
static bool ui_avatar_quiesce(unsigned ms){assert(ms==0);return avatar;}
static void book_close(void){assert(covers&&avatar);++book_closed;}
static void ttf_font_close_embedded(void){assert(book_closed);++embedded_closed;}
static void ttf_font_unload(void){assert(embedded_closed);++font_closed;}
static void app_sleep_discard_media_io(void){assert(font_closed);++wake_dropped;}
static void ui_text_set_system_font(bool on){assert(!on);}
static int ttf_font_open_builtin(void){++builtin;return ESP_OK;}
static void ui_avatar_invalidate(void){++invalidations;}
static void book_lock_collage_invalidate(void){++invalidations;}
static void metalio_cover_cache_clear(void){++invalidations;}
static void book_store_notify_changed(void){++invalidations;}
static int read_pico_sd_start_manual_recovery(void){assert(wake_dropped&&font_closed&&book_closed);++starts;return start_result;}
static int read_pico_sd_get_info(read_pico_sd_info_t *i){i->mounted=mounted;return card_result;}
static int ttf_font_scan(void){assert(mounted&&card_result==ESP_OK);++scans;return font_count;}
static bool app_font_recover_media(void){assert(scans);++font_retry;return true;}
static const char *app_settings_system_font_path(void){return "/fonts/user.ttf";}
static bool ttf_font_ready(void){return selected_ready;}
static const char *ttf_font_path(void){return selected_ready?"/fonts/user.ttf":"builtin";}
'''
unit+=no_includes((ROOT/'main/app/sd_recovery.h').read_text())
unit+=no_includes((ROOT/'main/app/sd_recovery.c').read_text())
unit+=r'''
static void reset(void){s_status=(app_sd_recovery_status_t){0};s_deadline_ms=0;
 usb=boot=ota=online=mounted=false;selected_ready=covers=avatar=true;transfer=0;
 card_result=ESP_ERR_NOT_FINISHED;start_result=ESP_OK;font_count=128;
 book_closed=embedded_closed=font_closed=builtin=wake_dropped=starts=scans=font_retry=invalidations=0;}
static void prepared(void){assert(app_sd_recovery_begin(10)==ESP_OK&&app_sd_recovery_busy());
 assert(app_sd_recovery_tick(20)&&s_status.phase==APP_SD_RECOVERY_WAIT);
 assert(book_closed==1&&embedded_closed==1&&font_closed==1&&builtin==1&&wake_dropped==1&&invalidations==4);}
static void completed(void){assert(app_sd_recovery_tick(30)&&s_status.phase==APP_SD_RECOVERY_MOUNT);
 assert(!app_sd_recovery_tick(60000)&&app_sd_recovery_busy()&&!scans);
 card_result=ESP_OK;mounted=true;assert(app_sd_recovery_tick(60001)&&s_status.phase==APP_SD_RECOVERY_FONTS&&!scans);
 assert(app_sd_recovery_tick(60002)&&!app_sd_recovery_busy()&&s_status.phase==APP_SD_RECOVERY_DONE);
 assert(scans==1&&font_retry==1&&s_status.fonts==font_count);}
'''
# Exercise the settings button actually used by the page, including busy navigation protection.
ui=ROOT/'main/apps/app_device_settings.c'
unit+=r'''
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int64_t now_ms;int requested;}app_ctx_t;
typedef struct {int type,x0,y0;}ui_gesture_event_t;
typedef int app_redraw_t;
enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,UI_GESTURE_TAP,UI_GESTURE_MOVE};
enum {SETTINGS_MAIN,SETTINGS_SD_RECOVERY,SETTINGS_FACTORY_RESET,SETTINGS_CONFIG,SETTINGS_BOOT,SETTINGS_UPGRADE};
static int s_page;static char s_sd_notice[96],s_notice[96],s_boot_notice[96],s_reset_notice[96];
static bool s_reset_confirm,s_reset_pending,s_config_confirm,s_panel_repair_pending,s_boot_pending;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static void settings_prepare_profile(void){}
static int settings_nav_hit(int x,int y){(void)x;return y>=1096?0:-1;}
static void ui_nav_request(app_ctx_t *ctx,int tab){ctx->requested=tab+1;}
static void upgrade_open(void){s_page=SETTINGS_UPGRADE;}
'''
for name in ('sd_recovery_button','sd_recovery_gesture'):
    unit+=select_source(function(name,ui),'metalio47')+'\n'
body=select_source(function('on_gesture',ui),'metalio47')
body=body[body.index('    if(y>=SETTINGS_MAINTENANCE_Y'):body.rindex('    return APP_REDRAW_NONE;')]
unit+='''
#define SETTINGS_MAINTENANCE_Y 2000
#define SETTINGS_MAINTENANCE_ROWS 6
#define SETTINGS_ROW_H 82
static app_redraw_t maintenance(app_ctx_t *ctx,int y) {int row;(void)ctx;
'''+body+'return APP_REDRAW_NONE;}\n'
unit+=r'''
int main(void){
 for(int owner=0;owner<8;owner++){
  reset();usb=owner==0;boot=owner==1;ota=owner==2;online=owner==3;if(owner>=4)transfer=owner-3;
  assert(app_sd_recovery_begin(0)==ESP_ERR_INVALID_STATE&&!app_sd_recovery_busy()&&!book_closed&&!starts);
 }
 reset();covers=false;assert(app_sd_recovery_begin(0)==ESP_OK);
 assert(!app_sd_recovery_tick(4999)&&!book_closed&&!starts);
 assert(app_sd_recovery_tick(5000)&&!app_sd_recovery_busy()&&s_status.error==ESP_ERR_TIMEOUT&&!font_closed);
 reset();avatar=false;assert(app_sd_recovery_begin(0)==ESP_OK);assert(!app_sd_recovery_tick(1));
 avatar=true;assert(app_sd_recovery_tick(2)&&s_status.phase==APP_SD_RECOVERY_WAIT);
 reset();assert(app_sd_recovery_begin(0)==ESP_OK);usb=true;
 assert(app_sd_recovery_tick(1)&&s_status.phase==APP_SD_RECOVERY_FAILED&&!font_closed);
 reset();prepared();start_result=ESP_ERR_NOT_FINISHED;assert(!app_sd_recovery_tick(21));
 assert(app_sd_recovery_tick(10020)&&s_status.error==ESP_ERR_TIMEOUT&&!app_sd_recovery_busy());
 reset();prepared();start_result=ESP_ERR_NO_MEM;
 assert(app_sd_recovery_tick(30)&&s_status.error==ESP_ERR_NO_MEM&&!scans);
 reset();prepared();completed();assert(s_status.selected_font_ready&&!app_sd_recovery_tick(60003));
 reset();prepared();font_count=0;selected_ready=false;completed();assert(!s_status.selected_font_ready);
 reset();prepared();assert(app_sd_recovery_tick(30));card_result=ESP_FAIL;
 assert(app_sd_recovery_tick(40)&&s_status.phase==APP_SD_RECOVERY_FAILED&&!scans);
 reset();prepared();assert(app_sd_recovery_tick(30));card_result=ESP_OK;
 assert(app_sd_recovery_tick(40)&&s_status.phase==APP_SD_RECOVERY_FAILED&&s_status.error==ESP_FAIL&&!scans);
 reset();app_ctx_t ctx={.now_ms=50};s_page=SETTINGS_SD_RECOVERY;
 EpdRect button=sd_recovery_button();ui_gesture_event_t e={UI_GESTURE_TAP,button.x+1,button.y+1};
 e.type=UI_GESTURE_MOVE;assert(sd_recovery_gesture(&ctx,&e)==APP_REDRAW_NONE&&!app_sd_recovery_busy());
 e.type=UI_GESTURE_TAP;e.y0=button.y-1;assert(sd_recovery_gesture(&ctx,&e)==APP_REDRAW_NONE&&!app_sd_recovery_busy());
 e.y0=button.y+button.height;assert(sd_recovery_gesture(&ctx,&e)==APP_REDRAW_NONE&&!app_sd_recovery_busy());
 e.y0=button.y;usb=true;assert(sd_recovery_gesture(&ctx,&e)==APP_REDRAW_PAGE&&s_sd_notice[0]&&!app_sd_recovery_busy());
 usb=false;assert(sd_recovery_gesture(&ctx,&e)==APP_REDRAW_PAGE&&app_sd_recovery_busy()&&!s_sd_notice[0]);
 e.y0=1100;assert(sd_recovery_gesture(&ctx,&e)==APP_REDRAW_NONE&&!ctx.requested);
 assert(sd_recovery_gesture(&ctx,&e)==APP_REDRAW_NONE&&!ctx.requested);
 reset();int pages[]={SETTINGS_UPGRADE,SETTINGS_SD_RECOVERY,SETTINGS_CONFIG,SETTINGS_MAIN,SETTINGS_BOOT,SETTINGS_FACTORY_RESET};
 for(int row=0;row<6;row++)for(int edge=0;edge<2;edge++){
  s_page=SETTINGS_MAIN;assert(maintenance(&ctx,2000+82*row+(edge?81:0))==APP_REDRAW_PAGE&&s_page==pages[row]);
 }
 s_page=SETTINGS_MAIN;assert(maintenance(&ctx,2492)==APP_REDRAW_NONE&&s_page==SETTINGS_MAIN);
 puts("PASS: manual SD ownership, worker join/timeout, pending mounts, no unsafe cancellation, font rescan, selected-font warning; actual settings 6-row mapping/button bounds/busy navigation");
}
'''
loop=select_source((ROOT/'main/app/app_loop.c').read_text(),'metalio47')
assert loop.index('if (app_sd_recovery_busy()) {')<loop.index('if (ctx.now_ms - last_media_poll_ms')
busy=loop[loop.index('if (app_sd_recovery_busy()) {'):loop.index('if (ctx.now_ms - last_media_poll_ms')]
assert 'current->on_tick' in busy and 'continue;' in busy and 'read_pico_pmu_drain_events' in busy
assert not re.search(r'read_pico_sd_format|app_settings_set_', (ROOT/'main/app/sd_recovery.c').read_text())
pico_overlay=ROOT/'boards/pico/src/main/apps/app_device_settings.c'
pico=select_source((pico_overlay if pico_overlay.exists() else ui).read_text(),'pico')
assert '内存卡恢复' not in pico and 'sd_recovery' not in pico
with tempfile.TemporaryDirectory() as folder:
    c,exe=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable',
                    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

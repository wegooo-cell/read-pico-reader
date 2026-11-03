#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：执行真实深睡锁图恢复，慢挂载不覆盖面板，坏缓存不冒充成功。
English: Execute retained-art restoration; slow media preserves physical pixels and corrupt caches cannot claim success.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
S=ROOT/'boards/pico/src/main/sleep.c';B=ROOT/'boards/pico/src/main/app_main.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BYTES (684u*1216u/2u)
#define ESP_ERR_NOT_FINISHED 1
#define EPD_DRAW_SUCCESS 0
#define ESP_LOGI(...) ((void)0)
typedef struct{uint8_t *back_fb;}EpdiyHighlevelState;
typedef struct{bool mounted;}read_pico_sd_info_t;
typedef struct{unsigned marker;}app_lock_font_t;
enum EpdDrawError{EPD_OK=0,EPD_BAD=1};
static uint8_t front[BYTES],back[BYTES],panel[BYTES],style;
static bool pin,cached,valid,mounted,draw_ok,scan_ok;
static unsigned remaining,probes,reads,draws,scans,saves,marks,fonts,clears,power,censuses,white_push,fallback;
static EpdiyHighlevelState *s_lock_hint_hl,*s_boot_lock_hl;
static bool s_boot_lock_active,s_boot_lock_known,s_boot_lock_draw;
static int epd_width(void){return 684;}static int epd_height(void){return 1216;}
static uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState *hl){assert(hl->back_fb==back);return front;}
static int read_pico_sd_get_info(read_pico_sd_info_t *i){++probes;if(remaining){--remaining;i->mounted=false;return ESP_ERR_NOT_FINISHED;}i->mounted=mounted;return 0;}
static bool pico_boot_resume_frame_cached(void){return cached;}
static bool lock_screen_restore(uint8_t *fb){assert(mounted&&remaining==0);++reads;memset(fb,valid?(0x10+style):0x23,BYTES);return valid;}
static uint8_t app_settings_lock_style(void){return style;}
static void app_font_begin_lock(app_lock_font_t *f){f->marker=1;++fonts;}
static void app_font_end_lock(app_lock_font_t *f){assert(f->marker);}
static void epd_hl_set_all_white(EpdiyHighlevelState *h){assert(h->back_fb==back);memset(front,255,BYTES);memset(back,255,BYTES);}
static bool draw(uint8_t *fb,unsigned expected){assert(style==expected);++draws;memset(fb,0x10+style,BYTES);return draw_ok;}
static bool book_ticket_draw(uint8_t *fb,bool bg){assert(!bg);return draw(fb,0);}
static bool book_lock_collage_draw(uint8_t *fb){return draw(fb,2);}
static bool draw_wallpaper(uint8_t *fb){return draw(fb,1);}
static enum EpdDrawError update_display_full(EpdiyHighlevelState *h){assert(h->back_fb==back);++scans;if(!scan_ok)return EPD_BAD;memcpy(panel,front,BYTES);memcpy(back,front,BYTES);return EPD_OK;}
static void guard_draw_result(EpdiyHighlevelState *h,enum EpdDrawError err){(void)h;(void)err;}
static void epd_poweroff(void){}
static bool lock_screen_save(const uint8_t *fb){assert(fb==front&&scan_ok&&(!draws||draw_ok));++saves;return true;}
static int pico_boot_mark_frame_cached(void){++marks;return 0;}
static void epd_poweron(void){++power;}static void epd_clear(void){++clears;memset(panel,255,BYTES);}
static void read_pico_i2c_census_take(void){++censuses;}
static void draw_startup_frame(uint8_t *fb){memset(fb,0x56,BYTES);}
static void ui_lock_fallback_draw(uint8_t *fb){++fallback;memset(fb,255,BYTES);}
static void update_display_from_white(EpdiyHighlevelState *h){(void)h;++white_push;memcpy(panel,front,BYTES);}
'''
for name,path in [('lock_resume_service',S),('app_lock_resume_frame',S),('prepare_startup_frame',B)]:unit+=function(name,path)+'\n'
unit+=r'''
static void reset(void){
 memset(front,0x31,BYTES);memset(back,0x42,BYTES);memset(panel,0x10+style,BYTES);
 s_boot_lock_hl=s_lock_hint_hl=NULL;s_boot_lock_active=s_boot_lock_known=s_boot_lock_draw=false;
 remaining=probes=reads=draws=scans=saves=marks=fonts=clears=power=censuses=white_push=fallback=0;
 mounted=valid=cached=draw_ok=scan_ok=true;
}
int main(void){EpdiyHighlevelState hl={back};
 for(style=0;style<4;++style)for(unsigned password=0;password<2;++password){
  pin=password;reset();remaining=450;
  assert(!prepare_startup_frame(&hl,front,true,true));assert(probes==1&&!reads&&!scans&&!fallback&&!clears);
  s_boot_lock_draw=true;
  for(unsigned n=0;n<449;++n){lock_resume_service();assert(!reads&&!scans&&panel[0]==0x10+style&&!s_lock_hint_hl);}
  lock_resume_service();assert(reads==1&&s_boot_lock_known&&s_lock_hint_hl==&hl);
  assert(!memcmp(front,back,BYTES)&&!memcmp(front,panel,BYTES)&&!scans&&!draws&&!saves);
  unsigned done=probes;for(unsigned n=0;n<100;++n)lock_resume_service();assert(probes==done);
  reset();cached=false;assert(prepare_startup_frame(&hl,front,true,true));assert(scans==1&&saves==1&&marks==1);
  reset();remaining=500;assert(!prepare_startup_frame(&hl,front,true,true));
  (void)app_lock_resume_frame(NULL);unsigned before=probes;lock_resume_service();assert(before==probes&&!s_boot_lock_active&&!s_boot_lock_hl);
 }
 for(style=0;style<4;++style){reset();valid=false;
  assert(!prepare_startup_frame(&hl,front,true,true));assert(!draws&&!scans&&panel[0]==0x10+style);
  s_boot_lock_draw=true;lock_resume_service();
  assert(draws==(style!=3)&&scans==(style!=3)&&saves==(style!=3)&&marks==saves);
  assert(s_boot_lock_known==(style!=3)&&panel[0]==0x10+style&&!fallback);
 }
 for(unsigned failure=0;failure<3;++failure){style=2;reset();valid=false;s_boot_lock_draw=true;
  if(failure==0)mounted=false;else if(failure==1)draw_ok=false;else scan_ok=false;
  (void)app_lock_resume_frame(&hl);s_boot_lock_draw=true;lock_resume_service();
  assert(!s_boot_lock_known&&!s_lock_hint_hl&&!saves&&!marks&&!fallback);
 }
 reset();assert(!prepare_startup_frame(&hl,front,false,true));assert(power==1&&clears==1&&censuses==1&&white_push==1&&!reads&&!fallback);
 puts("PASS: four styles and PIN choices survive slow mounts without panel replacement, trusted cache hits need zero scans; stale/corrupt cache regeneration, transparent physical retention, failures, one-press cancellation and cold splash retain boundaries");
}
'''
with tempfile.TemporaryDirectory(prefix='kiikoread-deep-lock-') as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：实际锁屏入口按当前样式绘图；自动/手动、深睡与密码缓存回退不改选择。
# English: Actual sleep entries honor the selected artwork for auto/manual locks, deep sleep and PIN cache fallback.
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
ROOT = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>
typedef struct{bool time_synced;time_t unix_sec;}pmu_snapshot_t;
static const pmu_snapshot_t *read_pico_pmu_get(void){return NULL;}
static void app_settings_record_pin_lock(uint32_t now){(void)now;}
static void app_lock_wake_single_once(void){}
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define FRAME_BYTES (684u*1216u/2u)
#define MODE_GC16 1
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_FAULT=1,EPD_DRAW_FAILED_ALLOC=0x10};
#define APP_WAKE_KEY 1
#define APP_WAKE_TIMEOUT 3
#define APP_SLEEP_DEEP 1
#define APP_LOCK_LIGHT_SLEEP_MS 600000u
#define APP_LOCK_IGNORE_BOOT_MS 1000
#define ESP_ERR_NOT_FINISHED 1
#define ESP_LOGI(tag,...) test_log(__VA_ARGS__)
static void test_log(const char *format,...){(void)format;}
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint8_t *back_fb,*front_fb;} EpdiyHighlevelState;
typedef struct {bool mounted;} read_pico_sd_info_t;
typedef struct {bool active;} app_lock_font_t;
typedef void *sc7a20h_handle_t;typedef void *cst836u_handle_t;
typedef int app_wake_source_t;
static EpdiyHighlevelState *s_lock_hint_hl,*s_boot_lock_hl;
static struct{unsigned serial;}s_lock_hint;
static bool s_boot_lock_active,s_boot_lock_known,s_boot_lock_draw;
static unsigned read_pico_pmu_lock_key_serial(void){return 0;}
static void lock_resume_service(void){}
static uint8_t pixels[FRAME_BYTES],panel[FRAME_BYTES],history[FRAME_BYTES],style,current_background=0x32;
static bool frame_ready, s_lock_feedback_failed, s_lock_feedback_visible, feedback_seen, final_ok=true;
static unsigned full_presents, entry_clears, exit_clears, exit_white;
static bool unlocking,recovery_ok=true,hint_visible;
static bool white_ok=true,white_alloc_fail;
static unsigned white_scans,white_allocations;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_aligned_alloc(size_t alignment,size_t size,int caps){assert(alignment==16&&size==FRAME_BYTES&&caps==3);if(white_alloc_fail)return NULL;++white_allocations;return malloc(size);}
void heap_caps_free(void *p){assert(p&&white_allocations);--white_allocations;free(p);}
static uint8_t wake_frame[FRAME_BYTES];
static unsigned draws[4],fallback,presents,auths,font_begin,font_end,pause,resume,clear_resume;
static bool pin,draw_ok=true,transfer_ok=true,timeout,reader,boot_cache,cache_ok;
static unsigned cached_style;
static jmp_buf deep;
static const char *app_settings_wallpaper_path(void){return "/sdcard/wallpaper.jpg";}
static uint8_t app_settings_lock_style(void){return style;}
static bool lock_pin_enabled(void){return pin;}
static bool draw(unsigned chosen,uint8_t *fb){assert(chosen==style&&panel[0]==(current_background));if(chosen==3)assert(fb[0]==(boot_cache?0xff:current_background));++draws[chosen];memset(fb,0x10+chosen,FRAME_BYTES);frame_ready=draw_ok;return draw_ok;}
static bool ui_wallpaper_prepare_lock(uint8_t *fb,const char *path){assert(!strcmp(path,app_settings_wallpaper_path()));return draw(1,fb);}
static void ui_wallpaper_cache_commit(const uint8_t *fb){assert(style==1&&draw_ok&&presents==1&&fb==pixels&&!memcmp(panel,pixels,FRAME_BYTES));}
static bool book_ticket_draw(uint8_t *fb,bool background){assert(background==reader);return draw(0,fb);}
static const char *app_settings_bookmark_path(void){return "/sdcard/overlay.png";}
static bool book_lock_bookmark_draw(uint8_t *fb,const char *p){assert(!strcmp(p,app_settings_bookmark_path()));return draw(3,fb);}
static bool lock_screen_save(const uint8_t *fb){assert(fb==pixels&&presents&&!memcmp(pixels,panel,FRAME_BYTES)&&!memcmp(history,panel,FRAME_BYTES));return true;}
static int pico_boot_mark_frame_cached(void){return 0;}
static bool book_lock_collage_draw(uint8_t *fb){return draw(2,fb);}
static bool book_lock_collage_prepare(uint8_t *fb){return draw(2,fb);}
static enum EpdDrawError update_display_full(EpdiyHighlevelState *h){
 assert(!s_lock_feedback_visible&&frame_ready&&h->back_fb==history&&!memcmp(history,panel,FRAME_BYTES));
 if(h->front_fb!=pixels){
  assert(draw_ok&&h->front_fb&&white_allocations==1);++white_scans;
  for(size_t i=0;i<FRAME_BYTES;++i)assert(h->front_fb[i]==255&&pixels[i]==0x10+style);
  if(!white_ok){panel[0]=0x77;return EPD_DRAW_FAULT;}
  memcpy(panel,h->front_fb,FRAME_BYTES);memcpy(history,panel,FRAME_BYTES);return EPD_DRAW_SUCCESS;
 }
 ++full_presents;assert(!white_allocations);
 assert(panel[0]==255);
 assert(panel[1]==255&&draw_ok&&pixels[0]==0x10+style);
 if(!final_ok){panel[0]=0x77;return EPD_DRAW_FAULT;}
 memcpy(panel,pixels,FRAME_BYTES);memcpy(history,pixels,FRAME_BYTES);++presents;return EPD_DRAW_SUCCESS;
}
static void book_lock_collage_cache_commit(const uint8_t *fb){assert(style==2&&draw_ok&&presents==1&&fb==pixels&&!memcmp(panel,pixels,FRAME_BYTES));}
static void ui_lock_fallback_draw(uint8_t *fb){assert(panel[0]==(current_background));++fallback;memset(fb,255,FRAME_BYTES);frame_ready=true;}
static uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState *h){(void)h;return pixels;}
static void epd_poweron(void){}
static void epd_poweroff(void){}
static void epd_clear(void){if(unlocking)++exit_clears;else{assert(frame_ready);++entry_clears;}memset(panel,255,FRAME_BYTES);}
static void epd_hl_set_all_white(void *h){(void)h;if(unlocking)++exit_white;memset(pixels,255,FRAME_BYTES);}
static enum EpdDrawError epd_hl_update_screen_from_white(void *h,int mode,int temp){(void)h;assert(mode==MODE_GC16&&temp==25&&frame_ready&&panel[0]==255);assert(pixels[0]==(draw_ok?0x10+style:0xff));if(!recovery_ok){panel[0]=0x77;return EPD_DRAW_FAULT;}memcpy(panel,pixels,FRAME_BYTES);memcpy(history,pixels,FRAME_BYTES);++presents;return EPD_DRAW_SUCCESS;}
static void app_font_begin_lock(app_lock_font_t *s){assert(panel[0]==current_background);s->active=true;++font_begin;}
static void app_font_end_lock(app_lock_font_t *s){assert(s->active);++font_end;}
static bool read_pico_transfer_pause_for_sleep(void){++pause;return transfer_ok;}

#define read_pico_transfer_resume_after_sleep() (++resume)
static void pico_boot_clear_resume(void){++clear_resume;unlocking=true;memcpy(wake_frame,panel,FRAME_BYTES);}
static bool pico_boot_has_resume(void){return boot_cache;}
static void pico_boot_ready(void){}
static void pico_boot_hold_resume(void){}
static void app_lock_wait_key_idle(int ms){assert(ms==800);}
static app_wake_source_t app_light_sleep_wait_timed(void *acc,unsigned ms){assert(!acc&&ms==600000);if(hint_visible){memset(panel,0x66,FRAME_BYTES);memcpy(history,panel,FRAME_BYTES);memset(pixels,0x13,FRAME_BYTES);}return timeout?APP_WAKE_TIMEOUT:APP_WAKE_KEY;}
static void app_enter_host_sleep(int mode){assert(mode==APP_SLEEP_DEEP);longjmp(deep,1);}
static void read_pico_pmu_drain_events(void){}
static int64_t esp_timer_get_time(void){return 2000000;}
static bool lock_screen_restore(uint8_t *fb){if(!cache_ok||cached_style!=style)return false;memset(fb,0x10+style,FRAME_BYTES);return true;}
static void lock_screen_authenticate(void *h,void *tp,void *acc,bool boot){(void)h;assert(tp==(void*)2&&acc==(void*)1);assert(pin);assert(boot==boot_cache);assert(pixels[0]==(draw_ok?0x10+style:0xff));++auths;if(!boot){memset(pixels,0x44,FRAME_BYTES);memcpy(panel,pixels,FRAME_BYTES);memcpy(history,pixels,FRAME_BYTES);}}
static int read_pico_sd_get_info(read_pico_sd_info_t *i){i->mounted=true;return 0;}
static void vTaskDelay(int ms){(void)ms;}
static int epd_width(void){return 684;}
static int epd_height(void){return 1216;}
'''
for name in ('draw_wallpaper', 'present_clean_lock', 'enter_lock_and_sleep', 'app_lock_boot_gate'):
    unit += function(name, ROOT/'main/sleep.c') + '\n'
unit += r'''
static void reset(void){assert(!white_allocations);white_scans=0;white_ok=true;white_alloc_fail=false;current_background=0x32;memset(draws,0,sizeof(draws));fallback=presents=auths=font_begin=font_end=pause=resume=clear_resume=0;memset(pixels,0x32,FRAME_BYTES);memset(panel,0x32,FRAME_BYTES);memset(history,0x32,FRAME_BYTES);frame_ready=s_lock_feedback_failed=s_lock_feedback_visible=feedback_seen=false;full_presents=entry_clears=exit_clears=exit_white=0;unlocking=hint_visible=false;recovery_ok=true;final_ok=true;draw_ok=transfer_ok=true;timeout=reader=boot_cache=cache_ok=false;}
int main(void){
 EpdiyHighlevelState hl={.back_fb=history,.front_fb=pixels};int64_t ignore=0;
 // 两种来源的阅读背景只影响票根内容，不决定样式。/ Reader provenance affects ticket content, never the style.
 for(unsigned s=0;s<4;++s)for(unsigned password=0;password<2;++password)for(unsigned body=0;body<2;++body){
  reset();style=s;pin=password;reader=body;
  enter_lock_and_sleep(&hl,&ignore,(void*)1,(void*)2,reader);
  assert(draws[s]==1&&draws[(s+1)%3]==0&&draws[(s+2)%3]==0&&!fallback&&presents==1);
  assert(full_presents==1&&!entry_clears&&white_scans==1&&!white_allocations);
  assert(!exit_clears&&!exit_white);
  {assert(!memcmp(panel,wake_frame,FRAME_BYTES)&&!memcmp(history,wake_frame,FRAME_BYTES));assert(panel[0]==(password?0x44:0x10+s));}
  assert(auths==password&&pause==1&&resume==0&&clear_resume==1&&ignore==3000);
  assert(font_begin==(s==0||s==2||password)&&font_end==font_begin&&app_settings_lock_style()==s);
 }
 // 首页/书架/设置/阅读都保留当前帧；没有全屏阅读重排或历史书籍依赖。
 // Home/shelf/settings/reader all preserve their actual frame without fullscreen reflow or book history.
 for(unsigned screen=0;screen<4;++screen){reset();style=3;pin=false;current_background=0x21+screen*17;memset(pixels,current_background,FRAME_BYTES);memset(panel,current_background,FRAME_BYTES);memset(history,current_background,FRAME_BYTES);enter_lock_and_sleep(&hl,&ignore,(void*)1,(void*)2,false);assert(draws[3]==1&&presents==1&&!fallback);}
 // Transparent art submits from real history once; a failed scan restores through the original clear/from-white route.
 for(unsigned selected=0;selected<4;++selected)for(unsigned success=0;success<2;++success)for(unsigned password=0;password<2;++password){
  reset();style=selected;pin=password;final_ok=success;
  enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);
  assert(full_presents==1&&entry_clears==!success&&presents==1&&auths==password&&!exit_clears&&!exit_white);
 }
 // Failed allocation or white output must keep the completed target intact for the original recovery.
 for(unsigned selected=0;selected<4;++selected)for(unsigned password=0;password<2;++password)for(int failure=0;failure<2;++failure){reset();style=selected;pin=password;white_alloc_fail=failure==0;white_ok=failure==0;
  enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);
  assert(!full_presents&&white_scans==(failure==1)&&entry_clears==1&&presents==1&&!white_allocations&&!exit_clears&&auths==password);
 }
 // A double-click hint may still be physical while the front buffer has restored the artwork.
 reset();style=3;pin=false;hint_visible=true;
 enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);
 assert(!exit_clears&&!exit_white&&panel[0]==0x66&&history[0]==0x66&&pixels[0]==0x13);
 // Unknown/failed physical history retains the old unlock clear even for transparent art.
 for(unsigned selected=0;selected<4;++selected){reset();style=selected;pin=false;final_ok=recovery_ok=false;
 enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);
 assert(full_presents==1&&entry_clears==1&&exit_clears==1&&exit_white==1&&!presents);}
 // Failed local feedback may have partially changed the panel: require the original clear before final art.
 reset();style=2;pin=false;s_lock_feedback_failed=true;panel[1]=0x77;
 enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);
 assert(!full_presents&&entry_clears==1&&presents==1&&!exit_clears&&!s_lock_feedback_failed);
 // 白底圆锁历史一次GC16完成后再缓存/PIN，失败恢复原清屏，自动/其他样式不走提前反馈。/ Present once from true badge history before cache/PIN; faults clear and idle/styles keep the original path.
 for(unsigned success=0;success<2;++success)for(unsigned password=0;password<2;++password){reset();style=2;pin=password;final_ok=success;s_lock_feedback_visible=feedback_seen=true;memset(panel,current_background,FRAME_BYTES);panel[1]=0;memcpy(history,panel,FRAME_BYTES);enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);assert(full_presents==1&&presents==1&&entry_clears==!success&&auths==password&&!s_lock_feedback_visible);}
 reset();style=2;pin=false;transfer_ok=false;s_lock_feedback_visible=feedback_seen=true;enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);assert(!full_presents&&!s_lock_feedback_visible&&!presents);
 // A successful badge must not bypass clearing if final collage composition fails.
 reset();style=2;pin=false;draw_ok=false;s_lock_feedback_visible=feedback_seen=true;panel[1]=0;memcpy(history,panel,FRAME_BYTES);
 enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);
 assert(!full_presents&&entry_clears==1&&fallback==1&&exit_clears==1);
 // 失败只用内建回退，绝不改成其他样式；传输忙时不绘锁屏。/ Draw failure falls back only to built-in art; active uploads defer lock.
 for(unsigned s=0;s<4;++s){reset();style=s;pin=false;draw_ok=false;enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);assert(draws[s]==1&&fallback==1&&style==s&&!full_presents&&entry_clears==1&&exit_clears==1&&exit_white==1);}
 reset();transfer_ok=false;enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);assert(!presents&&!auths&&!font_begin&&clear_resume==1&&!resume);
 // 超时深睡保留已选择的实际画面，不重绘拼贴。/ Deep timeout retains selected art without redrawing a collage.
 for(unsigned s=0;s<4;++s){reset();style=s;pin=false;timeout=true;if(!setjmp(deep)){enter_lock_and_sleep(&hl,NULL,(void*)1,(void*)2,false);assert(0);}assert(presents==1&&draws[s]==1&&style==s&&!resume);}
 // 密码冷启动：同样式缓存才命中，跨样式/损坏时重画当前选择。/ Cold PIN gate redraws the current choice for stale/corrupt cache.
 for(unsigned s=0;s<4;++s)for(unsigned previous=0;previous<4;++previous){
  reset();style=s;pin=boot_cache=cache_ok=true;cached_style=previous;
  app_lock_boot_gate(&hl,(void*)2,(void*)1);
  assert(auths==1&&font_begin==1&&font_end==1&&!presents&&!fallback&&style==s);
  assert(draws[s]==(previous!=s)&&draws[(s+1)%3]==0&&draws[(s+2)%3]==0);
 }
 for(unsigned s=0;s<4;++s){reset();style=s;pin=boot_cache=true;draw_ok=false;app_lock_boot_gate(&hl,(void*)2,(void*)1);assert(auths==1&&draws[s]==1&&fallback==1&&style==s);}
 reset();pin=false;app_lock_boot_gate(&hl,(void*)2,(void*)1);assert(!auths&&!font_begin);
 puts("PASS: all four styles use one white target then intact art and unlock from actual art/PIN history without preclear; failed history retains clearing; failed output/art retains clear fallback; real single-pass final collage retains white/badge history, avoids duplicate entry clear, recovers final faults and consumes feedback before cache/PIN; auto/manual lock entry chooses ticket/wallpaper/collage with/without PIN and reader background; upload deferral; deep timeout; cold gate cache/fault fallback keeps selected style");
}
'''
with tempfile.TemporaryDirectory() as folder:
    c, exe = Path(folder)/'test.c', Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

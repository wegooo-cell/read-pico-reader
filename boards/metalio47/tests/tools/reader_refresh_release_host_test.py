#!/usr/bin/env python3
"""中文：验证S31真实阅读出口、完整图文灰阶和单轮进度。/ English: Verify S31 presenter routing, complete image grays and deferred metadata.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
# Execute the maintained presenter rather than a copy of its routing policy.
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl;uint8_t *fb;int64_t now_ms;bool request_app;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
enum {READING,SHELF,MANAGE,BULK,EDIT,SEARCH,READER_PANEL_NONE,READER_PANEL_BACKGROUND_PREVIEW,READER_PANEL_FONT_SETTINGS,READER_PANEL_ONLINE_FONTS};
enum {MODE_DU,MODE_GL16,MODE_GC16,APP_PAGE_REFRESH_MODE=MODE_GL16};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_BAR_H 96
#define UI_BAR_TOP (UI_LOCK_HEIGHT-24-UI_BAR_H)
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT-12)
#define TAG "test"
#define ESP_LOGI(...) ((void)0)
#define portMAX_DELAY 0
#define E0470_TURN_FAST_TICK_US 11090
static int E0470_WAVEFORM,E0470_FULL_WAVEFORM,E0470_FOLLOW_WAVEFORM;
typedef int EpdWaveform;
static int soft_waveform;
static const int *display_reader_soft_turn_waveform(void){return &soft_waveform;}
static struct{bool valid;}s_reader_panel_frame;
static int s_online_fonts;
static bool ui_online_fonts_present(int *fonts,app_ctx_t *ctx,app_redraw_t redraw){(void)fonts;(void)ctx;(void)redraw;assert(false);return false;}
static bool s_reader_clock_pending,s_input_settle,s_shelf_feedback_pending,s_cover_batch_present_pending,s_shelf_page_pending;
static bool s_reader_image_refresh_pending,s_reader_cleanup,s_reader_turn_pending,s_water_turn_pending,s_clear_confirm,s_reader_fullscreen,s_reader_footer_pending,s_reader_text_frame;
static bool s_reader_image_frame,s_reader_image_exit_pending,hide_images;
static int64_t s_reader_footer_due_ms;
static bool s_toolbar,s_shelf_key_paint_pending,test_panel_short;
static bool reader_indent_present(app_ctx_t*c,app_redraw_t d){(void)c;(void)d;return false;}
static int s_view=READING,s_reader_panel,s_mode=MODE_GL16,s_presented_view=READING,s_water_turn_dir,s_page,s_du_count;
static unsigned s_cover_dirty_mask,s_cover_present_mask;
static int64_t s_reader_clock_minute,s_reader_clock_painted_minute,s_du_ms;
static const char *s_text="text";
static EpdRect s_area={0,0,684,1216},s_du_area;
static void reader_prefetch_end(bool started){assert(!started);}
static int calls,selected,fail,rendered,prepares,images,tick;static unsigned s_turns;
static bool background,immersive;
static bool app_settings_reader_immersive(void){return immersive;}
static int app_settings_reader_turn_effect(void){return tick;}
static EpdRect last_water_area;
static EpdRect last_gray_area;
static int last_gray_mode;static const int *last_gray_wave;
static enum EpdDrawError push(int tag){++calls;selected=tag;return fail?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;}
static enum EpdDrawError push_gray(void *hl,const int *wave,int mode,EpdRect area,int tag){
 (void)hl;last_gray_wave=wave;last_gray_mode=mode;last_gray_area=area;return push(tag);
}
static bool reader_status_visible(void){return !s_reader_fullscreen;}
static int64_t ui_nav_clock_minute(void){return 123;}
static int64_t esp_timer_get_time(void){return 1000000;}
static bool kick_prep(void){return false;}

static const char *app_settings_book_background_path(void){return background?"wallpaper":"";}
static void display_main_transition_cancel(void){}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++rendered;}
static void prepare_covers(app_ctx_t *ctx){(void)ctx;assert(s_view!=READING);}
static void gather_cover_entry(app_ctx_t *ctx){(void)ctx;assert(s_view!=READING);}
static void prepare_inline_image(void){++prepares;}
static bool ui_reader_background_prepare(const char *p){(void)p;return true;}
static unsigned book_layout_page_image_count(int page){(void)page;return images;}
static bool app_settings_reader_hide_images(void){return hide_images;}
static bool shelf_cover_frame_ready(void){return true;}
static EpdRect ui_rect_union(EpdRect a,EpdRect b){int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y;
 int right=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width;
 int bottom=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;
 return(EpdRect){x,y,right-x,bottom-y};}
static EpdRect reader_status_area(void){return(EpdRect){0,0,684,80};}
static void guard_draw_result(void *hl,enum EpdDrawError err){(void)hl;(void)err;}
static int e0470_page_turn_tick_us(void){return 11090;}
static void e0470_page_turn_set_tick_us(int t){assert(t==11090);}
#define display_main_transition_shelf_page() ((void)0)
#define update_display_reader_cleanup(...) push(11)
#define update_display_full(...) push(12)
static enum EpdDrawError update_display_area_diff_with(void *h,const int *w,int m,EpdRect a){return push_gray(h,w,m,a,13);}
static enum EpdDrawError update_display_area_full_with(void *h,const int *w,int m,EpdRect a){return push_gray(h,w,m,a,14);}
#define update_display_area_with(...) push(15)
#define update_display_with(...) push(16)
#define update_display_mode_diff(...) push(17)
static enum EpdDrawError update_display_water_turn(void*hl,EpdRect area,int dir){(void)hl;(void)dir;last_water_area=area;return push(18);}
#define E0470_TURN_RTL 0
#define update_display_text_turn(...) push(20)
#define update_display_reader_panel(...) push(21)
#define update_display_reader_panel_fast(...) push(21)
#define update_display_reader_panel_gray4(...) push(21)
#define update_display_fast_page(...) push(19)
static EpdRect reader_fast_panel_rect(void){return(EpdRect){0};}
static bool reader_slider_feedback_present(app_ctx_t *ctx,app_redraw_t redraw){(void)ctx;(void)redraw;return false;}
'''
unit+='static bool reader_panel_short_ready(app_redraw_t r){return test_panel_short&&r==APP_REDRAW_PAGE;}\nstatic bool reader_panel_fast_present(app_ctx_t *ctx,app_redraw_t redraw){(void)ctx;(void)redraw;return false;}\nstatic void reader_panel_record(enum EpdDrawError err){(void)err;}\nstatic bool display_page_prepare_begin(void){return false;}\nstatic void display_page_prepare_end(bool held){(void)held;}\n'
unit+='\n'.join(line for line in (root/'main/ui/ui_reader_layout.h').read_text().splitlines() if not line.startswith(('#include','#pragma'))) + '\n'
for name in ('reader_area','reader_fullscreen_progress_area','progress_rect'):
 unit+=function(name,root/'main/apps/app_book.c')+'\n'
unit+='static enum EpdDrawError reader_present_crop(void *hl,const EpdWaveform *wave,int mode,EpdRect area,bool full){return full?update_display_area_full_with(hl,wave,mode,area):update_display_area_diff_with(hl,wave,mode,area); }\n'
unit+=function('reader_soft_present',root/'main/apps/app_book.c')+'\n'
unit+=function('present',root/'main/apps/app_book.c')
unit+=r'''
static void reset(void){
 calls=selected=rendered=prepares=fail=images=tick=0;background=test_panel_short=hide_images=immersive=false;s_view=s_presented_view=READING;
 s_reader_image_frame=s_reader_image_exit_pending=false;
 s_reader_panel=READER_PANEL_NONE;s_mode=MODE_GL16;s_reader_fullscreen=true;
 s_reader_footer_pending=true;s_reader_cleanup=false;s_water_turn_pending=false;s_reader_turn_pending=true;
 s_toolbar=s_clear_confirm=s_reader_image_refresh_pending=s_reader_clock_pending=false;s_reader_text_frame=false;s_area=reader_area();
}
int main(void){
 app_ctx_t ctx={0};
 for(unsigned entry=0;entry<2;++entry)for(unsigned img=0;img<2;++img){
  reset();s_presented_view=entry?SEARCH:SHELF;images=img;background=img;s_turns=9;
  present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==14&&s_turns==0&&s_presented_view==READING);
 }
 reset();s_presented_view=SEARCH;fail=1;s_turns=9;
 present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==14&&s_presented_view==SEARCH&&s_turns==9);
 fail=0;calls=0;present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==14&&s_presented_view==READING);

 for(int full=0;full<2;++full){
  reset();s_reader_fullscreen=full;s_area=reader_area();s_reader_footer_pending=false;
  assert(present(&ctx,APP_REDRAW_AREA));
  assert(calls==1&&selected==14&&last_gray_wave==&soft_waveform&&last_gray_mode==MODE_GL16&&s_reader_text_frame);
  assert(s_reader_footer_pending&&!s_reader_cleanup);
 }
 // 速读必须进入独立DU出口；普通/图像不能误用该路径。/ FastBW must reach its separate DU outlet; ordinary/images must not.
 for(int full=0;full<2;++full)for(int hidden=0;hidden<2;++hidden){
  reset();tick=2;s_reader_fullscreen=full;s_area=reader_area();immersive=hidden;s_reader_footer_pending=false;
  present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==20&&s_reader_text_frame);
 }
 reset();tick=2;s_reader_cleanup=true;present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==14);
 reset();tick=2;present(&ctx,APP_REDRAW_FULL);assert(calls==1&&selected==12);
 reset();tick=2;fail=1;present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==20&&!s_reader_text_frame);
 for(int full=0;full<2;++full){
  reset();s_reader_fullscreen=full;s_area=reader_area();s_reader_cleanup=true;
  assert(present(&ctx,APP_REDRAW_AREA));assert(calls==1&&selected==14&&s_reader_text_frame);
  assert(!s_reader_cleanup&&!s_reader_turn_pending);
 }
 reset();s_reader_turn_pending=false;present(&ctx,APP_REDRAW_FULL);assert(calls==1&&selected==12&&rendered==1);
 reset();s_reader_turn_pending=false;present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==17&&rendered==1);
 reset();s_reader_panel=READER_PANEL_BACKGROUND_PREVIEW;s_reader_turn_pending=s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==13&&rendered==1); // UI does not count as whole-body cleanup
 reset();s_reader_panel=READER_PANEL_FONT_SETTINGS;s_reader_turn_pending=s_reader_footer_pending=false;test_panel_short=true;
 present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==21&&rendered==1);
 reset();s_reader_panel=READER_PANEL_FONT_SETTINGS;s_reader_turn_pending=s_reader_footer_pending=false;test_panel_short=true;
 present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==21&&rendered==0); // local UI: no footer replay
 reset();fail=1;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==14&&!s_reader_text_frame);
 reset();s_water_turn_pending=true;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(selected==18&&calls==1&&!s_water_turn_pending);
 for(int full=0;full<2;++full){
  reset();s_reader_fullscreen=full;s_area=reader_area();s_water_turn_pending=true;
  s_area=(EpdRect){0,160,684,900};
  present(&ctx,APP_REDRAW_AREA);assert(selected==18&&calls==1&&!s_water_turn_pending);
  assert(last_water_area.x==0&&last_water_area.y==0&&last_water_area.width==684&&last_water_area.height==1216);
 }
 s_area=(EpdRect){0,0,684,1216};
 reset();fail=1;s_water_turn_pending=true;
 present(&ctx,APP_REDRAW_AREA);assert(selected==18&&calls==1&&!s_reader_text_frame);
 // 图像始终保留灰阶，进度同轮输出。/ Images retain gray targets with deferred metadata.
 for(int effect=0;effect<3;++effect)for(int full=0;full<2;++full)for(int hide_status=0;hide_status<2;++hide_status)for(int kind=0;kind<3;++kind){
  reset();tick=effect;immersive=hide_status;s_reader_fullscreen=full;s_area=reader_area();s_reader_footer_pending=false;s_reader_turn_pending=false;
  images=kind!=2;s_reader_image_refresh_pending=images!=0;s_reader_image_exit_pending=kind!=0;
  present(&ctx,APP_REDRAW_AREA);
  assert(calls==1&&selected==14&&last_gray_wave==&soft_waveform&&last_gray_mode==MODE_GL16);
  assert(last_gray_area.x==0&&last_gray_area.width==684&&last_gray_area.y==0);
  assert(last_gray_area.height==1216);
  assert(s_reader_image_frame==(images!=0)&&s_reader_text_frame==!images&&!s_reader_image_exit_pending);
 }
 // Manual/periodic cleanup takes priority even on image-bearing pages, and never uses a sparse GC mask.
 for(int full=0;full<2;++full)for(int kind=0;kind<2;++kind){
  reset();images=1;s_reader_image_refresh_pending=true;s_reader_fullscreen=full;s_area=reader_area();s_reader_turn_pending=false;
  s_reader_cleanup=kind==0;present(&ctx,kind?APP_REDRAW_FULL:APP_REDRAW_AREA);
  assert(calls==1&&selected==(kind?12:14)&&s_reader_image_frame&&!s_reader_text_frame);
  if(!kind)assert(last_gray_mode==MODE_GC16&&last_gray_wave==&E0470_FULL_WAVEFORM&&last_gray_area.y==0&&last_gray_area.height==1216);
 }
 reset();images=1;s_reader_image_refresh_pending=true;s_reader_turn_pending=false;
 present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==14&&last_gray_wave==&soft_waveform&&last_gray_mode==MODE_GL16&&last_gray_area.y==0&&last_gray_area.height==1216);
 // A failed image/exit output cannot become either displayed-image history or a text-only fast baseline.
 for(int exit=0;exit<2;++exit){
  reset();fail=1;images=!exit;s_reader_image_refresh_pending=!exit;s_reader_image_exit_pending=exit;
  present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==14&&!s_reader_image_frame&&!s_reader_text_frame);
 }
 reset();images=1;hide_images=true;s_reader_turn_pending=false;s_reader_fullscreen=false;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(!s_reader_image_frame);
 reset();s_reader_image_exit_pending=true;ctx.request_app=true;
 present(&ctx,APP_REDRAW_AREA);assert(calls==0);ctx.request_app=false;
 reset();s_reader_clock_pending=true;s_reader_fullscreen=false;s_reader_turn_pending=false;
 s_reader_clock_painted_minute=321;
 present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==13&&s_reader_clock_minute==321&&!s_reader_clock_pending);
 reset();ctx.request_app=true;present(&ctx,APP_REDRAW_AREA);assert(calls==0);ctx.request_app=false;
 puts("PASS: S31 ordinary soft / FastBW DU / ripple and fused image/exit outputs with deferred metadata; full GC48 cleanup priority, failed-frame history, panels, clock and cancellation guards");
}
'''
with tempfile.TemporaryDirectory(prefix='kiikoread-reader-release-') as folder:
 c=Path(folder)/'test.c';c.write_text(unit)
 for enabled in (0,1):
  binary=Path(folder)/f'test-{enabled}'
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-g','-fsanitize=address,undefined',f'-DKIIKO_S31_TEXT_RIPPLE={enabled}',str(c),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)

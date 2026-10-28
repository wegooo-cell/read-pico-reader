#!/usr/bin/env python3
"""中文：验证进入阅读的完整清理、保留擦除的灰阶差分与合并进度。/ English: Verify entry cleanup, erase-preserving gray differences and fused progress.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
# 生成视图没有历史Git对象；下面执行真实present，校验调用次数、模式及失败边界。
# Generated views have no historical Git objects; execute the real presenter and check calls, modes and failure boundaries.
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl;uint8_t *fb;bool request_app;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
enum {READING,SHELF,MANAGE,EDIT,SEARCH,TOC,READER_PANEL_NONE,READER_PANEL_BACKGROUND_PREVIEW};
enum {MODE_DU,MODE_GL16,MODE_GC16,APP_PAGE_REFRESH_MODE=MODE_GL16};
#define TAG "test"
#define ESP_LOGI(...) ((void)0)
#define portMAX_DELAY 0
#define E0470_TURN_FAST_TICK_US 11090
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_BAR_H 96
#define UI_BAR_TOP (UI_LOCK_HEIGHT-24-UI_BAR_H)
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT-12)
static int E0470_WAVEFORM,E0470_FULL_WAVEFORM,E0470_FOLLOW_WAVEFORM;
static int reader_waveform,soft_turn_waveform;
static const int *display_main_gray_waveform(void){return &reader_waveform;}
static const int *display_reader_soft_turn_waveform(void){return &soft_turn_waveform;}
static bool s_reader_clock_pending,s_input_settle,s_shelf_feedback_pending,s_cover_batch_present_pending,s_shelf_page_pending;
static bool s_reader_image_refresh_pending,s_reader_cleanup,s_reader_turn_pending,s_water_turn_pending,s_clear_confirm,s_reader_fullscreen,s_reader_footer_pending,s_reader_text_frame;
static bool s_toolbar;
static int s_view=READING,s_reader_panel,s_mode=MODE_GL16,s_presented_view=READING,s_water_turn_dir,s_page,s_du_count;
static unsigned s_cover_dirty_mask,s_cover_present_mask;
static unsigned s_turns;
static int64_t s_reader_clock_minute,s_reader_clock_painted_minute,s_du_ms;
static const char *s_text="text";
static EpdRect s_area,s_du_area;
static void reader_prefetch_end(bool started){assert(!started);}
static int calls,selected,fail,rendered,prepares,images,tick;
static bool background,immersive;
static EpdRect areas[8];
static int modes[8];
static const int *waves[8];
static enum EpdDrawError push(int tag){++calls;selected=tag;return fail?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;}
static enum EpdDrawError push_area(int tag,const int *wave,int mode,EpdRect area){
 assert(calls<8);areas[calls]=area;modes[calls]=mode;waves[calls]=wave;return push(tag);
}
static bool reader_status_visible(void){return !s_reader_fullscreen;}
static int64_t ui_nav_clock_minute(void){return 123;}
static int64_t esp_timer_get_time(void){return 1000000;}
static bool kick_prep(void){return false;}

static const char *app_settings_book_background_path(void){return background?"wallpaper":"";}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++rendered;}
static void prepare_covers(app_ctx_t *ctx){(void)ctx;assert(s_view!=READING);}
static void gather_cover_entry(app_ctx_t *ctx){(void)ctx;assert(s_view!=READING);}
static void prepare_inline_image(void){++prepares;}
static bool ui_reader_background_prepare(const char *p){(void)p;return true;}
static unsigned book_layout_page_image_count(int page){(void)page;return images;}
static bool shelf_cover_frame_ready(void){return true;}
static bool app_settings_reader_immersive(void){return immersive;}
static EpdRect reader_status_area(void){return (EpdRect){0,0,UI_LOCK_WIDTH,80};}
static int app_settings_reader_turn_effect(void){return tick;}
static void guard_draw_result(void *hl,enum EpdDrawError err){(void)hl;(void)err;}
static int e0470_page_turn_tick_us(void){return 11090;}
static void e0470_page_turn_set_tick_us(int t){assert(t==11090);}
#define display_main_transition_shelf_page() ((void)0)
#define update_display_full(...) push(12)
static enum EpdDrawError update_display_area_diff_with(void *hl,const int *wave,int mode,EpdRect area){
 (void)hl;return push_area(13,wave,mode,area);
}
static enum EpdDrawError update_display_area_full_with(void *hl,const int *wave,int mode,EpdRect area){
 (void)hl;return push_area(14,wave,mode,area);
}
static enum EpdDrawError update_display_area_with(void *hl,const int *wave,int mode,EpdRect area){
 (void)hl;return push_area(15,wave,mode,area);
}
#define update_display_with(...) push(16)
#define update_display_mode_diff(...) push(17)
#define display_main_normal_waveform() (&E0470_WAVEFORM)
#define update_display_water_turn(...) push(18)
#define E0470_TURN_RTL 0
static enum EpdDrawError update_display_text_turn(void *hl,EpdRect area,int dir) {
 (void)hl;(void)dir;assert(tick==2);return push_area(20,&E0470_FULL_WAVEFORM,MODE_DU,area);
}
#define update_display_fast_page(...) push(19)
'''
unit+='static bool s_shelf_key_paint_pending;\n'
unit+='static bool reader_indent_present(app_ctx_t *ctx,app_redraw_t redraw){(void)ctx;(void)redraw;return false;}\n'
for name in ('clip_refresh_rect','ui_rect_union'):
 unit+=function(name,root/'main/ui/ui_kit.c')+'\n'
for name in ('reader_area','reader_fullscreen_progress_area','progress_rect'):
 unit+=function(name,root/'main/apps/app_book.c')+'\n'
unit+=function('present',root/'main/apps/app_book.c')
unit+=r'''
static void reset(void){
 calls=selected=rendered=prepares=fail=images=tick=0;background=immersive=false;s_view=s_presented_view=READING;
 s_reader_panel=READER_PANEL_NONE;s_mode=MODE_GL16;s_reader_fullscreen=true;
 s_reader_footer_pending=true;s_reader_cleanup=false;s_water_turn_pending=false;s_reader_turn_pending=true;
 s_toolbar=s_clear_confirm=s_reader_image_refresh_pending=s_reader_clock_pending=false;s_reader_text_frame=false;
 s_turns=7;
 s_area=reader_area();memset(areas,0,sizeof(areas));memset(modes,0,sizeof(modes));memset(waves,0,sizeof(waves));
}
static void same_area(EpdRect a,EpdRect b){assert(a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height);}
int main(void){
 app_ctx_t ctx={0};
 // 首页/书架/目录/唤醒进入正文：纯文字、背景或图片都只完整GC16一次，成功后重置清理计数。
 // Home/shelf/TOC/wake entry: text, wallpaper and images each get one complete GC16, resetting the cleanup count on success.
 const int origins[]={-1,SHELF,TOC};
 const app_redraw_t entry_redraws[]={APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL};
 for(unsigned origin=0;origin<3;++origin)for(unsigned redraw=0;redraw<3;++redraw)
 for(int content=0;content<3;++content){
  reset();s_presented_view=origins[origin];s_reader_turn_pending=false;
  images=content==1;s_reader_image_refresh_pending=images!=0;background=content==2;
  present(&ctx,entry_redraws[redraw]);
  assert(calls==1&&selected==14&&modes[0]==MODE_GC16&&waves[0]==&E0470_FULL_WAVEFORM);
  same_area(areas[0],(EpdRect){0,0,684,1216});
  assert(rendered==1&&s_presented_view==READING&&s_turns==0);
 }
 reset();s_presented_view=SHELF;fail=1;present(&ctx,APP_REDRAW_PAGE);
 assert(calls==1&&selected==14&&s_presented_view==SHELF&&s_turns==7&&!s_reader_text_frame);
 fail=0;calls=rendered=0;present(&ctx,APP_REDRAW_PAGE);
 assert(calls==1&&selected==14&&s_presented_view==READING&&s_turns==0);
 reset();s_presented_view=SHELF;present(&ctx,APP_REDRAW_NONE);
 assert(!calls&&s_turns==7&&s_presented_view==SHELF);
 present(&ctx,APP_REDRAW_DONE);assert(!calls&&s_turns==7&&s_presented_view==SHELF);
 ctx.request_app=true;present(&ctx,APP_REDRAW_PAGE);
 assert(!calls&&s_turns==7&&s_presented_view==SHELF);ctx.request_app=false;
 // 普通翻页整屏推白：一次柔和差分覆盖整屏白底（含页脚），黑字/中灰保持。
 // Ordinary turns whiten the whole panel in one soft difference, footer included.
 for(int full=0;full<2;++full)for(int hide_status=0;hide_status<2;++hide_status){
  reset();s_reader_fullscreen=full;immersive=hide_status;s_reader_footer_pending=!full;s_area=reader_area();
  assert(present(&ctx,APP_REDRAW_AREA));
  assert(calls==1&&selected==13&&modes[0]==MODE_GL16&&waves[0]==&soft_turn_waveform&&s_reader_text_frame);
  same_area(areas[0],(EpdRect){0,0,684,1216});
  assert(!s_reader_footer_pending&&!s_reader_cleanup);
 }
 for(int full=0;full<2;++full){
  reset();s_reader_fullscreen=full;s_reader_cleanup=true;
  assert(present(&ctx,APP_REDRAW_AREA));assert(calls==1&&selected==14&&s_reader_text_frame);
  assert(modes[0]==MODE_GC16&&waves[0]==&E0470_FULL_WAVEFORM);
  same_area(areas[0],(EpdRect){0,0,684,1216});
  assert(!s_reader_cleanup&&!s_reader_turn_pending);
 }
 reset();s_reader_turn_pending=false;present(&ctx,APP_REDRAW_FULL);assert(calls==1&&selected==12&&rendered==1);
 reset();s_reader_turn_pending=false;present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==13&&rendered==1);
 reset();fail=1;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==13&&!s_reader_text_frame);
 // 非翻页局部、背景和图片同样只推真实差分；未变化像素不驱动，DU 继续用跟随表。
 // Other local, background and image updates also present true differences only;
 // unchanged pixels are not driven and DU keeps the follow table.
 reset();s_reader_turn_pending=false;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(calls==2&&selected==13&&waves[0]==&soft_turn_waveform&&modes[0]==MODE_GL16);
 same_area(areas[0],reader_area());same_area(areas[1],reader_fullscreen_progress_area());
 reset();background=true;s_reader_turn_pending=false;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(calls==2&&waves[0]==&soft_turn_waveform&&!s_reader_text_frame);
 reset();s_reader_turn_pending=false;s_mode=MODE_DU;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(calls==2&&modes[0]==MODE_DU&&waves[0]==&E0470_FOLLOW_WAVEFORM);
 reset();s_water_turn_pending=true;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(selected==13&&calls==2&&!s_water_turn_pending);
 reset();images=1;s_reader_image_refresh_pending=true;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(selected==13&&calls==1&&waves[0]==&soft_turn_waveform&&!s_reader_text_frame);
 same_area(areas[0],(EpdRect){0,0,684,1216});
 reset();images=1;s_reader_image_refresh_pending=true;s_reader_footer_pending=false;tick=1;
 present(&ctx,APP_REDRAW_AREA);assert(selected==13&&calls==1&&waves[0]==&soft_turn_waveform&&!s_reader_text_frame);tick=0;
 reset();s_reader_clock_pending=true;s_reader_fullscreen=false;s_reader_turn_pending=false;
 s_reader_clock_painted_minute=321;
 present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==13&&s_reader_clock_minute==321&&!s_reader_clock_pending);
 reset();s_view=s_presented_view=SHELF;s_shelf_key_paint_pending=true;
 present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==13&&!s_shelf_key_paint_pending&&!prepares);
 reset();ctx.request_app=true;present(&ctx,APP_REDRAW_AREA);assert(calls==0);ctx.request_app=false;
 // 新速读与普通共用连续文字资格，全文/页脚一次；清理和图像不被短路。
 // Fast BW shares consecutive-text eligibility, fusing the footer; cleanup and images take priority.
 for(int full=0;full<2;++full)for(int hide_status=0;hide_status<2;++hide_status) {
  reset();tick=2;s_reader_fullscreen=full;immersive=hide_status;s_reader_footer_pending=!full;s_area=reader_area();
  assert(present(&ctx,APP_REDRAW_AREA)&&calls==1&&selected==20&&s_reader_text_frame);
  same_area(areas[0],(EpdRect){0,full?(hide_status?0:80):160,684,
             (full?1216:1192)-(full?(hide_status?0:80):160)});
 }
 for(int kind=0;kind<9;++kind) {
  reset();tick=2;
  if(kind==0)s_reader_cleanup=true;
  if(kind==1){images=1;s_reader_image_refresh_pending=true;}
  if(kind==2)background=true;
  if(kind==3)s_reader_turn_pending=false;
  if(kind==4)s_reader_panel=99;
  if(kind==5)s_toolbar=true;
  if(kind==6)s_clear_confirm=true;
  if(kind==7)s_presented_view=SHELF;
  present(&ctx,kind==8?APP_REDRAW_FULL:APP_REDRAW_AREA);assert(selected!=20);
 }
 reset();tick=2;fail=1;present(&ctx,APP_REDRAW_AREA);assert(selected==20&&!s_reader_text_frame);
 puts("PASS: FastBW fuses body/footer, preserves full/periodic/entry/image/background/panel routes and rejects failed history");
 puts("PASS: reader entry cleans once with complete GC16 including images/backgrounds, retaining failure retry; ordinary text/progress fuse erase-preserving gray differences; periodic GC16, ripple, local controls, clock and cancellation retain their paths");
}
'''
with tempfile.TemporaryDirectory(prefix='kiikoread-reader-release-') as folder:
 c=Path(folder)/'test.c';binary=Path(folder)/'test';c.write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-g','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)

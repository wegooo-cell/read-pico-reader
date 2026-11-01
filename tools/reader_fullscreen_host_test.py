"""真实翻页与全屏重排回归，确保不跨过未读文字。/ Actual turns/reflow must not skip unread text.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "book/book_file_path.h"
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int64_t now_ms;int leaf;bool consumed,user_activity;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {READING,TOC,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS};
#define BOOK_TOC_ROWS 7
#define MODE_GL16 1
#define E0470_TURN_RTL 1
#define E0470_TURN_LTR -1
#define ESP_LOGI(...) ((void)0)
static char s_requested_open[288];
static bool s_requested_fullscreen=true,s_requested_open_home;
// Real reader_zoom_reset dependencies: keep its cleanup in the exercised turn path.
#define READER_IMAGE_ZOOM_MIN 1000
static bool s_zoom_active,s_image_pan_active,s_image_zoom_bw;
static int s_zoom_slot,s_image_zoom;
static size_t s_image_zoom_page,s_zoom_bits_bytes;
static uint32_t s_image_zoom_start_distance;
static uint8_t *s_zoom_bits;
static struct {bool pending;} s_reader_tap;
static const char *s_text="text";
static size_t s_text_len=900,s_block_count,s_chapter,s_page,s_jump_offset;
typedef struct {int image;unsigned image_width,image_height;} blk_t;
static blk_t *s_blocks;
static int s_px=48,s_reader_panel=READER_PANEL_NONE,s_reader_slider=-1,s_view=READING;
#define s_toolbar (s_reader_panel!=READER_PANEL_NONE)
static bool s_reader_fullscreen,s_clear_confirm,s_reader_text_frame,s_reader_turn_pending;
static bool s_water_turn_pending,s_reader_cleanup,draw_locked,load_failed;
static int s_water_turn_dir;
static unsigned s_stats_pending_turns,s_session_turns,s_turns,s_unsaved;
static int64_t s_last_turn_ms,s_stats_activity_ms;
static bool s_save_failed;
static char s_message[128];
static unsigned layout_span=100,reflows,paints,saves,failed_reflows;
static uint8_t full_pages;
static const char *app_settings_book_background_path(void){return "";}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static void lock_draw(void){assert(!draw_locked);draw_locked=true;}
static void unlock_draw(void){assert(draw_locked);draw_locked=false;}
static void invalidate_prep(void){}
static void refresh_chapter_lead(void){}
static bool immersive,persisted_immersive,hide_images,vertical,power_turn;
static unsigned style_saves;
static bool app_settings_reader_immersive(void){return immersive;}
static void app_settings_set_reader_immersive(bool v){immersive=persisted_immersive=v;++style_saves;}
static bool app_settings_reader_hide_images(void){return hide_images;}
static void app_settings_set_reader_hide_images(bool v){hide_images=v;}
static void book_layout_set_images_visible(bool v){(void)v;}
static bool app_settings_book_vertical(void){return vertical;}
static void app_settings_set_book_vertical(bool v){vertical=v;}
static void book_layout_set_vertical(bool v){(void)v;}
static bool app_settings_reader_power_turn(void){return power_turn;}
static void app_settings_set_reader_power_turn(bool v){power_turn=v;}
static EpdRect body_rect(void){return(EpdRect){0,0,684,s_reader_fullscreen?(app_settings_reader_immersive()?180:150):100};}
static char s_chapter_heading_title[8],s_chapter_heading_label[8],s_reader_notice[128];
static size_t s_chapter_lead_skip,s_image_count;static unsigned s_chapter_lead_row_height;
static bool s_reader_footer_pending,s_reader_image_refresh_pending;
static int64_t s_reader_notice_until;
static void *s_images[1];
static int book_chapter_image_dimensions(size_t ch,void *image,unsigned *w,unsigned *h){(void)ch;(void)image;*w=*h=1;return 0;}
#define ESP_OK 0
static void vTaskDelay(int ticks){(void)ticks;}
static int64_t esp_timer_get_time(void){return 1000;}
static void apply_chapter_lead(const char*t,const char*l,size_t skip,unsigned h){(void)t;(void)l;(void)skip;(void)h;assert(draw_locked);}
static size_t book_layout_page_start_offset(size_t page){return page*layout_span;}
static size_t book_layout_page_for_offset(size_t off){return off/layout_span;}
static size_t book_layout_page_count(void){return (s_text_len+layout_span-1)/layout_span;}
static bool book_layout_build_blocks(const char *text,size_t len,const void *blocks,size_t count,EpdRect r,int px){
 (void)text;(void)len;(void)blocks;(void)count;(void)px;assert(draw_locked);++reflows;
 if(failed_reflows){--failed_reflows;return false;}layout_span=r.height;return true;
}
static void release_page_images(void){}
static void prepare_inline_image(void){}
static void save_progress(void){++saves;}
static void free_book(void){assert(!draw_locked);s_text=NULL;}
static size_t book_chapter_count(void){return 2;}
static bool load_chapter(app_ctx_t *ctx,size_t chapter,int off,bool last){(void)ctx;(void)off;if(load_failed)return false;s_chapter=chapter;s_page=last?book_layout_page_count()-1:0;return true;}
static void set_reader_view(int view){s_view=view;}
// 目录实现另有真实源测试；这里记录边界失败返回的当前位置。/ TOC has its own source tests; track the failed-turn anchor here.
static size_t current_toc_position(void){return s_chapter;}
static int book_toc_open(size_t current){assert(current==s_chapter);return (int)current;}
static void skip_hidden_image_pages(app_ctx_t *ctx,int dir){(void)ctx;(void)dir;}
static uint8_t app_settings_reader_full_pages(void){return full_pages;}
static int effect=1;
static int app_settings_reader_turn_effect(void){return effect;}
static int test_images;
static int book_layout_page_image_count(size_t page){(void)page;return test_images;}
static app_redraw_t paint_reading(app_ctx_t *ctx,int mode){(void)ctx;assert(mode==MODE_GL16);++paints;return APP_REDRAW_AREA;}
'''
unit += r"""
static int indent=2,adjustment,layout_indent=2,layout_adjustment;
static int app_settings_book_indent(void){return indent;}
static int app_settings_book_indent_adjust(void){return adjustment;}
static void app_settings_set_book_indent(uint8_t em){indent=em;}
static void app_settings_set_book_indent_adjust(int8_t px){adjustment=px;}
static void book_layout_set_first_line_indent(unsigned em){assert(draw_locked);layout_indent=em;}
static void book_layout_set_first_line_indent_adjust(int px){assert(draw_locked);layout_adjustment=px;}
"""
unit += function('apply_reader_indent', BOOK) + '\n'
for name in ('reader_zoom_reset', 'app_book_request_open', 'app_book_request_open_from_home', 'app_book_request_resume',
             'toggle_reader_fullscreen', 'apply_reader_option', 'reader_turn_chrome', 'turn_page'):
    unit += function(name, BOOK) + '\n'
unit += r'''
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
enum {UI_GESTURE_SWIPE_U,UI_GESTURE_TAP};
typedef struct {int type;unsigned x0,y0,x,y;} ui_gesture_event_t;
static bool s_scan_pending;
static int s_pressed_control;
static void pause_reader_prep(void){}
'''
unit += function('reader_tools_swipe', BOOK) + '\n'
unit += r'''
static void reset(bool full,bool tools,size_t page,size_t chapter){
 immersive=persisted_immersive=false;style_saves=0;s_text="text";s_view=READING;s_reader_fullscreen=full;layout_span=full?150:100;s_page=page;s_chapter=chapter;
 s_reader_panel=tools?READER_PANEL_TOOLS:READER_PANEL_NONE;s_reader_slider=2;
 reflows=paints=saves=s_stats_pending_turns=s_session_turns=s_turns=s_unsaved=0;
 failed_reflows=0;load_failed=false;full_pages=0;s_reader_text_frame=true;
 s_reader_turn_pending=s_water_turn_pending=s_reader_cleanup=false;s_jump_offset=0;
}
int main(void){
 app_ctx_t ctx={.now_ms=1000};
 reset(true,false,2,0);size_t anchor=book_layout_page_start_offset(s_page);
 assert(apply_reader_indent(&ctx,2,-7)==APP_REDRAW_PAGE&&adjustment==-7&&layout_adjustment==-7&&indent==2);
 assert(book_layout_page_start_offset(s_page)==anchor&&saves==1);
 assert(apply_reader_indent(&ctx,2,-7)==APP_REDRAW_NONE&&saves==1);
 failed_reflows=1;
 assert(apply_reader_indent(&ctx,3,12)==APP_REDRAW_NONE&&indent==2&&adjustment==-7&&layout_indent==2&&layout_adjustment==-7&&saves==1);
 assert(book_layout_page_start_offset(s_page)==anchor&&!draw_locked);
 assert(apply_reader_indent(&ctx,2,101)==APP_REDRAW_NONE&&saves==1);
 assert(apply_reader_indent(&ctx,2,-101)==APP_REDRAW_NONE&&saves==1);

 assert(app_book_request_open("/sdcard/book.epub")&&!s_requested_fullscreen&&!s_requested_open_home);
 assert(app_book_request_resume("/sdcard/book.epub",false)&&!s_requested_fullscreen&&s_requested_open_home);
 assert(app_book_request_open_from_home("/sdcard/other.epub")&&!s_requested_fullscreen);
 assert(!app_book_request_open(NULL)&&!app_book_request_open(""));
 for(int full=0;full<2;++full)for(int tools=0;tools<2;++tools)for(int dir=-1;dir<=1;dir+=2){
  reset(full,tools,2,0);size_t destination=book_layout_page_start_offset(s_page+dir);
  s_zoom_bits=malloc(32);assert(s_zoom_bits);s_zoom_bits_bytes=32;
  s_zoom_active=s_image_pan_active=s_image_zoom_bw=s_reader_tap.pending=true;
  s_zoom_slot=2;s_image_zoom=2000;s_image_zoom_page=2;s_image_zoom_start_distance=99;
  app_redraw_t r=turn_page(&ctx,dir);
  assert(!s_zoom_bits&&!s_zoom_bits_bytes&&!s_zoom_active&&!s_image_pan_active&&!s_image_zoom_bw&&!s_reader_tap.pending);
  assert(s_zoom_slot==-1&&s_image_zoom==READER_IMAGE_ZOOM_MIN&&s_image_zoom_page==SIZE_MAX&&!s_image_zoom_start_distance);
  assert(s_reader_fullscreen==!!full&&s_reader_panel==READER_PANEL_NONE&&s_reader_slider==-1);
  assert(s_stats_pending_turns==1&&s_session_turns==1&&paints==1);
  assert(book_layout_page_start_offset(s_page)<=destination&&destination<book_layout_page_start_offset(s_page)+layout_span);
  assert(!reflows);
  assert(r==(tools?APP_REDRAW_PAGE:APP_REDRAW_AREA));
  assert(s_water_turn_pending==!tools);
 }
 // 普通文字恢复正式版的翻页与周期清屏标志。/ Restore release text-turn and periodic-cleanup state.
 effect=0;
 for(int tools=0;tools<2;++tools)for(int full=0;full<2;++full)for(int prior=0;prior<2;++prior){
  reset(full,tools,2,0);s_reader_text_frame=prior;full_pages=5;s_turns=4;
  assert(turn_page(&ctx,1)!=APP_REDRAW_NONE&&!s_water_turn_pending&&s_reader_cleanup);
 }
 reset(true,false,2,0);test_images=1;assert(turn_page(&ctx,1)==APP_REDRAW_AREA);
 test_images=0;effect=1;
 // Mixed/image targets and leaving an image never run the text ripple waveform.
 reset(true,false,2,0);test_images=2;assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_water_turn_pending&&!s_reader_turn_pending);
 reset(true,false,2,0);test_images=0;s_reader_text_frame=false;
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_water_turn_pending&&!s_reader_turn_pending);
 test_images=0;
 reset(false,true,0,0);assert(turn_page(&ctx,-1)==APP_REDRAW_PAGE);
 assert(!s_reader_fullscreen&&!s_toolbar&&s_page==0&&!paints&&!s_stats_pending_turns);
 reset(false,true,8,1);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE);
 assert(!s_reader_fullscreen&&!s_toolbar&&s_page==8&&!s_stats_pending_turns);
 reset(true,true,5,1);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&!s_toolbar&&!reflows);
 reset(false,true,8,0);assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&s_chapter==1&&s_page==0&&!s_reader_fullscreen);
 reset(false,true,0,1);assert(turn_page(&ctx,-1)==APP_REDRAW_PAGE&&s_chapter==0&&s_page==8&&!s_reader_fullscreen);
 reset(false,true,8,0);load_failed=true;assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&s_view==TOC&&!paints&&!s_stats_pending_turns);
 // Explicit mode changes retain the anchor and roll back failed pagination; turns never reflow.
 reset(false,true,2,0);assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_FULL);
 assert(s_reader_fullscreen&&s_page==1&&layout_span==150&&reflows==1&&saves==1);
 assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_FULL&&!s_reader_fullscreen&&s_page==1&&layout_span==100);
 reset(false,true,2,0);failed_reflows=1;assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_NONE);
 assert(s_text&&!s_reader_fullscreen&&layout_span==100&&s_page==2&&reflows==2&&!saves);
 reset(false,true,2,0);failed_reflows=2;assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_PAGE);
 assert(!s_text&&s_view==SHELF&&!paints&&!s_stats_pending_turns&&s_message[0]);
 // Real fullscreen exit/reflow plus tools must preserve the source anchor in one gesture.
 for(int style=0;style<2;++style){
  reset(true,false,3,0);immersive=style;layout_span=style?180:150;
  size_t before=book_layout_page_start_offset(s_page);bool handled=false;
  ui_gesture_event_t swipe={UI_GESTURE_SWIPE_U,342,1180,342,980};
  assert(reader_tools_swipe(&ctx,&swipe,&handled)==APP_REDRAW_FULL&&handled);
  assert(!s_reader_fullscreen&&s_reader_panel==READER_PANEL_TOOLS&&reflows==1&&saves==1);
  assert(book_layout_page_start_offset(s_page)<=before&&before<book_layout_page_start_offset(s_page+1));
  reset(true,false,3,0);failed_reflows=1;
  assert(reader_tools_swipe(&ctx,&swipe,&handled)==APP_REDRAW_NONE&&handled);
  assert(s_reader_fullscreen&&s_reader_panel==READER_PANEL_NONE&&s_page==3&&reflows==2&&!saves);
 }
 // 沉浸偏好独立保存：非全屏不重排，全屏改样式保留字节锚点。/ Persist style independently: no nonfullscreen reflow; preserve fullscreen byte anchors.
 reset(false,true,2,0);assert(apply_reader_option(&ctx,1)==APP_REDRAW_PAGE);
 assert(!s_reader_fullscreen&&immersive&&persisted_immersive&&style_saves==1&&!reflows&&s_page==2);
 assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_FULL&&s_reader_fullscreen&&immersive&&layout_span==180&&s_page==1&&style_saves==1);
 assert(toggle_reader_fullscreen(&ctx)==APP_REDRAW_FULL&&!s_reader_fullscreen&&immersive&&style_saves==1);
 reset(true,true,2,0);size_t style_anchor=book_layout_page_start_offset(s_page);
 assert(apply_reader_option(&ctx,1)==APP_REDRAW_FULL&&s_reader_fullscreen&&immersive&&persisted_immersive);
 assert(book_layout_page_start_offset(s_page)<=style_anchor&&style_anchor<book_layout_page_start_offset(s_page)+layout_span&&layout_span==180&&s_reader_panel==READER_PANEL_TOOLS);
 reset(true,true,2,0);failed_reflows=1;
 assert(apply_reader_option(&ctx,1)==APP_REDRAW_PAGE&&s_reader_fullscreen&&!immersive&&!persisted_immersive&&s_page==2&&layout_span==150&&reflows==2&&!saves);
 reset(true,true,2,0);failed_reflows=2;
 assert(apply_reader_option(&ctx,1)==APP_REDRAW_PAGE&&!s_text&&s_view==SHELF&&!immersive&&!persisted_immersive);
 // 速读黑白严格使用Pico既有0/5/10/15/30间隔，没有隐藏十页清理上限。
 // Fast BW uses Pico's existing0/5/10/15/30 intervals without a hidden ten-turn cap.
 const unsigned intervals[]={0,5,10,15,30};effect=2;
 for(unsigned period=0;period<sizeof(intervals)/sizeof(intervals[0]);++period) {
  reset(true,false,2,0);full_pages=intervals[period];
  for(unsigned n=1;n<=65;++n) {
   s_reader_text_frame=true;
   assert(turn_page(&ctx,n&1?1:-1)==APP_REDRAW_AREA&&s_reader_turn_pending&&!s_water_turn_pending);
   assert(s_reader_cleanup==(full_pages&&n%full_pages==0));
  }
 }
 puts("PASS: standard entry and explicit sleep overrides; mode-preserving destination anchors; toolbar dismissal; chapter/boundary turns; failed reflow restores safely or returns to the shelf");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    c, exe = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(ROOT / 'main'),
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

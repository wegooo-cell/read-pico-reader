#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：真实手势与缩进控件的连续调整、抬手提交、中断和边界回归。
# English: Actual gesture/indent controls: hold repeats, release commits, interruptions and hit boundaries.
from pathlib import Path
import re, subprocess, tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
book=root/'main/apps/app_book.c'
text=book.read_text()
unit=r'''
#include <assert.h>
#include "book/book_font_size.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool touched;uint8_t count;uint16_t x,y;} cst836u_touch_t;
typedef struct {uint8_t *fb;void *hl;const cst836u_touch_t *touch;int64_t now_ms;bool pressed,released,consumed,user_activity,request_menu,request_return;void *request_app;}app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL}app_redraw_t;
enum {READING,SHELF,READER_PANEL_NONE,READER_PANEL_LAYOUT_SETTINGS,READER_PANEL_FONT_SETTINGS};
enum {EPD_DRAW_ALIGN_CENTER,MODE_DU};
#define UI_LONG_PRESS_MS 500
#define UI_TOUCH_SLOP_PX 24
#define UI_SWIPE_MIN_PX 120
#define UI_GRAY_BLACK 0
static int s_view=READING,s_reader_panel=READER_PANEL_LAYOUT_SETTINGS,s_pressed_control=-1,s_reader_slider=-1,s_margin,s_px=48;
static bool s_clear_confirm,s_indent_feedback_pending;
static EpdRect s_area;
static int s_mode;
static struct {bool pending;}s_reader_tap;
'''
settings=(root/'main/settings.h').read_text()
for name in ('APP_BOOK_TRACK_MIN','APP_BOOK_TRACK_MAX','APP_BOOK_TRACK_COUNT','APP_BOOK_PARAGRAPH_COUNT'):
    unit+=re.search(r'^#define '+name+r' .*',settings,re.M)[0]+'\n'
for name in ('app_settings_book_paragraph_value','app_settings_book_paragraph_level'):
    unit+=function(name,root/'main/settings.c')+'\n'
unit+=re.search(r'static struct \{\s+bool active, cancelled, repeated;[\s\S]*?\} s_indent_hold;',text)[0]+'\n'
unit+=r'''
static int saved,reflows,draws,pushes,invalidated;
static int app_settings_book_indent_adjust(void){return saved;}
static int app_settings_book_indent(void){return 2;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void ui_text_set_system_scale(bool on){(void)on;}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int color){(void)fb;(void)radius;(void)color;assert(r.y==1044&&r.height==72&&r.x+r.width<=638);++draws;}
static void ui_draw_control_frame(uint8_t *fb,EpdRect r,int radius,int color){(void)fb;(void)r;(void)radius;(void)color;}
static void ui_text_box(uint8_t *fb,EpdRect r,int px,const char *s,int align,bool invert){(void)fb;(void)px;(void)s;(void)align;(void)invert;assert(r.y+r.height<1132);}
static const int E0470_FOLLOW_WAVEFORM=1;
static int update_display_area_diff_with(void *hl,const int *wave,int mode,EpdRect r){(void)hl;assert(wave==&E0470_FOLLOW_WAVEFORM&&mode==MODE_DU&&r.y==1044&&r.height==72);++pushes;return 0;}
static void guard_draw_result(void *hl,int result){(void)hl;assert(result==0);}
static app_redraw_t apply_reader_indent(app_ctx_t *ctx,int em,int value){(void)ctx;assert(em>=0&&em<=3&&value>=-100&&value<=100);if(value==saved)return APP_REDRAW_NONE;saved=value;++reflows;return APP_REDRAW_PAGE;}
static EpdRect reader_slider_rect(int n){(void)n;return(EpdRect){0};}
static int reader_margin_input(EpdRect r,int x,int margin,int px,int tracking){(void)r;(void)x;(void)px;(void)tracking;return margin;}
static int app_settings_book_tracking(void){return 4;}
static int app_settings_book_line_spacing(void){return 120;}
static int app_settings_book_paragraph_spacing(void){return 0;}
static int slider_index(EpdRect r,int x,int n){(void)r;(void)x;(void)n;return 0;}
static app_redraw_t apply_reader_layout(app_ctx_t *ctx,int px,int margin,int line,int para){(void)ctx;(void)px;(void)margin;(void)line;(void)para;assert(false);return APP_REDRAW_NONE;}
static app_redraw_t apply_reader_typography(app_ctx_t *ctx,int value){(void)ctx;(void)value;assert(false);return APP_REDRAW_NONE;}
static void invalidate_prep(void){++invalidated;}
'''
header=(root/'main/ui/ui_gesture.h').read_text()
unit+=header[header.index('typedef enum {'):header.index('/// 清除状态')]
for name in ('ui_gesture_reset','emit','ui_gesture_feed'):
    unit+=function(name,root/'main/ui/ui_gesture.c')+'\n'
for name in ('indent_adjust_rect','reader_layout_back_rect','indent_adjust_hit','reader_indent_hold_cancel','draw_indent_adjustments','reader_indent_feedback','reader_indent_present','reader_indent_finish','reader_indent_gesture','reader_indent_hold_tick'):
    unit+=function(name,book)+'\n'
action=function('reader_panel_action',book)
a=action.index('    if (s_reader_panel == READER_PANEL_LAYOUT_SETTINGS) {')
b=action.index('    if (s_reader_panel == READER_PANEL_RULE_SETTINGS)',a)
unit+='static app_redraw_t layout_tap(app_ctx_t *ctx,uint16_t x,uint16_t y){\n'+action[a:b]+'return APP_REDRAW_NONE;\n}\n'
unit+=r'''
static ui_gesture_t gesture;
static cst836u_touch_t touch;
static app_ctx_t ctx;
static uint8_t fb[1];
static void reset(int value){
 reader_indent_hold_cancel();ui_gesture_reset(&gesture);s_view=READING;s_reader_panel=READER_PANEL_LAYOUT_SETTINGS;s_clear_confirm=false;
 saved=value;reflows=draws=pushes=invalidated=0;touch=(cst836u_touch_t){0};ctx=(app_ctx_t){.fb=fb,.touch=&touch};
}
static app_redraw_t feed(int64_t at,int x,int y,int contacts,bool press,bool release,bool consumed){
 touch=(cst836u_touch_t){.touched=contacts>0,.count=contacts,.x=x,.y=y};
 ctx.now_ms=at;ctx.pressed=press;ctx.released=release;ctx.consumed=consumed;
 ui_gesture_event_t ev;app_redraw_t redraw=APP_REDRAW_NONE;
 if(ui_gesture_feed(&gesture,&ctx,&ev)){
  bool handled=false;redraw=reader_indent_gesture(&ctx,&ev,&handled);
  if(!handled&&ev.type==UI_GESTURE_TAP)redraw=layout_tap(&ctx,ev.x0,ev.y0);
 }
 if(redraw==APP_REDRAW_AREA)assert(reader_indent_present(&ctx,redraw));
 else if(redraw==APP_REDRAW_PAGE)assert(!reader_indent_present(&ctx,redraw));
 ctx.consumed=consumed||redraw!=APP_REDRAW_NONE;
 app_redraw_t tick=reader_indent_hold_tick(&ctx);
 if(tick==APP_REDRAW_AREA){assert(reader_indent_present(&ctx,tick));redraw=tick;}
 return redraw;
}
int main(void){
 reset(0);EpdRect back=reader_layout_back_rect();
 for(int i=0;i<3;++i){EpdRect r=indent_adjust_rect(i);for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x)assert(indent_adjust_hit(x,y)==i);}
 for(int y=1034;y<1216;++y)for(int x=0;x<684;++x){
  s_reader_panel=READER_PANEL_LAYOUT_SETTINGS;saved=0;reflows=0;
  layout_tap(&ctx,x,y);
  assert((s_reader_panel==READER_PANEL_FONT_SETTINGS)==ui_rect_hit(back,x,y));
  if(indent_adjust_hit(x,y)>=0)assert(!ui_rect_hit(back,x,y));
 }
 // 下沿容错及空白不返回；真实短按仅提交一次。/ Lower-edge tolerance and inert gaps; real taps commit once.
 reset(0);feed(0,580,1120,1,true,false,false);assert(saved==0&&reflows==0);
 feed(100,580,1121,0,false,true,false);assert(saved==1&&reflows==1&&s_reader_panel==READER_PANEL_LAYOUT_SETTINGS);
 reset(0);feed(0,580,1129,1,true,false,false);feed(100,580,1129,0,false,true,false);assert(saved==0&&reflows==0&&s_reader_panel==READER_PANEL_LAYOUT_SETTINGS);
 reset(0);feed(0,580,1160,1,true,false,false);feed(100,580,1160,0,false,true,false);assert(s_reader_panel==READER_PANEL_FONT_SETTINGS);
 // 长按识别后的抬手是CANCEL；不能丢提交或再补一次短按。/ Release after LONG_PRESS is CANCEL, with one commit and no extra tap.
 reset(0);feed(0,580,1080,1,true,false,false);feed(499,580,1080,1,false,false,false);assert(s_indent_hold.value==0);
 feed(500,580,1080,1,false,false,false);assert(s_indent_hold.value==1&&saved==0&&reflows==0);
 feed(619,580,1080,1,false,false,false);assert(s_indent_hold.value==1);
 feed(620,580,1080,1,false,false,false);feed(740,580,1080,1,false,false,false);assert(s_indent_hold.value==3&&reflows==0);
 feed(780,580,1080,0,false,true,false);assert(saved==3&&reflows==1&&!s_indent_hold.active);
 feed(2000,580,1080,0,false,false,false);assert(saved==3&&reflows==1);
 reset(0);feed(0,250,1080,1,true,false,false);feed(500,250,1080,1,false,false,false);feed(5000,250,1080,1,false,false,false);
 assert(s_indent_hold.value==-2&&saved==0);feed(5010,250,1080,0,false,true,false);assert(saved==-2&&reflows==1);
 for(int value=-100;value<=100;value+=200){int x=value<0?250:580;reset(value);feed(0,x,1080,1,true,false,false);for(int t=500;t<2000;t+=120)feed(t,x,1080,1,false,false,false);feed(2100,x,1080,0,false,true,false);assert(saved==value&&reflows==0);}
 reset(18);feed(0,400,1080,1,true,false,false);feed(100,400,1080,0,false,true,false);assert(saved==0&&reflows==1);
 // 滑到返回键也不能转交同一触摸；多指/读错/时间倒退/页面切换均丢弃未提交预览。
 // A slide onto Back never transfers the touch; multitouch/errors/time regression/navigation discard the uncommitted preview.
 reset(0);feed(0,580,1080,1,true,false,false);feed(500,580,1080,1,false,false,false);feed(620,580,1150,1,false,false,false);feed(700,580,1150,0,false,true,false);assert(saved==0&&reflows==0&&s_reader_panel==READER_PANEL_LAYOUT_SETTINGS);
 reset(0);feed(0,580,1080,1,true,false,false);feed(500,580,1080,1,false,false,false);feed(620,580,1080,2,false,false,false);assert(!s_indent_hold.active&&saved==0&&reflows==0);
 reset(0);feed(0,580,1080,1,true,false,false);feed(500,580,1080,1,false,false,false);ctx.touch=NULL;ctx.now_ms=620;assert(reader_indent_hold_tick(&ctx)==APP_REDRAW_AREA&&s_indent_hold.cancelled&&saved==0);ctx.touch=&touch;
 reset(0);feed(1000,580,1080,1,true,false,false);feed(1500,580,1080,1,false,false,false);feed(1200,580,1080,1,false,false,false);assert(saved==0&&reflows==0&&!s_indent_hold.active);
 reset(0);feed(0,580,1080,1,true,false,false);feed(500,580,1080,1,false,false,false);feed(620,580,1080,1,false,false,true);assert(saved==0&&reflows==0&&!s_indent_hold.active);
 reset(0);feed(0,580,1080,1,true,false,false);s_reader_panel=READER_PANEL_FONT_SETTINGS;assert(reader_indent_hold_tick(&ctx)==APP_REDRAW_NONE&&!s_indent_hold.active&&saved==0);
 reset(0);feed(0,580,1080,1,true,false,false);ctx.request_app=&ctx;assert(reader_indent_hold_tick(&ctx)==APP_REDRAW_NONE&&!s_indent_hold.active);
 reset(0);reader_indent_feedback(&ctx);assert(!reader_indent_present(&ctx,APP_REDRAW_PAGE)&&pushes==0&&!s_indent_feedback_pending);
 reader_indent_feedback(&ctx);assert(reader_indent_present(&ctx,APP_REDRAW_AREA)&&pushes==1);assert(!reader_indent_present(&ctx,APP_REDRAW_AREA)&&pushes==1);
 puts("PASS: real gesture hold/release, 124k footer hits, 1px repeat/clamp, one reflow on release, inert gaps, Back isolation, interruption cancellation and one-shot local FOLLOW DU");
}
'''
for name in ('on_enter','book_on_exit','book_on_media_lost','on_key','on_key_long','before_lock','set_reader_view'):
    assert 'reader_indent_hold_cancel();' in function(name,book),name
assert 'reader_indent_gesture(ctx,ev,&indent_handled)' in function('gesture_event',book)
assert 'reader_indent_hold_tick(ctx)' in function('on_tick',book)
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)/'test.c';p.write_text(unit);exe=p.with_suffix('')
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'main'),str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

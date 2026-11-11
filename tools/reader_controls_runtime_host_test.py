#!/usr/bin/env python3
"""中文：提取固件实际计时与底部手势分派，验证跨章、暂停和三个可改绑区域。
English: Exercise the real timer and bottom-swipe dispatcher across chapters, pauses and configurable zones.
"""
from pathlib import Path
import subprocess
import tempfile
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
try:
    from home_cover_cache_host_test import function
except ImportError:
    sys.path.insert(0,'/Users/weihongli/Documents/New project/read-pico-reader/build/multiboard-20261018/system-ripple-nav-fix-20261020/source/tools')
    from home_cover_cache_host_test import function
source=ROOT/'main/apps/book_reader_controls_runtime.inc'
unit=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reader_controls.h"
enum {SHELF,BULK,MANAGE,TOC,READING,READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_REFRESH_SETTINGS};
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_AREA}app_redraw_t;
typedef struct {bool touched;}touch_t;
typedef struct {int64_t now_ms;bool consumed,request_app,request_menu,request_return,user_activity;touch_t*touch;}app_ctx_t;
enum {UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D};
typedef struct {unsigned type,x0,y0;}ui_gesture_event_t;
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,s_footnote_note=-1;
static const char*s_text="reader";
static bool s_scan_pending,s_clear_confirm,s_rc_double_pending,s_rc_auto_turning,zoom,fail_turn;
static int64_t s_rc_double_until;
static int s_pressed_control,s_reader_slider,last_action,turns,notices;
static size_t s_page,s_chapter;
static struct {bool pending;}s_reader_tap;
static reader_auto_clock_t s_rc_auto;
static bool s_rc_auto_waiting;
static reader_controls_config_t config;
const reader_controls_config_t*app_settings_reader_controls(void){return &config;}
static bool rc_zoom_active(void){return zoom;}
static size_t book_layout_page_count(void){return 2;}
static size_t book_chapter_count(void){return 2;}
static app_redraw_t rc_execute(app_ctx_t*c,unsigned action){(void)c;last_action=action;return action?APP_REDRAW_PAGE:APP_REDRAW_NONE;}
static void rc_notice(app_ctx_t*c,const char*t){(void)c;(void)t;++notices;}
static app_redraw_t turn_page(app_ctx_t*c,int direction){
 (void)c;assert(s_rc_auto_turning&&direction==1);++turns;
 if(!fail_turn){if(s_page+1<2)++s_page;else if(s_chapter+1<2){++s_chapter;s_page=0;}}
 return APP_REDRAW_AREA;
}
'''
for name in ['rc_pause_auto','rc_stop_auto','rc_tick','rc_bottom_swipe']:
    unit+=function(name,source)+'\n'
actual=function('rc_execute',source)
a=actual.index('    case RC_AUTOTURN:');b=actual.index('    case RC_EFFECT:',a)
unit+='static app_redraw_t actual_auto_switch(app_ctx_t*ctx){switch(RC_AUTOTURN){\n'+actual[a:b]+'default:return APP_REDRAW_NONE;}}\n'
unit+=r'''
int main(void){
 config=reader_controls_defaults(false);config.seconds_le[0]=5;reader_controls_seal(&config);
 app_ctx_t ctx={.now_ms=1000};assert(actual_auto_switch(&ctx)==APP_REDRAW_PAGE&&s_rc_auto.running&&s_rc_auto.due_ms==6000);
 ctx.now_ms=5999;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&turns==0);
 ctx.now_ms=6000;assert(rc_tick(&ctx)==APP_REDRAW_AREA&&turns==1&&s_page==1&&ctx.user_activity);
 ctx.now_ms=11000;assert(rc_tick(&ctx)==APP_REDRAW_AREA&&s_chapter==1&&s_page==0&&turns==2);
 ctx.now_ms=16000;assert(rc_tick(&ctx)==APP_REDRAW_AREA&&s_page==1&&turns==3);
 ctx.now_ms=16001;assert(rc_tick(&ctx)==APP_REDRAW_PAGE&&!s_rc_auto.running&&turns==3);
 s_page=s_chapter=0;ctx.now_ms=50000;actual_auto_switch(&ctx);assert(s_rc_auto.running);actual_auto_switch(&ctx);assert(!s_rc_auto.running);
 ctx.now_ms=90000;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&turns==3);
 actual_auto_switch(&ctx);s_reader_panel=READER_PANEL_REFRESH_SETTINGS;ctx.now_ms=95000;
 assert(rc_tick(&ctx)==APP_REDRAW_NONE&&s_rc_auto.running&&s_rc_auto_waiting);s_reader_panel=READER_PANEL_NONE;
 ctx.now_ms=200000;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&s_rc_auto.running&&s_rc_auto.due_ms==205000&&turns==3);
 s_footnote_note=1;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&s_rc_auto.running);s_footnote_note=-1;
 zoom=true;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&s_rc_auto.running);zoom=false;
 ctx.consumed=true;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&s_rc_auto.running);ctx.consumed=false;
 s_view=TOC;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&s_rc_auto.running);s_view=READING;
 ctx.now_ms=300000;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&s_rc_auto.running&&s_rc_auto.due_ms==305000);
 // Fullscreen/manual callers use this real defer hook, preserving the enabled switch.
 rc_pause_auto();assert(s_rc_auto.running&&s_rc_auto_waiting);ctx.now_ms=400000;
 assert(rc_tick(&ctx)==APP_REDRAW_NONE&&s_rc_auto.due_ms==405000);
 ctx.now_ms=405000;touch_t touch={true};ctx.touch=&touch;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&turns==3);
 ctx.touch=NULL;fail_turn=true;assert(rc_tick(&ctx)==APP_REDRAW_AREA&&!s_rc_auto.running&&turns==4);fail_turn=false;
 config.seconds_le[0]=1;s_page=s_chapter=0;actual_auto_switch(&ctx);
 assert(s_rc_auto.running&&s_rc_auto.due_ms==406000);
 ctx.now_ms=405999;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&turns==4);
 ctx.now_ms=406000;assert(rc_tick(&ctx)==APP_REDRAW_AREA&&s_page==1&&turns==5);
 assert(rc_tick(&ctx)==APP_REDRAW_NONE&&turns==5);
 ctx.now_ms=407000;assert(rc_tick(&ctx)==APP_REDRAW_AREA&&s_chapter==1&&s_page==0&&turns==6);
 actual_auto_switch(&ctx);assert(!s_rc_auto.running);assert(rc_tick(&ctx)==APP_REDRAW_NONE&&turns==6);
 s_page=s_chapter=0;actual_auto_switch(&ctx);s_text=NULL;assert(rc_tick(&ctx)==APP_REDRAW_NONE&&!s_rc_auto.running);s_text="reader";
 s_rc_double_pending=true;s_rc_double_until=500001;ctx.now_ms=500001;
 assert(rc_tick(&ctx)==APP_REDRAW_PAGE&&last_action==RC_FULLSCREEN&&!s_rc_double_pending);
 unsigned x[]={0,227,228,455,456,683},expected[]={RC_BOOKMARK,RC_BOOKMARK,RC_TOOLS,RC_TOOLS,RC_AUTOTURN,RC_AUTOTURN};
 for(unsigned i=0;i<6;++i){bool handled=false;ui_gesture_event_t ev={UI_GESTURE_SWIPE_U,x[i],1096};s_pressed_control=203;s_reader_slider=1;
  assert(rc_bottom_swipe(&ctx,&ev,&handled)==APP_REDRAW_PAGE&&handled&&last_action==(int)expected[i]);assert(s_pressed_control==-1&&s_reader_slider==-1);}
 config.actions[RC_BOTTOM_CENTER]=RC_EFFECT;bool handled=false;ui_gesture_event_t ev={UI_GESTURE_SWIPE_U,340,1199};
 assert(rc_bottom_swipe(&ctx,&ev,&handled)==APP_REDRAW_PAGE&&handled&&last_action==RC_EFFECT);
 config.actions[RC_BOTTOM_CENTER]=RC_NONE;assert(rc_bottom_swipe(&ctx,&ev,&handled)==APP_REDRAW_PAGE&&last_action==RC_NONE);
 ev.x0=684;assert(rc_bottom_swipe(&ctx,&ev,&handled)==APP_REDRAW_NONE&&!handled);ev.x0=340;ev.y0=1095;
 assert(rc_bottom_swipe(&ctx,&ev,&handled)==APP_REDRAW_NONE&&!handled);ev.y0=1100;ev.type=UI_GESTURE_SWIPE_D;
 assert(rc_bottom_swipe(&ctx,&ev,&handled)==APP_REDRAW_NONE&&!handled);ev.type=UI_GESTURE_SWIPE_U;s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
 assert(rc_bottom_swipe(&ctx,&ev,&handled)==APP_REDRAW_NONE&&!handled);
 puts("PASS: actual auto switch and tick at a 1-second minimum, explicit off, fullscreen/manual/modal retain on, no catchup, chapters/EOF/errors/media; all three bottom swipe zones and center rebind work");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';c.write_text(unit);binary=Path(d)/'test'
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'main'),str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

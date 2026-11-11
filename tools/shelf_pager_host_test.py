#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实手势识别到书架四向分页的完整按下/抬手与取消。
English: Real gesture recognition through shelf pager press/release and cancellation.
"""
from pathlib import Path
import re,subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
book=R/'main/apps/app_book.c';kit=R/'main/ui/ui_kit.c'
header=(R/'main/ui/ui_kit.h').read_text()
gesture=(R/'main/ui/ui_gesture.h').read_text()
gesture='\n'.join(line for line in gesture.splitlines() if not line.startswith(('#include','#pragma')))
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint16_t x,y;unsigned count;bool touched;} cst836u_touch_t;
typedef struct {int leaf;int64_t now_ms;uint8_t *fb;cst836u_touch_t *touch;bool pressed,released,consumed;void *request_app;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {SHELF,READING,BULK,TOC,MODE_GL16=1,UI_LOCK_WIDTH=684,UI_GRAY_WHITE=255};
static int s_view=SHELF,s_pressed_control=-1,s_visible_count=27,rows=9,s_mode;
static bool s_category_menu,s_sort_menu,s_clear_confirm,s_batch_confirm,s_scan_pending;
static bool s_shelf_key_mode,s_shelf_key_paint_pending,s_shelf_page_pending;
static int s_shelf_key_pressed;
static EpdRect s_area;
static unsigned haptics;
int metalio_haptic_click(void){++haptics;return 0;}
static unsigned prepares,renders,paints;static int remembered;
static int shelf_rows(void){return rows;}
static int book_navigation_count(void){return 0;}
static int book_toc_pages(int count){(void)count;return 1;}
static void shelf_category_remember(app_ctx_t *ctx){remembered=ctx->leaf;}
static void prepare_covers(app_ctx_t *ctx){(void)ctx;++prepares;}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++renders;}
static void ui_fill_rect_fast(EpdRect rect,int color,uint8_t *fb){(void)fb;assert(rect.y==1006&&rect.height==50&&color==255);}
static void ui_pager_draw(uint8_t *fb,const char *label,int page,int pages,bool key_mode,int pressed){
 (void)fb;(void)key_mode;assert(!label[0]&&page>=0&&page<pages&&pressed>=-1&&pressed<4);++paints;
}
'''+gesture+'\n'
u+=re.search(r'enum \{ UI_PAGER_NONE=[^}]+\};',header)[0]+'\n'
for macro in ['UI_TOUCH_SLOP_PX','UI_SWIPE_MIN_PX','UI_LONG_PRESS_MS']:
 u+=re.search(r'^#define '+macro+r' .*',header,re.M)[0]+'\n'
for name in ['ui_rect_hit','ui_pager_area_at','ui_pager_rect_at','ui_pager_hit_at','ui_pager_area','ui_pager_rect','ui_pager_hit','ui_pager_target']:
 u+=function(name,kit)+'\n'

u+=r'''
#define EPD_DRAW_ALIGN_CENTER 1
static EpdRect shelf_page_label_rect(int page,int pages){(void)page;(void)pages;return(EpdRect){280,1006,60,50};}
static void ui_text_fixed_vc(uint8_t*f,int x,int y,int px,const char*label,int align,bool inverse){(void)f;(void)align;(void)inverse;assert(x>204&&x<480&&y==1031&&px<=32&&strchr(label,'/'));}
static void draw_shelf_search_button(uint8_t*f,int page,int pages){(void)f;(void)page;(void)pages;}
static EpdRect shelf_pager_group_rect(int page,int pages){(void)page;(void)pages;return(EpdRect){240,1004,204,54};}
static int ui_text_fixed_width_px(int px,const char*text){(void)text;return px*2;}
static void epd_fill_circle(int x,int y,int radius,int color,uint8_t*fb){(void)x;(void)fb;assert(y==1031&&radius==2&&color==0x80);}
'''
for name in ['leaves','draw_shelf_pager','shelf_key_paint','shelf_turn_page','shelf_pager_release_hit','shelf_pager_event']:
 u+=function(name,book)+'\n'
for name in ['ui_gesture_reset','emit','ui_gesture_feed']:
 u+=function(name,R/'main/ui/ui_gesture.c')+'\n'
# The handler must own shelf pager contacts before generic reader/control routing.
route=function('gesture_event',book)
assert route.index('shelf_pager_event(')<route.index('reader_tools_swipe(')
u+=r'''
static ui_gesture_t gesture;
static cst836u_touch_t touch;
static app_ctx_t ctx;
static unsigned dispatches;
static app_redraw_t sample(int x,int y,bool down,bool first,bool release,bool consumed,unsigned points){
 touch=(cst836u_touch_t){x,y,points,down};ctx.touch=&touch;ctx.now_ms+=100;
 ctx.pressed=first;ctx.released=release;ctx.consumed=consumed;
 ui_gesture_event_t e;
 if(!ui_gesture_feed(&gesture,&ctx,&e))return APP_REDRAW_NONE;
 app_redraw_t redraw=APP_REDRAW_NONE;assert(shelf_pager_event(&ctx,&e,&redraw));++dispatches;return redraw;
}
static void reset(int page){
 ctx=(app_ctx_t){.leaf=page};ui_gesture_reset(&gesture);s_view=SHELF;
 s_category_menu=s_clear_confirm=s_batch_confirm=s_scan_pending=false;
 s_pressed_control=-1;s_shelf_key_paint_pending=s_shelf_page_pending=false;
 prepares=renders=paints=dispatches=0;remembered=-1;
}
int main(void){
 for(rows=8;rows<=9;++rows){s_visible_count=rows*3;
  for(int a=0;a<4;++a){EpdRect r=ui_pager_rect(a);int x=r.x+r.width/2,y=r.y+r.height/2;
   reset(1);assert(sample(x,y,true,true,false,false,1)==APP_REDRAW_AREA);
   assert(ctx.leaf==1&&!prepares&&s_pressed_control==116+a&&s_shelf_key_paint_pending);
   assert(sample(x,y,false,false,true,false,0)==APP_REDRAW_AREA);
   assert(ctx.leaf==(a<2?0:2)&&remembered==ctx.leaf&&prepares==1&&renders==1);
   assert(!s_shelf_key_paint_pending&&s_shelf_page_pending&&s_area.y==176&&s_area.height==912);
   // A small release drift beyond the 50px painted target still belongs to its pressed arrow.
   // 轻点在箭头边缘小幅漂移不再被通用控件的严格终点命中吞掉。
   reset(1);assert(ui_pager_hit(x,r.y+r.height+3)<0);
   sample(x,r.y+r.height-3,true,true,false,false,1);
   sample(x,r.y+r.height+3,false,false,true,false,0);
   assert(ctx.leaf==(a<2?0:2)&&prepares==1);
   reset(1);sample(x,y,true,true,false,false,1);
   sample(x,y+40,true,false,false,false,1);sample(x,y+40,false,false,true,false,0);
   assert(ctx.leaf==1&&!prepares&&s_pressed_control<0);
   reset(1);sample(x,y,true,true,false,false,1);
   ctx.now_ms+=500;sample(x,y,true,false,false,false,1);sample(x,y,false,false,true,false,0);
   assert(ctx.leaf==1&&!prepares&&s_pressed_control<0);
   reset(1);sample(x,y,true,true,false,false,1);sample(x,y,true,false,false,false,2);
   assert(ctx.leaf==1&&!prepares&&s_pressed_control<0);
   reset(1);sample(x,y,true,true,false,false,1);sample(x,y,false,false,true,true,0);
   assert(ctx.leaf==1&&!prepares&&s_pressed_control<0);
   reset(a<2?0:2);sample(x,y,true,true,false,false,1);sample(x,y,false,false,true,false,0);
   assert(ctx.leaf==(a<2?0:2)&&!prepares);
   reset(1);s_scan_pending=true;sample(x,y,true,true,false,false,1);sample(x,y,false,false,true,false,0);
   assert(ctx.leaf==1&&!prepares&&!paints);
  }
 }
 reset(1);EpdRect first=ui_pager_rect(UI_PAGER_FIRST),previous=ui_pager_rect(UI_PAGER_PREVIOUS);
 sample(first.x+first.width-2,1030,true,true,false,false,1);
 sample(previous.x+2,1030,false,false,true,false,0);assert(ctx.leaf==1&&!prepares);
 reset(1);app_redraw_t redraw;ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=520,.y0=1030,.x=520,.y=1030};
 s_view=READING;assert(!shelf_pager_event(&ctx,&e,&redraw));s_view=BULK;assert(!shelf_pager_event(&ctx,&e,&redraw));
 s_view=SHELF;s_category_menu=true;assert(!shelf_pager_event(&ctx,&e,&redraw));s_category_menu=false;
 e.x0=342;assert(!shelf_pager_event(&ctx,&e,&redraw));e.x0=520;e.y0=1080;assert(!shelf_pager_event(&ctx,&e,&redraw));
 puts("PASS: actual recognizer + shelf pager: four arrows/list/grid, edge drift, press-only feedback, stale-feedback clear, scan/modal/end guards, drag/hold/multitouch/consumed cancellation");
}
'''
with tempfile.TemporaryDirectory(prefix='shelf-pager-') as d:
 p=Path(d)/'test.c';p.write_text(u);exe=p.with_suffix('')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

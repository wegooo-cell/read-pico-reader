#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际阅读点按派发的全屏、区域翻页、图片双击及取消回归。
English: Exercise actual reader dispatch for fullscreen, regional turns, image doubles and cancellation.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
book=R/'main/apps/app_book.c'
s=book.read_text()
a=s.index('\n',s.index('if(header_handled)return header_redraw;'))+1
b=s.index('\n    if (s_view == SHELF) {',a)
block=s[a:b]
a=s.index('        int tap = book_reader_tap_tick(')
b=s.index('\n    }\n    track_ticket_stats(ctx);',a)
tick=s[a:b]
header=(R/'main/book/book_reader_input.h').read_text()
header='\n'.join(x for x in header.splitlines() if not x.startswith(('#include','#pragma')))
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef int app_redraw_t;
typedef struct{bool touched;}touch_t;
typedef struct{int64_t now_ms;touch_t*touch;}app_ctx_t;
enum{APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL,READING=1,READER_PANEL_NONE=0,READER_PANEL_TOOLS=1,READER_PANEL_MODAL=2,READER_IMAGE_ZOOM_MIN=1000,UI_READER_TOOLS_TOP=1096};
'''+header+r'''
static int s_view=READING,s_reader_panel,s_image_zoom=1000,s_zoom_slot,s_image_zoom_start_distance;
static size_t s_page=1,s_image_zoom_page=SIZE_MAX;
static bool s_zoom_active,s_toolbar,s_reader_fullscreen,s_clear_confirm,single_image,image_present,vertical,rtl,s_image_pan_active,s_image_zoom_bw;
static uint8_t*s_zoom_bits;static size_t s_zoom_bits_bytes;
static int fullscreen_calls,zoom_calls,inline_calls,panel_calls,turn_calls,last_turn;
static book_reader_tap_t s_reader_tap;
static int s_footnote_note=-1,note_candidate=-1;static EpdRect s_footnote_marker;
static int reader_footnote_hit(int x,int y,EpdRect*r){(void)x;(void)y;(void)r;return note_candidate;}
static EpdRect body_rect(void){return(EpdRect){0,24,684,1178};}
static EpdRect reader_area(void){return s_reader_fullscreen?(EpdRect){0,0,684,1204}:(EpdRect){0,176,684,872};}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static EpdRect ui_rect_union(EpdRect a,EpdRect b){(void)b;return a;}
static EpdRect book_layout_image_rect(void){return body_rect();}
static bool reader_page_image_hit(int x,int y){return image_present&&ui_rect_hit((EpdRect){20,250,644,700},x,y);}
static bool reader_single_image_page(void){return single_image;}
static bool reader_zoom_mode(void){return image_present&&(single_image||s_zoom_active);}
static int reader_image_slots(size_t p){(void)p;return image_present?1:0;}
static bool app_settings_reader_vertical_turn(void){return vertical;}
static bool app_settings_book_vertical(void){return rtl;}
static app_redraw_t rc_body_tap(app_ctx_t*c,int x,int y,bool image,bool*handled){(void)c;(void)x;(void)y;(void)image;*handled=false;return APP_REDRAW_NONE;}
'''+function('reader_zoom_reset',book)+r'''
static int toggle_reader_fullscreen(app_ctx_t*c){(void)c;++fullscreen_calls;s_reader_fullscreen=!s_reader_fullscreen;s_toolbar=!s_reader_fullscreen;s_reader_panel=s_toolbar?READER_PANEL_TOOLS:READER_PANEL_NONE;s_reader_tap.pending=false;return APP_REDRAW_FULL;}
static int reader_panel_action(app_ctx_t*c,int x,int y){(void)c;(void)x;(void)y;++panel_calls;return APP_REDRAW_PAGE;}
static int zoom_jump(app_ctx_t*c,int scale,int x,int y){(void)c;(void)x;(void)y;++zoom_calls;s_image_zoom=scale;s_image_zoom_page=s_page;return APP_REDRAW_PAGE;}
static int inline_zoom_toggle(app_ctx_t*c,int x,int y){(void)c;(void)x;(void)y;++inline_calls;s_zoom_active=!s_zoom_active;s_image_zoom=s_zoom_active?2000:1000;s_image_zoom_page=s_zoom_active?s_page:SIZE_MAX;return APP_REDRAW_PAGE;}
static int turn_page(app_ctx_t*c,int target){(void)c;assert(target==-1||target==1);++turn_calls;last_turn=target;s_reader_tap.pending=false;return APP_REDRAW_PAGE;}
static int dispatch(app_ctx_t*ctx,uint16_t x,uint16_t y){if(s_view==READING){
'''+block+r'''
return APP_REDRAW_NONE;}
static int tick(app_ctx_t*ctx){
'''+tick+r'''
return APP_REDRAW_NONE;}
static void reset(bool full,int kind,bool v,bool r){
 reader_zoom_reset();s_view=READING;s_clear_confirm=false;s_footnote_note=note_candidate=-1;
 s_reader_fullscreen=full;s_toolbar=!full;s_reader_panel=s_toolbar?READER_PANEL_TOOLS:READER_PANEL_NONE;
 image_present=kind!=0;single_image=kind==2;vertical=v;rtl=r;
 fullscreen_calls=zoom_calls=inline_calls=panel_calls=turn_calls=last_turn=0;
}
static bool zoomed(void){return s_zoom_active||s_image_zoom>READER_IMAGE_ZOOM_MIN;}
int main(void){
 app_ctx_t c={.now_ms=1000};
 for(int v=0;v<2;v++)for(int r=0;r<2;r++){
  reset(false,0,v,r);c.now_ms=1000;
  assert(dispatch(&c,342,500)==APP_REDRAW_FULL&&s_reader_fullscreen&&fullscreen_calls==1&&!turn_calls);
  assert(dispatch(&c,342,500)==APP_REDRAW_PAGE&&fullscreen_calls==1&&last_turn==book_reader_tap_direction(body_rect(),342,500,v,r));
  const int xs[]={300,400,342,342},ys[]={500,500,300,800};
  for(int i=0;i<4;i++){
   reset(true,0,v,r);assert(dispatch(&c,xs[i],ys[i])==APP_REDRAW_PAGE&&turn_calls==1&&!fullscreen_calls);
   assert(last_turn==book_reader_tap_direction(body_rect(),xs[i],ys[i],v,r));
  }
  for(int full=0;full<2;full++)for(int kind=1;kind<=2;kind++){
   reset(full,kind,v,r);c.now_ms=2000;assert(dispatch(&c,50,500)==APP_REDRAW_NONE&&s_reader_tap.pending);
   c.now_ms=2340;assert(tick(&c)==APP_REDRAW_NONE&&!turn_calls);
   c.now_ms=2341;assert(tick(&c)==APP_REDRAW_PAGE&&turn_calls==1&&!fullscreen_calls);
   assert(last_turn==book_reader_tap_direction(body_rect(),50,500,v,r));
   reset(full,kind,v,r);c.now_ms=3000;assert(dispatch(&c,342,500)==APP_REDRAW_NONE&&s_reader_tap.pending);
   c.now_ms=3341;assert(tick(&c)==(full?APP_REDRAW_PAGE:APP_REDRAW_FULL));
   assert(full?(turn_calls==1&&!fullscreen_calls):(s_reader_fullscreen&&fullscreen_calls==1&&!turn_calls));
   reset(full,kind,v,r);c.now_ms=4000;dispatch(&c,342,500);c.now_ms=4100;dispatch(&c,342,500);
   assert(zoomed()&&!turn_calls&&!fullscreen_calls&&!s_reader_tap.pending);
   assert(kind==1?inline_calls==1:zoom_calls==1);
   c.now_ms=4500;dispatch(&c,342,500);c.now_ms=4841;tick(&c);assert(zoomed()&&!turn_calls&&!fullscreen_calls);
   c.now_ms=5000;dispatch(&c,342,500);c.now_ms=5100;dispatch(&c,342,500);
   assert(!zoomed()&&!turn_calls&&!fullscreen_calls);
   // 长时间按住后再点，过期的中间单击仍进入全屏，不误当双击缩放。
   // A delayed second tap delivers expired fullscreen intent without mistaking it for double zoom.
   reset(false,kind,v,r);c.now_ms=6000;dispatch(&c,342,500);c.now_ms=6500;dispatch(&c,342,500);
   assert(s_reader_fullscreen&&fullscreen_calls==1&&!zoomed()&&!turn_calls);
  }
 }
 reset(true,1,false,false);c.now_ms=7000;dispatch(&c,50,500);assert(s_reader_tap.pending);
 s_zoom_bits=malloc(8);s_zoom_bits_bytes=8;reader_zoom_reset();c.now_ms=7400;tick(&c);
 assert(!s_reader_tap.pending&&!s_zoom_bits&&!s_zoom_bits_bytes&&!turn_calls);
 reset(true,2,false,false);c.now_ms=8000;dispatch(&c,342,500);c.now_ms=7900;tick(&c);assert(!s_reader_tap.pending&&!turn_calls);
 reset(true,1,false,false);c.now_ms=9000;dispatch(&c,50,500);touch_t touching={true};c.touch=&touching;c.now_ms=9400;tick(&c);assert(s_reader_tap.pending&&!turn_calls);c.touch=NULL;tick(&c);assert(turn_calls==1);
 reset(true,1,false,false);note_candidate=0;c.now_ms=10000;dispatch(&c,342,500);assert(s_footnote_note==0&&!zoomed()&&!s_reader_tap.pending);dispatch(&c,342,500);assert(s_footnote_note==-1&&!turn_calls);
 reset(true,2,false,false);s_image_zoom=2000;s_image_zoom_page=s_page;s_toolbar=true;s_reader_panel=READER_PANEL_MODAL;
 dispatch(&c,342,500);assert(panel_calls==1&&!s_reader_tap.pending&&!zoom_calls);
 reset(false,1,false,false);s_zoom_active=true;s_image_zoom=2000;s_image_zoom_page=s_page;
 dispatch(&c,100,1150);assert(panel_calls==1&&!s_reader_tap.pending&&!inline_calls);
 reset(true,0,false,false);dispatch(&c,342,1215);assert(!turn_calls&&!fullscreen_calls);
 // Opaque chrome must win even when an image or footnote lies underneath.
 for(int panel=READER_PANEL_NONE;panel<=READER_PANEL_TOOLS;++panel){
  const int covered[]={40,114,175,1048,1075,1096,1150};
  for(unsigned i=0;i<sizeof(covered)/sizeof(covered[0]);++i){
   reset(false,1,false,false);s_reader_panel=panel;s_toolbar=panel!=READER_PANEL_NONE;
   note_candidate=0;dispatch(&c,342,covered[i]);
   assert(s_footnote_note==-1&&!turn_calls&&!fullscreen_calls&&!inline_calls&&!zoom_calls&&!s_reader_tap.pending);
   assert(panel_calls==(covered[i]>=UI_READER_TOOLS_TOP));
  }
 }
 // An existing note owns the first tap, even on chrome; the next setting tap must work.
 const int note_tap_y[]={40,114,175,500,1048,1075,1150};
 for(int panel=READER_PANEL_NONE;panel<=READER_PANEL_TOOLS;++panel)
 for(unsigned i=0;i<sizeof(note_tap_y)/sizeof(note_tap_y[0]);++i){
  reset(false,0,false,false);s_reader_panel=panel;s_toolbar=panel!=READER_PANEL_NONE;
  s_footnote_note=0;s_reader_tap.pending=true;
  assert(dispatch(&c,470,note_tap_y[i])==APP_REDRAW_PAGE);
  assert(s_footnote_note==-1&&!panel_calls&&!turn_calls&&!fullscreen_calls&&!s_reader_tap.pending);
  assert(s_reader_panel==panel);
  assert(dispatch(&c,470,1150)==APP_REDRAW_PAGE&&panel_calls==1);
  s_reader_panel=READER_PANEL_MODAL;s_toolbar=true;
  assert(dispatch(&c,340,700)==APP_REDRAW_PAGE&&panel_calls==2&&s_footnote_note==-1);
 }
 puts("PASS: actual reader action/tick/reset; standard centre entry, fullscreen horizontal/vertical/RTL turns, inline/whole-page singles and doubles, zoom exit, delayed fullscreen intent, modal/tools/notes, held touch, rollback and cancellation");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(u)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

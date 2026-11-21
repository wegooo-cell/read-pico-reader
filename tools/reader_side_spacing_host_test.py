#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：真实侧边纵滑在新字号、行距边界不回跳，取消不提交。
English: Actual side swipes retain expanded bounds and never commit cancelled changes."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
source=function('rc_gesture',R/'main/apps/book_reader_controls_runtime.inc')
start=source.index('    if(s_view!=READING||!s_text||s_clear_confirm||s_scan_pending||ctx->consumed||')
end=source.index('    if(ev->y0<1096',start)
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "book/book_font_size.h"
#define BOOK_PX_MIN BOOK_READER_PX_MIN
#define BOOK_PX_MAX BOOK_READER_PX_MAX
enum{READING,READER_PANEL_NONE,READER_PANEL_TOOLS,RC_SIDE_OFF,RC_SIDE_FONT,RC_SIDE_LINE,RC_SIDE_BRIGHTNESS,RC_SIDE_WARMTH};
enum{UI_GESTURE_PRESS,UI_GESTURE_MOVE,UI_GESTURE_TAP,UI_GESTURE_CANCEL};
enum{MODE_GL16};
typedef enum{APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE}app_redraw_t;
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{bool consumed;uint8_t *fb;}app_ctx_t;
typedef struct{int type,x0,y0,x,y;}ui_gesture_event_t;
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,s_px=48,s_margin=24,s_mode;
static const char *s_text="text";static bool s_clear_confirm,s_scan_pending;
static bool s_rc_side_active,s_rc_side_captured;static int s_rc_side_index,s_rc_side_action,s_rc_side_value,s_rc_side_start;
static EpdRect s_area;static struct{int sides[2];}controls;
static int line=130,commits,rendered,notices,paused;
static const void *app_settings_reader_controls_unused;
#define app_settings_reader_controls() (&controls)
static int app_settings_book_line_spacing(void){return line;}
static int app_settings_book_paragraph_spacing(void){return -100;}
static void rc_cancel_side(void){s_rc_side_active=s_rc_side_captured=false;}
static void rc_pause_auto(void){++paused;}
static const char *rc_side_name(int a){(void)a;return "value";}
static void rc_notice(app_ctx_t *c,const char *s){(void)c;(void)s;++notices;}
static void render(app_ctx_t *c,uint8_t *fb){(void)c;(void)fb;++rendered;}
#if KIIKO_BOARD_METALIO47
static int metalio_frontlight_position(bool warmth){(void)warmth;return 120;}
static void metalio_frontlight_preview_position(int b,int w){(void)b;(void)w;}
static void metalio_frontlight_commit(void){}
#endif
static app_redraw_t apply_reader_layout(app_ctx_t *c,int px,int margin,int l,int para){
 (void)c;assert(px>=20&&px<=120&&l>=100&&l<=300&&margin==24&&para==-100);s_px=px;line=l;++commits;return APP_REDRAW_PAGE;
}
static app_redraw_t side_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev,bool *handled){
 *handled=false;
'''+source[start:end]+r'''
 return APP_REDRAW_NONE;
}
int main(void){app_ctx_t ctx={0};
 for(int action=RC_SIDE_FONT;action<=RC_SIDE_LINE;++action)for(int side=0;side<2;++side){
  int minimum=action==RC_SIDE_FONT?20:100,maximum=action==RC_SIDE_FONT?120:300;
  controls.sides[side]=action;
  for(int start=minimum;start<=maximum;++start)for(int dy=-480;dy<=480;dy+=480){
   s_px=action==RC_SIDE_FONT?start:48;line=action==RC_SIDE_LINE?start:130;
   bool handled;int prior=commits,x=side?650:20;
   ui_gesture_event_t event={UI_GESTURE_PRESS,x,600,x,600};
   assert(side_gesture(&ctx,&event,&handled)==APP_REDRAW_NONE&&!handled&&s_rc_side_start==start);
   event.type=UI_GESTURE_MOVE;event.y=600+dy;
   if(dy==0)event.y=570;
   side_gesture(&ctx,&event,&handled);assert(handled&&commits==prior);
   int expected=start-(event.y-600)/24;if(expected<minimum)expected=minimum;if(expected>maximum)expected=maximum;
   assert(s_rc_side_value==expected);
   event.type=UI_GESTURE_TAP;assert(side_gesture(&ctx,&event,&handled)==APP_REDRAW_PAGE&&commits==prior+1);
   assert((action==RC_SIDE_FONT?s_px:line)==expected);
   event=(ui_gesture_event_t){UI_GESTURE_PRESS,x,600,x,600};side_gesture(&ctx,&event,&handled);
   event.type=UI_GESTURE_MOVE;event.y=100;side_gesture(&ctx,&event,&handled);
   event.type=UI_GESTURE_CANCEL;assert(side_gesture(&ctx,&event,&handled)==APP_REDRAW_PAGE&&commits==prior+1);
  }
 }
 assert(rendered&&notices&&paused);(void)app_settings_reader_controls_unused;
 puts("PASS: both side gestures across every font/line starting value; preview, signed paragraph preservation, clamp, release commit and cancellation; no snap to the retired 150% ceiling");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-side-spacing-') as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 for board in [0,1]:
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',f'-DKIIKO_BOARD_METALIO47={board}',
   '-I'+str(R/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

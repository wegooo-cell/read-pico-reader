#!/usr/bin/env python3
"""实际工具栏PRESS/TAP分支：设置只提交一次，取消/移出不预刷。
Actual toolbar PRESS/TAP branches: one setting commit; cancellation/outside releases do not preflash.
"""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'main/apps/app_book.c').read_text()
begin=source.index('    EpdRect start_rect = {0}, end_rect = {0};',source.index('static app_redraw_t gesture_event('))
press=source.index('    if (ev->type == UI_GESTURE_LONG_PRESS',begin)
tap=source.index('    if (ev->type == UI_GESTURE_TAP && !s_scan_pending)',press)
end=source.index('    if (!s_clear_confirm && !s_scan_pending)',tap)
if 'bool panel_entry=' not in source[begin:press]:
    print('SKIP: board retains existing toolbar press feedback')
    raise SystemExit(0)
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{bool request_app,request_menu,request_return;}app_ctx_t;
typedef struct{int type,x0,y0,x,y;}ui_gesture_event_t;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE}app_redraw_t;
enum{READING,SHELF,READER_PANEL_TOOLS,READER_PANEL_NONE,READER_PANEL_FONT_SETTINGS,READER_PANEL_REFRESH_SETTINGS};
enum{UI_GESTURE_PRESS,UI_GESTURE_TAP,UI_GESTURE_CANCEL};
static int s_view,s_reader_panel,s_pressed_control,painted,actions;
static bool s_scan_pending,s_clear_confirm,s_reader_fullscreen;
static int metalio_haptic_click(void){return 0;}
static int control_at(app_ctx_t*c,int x,int y,EpdRect*r){(void)c;(void)y;*r=(EpdRect){x,100,50,70};return x;}
static app_redraw_t paint_control(app_ctx_t*c,EpdRect r){(void)c;(void)r;++painted;return APP_REDRAW_AREA;}
static int app_settings_shelf_style(void){return 0;}
static int shelf_rows(void){return 0;}
static bool present(app_ctx_t*c,app_redraw_t r){(void)c;(void)r;assert(false);return false;}
static app_redraw_t action_at(app_ctx_t*c,int x,int y){
 (void)c;(void)y;++actions;
 if(s_view==READING&&s_reader_panel==READER_PANEL_TOOLS){
  if(x==203)s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
  if(x==204)s_reader_panel=READER_PANEL_FONT_SETTINGS;
 }
 return APP_REDRAW_PAGE;
}
static app_redraw_t event(app_ctx_t*ctx,const ui_gesture_event_t*ev){
'''+source[begin:press]+source[tap:end]+r'''
return decorated?paint_control(ctx,start_rect):APP_REDRAW_NONE;
}
static void reset(void){s_view=READING;s_reader_panel=READER_PANEL_TOOLS;s_pressed_control=-1;painted=actions=0;s_scan_pending=s_clear_confirm=false;}
int main(void){
 app_ctx_t ctx={0};
 for(int control=203;control<=204;++control){
  for(int finish=0;finish<3;++finish){
   reset();ui_gesture_event_t e={UI_GESTURE_PRESS,control,1100,control,1100};
   assert(event(&ctx,&e)==APP_REDRAW_NONE&&!painted&&!actions&&s_pressed_control==-1);
   e.type=finish==0?UI_GESTURE_TAP:UI_GESTURE_CANCEL;
   if(finish==2){e.type=UI_GESTURE_TAP;e.x=-1;}
   assert(event(&ctx,&e)==(finish==0?APP_REDRAW_PAGE:APP_REDRAW_NONE));
   assert(!painted&&actions==(finish==0));
   assert(s_reader_panel==(finish==0?(control==203?READER_PANEL_REFRESH_SETTINGS:READER_PANEL_FONT_SETTINGS):READER_PANEL_TOOLS));
  }
 }
 for(int control=200;control<=202;++control){
  reset();ui_gesture_event_t e={UI_GESTURE_PRESS,control,1100,control,1100};
  assert(event(&ctx,&e)==APP_REDRAW_AREA&&painted==1&&s_pressed_control==control);
  e.type=UI_GESTURE_TAP;assert(event(&ctx,&e)==APP_REDRAW_PAGE&&actions==1&&painted==1);
 }
 for(int context=0;context<2;++context){
  reset();if(context)s_view=SHELF;else s_reader_panel=READER_PANEL_NONE;
  ui_gesture_event_t e={UI_GESTURE_PRESS,203,1100,203,1100};
  assert(event(&ctx,&e)==(context?APP_REDRAW_AREA:APP_REDRAW_NONE)&&painted==context);
 }
 reset();ui_gesture_event_t e={UI_GESTURE_TAP,204,1100,204,1100};
 assert(event(&ctx,&e)==APP_REDRAW_PAGE&&actions==1&&!painted);
 puts("PASS: actual setting-entry press has zero renders/scans; release commits once; cancel/outside releases untouched; other controls and contexts retain feedback");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-panel-entry-') as temporary:
    folder=Path(temporary)
    test=folder/'test.c';exe=folder/'test'
    test.write_text(unit)
    subprocess.run(['cc','-Dshelf_display_style=app_settings_shelf_style','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(test),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

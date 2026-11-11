"""中文：真实设置页恢复分段滑动后的手势、三模式刷新与边界回归。
English: Real discrete settings gestures, three-mode presentation and boundary regression.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/apps/app_device_settings.c').read_text()

def block(text, start):
    at = text.index('{', start)
    depth, quote, comment, escape = 0, None, None, False
    while at < len(text):
        char, pair = text[at], text[at:at+2]
        if comment == 'line':
            if char == '\n': comment = None
        elif comment == 'block':
            if pair == '*/': comment = None; at += 1
        elif quote:
            if escape: escape = False
            elif char == '\\': escape = True
            elif char == quote: quote = None
        elif pair == '//': comment = 'line'; at += 1
        elif pair == '/*': comment = 'block'; at += 1
        elif char in "\"'": quote = char
        elif char == '{': depth += 1
        elif char == '}':
            depth -= 1
            if not depth: return text[start:at+1]
        at += 1
    raise AssertionError('Unclosed function')

def function(name):
    match = re.search(r'^static [^\n;{}]+\b'+name+r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    return block(source, match.start())

assert not any(word in source for word in ('ui_follow_', 'display_follow_', 's_scroll_cache', 'coasting', 'lighten_text'))
assert 'scroll_gesture(' not in function('on_tick')
start = source.index('if (s_page == SETTINGS_MAIN || s_page == SETTINGS_BLUETOOTH || s_page == SETTINGS_BLE_SCAN || s_page == SETTINGS_SHELF_STYLE) {', source.index('static app_redraw_t on_gesture'))
event_block = block(source, start)
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "epdiy.h"
#include "app.h"
#include "ui_gesture.h"
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {SETTINGS_MAIN,SETTINGS_BLUETOOTH,SETTINGS_BLE_SCAN,SETTINGS_UPGRADE,SETTINGS_SHELF_STYLE,
      SETTINGS_SYSTEM_FONT,SETTINGS_WALLPAPER,SETTINGS_AVATAR,SETTINGS_TEXT_EDIT,SETTINGS_PIN_ENTRY};
enum EpdDrawMode {MODE_GC16,MODE_GL16,MODE_DU};
#define UI_NAV_TOP 1096
#define UI_NAV_REFRESH_END 1088
#define UI_NAV_MARKER_SCAN_END 1120
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define SETTINGS_SCROLL_MAX 833
#define SHELF_STYLE_CHOICES 5
typedef int EpdWaveform;
enum {APP_MAIN_REFRESH_NORMAL,APP_MAIN_REFRESH_FAST,APP_MAIN_REFRESH_WATER};
static const int E0470_WAVEFORM=0,E0470_FOLLOW_WAVEFORM=1,settings_soft=2,settings_bw=3,E0470_FULL_WAVEFORM=4;
// 预览独立验证；本套件只运行原黑白列表出口。/ Preview has its own suite; this harness isolates BW lists.
static bool settings_gray_preview(void){return false;}
static bool binary=true;
static bool display_system_binary_set(bool b){bool old=binary;binary=b;return old;}
static int fulls;
static int update_display_full(void *hl){(void)hl;assert(!binary);++fulls;return 0;}
static int s_page,s_main_scroll,s_ble_scroll,s_style_scroll,selected_mode,paint_mode;
static const EpdWaveform *display_main_normal_waveform(void){return &settings_soft;}
static int app_settings_main_refresh_mode(void){return selected_mode;}
static bool s_scroll_drag_consumed,s_scroll_present_pending,s_input_layout,s_input_settle;
// 原离散单元隔离新手势钩子；下方另运行实际缓存/手势/推屏集成测试。
// Isolate legacy discrete units; the actual cache/gesture/presenter integration runs below.
static struct {bool tracking;int origin_offset,origin_y;} s_list_drag;
static void settings_drag_reset(void) {s_list_drag.tracking=false;}
static bool settings_drag_present(app_ctx_t *ctx){(void)ctx;return false;}
static void settings_drag_quick(app_ctx_t *ctx){(void)ctx;}
static bool settings_drag_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev,int *offset,int limit,app_redraw_t *redraw){
 (void)ctx;(void)ev;(void)offset;(void)limit;(void)redraw;return false;
}
static EpdRect s_input_area;
static int paints,pushes,row_taps,water_pushes,cancels;
static EpdRect pushed;
static enum EpdDrawMode pushed_mode;
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++paints;paint_mode=selected_mode;}
static int update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){
 (void)hl;assert(wave==&E0470_WAVEFORM||wave==&E0470_FOLLOW_WAVEFORM||wave==&settings_soft||wave==&settings_bw);if(s_scroll_present_pending==false&&s_page!=SETTINGS_UPGRADE&&s_page!=SETTINGS_TEXT_EDIT&&s_page!=SETTINGS_PIN_ENTRY)assert(!binary&&wave==&settings_soft&&mode==MODE_GL16);++pushes;pushed=area;pushed_mode=mode;return 0;
}
static int update_display_settings_list(void *hl,unsigned id,EpdRect area){assert(id==(unsigned)s_page+1);return update_display_area_diff_with(hl,&settings_soft,MODE_GL16,area);}
static int update_display_area_full_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){return update_display_area_diff_with(hl,wave,mode,area);}
static void guard_draw_result(void *hl,int status){(void)hl;assert(!status);}
static void display_main_transition_cancel(void){++cancels;}
enum {E0470_TURN_RTL=1};
static int update_display_main_turn(void *hl,EpdRect area,int dir){assert(dir==E0470_TURN_RTL);++water_pushes;return update_display_area_diff_with(hl,&settings_soft,MODE_GL16,area);}

static EpdRect upgrade_progress_area(void){return(EpdRect){52,468,580,64};}
typedef struct {int content_bottom;} layout_t;
static layout_t ble_layout(void){return(layout_t){2000-s_ble_scroll};}
static layout_t ble_scan_layout(void){return(layout_t){2000};}
''' + function('settings_plain_list') + '\n' + function('settings_grayscale_ui') + '\n' + function('settings_content_end') + '\n' + function('settings_viewport_end') + '\n' + function('scroll_gesture') + '\n' + function('settings_present')
unit += '\nstatic app_redraw_t event(app_ctx_t *ctx,const ui_gesture_event_t *ev){(void)ctx;'+event_block+'if(ev->type==UI_GESTURE_TAP)++row_taps;return APP_REDRAW_NONE;}\n'
unit += r'''
static ui_gesture_t recognizer;
static cst836u_touch_t touch;
static app_ctx_t ctx;
static app_redraw_t feed(int x,int y,bool down,bool press,int64_t now){
 touch=(cst836u_touch_t){.count=down?1:0,.touched=down,.x=x,.y=y};
 ctx=(app_ctx_t){.touch=&touch,.pressed=press,.released=!down,.now_ms=now};
 ui_gesture_event_t gesture;
 if(!ui_gesture_feed(&recognizer,&ctx,&gesture))return APP_REDRAW_NONE;
 app_redraw_t redraw=event(&ctx,&gesture);
 if(redraw==APP_REDRAW_AREA)assert(settings_present(&ctx,redraw));
 return redraw;
}
static void reset(int page,int offset){
 s_page=page;s_main_scroll=s_ble_scroll=offset;s_scroll_drag_consumed=s_scroll_present_pending=false;ui_gesture_reset(&recognizer);
}
int main(void){
 for(int mode=0;mode<3;++mode){
  selected_mode=mode;reset(SETTINGS_MAIN,0);int before=pushes,taps=row_taps;
  assert(feed(300,800,true,true,0)==APP_REDRAW_NONE);
  assert(feed(300,693,true,false,100)==APP_REDRAW_NONE&&s_main_scroll==0);
  assert(feed(300,692,true,false,110)==APP_REDRAW_AREA&&s_main_scroll==204);
  assert(pushes==before+1&&paints==pushes&&paint_mode==mode&&pushed_mode==MODE_GL16&&binary);
  assert(pushed.y==160&&pushed.y+pushed.height==UI_NAV_REFRESH_END);
  for(int i=1;i<=30;++i)assert(feed(300,692-i*10,true,false,110+i*10)==APP_REDRAW_NONE&&s_main_scroll==204);
  assert(feed(300,392,false,false,430)==APP_REDRAW_NONE&&s_main_scroll==204&&pushes==before+1&&row_taps==taps);
  for(int i=1;i<=30;++i)assert(feed(300,392,false,false,430+i*100)==APP_REDRAW_NONE&&s_main_scroll==204&&pushes==before+1);
  assert(feed(300,700,true,true,4000)==APP_REDRAW_NONE);
  assert(feed(300,700,false,false,4010)==APP_REDRAW_NONE&&row_taps==taps+1);
  // 短拖/横向/题头/底栏均不滚动；上下阈值、最大步长和上下边界保持原行为。
  // Short/horizontal/chrome drags do not scroll; retain thresholds, maximum step and both boundaries.
  reset(SETTINGS_MAIN,100);before=pushes;
  feed(300,800,true,true,0);feed(300,770,true,false,20);feed(300,770,false,false,40);assert(s_main_scroll==100&&pushes==before);
  reset(SETTINGS_MAIN,100);feed(300,800,true,true,0);feed(450,680,true,false,20);feed(450,680,false,false,40);assert(s_main_scroll==100);
  reset(SETTINGS_MAIN,100);feed(300,150,true,true,0);feed(300,20,true,false,20);feed(300,20,false,false,40);assert(s_main_scroll==100);
  reset(SETTINGS_MAIN,100);feed(300,1100,true,true,0);feed(300,900,true,false,20);feed(300,900,false,false,40);assert(s_main_scroll==100);
  reset(SETTINGS_MAIN,500);feed(300,300,true,true,0);assert(feed(300,410,true,false,20)==APP_REDRAW_AREA&&s_main_scroll==296);feed(300,410,false,false,40);
  reset(SETTINGS_MAIN,10);feed(300,300,true,true,0);feed(300,410,true,false,20);assert(s_main_scroll==0);feed(300,410,false,false,40);
  reset(SETTINGS_MAIN,10);feed(300,800,true,true,0);feed(300,200,true,false,20);assert(s_main_scroll==460);feed(300,200,false,false,40);
  reset(SETTINGS_MAIN,800);feed(300,800,true,true,0);feed(300,680,true,false,20);assert(s_main_scroll==833);feed(300,680,false,false,40);
  // 多指/故障取消后无额外推进，正常点击恢复；连续多次滑动不产生定稿/周期全刷。
  // Multitouch/fault cancellation adds no motion, taps recover, and repeated swipes add no settling/periodic full refresh.
  reset(SETTINGS_MAIN,0);feed(300,800,true,true,0);touch.count=2;ctx.pressed=false;ui_gesture_event_t canceled;
  assert(ui_gesture_feed(&recognizer,&ctx,&canceled)&&canceled.type==UI_GESTURE_CANCEL);event(&ctx,&canceled);assert(!s_main_scroll);
  for(int i=0;i<80;++i){reset(SETTINGS_MAIN,400);before=pushes;feed(300,800,true,true,0);feed(300,680,true,false,20);feed(300,680,false,false,40);assert(pushes==before+1&&pushed_mode==MODE_GL16);}
  for(int page=SETTINGS_BLUETOOTH;page<=SETTINGS_BLE_SCAN;++page){
   reset(page,0);feed(300,800,true,true,0);assert(feed(300,680,true,false,20)==APP_REDRAW_AREA&&s_ble_scroll==204);
   assert(pushed.y==190&&pushed.y+pushed.height==settings_content_end());feed(300,680,false,false,40);
  }
  const int pages[]={SETTINGS_SHELF_STYLE,SETTINGS_SYSTEM_FONT,SETTINGS_WALLPAPER,SETTINGS_AVATAR};
  for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);++i){
   s_page=pages[i];s_scroll_present_pending=true;assert(settings_present(&ctx,APP_REDRAW_AREA));
   assert(pushed_mode==MODE_GL16&&pushed.y==(s_page==SETTINGS_SHELF_STYLE?242:190)&&pushed.y+pushed.height==settings_content_end());
  }
 }
 for(int mode=0;mode<3;++mode){
  selected_mode=mode;s_page=SETTINGS_MAIN;int old=pushes,old_water=water_pushes;
  assert(settings_grayscale_ui(&ctx)&&settings_present(&ctx,APP_REDRAW_PAGE));
  assert(pushes==old+1&&binary&&pushed.y==0&&pushed.height==UI_NAV_MARKER_SCAN_END);
  assert(water_pushes==old_water+(mode==APP_MAIN_REFRESH_WATER));
  assert(settings_present(&ctx,APP_REDRAW_FULL)&&fulls==mode+1&&binary);
 }
 s_page=SETTINGS_SYSTEM_FONT;assert(!settings_grayscale_ui(&ctx));
 s_page=SETTINGS_UPGRADE;assert(settings_present(&ctx,APP_REDRAW_AREA)&&pushed.y==468&&pushed.height==64);
 assert(!settings_present(&ctx,APP_REDRAW_NONE));
 puts("PASS: actual generic gestures + restored settings handler/presenter: all three modes use one discrete step and one root/child gray58 update per swipe; binary scope restored; entry/manual full/water remain distinct; no tracking, momentum, motion ink or extra release/idle frames; list-cleanup accounting tested separately in the real display scheduler; root crop preserves navigation and child crop stops above persistent navigation; selectors and OTA remain");
}
'''
with tempfile.TemporaryDirectory(prefix='kiikoread-settings-rollback-') as directory:
    path=Path(directory)/'settings.c';binary=path.with_suffix('');path.write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(root/'tools/ui_gesture_stubs'),'-I'+str(root/'main/ui'),str(path),str(root/'main/ui/ui_gesture.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

subprocess.run(['python3',str(root/'tools/settings_hold_host_test.py')],check=True)
subprocess.run(['python3',str(root/'tools/settings_motion_waveform_host_test.py')],check=True)

# 真实绘制函数验证装饰与字缘分离，不能用整帧二值化掩盖灰色框线。
# Exercise real drawing helpers: binary decoration must not threshold glyph/avatar pixels.
paint=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int app_ctx_t;
typedef struct {int x,y,width,height;} EpdRect;
enum {SETTINGS_MAIN,SETTINGS_BLUETOOTH,SETTINGS_BLE_SCAN,SETTINGS_SYSTEM_FONT};
enum {UI_GRAY_BLACK=0,UI_GRAY_WHITE=255};
static int s_page,filled,framed,plain,separator,normalizations;
static bool preview;
static void ui_fill_round_rect(uint8_t*f,EpdRect r,int radius,uint8_t ink){(void)f;(void)r;(void)radius;filled=ink;}
static void ui_draw_round_rect(uint8_t*f,EpdRect r,int radius,uint8_t ink){(void)f;(void)r;(void)radius;assert(!ink);++plain;}
static void ui_draw_control_frame(uint8_t*f,EpdRect r,int radius,uint8_t ink){(void)f;(void)r;(void)radius;framed=ink;}
static void ui_hairline(uint8_t*f,int y,int x,int width,uint8_t ink){(void)f;(void)y;(void)x;(void)width;assert(!ink);++plain;}
static void ui_draw_separator(uint8_t*f,int y,int x,int width,uint8_t ink){(void)f;(void)y;(void)x;(void)width;separator=ink;}
static void render_content(app_ctx_t*c,uint8_t*f){(void)c;f[0]=0x58;f[1]=0xa5;}
static void display_monochrome_content(uint8_t*f){++normalizations;f[0]=0;f[1]=255;}
static void settings_navigation(uint8_t*f){(void)f;}
static bool settings_gray_preview(void){return preview;}
'''+function('settings_plain_list')+'\n'+function('settings_card')+'\n'+function('settings_divider')+'\n'+function('render')
paint+=r'''
int main(void){
 uint8_t fb[2];app_ctx_t ctx=0;
 for(s_page=SETTINGS_MAIN;s_page<=SETTINGS_BLE_SCAN;++s_page){
  int old=plain;settings_card(fb,(EpdRect){0,0,20,20},4,0xd0,0x70);
  assert(filled==255&&plain==old+1&&!framed);
  settings_divider(fb,10,0,20);assert(plain==old+2&&!separator);
  render(&ctx,fb);assert(fb[0]==0x58&&fb[1]==0xa5&&!normalizations);
 }
 s_page=SETTINGS_SYSTEM_FONT;settings_card(fb,(EpdRect){0,0,20,20},4,0xd0,0x70);
 settings_divider(fb,10,0,20);assert(filled==0xd0&&framed==0x70&&separator==0x60);
 render(&ctx,fb);assert(normalizations==1);
 preview=true;render(&ctx,fb);assert(fb[0]==0x58&&fb[1]==0xa5&&normalizations==1);
 puts("PASS: actual S31 list decoration is binary while glyph/avatar samples stay gray; non-list controls and gray previews retain their own paint policy");
}
'''
with tempfile.TemporaryDirectory(prefix='s31-list-paint-') as directory:
    path=Path(directory)/'paint.c';binary=path.with_suffix('');path.write_text(paint)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

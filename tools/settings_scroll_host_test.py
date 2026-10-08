"""设置页实际滚动反馈及字号选择回归。/ Actual settings scrolling and size-picker regression.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'main/apps/app_device_settings.c').read_text()
def block(start):
    at=s.index('{',start)+1
    depth,quote,escape=1,None,False
    while depth:
        c=s[at]
        if quote:
            if escape:escape=False
            elif c=='\\':escape=True
            elif c==quote:quote=None
        elif c in "\"'":quote=c
        elif c=='{':depth+=1
        elif c=='}':depth-=1
        at+=1
    return s[start:at]
def function(name):
    m=re.search(r'^static [^\n]+\b'+name+r'\([^;{}]*\)\s*\{',s,re.M)
    assert m,name
    return block(m.start())
render_start=s.index('static void render(')
render_size=block(s.index('if (s_page == SETTINGS_SYSTEM_SIZE)',render_start))
gesture_start=s.index('static app_redraw_t on_gesture(')
select_size=block(s.index('if (s_page == SETTINGS_SYSTEM_SIZE)',gesture_start))
select_style=block(s.index('if (s_page == SETTINGS_SHELF_STYLE) {',gesture_start))
scroll_style=block(s.index('if (s_page == SETTINGS_SHELF_STYLE &&',gesture_start))
# 资料卡必须在蓝牙和扫描页 return 之后绘制。/ The profile must draw only after both Bluetooth early returns.
profile=s.index('const int profile_y',render_start)
for page in ('SETTINGS_BLUETOOTH','SETTINGS_BLE_SCAN'):
    pos=s.index('if (s_page == '+page+')',render_start)
    assert pos<profile and 'return;' in block(pos) and 'profile_y' not in block(pos)
unit=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint8_t *fb;void *hl;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE} app_redraw_t;
typedef enum {UI_GESTURE_PRESS,UI_GESTURE_MOVE,UI_GESTURE_TAP,UI_GESTURE_CANCEL,UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D} gesture_t;
typedef struct {gesture_t type;int x,y,x0,y0;} ui_gesture_event_t;
enum {SETTINGS_MAIN,SETTINGS_BLUETOOTH,SETTINGS_BLE_SCAN,SETTINGS_UPGRADE,SETTINGS_SYSTEM_SIZE,
      SETTINGS_SHELF_STYLE,SETTINGS_SYSTEM_FONT,SETTINGS_WALLPAPER,SETTINGS_AVATAR,SETTINGS_TEXT_EDIT};
enum EpdDrawMode {MODE_GC16,MODE_GL16,MODE_DU};
#define UI_NAV_TOP 1096
#define UI_LOCK_WIDTH 684
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define EPD_DRAW_ALIGN_LEFT 0
static const int E0470_WAVEFORM=0;
static const int E0470_FOLLOW_WAVEFORM=1;
static int s_page;
static bool s_scroll_drag_consumed,s_scroll_present_pending;
static bool s_input_layout,s_input_settle;
static EpdRect s_input_area;
static int paints,pushes;
static EpdRect pushed;
static enum EpdDrawMode push_mode;
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++paints;}
static int update_display_area_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;(void)wave;pushed=area;push_mode=mode;++pushes;return 0;}
static int update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){return update_display_area_with(hl,wave,mode,area);}
static int update_display_area_full_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){return update_display_area_with(hl,wave,mode,area);}
static EpdRect upgrade_progress_area(void){return (EpdRect){52,468,580,64};}
static void guard_draw_result(void *hl,int status){(void)hl;assert(!status);}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static uint8_t chosen=120;
static uint8_t chosen_style=2;
static int s_style_scroll;
static void app_settings_set_shelf_style(uint8_t style){chosen_style=style;}
static uint8_t app_settings_system_font_size(void){return chosen;}
static void app_settings_set_system_font_size(uint8_t percent){chosen=percent;}
static void ui_text_set_system_scale(bool enabled){assert(enabled);}
static void back_header(uint8_t *fb,const char *s){(void)fb;(void)s;}
static void section(uint8_t *fb,int y,const char *s){(void)fb;(void)y;(void)s;}
static unsigned cards;
static EpdRect boxes[11];
static void settings_card(uint8_t *fb,EpdRect r,int radius,int fill,int border){(void)fb;(void)radius;(void)fill;(void)border;assert(cards<11);boxes[cards++]=r;assert(r.y+r.height<UI_NAV_TOP);}
static void ui_text_vc(uint8_t *fb,...){(void)fb;}
static void epd_fill_circle(int x,int y,int r,int gray,uint8_t *fb){(void)x;(void)y;(void)r;(void)gray;(void)fb;}
static void ui_nav_draw(uint8_t *fb,int tab){(void)fb;(void)tab;}
"""+function('scroll_gesture')+'\n'+function('settings_present')+'\n'
unit+='static void draw_size(uint8_t *fb){'+render_size+'}\n'
unit+='static app_redraw_t choose_size(int x,int y){ui_gesture_event_t event={.x0=x,.y0=y};const ui_gesture_event_t *ev=&event;'+select_size+'return APP_REDRAW_NONE;}\n'
unit+='static app_redraw_t choose_style(int x,int y){ui_gesture_event_t event={.x0=x,.y0=y};const ui_gesture_event_t *ev=&event;'+select_style+'return APP_REDRAW_NONE;}\n'
unit+='static app_redraw_t swipe_style(const ui_gesture_event_t *ev){'+scroll_style+'return APP_REDRAW_NONE;}\n'
unit+=r"""
int main(void){
 app_ctx_t ctx={0};ui_gesture_event_t ev={.type=UI_GESTURE_PRESS,.x0=300,.y0=700,.x=300,.y=700};int offset=0;bool handled;
 s_page=SETTINGS_MAIN;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_NONE&&handled);
 ev.type=UI_GESTURE_MOVE;ev.y=600;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_NONE&&!offset);
 ev.y=580;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_AREA&&offset==204&&handled);
 assert(settings_present(&ctx,APP_REDRAW_AREA)&&paints==1&&pushes==1&&pushed.y==160&&pushed.height==936&&push_mode==MODE_GL16);
 ev.y=400;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_NONE&&offset==204);
 ev.type=UI_GESTURE_SWIPE_U;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_NONE&&offset==204&&!s_scroll_drag_consumed);
 ev.type=UI_GESTURE_PRESS;scroll_gesture(&ev,&offset,334,&handled);ev.type=UI_GESTURE_SWIPE_U;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_AREA&&offset==334);assert(settings_present(&ctx,APP_REDRAW_AREA));
 assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_NONE);
 ev.type=UI_GESTURE_SWIPE_D;ev.y=1000;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_AREA&&offset==34);assert(settings_present(&ctx,APP_REDRAW_AREA));
 ev.y0=1120;ev.y=400;ev.type=UI_GESTURE_MOVE;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_NONE&&offset==34);
 ev.y0=100;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_NONE);
 ev.type=UI_GESTURE_TAP;assert(scroll_gesture(&ev,&offset,334,&handled)==APP_REDRAW_NONE&&!handled);
 s_page=SETTINGS_BLUETOOTH;
 for(int i=0;i<80;++i){s_scroll_present_pending=true;assert(settings_present(&ctx,APP_REDRAW_AREA));assert(pushed.y==190&&pushed.height==906&&push_mode==MODE_GL16);}
 assert(pushes==83&&paints==83);
 const int pages[]={SETTINGS_BLE_SCAN,SETTINGS_SYSTEM_FONT,SETTINGS_WALLPAPER,SETTINGS_AVATAR,SETTINGS_SHELF_STYLE};
 for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);++i){
   s_page=pages[i];s_scroll_present_pending=true;assert(settings_present(&ctx,APP_REDRAW_AREA));
   assert(push_mode==MODE_GL16&&pushed.y==(s_page==SETTINGS_SHELF_STYLE?242:190)&&pushed.y+pushed.height==UI_NAV_TOP);
 }
 assert(!settings_present(&ctx,APP_REDRAW_PAGE));
 s_page=SETTINGS_UPGRADE;assert(settings_present(&ctx,APP_REDRAW_AREA)&&pushed.y==468&&pushed.height==64);
 s_page=SETTINGS_SYSTEM_SIZE;draw_size(NULL);assert(cards==11);
 for(unsigned i=0;i<11;++i){EpdRect r=boxes[i];assert(choose_size(r.x+r.width/2,r.y+r.height/2)==APP_REDRAW_PAGE&&chosen==100+i*10);}
 assert(chosen==200&&choose_size(340,310)==APP_REDRAW_NONE&&chosen==200&&choose_size(400,970)==APP_REDRAW_NONE);
 // 第五种样式可滚动选择；屏幕外卡片、题头、导航和卡片间隙不得误选。
 // Reach the fifth style without selecting clipped cards, headers, navigation or gutters.
 s_page=SETTINGS_SHELF_STYLE;s_style_scroll=0;
 ev.type=UI_GESTURE_SWIPE_D;assert(swipe_style(&ev)==APP_REDRAW_NONE);
 ev.type=UI_GESTURE_SWIPE_U;
 assert(swipe_style(&ev)==APP_REDRAW_AREA&&s_style_scroll==253);
 assert(swipe_style(&ev)==APP_REDRAW_AREA&&s_style_scroll==506);
 assert(swipe_style(&ev)==APP_REDRAW_NONE&&s_style_scroll==506);
 for(int offset=0;offset<=506;offset+=253)for(int i=0;i<5;++i){
   s_style_scroll=offset;s_page=SETTINGS_SHELF_STYLE;chosen_style=0;
   int y=263+i*253-offset+115;
   if(y<242||y>=1096)continue;
   assert(choose_style(340,y)==APP_REDRAW_PAGE&&chosen_style==i+1&&s_page==SETTINGS_MAIN);
 }
 s_style_scroll=506;s_page=SETTINGS_SHELF_STYLE;chosen_style=5;
 assert(choose_style(340,230)==APP_REDRAW_NONE&&chosen_style==5);
 assert(choose_style(340,1096)==APP_REDRAW_NONE&&chosen_style==5);
 assert(choose_style(35,800)==APP_REDRAW_NONE&&chosen_style==5);
 assert(choose_style(648,800)==APP_REDRAW_NONE&&chosen_style==5);
 assert(choose_style(340,750)==APP_REDRAW_NONE&&chosen_style==5);
 puts("PASS: all five shelf styles reachable; scroll clamps and clipped-header/nav/gutter hit guards");
 puts("PASS: immediate bounded scroll, one update per drag, no periodic GC16 during 80 swipes, header/nav excluded across all settings lists, Bluetooth profile isolation and eleven size options through 200%");
}
"""
with tempfile.TemporaryDirectory() as folder:
    c,binary=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

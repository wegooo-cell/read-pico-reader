#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：执行真实样式选择、缩略图及滚动片段，验证新编号/触摸/固定页头。
# English: Execute actual picker, thumbnail and scrolling code for new IDs, hit testing and fixed headers.
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
source = root / 'main/apps/app_device_settings.c'
text = source.read_text()

def block_at(start):
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m[0]), text)
    at = masked.index('{', start) + 1
    depth = 1
    while depth:
        depth += (masked[at] == '{') - (masked[at] == '}')
        at += 1
    return text[start:at]

render_start = text.index('static void render_content(')
render = block_at(text.index('    if (s_page == SETTINGS_SHELF_STYLE) {', render_start))
gesture_start = text.index('static app_redraw_t on_gesture(')
choose = block_at(text.rindex('    if (s_page == SETTINGS_SHELF_STYLE) {', gesture_start, text.index('static app_redraw_t on_key(',gesture_start)))
scroll = block_at(text.index('    if (s_page == SETTINGS_MAIN || s_page == SETTINGS_BLUETOOTH', gesture_start))
catalog = re.search(r'^static const uint8_t s_shelf_style_ids\[\].*;', text, re.M)[0]
count = re.search(r'^#define SHELF_STYLE_CHOICES .*', text, re.M)[0]
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
enum {SETTINGS_MAIN,SETTINGS_SHELF_STYLE,SETTINGS_BLUETOOTH,SETTINGS_BLE_SCAN};
enum {UI_GESTURE_PRESS,UI_GESTURE_TAP,UI_GESTURE_CANCEL,UI_GESTURE_MOVE,UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D};
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE} app_redraw_t;
typedef struct {int type,x0,y0,x,y;} ui_gesture_event_t;
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_ALIGN_CENTER 1
#define SETTINGS_SCROLL_MAX 1000
static int s_page=SETTINGS_SHELF_STYLE,s_style_scroll,s_main_scroll,s_ble_scroll,viewport=1088,style=2;
static bool s_scroll_drag_consumed,s_scroll_present_pending,s_main_wifi_saved;
static char s_main_wifi[33];
static unsigned glass,acrylic,headers,footers,cards,titles;
static int settings_viewport_end(void){return viewport;}
static int settings_content_end(void){return viewport;}
static int app_settings_shelf_style(void){return style;}
static void app_settings_set_shelf_style(uint8_t v){style=v;}
static void settings_prepare_profile(void){}
static int read_pico_transfer_get_saved_wifi(char *s,bool *saved){(void)s;(void)saved;return 0;}
typedef struct {int content_bottom;} layout_t;
static layout_t ble_layout(void){return (layout_t){0};}
static layout_t ble_scan_layout(void){return (layout_t){0};}
static void ui_fill_rect_fast(EpdRect r,int c,uint8_t *fb){(void)c;(void)fb;if(r.y==0&&r.height==242)++headers;if(r.y==viewport)++footers;}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int c){(void)fb;(void)r;(void)radius;(void)c;}
static void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,int c){(void)fb;(void)r;(void)radius;(void)c;}
static void ui_hairline(uint8_t *fb,int y,int x,int w,int c){(void)fb;(void)y;(void)x;(void)w;(void)c;}
static void ui_draw_acrylic_guard(uint8_t *fb,EpdRect r){(void)fb;assert(r.height==70);++acrylic;}
static void ui_draw_liquid_shelf_rail(uint8_t *fb,EpdRect r){(void)fb;assert(r.height==36&&r.width==486);++glass;}
static void ui_nav_back(uint8_t *fb,int x,int y){(void)fb;(void)x;(void)y;}
static void ui_nav_status(uint8_t *fb){(void)fb;}
static void settings_navigation(uint8_t *fb){(void)fb;}
static void settings_card(uint8_t *fb,EpdRect r,int radius,int a,int b){(void)fb;(void)r;(void)radius;(void)a;(void)b;++cards;}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *s,int a,bool i){(void)fb;(void)x;(void)y;(void)px;(void)s;(void)a;(void)i;}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *s,int a,bool i){ui_text_vc(fb,x,y,px,s,a,i);}
static void ui_text_box(uint8_t *fb,EpdRect r,int px,const char *s,int a,bool i){
 (void)fb;(void)px;(void)a;(void)i;assert(r.x>=68&&r.x+r.width<=582&&r.y>=242&&r.y+r.height<viewport);
 if(!strcmp(s,"液态玻璃书架"))++titles;
}
static void epd_draw_circle(int x,int y,int r,int c,uint8_t *fb){(void)x;(void)y;(void)r;(void)c;(void)fb;}
static void epd_fill_circle(int x,int y,int r,int c,uint8_t *fb){(void)x;(void)y;(void)r;(void)c;(void)fb;}
'''
unit += r'''
enum {SHELF,BULK};
static int s_view=SHELF;
#define BOOK_GRID_ROWS 9
#define BOOK_BULK_ROWS 5
#define UI_MARGIN 36
#define UI_GAP 20
#define UI_BTN_H 60
static int ui_content_width(void){return 612;}
'''
unit+='\n'.join(line for line in (root/'main/ui/ui_book_manage_layout.h').read_text().splitlines() if not line.startswith(('#include','#pragma')))+'\n'
unit+='static bool s_bulk_grid;\n'
for name in ('shelf_rows','row_rect','shelf_cover_image'):
    unit += function(name,root/'main/apps/app_book.c')+'\n'
unit += catalog + '\n' + count + '\n' + function('draw_style_thumbnail', source) + '\n'
unit += 'static void paint(uint8_t *fb){' + render + '}\n'
unit += function('scroll_gesture', source) + '\n'
unit += 'static app_redraw_t scroll_event(const ui_gesture_event_t *ev){' + scroll + 'return APP_REDRAW_NONE;}\n'
unit += 'static app_redraw_t tap(int x,int y){ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=x,.y0=y};const ui_gesture_event_t *ev=&e;' + choose + 'return APP_REDRAW_NONE;}\n'
unit += r'''
int main(void){
 const int expected[]={1,2,6,5};uint8_t fb=0;
 for(unsigned choice=0;choice<4;++choice){
  style=expected[choice];int rows=style==5?4:9;assert(shelf_rows()==rows);
  for(int i=0;i<rows;++i){EpdRect hit=row_rect(i),cover=shelf_cover_image(i);
   assert(cover.x>=36&&cover.x+cover.width<=648&&cover.y>=184&&cover.y+cover.height<=972);
   assert(cover.x>=hit.x&&cover.x+cover.width<=hit.x+hit.width&&cover.y>=hit.y&&cover.y+cover.height<=hit.y+hit.height);
   if(style==6)assert(cover.width==164&&cover.height==214&&hit.y==192+i/3*282);
  }
 }
 for(viewport=1088;viewport<=1216;viewport+=128){
  s_page=SETTINGS_SHELF_STYLE;s_style_scroll=0;headers=footers=cards=titles=glass=acrylic=0;
  paint(&fb);assert(headers==1&&footers==1&&titles==1&&glass==1&&acrylic==1);
  assert(SHELF_STYLE_CHOICES==4);
  for(int i=0;i<3;++i){s_page=SETTINGS_SHELF_STYLE;s_style_scroll=0;style=0;assert(tap(300,263+i*253+100)==APP_REDRAW_PAGE&&style==expected[i]&&s_page==SETTINGS_MAIN);}
  s_page=SETTINGS_SHELF_STYLE;s_style_scroll=0;style=6;
  ui_gesture_event_t swipe={.type=UI_GESTURE_SWIPE_U,.x0=350,.y0=900,.x=350,.y=600};
  assert(scroll_event(&swipe)==APP_REDRAW_AREA&&s_scroll_present_pending);
  assert(s_style_scroll>0&&s_style_scroll<=176);
  int offset=s_style_scroll;
  headers=footers=0;paint(&fb);assert(headers==1&&footers==1);
  assert(tap(300,225)==APP_REDRAW_NONE&&style==6);
  assert(tap(10,1050)==APP_REDRAW_NONE&&style==6);
  assert(tap(300,263+3*253+100-offset)==APP_REDRAW_PAGE&&style==5);
  s_page=SETTINGS_SHELF_STYLE;swipe.type=UI_GESTURE_SWIPE_D;swipe.y=1200;
  assert(scroll_event(&swipe)==APP_REDRAW_AREA&&s_style_scroll==0);
 }
 puts("PASS: four distinct shelf IDs, liquid thumbnail, fixed chrome, scrolled list selection and isolated header/edge taps");
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / 'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)

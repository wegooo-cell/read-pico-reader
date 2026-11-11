"""Actual font AA control, compact text geometry and guide controls.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re,subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
book=ROOT/'main/apps/app_book.c'
s=book.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool consumed,user_activity;int64_t now_ms;} app_ctx_t;
typedef enum {UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_TAP,UI_GESTURE_CANCEL,UI_GESTURE_PRESS,UI_GESTURE_MOVE} ui_gesture_type_t;
typedef struct {ui_gesture_type_t type;unsigned x0,y0,x,y;} ui_gesture_event_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {READING,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_FONT_SETTINGS,READER_PANEL_LAYOUT_SETTINGS,READER_PANEL_BACKGROUND_PREVIEW,READER_PANEL_RULE_SETTINGS};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,s_reader_slider=3,s_pressed_control=2;
static char *s_text="text";
static bool s_clear_confirm,s_scan_pending;
static struct {bool pending;} s_reader_tap;
static int64_t s_stats_activity_ms,s_reader_notice_until;
static unsigned invalidations;
static void invalidate_prep(void){++invalidations;}
void pause_reader_prep(void){++invalidations;}
static bool locked,aa=true,raster_aa=true,save_fail;
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static bool app_settings_book_antialias(void){return aa;}
static bool vertical_book;
static bool app_settings_book_vertical(void){return vertical_book;}
static bool app_settings_set_book_antialias(bool on){assert(locked);if(save_fail)return false;aa=on;return true;}
static void ttf_set_antialias(bool on){assert(locked);raster_aa=on;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static char s_reader_notice[64];
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static int64_t esp_timer_get_time(void){return 1000000;}
'''
unit+='static unsigned font_scale=100;\nstatic unsigned app_settings_system_font_size(void){return font_scale;}\n'
unit+=function('reader_ui_extra',book)+'\n'+function('reader_font_card',book)+'\n'
for name in ['s_font_card','s_weight_card','s_bookfont_card','s_layout_card','s_rule_card','s_antialias_card']:
 unit+=re.search(r'^#define '+name+r' .*',s,re.M)[0]+'\n'

action=function('reader_panel_action',book)
a=action.index('        if (ui_rect_hit(s_layout_card, x, y)) {')
b=action.index('        if (ui_rect_hit(bookfont, x, y)) {',a)
unit+='static app_redraw_t tap_aa(int x,int y){\n'+action[a:b]+'return APP_REDRAW_NONE;}\n'
unit+=r'''
static int rule_style=1,rule_offset,raster_rule_style=1,raster_rule_offset;
static bool rule_full,raster_rule_full;
static int app_settings_book_reading_line(void){return rule_style;}
static void app_settings_set_book_reading_line(uint8_t style){rule_style=style;}
static void book_layout_set_reading_line(unsigned style){raster_rule_style=style;}
static bool app_settings_book_reading_line_full(void){return rule_full;}
static bool app_settings_set_book_reading_line_full(bool full){if(save_fail)return false;rule_full=full;return true;}
static void book_layout_set_reading_line_full(bool full){raster_rule_full=full;}
static int app_settings_book_reading_line_offset(void){return rule_offset;}
static void app_settings_set_book_reading_line_offset(int8_t offset){rule_offset=offset;}
static void book_layout_set_reading_line_offset(int offset){raster_rule_offset=offset;}
'''
for name in ['s_rule_offset_up','s_rule_offset_reset','s_rule_offset_down']:
 unit+=re.search(r'^static const EpdRect '+name+r' = .*',s,re.M)[0]+'\n'
unit+=function('rule_style_rect',book)+'\n'+function('rule_span_rect',book)+'\n'
a=action.index('    if (s_reader_panel == READER_PANEL_RULE_SETTINGS) {')
b=action.index('    if (s_reader_panel == READER_PANEL_FONT_PICKER)',a)
unit+='static app_redraw_t tap_rule(int x,int y){\n'+action[a:b]+'return APP_REDRAW_NONE;}\n'


unit+=r'''
int main(void){
 s_reader_panel=READER_PANEL_RULE_SETTINGS;
 assert(tap_rule(550,920)==APP_REDRAW_PAGE&&rule_full&&raster_rule_full&&invalidations==1);
 assert(tap_rule(330,920)==APP_REDRAW_PAGE&&!rule_full&&!raster_rule_full&&invalidations==2);
 save_fail=true;assert(tap_rule(550,920)==APP_REDRAW_NONE&&!rule_full&&!raster_rule_full&&invalidations==2);save_fail=false;
 assert(tap_rule(470,790)==APP_REDRAW_PAGE&&rule_style==2&&raster_rule_style==2);
 assert(tap_rule(70,1040)==APP_REDRAW_PAGE&&rule_offset==-2&&raster_rule_offset==-2);
 assert(tap_rule(280,1040)==APP_REDRAW_PAGE&&rule_offset==0&&raster_rule_offset==0);
 assert(tap_rule(70,790)==APP_REDRAW_PAGE&&!rule_style&&!raster_rule_style);
 assert(tap_rule(550,920)==APP_REDRAW_PAGE&&rule_full&&raster_rule_full);
 assert(tap_rule(70,1040)==APP_REDRAW_NONE&&rule_offset==0);
 for(font_scale=100;font_scale<=200;font_scale+=10){
  const EpdRect cards[]={s_font_card,s_weight_card,s_bookfont_card,s_layout_card};
  for(unsigned i=0;i<4;++i){EpdRect r=cards[i];assert(r.x>=36&&r.y>=552&&r.x+r.width<=648&&r.y+r.height<1141);
   for(unsigned j=0;j<i;++j){EpdRect q=cards[j];assert(r.x+r.width<=q.x||q.x+q.width<=r.x||r.y+r.height<=q.y||q.y+q.height<=r.y);}}
 }
 font_scale=100;assert(aa&&raster_aa);assert(tap_aa(400,1090)==APP_REDRAW_PAGE&&!aa&&!raster_aa&&!locked);
 save_fail=true;assert(tap_aa(400,1090)==APP_REDRAW_PAGE&&!aa&&!raster_aa&&!locked&&s_reader_notice_until==2800);
 save_fail=false;assert(tap_aa(400,1090)==APP_REDRAW_PAGE&&aa&&raster_aa&&!locked);
 assert(tap_aa(341,1090)==APP_REDRAW_NONE&&tap_aa(650,1090)==APP_REDRAW_NONE);
 puts("PASS: four compact text cards at100-200%, AA moved to former layout card with atomic failure behavior; native guide styles/span/offset preserved");
}
'''
font=function('draw_font_settings',book)
assert '阅读线' not in font and '排版设置' not in font and 'const EpdRect toggles[]={s_bookfont_card,s_layout_card}' in font and 'card.x+21,cy,23' in font and 'cy-19,69,39' in font
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

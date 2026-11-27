#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：Plan A1 顶栏搜索胶囊（300,94,108,54）、48px X 清除区与 72px 底部四键分页测试。
# English: Plan A1 top-bar search capsule (300,94,108,54), 48px X clear hit and 72px bottom pager test.
from pathlib import Path
import re,subprocess,tempfile
from home_cover_cache_host_test import function
r=Path(__file__).resolve().parents[1];b=r/'main/apps/app_book.c';k=r/'main/ui/ui_kit.c'
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct{int x,y,width,height;}EpdRect;
enum{
    UI_LOCK_WIDTH=684,
    UI_ICON_CHEVRONS_LEFT=10,
    UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT=11,
    UI_ICON_ANIMATE_CIRCLE_CHEVRON_RIGHT=12,
    UI_ICON_CHEVRONS_RIGHT=13,
    UI_ICON_SEARCH=17,
    UI_ICON_X=18,
    EPD_DRAW_ALIGN_CENTER=1,
    EPD_DRAW_ALIGN_RIGHT=2,
    EPD_DRAW_ALIGN_LEFT=0,
    UI_MARGIN=40,
    UI_PX_BODY=38
};
typedef int ui_icon_t;
typedef struct{char*text;}ui_text_edit_t;
static ui_text_edit_t s_search_input;
static char s_query[65],label_seen[64];static int s_pressed_control=-1;
static bool s_shelf_key_mode;static int s_shelf_key_pressed;
static int icon_x,icon_y,keyboard,field,underlines,frames,pressed_frames;
static EpdRect page_box,search_text;
static int ui_text_fixed_width_px(int px,const char*text){int w=0;for(;*text;++text)if(((unsigned char)*text&0xc0)!=0x80)w+=((unsigned char)*text<128?px/2:px);return w;}
static void ui_fill_round_rect(uint8_t*f,EpdRect rect,int rad,int color){(void)f;(void)rect;(void)rad;(void)color;}
static void ui_draw_control_frame(uint8_t*f,EpdRect rect,int rad,int color){(void)f;(void)rect;(void)rad;(void)color;++frames;}
static void ui_draw_pressed_round_rect(uint8_t*f,EpdRect rect,int rad){(void)f;(void)rect;(void)rad;++pressed_frames;}
static void ui_draw_icon(uint8_t*f,int x,int y,int px,int icon,int color){
 (void)f;(void)color;
 if(icon==UI_ICON_X||icon==UI_ICON_SEARCH){assert(px==28&&icon==(s_query[0]?UI_ICON_X:UI_ICON_SEARCH));icon_x=x;icon_y=y;}
 else{assert(y==1034&&(px==40||px==44||px==48));}
}
static void ui_hairline(uint8_t*f,int y,int x,int width,int color){(void)f;(void)color;assert(y==94+54-9&&x>=346&&width>0);++underlines;}
static void ui_clear_page(uint8_t*f){(void)f;}
static void ui_nav_status(uint8_t*f){(void)f;}
static void ui_nav_back(uint8_t*f,int x,int y){(void)f;assert(x==36&&y==79);}
static void ui_text_vc(uint8_t*f,int x,int y,int px,const char*text,int align,bool inv){(void)f;(void)align;assert(!inv);
 if(!strcmp(text,"搜索")&&px==22){int width=ui_text_fixed_width_px(22,text);search_text=(EpdRect){x-width/2,y-27,width,54};}
 else if(strchr(text,'/')){assert(px<=32&&y==1034);page_box=(EpdRect){x,0,px,0};snprintf(label_seen,sizeof(label_seen),"%s",text);}
}
#define ui_text_fixed_vc ui_text_vc
static int ui_content_width(void){return 604;}
static void ui_text_input_draw(uint8_t*f,ui_text_edit_t*edit,EpdRect rect,int px,bool inv,void*placeholder){(void)f;assert(edit==&s_search_input&&rect.x==40&&rect.y==200&&rect.width==604&&rect.height==88&&px==38&&!inv&&!placeholder);++field;}
static void ui_draw_button(uint8_t*f,EpdRect rect,const char*text,bool selected){(void)f;assert(rect.x==36&&rect.y==392&&rect.height==66&&!strcmp(text,"清空")&&!selected);}
static void ui_keyboard_draw(uint8_t*f,int top){(void)f;assert(top==560);++keyboard;}
'''
u+=re.search(r'enum \{ UI_PAGER_NONE=[^}]+\};',(r/'main/ui/ui_kit.h').read_text())[0]+'\n'
for n in ['ui_rect_hit','ui_pager_target']:u+=function(n,k)+'\n'
for n in ['shelf_pager_rect','shelf_pager_hit','shelf_page_label_rect','shelf_search_rect','shelf_search_clear_rect','draw_shelf_search_button','draw_shelf_pager','draw_search']:u+=function(n,b)+'\n'
u+=r'''
int main(void){
 draw_shelf_pager(NULL,32,0,4);assert(!strcmp(label_seen,"01/04")&&!underlines);
 draw_shelf_pager(NULL,32,3,4);assert(!strcmp(label_seen,"04/04"));
 draw_shelf_pager(NULL,0,0,1);assert(!strcmp(label_seen,"01/01"));
 EpdRect sr=shelf_search_rect(0,4);
 assert(sr.x==300&&sr.y==94&&sr.width==108&&sr.height==54);
 draw_shelf_search_button(NULL,0,4);
 assert(icon_x==328&&icon_y==121&&frames>0);
 assert(ui_rect_hit(sr,icon_x,icon_y)&&ui_rect_hit(sr,search_text.x+search_text.width-1,121));
 assert(shelf_pager_hit(sr.x,sr.y)==UI_PAGER_NONE);
 for(int i=UI_PAGER_FIRST;i<=UI_PAGER_LAST;++i){
  EpdRect pr=shelf_pager_rect(i);
  assert(pr.y==998&&pr.height==72);
  assert(shelf_pager_hit(pr.x+pr.width/2,pr.y+pr.height/2)==i);
 }
 s_pressed_control=112;draw_shelf_search_button(NULL,0,4);assert(icon_y==123&&pressed_frames==1);
 s_pressed_control=-1;strcpy(s_query,"三体");draw_shelf_search_button(NULL,0,4);assert(underlines==1&&icon_y==121);
 EpdRect close=shelf_search_clear_rect(0,4);
 assert(close.width==48&&close.height==sr.height&&close.x==sr.x);
 assert(ui_rect_hit(close,icon_x,icon_y)&&!ui_rect_hit(close,search_text.x+search_text.width-1,121));
 s_pressed_control=113;draw_shelf_search_button(NULL,0,4);assert(icon_y==123&&underlines==2);s_pressed_control=-1;
 draw_search(NULL);assert(keyboard==1&&field==1);
 puts("PASS: Plan A1 top-bar search capsule (108x54), 48px X clear hit, 72px framed bottom pager and existing keyboard layout");
}
'''
action=function('action_at',b);control=function('control_at',b);paint=function('paint_control',b)
assert 'if(ui_rect_hit(shelf_search_rect(ctx->leaf,leaves()),x,y)){search_begin();return APP_REDRAW_PAGE;}' in action
assert action.index('if(ui_rect_hit(shelf_search_rect(')<action.index('int pager=shelf_pager_hit')
assert action.index('return shelf_search_clear(ctx);')<action.index('if(ui_rect_hit(shelf_search_rect(')
assert 'return 113;' in control and 'shelf_search_clear_rect' in paint
assert 'if(s_view==SHELF&&ui_rect_hit(shelf_search_rect(ctx->leaf,leaves()),x,y)){*rect=shelf_search_rect(ctx->leaf,leaves());return 112;}' in control
assert 'draw_shelf_search_button(ctx->fb,ctx->leaf,leaves());' in paint
with tempfile.TemporaryDirectory()as d:
 p=Path(d)/'test.c';p.write_text(u);out=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)

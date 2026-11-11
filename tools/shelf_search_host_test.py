#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：真实页码/搜索绘图、整组居中和图标与文字共同点击区。
# English: Actual footer drawing, grouped centring and a shared icon/text search hit.
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
enum{UI_LOCK_WIDTH=684,UI_ICON_SEARCH=17,UI_ICON_X=18,EPD_DRAW_ALIGN_CENTER=1,EPD_DRAW_ALIGN_RIGHT=2,EPD_DRAW_ALIGN_LEFT=0,UI_MARGIN=40,UI_PX_BODY=38};
typedef struct{char*text;}ui_text_edit_t;
static ui_text_edit_t s_search_input;
static char s_query[65],label_seen[64];static int s_pressed_control=-1;
static bool s_shelf_key_mode;static int s_shelf_key_pressed;
static int icon_x,icon_y,dot_x,keyboard,field,underlines;
static EpdRect page_box,search_text;
static int test_scale=100;
static int ui_text_effective_px(int px){return (px+2)*test_scale/100;}
static int ui_text_fixed_width_px(int px,const char*text){int w=0;for(;*text;++text)if(((unsigned char)*text&0xc0)!=0x80)w+=((unsigned char)*text<128?px/2:px);return w;}
static void ui_draw_icon(uint8_t*f,int x,int y,int px,int icon,int color){(void)f;assert(px==26&&icon==(s_query[0]?UI_ICON_X:UI_ICON_SEARCH)&&color==0x30);icon_x=x;icon_y=y;}

static void ui_hairline(uint8_t*f,int y,int x,int width,int color){(void)f;assert(y==1053&&x==search_text.x&&width==search_text.width&&color==0x30);++underlines;}
static void epd_fill_circle(int x,int y,int radius,int color,uint8_t*f){(void)f;assert(y==1031&&radius==2&&color==0x80);dot_x=x;}
static void ui_pager_draw(uint8_t*f,const char*label,int page,int pages,bool key_mode,int pressed){(void)f;(void)key_mode;(void)pressed;assert(!*label&&page>=0&&page<pages);}
static void ui_clear_page(uint8_t*f){(void)f;}
static void ui_nav_status(uint8_t*f){(void)f;}
static void ui_nav_back(uint8_t*f,int x,int y){(void)f;assert(x==36&&y==79);}
static void ui_text_vc(uint8_t*f,int x,int y,int px,const char*text,int align,bool inv){(void)f;(void)align;assert(!inv);
 if(!strcmp(text,"搜索")&&px==26){int width=ui_text_fixed_width_px(26,text);search_text=(EpdRect){x-width/2,y-25,width,50};}
 else if(strchr(text,'/')){assert(px<=32&&y==1031);page_box=(EpdRect){x,0,px,0};snprintf(label_seen,sizeof(label_seen),"%s",text);}
}
#define ui_text_fixed_vc ui_text_vc
static int ui_content_width(void){return 604;}
static void ui_text_input_draw(uint8_t*f,ui_text_edit_t*edit,EpdRect rect,int px,bool inv,void*placeholder){(void)f;assert(edit==&s_search_input&&rect.x==40&&rect.y==200&&rect.width==604&&rect.height==88&&px==38&&!inv&&!placeholder);++field;}
static void ui_draw_button(uint8_t*f,EpdRect rect,const char*text,bool selected){(void)f;assert(rect.x==36&&rect.y==392&&rect.height==66&&!strcmp(text,"清空")&&!selected);}
static void ui_keyboard_draw(uint8_t*f,int top){(void)f;assert(top==560);++keyboard;}
'''
u+=re.search(r'enum \{ UI_PAGER_NONE=[^}]+\};',(r/'main/ui/ui_kit.h').read_text())[0]+'\n'
for n in ['ui_rect_hit','ui_pager_area_at','ui_pager_rect_at','ui_pager_hit_at']:u+=function(n,k)+'\n'
for n in ['shelf_pager_group_rect','shelf_page_label_rect','shelf_search_rect','shelf_search_clear_rect','draw_shelf_search_button','draw_shelf_pager','draw_search']:u+=function(n,b)+'\n'
u+=r'''
int main(void){
 draw_shelf_pager(NULL,32,0,4);assert(!strcmp(label_seen,"01/04")&&!underlines);
 draw_shelf_pager(NULL,32,3,4);assert(!strcmp(label_seen,"04/04"));
 draw_shelf_pager(NULL,0,0,1);assert(!strcmp(label_seen,"01/01"));
 int totals[]={1,4,12,125,12345};
 for(unsigned i=0;i<sizeof(totals)/sizeof(*totals);++i)for(int last=0;last<2;++last){
  int pages=totals[i],page=last?pages-1:0;draw_shelf_pager(NULL,pages*8,page,pages);
  EpdRect hit=shelf_search_rect(page,pages),group=shelf_pager_group_rect(page,pages);int drawn_page_x=page_box.x;page_box=shelf_page_label_rect(page,pages);assert(drawn_page_x==page_box.x+page_box.width/2);
  assert(group.x+group.width/2==342&&group.x>=212&&group.x+group.width<=472);
  assert(icon_y==1031&&search_text.y+search_text.height/2==1031&&page_box.y+page_box.height/2==1031);
  assert(search_text.x-(icon_x+13)==8&&dot_x-2-search_text.x-search_text.width==16&&page_box.x-dot_x-2==16);
  assert(ui_rect_hit(hit,icon_x,icon_y)&&ui_rect_hit(hit,search_text.x+search_text.width-1,1031));
  assert(!ui_rect_hit(hit,dot_x,1031)&&!ui_rect_hit(hit,page_box.x+page_box.width/2,1031));
  for(int y=hit.y;y<hit.y+hit.height;++y)for(int x=hit.x;x<hit.x+hit.width;++x)assert(ui_pager_hit_at(x,y,1006)==UI_PAGER_NONE);
 }
 int initial_icon_x=icon_x,initial_dot_x=dot_x;
 for(int scale=80;scale<=200;scale+=20){test_scale=scale;assert(ui_text_effective_px(24)==26*scale/100);draw_shelf_pager(NULL,32,0,4);assert(!strcmp(label_seen,"01/04"));
  if(scale==80){initial_icon_x=icon_x;initial_dot_x=dot_x;}else assert(icon_x==initial_icon_x&&dot_x==initial_dot_x);
  assert(search_text.width==52&&page_box.width==32);
 }
 test_scale=100;
 s_pressed_control=112;draw_shelf_search_button(NULL,0,4);assert(icon_y==1033&&search_text.y+search_text.height/2==1033);
 s_pressed_control=-1;strcpy(s_query,"三体");draw_shelf_search_button(NULL,0,4);assert(underlines==1&&icon_y==1031);
 EpdRect close=shelf_search_clear_rect(0,4),all=shelf_search_rect(0,4);
 assert(close.width==40&&close.height==all.height&&close.x==all.x);
 assert(ui_rect_hit(close,icon_x,icon_y)&&!ui_rect_hit(close,search_text.x,1031));
 s_pressed_control=113;draw_shelf_search_button(NULL,0,4);assert(icon_y==1033&&underlines==2);s_pressed_control=-1;

 draw_search(NULL);assert(keyboard==1&&field==1);
 puts("PASS: 100-percent fixed footer across80-200-percent system sizes, zero-padded page labels, centred unframed search/text/dot/pager, arrow centreline, icon+text hit excluding dot/pager, separate X clear hit with editable underlined Search, press/active states and existing keyboard layout");
}
'''
action=function('action_at',b);control=function('control_at',b);paint=function('paint_control',b)
assert 'if(ui_rect_hit(shelf_search_rect(ctx->leaf,leaves()),x,y)){search_begin();return APP_REDRAW_PAGE;}' in action
assert action.index('if(ui_rect_hit(shelf_search_rect(')<action.index('int pager=ui_pager_hit')
assert action.index('return shelf_search_clear(ctx);')<action.index('if(ui_rect_hit(shelf_search_rect(')
assert 'return 113;' in control and 'shelf_search_clear_rect' in paint
assert 'if(s_view==SHELF&&ui_rect_hit(shelf_search_rect(ctx->leaf,leaves()),x,y)){*rect=shelf_search_rect(ctx->leaf,leaves());return 112;}' in control
assert 'shelf_search_rect(ctx->leaf,leaves());if(!memcmp(&rect,&search,sizeof(rect)))return shelf_key_paint(ctx);' in paint
with tempfile.TemporaryDirectory()as d:
 p=Path(d)/'test.c';p.write_text(u);out=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)

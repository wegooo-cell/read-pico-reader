#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：真实主键派发与书架页控制；包含箭头轮廓、按压、队列和中断。
# English: Actual main-key dispatch and shelf paging, including chevron contours, press feedback, queues and interruption.
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
r=Path(__file__).resolve().parents[1]
b=r/'main/apps/app_book.c'
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SHELF 1
#define UI_GRAY_WHITE 255
#define UI_KEY_1 0
#define UI_KEY_2 1
#define UI_KEY_3 2
typedef struct{int x,y,width,height;}EpdRect;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL}app_redraw_t;
struct app_desc_s;
typedef struct{int leaf;int64_t now_ms;bool consumed;const struct app_desc_s *request_app;uint8_t *fb;}app_ctx_t;
typedef struct app_desc_s{int tab;bool(*on_main_key)(app_ctx_t*,int,app_redraw_t*);}app_desc_t;
static bool s_shelf_key_mode,s_shelf_key_paint_pending,s_category_menu,s_sort_menu,s_clear_confirm,s_batch_confirm;
static int s_shelf_key_pressed,s_shelf_key_delta,s_view=SHELF,s_visible_count=20;
static int64_t s_shelf_key_until;
static EpdRect s_area;
static int outlines,presses,turns,requests=-1,s_pressed_control=-1;
enum {UI_PAGER_NONE=-1,UI_PAGER_FIRST,UI_PAGER_PREVIOUS,UI_PAGER_NEXT,UI_PAGER_LAST};
static int leaves(void){return 5;}
static EpdRect ui_pager_area(void){return(EpdRect){36,1006,612,50};}
static void ui_fill_rect_fast(EpdRect a,int c,uint8_t*f){(void)c;(void)f;assert(a.x==36&&a.y==1006&&a.width==612&&a.height==50);}
static void ui_pager_draw(uint8_t *fb,const char *label,int page,int pages,bool key_mode,int pressed){
 (void)fb;assert(page>=0&&page<pages&&pages==5&&!label[0]);if(key_mode)++outlines;if(pressed>=0){assert(pressed==UI_PAGER_PREVIOUS||pressed==UI_PAGER_NEXT);++presses;}
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx,int d){if(ctx->leaf+d<0||ctx->leaf+d>=leaves())return APP_REDRAW_NONE;ctx->leaf+=d;++turns;return APP_REDRAW_PAGE;}
static int app_index_of(const app_desc_t *p){return p->tab;}
static void ui_nav_request(app_ctx_t*c,int tab){(void)c;requests=tab;}
'''

u+=r'''
#define EPD_DRAW_ALIGN_CENTER 1
static EpdRect shelf_page_label_rect(int page,int pages){(void)page;(void)pages;return(EpdRect){280,1006,60,50};}
static void ui_text_fixed_vc(uint8_t*f,int x,int y,int px,const char*label,int align,bool inverse){(void)f;(void)align;(void)inverse;assert(x>204&&x<480&&y==1031&&px<=32&&strchr(label,'/'));}
static void draw_shelf_search_button(uint8_t*f,int page,int pages){(void)f;(void)page;(void)pages;}
static EpdRect shelf_pager_group_rect(int page,int pages){(void)page;(void)pages;return(EpdRect){240,1004,204,54};}
static int ui_text_fixed_width_px(int px,const char*text){(void)text;return px*2;}
static void epd_fill_circle(int x,int y,int radius,int color,uint8_t*fb){(void)x;(void)fb;assert(y==1031&&radius==2&&color==0x80);}
'''
for n in ('draw_shelf_pager','shelf_key_paint','shelf_main_key','shelf_key_tick'):u+=function(n,b)+'\n'
u+=function('main_key_action',r/'main/app/app_loop.c')+'\n'
u+=r'''
int main(void){app_ctx_t ctx={0};
 for(int tab=0;tab<4;++tab){app_desc_t p={tab,NULL};
  requests=-1;assert(main_key_action(&ctx,&p,UI_KEY_1,false)==APP_REDRAW_NONE&&requests==(tab+3)%4);
  assert(main_key_action(&ctx,&p,UI_KEY_3,false)==APP_REDRAW_NONE&&requests==(tab+1)%4);
  requests=-1;assert(main_key_action(&ctx,&p,UI_KEY_2,true)==APP_REDRAW_FULL&&requests==-1);
 }
 app_desc_t shelf={1,shelf_main_key};requests=-1;
 assert(main_key_action(&ctx,&shelf,UI_KEY_2,false)==APP_REDRAW_AREA&&s_shelf_key_mode&&outlines==1);
 assert(main_key_action(&ctx,&shelf,UI_KEY_3,false)==APP_REDRAW_AREA&&s_shelf_key_delta==1&&presses==1);
 assert(shelf_key_tick(&ctx)==APP_REDRAW_NONE&&!turns);ctx.now_ms=40;
 assert(shelf_key_tick(&ctx)==APP_REDRAW_PAGE&&turns==1&&ctx.leaf==1&&!s_shelf_key_pressed);
 assert(main_key_action(&ctx,&shelf,UI_KEY_2,true)==APP_REDRAW_FULL&&s_shelf_key_mode);
 // 连续快按不丢翻页，反向抵消不误翻，边界仍被模式消费。
 // Rapid presses retain turns, opposite requests cancel, and boundary presses stay consumed by paging mode.
 main_key_action(&ctx,&shelf,UI_KEY_3,false);main_key_action(&ctx,&shelf,UI_KEY_3,false);ctx.now_ms=80;
 assert(shelf_key_tick(&ctx)==APP_REDRAW_PAGE&&ctx.leaf==2);ctx.now_ms=120;
 assert(shelf_key_tick(&ctx)==APP_REDRAW_PAGE&&ctx.leaf==3);
 main_key_action(&ctx,&shelf,UI_KEY_1,false);main_key_action(&ctx,&shelf,UI_KEY_3,false);ctx.now_ms=160;
 assert(shelf_key_tick(&ctx)==APP_REDRAW_AREA&&ctx.leaf==3);
 ctx.leaf=4;main_key_action(&ctx,&shelf,UI_KEY_3,false);ctx.now_ms=200;requests=-1;
 assert(shelf_key_tick(&ctx)==APP_REDRAW_AREA&&ctx.leaf==4&&requests==-1);
 main_key_action(&ctx,&shelf,UI_KEY_1,false);ctx.request_app=&shelf;
 assert(shelf_key_tick(&ctx)==APP_REDRAW_NONE&&!s_shelf_key_mode&&!s_shelf_key_delta);
 ctx.request_app=NULL;main_key_action(&ctx,&shelf,UI_KEY_2,false);main_key_action(&ctx,&shelf,UI_KEY_1,false);
 main_key_action(&ctx,&shelf,UI_KEY_2,false);assert(!s_shelf_key_mode&&!s_shelf_key_delta);
 s_view=9;app_redraw_t redraw;assert(!shelf_main_key(&ctx,UI_KEY_2,&redraw));
 puts("PASS: four-root cycling/wrap, shared middle-hold full refresh, shelf mode chevron-only contours and local pressed arrows, queued rapid turns, opposite cancellation, boundary consumption and navigation/subpage interruption");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(u);out=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)

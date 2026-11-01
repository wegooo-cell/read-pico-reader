#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""标准阅读标题操作与返回后常驻工具栏回归。/ Standard header actions and persistent tools after return."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_reader_layout.h"
typedef struct {uint8_t *fb;int leaf;int64_t now_ms;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_AREA,APP_REDRAW_FULL} app_redraw_t;
enum {READING,TOC,READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_FONT_SETTINGS,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_STATS,MODE_GL16};
typedef enum {APP_READER_KEY_PREV,APP_READER_KEY_NEXT,APP_READER_KEY_HOME,APP_READER_KEY_FULLSCREEN,APP_READER_KEY_REFRESH,APP_READER_KEY_TOOLS} app_reader_key_action_t;
#define UI_LOCK_WIDTH 684
#define BOOK_TOOL_COUNT 5
static bool s_reader_fullscreen,s_clear_confirm,s_reader_favorite,save_fail,mode_fail;
static const char *s_text="body";
static int64_t s_stats_activity_ms;
static int s_reader_panel=READER_PANEL_NONE,s_footnote_note=-1,s_mode,s_view=READING,s_line_spacing,s_reader_slider;
static bool s_toc_jump_open,s_toc_jump_drag;
static struct {bool pending;} s_reader_tap;
static const char s_path[]="/sdcard/book.epub";
static struct {char path[64];bool favorite;} s_shelf[2];
static int s_count=2;
static EpdRect s_area;
static char s_reader_notice[96],s_message[32];
static int64_t s_reader_notice_until;
static unsigned saves,sorts,paints,returns,bookmarks,progress,stats;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static bool favorite_save(const char *path,bool value){assert(!strcmp(path,s_path)&&value!=s_reader_favorite);++saves;return !save_fail;}
static void copy_text(char *out,size_t cap,const char *s){snprintf(out,cap,"%s",s);}
static int64_t esp_timer_get_time(void){return 1000000;}
static void sort_shelf(app_ctx_t *ctx){(void)ctx;++sorts;}
static void render(app_ctx_t *ctx,uint8_t *fb){assert(ctx->fb==fb);++paints;}
static app_redraw_t reader_return(app_ctx_t *ctx){(void)ctx;++returns;return APP_REDRAW_PAGE;}
static void pause_reader_prep(void){}
static void invalidate_prep(void){}
static void save_progress(void){++progress;}
static void set_reader_view(int view){s_view=view;}
static size_t current_toc_position(void){return 4;}
static int book_toc_open(size_t position){assert(position==4);return 2;}
static bool bookmark_toggle(void){++bookmarks;return true;}
static void reader_chapter_jump_begin(void){++stats;s_reader_panel=READER_PANEL_STATS;}
static int app_settings_book_line_spacing(void){return 130;}
static app_redraw_t toggle_reader_fullscreen(app_ctx_t *ctx){(void)ctx;if(mode_fail)return APP_REDRAW_NONE;s_reader_fullscreen=!s_reader_fullscreen;return APP_REDRAW_FULL;}
static app_redraw_t turn_page(app_ctx_t *ctx,int direction){(void)ctx;(void)direction;return APP_REDRAW_NONE;}
static app_redraw_t reader_manual_refresh(app_ctx_t *ctx){(void)ctx;return APP_REDRAW_FULL;}

'''
for name in ('tool_rect', 'reader_tools_visible', 'reader_header_action', 'reader_key_action'):
    unit += function(name, BOOK) + '\n'
panel = function('reader_panel_action', BOOK)
a = panel.index('    if (reader_tools_visible()) {')
b = panel.index('    if (s_reader_panel == READER_PANEL_BOOKMARKS)', a)
unit += 'static app_redraw_t tool_action(app_ctx_t *ctx,int x,int y){\n' + panel[a:b] + 'return APP_REDRAW_NONE;}\n'
unit += r'''
static app_redraw_t tap_tool(app_ctx_t *ctx,int i){EpdRect r=tool_rect(i);return tool_action(ctx,r.x+r.width/2,r.y+r.height/2);}
int main(void){
 uint8_t fb=0;app_ctx_t ctx={.fb=&fb};bool handled;
 strcpy(s_shelf[0].path,s_path);strcpy(s_shelf[1].path,"/sdcard/other.epub");
 for(int panel=READER_PANEL_NONE;panel<=READER_PANEL_TOOLS;++panel){
  s_reader_panel=panel;s_reader_tap.pending=true;
  assert(reader_header_action(&ctx,620,114,&handled)==APP_REDRAW_AREA&&handled&&!s_reader_tap.pending);
  assert(s_reader_favorite==s_shelf[0].favorite&&!s_shelf[1].favorite);
  assert(s_area.x==584&&s_area.y==80&&s_area.width==64&&s_area.height==70&&s_mode==MODE_GL16);
 }
 assert(saves==2&&sorts==2&&paints==2&&!s_reader_favorite);
 save_fail=true;assert(reader_header_action(&ctx,620,114,&handled)==APP_REDRAW_PAGE&&handled);
 assert(!s_reader_favorite&&!s_shelf[0].favorite&&sorts==2&&paints==2&&s_reader_notice_until==2800&&s_reader_notice[0]);save_fail=false;
 assert(reader_header_action(&ctx,58,114,&handled)==APP_REDRAW_PAGE&&handled&&returns==1);
 assert(reader_header_action(&ctx,342,114,&handled)==APP_REDRAW_NONE&&!handled);
 for(int y=0;y<176;++y)if(y<80||y>=150)assert(reader_header_action(&ctx,620,y,&handled)==APP_REDRAW_NONE&&!handled);
 s_reader_fullscreen=true;assert(reader_header_action(&ctx,620,114,&handled)==APP_REDRAW_NONE&&!handled);s_reader_fullscreen=false;
 s_reader_panel=READER_PANEL_FONT_SETTINGS;assert(reader_header_action(&ctx,58,114,&handled)==APP_REDRAW_NONE&&!handled);
 s_reader_panel=READER_PANEL_NONE;s_clear_confirm=true;assert(reader_header_action(&ctx,620,114,&handled)==APP_REDRAW_NONE&&!handled);s_clear_confirm=false;
 s_footnote_note=0;assert(reader_header_action(&ctx,58,114,&handled)==APP_REDRAW_NONE&&!handled);s_footnote_note=-1;
 // 目录返回后为NONE，工具栏仍可点击；设置完成后也能再次打开其他入口。
 // Returning from TOC leaves NONE: persistent tools must still work, as after dismissing settings.
 s_reader_panel=READER_PANEL_TOOLS;assert(tap_tool(&ctx,0)==APP_REDRAW_PAGE&&s_view==TOC&&ctx.leaf==2&&progress==1);
 s_view=READING;s_reader_panel=READER_PANEL_NONE;assert(reader_tools_visible());
 assert(tap_tool(&ctx,1)==APP_REDRAW_PAGE&&bookmarks==1);
 assert(tap_tool(&ctx,3)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_REFRESH_SETTINGS);
 s_reader_panel=READER_PANEL_NONE;assert(tap_tool(&ctx,4)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_FONT_SETTINGS&&s_line_spacing==130);
 s_reader_panel=READER_PANEL_NONE;assert(tap_tool(&ctx,2)==APP_REDRAW_PAGE&&stats==1);
 for(int full=0;full<2;++full)for(int panel=READER_PANEL_NONE;panel<=READER_PANEL_STATS;++panel){
  s_reader_fullscreen=full;s_reader_panel=panel;
  assert(reader_tools_visible()==(!full&&(panel==READER_PANEL_TOOLS||panel==READER_PANEL_NONE)));
 }
 s_view=READING;s_reader_panel=READER_PANEL_NONE;s_reader_fullscreen=true;
 assert(reader_key_action(&ctx,APP_READER_KEY_TOOLS)==APP_REDRAW_FULL&&!s_reader_fullscreen&&reader_tools_visible());
 assert(reader_key_action(&ctx,APP_READER_KEY_FULLSCREEN)==APP_REDRAW_FULL&&s_reader_fullscreen&&!reader_tools_visible());
 mode_fail=true;assert(reader_key_action(&ctx,APP_READER_KEY_TOOLS)==APP_REDRAW_NONE&&s_reader_fullscreen&&!reader_tools_visible());
 puts("PASS: Back/Favorite bounds, persistence failure, modal guards, precise favorite crop, TOC-return bookmark and settings-to-font toolbar actions");
}
'''
action = function('action_at', BOOK)
assert action.index('reader_header_action(ctx,x,y,&header_handled)') < action.index('reader_footnote_hit')
assert 'reader_tools_visible()' in function('draw_reader_panel', BOOK)
assert 'reader_tools_visible()' in function('control_at', BOOK)
with tempfile.TemporaryDirectory(prefix='reader-header-') as temp:
    folder = Path(temp)
    (folder / 'epdiy.h').write_text('#pragma once\ntypedef struct{int x,y,width,height;}EpdRect;\n')
    (folder / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-I' + str(folder), '-I' + str(ROOT / 'main/ui'),
                    str(folder / 'test.c'), '-o', str(folder / 'test')], check=True)
    subprocess.run([str(folder / 'test')], check=True)

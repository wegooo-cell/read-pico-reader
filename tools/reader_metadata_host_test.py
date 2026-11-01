#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实信息栏绘制、章数及延迟局刷回归。/ English: Actual metadata paint, chapter counts and deferred partial updates."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
source = ROOT/'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_reader_layout.h"
enum {READING,SHELF,READER_PANEL_NONE,READER_PANEL_OTHER,MODE_DU,EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_RIGHT};
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
typedef enum {APP_REDRAW_NONE,APP_REDRAW_DONE} app_redraw_t;
typedef struct {bool touched;} touch_t;
typedef struct {void *hl;int64_t now_ms;bool consumed,request_app,request_return,request_menu;touch_t *touch;} app_ctx_t;
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,E0470_FOLLOW_WAVEFORM;
static bool immersive;
static bool app_settings_reader_immersive(void){return immersive;}
static bool s_reader_fullscreen=false,s_toolbar,s_clear_confirm,s_reader_footer_pending,s_save_failed;
static const char *s_text="body";
static char s_book_title[128]="Book title";
static size_t s_chapter=36,total_chapters=48;
static int64_t s_reader_footer_due_ms;
static int calls,guards,text_calls,bar_calls,fill_calls,status_calls;
static void ui_nav_status(uint8_t *fb){(void)fb;++status_calls;}

static enum EpdDrawError error;
static const char *expected_chapter="Chapter name";
static unsigned percent(size_t page){return (unsigned)page;}
static size_t book_chapter_count(void){return total_chapters;}
static void copy_text(char *out,size_t cap,const char *text){snprintf(out,cap,"%s",text);}
static void fit_fixed_text(char *text,int px,int width){assert(px>0&&width>0);(void)text;}
static int ui_text_fixed_width_px(int px,const char *text){return (int)strlen(text)*px/2;}
static void reader_footer_chapter_name(char *text,size_t cap){snprintf(text,cap,"%s",expected_chapter);}
static void ui_fill_rect_fast(EpdRect r,int color,uint8_t *fb){(void)fb;assert(color==255);if(s_reader_fullscreen&&immersive)assert(r.y==1204&&r.height==12);else assert(r.y==0||r.y==(s_reader_fullscreen?1168:1048));++fill_calls;}
static void ui_reader_background_paint(uint8_t *fb,EpdRect r,bool overlay){(void)fb;(void)r;assert(overlay);}
static void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inverted){
 (void)fb;assert(x==32&&y==16&&px==25&&!strcmp(text,s_book_title)&&align==EPD_DRAW_ALIGN_LEFT&&!inverted);++text_calls;
}
static void ui_text_fixed_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inverted){
 (void)fb;assert(y==(s_reader_fullscreen?1180:1060)&&px==21&&!inverted);
 if(x==652){assert(align==EPD_DRAW_ALIGN_RIGHT&&!strcmp(text,"37/48  74%"));}
 else assert(x==32&&align==EPD_DRAW_ALIGN_LEFT&&!strcmp(text,expected_chapter));
 ++text_calls;
}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int color){
 (void)fb;int width=(s_reader_fullscreen&&immersive)?612:648;
 assert(r.x==((s_reader_fullscreen&&immersive)?36:18)&&r.y==((s_reader_fullscreen&&immersive)?1207:(s_reader_fullscreen?1194:1074))&&r.height==((s_reader_fullscreen&&immersive)?3:6)&&radius==((s_reader_fullscreen&&immersive)?1:3));
 assert(r.width==(color?width:width*74/100));++bar_calls;
}
static enum EpdDrawError reader_present_crop(void *hl,const int *wave,int mode,EpdRect area,bool full){
 (void)hl;assert(wave==&E0470_FOLLOW_WAVEFORM&&mode==MODE_DU&&!full);
 assert(area.x==0&&area.y==((s_reader_fullscreen&&immersive)?1204:(s_reader_fullscreen?1168:1048))&&area.width==684&&area.height==((s_reader_fullscreen&&immersive)?12:48));++calls;return error;
}

static void guard_draw_result(void *hl,enum EpdDrawError result){(void)hl;assert(result==error);++guards;}
'''
for name in ['draw_reader_header','progress_rect','reader_footer_progress', 'draw_reader_metadata', 'reader_metadata_tick','reader_status_visible']:
    unit += function(name, source)+'\n'
unit += r'''
int main(void){
 char text[48];reader_footer_progress(text,sizeof(text),74);assert(!strcmp(text,"37/48  74%"));
 total_chapters=0;reader_footer_progress(text,sizeof(text),0);assert(!strcmp(text,"0/0  0%"));total_chapters=48;
 s_chapter=99;reader_footer_progress(text,sizeof(text),100);assert(!strcmp(text,"48/48  100%"));s_chapter=36;
 s_reader_fullscreen=false;uint8_t none=0;
 for(int style=0;style<2;++style){
  immersive=style;text_calls=bar_calls=fill_calls=status_calls=0;
  draw_reader_header(&none);draw_reader_metadata(&none,74);
  assert(status_calls==1&&text_calls==2&&bar_calls==2&&fill_calls==1&&reader_status_visible());
  app_ctx_t ctx={.now_ms=1000};s_reader_footer_pending=true;s_reader_footer_due_ms=900;
  assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&!s_reader_footer_pending);
 }
 for(int full=0;full<2;++full){
 s_reader_fullscreen=true;immersive=full;s_reader_footer_pending=false;s_view=READING;calls=guards=text_calls=bar_calls=fill_calls=0;
 assert(!reader_status_visible());
 uint8_t fb=0;draw_reader_metadata(&fb,74);assert(text_calls==(full?0:3)&&bar_calls==2&&fill_calls==(full?1:2));
 touch_t touch={0};app_ctx_t ctx={.now_ms=100,.touch=&touch};
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&!calls);
 s_reader_footer_pending=true;s_reader_footer_due_ms=900;
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&!calls);
 ctx.now_ms=900;
 for(int condition=0;condition<8;++condition){
  ctx.consumed=condition==0;ctx.request_app=condition==1;ctx.request_return=condition==2;ctx.request_menu=condition==3;
  touch.touched=condition==4;s_reader_panel=condition==5?READER_PANEL_OTHER:READER_PANEL_NONE;
  s_toolbar=condition==6;s_clear_confirm=condition==7;
  assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&s_reader_footer_pending&&!calls);
 }
 ctx.consumed=ctx.request_app=ctx.request_return=ctx.request_menu=touch.touched=s_toolbar=s_clear_confirm=false;
 s_reader_panel=READER_PANEL_NONE;
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&calls==1&&guards==1&&!s_reader_footer_pending);
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&calls==1);
 s_reader_footer_pending=true;error=EPD_DRAW_OTHER_ERROR;
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&s_reader_footer_pending&&s_reader_footer_due_ms==1700);
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&calls==2);
 ctx.now_ms=1700;error=EPD_DRAW_SUCCESS;assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&!s_reader_footer_pending&&calls==3);

 s_reader_footer_pending=true;s_view=SHELF;assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&!s_reader_footer_pending);
 s_view=READING;s_reader_footer_pending=true;s_text=NULL;assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&!s_reader_footer_pending);s_text="body";
 }
 puts("PASS: OFF title/chapter/count/percentage with fixed1194/downward6px bar; ON release1207/3px line without text/header, no minute overwrite; idle deadline, touch/navigation/panel guards, one FOLLOW DU footer crop, retry and exit cancellation");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-metadata-') as temporary:
    work=Path(temporary)
    (work/'epdiy.h').write_text('#pragma once\ntypedef struct{int x,y,width,height;}EpdRect;\n')
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-DKIIKO_BOARD_METALIO47='+str(int('metalio47' in str(ROOT))),'-I'+str(work),'-I'+str(ROOT/'main/ui'),str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)

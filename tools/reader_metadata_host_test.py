#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实信息栏绘制、本章页码及延迟局刷回归。/ English: Actual metadata paint, chapter page counts and deferred partial updates."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
source = ROOT/'main/apps/app_book.c'
separate_footer = 'static bool present_reader_footer(' in source.read_text()
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_reader_layout.h"
enum {READING,SHELF,READER_PANEL_NONE,READER_PANEL_OTHER,READER_PANEL_TOUCH_SETTINGS,READER_PANEL_TOUCH_MORE,MODE_DU,EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_RIGHT,EPD_DRAW_ALIGN_CENTER,UI_ICON_BOOK_BOOKMARK,UI_ICON_BOOK,UI_ICON_EXPAND};
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
typedef enum {APP_REDRAW_NONE,APP_REDRAW_DONE} app_redraw_t;
typedef struct {bool touched;} touch_t;
typedef struct {void *hl;uint8_t *fb;int64_t now_ms;bool consumed,request_app,request_return,request_menu;touch_t *touch;} app_ctx_t;
#define UI_LOCK_WIDTH 684
#define UI_GRAY_LIGHT 192
#define READER_TOOL_ICON_PX 40
#define UI_CLICK_BACK_SMALL 77
#define UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT 78
#define UI_GRAY_WHITE 255
#define UI_NAV_BACKGROUND_GRAY 0xe0
#define UI_GRAY_BLACK 0
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,E0470_FOLLOW_WAVEFORM;
static bool immersive;
static bool app_settings_reader_immersive(void){return immersive;}
static bool s_reader_fullscreen=false,s_toolbar,s_clear_confirm,s_reader_footer_pending,s_save_failed;
static const char *s_path="/sdcard/book.txt";
typedef struct{bool dirty,progress_saved;}pending_progress_t;
static pending_progress_t current_progress;
static bool has_current_progress;
static unsigned warnings,warning_ink;
static pending_progress_t *pending_find(const char *path){assert(!strcmp(path,s_path));return has_current_progress?&current_progress:NULL;}
static void ui_draw_control_frame(uint8_t *fb,EpdRect r,int radius,int gray){(void)fb;assert(r.x==8&&r.width==16&&r.height==16&&radius==5&&gray==0);++warnings;}
static const char *s_text="body";
static size_t s_chapter=36,total_pages=100;
static int64_t s_reader_footer_due_ms;
static int calls,guards,text_calls,bar_calls,fill_calls,status_calls;
static void ui_nav_status(uint8_t *fb){(void)fb;++status_calls;}
static char s_book_title[128]="Book title",s_title[128]="Fallback";
static bool s_reader_favorite;
static unsigned title_calls,back_calls,favorite_calls;
static void ui_click_feedback_register(uint8_t *fb,EpdRect r,int kind,int icon){(void)fb;assert(r.x==26&&r.y==82&&r.width==64&&r.height==64&&kind==UI_CLICK_BACK_SMALL&&icon==UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT&&!s_reader_fullscreen);++back_calls;}
static int ui_text_title_fit(char *s,int px,int width,const char *sample){assert(px==32&&width==340&&!strcmp(s,sample));return px;}
static int ui_text_title_width_px(int px,const char*t,const char*s){(void)px;(void)t;(void)s;return 24;}
static void ui_text_title_vc(uint8_t *fb,int x,int y,int px,const char *text,const char *sample,int align){(void)fb;assert(x==342&&y==114&&px==32&&align==EPD_DRAW_ALIGN_CENTER&&!strcmp(text,sample)&&!s_reader_fullscreen);++title_calls;}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){(void)fb;if(icon==UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT){assert(x==58&&y==114&&px==40&&gray==0x30);return;}if(icon==UI_ICON_EXPAND){assert(x==620&&y==114&&px==40&&!s_reader_fullscreen);return;}assert(x==552&&y==114&&px==40&&gray==0x38&&icon==(s_reader_favorite?UI_ICON_BOOK_BOOKMARK:UI_ICON_BOOK)&&!s_reader_fullscreen);++favorite_calls;}
static void ui_hairline(uint8_t *fb,int y,int x,int width,int gray){(void)fb;assert(y==151&&x==36&&width==612&&gray==UI_GRAY_LIGHT);}


static enum EpdDrawError error;
static const char *expected_chapter="Chapter name";
static size_t s_part_base;
static bool s_part_more;
static unsigned percent(size_t page){return (unsigned)page;}
static size_t book_layout_page_count(void){return total_pages;}
static void fit_fixed_text(char *text,int px,int width){assert(px>0&&width>0);(void)text;}
static int ui_text_fixed_width_px(int px,const char *text){return (int)strlen(text)*px/2;}
static void reader_footer_chapter_name(char *text,size_t cap){snprintf(text,cap,"%s",expected_chapter);}
static void ui_fill_rect_fast(EpdRect r,int color,uint8_t *fb){(void)fb;if(r.width==2){assert(color==0&&r.x==15&&(r.height==2||r.height==6));++warning_ink;return;}assert(color==255||color==UI_NAV_BACKGROUND_GRAY);if(s_reader_fullscreen&&immersive)assert(r.y==1204&&r.height==12);else if(r.y==0)assert(r.height==(s_reader_fullscreen?80:152));else if(!s_reader_fullscreen&&r.y==152)assert(r.height==24&&r.width==684&&color==UI_GRAY_WHITE);else assert(r.y==(s_reader_fullscreen?1168:1048));++fill_calls;}
static void ui_reader_background_paint(uint8_t *fb,EpdRect r,bool overlay){(void)fb;(void)r;assert(overlay);}
static void ui_text_fixed_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inverted){
 (void)fb;assert(y==(s_reader_fullscreen?1180:1060)&&px==21&&!inverted);
 if(x==652){assert(align==EPD_DRAW_ALIGN_RIGHT&&!strcmp(text,"75/100  74%"));}
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
static size_t s_page=74;
static void lock_draw(void){}
static void unlock_draw(void){}
static void ui_text_set_system_scale(bool scale){assert(!scale);}
'''
for name in ['draw_reader_header','progress_rect','reader_footer_progress']:
    unit += function(name, source)+'\n'
if separate_footer:
    unit += r'''
static int64_t test_now_ms;
static int64_t esp_timer_get_time(void){return test_now_ms*1000;}
// 这里隔离已单测的像素差分，只验证实际页脚绘制与提交状态机。
// Isolate pixel differencing and exercise the actual footer paint/submission state machine.
static EpdRect reader_footer_dirty(void *hl){(void)hl;return progress_rect();}
static enum EpdDrawError update_display_reader_area(void *hl,const int *wave,int mode,EpdRect area,bool full){return reader_present_crop(hl,wave,mode,area,full);}
'''
unit += function('reader_current_progress_unsaved', source)+'\n'
unit += function('draw_reader_footer', source)+'\n'
unit += function('draw_reader_metadata', source)+'\n'
if separate_footer:
    unit += function('present_reader_footer', source)+'\n'
for name in ['reader_metadata_tick','app_book_status_bar_visible','reader_status_visible']:
    unit += function(name, source)+'\n'
unit += r'''
int main(void){
 uint8_t fb[1]={0};
 // A failed record for another book or for the resume pointer cannot displace this chapter.
 s_save_failed=true;
 for(int full=0;full<2;++full)for(int mode=0;mode<5;++mode){
  s_reader_fullscreen=full;immersive=false;warnings=warning_ink=0;
  has_current_progress=mode!=0;current_progress.dirty=mode>=2;current_progress.progress_saved=mode==3;
  draw_reader_footer(fb,74);
  assert(warnings==(unsigned)(mode==2||mode==4)&&warning_ink==2*warnings);
 }
 has_current_progress=false;s_save_failed=false;warnings=warning_ink=0;
 s_reader_fullscreen=false;calls=guards=text_calls=bar_calls=fill_calls=0;
 char text[48];reader_footer_progress(text,sizeof(text),74);assert(!strcmp(text,"75/100  74%"));
 total_pages=0;reader_footer_progress(text,sizeof(text),0);assert(!strcmp(text,"0/0  0%"));total_pages=100;
 s_chapter=99;reader_footer_progress(text,sizeof(text),100);assert(!strcmp(text,"100/100  100%"));s_chapter=36;
 s_reader_fullscreen=false;uint8_t none=0;
 for(int style=0;style<2;++style){
  immersive=style;s_reader_favorite=style;text_calls=bar_calls=fill_calls=status_calls=0;title_calls=back_calls=favorite_calls=0;
  draw_reader_header(&none);draw_reader_metadata(&none,74);
  assert(status_calls==1&&text_calls==2&&bar_calls==2&&fill_calls==3&&reader_status_visible());
  assert(title_calls==1&&back_calls==1&&favorite_calls==1&&ui_reader_body(false,style,24,false).y==(style?16:84));
  app_ctx_t ctx={.now_ms=1000};s_reader_footer_pending=true;s_reader_footer_due_ms=900;
  assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&!s_reader_footer_pending);
 }
 for(int full=0;full<2;++full){
 s_reader_fullscreen=true;immersive=full;s_reader_footer_pending=false;s_view=READING;calls=guards=text_calls=bar_calls=fill_calls=0;
 assert(reader_status_visible()==!full);status_calls=0;title_calls=back_calls=favorite_calls=0;
 uint8_t fb=0;draw_reader_metadata(&fb,74);assert(text_calls==(full?0:2)&&bar_calls==2&&fill_calls==(full?1:2)&&status_calls==(full?0:1));
 assert(ui_reader_body(true,false,24,false).y==84&&ui_reader_header().height==80&&!title_calls&&!back_calls&&!favorite_calls);
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
 const int before_footer=text_calls;
 expected_chapter="Next chapter";
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&calls==1&&guards==1&&!s_reader_footer_pending);
 assert(text_calls==before_footer+(full?0:2));
 expected_chapter="Chapter name";
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&calls==1);
 s_reader_footer_pending=true;error=EPD_DRAW_OTHER_ERROR;
#ifdef TEST_SEPARATE_FOOTER
 test_now_ms=ctx.now_ms;
#endif
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&s_reader_footer_pending&&s_reader_footer_due_ms==TEST_RETRY_DUE);
 assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&calls==2);
 ctx.now_ms=TEST_RETRY_DUE;error=EPD_DRAW_SUCCESS;assert(reader_metadata_tick(&ctx)==APP_REDRAW_DONE&&!s_reader_footer_pending&&calls==3);

 s_reader_footer_pending=true;s_view=SHELF;assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&!s_reader_footer_pending);
 s_view=READING;s_reader_footer_pending=true;s_text=NULL;assert(reader_metadata_tick(&ctx)==APP_REDRAW_NONE&&!s_reader_footer_pending);s_text="body";
 }
 puts("PASS: OFF status/chapter/page-count/percentage with fixed1194/downward6px bar; ON release1207/3px line without text/header or minute overwrite; idle deadline, touch/navigation/panel guards, one FOLLOW DU footer crop, retry and exit cancellation");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-metadata-') as temporary:
    work=Path(temporary)
    (work/'epdiy.h').write_text('#pragma once\ntypedef struct{int x,y,width,height;}EpdRect;\n')
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    *(['-DTEST_SEPARATE_FOOTER'] if separate_footer else []),
                    '-DTEST_RETRY_DUE='+str(1050 if separate_footer else 1700),
                    '-DKIIKO_BOARD_METALIO47='+str(int('metalio47' in str(ROOT))),'-I'+str(work),'-I'+str(ROOT/'main/ui'),str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：执行真实间距滑杆、重排与设置保存，覆盖旧上限回跳及重排失败回退。
# English: Run actual spacing input, reflow and persistence, covering stale bounds and failed-layout restoration.
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
reader = root / 'main/apps/app_book.c'
settings = root / 'main/settings.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "book/book_font_size.h"
#define BOOK_PX_MIN BOOK_READER_PX_MIN
#define BOOK_PX_MAX BOOK_READER_PX_MAX
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int unused;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
static int s_px=36,s_margin=24,s_margin_y,s_reader_slider=-1;
static int s_reader_preview_px,s_reader_preview_margin,s_reader_preview_margin_y,s_reader_preview_line;
static int s_reader_preview_para,s_reader_preview_tracking;
static bool s_reader_slider_endpoint,fail_layout;
static uint8_t s_book_tracking=15;static int s_book_para=30;
static unsigned writes,saves,builds,invalidations;
static char s_text_storage[1],*s_text=s_text_storage;
static size_t s_text_len=1,s_page=2,s_block_count;
static const void *s_blocks;
static int typography;
static uint8_t stored_tracking=207;static int stored_para=30;
#define NVS_KEY_BOOK_TRACK "bk_track"
#define NVS_KEY_BOOK_PARA "bk_para"
static void nvs_put_u8(const char *key,uint8_t value){
 ++writes;
 if(!strcmp(key,NVS_KEY_BOOK_TRACK))stored_tracking=value;
 else {assert(!strcmp(key,NVS_KEY_BOOK_PARA));stored_para=value;}
}
static uint8_t app_settings_book_line_spacing(void){return 130;}
static int reader_ui_extra(int pixels){(void)pixels;return 0;}
#define BOOK_MARGIN_CHOICES 9
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 48
#define UI_LOCK_WIDTH 684
enum {READER_PANEL_FONT_SETTINGS, READER_PANEL_LAYOUT_SETTINGS};
static int s_reader_panel=READER_PANEL_LAYOUT_SETTINGS;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static EpdRect body_rect_for_tracking(int tracking){
 (void)tracking;return (EpdRect){24,188,636,940};
}
static EpdRect body_rect(void){return body_rect_for_tracking(s_book_tracking);}
static size_t book_layout_page_start_offset(size_t page){return page*100;}
static size_t book_layout_page_for_offset(size_t off){return off/100;}
static void lock_draw(void){}
static void unlock_draw(void){}
static void invalidate_prep(void){++invalidations;}
static void book_layout_set_typography(int pixels){typography=pixels;}
static bool book_layout_build_blocks(const char *text,size_t length,const void *blocks,size_t count,EpdRect rect,int px){
 assert(text==s_text&&length==s_text_len&&blocks==s_blocks&&count==s_block_count);
 assert(rect.width==636&&px==s_px);++builds;return !fail_layout;
}
static void save_progress(void){++saves;}
'''
unit+=r'''
#define NVS_KEY_BOOK_PARA_FULL "bk_para_full"
#define s_book_px s_px
#define s_book_margin_y s_margin_y
#define s_book_margin s_margin
#define s_book_line 130
static bool app_settings_try_book_layout(uint8_t px,uint8_t margin,int line,int para, uint8_t margin_y){(void)margin_y;(void)px;(void)margin;(void)line;++writes;stored_para=s_book_para=para;return true;}
'''
header = (root / 'main/settings.h').read_text()
for name in ('APP_BOOK_TRACK_MIN', 'APP_BOOK_TRACK_MAX', 'APP_BOOK_TRACK_COUNT', 'APP_BOOK_PARAGRAPH_COUNT'):
    unit += re.search(r'^#define ' + name + r' .*', header, re.M)[0] + '\n'
for name in ('app_settings_book_paragraph_value', 'app_settings_book_paragraph_level',
             'app_settings_book_tracking', 'app_settings_set_book_tracking',
             'app_settings_book_paragraph_spacing', 'app_settings_set_book_paragraph_spacing'):
    unit += function(name, settings) + '\n'
unit += r'''
static app_redraw_t apply_reader_layout(app_ctx_t *ctx,int px,int margin,int line,int para, int margin_y){(void)margin_y;
 (void)ctx;assert(px==s_px&&line==130);
 if(fail_layout)return APP_REDRAW_NONE;
 s_margin=margin;s_margin_y=margin_y;
 app_settings_set_book_paragraph_spacing(para);save_progress();return APP_REDRAW_PAGE;
}
'''
for name in ('apply_reader_typography', 'slider_index', 'reader_slider_rect',
             'reader_slider_at','reader_margin_width','reader_margin_levels','reader_margin_level_for',
             'reader_margin_for_level','reader_margin_input','reader_slider_map', 'reader_slider_begin', 'reader_slider_move', 'reader_slider_commit'):
    unit += function(name, reader) + '\n'
unit += r'''
int main(void){
 app_ctx_t ctx={0};
 // 行距/段距/字间距占整行，边距成对；所有滑杆互不重叠。/ Line, paragraph and tracking rails span full rows; margins pair; no rails overlap.
 for(int i=2;i<=4;++i)assert(reader_slider_rect(i).x==36&&reader_slider_rect(i).width==612);
 assert(reader_slider_rect(1).y==reader_slider_rect(5).y&&reader_slider_rect(1).width==294&&reader_slider_rect(5).width==294);
 for(int a=1;a<=5;++a)for(int b=a+1;b<=5;++b){EpdRect A=reader_slider_rect(a),B=reader_slider_rect(b);
  assert(A.x+A.width<=B.x||B.x+B.width<=A.x||A.y+A.height<=B.y||B.y+B.height<=A.y);}
 for(int axis=0;axis<2;++axis){
  int slider=axis?5:1; EpdRect r=reader_slider_rect(slider);
  assert(reader_slider_at(r.x+r.width/2,r.y+r.height/2)==slider);
  for(int i=0;i<=8;++i){
   int other=axis?s_margin:s_margin_y;
   reader_slider_begin(slider,r.x+r.width/2);
   reader_slider_move(r.x+64+(i*(r.width-128)+4)/8);
   assert((axis?s_reader_preview_margin_y:s_reader_preview_margin)==6*i);
   reader_slider_commit(&ctx);
   assert((axis?s_margin_y:s_margin)==6*i&&(axis?s_margin:s_margin_y)==other);
  }
  reader_slider_begin(slider,r.x);fail_layout=true;
  assert(reader_slider_commit(&ctx)==APP_REDRAW_PAGE&&(axis?s_margin_y:s_margin)==48);
  fail_layout=false;
 }
 EpdRect track=reader_slider_rect(4),para=reader_slider_rect(3);
 for(int start=0;start<APP_BOOK_TRACK_COUNT;++start)for(int direction=-1;direction<=1;direction+=2){
  s_book_tracking=start;stored_tracking=192+start;typography=start+APP_BOOK_TRACK_MIN;
  int expected=start+direction;
  if(expected<0)expected=0;
  if(expected>=APP_BOOK_TRACK_COUNT)expected=APP_BOOK_TRACK_COUNT-1;
  reader_slider_begin(4,direction<0?track.x:track.x+track.width-1);
  assert(s_reader_slider_endpoint&&s_reader_preview_tracking==expected);
  reader_slider_commit(&ctx);
  assert(s_reader_slider==-1&&app_settings_book_tracking()==expected);
  assert(stored_tracking==192+expected&&typography==expected+APP_BOOK_TRACK_MIN);
 }
 for(int start=0;start<APP_BOOK_TRACK_COUNT;++start)for(int target=0;target<APP_BOOK_TRACK_COUNT;++target){
  s_book_tracking=start;stored_tracking=192+start;typography=start+APP_BOOK_TRACK_MIN;
  int x=track.x+64+(target*(track.width-128)+15)/30;
  if(x>=track.x+track.width-64)x=track.x+track.width-65;
  reader_slider_begin(4,x);
  assert(s_reader_preview_tracking==target);
  reader_slider_move(x);
  assert(!s_reader_slider_endpoint&&s_reader_preview_tracking==target);
  reader_slider_commit(&ctx);
  assert(app_settings_book_tracking()==target&&stored_tracking==192+target);
  assert(typography==target+APP_BOOK_TRACK_MIN&&s_page==2);
 }
 for(int start=0;start<APP_BOOK_PARAGRAPH_COUNT;++start)for(int direction=-1;direction<=1;direction+=2){
  s_book_para=app_settings_book_paragraph_value(start);stored_para=s_book_para;
  int expected=start+direction;
  if(expected<0)expected=0;
  if(expected>=APP_BOOK_PARAGRAPH_COUNT)expected=APP_BOOK_PARAGRAPH_COUNT-1;
  reader_slider_begin(3,direction<0?para.x:para.x+para.width-1);
  assert(s_reader_preview_para==app_settings_book_paragraph_value(expected));
  reader_slider_commit(&ctx);
  assert(app_settings_book_paragraph_spacing()==app_settings_book_paragraph_value(expected));
  assert(stored_para==app_settings_book_paragraph_spacing());
 }
 for(int target=0;target<APP_BOOK_PARAGRAPH_COUNT;++target){
  int x=para.x+64+(target*(para.width-128)+10)/20;
  if(x>=para.x+para.width-64)x=para.x+para.width-65;
  reader_slider_begin(3,x);reader_slider_commit(&ctx);
  assert(s_book_para==app_settings_book_paragraph_value(target)&&stored_para==s_book_para);
 }
 for(int start=0;start<APP_BOOK_TRACK_COUNT;++start){
  s_book_tracking=start;stored_tracking=192+start;typography=start+APP_BOOK_TRACK_MIN;
  unsigned old_writes=writes,old_saves=saves,old_builds=builds;
  fail_layout=true;
  assert(apply_reader_typography(&ctx,(start+1)%APP_BOOK_TRACK_COUNT)==APP_REDRAW_NONE);
  fail_layout=false;
  assert(s_book_tracking==start&&stored_tracking==192+start);
  assert(typography==start+APP_BOOK_TRACK_MIN&&s_page==2);
  assert(writes==old_writes&&saves==old_saves&&builds==old_builds+2);
 }
 s_px=120;reader_slider_begin(0,648);assert(s_reader_preview_px==120);
 s_px=20;reader_slider_begin(0,36);assert(s_reader_preview_px==20);
 reader_slider_begin(2,648);assert(s_reader_preview_line==131);
 reader_slider_begin(2,36);assert(s_reader_preview_line==129);
 assert(invalidations&&saves&&writes);
 puts("PASS: actual press/drag/commit for all 31 tracking and 21 paragraph stops, endpoint bounds, NVS encoding, anchored reflow and unchanged saved spacing on reflow failure");
}
'''
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'test.c'
    source.write_text(unit)
    binary = source.with_suffix('')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-I' + str(root / 'main'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

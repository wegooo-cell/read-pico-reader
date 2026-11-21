#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：执行真实排版提交，覆盖完整新范围、原文锚点与重排失败回退。
English: Real layout commits across the new ranges, source anchors and failed reflow rollback."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "book/book_font_size.h"
#define BOOK_PX_MIN BOOK_READER_PX_MIN
#define BOOK_PX_MAX BOOK_READER_PX_MAX
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 48
#define APP_BOOK_TRACK_MIN -15
#define APP_BOOK_TRACK_COUNT 31
typedef struct {int unused;}app_ctx_t;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_PAGE}app_redraw_t;
static int s_px=48,s_margin=24,s_line_spacing=130,line=130,para=50,tracking=15,indent=2,adjustment;
static const char *s_text="text";static size_t s_text_len=4,s_page=7,s_block_count;static const void *s_blocks;
static int layout_line=130,layout_para=50,layout_tracking,layout_indent=2,layout_adjustment;
static int failures,builds,saves,writes;static bool locked,persist_failure;
static int app_settings_book_line_spacing(void){return line;}
static int app_settings_book_paragraph_spacing(void){return para;}
static int app_settings_book_tracking(void){return tracking;}
static int app_settings_book_indent(void){return indent;}
static int app_settings_book_indent_adjust(void){return adjustment;}
static uint8_t app_settings_book_paragraph_level(int p){if(p<-100)p=-100;if(p>100)p=100;return (p+105)/10;}
static int app_settings_book_paragraph_value(uint8_t i){return (i>20?20:i)*10-100;}
static void app_settings_set_book_px(uint8_t p){assert(p==s_px);++writes;}
static void app_settings_set_book_margin(uint8_t m){assert(m==s_margin);++writes;}
static void app_settings_set_book_line_spacing(int p){line=p;++writes;}
static void app_settings_set_book_paragraph_spacing(int p){para=p;++writes;}
static void app_settings_set_book_tracking(uint8_t p){tracking=p;++writes;}
static void app_settings_set_book_indent(uint8_t p){indent=p;++writes;}
static void app_settings_set_book_indent_adjust(int8_t p){adjustment=p;++writes;}
static bool app_settings_try_book_layout(uint8_t px,uint8_t margin,int l,int p){
 if(persist_failure)return false;
 app_settings_set_book_px(px);app_settings_set_book_margin(margin);app_settings_set_book_line_spacing(l);app_settings_set_book_paragraph_spacing(p);return true;
}
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static void invalidate_prep(void){assert(locked);}
static void refresh_chapter_lead(void){assert(locked);}
static int body_rect(void){return 1;}
static int body_rect_for_tracking(int t){(void)t;return 1;}
static size_t book_layout_page_start_offset(size_t p){(void)p;return 1234;}
static size_t book_layout_page_for_offset(size_t p){assert(p==1234);return 7;}
static void book_layout_set_spacing(int l,int p){assert(locked);layout_line=l;layout_para=p;}
static void book_layout_set_typography(int t){assert(locked);layout_tracking=t;}
static void book_layout_set_first_line_indent(unsigned p){assert(locked);layout_indent=p;}
static void book_layout_set_first_line_indent_adjust(int p){assert(locked);layout_adjustment=p;}
static bool book_layout_build_blocks(const char *text,size_t len,const void *b,size_t count,int rect,int px){
 assert(locked&&text==s_text&&len==s_text_len&&b==s_blocks&&count==s_block_count&&rect==1&&px==s_px);
 ++builds;if(failures){--failures;return false;}return true;
}
static void save_progress(void){assert(!locked);++saves;}
'''
for name in ['apply_reader_layout','apply_reader_typography','apply_reader_indent']:
 unit+=function(name,R/'main/apps/app_book.c')+'\n'
unit+=r'''
static void verify(void){assert(!locked&&s_page==7&&layout_line==line&&layout_para==para&&layout_tracking==tracking-15&&layout_indent==indent&&layout_adjustment==adjustment);}
int main(void){app_ctx_t ctx={0};
 for(int px=20;px<=120;++px){apply_reader_layout(&ctx,px,s_margin,line,para);assert(s_px==px);verify();}
 for(int margin=0;margin<=48;++margin){apply_reader_layout(&ctx,s_px,margin,line,para);assert(s_margin==margin);verify();}
 for(int l=100;l<=300;++l){apply_reader_layout(&ctx,s_px,s_margin,l,para);assert(line==l&&s_line_spacing==l);verify();}
 for(int p=-100;p<=100;p+=10){apply_reader_layout(&ctx,s_px,s_margin,line,p);assert(para==p);verify();}
 for(int t=0;t<31;++t){apply_reader_typography(&ctx,t);assert(tracking==t);verify();}
 for(int em=0;em<=3;++em)for(int adj=-100;adj<=100;++adj){apply_reader_indent(&ctx,em,adj);assert(indent==em&&adjustment==adj);verify();}
 int old_writes=writes,old_saves=saves;
 assert(apply_reader_layout(&ctx,19,s_margin,line,para)==APP_REDRAW_NONE);
 assert(apply_reader_layout(&ctx,121,s_margin,line,para)==APP_REDRAW_NONE);
 assert(apply_reader_layout(&ctx,s_px,-1,line,para)==APP_REDRAW_NONE);
 assert(apply_reader_layout(&ctx,s_px,49,line,para)==APP_REDRAW_NONE);
 assert(apply_reader_layout(&ctx,s_px,s_margin,99,para)==APP_REDRAW_NONE);
 assert(apply_reader_layout(&ctx,s_px,s_margin,301,para)==APP_REDRAW_NONE);
 assert(apply_reader_layout(&ctx,s_px,s_margin,line,-101)==APP_REDRAW_NONE);
 assert(apply_reader_layout(&ctx,s_px,s_margin,line,101)==APP_REDRAW_NONE);
 assert(apply_reader_layout(&ctx,s_px,s_margin,line,5)==APP_REDRAW_NONE);
 assert(old_writes==writes&&old_saves==saves);
 persist_failure=true;assert(apply_reader_layout(&ctx,48,24,100,-100)==APP_REDRAW_NONE);verify();persist_failure=false;
 failures=1;assert(apply_reader_layout(&ctx,48,24,100,-100)==APP_REDRAW_NONE);verify();
 failures=1;assert(apply_reader_typography(&ctx,15)==APP_REDRAW_NONE);verify();
 failures=1;assert(apply_reader_indent(&ctx,2,0)==APP_REDRAW_NONE);verify();
 assert(old_writes==writes&&old_saves==saves&&builds>1000);
 puts("PASS: real commits for 101 sizes, 49 margins, 201 line heights, 21 signed gaps, 31 tracking stops and 804 indent combinations; source anchors and save failure and all three failed-reflow rollbacks");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-layout-transaction-') as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
  '-I'+str(R/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

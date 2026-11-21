"""真实字号滑杆的逐像素拖动和两端点击回归。
Real reader-size rail drags and endpoint clicks.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1];source=(root/'main/apps/app_book.c').read_text()
def function(name):
 m=re.search(r'^static [^\n]+\b'+name+r'\([^;{}]*\)\s*\{',source,re.M);assert m,name
 at=m.end();depth=1
 while depth:
  depth+=(source[at]=='{')-(source[at]=='}');at+=1
 return source[m.start():at]+'\n'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "book_font_size.h"
#define BOOK_PX_MIN BOOK_READER_PX_MIN
#define BOOK_PX_MAX BOOK_READER_PX_MAX
#define BOOK_MARGIN_CHOICES 9
#define APP_BOOK_TRACK_MIN -15
#define APP_BOOK_TRACK_COUNT 31
#define APP_BOOK_PARAGRAPH_COUNT 21
typedef struct {int x,y,width,height;}EpdRect;
static int s_px=48,s_margin=24,s_line_spacing=130;
static int s_reader_slider,s_reader_preview_px,s_reader_preview_margin,s_reader_preview_line,s_reader_preview_para,s_reader_preview_tracking;
static bool s_reader_slider_endpoint;
static EpdRect reader_slider_rect(int slider){(void)slider;return (EpdRect){36,700,612,76};}
static int app_settings_book_tracking(void){return 15;}
static int app_settings_book_line_spacing(void){return s_line_spacing;}
static int app_settings_book_paragraph_spacing(void){return 25;}
static int reader_margin_levels(int a,int b,int *c,int *d){(void)a;(void)b;(void)c;(void)d;return 9;}
static int reader_margin_for_level(int a,int b,int c,int d){(void)a;(void)c;(void)d;return b;}
static int reader_margin_input(EpdRect r,int a,int b,int c,int d){(void)r;(void)a;(void)c;(void)d;return b;}
'''
from home_cover_cache_host_test import function as real_function
for name in ['app_settings_book_paragraph_value','app_settings_book_paragraph_level']:
 unit+=real_function(name,root/'main/settings.c')+'\n'
for name in ['slider_index','reader_slider_map','reader_slider_begin']:unit+=function(name)
unit+=r'''
int main(void){
 bool seen[BOOK_PX_MAX+1]={0};int previous=BOOK_PX_MIN;
 for(int x=-200;x<1000;++x){reader_slider_map(0,x);assert(s_reader_preview_px>=20&&s_reader_preview_px<=120);assert(s_reader_preview_px>=previous);seen[s_reader_preview_px]=true;previous=s_reader_preview_px;}
 for(int px=20;px<=120;++px){
  assert(seen[px]);s_px=px;
  reader_slider_begin(0,40);assert(s_reader_preview_px==(px==20?20:px-1));
  reader_slider_begin(0,645);assert(s_reader_preview_px==(px==120?120:px+1));
 }
 s_px=48;reader_slider_begin(0,342);assert(s_reader_preview_px==70);
 assert(BOOK_READER_PX_DEFAULT==48);
 puts("PASS: all 101 reader sizes reachable monotonically on the real rail; 1px endpoint steps, clamped 20/120 and unchanged 48px default");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-size-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'main/book'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Actual chapter pagination fills the stable reader body and survives chrome toggles."""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
unit=(R/'tools/book_layout_host_test.c').read_text().replace('int main(void) {','static void legacy_layout_tests(void) {')+r'''
#include "ui/ui_reader_layout.h"
static bool fullscreen, immersive;
static EpdRect body_rect(void){return ui_reader_body(fullscreen,immersive,24,false);}
'''+function('reader_heading_viewport',R/'main/apps/book_reader_heading.inc')+r'''
static int ink=42;
static int measure_chapter(EpdRect r,const char *text,size_t len){
 assert(r.height>ink&&text&&len);return ink;
}
int main(void){
 legacy_layout_tests();
 book_layout_set_vertical(false);book_layout_set_vertical_balance(true);
 book_layout_set_reader_spacing(true);book_layout_set_first_line_indent(0);
 book_layout_set_first_line_indent_adjust(0);book_layout_set_reading_line(0);
 book_layout_set_inline_images(NULL,0);book_layout_set_image_dims(NULL,NULL);
 book_layout_set_runs(NULL,0);book_layout_set_typography(0);test_slot=0;
 memset(test_slot_advance,0,sizeof(test_slot_advance));test_cjk_advance=0;test_zero_width=false;
 book_layout_set_heading_measure(measure_chapter);book_layout_set_heading_draw(NULL);
 book_layout_set_chapter_lead(0,0);book_layout_set_heading_extent(272);
 char text[12000]="第1章 空屋\n\n\n";
 size_t title=strlen("第1章 空屋");size_t start=strlen(text);
 memset(text+start,'x',sizeof(text)-start-1);text[sizeof(text)-1]=0;
 blk_t block={.offset=0,.len=title,.image=-1,.reader_heading=BOOK_HEADING_TXT};
 const int sizes[]={20,40,57,72};uint8_t fb[32]={0};
 for(int im=0;im<2;++im)for(unsigned sz=0;sz<4;++sz)
 for(int line=110;line<=150;line+=20)for(int style=0;style<4;++style){
  immersive=im;fullscreen=true;ink=42+style*23;int px=sizes[sz],pitch=px*line/100;
  book_layout_set_spacing(line,50);reader_heading_viewport();EpdRect r=body_rect();
  assert(book_layout_build_blocks(text,strlen(text),&block,1,r,px));
  size_t pages=book_layout_page_count(),offsets[256];assert(pages>1&&pages<256);
  for(size_t p=0;p<pages;++p)offsets[p]=book_layout_page_start_offset(p);
  capture_lines=true;captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,r,px);
  assert(captured_count>2);
  int bottom=captured_y[captured_count-1];
  assert(bottom<=r.y+r.height&&r.y+r.height-bottom<pitch+px/2+8);
  assert(book_layout_page_content_height(0)+book_layout_page_top_padding(0)==r.height);
  fullscreen=false;reader_heading_viewport();assert(book_layout_build_blocks(text,strlen(text),&block,1,body_rect(),px));
  assert(book_layout_page_count()==pages);
  for(size_t p=0;p<pages;++p)assert(book_layout_page_start_offset(p)==offsets[p]);
  // Every source byte remains mapped to exactly its original page interval.
  size_t p=0;
  for(size_t off=0;off<strlen(text);++off){
   while(p+1<pages&&offsets[p+1]<=off)++p;
   assert(book_layout_page_for_offset(off)==p);
  }
 }
 book_layout_free();
 puts("PASS: complete chapter-first-page body in both immersive modes, four heading extents and body sizes, 110/130/150% line spacing; chrome toggles preserve every page/source offset");
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
  '-I'+str(R/'tools/book_layout_stubs'),'-I'+str(R/'tools/book_source_host_stubs'),'-I'+str(R/'main/book'),'-I'+str(R/'main'),
  str(p/'test.c'),str(R/'main/book/book_layout.c'),str(R/'main/book/html_text.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

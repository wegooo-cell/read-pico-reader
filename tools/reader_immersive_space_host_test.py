#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实排版预留完整状态栏，保持正文与页脚间隔。/ English: Actual layout reserves the complete status row without body/footer overlap."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
u=r'''
#define main original_layout_test
#include "book_layout_host_test.c"
#undef main
#include "ui/ui_reader_layout.h"
int main(void){
 original_layout_test();
 book_layout_set_vertical_balance(true);
 char text[12000];memset(text,'a',sizeof(text));for(int i=9;i<11999;i+=10)text[i]=' ';text[11999]=0;
 book_layout_set_vertical(false);book_layout_set_typography(0);book_layout_set_first_line_indent(0);
 book_layout_set_spacing(130,0);book_layout_set_chapter_lead(0,0);test_cjk_advance=0;
 EpdRect old={24,64,636,1088},now=ui_reader_body(true,false,24,false);uint8_t fb[32]={0};
 assert(book_layout_build(text,11999,old,48));capture_lines=true;captured_count=0;drawn[0]=0;
 book_layout_draw_page(fb,0,old,48);unsigned before=captured_count;
 assert(book_layout_build(text,11999,now,48));captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,now,48);
 assert(UI_READER_HEADER_TEXT_PX==25&&UI_READER_FOOTER_TEXT_PX==21);
 assert(ui_reader_progress().y==1194&&ui_reader_progress().height==6&&UI_READER_FOOTER_TEXT_Y==1180);
 assert(captured_count<=before&&now.y==84);assert(now.y+now.height==ui_reader_footer().y-16);
 assert(UI_READER_FOOTER_TEXT_Y-UI_READER_FOOTER_TEXT_PX/2>=ui_reader_footer().y);
 assert(UI_READER_FOOTER_TEXT_Y+UI_READER_FOOTER_TEXT_PX/2<ui_reader_progress().y);
 printf("PASS: real layout at48px/130%% reserves84px text top with%u/%u lines; body, metadata and progress remain disjoint\n",before,captured_count);
 // 对照8863相同字体和正文：保留尾部边界，首行基线至少上移6px，不能裁入状态栏。
 // Compare8863 with identical text/font: preserve the bottom edge, move the first baseline up at least6px and stay below chrome.
 EpdRect prior={24,96,636,1056};
 assert(book_layout_build(text,11999,prior,48));captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,prior,48);
 int prior_first=captured_y[0];unsigned prior_lines=captured_count;
 assert(book_layout_build(text,11999,now,48));captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,now,48);
 assert(captured_count>=prior_lines&&captured_y[0]<=prior_first-6&&captured_y[0]>=now.y);
 assert(now.y>=ui_reader_header().y+ui_reader_header().height);
 EpdRect full=ui_reader_body(true,true,24,false);
 assert(full.y==16&&full.y+full.height==1188);
 assert(book_layout_build(text,11999,full,48));captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,full,48);
 assert(captured_count>before+1);
 assert(ui_reader_fullscreen_progress().y==1207&&ui_reader_fullscreen_progress().height==3);
 puts("PASS: full immersion recovers symmetric body16..1188 and thin1207/3px progress, adding more real text lines");
 // Both chrome states feed identical geometry to the real paginator, including vertical text.
 for(int style=0;style<2;++style)for(int vertical=0;vertical<2;++vertical){
  book_layout_set_vertical(vertical);
  EpdRect a=ui_reader_body(true,style,24,vertical),b=ui_reader_body(false,style,24,vertical);
  assert(a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height);
  assert(book_layout_build(text,11999,a,48));
  size_t pages=book_layout_page_count(),offsets[1024];assert(pages<1024);
  for(size_t page=0;page<pages;++page)offsets[page]=book_layout_page_start_offset(page);
  assert(book_layout_build(text,11999,b,48)&&book_layout_page_count()==pages);
  for(size_t page=0;page<pages;++page)assert(book_layout_page_start_offset(page)==offsets[page]);
 }
 puts("PASS: real horizontal/vertical pagination and every page offset are identical with chrome shown/hidden in both immersive styles");
 book_layout_free();}
'''
u=(R/'tools/book_layout_host_test.c').read_text().replace('int main(void) {','static void original_layout_test(void) {')+'\n'+u.split('#undef main\n',1)[1]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(u);exe=p/'test'
 subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'tools'),'-I'+str(R/'tools/book_layout_stubs'),'-I'+str(R/'tools/book_source_host_stubs'),'-I'+str(R/'main/book'),'-I'+str(R/'main'),str(p/'test.c'),str(R/'main/book/book_layout.c'),str(R/'main/book/html_text.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

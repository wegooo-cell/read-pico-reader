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
 char text[12000];memset(text,'a',sizeof(text));for(int i=9;i<11999;i+=10)text[i]=' ';text[11999]=0;
 book_layout_set_vertical(false);book_layout_set_typography(0);book_layout_set_first_line_indent(0);
 book_layout_set_spacing(130,0);book_layout_set_chapter_lead(0,0);test_cjk_advance=0;
 EpdRect old={24,64,636,1088},now=ui_reader_body(true,false,24,false);uint8_t fb[32]={0};
 assert(book_layout_build(text,11999,old,48));capture_lines=true;captured_count=0;drawn[0]=0;
 book_layout_draw_page(fb,0,old,48);unsigned before=captured_count;
 assert(book_layout_build(text,11999,now,48));captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,now,48);
 assert(UI_READER_HEADER_TEXT_PX==25&&UI_READER_FOOTER_TEXT_PX==21);
 assert(ui_reader_progress().y==1194&&ui_reader_progress().height==6&&UI_READER_FOOTER_TEXT_Y==1180);
 assert(captured_count==before&&now.y==80);assert(now.y+now.height==ui_reader_footer().y);
 assert(UI_READER_FOOTER_TEXT_Y-UI_READER_FOOTER_TEXT_PX/2>=ui_reader_footer().y);
 assert(UI_READER_FOOTER_TEXT_Y+UI_READER_FOOTER_TEXT_PX/2<ui_reader_progress().y);
 printf("PASS: real layout at48px/130%% reserves80px status row with%u/%u lines; body, metadata and progress remain disjoint\n",before,captured_count);
 EpdRect full=ui_reader_body(true,true,24,false);
 assert(full.y==24&&full.y+full.height==1202);
 assert(book_layout_build(text,11999,full,48));captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,full,48);
 assert(captured_count>before+1);
 assert(ui_reader_fullscreen_progress().y==1207&&ui_reader_fullscreen_progress().height==3);
 puts("PASS: full immersion recovers release body24..1202 and thin1207/3px progress, adding more real text lines");
 book_layout_free();}
'''
u=(R/'tools/book_layout_host_test.c').read_text().replace('int main(void) {','static void original_layout_test(void) {')+'\n'+u.split('#undef main\n',1)[1]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(u);exe=p/'test'
 subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'tools'),'-I'+str(R/'tools/book_layout_stubs'),'-I'+str(R/'tools/book_source_host_stubs'),'-I'+str(R/'main/book'),'-I'+str(R/'main'),str(p/'test.c'),str(R/'main/book/book_layout.c'),str(R/'main/book/html_text.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

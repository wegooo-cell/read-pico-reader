#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""题头后空一行、正文段距及书内留白的真实分页回归。
Real pagination checks for one-row heading gaps, paragraph spacing and authored blanks."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
u=(R/'tools/book_layout_host_test.c').read_text().replace('int main(void) {','static void legacy_layout_tests(void) {')+r'''
static int spacing_ink=42;
static EpdRect spacing_area;
static int spacing_measure(EpdRect area,const char *text,size_t len){
 assert(area.height>spacing_ink&&len&&text);return spacing_ink;
}
static void spacing_draw(uint8_t *fb,EpdRect area,const char *text,size_t len,bool vertical,bool volume){
 (void)fb;(void)text;(void)len;assert(!vertical&&!volume);spacing_area=area;
}
int main(void){
 legacy_layout_tests();
 book_layout_set_vertical(false);book_layout_set_vertical_balance(true);
 book_layout_set_chapter_viewport(700,0);book_layout_set_heading_extent(272);
 book_layout_set_first_line_indent(0);book_layout_set_first_line_indent_adjust(0);
 book_layout_set_reading_line(0);book_layout_set_inline_images(NULL,0);
 book_layout_set_image_dims(NULL,NULL);book_layout_set_image_bleed_width(0);
 book_layout_set_runs(NULL,0);book_layout_set_typography(0);test_slot=0;
 memset(test_slot_advance,0,sizeof(test_slot_advance));test_cjk_advance=0;test_opener_bearing=0;test_zero_width=false;
 book_layout_set_heading_draw(spacing_draw);book_layout_set_heading_measure(spacing_measure);
 book_layout_set_reader_spacing(true);
 EpdRect r={16,20,600,700};uint8_t fb[32]={0};
 const char *text="12\n\n\nFirst\n　\n \nSecond\n\nThird";
 blk_t block={.offset=0,.len=2,.image=-1,.reader_heading=BOOK_HEADING_TXT};
 for(unsigned percent=110;percent<=200;percent+=30)for(unsigned para=0;para<=100;para+=25){
  book_layout_set_spacing(percent,para);int pitch=20*percent/100;
  assert(book_layout_build_blocks(text,strlen(text),&block,1,r,20)&&book_layout_page_count()==1);
  capture_lines=true;captured_count=0;drawn[0]=0;
  book_layout_draw_page(fb,0,r,20);
  assert(captured_count==3&&!strcmp(drawn,"FirstSecondThird"));
  assert(spacing_area.height==spacing_ink+pitch);
  assert(captured_y[0]==spacing_area.y+spacing_area.height+20);
  assert(captured_y[1]-captured_y[0]==pitch+pitch*(int)para/100);
  assert(captured_y[2]-captured_y[1]==captured_y[1]-captured_y[0]);
  int ys[3];memcpy(ys,captured_y,sizeof(ys));
  captured_count=0;drawn[0]=0;book_layout_draw_cursor_t cursor={0};int state;
  do{state=book_layout_draw_slice(fb,0,r,20,&cursor);assert(state>=0);}while(!state);
  assert(captured_count==3&&!strcmp(drawn,"FirstSecondThird")&&!memcmp(ys,captured_y,sizeof(ys)));
  for(size_t off=0;off<strlen(text);++off)assert(book_layout_page_for_offset(off)==0);
 }
 // 章首独立题头也只预留实高加一行，且不重复计算原文空白。
 // Standalone leads also reserve ink plus one row without counting source blanks twice.
 book_layout_set_spacing(150,25);book_layout_set_chapter_lead(3,42);
 assert(book_layout_build_blocks(text,strlen(text),&block,1,r,20));
 captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,r,20);
 assert(captured_count==3&&captured_y[0]==r.y+book_layout_page_top_padding(0)+72+20);
 assert(captured_y[1]-captured_y[0]==37);
 assert(book_layout_build_blocks(text,strlen(text),&block,1,r,30));
 captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,r,30);
 assert(captured_y[0]==r.y+book_layout_page_top_padding(0)+42+45+30);
 assert(captured_y[1]-captured_y[0]==56);
 // 有明确书内样式的空白段不被阅读器折叠。
 // Preserve explicitly authored blank paragraphs.
 book_layout_set_chapter_lead(0,0);
 const char *authored="First\n \nSecond";
 blk_t native[]={ {.offset=0,.len=5,.image=-1,.authored_style=true},
  {.offset=6,.len=1,.image=-1,.authored_style=true},
  {.offset=8,.len=6,.image=-1,.authored_style=true}};
 assert(book_layout_build_blocks(authored,strlen(authored),native,3,r,20));
 captured_count=0;drawn[0]=0;book_layout_draw_page(fb,0,r,20);
 assert(captured_count==2&&captured_y[1]-captured_y[0]==74);
 book_layout_set_heading_measure(NULL);book_layout_set_heading_draw(NULL);book_layout_set_reader_spacing(false);book_layout_free();
 puts("PASS: measured heading plus one body row; first/second/third paragraph gaps follow settings, redundant blanks collapse, source offsets and authored spacing survive, sliced/full drawing agree");
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(u)
 subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
  '-I'+str(R/'tools/book_layout_stubs'),'-I'+str(R/'tools/book_source_host_stubs'),'-I'+str(R/'main/book'),'-I'+str(R/'main'),
  str(p/'test.c'),str(R/'main/book/book_layout.c'),str(R/'main/book/html_text.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：扩展间距的真实分页、绘制、负段距与源位置一致性。
English: Actual pagination/drawing for extended gaps, signed spacing and source anchors."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
unit=(R/'tools/book_layout_host_test.c').read_text().replace('int main(void) {','static void legacy_layout_tests(void) {')+r'''
int main(void){
 legacy_layout_tests();
 book_layout_set_vertical(false);book_layout_set_vertical_balance(false);
 book_layout_set_chapter_lead(0,0);book_layout_set_heading_draw(NULL);book_layout_set_heading_measure(NULL);
 book_layout_set_first_line_indent(0);book_layout_set_first_line_indent_adjust(0);
 book_layout_set_reading_line(0);book_layout_set_inline_images(NULL,0);
 book_layout_set_image_dims(NULL,NULL);book_layout_set_image_bleed_width(0);
 book_layout_set_runs(NULL,0);book_layout_set_typography(0);test_slot=0;
 memset(test_slot_advance,0,sizeof(test_slot_advance));test_cjk_advance=0;test_opener_bearing=0;test_zero_width=false;
 book_layout_set_reader_spacing(true);
 const char *text="A\nB\nC";EpdRect rect={0,0,600,1200};uint8_t fb[16]={0};
 for(int px=20;px<=120;px+=px==20?28:72){
  for(int line=100;line<=300;++line)for(int para=-100;para<=100;para+=10){
   int pitch=px*line/100,gap=pitch*para/100;if(gap<px-pitch)gap=px-pitch;
   book_layout_set_spacing(line,para);
   assert(book_layout_build(text,strlen(text),rect,px));
   size_t pages=book_layout_page_count();assert(pages&&pages<=3);
   int total=0;
   for(size_t page=0;page<pages;++page){
    capture_lines=true;captured_count=0;drawn[0]=0;book_layout_draw_page(fb,page,rect,px);
    for(unsigned i=1;i<captured_count;++i)assert(captured_y[i]-captured_y[i-1]==pitch+gap);
    assert(pitch+gap>=px);total+=captured_count;
    int ys[3];memcpy(ys,captured_y,sizeof(ys));unsigned count=captured_count;
    captured_count=0;drawn[0]=0;book_layout_draw_cursor_t cursor={0};int state;
    do{state=book_layout_draw_slice(fb,page,rect,px,&cursor);assert(state>=0);}while(!state);
    assert(captured_count==count&&!memcmp(ys,captured_y,sizeof(ys)));
    size_t begin=book_layout_page_start_offset(page);
    assert(book_layout_page_for_offset(begin)==page);
   }
   assert(total==3);
  }
 }
 // 每一种字号均检查最紧和最松档，不出现无效页或正文丢失。/ Check both extremes at every type size without invalid pages or lost text.
 for(int px=20;px<=120;++px)for(int vertical=0;vertical<2;++vertical){
  book_layout_set_vertical(vertical);
  for(int line=100;line<=300;line+=200)for(int para=-100;para<=100;para+=200){
   book_layout_set_spacing(line,para);assert(book_layout_build(text,strlen(text),rect,px));
   assert(book_layout_page_count());
   for(size_t page=0;page<book_layout_page_count();++page){
    size_t start=book_layout_page_start_offset(page);assert(start<strlen(text)&&book_layout_page_for_offset(start)==page);
    if(page)assert(start>book_layout_page_start_offset(page-1));
   }
  }
 }
 book_layout_free();
 puts("PASS: all 201 line heights x 21 signed gaps at 20/48/120px; full/sliced drawing, no overlap or lost paragraphs; all 101 sizes and both orientations at range extremes");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-gap-geometry-') as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
  '-I'+str(R/'tools/book_layout_stubs'),'-I'+str(R/'tools/book_source_host_stubs'),'-I'+str(R/'main/book'),'-I'+str(R/'main'),
  str(p/'test.c'),str(R/'main/book/book_layout.c'),str(R/'main/book/html_text.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

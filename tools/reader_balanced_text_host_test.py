#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实分页的尾行容纳、上下留白、脚注、分片一致性和原默认行为。
English: Actual tail-row fitting,balanced slack,notes,sliced equivalence and legacy defaults.
"""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
u=(R/'tools/book_layout_host_test.c').read_text().replace('int main(void) {','static void legacy_layout_tests(void) {')+r'''
int main(void){
 legacy_layout_tests();
 char text[128]={0};for(int i=0;i<20;++i){text[2*i]='A'+i;text[2*i+1]='\n';}
 EpdRect rect={0,10,200,118};uint8_t fb[32]={0};
 book_layout_set_vertical(false);book_layout_set_typography(0);book_layout_set_first_line_indent(0);
 book_layout_set_first_line_indent_adjust(0);book_layout_set_spacing(150,0);book_layout_set_chapter_lead(0,0);
 book_layout_set_reading_line(0);book_layout_set_image_dims(NULL,NULL);book_layout_set_runs(NULL,0);
 book_layout_set_inline_images(NULL,0);book_layout_set_image_bleed_width(0);test_slot_ready_mask=1;test_slot=0;
 memset(test_slot_advance,0,sizeof(test_slot_advance));test_cjk_advance=0;test_opener_bearing=0;test_zero_width=false;
 book_layout_set_vertical_balance(false);assert(book_layout_build(text,40,rect,20));
 assert(book_layout_page_start_offset(1)==6&&book_layout_page_top_padding(0)==0);
 book_layout_set_vertical_balance(true);
 EpdRect tight=rect;tight.height=112;assert(book_layout_build(text,40,tight,20)&&book_layout_page_start_offset(1)==8);
 assert(book_layout_build(text,40,rect,20));
 assert(book_layout_page_start_offset(1)==8&&book_layout_page_top_padding(0)==2);
 html_note_t note={.marker_offset=0,.image=-1};book_layout_set_inline_images(&note,1);
 assert(book_layout_build(text,40,rect,20));int x,y,w,h,ni;
 assert(book_layout_page_note_marker_rect(0,0,&ni,&x,&y,&w,&h)&&y==2&&h==24);
 char all[128]={0};size_t pages=book_layout_page_count();
 for(size_t page=0;page<pages;++page){
  capture_lines=true;captured_count=0;drawn[0]=0;book_layout_draw_page(fb,page,rect,20);
  int count=captured_count,positions[128];memcpy(positions,captured_y,sizeof(positions));char full[20000];strcpy(full,drawn);
  if(page==0){assert(count==4&&first_draw_y==32&&last_draw_y==122);}
  if(page+1==pages)assert(book_layout_page_top_padding(page)==0);
  strcat(all,full);drawn[0]=0;captured_count=0;book_layout_draw_cursor_t cursor={0};int status;
  do{status=book_layout_draw_slice(fb,page,rect,20,&cursor);assert(status>=0);}while(status==0);
  assert(!strcmp(full,drawn)&&captured_count==(unsigned)count&&!memcmp(positions,captured_y,count*sizeof(int)));
 }
 assert(!strcmp(all,"ABCDEFGHIJKLMNOPQRST"));
 for(size_t off=0;off<40;++off){size_t page=book_layout_page_for_offset(off);assert(book_layout_page_start_offset(page)<=off);if(page+1<pages)assert(book_layout_page_start_offset(page+1)>off);}
 book_layout_set_reading_line(1);book_layout_set_reading_line_offset(8);assert(book_layout_build(text,40,rect,20));
 assert(book_layout_page_start_offset(1)==6);drawn[0]=0;captured_count=0;book_layout_draw_page(fb,0,rect,20);assert(test_guide_last_y+2<=rect.y+rect.height);
 book_layout_set_reading_line(0);book_layout_set_inline_images(NULL,0);book_layout_set_chapter_lead(0,20);
 assert(book_layout_build(text,40,rect,20)&&book_layout_page_top_padding(0)==0);
 book_layout_set_chapter_lead(0,0);
 const char art[]="ABCD\nIMG\nEFGHIJ";blk_t blocks[]={
 {.offset=0,.len=4,.image=-1},{.offset=5,.len=3,.image=0,.image_width=80,.image_height=400},{.offset=9,.len=6,.image=-1}};
 html_text_t chapter={.blocks=blocks,.count=3};book_layout_set_image_dims(fixture_image,&chapter);
 assert(book_layout_build_blocks(art,strlen(art),blocks,3,rect,20)&&book_layout_page_top_padding(0)==0);
 for(size_t page=0;page<book_layout_page_count();++page)if(book_layout_page_image_count(page))assert(book_layout_page_top_padding(page)==0);
 book_layout_set_image_dims(NULL,NULL);
 for(int failure=0;failure<18;++failure){test_heap_fail_after=failure;bool ok=book_layout_build(text,40,rect,20);if(!ok)assert(book_layout_page_count()==0);book_layout_free();}
 test_heap_fail_after=-1;book_layout_set_vertical_balance(false);assert(book_layout_build(text,40,rect,20)&&book_layout_page_start_offset(1)==6);
 book_layout_free();puts("PASS: legacy paginator suite unchanged; 4 rows replace3 when glyphs fit,2px balanced slack,natural chapter/heading/art pages,guide bounds,note hits,all offsets/text conserved,sliced draw matches,and OOM cleanup");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(u)
 subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'tools/book_layout_stubs'),'-I'+str(R/'tools/book_source_host_stubs'),'-I'+str(R/'main/book'),'-I'+str(R/'main'),str(p/'test.c'),str(R/'main/book/book_layout.c'),str(R/'main/book/html_text.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

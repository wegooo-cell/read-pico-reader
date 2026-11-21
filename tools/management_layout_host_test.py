#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""页面空间和输入不串区：两种阅读模式、目录/选择/分页及书架五行。 / Cross-region layout and input invariants."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'epdiy.h').write_text('#pragma once\ntypedef struct {int x,y,width,height;} EpdRect;\n')
 (p/'test.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include "ui/ui_reader_layout.h"
#include "ui/ui_files_layout.h"
#include "ui/ui_book_manage_layout.h"
#include "book/book_reader_input.h"
static bool hit(EpdRect a,int x,int y){return x>=a.x&&y>=a.y&&x<a.x+a.width&&y<a.y+a.height;}
static bool overlaps(EpdRect a,EpdRect b){return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;}
static void bounds(EpdRect a){assert(a.x>=0&&a.y>=0&&a.width>0&&a.height>0&&a.x+a.width<=684&&a.y+a.height<=1216);}
int main(void){
 for(int full=0;full<2;++full)for(int immersive=0;immersive<2;++immersive)for(int vertical=0;vertical<2;++vertical)for(int margin=0;margin<=48;++margin){
  EpdRect body=ui_reader_body(full,immersive,margin,vertical);bounds(body);
  EpdRect hidden=ui_reader_body(!full,immersive,margin,vertical);
  assert(body.x==hidden.x&&body.y==hidden.y&&body.width==hidden.width&&body.height==hidden.height);
  EpdRect controls=full?ui_reader_footer_for(full,immersive):(EpdRect){0,1096,684,120};bounds(controls);
  EpdRect visible=ui_reader_area(full,immersive);bounds(visible);assert(!overlaps(visible,controls));
  if(full)assert(!overlaps(body,controls));
  else { EpdRect footer=ui_reader_footer_for(false,immersive);bounds(footer);assert(!overlaps(visible,footer)&&!overlaps(footer,controls));assert(controls.y==UI_READER_TOOLS_TOP); }
  assert(body.y>= (immersive?UI_READER_FULLSCREEN_TOP:UI_READER_METADATA_TOP));assert(body.y+body.height<= (immersive?UI_READER_FULLSCREEN_BOTTOM:UI_READER_METADATA_BOTTOM));
  book_reader_tap_t tap={0};
  for(int y=body.y;y<body.y+body.height;++y){
   assert(book_reader_tap_feed(&tap,body,342,y,vertical,vertical,10)==book_reader_tap_direction(body,342,y,vertical,vertical)&&!tap.pending);
   assert(!book_reader_tap_tick(&tap,1000,false));
  }
  assert(!book_reader_tap_feed(&tap,body,342,1216,false,false,10));
  assert(book_reader_tap_feed_any(&tap,body,body.x+2,body.y+20,false,false,100)==0);
  assert(book_reader_tap_feed_any(&tap,body,body.x+2,body.y+21,false,false,200)==BOOK_READER_TAP_FULLSCREEN&&!tap.pending);
  assert(!book_reader_tap_feed_any(&tap,body,body.x+2,body.y+20,false,false,1000));
  assert(book_reader_tap_tick(&tap,1400,false)==-1); // image side single still turns / 插图侧边单击翻页
 }
 bounds(ui_reader_progress());assert(!overlaps(ui_reader_progress(),ui_reader_body(true,false,0,false)));
 bounds(ui_reader_fullscreen_progress());assert(!overlaps(ui_reader_fullscreen_progress(),ui_reader_body(true,true,0,false)));
 EpdRect footer={0,1096,684,120};
 for(int row=0;row<UI_FILES_ROWS;++row){EpdRect r={36,UI_FILES_LIST_TOP+row*UI_FILES_ROW_HEIGHT,612,UI_FILES_ROW_HEIGHT};bounds(r);assert(!overlaps(r,footer));assert(r.y+r.height<=1006);}
 for(int i=0;i<4;++i){EpdRect r=ui_files_batch_rect(i);bounds(r);assert(hit(r,r.x+r.width/2,1150));for(int j=0;j<i;++j)assert(!overlaps(r,ui_files_batch_rect(j)));}
 for(int i=0;i<3;++i){bounds(ui_files_category_rect(i));bounds(ui_files_transfer_rect(i));assert(!overlaps(ui_files_storage_rect(),ui_files_category_rect(i)));}
 for(int grid=0;grid<2;++grid){
  for(int row=0;row<ui_book_manage_rows(grid);++row){
   EpdRect r=ui_book_manage_entry(grid,row),cover=ui_book_manage_cover(grid,row);bounds(r);if(grid)bounds(cover);
   assert(r.y+r.height<1006&&(!grid||(cover.x>=r.x&&cover.x+cover.width<=r.x+r.width&&cover.y>=r.y&&cover.y+cover.height<=r.y+r.height)));
   for(int j=0;j<row;++j)assert(!overlaps(r,ui_book_manage_entry(grid,j)));
  }
 }
 for(int grid=0;grid<2;++grid)for(int i=0;i<(grid?12:13);++i){EpdRect r=ui_book_import_entry(grid,i);bounds(r);assert(r.y>=244&&r.y+r.height<=1020);if(grid)bounds(ui_book_import_cover(grid,i));for(int j=0;j<i;++j)assert(!overlaps(r,ui_book_import_entry(grid,j)));}
 EpdRect imports[]={ui_book_import_search(),ui_book_import_layout(),ui_book_import_parent(),ui_book_import_select(),ui_book_import_cancel()};
 for(int i=0;i<5;++i){bounds(imports[i]);for(int j=0;j<i;++j)assert(!overlaps(imports[i],imports[j]));}
 assert(ui_book_manage_rows(false)==13&&ui_book_manage_rows(true)==12);
 EpdRect controls[]={ui_book_manage_tab(0),ui_book_manage_tab(1),ui_book_manage_search(),ui_book_manage_layout(),ui_book_manage_sort(),ui_book_manage_select(),ui_book_manage_cancel(),ui_book_manage_get_cover()};
 for(int i=0;i<8;++i){bounds(controls[i]);for(int j=0;j<i;++j)assert(!overlaps(controls[i],controls[j]));}
 for(int i=3;i<7;++i){EpdRect action=ui_book_manage_action(i);bounds(action);assert(action.y==1096&&action.height==120);assert(!overlaps(action,(EpdRect){36,1006,612,50}));for(int j=3;j<i;++j)assert(!overlaps(action,ui_book_manage_action(j)));}
 puts("PASS: stable body with disjoint visible crop/header/footer, immediate centre mode tap, retained image side zoom, 7 file rows/4 actions, 13 coverless list rows/12 grid covers with source tabs/import/rescan and separated pager/actions");
}
''')
 exe=p/'test';subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(root/'main'),str(p/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

# 验证作者按实际测量限宽，不覆盖书名或百分比。/ Exercise author clipping with variable-width font measurements.
from home_cover_cache_host_test import function
for source in [root/'main/apps/app_book.c',root/'boards/pico/src/main/apps/app_book.c']:
 unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum{EPD_DRAW_ALIGN_RIGHT};
static int font_width=1;static char rendered[160];
static int ui_text_fixed_width_px(int px,const char*text){
 assert(px==18);int width=0;
 for(const unsigned char*p=(const unsigned char*)text;*p;){
  if(*p<128){width+=font_width*((*p=='W'||*p=='M')?14:7);++p;}
  else{unsigned n=(*p&0xe0)==0xc0?2:(*p&0xf0)==0xe0?3:4;
   for(unsigned i=1;i<n;++i)assert(p[i]&&(p[i]&0xc0)==0x80);
   width+=font_width*18;p+=n;}
 }return width;
}
static void ui_text_fixed_ink_vc(uint8_t*fb,int x,int y,int px,const char*text,int align,uint8_t gray){
 (void)fb;assert(x==578&&y==337&&px==18&&align==EPD_DRAW_ALIGN_RIGHT&&gray==0x60);
 assert(ui_text_fixed_width_px(px,text)<=152&&x-ui_text_fixed_width_px(px,text)>=426);
 snprintf(rendered,sizeof(rendered),"%s",text);
}
'''
 unit+=function('copy_text',source)+'\n'+function('fit_fixed_text',source)+'\n'+function('bulk_draw_author',root/'main/apps/book_manage_ui.h')
 unit+=r'''
int main(void){uint8_t fb=0;char long_name[512],unchanged[512];
 for(font_width=1;font_width<=3;++font_width){
  const char *samples[]={"","鲁迅","J. K. Rowling","[美] 很长很长的作者姓名与译者姓名以及出版信息", "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", "作者😀作者😀作者😀作者😀作者😀作者😀"};
  for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);++i){
   bulk_draw_author(&fb,samples[i],337);
   if(ui_text_fixed_width_px(18,samples[i])<=152)assert(!strcmp(rendered,samples[i]));
   else assert(strstr(rendered,"…"));
  }
  memset(long_name,'W',sizeof(long_name)-1);long_name[sizeof(long_name)-1]=0;strcpy(unchanged,long_name);
  bulk_draw_author(&fb,long_name,337);assert(!strcmp(long_name,unchanged)&&strstr(rendered,"…"));
 }
 assert(70+340<578-152);puts("PASS: actual author rendering clips UTF-8/English/emoji at measured widths, keeps source intact and cannot overlap title/progress");
}
'''
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'author.c').write_text(unit)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'author.c'),'-o',str(p/'author')],check=True)
  subprocess.run([str(p/'author')],check=True)
ui=(root/'main/apps/book_manage_ui.h').read_text()
assert '"已上架书籍"' in ui and '"已上架书架"' not in ui
assert 'if(!s_import_candidates&&!s_manage_books)draw_shelf_order_button' in ui
assert 'else bulk_draw_author(fb,item->author' in ui

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
  EpdRect controls=full?ui_reader_footer_for(full,immersive):(EpdRect){0,1096,684,120};bounds(controls);assert(!overlaps(body,controls));
  if(!full) { EpdRect footer=ui_reader_footer_for(false,immersive);bounds(footer);assert(!overlaps(body,footer)&&!overlaps(footer,controls));assert(controls.y==UI_READER_TOOLS_TOP); }
  assert(body.y>= (!full?UI_READER_STANDARD_TOP:immersive?UI_READER_FULLSCREEN_TOP:UI_READER_METADATA_TOP));assert(body.y+body.height<= (!full?UI_READER_STANDARD_BOTTOM:immersive?UI_READER_FULLSCREEN_BOTTOM:UI_READER_METADATA_BOTTOM));
  book_reader_tap_t tap={0};
  for(int y=body.y;y<body.y+body.height;++y){
   assert(book_reader_tap_feed(&tap,body,342,y,vertical,vertical,10)==BOOK_READER_TAP_FULLSCREEN&&!tap.pending);
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
   EpdRect r=ui_book_manage_entry(grid,row),cover=ui_book_manage_cover(grid,row);bounds(r);bounds(cover);
   assert(r.y+r.height<1006&&cover.x>=r.x&&cover.x+cover.width<=r.x+r.width&&cover.y>=r.y&&cover.y+cover.height<=r.y+r.height);
   for(int j=0;j<row;++j)assert(!overlaps(r,ui_book_manage_entry(grid,j)));
  }
 }
 EpdRect controls[]={ui_book_manage_search(),ui_book_manage_layout(),ui_book_manage_sort(),ui_book_manage_select()};
 for(int i=0;i<4;++i){bounds(controls[i]);for(int j=0;j<i;++j)assert(!overlaps(controls[i],controls[j]));}
 for(int i=3;i<7;++i){EpdRect action=ui_book_manage_action(i);bounds(action);assert(action.y==1096&&action.height==120);assert(!overlaps(action,(EpdRect){36,1006,612,50}));for(int j=3;j<i;++j)assert(!overlaps(action,ui_book_manage_action(j)));}
 puts("PASS: no reader/header/footer intersection, immediate centre mode tap, retained image side zoom, 7 file rows/4 actions, 5 shelf rows and separated pager/actions");
}
''')
 exe=p/'test';subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(root/'main'),str(p/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

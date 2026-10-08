# SPDX-FileCopyrightText: 2026 cj3513
# SPDX-License-Identifier: Apache-2.0
"""验证书架布局与UTF-8书名换行。 / Exercise production shelf layout and UTF-8 wrapping."""
from pathlib import Path
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'main/apps/app_book.c').read_text(encoding='utf-8')
def function(name):
    start=re.search(r'^static [^\n]+\b'+name+r'\([^;]*?\)\s*\{',source,re.M)
    assert start,name
    at=start.end();depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start.start():at]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
enum {SHELF,BULK,TOC};
static int style,s_view,s_visible_count;
#define BOOK_ROWS 13
#define BOOK_GRID_ROWS 9
#define BOOK_BULK_ROWS 6
#define UI_MARGIN 36
#define UI_GAP 12
#define UI_BTN_H 84
static int app_settings_shelf_style(void){return style;}
static int ui_content_width(void){return 612;}
static int book_navigation_count(void){return 0;}
static int book_toc_pages(int n){return n?n:1;}
static int ui_text_fixed_width_px(int px,const char *s){
 int width=0;while(*s){unsigned char c=*s;int n=c<128?1:c<224?2:c<240?3:4;
 assert(strlen(s)>=(unsigned)n);for(int i=1;i<n;i++)assert(((unsigned char)s[i]&192)==128);
 width+=n==1?px/2:px;s+=n;}return width;
}
'''
for name in ['shelf_rows','row_rect','leaves','fit_fixed_text','shelf_title_lines','reader_effect_rect']:
    unit+=function(name)+'\n'
unit+=r'''
int main(void){
 s_view=SHELF;
 for(style=1;style<=5;style++) {
  int expected=style==5?5:style==4?13:9;assert(shelf_rows()==expected);
  for(s_visible_count=0;s_visible_count<80;s_visible_count++) {
   assert(leaves()==(s_visible_count?(s_visible_count+expected-1)/expected:1));
   int visited=0;
   for(int p=0;p<leaves();p++)for(int i=0;i<shelf_rows()&&p*shelf_rows()+i<s_visible_count;i++)visited++;
   assert(visited==s_visible_count);
  }
 }
 style=5;
 for(int i=0;i<5;i++) {
  EpdRect r=row_rect(i);assert(r.x==36&&r.width==612&&r.y>=224&&r.y+r.height<=1000);
  if(i)assert(row_rect(i-1).y+row_rect(i-1).height<r.y);
  assert(r.y+9+122<=r.y+r.height);
 }
 s_view=BULK;assert(row_rect(0).y==308&&row_rect(0).height==84);
 for(int i=0;i<3;i++) {
  EpdRect r=reader_effect_rect(i);
  assert(r.x>=36&&r.x+r.width<=648&&r.y==548&&r.y+r.height==616);
  if(i)assert(reader_effect_rect(i-1).x+reader_effect_rect(i-1).width<r.x);
 }
 const char *titles[]={"三体","人类简史：从动物到上帝","这是一本非常非常长的中文书名用于验证第二行省略且不切断字符", "Reading 中文 Mixed English Title with long subtitle", "", "😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀"};
 for(unsigned i=0;i<sizeof(titles)/sizeof(*titles);i++) {
  char lines[2][256];shelf_title_lines(titles[i],lines);
  assert(ui_text_fixed_width_px(36,lines[0])<=446&&ui_text_fixed_width_px(36,lines[1])<=446);
  if(i==0)assert(!strcmp(lines[0],"三体")&&!lines[1][0]);
  if(i==2)assert(strstr(lines[1],"…"));
 }
 puts("PASS: five styles, paging 0-79 books, clear-list row bounds, bulk unchanged, long CJK/mixed/emoji UTF-8 titles");
}
'''
with tempfile.TemporaryDirectory() as folder:
    c=Path(folder)/'shelf.c';exe=Path(folder)/'shelf.exe';c.write_text(unit,encoding='utf-8')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

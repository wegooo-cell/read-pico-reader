#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：真实边距输入/几何与扩展缩进编码，覆盖不同字体和旧配置。
# English: Actual margin input/geometry and extended indent encoding, across font sizes and legacy configurations.
from pathlib import Path
import subprocess,tempfile,re
from home_cover_cache_host_test import function
r=Path(__file__).resolve().parents[1]
f=r/'main/apps/app_book.c'
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define UI_LOCK_WIDTH 684
#define UI_BAR_TOP 1120
#define READER_FULLSCREEN_PROGRESS_TOP 1198
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 48
#define BOOK_MARGIN_CHOICES 9
typedef struct {int x,y,width,height;} EpdRect;
static bool s_reader_fullscreen,vertical,immersive;
static int s_margin;
static bool app_settings_reader_immersive(void){return immersive;}
static bool app_settings_book_vertical(void){return vertical;}
static unsigned s_book_indent_adjust=20;
static void nvs_put_u8(const char *key,uint8_t v){(void)key;assert(v==s_book_indent_adjust);}
#define NVS_KEY_BOOK_INDENT_ADJUST "bk_indent_adj"
'''
u+='\n'.join(line for line in (r/'main/ui/ui_reader_layout.h').read_text().splitlines() if not line.startswith(('#include','#pragma'))) + '\n'
settings=(r/'main/settings.h').read_text()
for name in ('APP_BOOK_TRACK_MIN','APP_BOOK_TRACK_MAX','APP_BOOK_TRACK_COUNT','APP_BOOK_PARAGRAPH_COUNT'):
    u+=re.search(r'^#define '+name+r' .*',settings,re.M)[0]+'\n'
for name in ('app_settings_book_paragraph_value','app_settings_book_paragraph_level'):
    u+=function(name,r/'main/settings.c')+'\n'
u+=function('slider_index',f)+'\n'
for n in ('body_rect_for_tracking','reader_margin_width','reader_margin_levels','reader_margin_level_for','reader_margin_for_level','reader_margin_input'):
    u+=function(n,f)+'\n'
for n in ('app_settings_book_indent_adjust','app_settings_set_book_indent_adjust'):
    u+=function(n,r/'main/settings.c')+'\n'
u+=r'''
int main(void){
 const uint8_t stops[]={0,5,10,15,20,25,30,40,50,75,100};
 EpdRect slider={156,0,396,50};
 assert(APP_BOOK_PARAGRAPH_COUNT==11&&APP_BOOK_TRACK_COUNT==31);
 int midpoint=slider.x+(slider.width-1)/2;
 assert(app_settings_book_paragraph_value(slider_index(slider,midpoint,APP_BOOK_PARAGRAPH_COUNT))==25);
 assert(slider_index(slider,midpoint,APP_BOOK_TRACK_COUNT)+APP_BOOK_TRACK_MIN==0);
 for(unsigned i=0;i<11;++i){
  int x=slider.x+64+(i*(slider.width-128)+5)/10;
  assert(app_settings_book_paragraph_value(slider_index(slider,x,11))==stops[i]);
  assert(app_settings_book_paragraph_level(stops[i])==i);
 }
 for(int px=-15;px<=15;++px){
  int x=slider.x+64+((px+15)*(slider.width-128)+15)/30;
  assert(slider_index(slider,x,31)+APP_BOOK_TRACK_MIN==px);
 }

 for(int px=36;px<=72;++px)for(int tr=-15;tr<=15;++tr){
  int widths[9],values[9];assert(reader_margin_levels(px,tr,widths,values)==9);
  for(int i=0;i<9;++i){assert(values[i]==i*6&&widths[i]==684-i*12);assert(reader_margin_level_for(i*6,px,tr)==i);}
  for(int i=0;i<=48;++i)assert(reader_margin_for_level(reader_margin_level_for(i,px,tr),i,px,tr)==i);
 }
 for(int full=0;full<2;++full)for(int im=0;im<2;++im)for(int v=0;v<2;++v){
  s_reader_fullscreen=full;immersive=im;vertical=v;
  for(int i=0;i<9;++i){s_margin=i*6;EpdRect a=body_rect_for_tracking(2);
   if(v){assert(a.y==(!full?176:im?24:80)+s_margin);assert(a.height==(!full?1048:im?1202:1168)-(!full?176:im?24:80)-2*s_margin);}
   else assert(a.x==s_margin&&a.width==684-2*s_margin);
  }
 }
 for(int i=-100;i<=100;++i){app_settings_set_book_indent_adjust(i);assert(app_settings_book_indent_adjust()==i&&s_book_indent_adjust<=200);}
 for(int old=0;old<=40;++old){s_book_indent_adjust=old;assert(app_settings_book_indent_adjust()==old-20);}
 puts("PASS: centered paragraph/tracking ladders and nine distinct physical margins in both orientations, preserved legacy margin values, all 201 indent adjustments and legacy encoding");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(u);out=p.with_suffix('')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)

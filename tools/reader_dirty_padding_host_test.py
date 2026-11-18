#!/usr/bin/env python3
"""阅读外扩掩码不级联、不越界、不分配；零参数保留原图。
Reader mask dilation cannot cascade, escape bounds or allocate; zero padding preserves input.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
enum {W=1216,H=684};
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{bool *dirty_lines;uint8_t *dirty_columns;}EpdiyHighlevelState;
static int epd_width(void){return W;}
static int epd_height(void){return H;}
static void epd_difference_column_range(EpdRect a,int *lo,int *hi){*lo=a.x/32*32;*hi=(a.x+a.width+31)/32*32;}
'''+function('reader_dilate_masks',root/'components/epdiy/src/highlevel.c')+r'''
int main(void){
 bool lines[H+2],old_lines[H+2];uint8_t cols[W/2+2],old_cols[W/2+2];
 EpdiyHighlevelState state={lines+1,cols+1};unsigned seed=56789;
 for(int test=0;test<1200;++test){
  for(int i=0;i<H+2;++i){seed=seed*1664525u+1013904223u;lines[i]=(seed>>25)==0;}
  for(int i=0;i<W/2+2;++i){seed=seed*1664525u+1013904223u;cols[i]=(seed>>25)==0?0x0f:0;}
  lines[0]=lines[H+1]=true;cols[0]=cols[W/2+1]=0xab;
  memcpy(old_lines,lines,sizeof(lines));memcpy(old_cols,cols,sizeof(cols));
  int y=test%H,h=1+(test*7)%(H-y),x=32*(test%38),w=32*(1+(test%((W-x)/32)));
  int pl=test&1,pc=(test>>1)&1;
  reader_dilate_masks(&state,(EpdRect){x,y,w,h},pl,pc);
  assert(lines[0]==old_lines[0]&&lines[H+1]==old_lines[H+1]&&cols[0]==0xab&&cols[W/2+1]==0xab);
  for(int i=0;i<H;++i){
   bool expected=old_lines[i+1];
   if(pl&&i>=y&&i<y+h)expected=expected||(i>y&&old_lines[i])||(i+1<y+h&&old_lines[i+2]);
   assert(state.dirty_lines[i]==expected);
  }
  int c0=x/2,c1=(x+w)/2;
  for(int i=0;i<W/2;++i){
   uint8_t expected=old_cols[i+1];
   if(pc&&i>=c0&&i<c1&&(expected||(i>c0&&old_cols[i])||(i+1<c1&&old_cols[i+2])))expected=0xff;
   assert(state.dirty_columns[i]==expected);
  }
 }
 puts("PASS:1200 independent row/column dilation oracles, crop/edge sentinels, no cascading and zero-padding identity");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-padding-') as temporary:
    folder = Path(temporary); source = folder/'test.c'; source.write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(source),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)

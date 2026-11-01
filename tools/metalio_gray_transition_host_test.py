#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run S31's actual image outlet and same-pass white erase with native gray LUTs.
中文：验证图文擦白、灰阶保持、原厂脉冲、四方向对齐裁剪和失败保护。
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "epdiy.h"
#include "epd_highlevel.h"
#include "e0470_epaper_waveform.h"
#include "display_main_waveform.h"
#define W 1216
#define H 684
#define B (W*H/2)
#define ESP_LOGI(...) ((void)0)
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
#define LATENCY_DISPLAY_BEGIN() (++latency)
#define LATENCY_DISPLAY_END() (--latency)
static uint8_t front[B],back[B],saved[B],saved_back[B];
static enum EpdRotation rotation;
static bool s_system_binary,s_main_fault,force_full;
static int depth,latency,calls,fulls,picked_mode;
static enum EpdDrawError fail;
static const EpdWaveform *picked;
static EpdRect picked_area;
int epd_width(void){return W;}
int epd_height(void){return H;}
enum EpdRotation epd_get_rotation(void){return rotation;}
void *heap_caps_malloc(size_t n,int caps){assert(caps==3&&n<=50*64);return malloc(n);}
static bool pico_online_display_begin(void){++depth;return true;}
static void pico_online_display_end(bool held){assert(held&&depth==1);--depth;}
static bool display_needs_full(void){return force_full;}
static enum EpdDrawError update_display_full_unlocked(EpdiyHighlevelState *h){assert(depth&&!s_system_binary&&h);++fulls;return fail;}
static enum EpdDrawError update_display_area_diff_with_unlocked(EpdiyHighlevelState *h,const EpdWaveform *w,enum EpdDrawMode mode,EpdRect a){
 assert(depth==1&&!s_system_binary&&h->front_fb==front&&h->back_fb==back);++calls;
 picked=w;picked_mode=mode;picked_area=a;return fail;
}
'''
display = root / 'boards/metalio47/src/main/display.c'
unit += function('transition_gray_changed', display) + '\n'
unit += function('update_display_gray_transition', display) + '\n'
unit += r'''
static void set(int x,int y,unsigned value){size_t at=(size_t)y*W/2+x/2;unsigned shift=4*(x&1);front[at]=(front[at]&~(15u<<shift))|(value<<shift);}
static void pixel_position(int x,int y,int *px,int *py){
 switch(rotation){
  case EPD_ROT_LANDSCAPE:*px=x;*py=y;break;
  case EPD_ROT_PORTRAIT:*px=W-1-y;*py=x;break;
  case EPD_ROT_INVERTED_LANDSCAPE:*px=W-1-x;*py=H-1-y;break;
  default:*px=y;*py=H-1-x;break;
 }
}
int main(void){
 e0470_waveform_init();EpdiyHighlevelState h={.front_fb=front,.back_fb=back};
 const EpdWaveform *soft=display_main_shelf_soft_waveform(),*bw=display_main_bw_waveform();
 const EpdWaveformPhases *g=e0470_waveform_phases(soft,MODE_GL16),*d=e0470_waveform_phases(bw,MODE_DU);
 const EpdWaveformPhases *native_g=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);
 const EpdWaveformPhases *native_d=e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_DU);
 assert(g&&d&&g->phases==E0470_GL16_FRAMES&&d->phases==20);
 // 用户批准PR64默认灰阶；等值保持，中灰逐相对照该默认表。
 // User-approved PR64 default grays: equal targets hold; compare changed middle tones phase by phase.
 // black/white endpoints in the source remain the complete native DU20 followed by holds.
 for(int from=0;from<16;++from)for(int to=0;to<16;++to)for(int f=0;f<E0470_GL16_FRAMES;++f){
  int action=e0470_phase_action(g,f,to,from);
  assert(action==(from==to?0:to==0||to==15?(f<20?e0470_phase_action(native_d,f,to,from):0):e0470_phase_action(native_g,f,to,from)));
  if(f<20)assert(e0470_phase_action(d,f,to,from)==(from==to?0:e0470_phase_action(e0470_waveform_phases(&E0470_WAVEFORM,MODE_DU),f,to,from)));
 }
 const EpdWaveform *strong_gray=display_main_switch_waveform(soft,MODE_GL16);
 const EpdWaveform *strong_bw=display_main_switch_waveform(bw,MODE_DU);
 assert(strong_gray!=soft&&strong_bw!=bw);
 // The actual image profiles insert exactly two white-only drives, before the original release tail.
 // 中文：直接检验图文真实派生表；所有原有灰/黑脉冲保持原次序，未变白底不受推动。
 const EpdWaveform *originals[]={soft,bw},*strong[]={strong_gray,strong_bw};
 const int modes[]={MODE_GL16,MODE_DU};
 for(int kind=0;kind<2;++kind){
  const EpdWaveformPhases *a=e0470_waveform_phases(originals[kind],modes[kind]);
  const EpdWaveformPhases *b=e0470_waveform_phases(strong[kind],modes[kind]);
  assert(b&&b->phases==a->phases+2&&!b->phase_times);
  int insert=a->phases-2;
  for(int from=0;from<16;++from)for(int to=0;to<16;++to)for(int f=0;f<b->phases;++f){
   int expected=f<insert?e0470_phase_action(a,f,to,from):
     f<insert+2?(to==15&&from!=15?2:0):e0470_phase_action(a,f-2,to,from);
   assert(e0470_phase_action(b,f,to,from)==expected);
  }
 }
 for(int rot=0;rot<4;++rot)for(int from=0;from<16;++from)for(int to=0;to<16;++to){
  rotation=(enum EpdRotation)rot;memset(front,to*17,B);memset(back,from*17,B);memcpy(saved,front,B);memcpy(saved_back,back,B);
  EpdRect a={0,0,(rot&1)?H:W,(rot&1)?W:H};calls=fulls=0;s_system_binary=true;force_full=false;fail=EPD_DRAW_SUCCESS;
  bool gray=to!=from&&to!=0&&to!=15;
  assert(transition_gray_changed(&h,a)==gray);
  assert(update_display_gray_transition(&h,a)==EPD_DRAW_SUCCESS&&calls==1&&!fulls&&!depth&&!latency&&!s_main_fault&&s_system_binary);
  assert(picked==(gray?strong_gray:strong_bw)&&picked_mode==(gray?MODE_GL16:MODE_DU));
  assert(picked_area.width==a.width&&!memcmp(front,saved,B)&&!memcmp(back,saved_back,B));
 }
 // Independent pixel-coordinate oracle: include the real32-column expansion, exclude other rows/columns.
 for(int rot=0;rot<4;++rot){
  rotation=(enum EpdRotation)rot;EpdRect a={23,41,31,37};
  int minx=W,maxx=-1,miny=H,maxy=-1;
  for(int y=a.y;y<a.y+a.height;++y)for(int x=a.x;x<a.x+a.width;++x){int px,py;pixel_position(x,y,&px,&py);if(px<minx)minx=px;if(px>maxx)maxx=px;if(py<miny)miny=py;if(py>maxy)maxy=py;}
  int left=minx/32*32,right=(maxx+32)/32*32;if(right>W)right=W;
  const int points[][2]={{left,miny},{right-1,maxy},{left-1,miny},{right,miny},{left,miny-1},{left,maxy+1}};
  for(unsigned i=0;i<6;++i){
   memset(front,255,B);memset(back,255,B);
   int x=points[i][0],y=points[i][1];if(x>=0&&x<W&&y>=0&&y<H)set(x,y,7);
   assert(transition_gray_changed(&h,a)==(i<2));
  }
  memset(front,0x77,B);memset(back,0x77,B);assert(!transition_gray_changed(&h,a));
 }
 rotation=EPD_ROT_PORTRAIT;EpdRect a={0,80,H,W-80};
 assert(transition_gray_changed(NULL,a)&&transition_gray_changed(&h,(EpdRect){0,0,0,0}));
 for(int error=0;error<2;++error){
  fail=error?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;force_full=false;calls=fulls=0;s_system_binary=true;
  assert(update_display_gray_transition(&h,a)==fail&&calls==1&&!fulls&&s_main_fault==!!error&&s_system_binary&&!depth&&!latency);
  force_full=true;calls=fulls=0;
  assert(update_display_gray_transition(&h,a)==fail&&!calls&&fulls==1&&s_main_fault==!!error&&s_system_binary&&!depth&&!latency);
 }
 puts("PASS:1024 native-size old/target gray pairs; same-pass two white drives on image/exit only; every approved DU20/default-GL action and release tail preserved; equal pairs hold; four rotations/crop; one output/no quantization/no framebuffer overwrite; OTA/full/failure guards");
}
'''
with tempfile.TemporaryDirectory(prefix='s31-gray-transition-') as folder:
    c = Path(folder)/'test.c'
    exe = Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
        '-Imain','-Itools/shelf_waveform_host_stubs','-Itools/waveform_host_stubs',
        '-Icomponents/epdiy/include','-Icomponents/epdiy/src','-Icomponents/e0470_epaper_waveform/include',
        '-Icomponents/e0470_epaper_waveform/metalio',str(c),'main/display_main_waveform.c',
        'components/e0470_epaper_waveform/e0470_epaper_waveform.c',
        'components/e0470_epaper_waveform/e0470_waveform_trim.c','-o',str(exe)],cwd=root,check=True)
    subprocess.run([str(exe)],check=True)

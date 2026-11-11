#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：执行Pico速读出口与真实highlevel提交，检查旧灰阶、失败与整轮输出。
English: Execute Pico Fast BW and real highlevel commits, checking gray history, failures and single batches.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int id;}EpdWaveform;
typedef struct{uint8_t *front_fb,*back_fb,*difference_fb;bool *dirty_lines;uint8_t *dirty_columns;const EpdWaveform *waveform;}EpdiyHighlevelState;
enum EpdRotation{EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
enum EpdDrawMode{MODE_DU=1,MODE_GC16=2,MODE_GL16=5,MODE_PACKING_1PPB_DIFFERENCE=0x100};
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_EMPTY_LINE_QUEUE,EPD_DRAW_NO_PHASES_AVAILABLE,EPD_DRAW_OTHER_ERROR};
typedef int e0470_turn_dir_t;
enum{READ_PICO_EPD_SCAN_FAST,READ_PICO_EPD_SCAN_FULL};
#define ESP_LOGI(...) ((void)0)
#define _swap_int(a,b) do{int saved=(a);(a)=(b);(b)=saved;}while(0)
static void hl_record_timing(uint32_t a,uint32_t b,uint32_t c){(void)a;(void)b;(void)c;}
#define W 1216
#define H 684
#define B (W*H/2)
static uint8_t front[B],back[B],difference[W*H],columns[W/2],old_back[B],old_front[B];
static bool lines[H];
static enum EpdRotation rotation;
static EpdWaveform E0470_WAVEFORM={1},native_du={2},normal={3},E0470_FOLLOW_WAVEFORM={4},PICO_SETTINGS_MOTION_WAVEFORM={5};
static bool s_bulk_io;
static int effect=2,depth,latency,scans,recoveries,regular,fulls,prefill,scan_type;
static bool full_requested,intercept,available=true;
static enum EpdDrawError result,recovered;
static EpdRect requested;
static int64_t now;
static const char *TAG="test";
#define LATENCY_DISPLAY_BEGIN() (++latency)
#define LATENCY_DISPLAY_END() (--latency)
static int epd_width(void){return W;}
static int epd_height(void){return H;}
static enum EpdRotation epd_get_rotation(void){return rotation;}
static int app_settings_reader_turn_effect(void){return effect;}
static int64_t esp_timer_get_time(void){return ++now;}
static const EpdWaveform *display_reader_fast_text_waveform(void){return available?&native_du:NULL;}
static const EpdWaveform *display_main_gray_waveform(void){return &normal;}
static bool pico_online_display_begin(void){++depth;return true;}
static void pico_online_display_end(bool held){assert(held&&depth);--depth;}
static bool display_needs_full(void){return full_requested;}
static bool main_before_update(EpdiyHighlevelState*h,enum EpdDrawError*r){(void)h;if(intercept){*r=EPD_DRAW_SUCCESS;return true;}return false;}
static void read_pico_epd_use_scan(int n){scan_type=n;}
static void epd_lcd_set_prefill_lines(int n){prefill=n;}
static void epd_poweron(void){assert(depth);}
static void rails_keepalive(void){assert(depth);}
static void epd_hl_waveform(EpdiyHighlevelState*h,const EpdWaveform*w){h->waveform=w;}
static EpdRect epd_full_screen(void){return(EpdRect){0,0,W,H};}
static void epd_leading_skip_discard(void){}
static void epd_difference_column_range(EpdRect a,int *x0,int*x1){*x0=a.x&~31;*x1=(a.x+a.width+31)&~31;if(*x1>W)*x1=W;}
static EpdRect epd_difference_image_cropped(const uint8_t*to,const uint8_t*from,EpdRect a,uint8_t*d,bool*r,uint8_t*c){
 assert(to==front&&from==back&&d==difference&&r==lines&&c==columns);
 assert(!memcmp(back,old_back,B));memset(r,0,H*sizeof(bool));memset(c,0,W/2);
 int x0,x1;epd_difference_column_range(a,&x0,&x1);
 for(int y=a.y;y<a.y+a.height;++y){r[y]=true;for(int x=x0;x<x1;++x)d[y*W+x]=(uint8_t)(((front[y*W/2+x/2]>>((x&1)*4)&15)<<4)|(back[y*W/2+x/2]>>((x&1)*4)&15));}
 requested=a;return a;
}
static enum EpdDrawError epd_draw_base(EpdRect a,const uint8_t*d,EpdRect crop,enum EpdDrawMode m,int t,const bool*r,const uint8_t*c,const EpdWaveform*w){
 assert(depth&&a.width==W&&crop.height==H&&d==difference&&r==lines&&c==columns&&t==25);
 assert(w==&native_du&&m==(MODE_DU|MODE_PACKING_1PPB_DIFFERENCE));
 assert(scan_type==READ_PICO_EPD_SCAN_FULL&&prefill==127&&!memcmp(back,old_back,B));
 for(int y=requested.y;y<requested.y+requested.height;++y)for(int x=requested.x;x<requested.x+requested.width;++x){unsigned p=d[y*W+x];assert((p>>4)==0||(p>>4)==15);assert((p&15)==6);}
 ++scans;return result;
}
'''
highlevel=ROOT/'components/epdiy/src/highlevel.c'
unit+=function('_inverse_rotated_area',highlevel)+'\n'+function('hl_update_area',highlevel)+'\n'
unit+=r'''
static enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState*h,enum EpdDrawMode m,int t,EpdRect a){return hl_update_area(h,m,t,a,false);}
static enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*h,const EpdWaveform*w,int m,EpdRect a){(void)h;(void)a;assert(w==&normal&&m==MODE_GL16);++regular;return result;}
static enum EpdDrawError update_display_area_diff_with_unlocked(EpdiyHighlevelState*h,const EpdWaveform*w,int m,EpdRect a){return update_display_area_diff_with(h,w,m,a);}
static enum EpdDrawError update_display_full_unlocked(EpdiyHighlevelState*h){(void)h;++fulls;return result;}
static enum EpdDrawError main_recover(EpdiyHighlevelState*h,enum EpdDrawError e){
 assert(e==result&&e!=EPD_DRAW_SUCCESS&&!memcmp(back,old_back,B));++recoveries;
 if(recovered==EPD_DRAW_SUCCESS)memcpy(h->back_fb,h->front_fb,B);
 return recovered;
}
'''
unit+=function('display_reader_fast_text_prepare',ROOT/'main/display_reader_waveform.c')+'\n'
unit+=function('use_scan_for',ROOT/'main/display.c')+'\n'
unit+=function('update_display_text_turn',ROOT/'main/display.c')+'\n'
unit+=r'''
static void reset(void){memset(front,0x77,B);memset(back,0x66,B);memcpy(old_back,back,B);memcpy(old_front,front,B);scans=regular=recoveries=fulls=0;full_requested=intercept=false;available=true;effect=2;result=recovered=EPD_DRAW_SUCCESS;}
int main(void){
 EpdiyHighlevelState h={front,back,difference,lines,columns,&E0470_WAVEFORM};
 for(int rot=0;rot<4;++rot){
  rotation=(enum EpdRotation)rot;reset();EpdRect a={0,160,(rot&1)?H:W,320};
  assert(update_display_text_turn(&h,a,0)==EPD_DRAW_SUCCESS&&scans==1&&!regular&&!recoveries);
  assert(!depth&&!latency&&h.waveform==&E0470_WAVEFORM);
  for(int y=0;y<H;++y)for(int x=0;x<W;++x){bool inside=x>=requested.x&&x<requested.x+requested.width&&y>=requested.y&&y<requested.y+requested.height;size_t p=(size_t)y*W+x;assert(((front[p/2]>>((p&1)*4))&15)==(inside?0:7));assert(((back[p/2]>>((p&1)*4))&15)==(inside?0:6));}
 }
 rotation=EPD_ROT_INVERTED_PORTRAIT;EpdRect a={0,160,H,320};
 for(int failure=EPD_DRAW_EMPTY_LINE_QUEUE;failure<=EPD_DRAW_OTHER_ERROR;++failure)for(int fail_recovery=0;fail_recovery<2;++fail_recovery){
  reset();result=(enum EpdDrawError)failure;recovered=fail_recovery?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;
  enum EpdDrawError got=update_display_text_turn(&h,a,0);assert(scans==1&&!depth&&!latency);
  if(failure==EPD_DRAW_NO_PHASES_AVAILABLE)assert(regular==1&&!recoveries&&got==result&&!memcmp(back,old_back,B));
  else assert(recoveries==1&&got==recovered);
 }
 for(effect=0;effect<2;++effect){int saved=effect;reset();effect=saved;assert(update_display_text_turn(&h,a,0)==EPD_DRAW_SUCCESS&&!scans&&regular==1&&!memcmp(front,old_front,B));}
 reset();full_requested=true;assert(update_display_text_turn(&h,a,0)==EPD_DRAW_SUCCESS&&fulls==1&&!scans&&!memcmp(front,old_front,B));
 reset();intercept=true;assert(update_display_text_turn(&h,a,0)==EPD_DRAW_SUCCESS&&!scans&&!memcmp(front,old_front,B));
 reset();available=false;assert(update_display_text_turn(&h,a,0)==EPD_DRAW_SUCCESS&&regular==1&&!scans&&!memcmp(front,old_front,B));
 puts("PASS: actual Pico outlet/highlevel submits one native-DU batch,FULL timing/127 prefill,real gray history,4 rotations,success-only back commit,full priority,fallback and failed-scan recovery");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-fast-bw-') as tmp:
    c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-g','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

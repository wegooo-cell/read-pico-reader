#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Check the lock white pass against real highlevel full-frame history commits."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 1216
#define H 684
#define B (W*H/2)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(...) ((void)0)
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int id;}EpdWaveform;
typedef struct{uint8_t *front_fb,*back_fb,*difference_fb;bool *dirty_lines;uint8_t *dirty_columns;const EpdWaveform *waveform;}EpdiyHighlevelState;
enum EpdDrawMode{MODE_GC16=2,MODE_PACKING_1PPB_DIFFERENCE=256};
enum EpdDrawError{EPD_DRAW_SUCCESS=0,EPD_DRAW_FAILED_ALLOC=0x10,EPD_DRAW_EMPTY_LINE_QUEUE=0x400};
static uint8_t front[B],back[B],original[B],diff[W*H],columns[W/2];
static bool lines[H],oom;static unsigned calls,fail_at,live;
static int epd_width(void){return W;}static int epd_height(void){return H;}
static EpdRect epd_full_screen(void){return(EpdRect){0,0,W,H};}
static int64_t esp_timer_get_time(void){return 0;}
static void hl_record_timing(int a,int b,int c){(void)a;(void)b;(void)c;}
static void *heap_caps_aligned_alloc(size_t a,size_t n,unsigned caps){assert(a==16&&n==B&&caps==3);if(oom)return NULL;++live;return malloc(n);}
static void heap_caps_free(void*p){assert(p&&live==1);--live;free(p);}
static EpdRect epd_difference_image_cropped(const uint8_t*t,const uint8_t*f,EpdRect a,uint8_t*d,bool*r,uint8_t*c){
 assert(a.width==W&&a.height==H&&f==back&&d==diff&&r==lines&&c==columns);
 for(size_t i=0;i<B;++i){d[2*i]=((t[i]&15)<<4)|(f[i]&15);d[2*i+1]=(t[i]&240)|(f[i]>>4);}
 memset(r,0,sizeof(lines));memset(c,0,sizeof(columns));return a;
}
static enum EpdDrawError epd_draw_base(EpdRect a,const uint8_t*d,EpdRect crop,enum EpdDrawMode m,int t,const bool*r,const uint8_t*c,const EpdWaveform*w){
 (void)crop;(void)w;assert(a.width==W&&m==(MODE_GC16|MODE_PACKING_1PPB_DIFFERENCE)&&t==25&&!memcmp(front,original,B));
 ++calls;assert(live==(calls==1));
 for(int y=0;y<H;++y)assert(r[y]);for(int i=0;i<W/2;++i)assert(c[i]==255);
 for(size_t i=0;i<W*H;++i){unsigned target=calls==1?15:(front[i/2]>>(4*(i&1)))&15;assert((d[i]>>4)==target);assert((d[i]&15)==(calls==1?5:15));}
 return calls==fail_at?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS;
}
'''
unit+=function('epd_hl_update_screen_full',ROOT/'components/epdiy/src/highlevel.c')+'\n'
unit+='static enum EpdDrawError update_display_full(EpdiyHighlevelState*h){return epd_hl_update_screen_full(h,MODE_GC16,25);}\n'
unit+=function('present_clean_lock',ROOT/'main/sleep.c')+'\n'
unit+=r'''
int main(void){
 EpdiyHighlevelState h={front,back,diff,lines,columns,NULL};
 for(size_t i=0;i<B;++i)original[i]=(uint8_t)(i*37u+11u);
 for(unsigned failure=0;failure<4;++failure){
  memcpy(front,original,B);memset(back,0x55,B);calls=0;fail_at=failure;oom=failure==3;
  enum EpdDrawError e=present_clean_lock(&h);
  assert(e==(oom?EPD_DRAW_FAILED_ALLOC:failure?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS));
  assert(!live&&h.front_fb==front&&h.back_fb==back&&!memcmp(front,original,B));
  assert(calls==(oom?0:failure==1?1:2));
  if(!failure)assert(!memcmp(back,front,B));
  else for(size_t i=0;i<B;++i)assert(back[i]==(failure==2?255:0x55));
 }
 puts("PASS: real highlevel commits white only after success, retains final art/pointers, two outputs on success, stops on either failure, frees temporary canvas and handles OOM without scans");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-lock-white-') as tmp:
    c=Path(tmp)/'test.c';c.write_text(unit);exe=Path(tmp)/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

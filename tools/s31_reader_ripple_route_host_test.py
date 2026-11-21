#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute the real S31 reader dispatcher: paced white transition only for ripple."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct {int id;}EpdiyHighlevelState;
typedef struct {int x,y,width,height;}EpdRect;
typedef struct {int id;}EpdWaveform;
typedef int display_reader_gray_profile_t;typedef int e0470_turn_dir_t;
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_NO_PHASES_AVAILABLE,EPD_DRAW_OTHER_ERROR};
enum{DISPLAY_READER_GRAY_PAGE,DISPLAY_READER_GRAY_TEXT,DISPLAY_READER_GRAY_BACKGROUND,DISPLAY_READER_GRAY_IMAGE,DISPLAY_READER_GRAY_WATER_TAIL,READ_PICO_EPD_SCAN_FAST,MODE_GL16};
static EpdWaveform E0470_WAVEFORM, wave;
static bool s_bulk_io,background,unavailable;static int gray_calls,wave_calls,turn_calls,scheduled,profile_seen,prefill,powered,rails;
static enum EpdDrawError result;
static const char *app_settings_book_background_path(void){return background?"/sdcard/bg.png":"";}
static enum EpdDrawError reader_gray_present_unlocked(EpdiyHighlevelState*h,EpdRect r,int p,bool full){assert(h&&r.x==8&&r.height==600);assert(full==(p==DISPLAY_READER_GRAY_PAGE));++gray_calls;profile_seen=p;return result;}
static const EpdWaveform *display_reader_ripple_waveform(int p){++wave_calls;profile_seen=p;return unavailable?NULL:&wave;}
static void read_pico_epd_use_scan(int s){assert(s==READ_PICO_EPD_SCAN_FAST);}
static void epd_lcd_set_prefill_lines(int n){prefill=n;}
static void epd_poweron(void){++powered;}
static enum EpdDrawError e0470_reader_ripple_with_waveform_timed(EpdiyHighlevelState*h,EpdRect r,int d,const EpdWaveform*w,unsigned b,unsigned t){assert(h&&r.x==8&&r.height==600&&d==2&&w==&wave&&b==4&&t==3);++turn_calls;return result;}
static void use_scan_for(const EpdWaveform*w,int m){assert(w==&E0470_WAVEFORM&&m==MODE_GL16);}
static void rails_keepalive(void){++rails;}
static void reader_rails_schedule_check(void){++scheduled;}
'''
unit+=function('reader_gl37_turn_unlocked',R/'boards/metalio47/src/main/display.c')+'\n'
unit+=r'''
int main(void){EpdiyHighlevelState h={0};EpdRect r={8,28,640,600};
 for(int image=0;image<2;++image)for(int bg=0;bg<2;++bg){
  background=bg;gray_calls=wave_calls=turn_calls=scheduled=powered=rails=0;result=EPD_DRAW_SUCCESS;
  assert(!reader_gl37_turn_unlocked(&h,r,2,1,image)&&gray_calls==1&&!wave_calls&&!turn_calls);
  assert(profile_seen==(image?DISPLAY_READER_GRAY_IMAGE:bg?DISPLAY_READER_GRAY_BACKGROUND:DISPLAY_READER_GRAY_TEXT));
  for(int bulk=0;bulk<2;++bulk){s_bulk_io=bulk;assert(!reader_gl37_turn_unlocked(&h,r,2,8,image));assert(prefill==(bulk?127:64));assert(profile_seen==(image?DISPLAY_READER_GRAY_IMAGE:bg?DISPLAY_READER_GRAY_BACKGROUND:DISPLAY_READER_GRAY_WATER_TAIL));}
  assert(turn_calls==2&&scheduled==2&&powered==2&&rails==2);
 }
 unavailable=true;gray_calls=turn_calls=0;assert(!reader_gl37_turn_unlocked(&h,r,2,8,false)&&gray_calls==1&&!turn_calls&&profile_seen==DISPLAY_READER_GRAY_PAGE);
 unavailable=false;result=EPD_DRAW_NO_PHASES_AVAILABLE;gray_calls=0;assert(reader_gl37_turn_unlocked(&h,r,2,8,false)==result&&gray_calls==1);
 result=EPD_DRAW_OTHER_ERROR;gray_calls=scheduled=0;assert(reader_gl37_turn_unlocked(&h,r,2,8,false)==result&&!gray_calls&&!scheduled);
 puts("PASS: actual S31 reader route preserves synchronous text/image/background, applies four-band three-tick white transition only to ripple, retains crop/prefill/rails and checked fallback");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(unit);subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)

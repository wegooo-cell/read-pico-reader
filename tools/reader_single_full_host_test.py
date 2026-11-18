#!/usr/bin/env python3
"""验证正常全刷一轮GC48，无盲清或二次补字；故障恢复保持。
Verify one GC48 pass without a blind preclear or text replay, retaining fault recovery.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
driver = root/'boards/metalio47/src/main/display.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct{int id;}EpdWaveform;
typedef struct{int id;}EpdiyHighlevelState;
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
enum EpdDrawMode{MODE_GC16};
static EpdWaveform E0470_FULL_WAVEFORM={48},E0470_WAVEFORM={53};
static bool s_full_refresh_requested,s_system_binary,s_main_fault;
static int scans,clears,replays,recoveries,locks,cancels,powerons,releases,normalized;
static enum EpdDrawError failure;
#define LATENCY_DISPLAY_BEGIN() ((void)0)
#define LATENCY_DISPLAY_END() ((void)0)
static bool pico_online_display_begin(void){assert(!locks);++locks;return true;}
static void pico_online_display_end(bool held){assert(held&&locks==1);--locks;}
static void system_normalize(EpdiyHighlevelState*h){(void)h;++normalized;}
static enum EpdDrawError main_recover(EpdiyHighlevelState*h,enum EpdDrawError e){(void)h;(void)e;++recoveries;return failure;}
static void display_main_transition_cancel(void){++cancels;}
static void epd_hl_waveform(EpdiyHighlevelState*h,const EpdWaveform*w){(void)h;assert(w==&E0470_FULL_WAVEFORM||w==&E0470_WAVEFORM);}
static void epd_poweron(void){++powerons;}
static void rails_keepalive(void){++releases;}
static enum EpdDrawError hl_update(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m,bool full,const void*area,bool diff){
 (void)h;assert(locks&&w==&E0470_FULL_WAVEFORM&&m==MODE_GC16&&full&&!area&&!diff);++scans;return failure;
}
'''+function('update_display_full_unlocked',driver)+function('update_display_full',driver)+r'''
int main(void){
 EpdiyHighlevelState hl={0};
 for(int destination=0;destination<3;++destination)for(int pending=0;pending<2;++pending){
  int old=scans;
  s_full_refresh_requested=pending;s_system_binary=destination==2;
  assert(update_display_full(&hl)==EPD_DRAW_SUCCESS);
  assert(scans==old+1&&!s_full_refresh_requested&&!s_main_fault&&!clears&&!replays&&!recoveries&&!locks);
 }
 assert(cancels==6&&powerons==6&&releases==6&&normalized==2);
 failure=EPD_DRAW_OTHER_ERROR;
 assert(update_display_full(&hl)==EPD_DRAW_OTHER_ERROR&&s_main_fault&&!locks);
 int old=scans;
 failure=EPD_DRAW_SUCCESS;
 assert(update_display_full(&hl)==EPD_DRAW_SUCCESS&&!s_main_fault&&recoveries==1&&scans==old&&!locks);
 puts("PASS: healthy entry/TOC/Home full output is one native GC48; pending requests coalesce, targets retain grays and fault recovery remains available");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-single-full-') as temporary:
    folder=Path(temporary);source=folder/'test.c';source.write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(source),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)

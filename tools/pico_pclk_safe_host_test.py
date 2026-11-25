#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：保留欠载恢复，像素时钟自动回升须另行验收。
English: Retain underrun recovery without promoting PCLK before separate acceptance."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1];source=root/'boards/pico/src/main/display.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef int EpdWaveform;
enum EpdDrawMode{MODE_DU=1,MODE_GC16=2};
enum EpdDrawError{EPD_DRAW_SUCCESS=0,EPD_DRAW_EMPTY_LINE_QUEUE=1,EPD_DRAW_ERROR=2};
typedef int EpdiyHighlevelState;
#define DISPLAY_PCLK_DEFAULT_MHZ 18
#define DISPLAY_PCLK_SAFE_MHZ 12
#define READ_PICO_EPD_SCAN_FAST 1
#define READ_PICO_EPD_SCAN_FULL 0
#define TAG "test"
#define ESP_LOGW(...) ((void)0)
static EpdWaveform E0470_WAVEFORM,E0470_FOLLOW_WAVEFORM,PICO_SETTINGS_MOTION_WAVEFORM;
static int s_pclk_mhz=DISPLAY_PCLK_DEFAULT_MHZ,clock_changes,applied_clock,scan,clears,recoveries,prefill;
static bool s_main_fault,s_bulk_io;
static unsigned s_page_refreshes;
static enum EpdDrawError recovery_result;
static void read_pico_epd_set_pclk(int mhz){applied_clock=mhz;++clock_changes;}
static void read_pico_epd_use_scan(int mode){scan=mode;}
static void epd_lcd_set_prefill_lines(int lines){prefill=lines;}
static void display_main_transition_cancel(void){}
static void epd_poweron(void){}
static void epd_clear(void){++clears;}
static void rails_keepalive(void){}
static enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState*hl,int mode,int temp){assert(hl&&mode==MODE_GC16&&temp==25);++recoveries;return recovery_result;}
'''
unit+=function('use_scan_for',source)+'\n'+function('guard_draw_result_unlocked',source)+'\n'
unit+=r'''
int main(void){EpdiyHighlevelState state=0;
 use_scan_for(&E0470_WAVEFORM,MODE_GC16);assert(!clock_changes&&s_pclk_mhz==18&&scan==0&&prefill==32);
 s_bulk_io=true;use_scan_for(&E0470_WAVEFORM,MODE_GC16);assert(prefill==127);s_bulk_io=false;
 guard_draw_result_unlocked(&state,EPD_DRAW_SUCCESS);assert(!recoveries&&!clears);
 guard_draw_result_unlocked(&state,EPD_DRAW_EMPTY_LINE_QUEUE);assert(s_pclk_mhz==12&&applied_clock==12&&recoveries==1&&clears==1&&!s_main_fault);
 for(int i=0;i<5000;++i){use_scan_for(&E0470_FOLLOW_WAVEFORM,MODE_DU);assert(scan==1&&prefill==64);use_scan_for(&E0470_WAVEFORM,MODE_GC16);assert(s_pclk_mhz==12&&applied_clock==12&&clock_changes==1);}
 recovery_result=EPD_DRAW_ERROR;guard_draw_result_unlocked(&state,EPD_DRAW_EMPTY_LINE_QUEUE);assert(s_main_fault&&recoveries==2&&s_pclk_mhz==12);
 puts("PASS: real underrun drops to safe PCLK, preserves error reporting and never auto-promotes during later outputs");
}
'''
driver=(root/'boards/pico/src/components/epdiy/src/output_lcd/render_lcd.c').read_text()
assert 'epd_scan_hook(true);' in driver and 'epd_scan_hook(false);' in driver
assert 'pclk_retry_default' not in source.read_text()
with tempfile.TemporaryDirectory(prefix='pico-safe-clock-') as directory:
    p=Path(directory);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

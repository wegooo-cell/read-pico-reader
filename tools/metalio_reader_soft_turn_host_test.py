#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""核对Moku普通原字节、八相擦白、活动灰阶/OOM回退及独立DU加四白；不连接设备。
Check Moku ordinary bytes, eight white phases, active-gray/OOM fallback and separate DU plus four whites; no device access.
"""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
from board_source_view import materialize

ROOT = Path(__file__).resolve().parents[1]
# PR6 3ea8115 的真实GL37+6输出；保留贡献者普通方案，不将光学效果视为已验证。
# Actual GL37+6 output from PR6 3ea8115; preserve its ordinary profile without claiming optical validation.
MOKU_GL37_WHITE6_SHA256 = 'b1380d6ebe9496455a85338c4396011f6f8b242a765d6c1ee457b540056cb39e'
UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display_reader_waveform.h"
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
static bool oom;
static int allocations;
void *heap_caps_malloc(size_t n,int caps){assert(caps==3);++allocations;return oom?NULL:malloc(n);}
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
enum EpdRotation epd_get_rotation(void){return EPD_ROT_INVERTED_PORTRAIT;}
static int trailing_holds(const EpdWaveformPhases *p){
 int count=0;
 for(int f=p->phases-1;f>=0;--f){bool active=false;
  for(int t=0;t<16;++t)for(int a=0;a<16;++a)active|=e0470_phase_action(p,f,t,a)!=0;
  if(active)break;
  ++count;
 }
 return count;
}
int main(int argc,char **argv){
 assert(argc==3);oom=atoi(argv[1]);e0470_waveform_init();
 const EpdWaveformPhases *raw=e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_GL16);
 unsigned char saved[48*64];assert(raw&&raw->phases==48);memcpy(saved,raw->luts,sizeof(saved));
 const EpdWaveform *normal=display_reader_soft_turn_waveform();
 const EpdWaveformPhases *n=e0470_waveform_phases(normal,MODE_GL16);
 const EpdWaveform *panel_wave=display_reader_panel37_waveform();
 const EpdWaveform *base_wave=panel_wave?panel_wave:&E0470_WAVEFORM;
 const EpdWaveformPhases *base=e0470_waveform_phases(base_wave,MODE_GL16);
 int hold=trailing_holds(base),drive=base->phases-hold;
 if(hold<1){
  assert(normal==&E0470_WAVEFORM&&n==e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16));
 }else{
  assert(normal!=&E0470_WAVEFORM&&n&&n->phases==base->phases+8);
  for(int to=0;to<16;++to)for(int from=0;from<16;++from){
   for(int f=0;f<drive;++f){int expected=e0470_phase_action(base,f,to,from);
    if(to==0&&from==0&&expected==2)expected=0;
    assert(e0470_phase_action(n,f,to,from)==expected);
   }
   for(int f=drive;f<drive+8;++f)assert(e0470_phase_action(n,f,to,from)==(to==15?2:0));
   for(int f=0;f<hold;++f)assert(e0470_phase_action(n,drive+8+f,to,from)==e0470_phase_action(base,drive+f,to,from));
  }
 }
 assert(display_reader_soft_turn_waveform()==normal);
 assert(!memcmp(saved,raw->luts,sizeof(saved)));
 assert(allocations==(KIIKO_S31_READER_PANEL37?1:0));
 FILE *dump=fopen(argv[2],"wb");assert(dump&&fwrite(n->luts,64,n->phases,dump)==(size_t)n->phases&&fclose(dump)==0);
 const EpdWaveform *binary=&E0470_FULL_WAVEFORM;
#if KIIKO_S31_DU14
 binary=display_du14_white_waveform();
#endif
 const EpdWaveformPhases *du=e0470_waveform_phases(binary,MODE_DU);
 unsigned char old_du[24*64];assert(du&&du->phases<=20);memcpy(old_du,du->luts,(size_t)du->phases*64);
 const EpdWaveform *fast=display_reader_fast_text_waveform();
 const EpdWaveformPhases *b=e0470_waveform_phases(fast,MODE_GL16);
 assert(fast&&fast==display_reader_fast_text_waveform()&&fast!=normal&&b&&b->phases==du->phases+4);
 assert(!e0470_waveform_phases(fast,MODE_GC16)&&!e0470_waveform_phases(fast,MODE_DU));
 hold=trailing_holds(du);drive=du->phases-hold;assert(trailing_holds(b)==hold);
 for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  for(int f=0;f<drive;++f)assert(e0470_phase_action(b,f,to,from)==e0470_phase_action(du,f,to,from));
  for(int f=drive;f<drive+4;++f)assert(e0470_phase_action(b,f,to,from)==(to==15?2:0));
  for(int f=0;f<hold;++f)assert(e0470_phase_action(b,drive+4+f,to,from)==e0470_phase_action(du,drive+f,to,from));
 }
 assert(!memcmp(old_du,du->luts,(size_t)du->phases*64)&&!memcmp(saved,raw->luts,sizeof(saved)));
 printf("PASS: panel37=%d OOM=%d normal=%d fast=%d; all256 pairs, vendor bytes, white-only extras, original DU settling and bounded caches\n",KIIKO_S31_READER_PANEL37,oom,n->phases,b->phases);
}
'''

with tempfile.TemporaryDirectory(prefix='s31-reader-profiles-') as temporary:
    folder = Path(temporary)
    view = materialize(ROOT, folder / 'view', 'metalio47', tests=True)
    test = folder / 'test.c'
    test.write_text(UNIT)
    headers = (view / 'components/e0470_epaper_waveform/include/e0470_epaper_waveform.h').read_text()
    variants = (0, 1) if 'e0470_du14_waveform' in headers else (0,)
    linker = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
    flags = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
             '-ffunction-sections', '-fdata-sections', linker, '-DKIIKO_S31_FAST_READER_PANELS=0',
             '-Imain', '-Itools/shelf_waveform_host_stubs', '-Itools/waveform_host_stubs',
             '-Icomponents/epdiy/include', '-Icomponents/epdiy/src',
             '-Icomponents/e0470_epaper_waveform/include', '-Icomponents/e0470_epaper_waveform/metalio']
    sources = [str(test), 'main/display_reader_waveform.c', 'main/display_main_waveform.c',
               'components/e0470_epaper_waveform/e0470_epaper_waveform.c',
               'components/e0470_epaper_waveform/e0470_waveform_trim.c']
    for du14 in variants:
        for panel37 in (0, 1):
            exe = folder / f'profile-{du14}-{panel37}'
            subprocess.run(flags + [f'-DKIIKO_S31_DU14={du14}',
                           f'-DKIIKO_S31_READER_PANEL37={panel37}', *sources, '-o', str(exe)], cwd=view, check=True)
            for oom in (0, 1):
                dump = folder / 'normal.lut'
                subprocess.run([str(exe), str(oom), str(dump)], check=True)
                assert len(dump.read_bytes()) in (37*64,45*64)

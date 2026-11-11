#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：设置列表14相实验的实际LUT及阅读/停稳灰阶隔离。/ English: Actual list14 LUT and reader/idle-gray isolation."""
from pathlib import Path
import hashlib
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
raw=root/'boards/metalio47/src/components/e0470_epaper_waveform/my_waveform_du14.h'
assert hashlib.sha256(raw.read_bytes()).hexdigest()=='cac0efbcb2801114b36a4bee5eaa9c453dcd7d098210ef2739f853e86a0c39f4'
unit=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "display_reader_waveform.h"
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
#include "my_waveform_du14.h"
static unsigned allocations;
void *heap_caps_malloc(size_t n,int caps){assert(caps==3);++allocations;return malloc(n);}
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
enum EpdRotation epd_get_rotation(void){return EPD_ROT_INVERTED_PORTRAIT;}
static void dump(FILE *out,const EpdWaveform *wave,int mode){
 const EpdWaveformPhases *p=e0470_waveform_phases(wave,mode);
 assert(p&&p->luts&&!p->phase_times);
 assert(fwrite(&p->phases,sizeof(p->phases),1,out)==1);
 assert(fwrite(p->luts,64,p->phases,out)==(size_t)p->phases);
}
int main(int argc,char **argv){
 assert(argc==2);e0470_waveform_init();unsigned before=allocations;
 (void)&MY_WAVEFORM_DU14;
 const EpdWaveform *motion=e0470_settings_motion_waveform();
 assert(e0470_settings_motion_waveform()==motion&&allocations==before);
 assert(!e0470_du14_waveform()); // 全局DU14保持关闭。/ Global DU14 remains OFF.
 assert(e0470_waveform_phases(display_main_bw_waveform(),MODE_DU)->phases==20);
 assert(e0470_waveform_phases(display_main_page_bw_waveform(),MODE_DU)->phases==20);
 assert(e0470_waveform_phases(display_reader_fast_text_waveform(),MODE_GL16)->phases==24);
#if KIIKO_S31_SETTINGS_SHORT_MOTION
 assert(motion&&motion!=&MY_WAVEFORM_DU14&&motion->num_modes==1&&motion->num_temp_ranges==1);
 assert(motion->temp_intervals[0].min==0&&motion->temp_intervals[0].max==50);
 const EpdWaveformPhases *p=e0470_waveform_phases(motion,MODE_DU);
 assert(p&&p->phases==14&&!p->phase_times);
 for(int f=0;f<14;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  int expected=to==from?0:e0470_phase_action(&my_du14_phases,f,to,from);
  assert(e0470_phase_action(p,f,to,from)==expected);
 }
 int black=0,white=0;
 for(int f=0;f<14;++f){black+=e0470_phase_action(p,f,0,15)==1;white+=e0470_phase_action(p,f,15,0)==2;}
 assert(black==13&&white==14);
#else
 assert(!motion);
#endif
 FILE *out=fopen(argv[1],"wb");assert(out);
 dump(out,&E0470_FULL_WAVEFORM,MODE_DU);dump(out,&E0470_FULL_WAVEFORM,MODE_GC16);dump(out,&E0470_FULL_WAVEFORM,MODE_GL16);
 dump(out,&E0470_WAVEFORM,MODE_DU);dump(out,&E0470_WAVEFORM,MODE_GL16);
 dump(out,&E0470_FOLLOW_WAVEFORM,MODE_DU);dump(out,display_reader_text_waveform(),MODE_GL16);
 dump(out,display_main_normal_waveform(),MODE_GL16);dump(out,display_main_gray_waveform(),MODE_GL16);
 dump(out,display_main_navigation_waveform(),MODE_GL16);
 dump(out,display_main_shelf37_waveform(false),MODE_GL16);dump(out,display_main_shelf37_waveform(true),MODE_GL16);
 dump(out,display_reader_panel37_waveform(),MODE_GL16);
 dump(out,display_reader_panel_gray4_waveform(false),MODE_GL16);dump(out,display_reader_panel_gray4_waveform(true),MODE_GL16);
 const EpdWaveform *upper=display_settings_scroll_waveform(),*lower=display_settings_scroll_lower_waveform();
 for(int i=0;i<2;++i){
  const EpdWaveform *wave=i?lower:upper;
  const EpdWaveformPhases *p=e0470_waveform_phases(wave,MODE_GL16);
  assert(p&&p->phases==48+(i?10:8)+2);
  for(int f=0;f<p->phases;++f)for(int from=0;from<16;++from){
   int white_action=e0470_phase_action(p,f,15,from);assert(white_action==0||white_action==2);
  }
  dump(out,wave,MODE_GL16);
 }
 assert(!fclose(out));
 puts("PASS: settings-only14 phases/3584 transitions,13 black14 white drives,equal pairs held,no heap; global BW20/reader24 and soft-white idle profiles retained");
}
'''
with tempfile.TemporaryDirectory(prefix='s31-settings-motion-wave-') as folder:
    p=Path(folder);(p/'test.c').write_text(unit)
    (p/'epd_internals.h').write_text('#include "epd_waveform.h"\n')
    flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
           '-DKIIKO_BOARD_METALIO47=1','-DKIIKO_S31_DU14=0','-DKIIKO_S31_SHELF37=1','-DKIIKO_S31_READER_PANEL37=1','-DKIIKO_S31_FAST_READER_PANELS=1',
           '-I'+str(p),'-Imain','-Itools/shelf_waveform_host_stubs','-Itools/waveform_host_stubs',
           '-Icomponents/epdiy/include','-Icomponents/epdiy/src','-Icomponents/e0470_epaper_waveform/include',
           '-Icomponents/e0470_epaper_waveform/metalio','-Iboards/metalio47/src/components/e0470_epaper_waveform']
    files=[str(p/'test.c'),'boards/metalio47/src/main/display_main_waveform.c','boards/metalio47/src/main/display_reader_waveform.c',
           'boards/metalio47/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c','components/e0470_epaper_waveform/e0470_waveform_trim.c']
    profiles=[]
    for enabled in (0,1):
        exe=p/f'test-{enabled}';output=p/f'profiles-{enabled}'
        subprocess.run(flags+[f'-DKIIKO_S31_SETTINGS_SHORT_MOTION={enabled}',*files,'-o',str(exe)],cwd=root,check=True)
        subprocess.run([str(exe),str(output)],check=True);profiles.append(output.read_bytes())
    assert profiles[0]==profiles[1]
    print('PASS:17 reader/native/gray/idle profiles byte-identical with settings short motion OFF/ON;SHA256',hashlib.sha256(profiles[0]).hexdigest())

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：验证Pico列表专用白向维护及原波形隔离；残影仍须实屏验收。
English: Verify Pico list-only white maintenance and original profile isolation; optical ghosts require panel tests.
"""
from pathlib import Path
import hashlib
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "display_main_waveform.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
enum EpdRotation epd_get_rotation(void){return EPD_ROT_PORTRAIT;}
static unsigned char before[128*1024],after[128*1024];
static size_t snapshot(unsigned char *target){
 const EpdWaveform *waves[]={&E0470_WAVEFORM,&E0470_FULL_WAVEFORM,&E0470_GRAY8_WAVEFORM,
  &E0470_TEXTTURN_WAVEFORM,&E0470_FOLLOW_WAVEFORM,&E0470_APPLY_WAVEFORM,
  display_main_bw_waveform(),display_main_gray_waveform(),display_main_normal_waveform(),
  display_main_shelf_exit_waveform(),display_main_water_waveform(),display_main_ui_waveform(),
  display_main_shelf_waveform(MODE_GL16),display_main_shelf_waveform(MODE_GC16),
  display_settings_scroll_waveform(),display_reader_soft_turn_waveform(),
  display_reader_text_waveform(),display_reader_water_waveform(),display_reader_fast_text_waveform()};
 size_t used=0;
 for(unsigned n=0;n<sizeof(waves)/sizeof(waves[0]);++n){
  const EpdWaveform *w=waves[n];assert(w);
  for(int m=0;m<w->num_modes;++m){
   const EpdWaveformMode *mode=w->mode_data[m];assert(mode);
   for(int r=0;r<mode->temp_ranges;++r){
    const EpdWaveformPhases *p=mode->range_data[r];assert(p&&p->luts&&!p->phase_times);
    size_t bytes=(size_t)p->phases*64u;assert(used+bytes+sizeof(p->phases)<sizeof(before));
    memcpy(target+used,&p->phases,sizeof(p->phases));used+=sizeof(p->phases);
    memcpy(target+used,p->luts,bytes);used+=bytes;
   }
  }
 }
 return used;
}
int main(int argc,char **argv){
 assert(argc==2);e0470_waveform_init();
 const size_t old_size=snapshot(before);
 const EpdWaveformPhases *follow=e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM,MODE_DU);
 assert(follow&&follow->phases==8);uint8_t original[8*64];memcpy(original,follow->luts,sizeof(original));
 const EpdWaveform *wave=display_settings_motion_waveform();
 assert(wave==&PICO_SETTINGS_MOTION_WAVEFORM&&wave!=&E0470_FOLLOW_WAVEFORM);
 const EpdWaveformPhases *motion=e0470_waveform_phases(wave,MODE_DU);
 assert(motion&&motion->phases==12&&!motion->phase_times);
 assert(wave->num_modes==1&&wave->num_temp_ranges==1&&wave->temp_intervals[0].min==0&&wave->temp_intervals[0].max==50);
 assert(!memcmp(motion->luts,original,sizeof(original)));
 for(int from=0;from<16;++from)for(int to=0;to<16;++to)for(int f=0;f<12;++f){
  int expected=f<8?e0470_phase_action(follow,f,to,from):
    to==15&&from!=15?2:0;
  assert(e0470_phase_action(motion,f,to,from)==expected);
 }
 int black=0,white=0,equal_white=0;
 for(int f=0;f<12;++f){
  black+=e0470_phase_action(motion,f,0,15)==1;
  white+=e0470_phase_action(motion,f,15,0)==2;
  equal_white+=e0470_phase_action(motion,f,15,15)==2;
  assert(e0470_phase_action(motion,f,0,0)==0);
 }
 assert(black==7&&white==12&&equal_white==0);
 for(int gray=0;gray<16;++gray)for(int f=0;f<12;++f)assert(e0470_phase_action(motion,f,gray,gray)==0);
 for(int n=0;n<100;++n)assert(display_settings_motion_waveform()==wave);
 assert(snapshot(after)==old_size&&!memcmp(before,after,old_size));
 assert(!memcmp(follow->luts,original,sizeof(original)));
 FILE *out=fopen(argv[1],"wb");assert(out&&fwrite(before,1,old_size,out)==old_size);assert(fclose(out)==0);
 puts("PASS: Pico list DU12/3072 transitions; FOLLOW8 prefix exact, black7 retained,white12/all16 equal pairs held;19 existing native/reader/gray/idle profiles byte-identical and native FOLLOW8 untouched");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-settings-white-motion-') as directory:
    p=Path(directory);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-I'+str(ROOT/'main'),'-I'+str(ROOT/'main/app'),
                    '-I'+str(ROOT/'tools/waveform_host_stubs'),'-I'+str(ROOT/'components/epdiy/include'),
                    '-I'+str(ROOT/'components/epdiy/src'),'-I'+str(ROOT/'components/e0470_epaper_waveform/include'),
                    '-I'+str(ROOT/'components/e0470_epaper_waveform/waveforms'),
                    str(p/'test.c'),str(ROOT/'main/display_main_waveform.c'),str(ROOT/'main/display_reader_waveform.c'),
                    str(ROOT/'components/e0470_epaper_waveform/e0470_epaper_waveform.c'),
                    str(ROOT/'components/e0470_epaper_waveform/e0470_waveform_trim.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(p/'profiles.bin')],check=True)
    print('Retained19-profile SHA256:',hashlib.sha256((p/'profiles.bin').read_bytes()).hexdigest())

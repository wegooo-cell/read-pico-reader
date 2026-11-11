#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：验证Pico自己的根页六/八相擦白LUT和45相条带边界。
English: Verify Pico-native six/eight-phase root whitening LUTs and the45-phase band boundary.
"""
from pathlib import Path
import re
import subprocess
import tempfile
from board_source_view import select_source
from home_cover_cache_host_test import function

ROOT=Path(__file__).resolve().parents[1]
UNIT=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
static bool oom;
static unsigned allocations;
static size_t bytes;
void *heap_caps_malloc(size_t n,int caps){assert(caps==3);if(oom)return NULL;++allocations;bytes+=n;return malloc(n);}
static void verify(const EpdWaveform *source,int mode,unsigned white){
 const EpdWaveformPhases *s=e0470_waveform_phases(source,mode);assert(s&&s->phases<=37);
 uint8_t saved[37*64];memcpy(saved,s->luts,(size_t)s->phases*64);
 const EpdWaveform *wave=display_main_clean_waveform_with_phases(source,mode,white);
#if KIIKO_PICO_MAIN_CLEAN
 const EpdWaveformPhases *p=e0470_waveform_phases(wave,mode);
 assert(wave!=source&&p&&p->phases>=s->phases+(int)white&&p->phases<=45&&!p->phase_times);
 assert(wave->temp_intervals==source->temp_intervals&&wave->num_temp_ranges==1);
 int holds=0;
 for(int f=s->phases-1;f>=0&&holds<3;--f){
  bool empty=true;for(int to=0;to<16;++to)for(int from=0;from<16;++from)
   if(to!=from&&e0470_phase_action(s,f,to,from))empty=false;
  if(!empty)break;
  ++holds;
 }
 assert(p->phases==s->phases+(int)white+(holds?0:1));
 const int drive=s->phases-holds;
 for(int f=0;f<p->phases;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  int want=from==to?0:f<drive?e0470_phase_action(s,f,to,from):
           f<drive+(int)white?(to==15&&from!=15?2:0):f<s->phases+(int)white?e0470_phase_action(s,f-(int)white,to,from):0;
  assert(e0470_phase_action(p,f,to,from)==want);
 }
 for(int to=0;to<16;++to)for(int from=0;from<16;++from)
  assert(e0470_phase_action(p,p->phases-1,to,from)==0);
 assert(!memcmp(saved,s->luts,(size_t)s->phases*64));
 unsigned before=allocations;size_t used=bytes;
 for(int i=0;i<100;++i){assert(display_main_clean_waveform_with_phases(source,mode,white)==wave);assert(display_main_clean_waveform_with_phases(wave,mode,white)==wave);}
 assert(allocations==before&&bytes==used);
#else
 assert(wave==source);
#endif
}
static void verify_scroll(void){
 const EpdWaveform *base=display_main_normal_waveform();
 const EpdWaveformPhases *source=e0470_waveform_phases(base,MODE_GL16);
 uint8_t saved[37*64];memcpy(saved,source->luts,sizeof(saved));
 oom=true;assert(display_settings_scroll_waveform()==base);oom=false;
 const EpdWaveform *wave=display_settings_scroll_waveform();
 const EpdWaveformPhases *p=e0470_waveform_phases(wave,MODE_GL16);
 assert(wave!=base&&p&&p->phases==45&&!p->phase_times);
 const int drive=source->phases-3;
 for(int f=0;f<45;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  int want=from==to?0:f<drive?e0470_phase_action(source,f,to,from):
           f<drive+8?(to==15?2:0):e0470_phase_action(source,f-8,to,from);
  assert(e0470_phase_action(p,f,to,from)==want);
  if(f>=42)assert(e0470_phase_action(p,f,to,from)==0);
 }
 assert(!memcmp(saved,source->luts,sizeof(saved)));
 unsigned before=allocations;size_t used=bytes;
 for(int i=0;i<100;++i)assert(display_settings_scroll_waveform()==wave);
 assert(allocations==before&&bytes==used);
 puts("PASS: dedicated45-phase settings list retains all native erasure,adds eight changed-white phases,holds every equal gray/three settle phases,bounded cache and OOM fallback");
}
int main(void){
 e0470_waveform_init();
 const EpdWaveform *sources[]={display_main_normal_waveform(),display_main_gray_waveform(),display_main_water_waveform(),display_main_shelf_waveform(MODE_GL16),display_main_bw_waveform(),display_main_shelf_exit_waveform()};
 const int modes[]={MODE_GL16,MODE_GL16,MODE_GL16,MODE_GL16,MODE_DU,MODE_DU};
 uint8_t vendor[3][48*64];int native_modes[]={MODE_DU,MODE_GL16,MODE_GC16};
 for(int i=0;i<3;++i){const EpdWaveformPhases *p=e0470_waveform_phases(&E0470_WAVEFORM,native_modes[i]);memcpy(vendor[i],p->luts,(size_t)p->phases*64);}
 assert(display_main_clean_waveform(NULL,MODE_GL16)==NULL);
 assert(display_main_clean_waveform(&E0470_WAVEFORM,MODE_GC16)==&E0470_WAVEFORM);
 assert(display_main_clean_waveform(&E0470_FOLLOW_WAVEFORM,MODE_DU)==&E0470_FOLLOW_WAVEFORM);
 EpdWaveformPhases phase=*e0470_waveform_phases(sources[0],MODE_GL16);const EpdWaveformPhases *range=&phase;
 EpdWaveformMode mode={MODE_GL16,1,&range};const EpdWaveformMode *mp=&mode;
 EpdWaveform bad={.num_modes=1,.num_temp_ranges=1,.mode_data=&mp,.temp_intervals=sources[0]->temp_intervals};
 int timing=100;phase.phase_times=&timing;assert(display_main_clean_waveform(&bad,MODE_GL16)==&bad);phase.phase_times=NULL;
 phase.phases=38;assert(display_main_clean_waveform(&bad,MODE_GL16)==&bad);phase.phases=37;
 bad.num_temp_ranges=2;assert(display_main_clean_waveform(&bad,MODE_GL16)==&bad);bad.num_temp_ranges=1;
 bad.temp_intervals=NULL;assert(display_main_clean_waveform(&bad,MODE_GL16)==&bad);
 unsigned before=allocations;oom=true;
 for(unsigned white=6;white<=8;white+=2)assert(display_main_clean_waveform_with_phases(sources[0],modes[0],white)==sources[0]&&allocations==before);
 oom=false;
 for(unsigned white=0;white<=10;++white)if(white!=6&&white!=8)
  assert(display_main_clean_waveform_with_phases(sources[0],modes[0],white)==sources[0]&&allocations==before);
 before=allocations;size_t start=bytes;
 for(unsigned white=6;white<=8;white+=2)for(unsigned i=0;i<6;++i)verify(sources[i],modes[i],white);
#if KIIKO_PICO_MAIN_CLEAN
 assert(allocations==before+12&&bytes-start<=12*45*64);
 for(unsigned i=0;i<6;++i){
  const EpdWaveform *six=display_main_clean_waveform_with_phases(sources[i],modes[i],6);
  const EpdWaveform *eight=display_main_clean_waveform_with_phases(sources[i],modes[i],8);
  assert(six!=eight && display_main_clean_waveform(sources[i],modes[i])==six);
  assert(display_main_clean_waveform_with_phases(six,modes[i],8)==eight);
  assert(display_main_clean_waveform_with_phases(eight,modes[i],6)==six);
 }
#else
 assert(allocations==before&&bytes==start);
#endif
 EpdWaveform overflow=*sources[0];assert(display_main_clean_waveform(&overflow,MODE_GL16)==&overflow);
 for(int i=0;i<3;++i){const EpdWaveformPhases *p=e0470_waveform_phases(&E0470_WAVEFORM,native_modes[i]);assert(!memcmp(vendor[i],p->luts,(size_t)p->phases*64));}
 verify_scroll();
 puts("PASS: Pico native six root LUTs,20/24/37 sources,exact original actions,six/eight changed-white/original settling,equal-gray hold,45-phase cap,OOM retry/idempotent bounded cache/vendor bytes intact");
}
'''

with tempfile.TemporaryDirectory(prefix='pico-main-clean-') as folder:
    d=Path(folder);(d/'lut.c').write_text(UNIT)
    flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
           '-fno-sanitize-recover=all','-Imain','-Imain/app','-Itools/shelf_waveform_host_stubs',
           '-Itools/waveform_host_stubs','-Icomponents/epdiy/include','-Icomponents/epdiy/src',
           '-Icomponents/e0470_epaper_waveform/include','-Icomponents/e0470_epaper_waveform/waveforms']
    files=[str(d/'lut.c'),'boards/pico/src/main/display_main_waveform.c',
           'boards/pico/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c',
           'components/e0470_epaper_waveform/e0470_waveform_trim.c']
    for enabled in (0,1):
        exe=d/f'lut-{enabled}'
        subprocess.run(flags+[f'-DKIIKO_PICO_MAIN_CLEAN={enabled}',*files,'-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],check=True)
    # Run the real Pico engine with reader41, ordinary-root43 and shelf-root45.
    for phases in (41,43,45):
        ticks=phases+7
        prefix=(ROOT/'tools/water_turn_host_test.c').read_text().split('int main(void) {')[0]
        prefix=re.sub(r'\b37\b',str(phases),prefix)
        main=r'''
int main(void){
 EpdiyHighlevelState hl={front,back,diff,dirty_lines,dirty_columns};
 test_bands=8;test_launch_step=1;check_direction=false;e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
 for(int rot=0;rot<4;++rot)for(int dir=0;dir<4;++dir){
  rotation=(enum EpdRotation)rot;direction=(e0470_turn_dir_t)dir;
  EpdRect area={0,0,rot&1?HEIGHT:WIDTH,(rot&1?WIDTH:HEIGHT)-128};
  memset(front,0x24,sizeof(front));memset(back,255,sizeof(back));memset(phase_hits,0,sizeof(phase_hits));
  scans=fail_at=0;now_us=0;scan_time_us=18000;
  assert(e0470_page_turn_with_waveform_compact(&hl,area,direction,&soft_wave,8)==EPD_DRAW_SUCCESS);
  assert(scans==TICKS&&now_us==TICKS*18000&&!staged_luts);check_crop_baseline(area);
  for(int phase=0;phase<PHASES;++phase)assert(phase_hits[phase]==8);
 }
 rotation=EPD_ROT_INVERTED_PORTRAIT;direction=E0470_TURN_RTL;
 for(int failure=1;failure<=TICKS;++failure){
  memset(back,255,sizeof(back));scans=0;fail_at=failure;
  assert(e0470_page_turn_with_waveform_compact(&hl,(EpdRect){0,0,HEIGHT,1088},direction,&soft_wave,8)==EPD_DRAW_OTHER_ERROR);
  assert(scans==failure&&!staged_luts);for(size_t i=0;i<sizeof(back);++i)assert(back[i]==255);
 }
 e0470_page_turn_release();water_test_alloc_fail=1;memset(back,255,sizeof(back));
 assert(e0470_page_turn_with_waveform_compact(&hl,(EpdRect){0,0,HEIGHT,1088},direction,&soft_wave,8)==EPD_DRAW_NO_PHASES_AVAILABLE);
 for(size_t i=0;i<sizeof(back);++i)assert(back[i]==255);
 water_test_alloc_fail=0;
 e0470_page_turn_release();puts("PASS: actual Pico PHASES-phase engine,eight bands/all phases,four rotations/directions,slow scans,cropped history and all TICKS failure ticks");
}
'''
        main=re.sub(r'\bPHASES\b',str(phases),main)
        main=re.sub(r'\bTICKS\b',str(ticks),main)
        (d/'band.c').write_text(prefix+main)
        exe=d/f'band-{phases}'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                        '-Itools/water_turn_stubs','-Icomponents/e0470_page_turn/include',str(d/'band.c'),
                        'boards/pico/src/components/e0470_page_turn/e0470_page_turn.c','-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],check=True)
    # Both the common implementation and the actual Pico override must select strength.
    for scope_path in ('main/app/app_loop.c','boards/pico/src/main/app/app_loop.c'):
        scope=r'''
    #include <assert.h>
    #include <stdbool.h>
    #include <stddef.h>
    #include <stdio.h>
    typedef struct {int ignored;}app_ctx_t;
    typedef enum{APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE}app_redraw_t;
    typedef struct app_desc_s{int index;bool visible,persistent_navigation,white_child_transition;bool(*navigation_visible)(app_ctx_t*);bool(*main_page_visible)(app_ctx_t*);}app_desc_t;
    static const app_desc_t *s_presented_root,*s_presented_main,*drawing;
    static unsigned quality,observed;
    static app_ctx_t ctx;
    static int app_index_of(const app_desc_t *app){return app->index;}
    static bool visible(app_ctx_t *ctx){(void)ctx;return drawing->visible;}
    enum{APP_MAIN_REFRESH_FAST=1};
    static __attribute__((unused)) int app_settings_main_refresh_mode(void){return 0;}
    static __attribute__((unused)) bool display_main_fast_background_set(bool active){(void)active;return false;}
    static unsigned display_main_quality_set(unsigned phases){unsigned prior=quality;quality=phases;return prior;}
    static void display_main_transition_disarm(void){}
    '''
        path=ROOT/scope_path
        for name in ('main_page_ready','navigation_page_ready','main_present_finished'):
            scope+=select_source(function(name,path),'pico')+'\n'
        scope+='static void app_present_impl(app_ctx_t*c,const app_desc_t*a,app_redraw_t r){observed=quality;main_present_finished(c,a,r);}\n'
        scope+=select_source(function('app_present',path),'pico')+'\n'
        scope+=r'''
    static void present(app_desc_t *app,app_redraw_t redraw,unsigned expected){drawing=app;observed=!expected;app_present(&ctx,app,redraw);assert(observed==expected&&!quality);}
    int main(void){
     app_desc_t pages[5];for(int i=0;i<5;++i)pages[i]=(app_desc_t){.index=i,.visible=true,.main_page_visible=visible};
     for(int a=0;a<4;++a)for(int b=0;b<4;++b){
      s_presented_root=s_presented_main=NULL;
      present(&pages[a],APP_REDRAW_PAGE,false);present(&pages[b],APP_REDRAW_PAGE,a==b?0u:a==1||b==1?8u:6u);
      present(&pages[b],APP_REDRAW_PAGE,false);present(&pages[a],APP_REDRAW_FULL,false);
      present(&pages[b],APP_REDRAW_AREA,false);pages[a].visible=false;
      present(&pages[a],APP_REDRAW_PAGE,false);present(&pages[b],APP_REDRAW_PAGE,false);pages[a].visible=true;
     }
     present(&pages[4],APP_REDRAW_PAGE,false);present(&pages[0],APP_REDRAW_PAGE,false);
     puts("PASS: actual presenter scopes Pico quality only to visible root changes,shelf source OR destination=8/other=6,excluding entry/same/full/area/children/reader and restores state");
    }
    '''
        (d/'scope.c').write_text(scope)
        exe=d/'scope'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(d/'scope.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

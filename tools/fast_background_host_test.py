#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run each board's approved fast-root LUT with the legacy experiment disabled."""
from pathlib import Path
import subprocess,tempfile,json
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
board=json.loads((R/'.kiiko-source-view.json').read_text())['board']
pico=board=='pico'
u=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
static bool oom;static size_t bytes;static unsigned allocations;
void *heap_caps_malloc(size_t n,int caps){assert(caps==3&&n<=62*64);if(oom)return NULL;bytes+=n;++allocations;return malloc(n);}
int main(void){
 e0470_waveform_init();
 const EpdWaveform* sources[]={display_main_bw_waveform(),display_main_gray_waveform(),display_main_shelf_exit_waveform()};
 const int modes[]={MODE_DU,MODE_GL16,MODE_DU};
 const EpdWaveformPhases *native=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);uint8_t vendor[48*64];memcpy(vendor,native->luts,(size_t)native->phases*64);
 oom=true;assert(display_main_fast_background_waveform_with_phases(sources[0],modes[0],6)==sources[0]);oom=false;
 for(unsigned white=6;white<=8;white+=2)for(unsigned i=0;i<3;++i){
  const EpdWaveform* source=sources[i];const EpdWaveformPhases* p=e0470_waveform_phases(source,modes[i]);assert(p&&p->phases<=48);
  uint8_t original[48*64];memcpy(original,p->luts,(size_t)p->phases*64);
  // Existing non-fast cleanup stays behind the original, disabled build gate.
  assert(display_main_clean_waveform_with_phases(source,modes[i],white)==source);
  const EpdWaveform* fast=display_main_fast_background_waveform_with_phases(source,modes[i],white);
  const EpdWaveformPhases* f=e0470_waveform_phases(fast,modes[i]);assert(fast!=source&&f&&!f->phase_times);
  int drive=p->phases;
#if TEST_PICO
  int holds=0;
  for(int n=p->phases-1;n>=0&&holds<3;--n){bool empty=true;for(int to=0;to<16;++to)for(int from=0;from<16;++from)if(to!=from&&e0470_phase_action(p,n,to,from))empty=false;if(!empty)break;++holds;}
  drive-=holds;assert(f->phases==p->phases+(int)white+(holds?0:1)&&f->phases<=45);
#else
  assert(f->phases==p->phases+(int)white+2&&f->phases<=62);
#endif
  for(int n=0;n<f->phases;++n)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
   int want=n<drive?e0470_phase_action(p,n,to,from):n<drive+(int)white?(to==15?2:0):0;
#if TEST_PICO
   if(n>=drive+(int)white&&n<drive+(int)white+holds)want=e0470_phase_action(p,n-(int)white,to,from);
#endif
   assert(e0470_phase_action(f,n,to,from)==want);
  }
  assert(!memcmp(original,p->luts,(size_t)p->phases*64));
  assert(display_main_fast_background_waveform_with_phases(fast,modes[i],white)==fast);
  unsigned before=allocations;for(int n=0;n<100;++n)assert(display_main_fast_background_waveform_with_phases(source,modes[i],white)==fast);assert(before==allocations);
 }
 assert(!memcmp(vendor,native->luts,(size_t)native->phases*64)&&bytes<40000);
 assert(display_main_fast_background_waveform_with_phases(&E0470_WAVEFORM,MODE_GC16,6)==&E0470_WAVEFORM);
 assert(display_main_fast_background_waveform_with_phases(&E0470_FOLLOW_WAVEFORM,MODE_DU,8)==&E0470_FOLLOW_WAVEFORM);
 assert(display_main_fast_background_waveform_with_phases(sources[0],modes[0],0)==sources[0]);
 puts("PASS: real board-native fast background6/8 drives with experiments OFF,stable-white coverage,native actions/release preserved,no stacking,bounded cache and OOM fallback");
}
"""
with tempfile.TemporaryDirectory(prefix='kiiko-fast-root-') as d:
 p=Path(d);(p/'lut.c').write_text(u)
 includes=['-Imain/app','-Imain','-Itools/shelf_waveform_host_stubs','-Itools/waveform_host_stubs','-Icomponents/epdiy/include','-Icomponents/epdiy/src','-Icomponents/e0470_epaper_waveform/include']
 files=['main/display_main_waveform.c','components/e0470_epaper_waveform/e0470_epaper_waveform.c','components/e0470_epaper_waveform/e0470_waveform_trim.c']
 includes+=['-Icomponents/e0470_epaper_waveform/'+('waveforms' if pico else 'metalio')]
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-fsanitize=address,undefined','-DKIIKO_BOARD_METALIO47='+str(int(not pico)),'-DTEST_PICO='+str(int(pico)),'-DKIIKO_PICO_MAIN_CLEAN=0','-DKIIKO_S31_MAIN_CLEAN=0','-DKIIKO_S31_DU14=0']+includes+[str(p/'lut.c')]+files+['-o',str(p/'lut')],cwd=R,check=True)
 subprocess.run([str(p/'lut')],check=True)
 # Execute each actual presenter wrapper: permission is restricted to changed root identities in Fast mode.
 scope=r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef struct {int unused;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE}app_redraw_t;
typedef struct app_desc_s {int index;bool visible,persistent_navigation,binary_ui,white_child_transition;bool(*navigation_visible)(app_ctx_t*);bool(*main_page_visible)(app_ctx_t*);bool(*grayscale_ui)(app_ctx_t*);}app_desc_t;
static const app_desc_t *s_presented_root,*s_presented_main,*drawing;
static unsigned quality;static bool fast_background,observed;
static int mode;enum{APP_MAIN_REFRESH_FAST=1};
static int app_settings_main_refresh_mode(void){return mode;}
static int app_index_of(const app_desc_t*a){return a->index;}
static bool visible(app_ctx_t*c){(void)c;return drawing->visible;}
static unsigned display_main_quality_set(unsigned n){unsigned prior=quality;quality=n;return prior;}
static bool display_main_fast_background_set(bool v){bool prior=fast_background;fast_background=v;return prior;}
static void display_main_transition_disarm(void){}
"""
 if not pico:scope+=r"""
static bool display_main_switch_erase_set(bool v){(void)v;return false;}
static bool display_main_shelf_home_erase_set(bool v){(void)v;return false;}
static bool display_system_binary_set(bool v){(void)v;return false;}
static bool display_system_navigation_set(bool v){(void)v;return false;}
"""
 source=R/'main/app/app_loop.c'
 for n in ['main_page_ready','navigation_page_ready','main_present_finished']+([] if pico else ['app_binary_ui']):scope+=function(n,source)+'\n'
 scope+='static void app_present_impl(app_ctx_t*c,const app_desc_t*a,app_redraw_t r){observed=fast_background;main_present_finished(c,a,r);}\n'
 scope+=function('app_present',source)+'\n'
 scope+=r"""
static void present(app_desc_t*a,app_redraw_t r,bool expected){app_ctx_t c={0};drawing=a;observed=!expected;app_present(&c,a,r);assert(observed==expected&&!fast_background&&!quality);}
int main(void){app_desc_t pages[5];for(int i=0;i<5;++i)pages[i]=(app_desc_t){.index=i,.visible=true,.main_page_visible=visible};
 for(mode=0;mode<3;++mode)for(int a=0;a<4;++a)for(int b=0;b<4;++b){
  s_presented_root=s_presented_main=NULL;present(&pages[a],APP_REDRAW_PAGE,false);present(&pages[b],APP_REDRAW_PAGE,mode==1&&a!=b);
  present(&pages[b],APP_REDRAW_PAGE,false);present(&pages[a],APP_REDRAW_FULL,false);present(&pages[b],APP_REDRAW_AREA,false);
  pages[a].visible=false;pages[a].white_child_transition=true;present(&pages[a],APP_REDRAW_PAGE,false);pages[a].visible=true;present(&pages[a],APP_REDRAW_PAGE,false);
  present(&pages[4],APP_REDRAW_PAGE,false);present(&pages[b],APP_REDRAW_PAGE,false);
 }
 puts("PASS: actual presenter grants fast background only for16root pairs inFast;initial,same,children,reader,full,area and other modes excluded;scopes restored");}
"""
 (p/'scope.c').write_text(scope)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'scope.c'),'-o',str(p/'scope')],check=True)
 subprocess.run([str(p/'scope')],check=True)

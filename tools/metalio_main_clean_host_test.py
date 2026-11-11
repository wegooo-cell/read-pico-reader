#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实根页派生表的完整迁移、释放、缓存与失败边界；光学效果需实屏。
English: Real root-quality LUT transitions, release, caching and failure bounds; optics need a panel.
"""
from pathlib import Path
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
 const EpdWaveformPhases *s=e0470_waveform_phases(source,mode);assert(s&&s->phases<=52);
 uint8_t saved[52*64];memcpy(saved,s->luts,(size_t)s->phases*64);
 const EpdWaveform *wave=display_main_clean_waveform_with_phases(source,mode,white);
#if KIIKO_S31_MAIN_CLEAN
 const EpdWaveformPhases *p=e0470_waveform_phases(wave,mode);
 assert(wave!=source&&p&&p->phases==s->phases+(int)white+2&&p->phases<=62&&!p->phase_times);
 assert(wave->temp_intervals==source->temp_intervals&&wave->num_temp_ranges==1);
 for(int f=0;f<p->phases;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  int want=f<s->phases?e0470_phase_action(s,f,to,from):f<s->phases+(int)white&&to==15&&from!=15?2:0;
  assert(e0470_phase_action(p,f,to,from)==want);
 }
 assert(!memcmp(saved,s->luts,(size_t)s->phases*64));
 unsigned before=allocations;size_t used=bytes;
 for(int i=0;i<100;++i){assert(display_main_clean_waveform_with_phases(source,mode,white)==wave);assert(display_main_clean_waveform_with_phases(wave,mode,white)==wave);}
 assert(allocations==before&&bytes==used);
#else
 assert(wave==source);
#endif
}
int main(void){
 e0470_waveform_init();
 const EpdWaveform *normal=display_main_switch_waveform(display_main_normal_waveform(),MODE_GL16);
 const EpdWaveform *lower=display_main_switch_waveform(normal,MODE_GL16);
 const EpdWaveform *art=display_main_switch_waveform(display_main_shelf37_waveform(true),MODE_GL16);
 const EpdWaveform *chrome=display_main_switch_waveform(display_main_shelf37_waveform(false),MODE_GL16);
 const EpdWaveform *water=display_main_switch_waveform(display_main_water_waveform(),MODE_GL16);
 const EpdWaveform *sources[]={display_du14_white_waveform(),normal,lower,art,chrome,water};
 int modes[]={MODE_DU,MODE_GL16,MODE_GL16,MODE_GL16,MODE_GL16,MODE_GL16};
 uint8_t vendor[3][48*64];int native_modes[]={MODE_DU,MODE_GL16,MODE_GC16};
 for(int i=0;i<3;++i){const EpdWaveformPhases *p=e0470_waveform_phases(&E0470_FULL_WAVEFORM,native_modes[i]);memcpy(vendor[i],p->luts,(size_t)p->phases*64);}
 assert(display_main_clean_waveform(NULL,MODE_GL16)==NULL);
 assert(display_main_clean_waveform(&E0470_FULL_WAVEFORM,MODE_GC16)==&E0470_FULL_WAVEFORM);
 assert(display_main_clean_waveform(&E0470_FOLLOW_WAVEFORM,MODE_DU)==&E0470_FOLLOW_WAVEFORM);
 unsigned before=allocations;oom=true;
 assert(display_main_clean_waveform(sources[0],modes[0])==sources[0]&&allocations==before);oom=false;
 for(unsigned white=0;white<=10;++white)if(white!=6&&white!=8)
  assert(display_main_clean_waveform_with_phases(sources[0],modes[0],white)==sources[0]&&allocations==before);
 for(unsigned white=6;white<=8;white+=2)for(unsigned i=0;i<sizeof(sources)/sizeof(sources[0]);++i)verify(sources[i],modes[i],white);
#if KIIKO_S31_MAIN_CLEAN
 for(unsigned i=0;i<6;++i){
  const EpdWaveform *six=display_main_clean_waveform_with_phases(sources[i],modes[i],6);
  const EpdWaveform *eight=display_main_clean_waveform_with_phases(sources[i],modes[i],8);
  assert(six!=eight&&display_main_clean_waveform(sources[i],modes[i])==six);
  assert(display_main_clean_waveform_with_phases(six,modes[i],8)==eight);
  assert(display_main_clean_waveform_with_phases(eight,modes[i],6)==six);
 }
#endif
 EpdWaveformPhases phase=*e0470_waveform_phases(normal,MODE_GL16);const EpdWaveformPhases *range=&phase;
 EpdWaveformMode mode={MODE_GL16,1,&range};const EpdWaveformMode *mp=&mode;
 EpdWaveform bad={.num_modes=1,.num_temp_ranges=1,.mode_data=&mp,.temp_intervals=normal->temp_intervals};
 int timing=100;phase.phase_times=&timing;
 assert(display_main_clean_waveform(&bad,MODE_GL16)==&bad);phase.phase_times=NULL;
 phase.phases=53;assert(display_main_clean_waveform(&bad,MODE_GL16)==&bad);phase.phases=50;
 bad.num_temp_ranges=2;assert(display_main_clean_waveform(&bad,MODE_GL16)==&bad);bad.num_temp_ranges=1;
 bad.temp_intervals=NULL;assert(display_main_clean_waveform(&bad,MODE_GL16)==&bad);
 static EpdWaveform unique[16];
 unsigned added=0;before=allocations;size_t start=bytes;
 for(int i=0;i<16;++i){unique[i]=*normal;added+=display_main_clean_waveform(&unique[i],MODE_GL16)!=&unique[i];}
#if KIIKO_S31_MAIN_CLEAN
 assert(added==12&&allocations==before+12&&bytes-start<=12*62*64);
#else
 assert(!added&&allocations==before&&bytes==start);
#endif
 for(int i=0;i<3;++i){const EpdWaveformPhases *p=e0470_waveform_phases(&E0470_FULL_WAVEFORM,native_modes[i]);assert(!memcmp(vendor[i],p->luts,(size_t)p->phases*64));}
 const EpdWaveform *list_base=display_main_normal_waveform();
 const EpdWaveformPhases *list_source=e0470_waveform_phases(list_base,MODE_GL16);
 oom=true;assert(display_settings_scroll_waveform()==list_base);oom=false;
 const EpdWaveform *list=display_settings_scroll_waveform();
 const EpdWaveformPhases *lp=e0470_waveform_phases(list,MODE_GL16);
 assert(list!=list_base&&lp&&lp->phases==58);
 for(int f=0;f<58;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  int want=f<48?e0470_phase_action(list_source,f,to,from):f<56&&to==15?2:0;
  assert(e0470_phase_action(lp,f,to,from)==want);
  if(to==from&&to!=15)assert(!want);
 }
 oom=true;assert(display_settings_scroll_lower_waveform()==list_base);oom=false;
 const EpdWaveform *list_lower=display_settings_scroll_lower_waveform();
 const EpdWaveformPhases *low=e0470_waveform_phases(list_lower,MODE_GL16);assert(low&&low->phases==60);
 for(int f=0;f<60;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  int want=f<48?e0470_phase_action(list_source,f,to,from):f<58&&to==15?2:0;
  assert(e0470_phase_action(low,f,to,from)==want);
 }
 before=allocations;start=bytes;
 for(int i=0;i<100;++i){assert(display_settings_scroll_waveform()==list);assert(display_settings_scroll_lower_waveform()==list_lower);}
 assert(allocations==before&&bytes==start);
 puts("PASS: settings list58/60: native48 gray/black retained,equal black/gray hold,8/10 white drives including equal whites,two releases,bounded caches and OOM retry");
 puts("PASS: actual DU14/GL37/GL48 roots and double lower-erase source; exact original actions, six/eight changed-white drives/two holds, no stacking/cross-strength cache, gray/black/equality intact, idempotent bounded cache, OOM retry, incompatible/GC/FOLLOW guards, vendor bytes retained");
}
'''

with tempfile.TemporaryDirectory(prefix='s31-main-clean-') as folder:
    d=Path(folder)
    (d/'test.c').write_text(UNIT)
    flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g',
           '-fsanitize=address,undefined','-fno-sanitize-recover=all','-DKIIKO_BOARD_METALIO47=1',
           '-DKIIKO_S31_DU14=1','-DKIIKO_S31_SHELF37=1','-Imain',
           '-Itools/shelf_waveform_host_stubs','-Itools/waveform_host_stubs',
           '-Icomponents/epdiy/include','-Icomponents/epdiy/src',
           '-Icomponents/e0470_epaper_waveform/include','-Icomponents/e0470_epaper_waveform/metalio']
    files=[str(d/'test.c'),'boards/metalio47/src/main/display_main_waveform.c',
           'boards/metalio47/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c',
           'components/e0470_epaper_waveform/e0470_waveform_trim.c']
    for enabled in (0,1):
        exe=d/f'test-{enabled}'
        subprocess.run(flags+[f'-DKIIKO_S31_MAIN_CLEAN={enabled}',*files,'-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],check=True)
    # 真实条带引擎验证增加的相位和每一个失败拍；不以LUT单测代替历史提交验证。
    # Exercise every added scan/failure in the actual band engine, beyond LUT-only checks.
    (d/'esp_log.h').write_text('#pragma once\n#include <stdio.h>\n#undef ESP_LOGI\n'
        '#define ESP_LOGI(tag,fmt,...) do { char b[256];(void)(tag);'
        '(void)snprintf(b,sizeof(b),fmt,__VA_ARGS__); } while(0)\n')
    (d/'esp_heap_caps.h').write_text((ROOT/'tools/water_turn_stubs/esp_heap_caps.h').read_text()+
        '\nstatic inline void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}\n')
    command=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unknown-attributes',
        '-g','-fsanitize=address,undefined','-DKIIKO_BOARD_METALIO47=1','-DKIIKO_S31_TEXT_RIPPLE=0',
        '-DKIIKO_TEST_MAIN_SWITCH_ERASE=1','-DKIIKO_TEST_ROOT_BACKGROUND=1','-DKIIKO_S31_MAIN_CLEAN=1',
        '-I'+str(d),'-Icomponents/epdiy/src','-Icomponents/epdiy/include',
        '-Icomponents/e0470_epaper_waveform/include','-Icomponents/e0470_epaper_waveform/metalio',
        '-Icomponents/e0470_page_turn/include','-Imain','-Iboards/metalio47/src/main',
        '-Itools/water_turn_stubs','-Itools/metalio_stubs',
        'tools/metalio_text_ripple_host_test.c','boards/metalio47/src/main/metalio_text_ripple.c',
        'boards/metalio47/src/main/display_main_waveform.c',
        'boards/metalio47/src/components/e0470_page_turn/e0470_page_turn.c',
        'boards/metalio47/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c',
        'components/e0470_epaper_waveform/e0470_waveform_trim.c','-o',str(d/'bands')]
    for white in (6,8):
        subprocess.run(command[:1]+[f'-DKIIKO_TEST_WHITE_PHASES={white}']+command[1:],cwd=ROOT,check=True)
        subprocess.run([str(d/'bands')],check=True)
    # Verify the real S31 app scope rather than a parallel rule implementation.
    scope=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef struct {int ignored;}app_ctx_t;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE}app_redraw_t;
typedef struct app_desc_s{int index;bool visible,persistent_navigation,binary_ui,white_child_transition;bool(*navigation_visible)(app_ctx_t*);bool(*main_page_visible)(app_ctx_t*);bool(*grayscale_ui)(app_ctx_t*);}app_desc_t;
static const app_desc_t *s_presented_root,*s_presented_main,*drawing;
static unsigned quality,observed;
enum{APP_MAIN_REFRESH_FAST=1};
static int app_settings_main_refresh_mode(void){return 0;}
static bool fast_background;
static bool display_main_fast_background_set(bool v){bool old=fast_background;fast_background=v;return old;}
static bool erase,home,binary,nav;
static app_ctx_t ctx;
static int app_index_of(const app_desc_t *app){return app->index;}
static bool visible(app_ctx_t *ctx){(void)ctx;return drawing->visible;}
static bool gray(app_ctx_t *ctx){(void)ctx;return true;}
static unsigned display_main_quality_set(unsigned p){unsigned prior=quality;quality=p;return prior;}
static bool display_main_switch_erase_set(bool v){bool p=erase;erase=v;return p;}
static bool display_main_shelf_home_erase_set(bool v){bool p=home;home=v;return p;}
static bool display_system_binary_set(bool v){bool p=binary;binary=v;return p;}
static bool display_system_navigation_set(bool v){bool p=nav;nav=v;return p;}
static void display_main_transition_disarm(void){}
'''
    path=ROOT/'main/app/app_loop.c'
    for name in ('main_page_ready','navigation_page_ready','main_present_finished','app_binary_ui'):
        scope+=select_source(function(name,path),'metalio47')+'\n'
    scope+=r'''
static void app_present_impl(app_ctx_t*c,const app_desc_t*a,app_redraw_t r){
 observed=quality;assert(erase==(quality!=0));
 assert(home==(quality&&s_presented_root->index==1&&a->index==0));
 assert(binary==(a->binary_ui&&!a->grayscale_ui));main_present_finished(c,a,r);
}
'''
    scope+=select_source(function('app_present',path),'metalio47')+'\n'
    scope+=r'''
static void present(app_desc_t*a,app_redraw_t r,unsigned expected){drawing=a;observed=99;app_present(&ctx,a,r);assert(observed==expected&&!quality&&!erase&&!home&&!binary&&!nav);}
int main(void){
 app_desc_t pages[5];for(int i=0;i<5;++i)pages[i]=(app_desc_t){.index=i,.visible=true,.binary_ui=i==2||i==3,.main_page_visible=visible};
 for(int gray_list=0;gray_list<2;++gray_list){
  pages[3].grayscale_ui=gray_list?gray:NULL;
  for(int a=0;a<4;++a)for(int b=0;b<4;++b){
   s_presented_root=s_presented_main=NULL;
   present(&pages[a],APP_REDRAW_PAGE,0);present(&pages[b],APP_REDRAW_PAGE,a==b?0:a==1||b==1?8:6);
   present(&pages[b],APP_REDRAW_PAGE,0);present(&pages[a],APP_REDRAW_FULL,0);
   present(&pages[b],APP_REDRAW_AREA,0);pages[a].visible=false;
   present(&pages[a],APP_REDRAW_PAGE,0);present(&pages[b],APP_REDRAW_PAGE,0);pages[a].visible=true;
  }
 }
 present(&pages[4],APP_REDRAW_PAGE,0);present(&pages[0],APP_REDRAW_PAGE,0);
 puts("PASS: actual S31 presenter:16 root pairs,source OR target shelf=8/other=6,no entry/same/full/area/children/reader,gray-list admission,all scopes restored");
}
'''
    (d/'scope.c').write_text(scope)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(d/'scope.c'),'-o',str(d/'scope')],check=True)
    subprocess.run([str(d/'scope')],check=True)

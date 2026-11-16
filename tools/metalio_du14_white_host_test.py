#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：核对14相白底维护、根页限定和真实高层一次提交/失败边界。
English: Verify DU14 white maintenance, root-only scope and actual highlevel single-pass/failure boundaries.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
#define W 1216
#define H 684
#define B (W*H/2)
#define MODE_PACKING_1PPB_DIFFERENCE 0x100
#define ESP_LOGI(...) ((void)0)
#define _swap_int(a,b) do{int saved=(a);(a)=(b);(b)=saved;}while(0)
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_EMPTY_LINE_QUEUE};
typedef struct{uint8_t *front_fb,*back_fb,*difference_fb;bool *dirty_lines;uint8_t *dirty_columns;const EpdWaveform *waveform;}EpdiyHighlevelState;
static uint8_t front[B],back[B],old[B],difference[W*H],columns[W/2];
static bool lines[H],s_main_switch_erase;
static int refresh,scans,frames,fail_phase,allocations;
enum{APP_MAIN_REFRESH_FAST=1};
static EpdRect physical;
static unsigned allocations_before;
static uint32_t now;
void *heap_caps_malloc(size_t n,int caps){assert(caps==3);++allocations;return malloc(n);}
int epd_width(void){return W;}int epd_height(void){return H;}
enum EpdRotation epd_get_rotation(void){return EPD_ROT_INVERTED_PORTRAIT;}
static int app_settings_main_refresh_mode(void){return refresh;}
static EpdRect epd_full_screen(void){return(EpdRect){0,0,W,H};}
static void epd_leading_skip_discard(void){}
static uint32_t esp_timer_get_time(void){return ++now;}
static void hl_record_timing(uint32_t a,uint32_t b,uint32_t c){(void)a;(void)b;(void)c;}
static void epd_difference_column_range(EpdRect a,int *x0,int*x1){*x0=a.x&~31;*x1=(a.x+a.width+31)&~31;if(*x1>W)*x1=W;}
static unsigned pixel(const uint8_t *fb,int x,int y){return (fb[(size_t)y*W/2+x/2]>>((x&1)*4))&15;}
static void set(uint8_t *fb,int x,int y,unsigned p){size_t n=(size_t)y*W/2+x/2;unsigned shift=(x&1)*4;fb[n]=(fb[n]&~(15u<<shift))|(p<<shift);}
// 模拟差分报告只有一个脏点，但仍为裁剪内每个像素生成真实to/from。
// Report one dirty point while generating real to/from for every cropped pixel.
static EpdRect epd_difference_image_cropped(const uint8_t*to,const uint8_t*from,EpdRect a,uint8_t*d,bool*r,uint8_t*c){
 assert(to==front&&from==back&&d==difference&&r==lines&&c==columns);
 assert(!memcmp(back,old,B));memset(r,0,H*sizeof(bool));memset(c,0,W/2);physical=a;
 int x0,x1;epd_difference_column_range(a,&x0,&x1);
 for(int y=a.y;y<a.y+a.height;++y)for(int x=x0;x<x1;++x){unsigned t=pixel(to,x,y),f=pixel(from,x,y);d[y*W+x]=(uint8_t)(t<<4|f);if(t!=f){r[y]=true;c[x/2]|=(t^f)<<((x&1)*4);}}
 return(EpdRect){40,2,1,1};
}
static enum EpdDrawError epd_draw_base(EpdRect a,const uint8_t*d,EpdRect crop,int mode,int t,const bool*r,const uint8_t*c,const EpdWaveform*w){
 assert(a.width==W&&crop.height==H&&d==difference&&r==lines&&c==columns&&t==25);
 assert(mode==(MODE_DU|MODE_PACKING_1PPB_DIFFERENCE)&&!memcmp(old,back,B));++scans;
 const EpdWaveformPhases *p=e0470_waveform_phases(w,MODE_DU);assert(p&&p->phases==14&&!p->phase_times);
 for(int y=0;y<H;++y)assert(r[y]);
 for(int x=0;x<W;++x)assert(!!c[x/2]==(x>=physical.x&&x<physical.x+physical.width));
 for(int f=0;f<14;++f){
  // 完全未变的行和列也要接到末三相白推；稳定黑字/灰阶不补黑。
  // Unchanged rows/columns must receive final white pushes; stable black/gray gets no reinforcement.
  assert(e0470_phase_action(p,f,15,15)==(f>=11?2:0));
  assert(!e0470_phase_action(p,f,0,0)&&!e0470_phase_action(p,f,5,5));
  assert(d[0]==0xff&&d[100*W+100]==0x00&&d[100*W+150]==0x55);
  ++frames;if(frames==fail_phase)return EPD_DRAW_EMPTY_LINE_QUEUE;
 }
 return EPD_DRAW_SUCCESS;
}
'''
unit+=r'''
static const EpdWaveformPhases *s_first_source;
static uint8_t s_first_phase[256];
extern const EpdWaveform* display_reader_fast_text_waveform(void);
'''
unit+=function('text_leading_holds',ROOT/'boards/metalio47/src/components/e0470_page_turn/e0470_page_turn.c')+'\n'
unit+=function('main_fast_white_waveform',ROOT/'boards/metalio47/src/main/display.c')+'\n'
for name in ['_inverse_rotated_area','hl_update_area']:
    unit+=function(name,ROOT/'components/epdiy/src/highlevel.c')+'\n'
unit+=r'''
int main(void){
 e0470_waveform_init();
 const EpdWaveform *raw=e0470_du14_waveform(),*white=display_du14_white_waveform();
#if KIIKO_S31_DU14
 assert(raw&&white&&raw!=white);const EpdWaveformPhases *a=e0470_waveform_phases(raw,MODE_DU),*b=e0470_waveform_phases(white,MODE_DU);
 assert(a->phases==14&&b->phases==14&&a->phase_times==b->phase_times&&white->temp_intervals==raw->temp_intervals);
 uint8_t saved[14*64];memcpy(saved,a->luts,sizeof(saved));
 for(int f=0;f<14;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  int want=to==from?(to==15&&f>=11?2:0):e0470_phase_action(a,f,to,from);
  assert(e0470_phase_action(b,f,to,from)==want);
 }
 assert(!memcmp(saved,a->luts,sizeof(saved)));
 const EpdWaveformPhases *reader=e0470_waveform_phases(display_reader_fast_text_waveform(),MODE_GL16);
 assert(reader&&reader->phases==18&&reader->luts!=b->luts);
 for(int f=0;f<18;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from)
  assert(e0470_phase_action(reader,f,to,from)==(f<14?e0470_phase_action(b,f,to,from):to==15?2:0));
 memset(front,0xff,B);memset(back,0xff,B);
 assert(text_leading_holds(reader,&(EpdiyHighlevelState){.front_fb=front,.back_fb=back},(EpdRect){0,0,1088,H})==10); // 原三白加额外四白都不可跳过，首页14相白尾独立。/ Retain original three whites plus four extras; root DU14 stays separate.
 set(back,40,2,0);
 assert(text_leading_holds(reader,&(EpdiyHighlevelState){.front_fb=front,.back_fb=back},(EpdRect){0,0,1088,H})==0);
#else
 assert(!raw&&!white);
#endif
 allocations_before=allocations;
 for(int scope=0;scope<2;++scope)for(int changed=0;changed<2;++changed)for(int switching=0;switching<2;++switching)for(int full=0;full<2;++full)for(int mode=1;mode<=5;++mode)for(refresh=0;refresh<4;++refresh){
  s_main_switch_erase=scope;
  bool expected=scope&&changed&&switching&&!full&&mode==MODE_DU&&refresh==APP_MAIN_REFRESH_FAST;
  assert(main_fast_white_waveform((enum EpdDrawMode)mode,changed,switching,full)==(expected?white:NULL));
 }
 assert((unsigned)allocations==allocations_before);
#if KIIKO_S31_DU14
 for(int extent=1088;extent<=1120;extent+=32)for(int fail=0;fail<=14;++fail){
  memset(back,0xff,B);set(back,100,100,0);set(back,150,100,5);memcpy(front,back,B);
  set(front,40,2,0);set(front,1150,500,0);memcpy(old,back,B);
  scans=frames=0;fail_phase=fail;
  EpdiyHighlevelState h={front,back,difference,lines,columns,white};
  assert(hl_update_area(&h,MODE_DU,25,(EpdRect){0,0,H,extent},true,false)==(fail?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS));
  assert(scans==1&&frames==(fail?fail:14));
  for(int y=0;y<H;++y)for(int x=0;x<W;++x)assert(pixel(back,x,y)==pixel(!fail&&x<extent?front:old,x,y));
 }
#endif
 puts("PASS:320 actual root-scope combinations and no warm allocation");
#if KIIKO_S31_DU14
 puts("PASS: DU14 changed transitions intact,WW tail3/nonwhite equal hold,reader18/real leading-hold scan retain four whites,root one batch/14 frames,unchanged-row white,footer bounds,all14 phase failures and real history commit");
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='s31-du14-white-') as folder:
    t=Path(folder);(t/'test.c').write_text(unit);(t/'epd_internals.h').write_text('#include "epd_waveform.h"\n')
    flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-DKIIKO_BOARD_METALIO47=1','-I'+str(t),'-Imain','-Itools/shelf_waveform_host_stubs','-Itools/waveform_host_stubs',
        '-Icomponents/epdiy/include','-Icomponents/epdiy/src','-Icomponents/e0470_epaper_waveform/include','-Icomponents/e0470_epaper_waveform/metalio']
    files=[str(t/'test.c'),'boards/metalio47/src/main/display_main_waveform.c','boards/metalio47/src/main/display_reader_waveform.c','boards/metalio47/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c','components/e0470_epaper_waveform/e0470_waveform_trim.c']
    for enabled in (0,1):
        exe=t/f'test-{enabled}';subprocess.run(flags+[f'-DKIIKO_S31_DU14={enabled}',*files,'-o',str(exe)],cwd=ROOT,check=True);subprocess.run([str(exe)],check=True)

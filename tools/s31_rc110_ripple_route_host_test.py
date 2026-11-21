#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Execute the actual local RC110 outlet with crop, timing and recovery failures."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct{int id;}EpdiyHighlevelState;
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int id;}EpdWaveform;
typedef int e0470_turn_dir_t;
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_NO_PHASES_AVAILABLE,EPD_DRAW_EMPTY_LINE_QUEUE,EPD_DRAW_OTHER_ERROR};
enum{DISPLAY_READER_GRAY_PAGE,DISPLAY_READER_GRAY_IMAGE,DISPLAY_READER_GRAY_BACKGROUND,READ_PICO_EPD_SCAN_FAST,MODE_GL16,MODE_GC16,DISPLAY_PCLK_SAFE_MHZ,METALIO_TEXT_RIPPLE_BANDS=8,E0470_TURN_FAST_TICK_US=11090};
static EpdWaveform E0470_WAVEFORM,E0470_FULL_WAVEFORM,wave;
static bool s_bulk_io,background,unavailable,prepare_ok=true,disabled;
static int turns,gray_calls,profile_seen,prepared,scheduled,prefill,power,rails,tick=8765;
static int s_pclk_mhz,clears,rebuilds,cleaned;
static EpdRect seen;
static enum EpdDrawError result,recovery;
static const char *app_settings_book_background_path(void){return background?"bg":"";}
static const EpdWaveform *metalio_text_ripple_rc110_waveform(void){return unavailable||disabled?NULL:&wave;}
static bool metalio_text_ripple_prepare(EpdiyHighlevelState*h,EpdRect a){assert(h);++prepared;seen=a;return prepare_ok;}
static enum EpdDrawError reader_gray_present_unlocked(EpdiyHighlevelState*h,EpdRect a,int p,bool full){assert(h);seen=a;++gray_calls;profile_seen=p;assert(full==(p==DISPLAY_READER_GRAY_PAGE));return recovery;}
static int e0470_page_turn_tick_us(void){return tick;}
static void e0470_page_turn_set_tick_us(int t){tick=t;}
static void read_pico_epd_use_scan(int s){assert(s==READ_PICO_EPD_SCAN_FAST);}
static void epd_lcd_set_prefill_lines(int n){prefill=n;}
static void epd_poweron(void){++power;}
static enum EpdDrawError e0470_reader_ripple_with_waveform(EpdiyHighlevelState*h,EpdRect a,int d,const EpdWaveform*w,unsigned b){assert(h&&w==&wave&&b==8&&tick==11090&&(d==0||d==1));seen=a;++turns;return result;}
static void use_scan_for(const EpdWaveform*w,int m){assert((w==&E0470_WAVEFORM&&m==MODE_GL16)||(w==&E0470_FULL_WAVEFORM&&m==MODE_GC16));}
static void rails_keepalive(void){++rails;}
static void reader_rails_schedule_check(void){++scheduled;}
static void metalio_text_ripple_disable(void){disabled=true;}
static void read_pico_epd_set_pclk(int p){assert(p==DISPLAY_PCLK_SAFE_MHZ);}
static void epd_hl_waveform(EpdiyHighlevelState*h,const EpdWaveform*w){assert(h&&(w==&E0470_FULL_WAVEFORM||w==&E0470_WAVEFORM));}
static void epd_clear(void){++clears;}
static enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState*h,int m,int t){assert(h&&m==MODE_GC16&&t==25);++rebuilds;return recovery;}
static void ui_page_cleaned(enum EpdDrawError r){assert(r==recovery);++cleaned;}
'''+function('reader_rc110_ripple_unlocked',R/'boards/metalio47/src/main/display.c')+r'''
int main(void){EpdiyHighlevelState h={0};EpdRect a={12,48,640,1060};
 for(int direction=0;direction<2;++direction)for(int bulk=0;bulk<2;++bulk){
  s_bulk_io=bulk;assert(reader_rc110_ripple_unlocked(&h,a,direction,false)==EPD_DRAW_SUCCESS);
  assert(seen.x==a.x&&seen.y==a.y&&seen.width==a.width&&seen.height==a.height);
  assert(prefill==(bulk?127:64)&&tick==8765&&!gray_calls);
 }
 assert(turns==4&&power==4&&rails==4&&scheduled==4&&prepared==4);
 int before=prepared;
 for(int image=0;image<2;++image)for(int bg=0;bg<2;++bg)if(image||bg){
  background=bg;assert(!reader_rc110_ripple_unlocked(&h,a,1,image));
  assert(prepared==before&&turns==4&&profile_seen==(image?DISPLAY_READER_GRAY_IMAGE:DISPLAY_READER_GRAY_BACKGROUND));
 }
 background=false;unavailable=true;assert(!reader_rc110_ripple_unlocked(&h,a,1,false));assert(profile_seen==DISPLAY_READER_GRAY_PAGE&&prepared==before&&turns==4);
 unavailable=false;prepare_ok=false;assert(!reader_rc110_ripple_unlocked(&h,a,1,false)&&turns==4);prepare_ok=true;
 for(int failure=EPD_DRAW_NO_PHASES_AVAILABLE;failure<=EPD_DRAW_OTHER_ERROR;++failure){
  disabled=false;result=failure;int old=turns,checks=scheduled,gray=gray_calls;
  recovery=EPD_DRAW_SUCCESS;assert(!reader_rc110_ripple_unlocked(&h,a,0,false));
  assert(disabled&&turns==old+1&&scheduled==checks&&gray_calls==gray+(failure==EPD_DRAW_NO_PHASES_AVAILABLE)&&tick==8765);
  if(failure==EPD_DRAW_NO_PHASES_AVAILABLE)assert(profile_seen==DISPLAY_READER_GRAY_PAGE);
  else assert(clears==failure-1&&rebuilds==clears&&cleaned==clears);
  assert(!reader_rc110_ripple_unlocked(&h,a,1,false)&&turns==old+1);
 }
 disabled=false;recovery=EPD_DRAW_OTHER_ERROR;
 assert(reader_rc110_ripple_unlocked(&h,a,1,false)==EPD_DRAW_OTHER_ERROR&&disabled&&tick==8765);
 puts("PASS: actual RC110 reader outlet retains crop/gray image/background,8 bands/11090us,restored clock,rails and checked recovery without history commits");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

# 同一生产分流器在测试开关启用时，仅阅读水波纹进入回退出口。
# With the comparison enabled, the same production dispatcher changes only reader ripple.
import ast
node=next(n for n in ast.parse((R/'tools/metalio_text_ripple_routes_host_test.py').read_text()).body
          if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='outlet' for t in n.targets))
source=eval(compile(ast.Expression(node.value),'<route-fixture>','eval'),{'root':R,'function':function})
source=source[:source.index('int main(void){')]
needle='static enum EpdDrawError update_display_reader_turn_unlocked('
helper=function('reader_rc110_ripple_unlocked',R/'boards/metalio47/src/main/display.c')
source=source.replace(needle,'static const EpdWaveform*metalio_text_ripple_rc110_waveform(void){return disabled?NULL:&ripple;}\n'+helper+'\n'+needle,1)
source+=r'''
int main(void){EpdiyHighlevelState h={.gray=9};EpdRect a={10,30,640,1080};
 effect=0;prepare=true;
 assert(!update_display_reader_turn_unlocked(&h,a,0,1,false,NULL));
 assert(!prepares&&h.gray==9&&last_wave==&gray_profiles[DISPLAY_READER_GRAY_TEXT]);
 effect=1;int old_tick=current_tick;
 assert(!update_display_reader_turn_unlocked(&h,a,0,8,false,NULL));
 assert(prepares==1&&h.gray==5&&last_wave==&ripple&&last_bands==8&&current_tick==old_tick);
 h.gray=9;int old=prepares;effect=2;
 assert(!update_display_reader_turn_unlocked(&h,a,0,1,false,NULL)&&prepares==old&&last_wave==&native_fast&&h.gray==9);
 effect=1;background=true;
 assert(!update_display_reader_turn_unlocked(&h,a,0,8,false,NULL)&&prepares==old&&last_wave==&gray_profiles[DISPLAY_READER_GRAY_BACKGROUND]&&h.gray==9);
 background=false;
 assert(!update_display_reader_turn_unlocked(&h,a,0,8,false,&image)&&prepares==old&&last_wave==&gray_profiles[DISPLAY_READER_GRAY_IMAGE]&&h.gray==9);
 s_system_binary=true;
 assert(!update_display_reader_turn_unlocked(&h,a,0,8,false,NULL)&&prepares==old&&last_wave==&normal);
 s_system_binary=false;
 assert(!update_display_reader_turn_unlocked(&h,a,0,8,true,NULL)&&prepares==old&&last_wave==&normal);
 effect=1;result=EPD_DRAW_EMPTY_LINE_QUEUE;
 assert(update_display_reader_turn_unlocked(&h,a,0,8,false,NULL)==EPD_DRAW_SUCCESS&&disabled&&current_tick==old_tick);
 assert(last_wave==&E0470_FULL_WAVEFORM&&clear==1&&recover==1);
 puts("PASS: actual enabled dispatcher changes only reader ripple; ordinary/FastBW/main/edge/images/backgrounds retain their correct routes and grays");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-DKIIKO_S31_RC110_RIPPLE=1',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

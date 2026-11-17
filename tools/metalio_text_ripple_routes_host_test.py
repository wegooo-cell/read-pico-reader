#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Execute actual turn-page eligibility/cleanup and the display outlet's experiment fallback."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root=Path(__file__).resolve().parents[1]
turn=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef struct{int64_t now_ms;unsigned leaf;}app_ctx_t;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_AREA}app_redraw_t;
enum{TOC,SHELF,MODE_GL16,E0470_TURN_LTR,E0470_TURN_RTL};
#define ESP_LOGI(...) ((void)0)
#define BOOK_TOC_ROWS 10
static const char*s_text="text";
static bool s_clear_confirm,s_reader_text_frame,s_reader_cleanup,s_save_failed,s_reader_turn_pending,s_water_turn_pending;
static bool s_reader_image_frame,s_reader_image_exit_pending;
static bool s_rc_auto_turning,s_part_more;
static size_t s_part_base,s_text_len;
static int64_t s_reader_notice_until;
static char s_reader_notice[64];
static void rc_pause_auto(void){}
static void ticket_count_turn(void){}
static void copy_text(char*d,size_t n,const char*s){snprintf(d,n,"%s",s);}
static bool chrome,background;
static void reader_zoom_reset(void){}
static size_t current_toc_position(void){return 0;}
static int book_toc_open(size_t position){(void)position;return 0;}
static int images,effect,full,s_turns,s_turn_direction,s_view,s_water_turn_dir,s_unsaved;
static unsigned s_page,s_chapter;
static int64_t s_last_turn_ms,s_stats_activity_ms;
static size_t s_jump_offset;
static unsigned book_layout_page_count(void){return 100000;}
static unsigned book_layout_page_image_count(unsigned p){(void)p;return images;}
static unsigned book_chapter_count(void){return 0;}
static void save_progress(void){}
static bool load_chapter(app_ctx_t*c,int a,int b,bool d){(void)c;(void)a;(void)b;(void)d;assert(false);return false;}
static bool reader_turn_chrome(app_ctx_t*c){(void)c;return chrome;}
static void set_reader_view(int view){s_view=view;}
static void skip_hidden_image_pages(app_ctx_t*c,int d){(void)c;(void)d;}
static uint8_t app_settings_reader_full_pages(void){return full;}
static int app_settings_reader_turn_effect(void){return effect;}
static const char*app_settings_book_background_path(void){return background?"wall":"";}
static app_redraw_t paint_reading(app_ctx_t*c,int mode){(void)c;assert(mode==MODE_GL16);return APP_REDRAW_AREA;}
'''+function('turn_page',root/'main/apps/app_book.c')+r'''
int main(void){
 app_ctx_t ctx={.now_ms=12345};const int periods[]={0,5,10,15,20,30};unsigned cases=0;
 for(int kind=0;kind<6;++kind)for(unsigned p=0;p<6;++p){
  full=periods[p];images=kind==1;background=kind==2;effect=kind==3?0:kind==5?2:1;
  bool from_text=kind!=4;chrome=false;s_page=1;s_turns=s_unsaved=0;
  int expected=full;
  for(int n=1;n<=65;++n){s_reader_text_frame=from_text;
   assert(turn_page(&ctx,1)==APP_REDRAW_AREA);
   assert(s_reader_cleanup==(expected&&n%expected==0));
   assert(s_water_turn_pending==(from_text&&!images&&!background&&effect==1&&!s_reader_cleanup));++cases;
  }
 }
 for(int old_image=0;old_image<2;++old_image)for(int changed_chrome=0;changed_chrome<2;++changed_chrome){
  full=0;images=0;background=false;effect=0;chrome=changed_chrome;
  s_reader_image_frame=old_image;s_reader_text_frame=!old_image;
  assert(turn_page(&ctx,1)==(changed_chrome?APP_REDRAW_PAGE:APP_REDRAW_AREA));
  assert(s_reader_image_exit_pending==(old_image&&!changed_chrome));
 }
 chrome=true;full=0;effect=1;images=0;background=false;s_reader_text_frame=true;s_turns=0;
 assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&!s_water_turn_pending&&!s_reader_turn_pending);
 printf("PASS: %u actual turns,0/5/10/15/20/30 exact cleanup without ten-turn caps,image/background/old-image/ordinary/chrome eligibility\n",cases);
}
'''
outlet=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int id;}EpdWaveform;
typedef struct{unsigned gray;}EpdiyHighlevelState;
typedef int e0470_turn_dir_t;
enum EpdDrawError{EPD_DRAW_SUCCESS=0,EPD_DRAW_NO_PHASES_AVAILABLE=1,EPD_DRAW_EMPTY_LINE_QUEUE=2};
enum{MODE_GL16,MODE_GC16,READ_PICO_EPD_SCAN_FAST,DISPLAY_PCLK_SAFE_MHZ};
enum{METALIO_TEXT_RIPPLE_BANDS=8};
static EpdWaveform E0470_WAVEFORM={1},E0470_FULL_WAVEFORM={2},normal={3},fast={4},ripple={6},soft={7},ordinary={8};
static bool s_bulk_io,prepare,disabled,s_system_binary;
static int effect,fast_prepares,prefill_at_scan,tps_prepares,tps_cancels;
static int s_prefill_lines=192;
static bool epd_pipeline_enabled(void){return true;}
static int metalio_tps_prepare(void){++tps_prepares;return 0;}
static void metalio_tps_cancel_prepare(void){++tps_cancels;}
static bool fast_available=true;
static EpdWaveform native_fast={5};
static int app_settings_reader_turn_effect(void){return effect;}
static bool display_reader_fast_text_prepare(EpdiyHighlevelState*h,EpdRect a){(void)h;(void)a;++fast_prepares;return fast_available;}
static const EpdWaveform*display_reader_fast_text_waveform(void){return &native_fast;}
static int compact_calls,reader_calls;
static int s_pclk_mhz,power,clear,regular,recover,last_bands,prepares;
static const EpdWaveform*last_wave;
static enum EpdDrawError result;
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static bool main_before_update(EpdiyHighlevelState*h,enum EpdDrawError*r){(void)h;(void)r;return false;}
static const EpdWaveform*display_reader_text_waveform(void){return &normal;}
static const EpdWaveform*display_reader_text_turn_waveform(void){return &normal;}
static int epd_rotated_display_width(void){return 684;}
static int epd_rotated_display_height(void){return 1216;}
static bool system_navigation_gray_changed(EpdiyHighlevelState*h){(void)h;return false;}
static const EpdWaveform*display_main_navigation_waveform(void){return &normal;}
static const EpdWaveform*display_main_bw_turn_waveform(void){return &normal;}
static bool s_main_switch_erase;
static const EpdWaveform*display_main_switch_quality_waveform(const EpdWaveform*w,int mode){(void)mode;return w;}
static const EpdWaveform*main_switch_quality_waveform(const EpdWaveform*w,int mode){return display_main_switch_quality_waveform(w,mode);}
static const EpdWaveform*display_main_water_waveform(void){return &normal;}
static const EpdWaveform*display_reader_soft_turn_waveform(void){return &soft;}
static const EpdWaveform*display_reader_ordinary_turn_waveform(void){return &ordinary;}
static const EpdWaveform*metalio_text_ripple_clean_waveform(void){return &ripple;}
static const EpdWaveform*main_switch_waveform(const EpdWaveform*w,int mode){assert(mode==MODE_GL16);return w;}
static void system_normalize(EpdiyHighlevelState*h){(void)h;assert(s_system_binary);}
static bool metalio_text_ripple_prepare(EpdiyHighlevelState*h,EpdRect a){
 (void)a;++prepares;if(!prepare||disabled)return false;h->gray=5;return true;
}
static const EpdWaveform*metalio_text_ripple_waveform(void){assert(!disabled);return &fast;}
static void metalio_text_ripple_disable(void){disabled=true;}
static void read_pico_epd_use_scan(int s){assert(s==READ_PICO_EPD_SCAN_FAST);}

static void epd_poweron(void){++power;}
static void rails_keepalive(void){}
static void reader_rails_schedule_check(void){}
static void use_scan_for(const EpdWaveform*w,int mode){(void)w;(void)mode;}
static enum EpdDrawError e0470_page_turn_with_waveform_compact(EpdiyHighlevelState*h,EpdRect a,int d,const EpdWaveform*w,unsigned b){(void)h;(void)a;(void)d;++compact_calls;prefill_at_scan=s_prefill_lines;last_wave=w;last_bands=b;return result;}
static enum EpdDrawError e0470_reader_ripple_with_waveform(EpdiyHighlevelState*h,EpdRect a,int d,const EpdWaveform*w,unsigned b){
 (void)h;(void)a;(void)d;++reader_calls;last_wave=w;last_bands=b;return result;
}
static enum EpdDrawError e0470_text_turn_with_waveform(EpdiyHighlevelState*h,EpdRect a,int d,const EpdWaveform*w){return e0470_page_turn_with_waveform_compact(h,a,d,w,1);}
static enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const EpdWaveform*w,int mode,EpdRect a){(void)h;(void)a;assert(w==&E0470_WAVEFORM&&mode==MODE_GL16);++regular;return EPD_DRAW_SUCCESS;}
static void read_pico_epd_set_pclk(int n){assert(n==DISPLAY_PCLK_SAFE_MHZ);}
static void epd_hl_waveform(EpdiyHighlevelState*h,const EpdWaveform*w){(void)h;(void)w;}
static void epd_clear(void){++clear;}
static enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState*h,int mode,int temp){(void)h;assert(mode==MODE_GC16&&temp==25);++recover;return EPD_DRAW_SUCCESS;}
static bool s_reader_phase_prefill_failed;
static int prefill_calls;
static enum EpdDrawError e0470_text_turn_with_prefill(EpdiyHighlevelState*h,EpdRect a,const EpdWaveform*w){
 ++prefill_calls;return e0470_text_turn_with_waveform(h,a,0,w);
}
static void ui_page_cleaned(enum EpdDrawError e){assert(e==EPD_DRAW_SUCCESS);}
static bool s_main_fault,full_requested;
static int online_depth,current_tick=7777,latency_depth,forced_full;
#define E0470_TURN_FAST_TICK_US 11090
#define E0470_TURN_RTL 0
#define LATENCY_DISPLAY_BEGIN() (++latency_depth)
#define LATENCY_DISPLAY_END() (--latency_depth)
static bool pico_online_display_begin(void){assert(!online_depth);++online_depth;return true;}
static void pico_online_display_end(bool held){assert(held&&online_depth==1);--online_depth;}
static bool display_needs_full(void){return full_requested;}
static int panel_full;
static enum EpdDrawError update_display_area_full_with_unlocked(EpdiyHighlevelState*h,const EpdWaveform*w,int m,EpdRect a){(void)h;(void)a;assert(w==&E0470_WAVEFORM&&m==MODE_GL16&&!s_system_binary);++panel_full;last_wave=w;return result;}
static enum EpdDrawError update_display_full_unlocked(EpdiyHighlevelState*h){(void)h;++forced_full;return result;}
'''+function('epd_lcd_set_prefill_lines',root/'boards/metalio47/src/components/epdiy/src/output_lcd/render_lcd.c')+function('update_display_reader_turn_unlocked',root/'boards/metalio47/src/main/display.c')+function('update_display_reader_panel',root/'boards/metalio47/src/main/display.c')+r'''
int main(void){
 EpdiyHighlevelState h={0};EpdRect a={0,0,684,1216};
 prepare=true;effect=1;
 assert(update_display_reader_turn_unlocked(&h,a,0,8,false)==EPD_DRAW_SUCCESS);
 assert(last_wave==&ripple&&last_bands==8&&reader_calls==1&&!compact_calls&&!disabled&&h.gray==5);
 effect=0;int ripple_prepares=prepares;
 for(unsigned gray=1;gray<15;++gray){
  h.gray=gray;assert(update_display_reader_turn_unlocked(&h,a,0,1,false)==EPD_DRAW_SUCCESS);
  assert(last_wave==&ordinary&&last_bands==1&&prefill_at_scan==192&&h.gray==gray&&prepares==ripple_prepares);
 }
 assert(tps_prepares==14&&tps_cancels==14&&prefill_calls==14);
 prepare=false;assert(update_display_reader_turn_unlocked(&h,a,0,1,false)==EPD_DRAW_SUCCESS);
 assert(last_wave==&ordinary&&prefill_at_scan==192&&!disabled);
 assert(prefill_calls==15);
 assert(update_display_reader_turn_unlocked(&h,(EpdRect){0,0,684,900},0,1,false)==EPD_DRAW_SUCCESS);
 assert(last_wave==&normal&&prefill_calls==15);
 prepare=true;effect=2;int before=prepares;
 assert(update_display_reader_turn_unlocked(&h,a,0,1,false)==EPD_DRAW_SUCCESS);
 assert(last_wave==&native_fast&&fast_prepares==1&&prepares==before&&prefill_at_scan==192);
 s_bulk_io=true;assert(update_display_reader_turn_unlocked(&h,a,0,1,false)==EPD_DRAW_SUCCESS);
 assert(prefill_at_scan==192);s_bulk_io=false;
 fast_available=false;assert(update_display_reader_turn_unlocked(&h,a,0,1,false)==EPD_DRAW_SUCCESS);
 assert(last_wave==&normal&&prefill_calls==15);fast_available=true;
 for(unsigned bands=1;bands<=8;bands+=7)for(int failure=0;failure<2;++failure){
  effect=bands==8?1:0;prepare=true;disabled=false;result=failure?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_NO_PHASES_AVAILABLE;
  int previous_regular=regular,previous_clear=clear,previous_prefills=prefill_calls;
  s_reader_phase_prefill_failed=false;
  assert(update_display_reader_turn_unlocked(&h,a,0,bands,false)==EPD_DRAW_SUCCESS&&disabled==(bands==8));
  if(failure)assert(clear==previous_clear+1&&recover==clear&&s_pclk_mhz==DISPLAY_PCLK_SAFE_MHZ);
  else assert(regular==previous_regular+1&&clear==previous_clear);
  result=EPD_DRAW_SUCCESS;assert(update_display_reader_turn_unlocked(&h,a,0,bands,false)==EPD_DRAW_SUCCESS);
  assert(prefill_calls==previous_prefills+(bands==1)&&s_reader_phase_prefill_failed==(bands==1));
  assert(last_wave==(bands==1?&ordinary:&soft)&&last_bands==(int)bands);
 }
 // Every graphical panel keeps complete grays in every reader effect, even when text quantization is unavailable.
 result=EPD_DRAW_SUCCESS;disabled=false;prepare=true;
 for(effect=0;effect<3;effect++){
  int previous_fast=fast_prepares,previous_prepare=prepares,previous_panel=panel_full;
  assert(update_display_reader_panel(&h,a)==EPD_DRAW_SUCCESS);
  assert(last_wave==&E0470_WAVEFORM&&panel_full==previous_panel+1&&fast_prepares==previous_fast&&prepares==previous_prepare);
 }
 prepare=false;assert(update_display_reader_panel(&h,a)==EPD_DRAW_SUCCESS&&last_wave==&E0470_WAVEFORM);
 s_system_binary=true;prepare=true;disabled=false;int previous=reader_calls,previous_prepares=prepares,previous_compact=compact_calls;
 assert(update_display_reader_turn_unlocked(&h,a,0,8,false)==EPD_DRAW_SUCCESS);
 assert(reader_calls==previous&&prepares==previous_prepares&&compact_calls==previous_compact+1);
 assert(tps_prepares==tps_cancels);
 s_system_binary=false;disabled=false;prepare=true;effect=2;full_requested=false;
 assert(update_display_reader_panel(&h,a)==EPD_DRAW_SUCCESS&&last_wave==&E0470_WAVEFORM);
 assert(current_tick==7777&&!online_depth&&!latency_depth&&!s_main_fault);
 full_requested=true;int previous_preparation=prepares;
 assert(update_display_reader_panel(&h,a)==EPD_DRAW_SUCCESS&&forced_full==1&&prepares==previous_preparation);
 result=EPD_DRAW_EMPTY_LINE_QUEUE;
 assert(update_display_reader_panel(&h,a)==EPD_DRAW_EMPTY_LINE_QUEUE&&s_main_fault&&forced_full==2);
 assert(current_tick==7777&&!online_depth&&!latency_depth);
 puts("PASS: ordinary preserves every glyph gray; ripple remains eight bands; panels, fast BW and recovery isolated");
}
'''
with tempfile.TemporaryDirectory(prefix='text-ripple-routes-') as folder:
 for name,code in [('eligibility',turn),('outlet',outlet)]:
  c=Path(folder)/(name+'.c');binary=c.with_suffix('');c.write_text(code)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                  '-DKIIKO_BOARD_METALIO47=1','-DKIIKO_S31_TEXT_RIPPLE=1',str(c),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)

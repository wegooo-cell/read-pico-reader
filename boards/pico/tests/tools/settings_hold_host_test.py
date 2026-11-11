#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际Pico设置缓存/通用触摸/刷新出口集成回归。/ English: Actual Pico cached-list, generic-touch and presenter integration."""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
book = root/'main/apps/app_device_settings.c'
source = book.read_text()
declaration = re.search(r'typedef struct \{\n    bool tracking, active,[\s\S]*?#define SETTINGS_HOLD_CACHE_MAX[^\n]+', source)[0]
# 连同真实入口的调用关系验证；设备事件循环仍使用未改动的通用识别器。
# Verify real entry-point wiring as well; the device loop retains its unchanged recognizer.
for callback in ('on_enter','on_key','settings_exit','on_before_lock','settings_media_lost','settings_media_ready'):
    assert 'settings_drag_reset();' in function(callback,book), callback
assert 'settings_drag_tick(ctx)' in function('on_tick',book)
assert 'settings_drag_warm(ctx,offset,limit)' in function('on_tick',book)
assert 'settings_drag_gesture(ctx,ev,offset,limit,&motion)' in function('on_gesture',book)

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "epdiy.h"
#include "ui_gesture.h"
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;
typedef int settings_page_t;
enum {SETTINGS_MAIN,SETTINGS_BLUETOOTH,SETTINGS_BLE_SCAN,SETTINGS_SHELF_STYLE,
      SETTINGS_ONLINE_FONTS,SETTINGS_PIN_ENTRY,SETTINGS_TEXT_EDIT,SETTINGS_UPGRADE,
      SETTINGS_SYSTEM_FONT,SETTINGS_WALLPAPER,SETTINGS_AVATAR};
enum EpdDrawMode {MODE_GC16,MODE_GL16,MODE_DU};
enum {APP_MAIN_REFRESH_NORMAL,APP_MAIN_REFRESH_FAST,APP_MAIN_REFRESH_WATER,E0470_TURN_RTL};
typedef int EpdWaveform;
static const int E0470_WAVEFORM=2,E0470_FOLLOW_WAVEFORM=3,gray_wave=4,normal_wave=5,bw_wave=6,scroll_wave=7,settle_wave=8,motion_wave=9;
static bool motion_available=true;
static int motion_pushes,fallback_pushes;
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_NAV_TOP 1096
#define UI_NAV_REFRESH_END 1088
#define UI_NAV_MARKER_SCAN_END 1120
#define SETTINGS_SCROLL_MAX 833
#define SHELF_STYLE_CHOICES 4
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static int s_page,s_main_scroll,s_ble_scroll,selected_mode;
static bool s_scroll_drag_consumed,s_scroll_present_pending,s_input_layout,s_input_settle;
static EpdRect s_input_area;
static bool oom,s_avatar_pending;
static int allocation_attempts;
static size_t free_memory=4u*1024u*1024u,allocated_bytes;
static void *allocation;
static int allocations,frees,paints,diffs,settles,discrete,taps,fulls;
static EpdRect pushed;
static enum EpdRotation rotation;
static const size_t fb_bytes=UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2u;
int epd_width(void){return(rotation&1)?UI_LOCK_HEIGHT:UI_LOCK_WIDTH;}
int epd_height(void){return(rotation&1)?UI_LOCK_WIDTH:UI_LOCK_HEIGHT;}
enum EpdRotation epd_get_rotation(void){return rotation;}
static size_t heap_caps_get_free_size(int caps){(void)caps;return free_memory;}
static void *heap_caps_calloc(size_t n,size_t size,int caps){
 (void)caps;++allocation_attempts;if(oom)return NULL;assert(!allocation);allocation=calloc(n,size);
 assert(allocation);allocated_bytes=n*size;++allocations;return allocation;
}
static void tracked_free(void *p){if(p){assert(p==allocation);allocation=NULL;allocated_bytes=0;++frees;}free(p);}
#define free tracked_free
static int physical(int x,int y,int *shift){
 int a=x,b=y,w=epd_width(),h=epd_height();
 if(rotation==EPD_ROT_PORTRAIT){a=w-y-1;b=x;}
 if(rotation==EPD_ROT_INVERTED_LANDSCAPE){a=w-x-1;b=h-y-1;}
 if(rotation==EPD_ROT_INVERTED_PORTRAIT){a=y;b=h-x-1;}
 *shift=(a&1)*4;return b*(w/2)+a/2;
}
static unsigned get(const uint8_t *fb,int x,int y){int shift;int p=physical(x,y,&shift);return(fb[p]>>shift)&15;}
static void put(uint8_t *fb,int x,int y,unsigned gray){int shift;int p=physical(x,y,&shift);fb[p]=(fb[p]&~(15u<<shift))|(gray<<shift);}
static int offset(void){return s_page==SETTINGS_MAIN?s_main_scroll:s_ble_scroll;}
static unsigned expansions;
static unsigned scene(int x,int y){
 if(y<(s_page==SETTINGS_MAIN?160:190))return 5;
 if(y>=UI_NAV_REFRESH_END)return 10;
 return(unsigned)(x/7+(y-160+offset())/9)%16;
}
static void render_content(app_ctx_t *ctx,uint8_t *fb){
 (void)ctx;++paints;for(int y=0;y<UI_LOCK_HEIGHT;++y)for(int x=0;x<UI_LOCK_WIDTH;++x)put(fb,x,y,scene(x,y));
}
static bool settings_gray_preview(void){return false;}
static void display_monochrome_content(uint8_t *fb){(void)fb;assert(false);}
static void settings_navigation(uint8_t *fb){(void)fb;}
static const EpdWaveform *display_main_gray_waveform(void){return &gray_wave;}
static const EpdWaveform *display_main_normal_waveform(void){return &normal_wave;}
static const EpdWaveform *display_main_bw_waveform(void){return &bw_wave;}
static const EpdWaveform *display_settings_scroll_waveform(void){return &scroll_wave;}
static const EpdWaveform *display_reader_soft_turn_waveform(void){return &settle_wave;}
static const EpdWaveform *display_settings_motion_waveform(void){return motion_available?&motion_wave:NULL;}
static void display_main_transition_cancel(void){}
static void commit(void *ptr,EpdRect area){
 test_hl_t *hl=ptr;pushed=area;
 assert(area.y+area.height<=UI_NAV_MARKER_SCAN_END);
 for(int y=0;y<UI_LOCK_HEIGHT;++y)if(y<area.y||y>=area.y+area.height)
  for(int x=0;x<UI_LOCK_WIDTH;++x)if(get(hl->front_fb,x,y)!=get(hl->back_fb,x,y)){
   fprintf(stderr,"chrome changed page=%d rotation=%d pixel=%d,%d area=%d,%d offset=%d\n",s_page,rotation,x,y,area.y,area.height,offset());assert(false);
  }
 memcpy(hl->back_fb,hl->front_fb,fb_bytes);
}
static int64_t test_clock_us;
static int test_output_ms;
static int64_t esp_timer_get_time(void){return test_clock_us;}
static int update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){
 test_clock_us+=(int64_t)test_output_ms*1000;
 assert((mode==MODE_DU&&(wave==&E0470_FOLLOW_WAVEFORM||wave==&motion_wave))||(mode==MODE_GL16&&(wave==&gray_wave||wave==&normal_wave||wave==&scroll_wave)));
 if(mode==MODE_DU){if(wave==&motion_wave)++motion_pushes;else ++fallback_pushes;}
 ++diffs;commit(hl,area);return 0;
}
static int update_display_area_full_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){
 assert(wave==&settle_wave&&mode==MODE_GL16);++settles;commit(hl,area);return 0;
}
static int update_display_settings_list(void *hl,unsigned page,EpdRect area){
 assert(page==(unsigned)s_page+1);++discrete;return update_display_area_diff_with(hl,&normal_wave,MODE_GL16,area);
}
static int update_display_full(void *hl){(void)hl;++fulls;return 0;}
static int update_display_main_turn(void *hl,EpdRect area,int direction){
 (void)direction;return update_display_area_diff_with(hl,&normal_wave,MODE_GL16,area);
}
static void guard_draw_result(void *hl,int result){(void)hl;assert(!result);}
static bool ui_online_fonts_present(int *online,app_ctx_t *ctx,app_redraw_t redraw){
 (void)online;(void)ctx;(void)redraw;assert(false);return false;
}
static int app_settings_main_refresh_mode(void){return selected_mode;}
static EpdRect upgrade_progress_area(void){return(EpdRect){52,468,580,64};}
''' + declaration + '\n'
for name in ('settings_content_end','settings_viewport_end','settings_plain_list','render',
             'settings_drag_reset','settings_drag_pixel','settings_drag_cache','settings_drag_warm','settings_drag_expand','settings_drag_blit',
             'settings_drag_sample','settings_drag_finish','settings_drag_gesture',
             'settings_drag_tick','settings_drag_present','settings_drag_quick','scroll_gesture','settings_present'):
    code=function(name,book)
    if name=='settings_drag_expand':code=code.replace('{','{++expansions;',1)
    unit += code+'\n'
unit += r'''
static app_redraw_t event(app_ctx_t *ctx,const ui_gesture_event_t *ev){
 int *position=s_page==SETTINGS_MAIN?&s_main_scroll:&s_ble_scroll;
 app_redraw_t motion;
 if(settings_drag_gesture(ctx,ev,position,SETTINGS_SCROLL_MAX,&motion))return motion;
 bool handled;app_redraw_t result=scroll_gesture(ev,position,SETTINGS_SCROLL_MAX,&handled);
 if(result==APP_REDRAW_AREA&&s_list_drag.tracking){s_list_drag.origin_offset=*position;s_list_drag.origin_y=ev->y;}
 if(!handled&&ev->type==UI_GESTURE_TAP)++taps;
 return result;
}
static ui_gesture_t recognizer;
static cst836u_touch_t touch;
static test_hl_t hl;
static app_ctx_t ctx;
static app_redraw_t feed(int x,int y,int fingers,bool press,int64_t now){
 touch=(cst836u_touch_t){.count=fingers,.touched=fingers!=0,.x=x,.y=y};
 ctx=(app_ctx_t){.fb=hl.front_fb,.hl=&hl,.touch=&touch,.pressed=press,.released=fingers==0,.now_ms=now};test_clock_us=now*1000;
 ui_gesture_event_t gesture;app_redraw_t result=APP_REDRAW_NONE;
 if(ui_gesture_feed(&recognizer,&ctx,&gesture)){
  result=event(&ctx,&gesture);
  if(result!=APP_REDRAW_NONE){assert(settings_present(&ctx,result));ctx.consumed=true;}
 }
 if(s_list_drag.tracking||s_list_drag.active){
  app_redraw_t tick=settings_drag_tick(&ctx);
  if(tick!=APP_REDRAW_NONE){assert(settings_present(&ctx,tick));result=tick;}
 }
 return result;
}
static void reset(int page,int position){
 test_output_ms=0;settings_drag_reset();ui_gesture_reset(&recognizer);s_page=page;s_main_scroll=s_ble_scroll=position;
 s_scroll_present_pending=s_scroll_drag_consumed=false;
 ctx=(app_ctx_t){.fb=hl.front_fb,.hl=&hl};render(&ctx,ctx.fb);memcpy(hl.back_fb,hl.front_fb,fb_bytes);
}
static void expect_gray(void){
 for(int y=0;y<UI_LOCK_HEIGHT;++y)for(int x=0;x<UI_LOCK_WIDTH;++x)assert(get(hl.back_fb,x,y)==scene(x,y));
}
static void expect_motion(const uint8_t *fb){
 static const uint8_t bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
 int top=s_page==SETTINGS_MAIN?160:190;
 for(int y=top;y<UI_NAV_REFRESH_END;++y)for(int x=0;x<UI_LOCK_WIDTH;++x){
  unsigned gray=scene(x,y),threshold=bayer[(((y-top+offset())&3)*4)+(x&3)];
  unsigned want=gray==0||(gray<15&&gray<=threshold)?0:15;
  assert(get(fb,x,y)==want);
 }
}
int main(void){
 hl.front_fb=calloc(1,fb_bytes);hl.back_fb=calloc(1,fb_bytes);assert(hl.front_fb&&hl.back_fb);
 for(unsigned v=0;v<256;++v){uint32_t pixels=settings_drag_expand(v);
  for(int i=0;i<8;++i)assert(((pixels>>(i*4))&15u)==((v&(0x80u>>i))?0u:15u));}
 for(rotation=0;rotation<4;++rotation)for(selected_mode=0;selected_mode<3;++selected_mode){
  reset(SETTINGS_MAIN,0);int before=diffs,old_settles=settles,old_alloc=allocations,old_taps=taps;
  feed(300,800,1,true,0);assert(feed(300,692,1,false,110)==APP_REDRAW_AREA);
  assert(s_main_scroll==108&&diffs==before+1);
  feed(300,692,0,false,160);assert(settles==old_settles&&allocations==old_alloc+1&&taps==old_taps&&allocation);
  feed(300,650,0,false,959);assert(settles==old_settles&&allocation);
  feed(300,650,0,false,960);assert(settles==old_settles+1&&!allocation);expect_gray();
  reset(SETTINGS_MAIN,0);before=diffs;old_settles=settles;
  feed(300,800,1,true,0);feed(300,789,1,false,5);
  assert(diffs==before&&!s_list_drag.active);
  feed(300,788,1,false,10);assert(s_list_drag.active&&s_main_scroll==12&&diffs==before+1);
  feed(300,750,1,false,20);assert(s_main_scroll==50&&diffs==before+1);
  assert(allocated_bytes<=SETTINGS_HOLD_CACHE_MAX&&allocated_bytes<200u*1024u);
  int captured=paints;feed(300,700,1,false,50);assert(diffs==before+1&&s_main_scroll==100);
  feed(300,650,1,false,80);assert(diffs==before+2&&s_main_scroll==150&&paints==captured);
  feed(300,700,1,false,500);assert(diffs==before+3&&s_main_scroll==100&&paints==captured);
  expect_motion(hl.back_fb);
  assert(pushed.y==160&&pushed.y+pushed.height==UI_NAV_REFRESH_END);
  // 缓存输出只有黑白，固定题头/导航保持原灰度；反向拖动不重绘文字。
  // Cached motion is binary, fixed chrome stays gray, and reversal never rasterizes text again.
  for(int y=160;y<UI_NAV_REFRESH_END;++y)for(int x=0;x<UI_LOCK_WIDTH;++x){unsigned v=get(hl.back_fb,x,y);assert(v==0||v==15);}
  feed(300,700,0,false,510);assert(s_list_drag.active&&!s_list_drag.following&&allocation&&settles==old_settles&&paints==captured);
  feed(300,700,0,false,1309);assert(settles==old_settles&&allocation);
  feed(300,700,0,false,1310);assert(!s_list_drag.active&&!allocation&&settles==old_settles+1&&paints==captured+1);
  expect_gray();before=diffs;for(int i=0;i<20;++i)feed(300,700,0,false,1400+i*1000);
  assert(diffs==before&&settles==old_settles+1&&taps==old_taps);
 }
 rotation=EPD_ROT_PORTRAIT;selected_mode=APP_MAIN_REFRESH_NORMAL;
 assert(motion_pushes>0&&fallback_pushes==0);
 // 源表不可用只回退原FOLLOW8；800ms空闲仍用已修复加强擦白。
 // Missing source falls back only motion to FOLLOW8; idle keeps strengthened white recovery.
 motion_available=false;reset(SETTINGS_MAIN,0);int prior_fallback=fallback_pushes,prior_settle=settles;
 feed(300,800,1,true,0);feed(300,750,1,false,400);assert(fallback_pushes==prior_fallback+1);
 feed(300,750,0,false,450);feed(300,750,0,false,1250);assert(settles==prior_settle+1&&!allocation);
 motion_available=true;

 // 忙时只收到按下和抬起的快速滑动，也要保留原步长并延后整理。
 // A quick swipe sampled only at press/release still retains its step and deferred cleanup.
 reset(SETTINGS_MAIN,0);int release_clean=settles;
 feed(300,800,1,true,0);feed(300,500,0,false,100);
 assert(s_main_scroll==SETTINGS_QUICK_STEP_PX&&s_list_drag.active&&allocation&&settles==release_clean);
 feed(300,500,0,false,899);assert(settles==release_clean);
 feed(300,500,0,false,900);assert(settles==release_clean+1&&!allocation);expect_gray();
 // 连续快滑复用一次缓存，最后松手后空闲800ms只恢复一次；新按下立即延期。
 // Repeated quick swipes reuse one capture; restore once after800ms idle and defer immediately on new contact.
 reset(SETTINGS_MAIN,0);int count=allocations,clean=settles;
 feed(300,800,1,true,0);feed(300,680,1,false,100);feed(300,680,0,false,160);
 int cached_paints=paints;
 for(int i=1;i<=2;++i){feed(300,800,1,true,i*200);feed(300,680,1,false,i*200+100);feed(300,680,0,false,i*200+160);}
 assert(s_main_scroll==SETTINGS_SCROLL_MAX&&allocations==count+1&&paints==cached_paints&&settles==clean);
 feed(300,680,0,false,1200);assert(settles==clean);
 feed(300,680,1,true,1300);feed(300,680,1,false,2100);assert(settles==clean&&allocation);
 feed(300,680,0,false,2200);feed(300,680,0,false,2999);assert(settles==clean&&allocation);
 feed(300,680,0,false,3000);assert(settles==clean+1&&!allocation);expect_gray();
 // 节流期间松手仍提交最后的黑白位置，不能让显示位置与点击坐标脱节。
 // Flush the last BW offset on release even inside frame pacing; never leave visual/hit coordinates apart.
 reset(SETTINGS_MAIN,0);clean=settles;
 feed(300,800,1,true,0);feed(300,750,1,false,350);cached_paints=paints;
 feed(300,700,1,false,390);assert(s_list_drag.shown_offset==50&&s_main_scroll==100);
 feed(300,700,0,false,400);assert(s_list_drag.shown_offset==100&&paints==cached_paints&&settles==clean);
 feed(300,700,0,false,1200);assert(settles==clean+1&&!allocation);expect_gray();
 // 首次有效位移已逐像素跟随，不先跳一大段；后续仍从按下位置累计。
 // The first displacement follows directly without a discrete jump; later samples retain the original anchor.
 reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(300,692,1,false,100);
 feed(300,650,1,false,360);assert(s_main_scroll==150&&s_list_drag.active);feed(300,650,0,false,400);
 // 静止长按发出LONG_PRESS后，原始有效快照仍能进入跟手；松手不得触发条目。
 // Raw valid snapshots allow dragging after LONG_PRESS; release must never activate a row.
 reset(SETTINGS_MAIN,0);int before=taps;feed(300,800,1,true,0);feed(300,800,1,false,900);
 feed(300,770,1,false,1000);assert(s_list_drag.active&&s_main_scroll==30);
 feed(300,770,0,false,1100);assert(taps==before&&allocation);feed(300,770,0,false,1900);assert(!allocation);
 // 上下界、多指取消、锁屏/离页清理、低内存降级。
 // Boundaries, multitouch cancellation, lock/exit cleanup and low-memory fallback.
 reset(SETTINGS_MAIN,800);feed(300,800,1,true,0);feed(300,600,1,false,400);assert(s_main_scroll==833);
 feed(300,1200,1,false,500);assert(s_main_scroll==400);feed(300,1600,1,false,600);assert(s_main_scroll==0);
 int old=settles;feed(300,1600,2,false,700);assert(settles==old&&allocation);
 feed(300,1600,0,false,800);feed(300,1600,0,false,1600);assert(settles==old+1&&!allocation);
 reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(300,750,1,false,400);old=settles;
 settings_drag_reset();assert(!allocation&&settles==old); // Lifecycle cancellation never pushes behind lock/navigation.
 for(int kind=0;kind<2;++kind){
  reset(SETTINGS_MAIN,0);oom=kind==0;free_memory=kind==1?1:4u*1024u*1024u;
  int count=allocations;old=settles;feed(300,800,1,true,0);feed(300,750,1,false,400);
  assert(!s_list_drag.active&&s_list_drag.disabled&&!allocation);
  feed(300,680,1,false,420);assert(s_main_scroll==204);feed(300,680,0,false,440);
  assert(allocations==count&&settles==old);oom=false;free_memory=4u*1024u*1024u;
 }
 for(int page=SETTINGS_BLUETOOTH;page<=SETTINGS_BLE_SCAN;++page){
  reset(page,100);feed(300,800,1,true,0);feed(300,750,1,false,400);
  assert(s_list_drag.active&&s_ble_scroll==150&&pushed.y==190);feed(300,750,0,false,500);
  expect_motion(hl.back_fb);
  feed(300,750,0,false,1300);expect_gray();
 }
 for(int y=150;y<=1100;y+=950){
  reset(SETTINGS_MAIN,0);old=allocations;feed(300,y,1,true,0);feed(300,y-120,1,false,400);
  feed(300,y-120,0,false,500);assert(allocations==old&&s_main_scroll==0);
 }
 for(int edge=0;edge<2;++edge){
  reset(SETTINGS_MAIN,edge?SETTINGS_SCROLL_MAX:0);old=allocations;int frames=diffs;
  feed(300,600,1,true,0);feed(300,edge?500:700,1,false,400);feed(300,edge?500:700,0,false,500);
  assert(allocations==old&&diffs==frames&&!allocation);
 }
 reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(450,770,1,false,400);assert(!s_list_drag.active);feed(450,770,0,false,500);
 reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(300,750,1,false,400);old=diffs;
 ctx.request_app=(void*)1;s_list_drag.frame_pending=true;assert(settings_drag_present(&ctx));assert(diffs==old&&!allocation);
 reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(300,750,1,false,400);old=fulls;
 assert(!settings_present(&ctx,APP_REDRAW_FULL)&&fulls==old&&!allocation);
 // 非整字节滚动偏移、缓存首尾及蓝牙两像素尾块均逐像素对照独立场景，防止批量搬运错位。
 // Independently verify bit-shift offsets, cache ends and the Bluetooth two-pixel tail against scene pixels.
 for(rotation=0;rotation<4;++rotation)for(int page=SETTINGS_MAIN;page<=SETTINGS_BLE_SCAN;++page){
  reset(page,0);feed(300,800,1,true,0);feed(300,750,1,false,400);int before_paints=paints;
  for(int position=0;position<=17;++position){*s_list_drag.offset=position;settings_drag_blit(&ctx);expect_motion(hl.front_fb);}
  *s_list_drag.offset=SETTINGS_SCROLL_MAX;settings_drag_blit(&ctx);expect_motion(hl.front_fb);
  assert(paints==before_paints);settings_drag_reset();
 }
 // 空闲预热不推屏，首个12px/10ms位移复用缓存，避免首次现场绘制。
 // Idle warming never outputs; the first12px/10ms move reuses the cache without rasterizing again.
 for(rotation=0;rotation<4;++rotation)for(int page=SETTINGS_MAIN;page<=SETTINGS_BLE_SCAN;++page){
  reset(page,100);int count=allocations,frames=diffs,clean=settles;
  int *position=page==SETTINGS_MAIN?&s_main_scroll:&s_ble_scroll;
  settings_drag_warm(&ctx,position,SETTINGS_SCROLL_MAX);
  assert(allocation&&!s_list_drag.active&&!s_list_drag.tracking&&allocations==count+1&&diffs==frames&&settles==clean);
  int captured=paints;settings_drag_warm(&ctx,position,SETTINGS_SCROLL_MAX);assert(paints==captured&&allocations==count+1);
  feed(300,800,1,true,0);feed(300,788,1,false,10);
  assert(s_list_drag.active&&offset()==112&&diffs==frames+1&&paints==captured&&allocations==count+1);
  feed(300,788,0,false,20);feed(300,788,0,false,820);
  assert(!allocation&&settles==clean+1);expect_gray();
 }
 rotation=EPD_ROT_PORTRAIT;
 // 点击、短抖及横滑不滚动；已热缓存也不能抢占条目点击。
 // Taps, small jitter and horizontal moves never scroll, including warmed views.
 reset(SETTINGS_MAIN,0);settings_drag_warm(&ctx,&s_main_scroll,SETTINGS_SCROLL_MAX);
 int taps_before=taps,frames_before=diffs;
 feed(300,800,1,true,0);feed(300,789,1,false,10);feed(300,789,0,false,20);
 assert(taps==taps_before+1&&diffs==frames_before&&!s_main_scroll);settings_drag_reset();
 // 松手的最后位移也提交；没有额外时间门槛或释放后的误点击。
 // Commit final lift coordinates without a time gate or post-drag item activation.
 reset(SETTINGS_MAIN,0);taps_before=taps;
 feed(300,800,1,true,0);feed(300,770,1,false,10);feed(300,740,0,false,20);
 assert(s_main_scroll==SETTINGS_QUICK_STEP_PX&&s_list_drag.shown_offset==SETTINGS_QUICK_STEP_PX&&taps==taps_before);
 settings_drag_reset();
 // 触摸/未提交像素/头像加载/切页/已消费输入均让预热让路；OOM空闲只尝试一次。
 // Warming yields to input,pending pixels,avatars,navigation and consumed ticks; idle OOM never loops allocations.
 for(int kind=0;kind<5;++kind){
  reset(SETTINGS_MAIN,0);int attempts=allocation_attempts;
  touch=(cst836u_touch_t){.touched=kind==0,.count=kind==0?1:0};ctx.touch=&touch;
  if(kind==1)ctx.fb[0]^=1;
  if(kind==2)s_avatar_pending=true;
  if(kind==3)ctx.request_app=(void*)1;
  if(kind==4)ctx.consumed=true;
  settings_drag_warm(&ctx,&s_main_scroll,SETTINGS_SCROLL_MAX);assert(!allocation&&allocation_attempts==attempts);
  s_avatar_pending=false;
 }
 reset(SETTINGS_MAIN,0);oom=true;int attempts=allocation_attempts;
 settings_drag_warm(&ctx,&s_main_scroll,SETTINGS_SCROLL_MAX);settings_drag_warm(&ctx,&s_main_scroll,SETTINGS_SCROLL_MAX);
 assert(!allocation&&s_list_drag.disabled&&allocation_attempts==attempts+1);oom=false;
 feed(300,800,1,true,0);feed(300,788,1,false,10);assert(s_list_drag.active&&allocation);
 settings_drag_reset();
 // 实际back来源的小幅平移与完整缓存展开逐字节对照；偶数边缘路径只处理新露出的16px。
 // Compare committed-back shifts byte-for-byte with full cache expansion; even edge updates expand only16 new pixels.
 for(rotation=0;rotation<4;++rotation)for(int page=SETTINGS_MAIN;page<=SETTINGS_BLE_SCAN;++page){
  reset(page,100);feed(300,800,1,true,0);feed(300,750,1,false,400);
  int rendered=s_list_drag.shown_offset,captured=paints,allocated=allocations;
  static uint8_t expected[684*1216/2];assert(fb_bytes==sizeof(expected));
  for(int delta=-18;delta<=18;++delta){
   *s_list_drag.offset=rendered+delta;expansions=0;settings_drag_blit(&ctx);unsigned partial=expansions;
   expect_motion(hl.front_fb);memcpy(expected,hl.front_fb,fb_bytes);
   s_list_drag.shown_offset=-1;expansions=0;settings_drag_blit(&ctx);unsigned complete=expansions;
   assert(!memcmp(expected,hl.front_fb,fb_bytes));s_list_drag.shown_offset=rendered;
   if(abs(delta)==16&&(rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_INVERTED_PORTRAIT))assert(partial*20<complete);
  }
  assert(paints==captured&&allocations==allocated);settings_drag_reset();
 }
 assert(allocations==frees);

 // 连续松手的快滑扣除真实推屏阻塞，采样节拍/首帧/跨接触不影响固定终点。
 // Repeated flicks exclude real output stalls; sampling cadence/first frames/contacts keep the fixed endpoint.
 for(int mode=0;mode<3;++mode)for(int page=SETTINGS_MAIN;page<=SETTINGS_BLE_SCAN;++page){
  selected_mode=mode;reset(page,0);int *pos=page==SETTINGS_MAIN?&s_main_scroll:&s_ble_scroll;
  int old_alloc=allocations,old_clean=settles;test_output_ms=120;
  feed(300,800,1,true,0);feed(300,740,1,false,20);feed(300,730,0,false,350);
  assert(*pos==SETTINGS_QUICK_STEP_PX&&s_list_drag.shown_offset==SETTINGS_QUICK_STEP_PX&&settles==old_clean);
  feed(300,800,1,true,400);feed(300,740,1,false,420);feed(300,730,0,false,750);
  assert(*pos==SETTINGS_SCROLL_MAX&&s_list_drag.shown_offset==SETTINGS_SCROLL_MAX&&allocations==old_alloc+1&&settles==old_clean);
  test_output_ms=0;feed(300,730,0,false,1550);assert(settles==old_clean+1&&!allocation);expect_gray();
 }
 reset(SETTINGS_MAIN,0);test_output_ms=900;
 feed(300,800,1,true,0);feed(300,740,1,false,20);feed(300,730,0,false,1000);
 assert(s_main_scroll==70&&s_list_drag.blocked_ms==300);settings_drag_reset();
 for(int distance=36;distance<=SETTINGS_QUICK_STEP_PX;distance+=100){
  reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(300,800-distance,0,false,100);
  assert(s_main_scroll==SETTINGS_QUICK_STEP_PX&&s_list_drag.shown_offset==SETTINGS_QUICK_STEP_PX);
 }
 puts("PASS: half-visible-view quick flicks; incremental committed-back strips byte-exact to full expansion (-18..18px/all rotations/3pages),16px shifts decode over95% fewer packed groups,no extra cache allocation; production cache/presenter and real generic gestures; Pico changed-only motion DU/FOLLOW8 fallback/full-pixel12-white-four-black idle routes,packed eight-pixel expansion and column cache match independent pixels at shifted/end offsets and child tails; release-only quick-step fallback,immediate12px admission,idle warm-cache reuse/touch and pending-pixel guards,60ms pacing,800ms idle-only restoration,no per-release cleanup,no rerasterization during motion,byte-exact AA restoration,all rotations/three modes,fixed chrome/reversal/bounds,stationary-long drag,multitouch,OOM fallback,lifecycle/manual-full priority,no release tap/inertia/duplicate idle frames,all cache allocations freed");
 return 0;
}
'''
header = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {bool touched;uint8_t count;uint16_t x,y;} cst836u_touch_t;
typedef struct {uint8_t *front_fb,*back_fb;} test_hl_t;
typedef struct {uint8_t *fb;test_hl_t *hl;void *request_app;int64_t now_ms;
 const cst836u_touch_t *touch;bool pressed,released,consumed,continuous_ready;} app_ctx_t;
'''
with tempfile.TemporaryDirectory(prefix='pico-settings-held-') as directory:
    path=Path(directory)
    (path/'app.h').write_text(header)
    (path/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function',
                    '-fsanitize=address,undefined','-I'+str(path),'-I'+str(root/'tools/ui_gesture_stubs'),
                    '-I'+str(root/'main/ui'),str(path/'test.c'),str(root/'main/ui/ui_gesture.c'),
                    '-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)

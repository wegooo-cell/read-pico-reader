#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际S31设置缓存/通用触摸/刷新出口集成回归。/ English: Actual S31 cached-list, generic-touch and presenter integration."""
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
static const int E0470_FULL_WAVEFORM=1,E0470_WAVEFORM=2,E0470_FOLLOW_WAVEFORM=3,gray_wave=4,normal_wave=5,bw_wave=6,scroll_wave=7,short_wave=8;
static bool short_motion=true;
static int short_pushes,fallback_pushes;
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
static bool binary=true,oom,s_avatar_pending;
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
static const EpdWaveform *e0470_settings_motion_waveform(void){return short_motion?&short_wave:NULL;}
static const EpdWaveform *display_settings_scroll_waveform(void){return &scroll_wave;}
static bool display_system_binary_set(bool b){bool old=binary;binary=b;return old;}
static void display_main_transition_cancel(void){}
static void commit(void *ptr,EpdRect area){
 test_hl_t *hl=ptr;assert(!binary);pushed=area;
 assert(area.y+area.height<=UI_NAV_MARKER_SCAN_END);
 for(int y=0;y<UI_LOCK_HEIGHT;++y)if(y<area.y||y>=area.y+area.height)
  for(int x=0;x<UI_LOCK_WIDTH;++x)if(get(hl->front_fb,x,y)!=get(hl->back_fb,x,y)){
   fprintf(stderr,"chrome changed page=%d rotation=%d pixel=%d,%d area=%d,%d offset=%d\n",s_page,rotation,x,y,area.y,area.height,offset());assert(false);
  }
 memcpy(hl->back_fb,hl->front_fb,fb_bytes);
}
static int update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){
 assert((mode==MODE_DU&&(wave==&bw_wave||wave==&short_wave))||(mode==MODE_GL16&&(wave==&gray_wave||wave==&normal_wave)));
 if(mode==MODE_DU){if(wave==&short_wave)++short_pushes;else ++fallback_pushes;}
 ++diffs;commit(hl,area);return 0;
}
static int update_display_area_full_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){
 assert(wave==&scroll_wave&&mode==MODE_GL16);++settles;commit(hl,area);return 0;
}
static int update_display_settings_list(void *hl,unsigned page,EpdRect area){
 assert(page==(unsigned)s_page+1);++discrete;return update_display_area_diff_with(hl,&normal_wave,MODE_GL16,area);
}
static int update_display_full(void *hl){(void)hl;assert(!binary);++fulls;return 0;}
static int update_display_main_turn(void *hl,EpdRect area,int direction){
 (void)direction;return update_display_area_diff_with(hl,&normal_wave,MODE_GL16,area);
}
static void guard_draw_result(void *hl,int result){(void)hl;assert(!result);}
static int app_settings_main_refresh_mode(void){return selected_mode;}
static EpdRect upgrade_progress_area(void){return(EpdRect){52,468,580,64};}
''' + declaration + '\n'
for name in ('settings_content_end','settings_viewport_end','settings_plain_list','render',
             'settings_drag_reset','settings_drag_pixel','settings_drag_cache','settings_drag_warm','settings_drag_expand','settings_drag_blit',
             'settings_drag_repeated','settings_drag_sample','settings_drag_finish','settings_drag_gesture',
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
 ctx=(app_ctx_t){.fb=hl.front_fb,.hl=&hl,.touch=&touch,.pressed=press,.released=fingers==0,.now_ms=now};
 ui_gesture_event_t gesture;app_redraw_t result=APP_REDRAW_NONE;
 if(ui_gesture_feed(&recognizer,&ctx,&gesture)){
  result=event(&ctx,&gesture);
  if(result!=APP_REDRAW_NONE){assert(settings_present(&ctx,result));ctx.consumed=true;}
 }
 if(s_list_drag.tracking||s_list_drag.settle_pending){
  app_redraw_t tick=settings_drag_tick(&ctx);
  if(tick!=APP_REDRAW_NONE){assert(settings_present(&ctx,tick));result=tick;}
 }
 return result;
}
static void reset(int page,int position){
 settings_drag_reset();ui_gesture_reset(&recognizer);s_page=page;s_main_scroll=s_ble_scroll=position;
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
  feed(300,800,1,true,0);
  assert(feed(300,692,1,false,110)==APP_REDRAW_NONE);
  assert(feed(300,650,1,false,145)==APP_REDRAW_NONE);
  assert(!s_main_scroll&&diffs==before&&settles==old_settles);
  feed(300,692,0,false,160);
  assert(settles==old_settles+1&&allocations==old_alloc&&taps==old_taps&&!allocation);
  assert(diffs==before&&s_main_scroll==SETTINGS_QUICK_STEP_PX);expect_gray();
  reset(SETTINGS_MAIN,0);before=diffs;old_settles=settles;
  feed(300,800,1,true,0);feed(300,795,1,false,305);
  assert(diffs==before&&!s_list_drag.active);
  feed(300,794,1,false,310);assert(s_list_drag.active&&s_main_scroll==6&&diffs==before+1);
  feed(300,750,1,false,320);assert(s_main_scroll==50&&diffs==before+1);
  assert(allocated_bytes<=SETTINGS_HOLD_CACHE_MAX&&allocated_bytes<200u*1024u);
  int captured=paints;feed(300,700,1,false,350);assert(diffs==before+1&&s_main_scroll==100);
  feed(300,650,1,false,380);assert(diffs==before+2&&s_main_scroll==150&&paints==captured);
  feed(300,700,1,false,500);assert(diffs==before+3&&s_main_scroll==100&&paints==captured);
  expect_motion(hl.back_fb);
  assert(pushed.y==160&&pushed.y+pushed.height==UI_NAV_REFRESH_END);
  // 缓存输出只有黑白，固定题头/导航保持原灰度；反向拖动不重绘文字。
  // Cached motion is binary, fixed chrome stays gray, and reversal never rasterizes text again.
  for(int y=160;y<UI_NAV_REFRESH_END;++y)for(int x=0;x<UI_LOCK_WIDTH;++x){unsigned v=get(hl.back_fb,x,y);assert(v==0||v==15);}
  feed(300,700,0,false,510);
  assert(!s_list_drag.active&&!s_list_drag.following&&allocation&&settles==old_settles+1&&paints==captured+1);
  expect_gray();before=diffs;for(int i=0;i<20;++i)feed(300,700,0,false,1400+i*1000);
  assert(diffs==before&&settles==old_settles+1&&taps==old_taps);
 }
 rotation=EPD_ROT_PORTRAIT;selected_mode=APP_MAIN_REFRESH_NORMAL;
 assert(short_pushes>0&&fallback_pushes==0);
 // Missing source falls back only motion to DU20; release still cleans in gray.
 short_motion=false;reset(SETTINGS_MAIN,0);int prior_fallback=fallback_pushes,prior_settle=settles;
 feed(300,800,1,true,0);feed(300,750,1,false,400);assert(fallback_pushes==prior_fallback+1);
 feed(300,750,0,false,450);assert(settles==prior_settle+1&&allocation);
 short_motion=true;

 // A release-only flick needs one gray output and no temporary black/white frame or allocation.
 reset(SETTINGS_MAIN,0);int release_clean=settles,release_diffs=diffs,release_allocs=allocations;
 feed(300,800,1,true,0);feed(300,500,0,false,100);
 assert(s_main_scroll==SETTINGS_QUICK_STEP_PX&&settles==release_clean+1);
 assert(diffs==release_diffs&&allocations==release_allocs&&!allocation);expect_gray();
 // A longer release-only swipe uses the discrete fallback and still cleans immediately.
 reset(SETTINGS_MAIN,0);release_clean=settles;release_diffs=diffs;
 feed(300,800,1,true,0);feed(300,800-SETTINGS_QUICK_STEP_PX-30,0,false,100);
 assert(s_main_scroll==450&&settles==release_clean+1&&diffs==release_diffs);expect_gray();
 // Repeated quick swipes advance exactly once each, with no motion or redraw at the boundary.
 reset(SETTINGS_MAIN,0);int count=allocations,clean=settles,quick_diffs=diffs;
 for(int i=0;i<=2;++i){
  int start=s_main_scroll;
  feed(300,800,1,true,i*200);feed(300,680,1,false,i*200+100);
  assert(s_main_scroll==start&&diffs==quick_diffs);
  feed(300,680,0,false,i*200+160);
  assert(settles==clean+(i<2?i+1:2));expect_gray();
 }
 assert(s_main_scroll==SETTINGS_SCROLL_MAX&&allocations==count&&diffs==quick_diffs);
 feed(300,680,0,false,1200);assert(settles==clean+2);
 // Release bypasses BW frame pacing and commits the final offset directly in gray.
 reset(SETTINGS_MAIN,0);clean=settles;
 feed(300,800,1,true,0);feed(300,750,1,false,350);int cached_paints=paints;int drag_diffs=diffs;
 feed(300,700,1,false,390);assert(s_list_drag.shown_offset==50&&s_main_scroll==100);
 feed(300,700,0,false,400);
 assert(s_main_scroll==100&&paints==cached_paints+1&&settles==clean+1&&diffs==drag_diffs);expect_gray();
 // 首次有效位移已逐像素跟随，不先跳一大段；后续仍从按下位置累计。
 // The first displacement follows directly without a discrete jump; later samples retain the original anchor.
 reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(300,692,1,false,100);
 feed(300,650,1,false,360);assert(s_main_scroll==150&&s_list_drag.active);feed(300,650,0,false,400);
 // 静止长按发出LONG_PRESS后，原始有效快照仍能进入跟手；松手不得触发条目。
 // Raw valid snapshots allow dragging after LONG_PRESS; release must never activate a row.
 reset(SETTINGS_MAIN,0);int before=taps;feed(300,800,1,true,0);feed(300,800,1,false,900);
 feed(300,770,1,false,1000);assert(s_list_drag.active&&s_main_scroll==30);
 clean=settles;feed(300,770,0,false,1100);assert(taps==before&&settles==clean+1);expect_gray();
 // 上下界、多指取消、锁屏/离页清理、低内存降级。
 // Boundaries, multitouch cancellation, lock/exit cleanup and low-memory fallback.
 reset(SETTINGS_MAIN,800);feed(300,800,1,true,0);feed(300,600,1,false,400);assert(s_main_scroll==833);
 feed(300,1200,1,false,500);assert(s_main_scroll==400);feed(300,1600,1,false,600);assert(s_main_scroll==0);
 int old=settles;feed(300,1600,2,false,700);assert(settles==old&&allocation);
 feed(300,1600,0,false,800);assert(settles==old+1&&allocation);expect_gray();
 reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(300,750,1,false,400);old=settles;
 settings_drag_reset();assert(!allocation&&settles==old); // Lifecycle cancellation never pushes behind lock/navigation.
 for(int kind=0;kind<2;++kind){
  reset(SETTINGS_MAIN,0);oom=kind==0;free_memory=kind==1?1:4u*1024u*1024u;
  int count=allocations;old=settles;feed(300,800,1,true,0);feed(300,750,1,false,400);
  assert(!s_list_drag.active&&s_list_drag.disabled&&!allocation);
  feed(300,680,1,false,420);assert(s_main_scroll==204);feed(300,680,0,false,440);
  assert(allocations==count&&settles==old+1);expect_gray();oom=false;free_memory=4u*1024u*1024u;
 }
 for(int page=SETTINGS_BLUETOOTH;page<=SETTINGS_BLE_SCAN;++page){
  reset(page,100);feed(300,800,1,true,0);feed(300,750,1,false,400);
  assert(s_list_drag.active&&s_ble_scroll==150&&pushed.y==190);
  expect_motion(hl.back_fb);
  feed(300,750,0,false,500);expect_gray();
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
 assert(settings_present(&ctx,APP_REDRAW_FULL)&&fulls==old+1&&!allocation);
 // 非整字节滚动偏移、缓存首尾及蓝牙两像素尾块均逐像素对照独立场景，防止批量搬运错位。
 // Independently verify bit-shift offsets, cache ends and the Bluetooth two-pixel tail against scene pixels.
 for(rotation=0;rotation<4;++rotation)for(int page=SETTINGS_MAIN;page<=SETTINGS_BLE_SCAN;++page){
  reset(page,0);feed(300,800,1,true,0);feed(300,750,1,false,400);int before_paints=paints;
  for(int position=0;position<=17;++position){*s_list_drag.offset=position;settings_drag_blit(&ctx);expect_motion(hl.front_fb);}
  *s_list_drag.offset=SETTINGS_SCROLL_MAX;settings_drag_blit(&ctx);expect_motion(hl.front_fb);
  assert(paints==before_paints);settings_drag_reset();
 }
 // 空闲预热不推屏，首个6px位移复用缓存，避免首次现场绘制。
 // Idle warming never outputs; the first 6px move reuses the cache without rasterizing again.
 for(rotation=0;rotation<4;++rotation)for(int page=SETTINGS_MAIN;page<=SETTINGS_BLE_SCAN;++page){
  reset(page,100);int count=allocations,frames=diffs,clean=settles;
  int *position=page==SETTINGS_MAIN?&s_main_scroll:&s_ble_scroll;
  settings_drag_warm(&ctx,position,SETTINGS_SCROLL_MAX);
  assert(allocation&&!s_list_drag.active&&!s_list_drag.tracking&&allocations==count+1&&diffs==frames&&settles==clean);
  int captured=paints;settings_drag_warm(&ctx,position,SETTINGS_SCROLL_MAX);assert(paints==captured&&allocations==count+1);
  feed(300,800,1,true,0);feed(300,794,1,false,310);
  assert(s_list_drag.active&&offset()==106&&diffs==frames+1&&paints==captured&&allocations==count+1);
  feed(300,794,0,false,320);
  assert(allocation&&settles==clean+1);expect_gray();
 }
 rotation=EPD_ROT_PORTRAIT;
 // 点击、短抖及横滑不滚动；已热缓存也不能抢占条目点击。
 // Taps, small jitter and horizontal moves never scroll, including warmed views.
 reset(SETTINGS_MAIN,0);settings_drag_warm(&ctx,&s_main_scroll,SETTINGS_SCROLL_MAX);
 int taps_before=taps,frames_before=diffs;
 feed(300,800,1,true,0);feed(301,795,1,false,10);feed(301,795,0,false,20);
 assert(taps==taps_before+1&&diffs==frames_before&&!s_main_scroll);settings_drag_reset();
 // 松手的最后位移也提交；没有额外时间门槛或释放后的误点击。
 // Commit final lift coordinates without a time gate or post-drag item activation.
 reset(SETTINGS_MAIN,0);taps_before=taps;
 feed(300,800,1,true,0);feed(300,770,1,false,10);feed(300,740,0,false,20);
 assert(s_main_scroll==SETTINGS_QUICK_STEP_PX&&taps==taps_before);expect_gray();
 settings_drag_reset();
 for(int kind=0;kind<5;++kind){
  reset(SETTINGS_MAIN,400);feed(300,800,1,true,0);feed(300,740,1,false,350);
  int prior=s_main_scroll;
  if(kind==0)feed(300,730,2,false,400);
  else if(kind==1){ctx.touch=NULL;ctx.released=true;ctx.now_ms=400;ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL,.x=300,.y=730};app_redraw_t redraw;assert(settings_drag_gesture(&ctx,&cancel,&s_main_scroll,SETTINGS_SCROLL_MAX,&redraw));}
  else if(kind==2)feed(450,730,0,false,400);
  else if(kind==3){ctx.consumed=true;ctx.released=true;ctx.now_ms=400;touch=(cst836u_touch_t){0};ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL,.x=300,.y=730};app_redraw_t redraw;assert(settings_drag_gesture(&ctx,&cancel,&s_main_scroll,SETTINGS_SCROLL_MAX,&redraw));}
  else feed(300,730,0,false,10);
  assert(s_main_scroll==(kind==2?470:prior));
 }
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
 feed(300,800,1,true,0);feed(300,794,1,false,310);assert(s_list_drag.active&&allocation);
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
 assert(allocations==frees&&binary);

 // A contact that lasts beyond the flick window starts live tracking at the current position.
 for(int mode=0;mode<3;++mode)for(int page=SETTINGS_MAIN;page<=SETTINGS_BLE_SCAN;++page){
  selected_mode=mode;reset(page,0);int first=diffs,clean=settles;
  feed(300,800,1,true,0);feed(300,740,1,false,150);feed(300,740,1,false,300);
  assert(!offset()&&diffs==first&&settles==clean);
  feed(300,740,1,false,301);assert(offset()==60&&diffs==first+1);
  feed(300,730,0,false,350);assert(offset()==70&&settles==clean+1);expect_gray();
  // A release just beyond the window may arrive before the first live update.
  reset(page,0);first=diffs;clean=settles;
  feed(300,800,1,true,0);feed(300,740,1,false,150);feed(300,730,0,false,301);
  assert(offset()==70&&diffs==first&&settles==clean+1);expect_gray();
 }
 for(int distance=36;distance<=SETTINGS_QUICK_STEP_PX;distance+=100){
  reset(SETTINGS_MAIN,0);feed(300,800,1,true,0);feed(300,800-distance,0,false,100);
  assert(s_main_scroll==SETTINGS_QUICK_STEP_PX);expect_gray();
 }
 // An isolated quick flick below the shared tap slop still steps on lift, in either direction.
 for(int page=SETTINGS_MAIN;page<=SETTINGS_BLE_SCAN;++page)for(int direction=-1;direction<=1;direction+=2){
  int start=direction>0?0:SETTINGS_SCROLL_MAX;
  for(int distance=SETTINGS_QUICK_MIN_PX;distance<=24;distance+=6){
   reset(page,start);taps_before=taps;
   feed(300,800,1,true,0);feed(301,800-direction*distance,1,false,30);
   feed(301,800-direction*distance,0,false,70);
   assert(offset()==start+direction*SETTINGS_QUICK_STEP_PX&&taps==taps_before);
   expect_gray();
  }
 }
 // Similar-sized tap jitter, a diagonal tap, and a slower tiny motion remain taps.
 const int tap_samples[][3]={{0,0,50},{1,5,70},{4,6,70},{0,8,200}};
 for(size_t i=0;i<sizeof(tap_samples)/sizeof(tap_samples[0]);++i){
  reset(SETTINGS_MAIN,0);taps_before=taps;
  feed(300,800,1,true,0);
  feed(300+tap_samples[i][0],800-tap_samples[i][1],0,false,tap_samples[i][2]);
  assert(!s_main_scroll&&taps==taps_before+1);
 }
 for(int fallback=0;fallback<2;++fallback){
  reset(SETTINGS_MAIN,SETTINGS_SCROLL_MAX);oom=fallback!=0;
  feed(300,500,1,true,0);feed(300,506,0,false,100);
  assert(s_main_scroll==SETTINGS_SCROLL_MAX-SETTINGS_QUICK_STEP_PX);
  feed(300,500,1,true,170);feed(300,506,1,false,210);feed(300,506,0,false,260);
  assert(s_main_scroll==0);
  if(fallback)assert(!allocation);
  oom=false;
 }
 reset(SETTINGS_MAIN,0);
 feed(300,800,1,true,0);feed(300,740,0,false,100);
 feed(300,800,1,true,170);feed(300,770,1,false,570);feed(300,770,0,false,630);
 assert(s_main_scroll==SETTINGS_QUICK_STEP_PX+30);
 settings_drag_reset();
 assert(allocations==frees&&binary);
 puts("PASS: half-viewport flicks without an intermediate frame, held drag; one final gray cleanup on lift, no last BW or delayed refresh; byte-exact gray pixels and static chrome; cache reuse, low-memory fallback, multitouch, navigation, rotation and pacing.");
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
with tempfile.TemporaryDirectory(prefix='s31-settings-held-') as directory:
    path=Path(directory)
    (path/'app.h').write_text(header)
    (path/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function',
                    '-fsanitize=address,undefined','-I'+str(path),'-I'+str(root/'tools/ui_gesture_stubs'),
                    '-I'+str(root/'main/ui'),str(path/'test.c'),str(root/'main/ui/ui_gesture.c'),
                    '-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)

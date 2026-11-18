#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual S31 panel overlays and body-cache fallback; no physical display or serial access."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1];book=root/'main/apps/app_book.c';source=book.read_text()
if 'static bool reader_panel_fast_present(' not in source:
 print('SKIP: S31 panel overlay is not used on this board');raise SystemExit(0)
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_READER_STANDARD_TOP 176
#define UI_READER_TOOLS_TOP 1096
#define UI_GRAY_WHITE 255
#define BYTES (684*1216/2)
#define ESP_LOGI(...) ((void)0)
#define TAG "test"
typedef struct{int x,y,width,height;}EpdRect;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL}app_redraw_t;
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
enum{READING,SHELF,MODE_GL16};
typedef enum{READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_FONT_SETTINGS,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_STATS,READER_PANEL_BACKGROUND,READER_PANEL_BACKGROUND_PREVIEW,READER_PANEL_LAYOUT_SETTINGS,READER_PANEL_FONT_PICKER,READER_PANEL_FONT_RECOVERY,READER_PANEL_TOUCH_SETTINGS,READER_PANEL_TOUCH_MORE,READER_PANEL_READING_DISPLAY,READER_PANEL_HEADING_SETTINGS}reader_panel_t;
enum EpdRotation{EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
typedef struct{uint8_t*front_fb,*back_fb;}EpdiyHighlevelState;
typedef EpdiyHighlevelState hl_t;
typedef struct{uint8_t*fb;hl_t*hl;bool request_app;}app_ctx_t;
static uint8_t front[BYTES],back[BYTES],original[BYTES],cached_body[BYTES];
static hl_t hl={front,back};static app_ctx_t ctx={front,&hl,false};
static int s_view=READING,s_mode,s_du_count,s_presented_view;
static const char*s_text="book";static size_t s_page=2,s_chapter=3;
static bool s_clear_confirm,s_reader_fullscreen=true,s_reader_text_frame,s_fonts_reload_pending;
static bool s_reader_cleanup,s_reader_turn_pending,s_water_turn_pending,s_reader_footer_pending;
static bool s_reader_image_exit_pending;
static char s_reader_notice[20];static reader_panel_t s_reader_panel;
static int images,rotation=3,draws,pushes,errors,prepared,ended,full_draws,outer_draws,stores,cancels,invalidates;
static uint32_t generation=9;static bool background,draw_lock,failed,cache_ready,cache_oom;
#define READER_STATS_TOP 586
static int font_top=580,settings_top=580;
static bool fast_panels;
typedef enum{DISPLAY_READER_GRAY_PAGE,DISPLAY_READER_GRAY_BACKGROUND}display_reader_gray_profile_t;
static int four_pushes,entry_pushes;static bool erased_underlay;
static display_reader_gray_profile_t last_profile;static bool last_full;
static bool display_reader_fast_panels_enabled(void){return fast_panels;}
static int s_reader_slider=-1,s_px=40,s_margin=10,s_reader_preview_px=40,s_reader_preview_margin=10,s_reader_preview_line=120,s_reader_preview_para=110,s_reader_preview_tracking=2;
static bool s_reader_slider_endpoint;
static unsigned committed;
enum{UI_GESTURE_PRESS,UI_GESTURE_MOVE,UI_GESTURE_TAP,UI_GESTURE_CANCEL,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D,UI_GESTURE_LONG_PRESS};
typedef struct{int type,x0,y0,x,y;}ui_gesture_event_t;
static int app_settings_book_line_spacing(void){return 120;}
static int app_settings_book_paragraph_spacing(void){return 110;}
static int app_settings_book_tracking(void){return 2;}
static EpdRect reader_slider_refresh_rect(int slider){(void)slider;return(EpdRect){20,692,644,92};}
static int reader_slider_at(int x,int y){(void)x;return y==700?0:-1;}
static void reader_slider_begin(int slider,int x){s_reader_slider=slider;s_reader_slider_endpoint=false;s_reader_preview_px=x;}
static void reader_slider_move(int x){s_reader_preview_px=x;}
static app_redraw_t apply_reader_typography(app_ctx_t*c,int t){(void)c;(void)t;++committed;return APP_REDRAW_PAGE;}
static app_redraw_t apply_reader_layout(app_ctx_t*c,int p,int m,int l,uint8_t pa){(void)c;(void)p;(void)m;(void)l;(void)pa;++committed;return APP_REDRAW_PAGE;}
static void render(app_ctx_t*c,uint8_t*f){(void)c;(void)f;++full_draws;}

static EpdRect area,s_area;
static size_t fb_bytes(void){return BYTES;}
static unsigned book_layout_page_image_count(size_t page){assert(page==s_page);return images;}
static uint32_t book_layout_generation(void){return generation;}
static int epd_get_rotation(void){return rotation;}
static int epd_width(void){return UI_LOCK_HEIGHT;}
static int epd_height(void){return UI_LOCK_WIDTH;}
static bool reader_tools_visible(void){return !s_reader_fullscreen&&(s_reader_panel==READER_PANEL_NONE||s_reader_panel==READER_PANEL_TOOLS);}
static const char*app_settings_book_background_path(void){return background?"wallpaper":"";}
static int reader_font_top(void){return font_top;}
static int reading_settings_top(void){return settings_top;}
static int reader_font_picker_top(void){return 270;}
static bool display_page_prepare_begin(void){++prepared;return false;}
static void display_page_prepare_end(bool held){assert(!held);++ended;}
static int64_t esp_timer_get_time(void){return 123456;}
static void lock_draw(void){assert(!draw_lock);draw_lock=true;}
static void unlock_draw(void){assert(draw_lock);draw_lock=false;}
static void draw_reader_panel(uint8_t*fb){
 assert(draw_lock);++draws;int top=s_reader_panel==READER_PANEL_TOOLS?1096:s_reader_panel==READER_PANEL_FONT_SETTINGS?font_top:s_reader_panel==READER_PANEL_REFRESH_SETTINGS?settings_top:READER_STATS_TOP;
 memset(fb+top*684/2,0,(1216-top)*684/2);
}
static enum EpdDrawError update_display_reader_panel_fast(hl_t*h,EpdRect a){
 assert(h==&hl&&!draw_lock);area=a;++pushes;
 if(!failed)memcpy(back,front,BYTES);return failed?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;
}
static enum EpdDrawError update_display_reader_gray(hl_t*h,EpdRect a,display_reader_gray_profile_t p,bool full){last_profile=p;last_full=full;return update_display_reader_panel_fast(h,a);}
static void guard_draw_result(hl_t*h,enum EpdDrawError err){assert(h==&hl);errors+=err!=EPD_DRAW_SUCCESS;}
static enum EpdDrawError update_display_reader_panel_gray4(hl_t*h,EpdRect a,bool erase){erased_underlay=erase;entry_pushes+=erase;++four_pushes;return update_display_reader_panel_fast(h,a);}
static size_t cached_page;static uint32_t cached_generation;
static const uint8_t*reader_page_cache_find(size_t page,uint32_t gen,size_t bytes){assert(bytes==BYTES);return cache_ready&&page==cached_page&&gen==cached_generation?cached_body:NULL;}
static bool reader_page_cache_store(size_t page,uint32_t gen,const uint8_t*fb,size_t bytes,size_t focus){
 assert(bytes==BYTES&&page==focus);++stores;if(cache_oom)return false;
 memcpy(cached_body,fb,bytes);cached_page=page;cached_generation=gen;cache_ready=true;return true;
}
static void draw_reader(uint8_t*fb,size_t page){assert(page==s_page);++full_draws;memset(fb,0x76,BYTES);}
static EpdRect body_rect(void){return(EpdRect){12,80,660,1110};}
static void ui_fill_rect_fast(EpdRect r,uint8_t v,uint8_t*fb){assert(r.x==0&&r.width==684);memset(fb+r.y*684/2,v,r.height*684/2);}
static void draw_reader_outer(uint8_t*fb,size_t page){assert(page==s_page);++outer_draws;fb[BYTES-1]=0x43;}
static int s_next_page,s_prep_page;static uint32_t s_next_generation;static uint8_t*s_next_fb;
static struct{int value;}s_prep_cursor;
static void reader_page_cache_cancel(void){++cancels;}
static void invalidate_prep(void){++invalidates;}
'''
a=source.rindex('static struct {',0,source.index('static void reader_panel_record('));b=source.index('} s_reader_panel_frame;',a)+len('} s_reader_panel_frame;')
unit+=source[a:b]+'\n'
for name in ['reader_fast_panel_rect','reader_panel_record','reader_panel_short_ready','reader_toolbar_panel','reader_toolbar_transition','reader_sheet_profile','reader_closing_panel_to_text','reader_panel_fast_present','reader_slider_overlay_ready','reader_slider_feedback_present','reader_slider_value','reader_slider_feedback','reader_slider_commit','draw_reader_cached','pause_reader_prep']:unit+=function(name,book)+'\n'
start=source.index('    if (s_view == READING &&\n        (s_reader_panel == READER_PANEL_FONT_SETTINGS',source.index('static app_redraw_t gesture_event('))
at=source.index('{',start)+1;depth=1
while depth:
 depth+=(source[at]=='{')-(source[at]=='}');at+=1
unit+='static app_redraw_t slider_gesture(app_ctx_t*ctx,const ui_gesture_event_t*ev){'+source[start:at]+'return APP_REDRAW_NONE;}\n'
unit+=r'''
static void reset(reader_panel_t prior){
 fast_panels=false;four_pushes=entry_pushes=0;erased_underlay=false;
 s_view=s_presented_view=READING;s_text="book";s_page=2;s_chapter=3;generation=9;rotation=3;s_reader_fullscreen=true;
 images=0;background=false;s_clear_confirm=false;s_reader_notice[0]=0;s_reader_panel=prior;
 s_reader_cleanup=s_reader_turn_pending=s_water_turn_pending=s_reader_footer_pending=false;
 s_reader_image_exit_pending=false;
 draws=pushes=errors=prepared=ended=0;failed=false;draw_lock=false;s_fonts_reload_pending=false;
 for(unsigned i=0;i<BYTES;i++)front[i]=(uint8_t)(i*13+7);
 memcpy(back,front,BYTES);memcpy(original,front,BYTES);ctx=(app_ctx_t){front,&hl,false};hl.back_fb=back;
 reader_panel_record(EPD_DRAW_SUCCESS);assert(s_reader_panel_frame.valid);
}
int main(void){
 (void)invalidate_prep;
 // 收起只对无插图正文归一；重排允许最终整屏差分，不复用过期底页叠加。
 // Normalize dismissal only for text; reflow admits final differences without stale-body overlays.
 for(int rotated=0;rotated<2;++rotated){
  reset(READER_PANEL_REFRESH_SETTINGS);rotation=rotated?1:3;reader_panel_record(EPD_DRAW_SUCCESS);
  s_reader_panel=READER_PANEL_NONE;assert(reader_closing_panel_to_text());
  images=1;assert(!reader_closing_panel_to_text());images=0;
  s_reader_panel=READER_PANEL_READING_DISPLAY;assert(reader_toolbar_transition(&ctx,APP_REDRAW_PAGE));
  s_reader_panel=READER_PANEL_TOUCH_SETTINGS;assert(!reader_toolbar_transition(&ctx,APP_REDRAW_PAGE));
 }
 reset(READER_PANEL_NONE);assert(!reader_toolbar_transition(&ctx,APP_REDRAW_PAGE));
 assert(!reader_toolbar_transition(&ctx,APP_REDRAW_FULL)&&!reader_toolbar_transition(&ctx,APP_REDRAW_AREA));
 for(int large=0;large<2;large++){
  font_top=large?468:580;settings_top=large?376:580;
  const reader_panel_t targets[]={READER_PANEL_TOOLS,READER_PANEL_FONT_SETTINGS,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_STATS};
  for(unsigned i=0;i<4;i++){
   reset(i?READER_PANEL_TOOLS:READER_PANEL_NONE);s_reader_panel=targets[i];
   assert(reader_panel_fast_present(&ctx,APP_REDRAW_PAGE)&&draws==1&&pushes==1&&prepared==1&&ended==1&&!errors);
   assert(area.x==0&&area.width==684&&area.height==1216-area.y);
   assert(area.y==(i==0?1096:i==1?font_top:i==2?settings_top:READER_STATS_TOP));
   assert(!memcmp(front,original,(size_t)area.y*684/2)&&!memcmp(front,back,BYTES));
   assert(s_reader_panel_frame.valid&&s_reader_panel_frame.panel==targets[i]);
   bool same=targets[i]==READER_PANEL_FONT_SETTINGS||targets[i]==READER_PANEL_REFRESH_SETTINGS;
   assert(reader_panel_fast_present(&ctx,APP_REDRAW_PAGE)==same&&pushes==(same?2:1));
  }
 }
 for(int risk=0;risk<28;risk++){
  reset(READER_PANEL_NONE);s_reader_panel=READER_PANEL_TOOLS;app_redraw_t redraw=APP_REDRAW_PAGE;
  switch(risk){
   case 0:ctx.request_app=true;break;case 1:generation++;break;case 2:s_page++;break;case 3:s_chapter++;break;
   case 4:rotation=1;break;case 5:s_reader_fullscreen=false;break;case 6:s_text="different";break;
   case 7:s_clear_confirm=true;break;case 8:s_reader_notice[0]='x';break;case 9:images=1;break;
   case 10:background=true;break;case 11:s_reader_cleanup=true;break;case 12:s_reader_turn_pending=true;break;
   case 13:s_water_turn_pending=true;break;case 14:s_reader_footer_pending=true;break;case 15:s_reader_panel_frame.valid=false;break;
   case 16:front[0]^=1;break;case 17:redraw=APP_REDRAW_FULL;break;case 18:s_reader_panel=READER_PANEL_BACKGROUND;break;
   case 19:s_reader_panel=READER_PANEL_NONE;break;case 20:s_reader_panel_frame.panel=READER_PANEL_FONT_SETTINGS;break;
   case 21:ctx.hl=NULL;break;case 22:ctx.fb=NULL;break;case 23:hl.back_fb=NULL;break;case 24:s_view=SHELF;break;
   case 25:redraw=APP_REDRAW_AREA;break;case 26:s_text=NULL;break;case 27:s_fonts_reload_pending=true;break;
  }
  if(risk==10)assert(reader_panel_fast_present(&ctx,redraw)&&last_profile==DISPLAY_READER_GRAY_BACKGROUND);
  else assert(!reader_panel_fast_present(&ctx,redraw)&&!draws&&!pushes&&!prepared&&!ended);
 }
 reset(READER_PANEL_NONE);s_reader_panel=READER_PANEL_TOOLS;failed=true;
 assert(reader_panel_fast_present(&ctx,APP_REDRAW_PAGE)&&pushes==1&&errors==1&&!s_reader_panel_frame.valid);
 // 图文/背景底页可同样叠加37相面板，图像数变化仍拒绝复用。
 // Image/background underlays also admit37-phase overlays while changed image counts reject reuse.
 for(int picture=0;picture<2;++picture)for(int wall=0;wall<2;++wall){
  reset(READER_PANEL_TOOLS);images=picture;background=wall;reader_panel_record(EPD_DRAW_SUCCESS);
  s_reader_panel=READER_PANEL_FONT_SETTINGS;
  assert(reader_panel_fast_present(&ctx,APP_REDRAW_PAGE)&&pushes==1&&!last_full);
  assert(last_profile==(wall?DISPLAY_READER_GRAY_BACKGROUND:DISPLAY_READER_GRAY_PAGE));
  images++;assert(!reader_panel_fast_present(&ctx,APP_REDRAW_PAGE));
 }
 reset(READER_PANEL_FONT_SETTINGS);s_reader_slider=0;s_area=reader_slider_refresh_rect(0);s_mode=MODE_GL16;
 assert(reader_slider_feedback_present(&ctx,APP_REDRAW_AREA)&&pushes==1&&last_full);
 // 真实滑动手势去重；面板局部绘图/提交不碰正文，不变数值不输出，松手只提交一次。
 // Deduplicate actual gesture feedback; paint/present locally without body work and commit once on release.
 reset(READER_PANEL_FONT_SETTINGS);s_reader_slider=-1;full_draws=0;committed=0;
 ui_gesture_event_t move={UI_GESTURE_PRESS,40,700,40,700};
 assert(slider_gesture(&ctx,&move)==APP_REDRAW_NONE&&s_reader_slider==0&&!draws&&!pushes);
 move.type=UI_GESTURE_MOVE;
 for(int i=0;i<30;++i)assert(slider_gesture(&ctx,&move)==APP_REDRAW_NONE&&!draws&&!pushes);
 move.x=41;assert(slider_gesture(&ctx,&move)==APP_REDRAW_AREA&&draws==1&&!full_draws);
 assert(reader_slider_feedback_present(&ctx,APP_REDRAW_AREA)&&pushes==1&&!prepared&&!ended);
 assert(!memcmp(front,original,(size_t)font_top*684/2));
 assert(slider_gesture(&ctx,&move)==APP_REDRAW_NONE&&pushes==1&&draws==1);
 move.type=UI_GESTURE_TAP;move.x=42;assert(slider_gesture(&ctx,&move)==APP_REDRAW_PAGE&&committed==1&&s_reader_slider==-1);
 assert(!reader_slider_feedback_present(&ctx,APP_REDRAW_AREA));
 move=(ui_gesture_event_t){UI_GESTURE_PRESS,41,700,41,700};
 assert(slider_gesture(&ctx,&move)==APP_REDRAW_AREA);move.type=UI_GESTURE_CANCEL;
 assert(slider_gesture(&ctx,&move)==APP_REDRAW_PAGE&&s_reader_slider==-1&&committed==1);
 // 过期正文/缺卡资源边界继续回退；不能拿新一代正文当旧底页。
 // Stale body/media boundaries retain fallback and never reuse another layout generation.
 reset(READER_PANEL_FONT_SETTINGS);full_draws=0;generation++;s_reader_slider=0;
 assert(reader_slider_feedback(&ctx,0)==APP_REDRAW_AREA&&full_draws==1);
 assert(!reader_slider_feedback_present(&ctx,APP_REDRAW_AREA));
 // Full-rendered font adjustments may use short grays; images/background/turns/other books stay isolated.
 reset(READER_PANEL_TOOLS);s_reader_panel=READER_PANEL_FONT_SETTINGS;
 assert(reader_panel_short_ready(APP_REDRAW_PAGE));generation++;s_page++;
 assert(reader_panel_short_ready(APP_REDRAW_PAGE)); // final reflowed text+UI is deliberately admitted
 assert(!reader_panel_short_ready(APP_REDRAW_FULL)&&!reader_panel_short_ready(APP_REDRAW_AREA));
 images=1;assert(reader_panel_short_ready(APP_REDRAW_PAGE));images=0;
 background=true;assert(reader_panel_short_ready(APP_REDRAW_PAGE));background=false;
 s_fonts_reload_pending=true;assert(!reader_panel_short_ready(APP_REDRAW_PAGE));s_fonts_reload_pending=false;
 s_reader_turn_pending=true;assert(!reader_panel_short_ready(APP_REDRAW_PAGE));s_reader_turn_pending=false;
 s_reader_panel=READER_PANEL_BACKGROUND_PREVIEW;assert(reader_panel_short_ready(APP_REDRAW_PAGE));
 reset(READER_PANEL_NONE);s_reader_panel=READER_PANEL_NONE;assert(!reader_panel_short_ready(APP_REDRAW_PAGE));
 // Reuse completed body slots; overlay frames and notices never become cached body art.
 reset(READER_PANEL_NONE);cache_ready=false;stores=full_draws=outer_draws=0;
 draw_reader_cached(front,s_page);assert(full_draws==1&&stores==1&&cache_ready);
 s_reader_panel=READER_PANEL_FONT_SETTINGS;memset(front,0,BYTES);draw_reader_cached(front,s_page);
 assert(full_draws==1&&outer_draws==1&&stores==1&&front[0]==255&&front[100*684/2]==0x76&&front[BYTES-1]==0x43);
 pause_reader_prep();assert(cancels==1&&!invalidates&&cache_ready);
 generation++;draw_reader_cached(front,s_page);assert(full_draws==2&&stores==1); // new font/layout generation
 s_reader_panel=READER_PANEL_NONE;cache_oom=true;cache_ready=false;draw_reader_cached(front,s_page);assert(full_draws==3&&!cache_ready);
 cache_oom=false;strcpy(s_reader_notice,"notice");draw_reader_cached(front,s_page);assert(full_draws==4&&!cache_ready);
 s_reader_notice[0]=0;images=1;draw_reader_cached(front,s_page);assert(full_draws==5&&!cache_ready);
 images=0;background=true;draw_reader_cached(front,s_page);assert(full_draws==6&&!cache_ready);
 puts("PASS: actual S31 overlay one regional gray push,entry erasure only on panel changes,same-sheet/slider short feedback,untouched body,100/200% panel bounds,27 stale/state/error fallbacks;existing body slots reused,no overlay/notice cache,OOM and generation guards");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test';p.with_suffix('.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)

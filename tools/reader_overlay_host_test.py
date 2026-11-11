#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实阅读绘制/缩放路径：覆盖层遮住图文，收起后恢复原帧。 / Actual overlay paint and zoom crop regression."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
book = root / 'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_reader_layout.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_NAV_BACKGROUND_GRAY 0xe0
#define UI_GRAY_LIGHT 192
#define READER_TOOL_ICON_PX 40
#define UI_CLICK_BACK_SMALL 77
#define UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT 78
#define EPD_DRAW_ALIGN_CENTER 0
#define EPD_DRAW_ALIGN_LEFT 1
#define UI_ICON_BOOK_BOOKMARK 1
#define UI_ICON_BOOK 2
#define UI_ICON_EXPAND 3
#define READER_IMAGE_ZOOM_MIN 1000
#define MODE_GL16 1
#define TAG "overlay"
static char log_buf[128];
#define ESP_LOGI(tag,...) ((void)(tag),(void)snprintf(log_buf,sizeof(log_buf),__VA_ARGS__))
enum {READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_BACKGROUND_PREVIEW,READER_PANEL_SETTINGS};
typedef enum {APP_REDRAW_NONE,APP_REDRAW_DONE} app_redraw_t;
typedef struct {uint8_t *fb;void *hl;int64_t now_ms;} app_ctx_t;
static bool s_reader_fullscreen,immersive,vertical,s_reader_favorite,s_zoom_active;
static size_t s_page=1;
static int s_reader_panel,s_px=48,s_margin=24,s_chapter_lead_height,s_chapter_lead_width;
static char s_book_title[128]="Title",s_title[128]="Title",s_chapter_heading_title[128],s_chapter_heading_label[128],s_reader_notice[128];
static bool app_settings_reader_immersive(void){return immersive;}
static bool app_settings_book_vertical(void){return vertical;}
static int app_settings_book_tracking(void){return 0;}
static void ui_fill_rect_fast(EpdRect r,uint8_t color,uint8_t *fb){
 assert(r.x>=0&&r.y>=0&&r.x+r.width<=684&&r.y+r.height<=1216);
 for(int y=r.y;y<r.y+r.height;++y)memset(fb+y*684+r.x,color,r.width);
}
static void ui_text_set_system_scale(bool enabled){(void)enabled;}
static void ui_clear_page(uint8_t *fb){memset(fb,255,684*1216);}
static void ui_nav_status(uint8_t *fb){fb[40*684+342]=0;}
static void ui_click_feedback_register(uint8_t *fb,EpdRect r,int kind,int icon){(void)fb;(void)r;(void)kind;(void)icon;}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static int ui_text_title_fit(char *text,int px,int width,const char *sample){(void)text;(void)width;(void)sample;return px;}
static int ui_text_title_width_px(int px,const char*t,const char*s){(void)px;(void)t;(void)s;return 24;}
static void ui_text_title_vc(uint8_t *fb,int x,int y,int px,const char *text,const char *sample,int align){(void)px;(void)text;(void)sample;(void)align;fb[y*684+x]=0;}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){(void)px;(void)icon;(void)gray;fb[y*684+x]=0;}
static void ui_hairline(uint8_t *fb,int y,int x,int width,int gray){ui_fill_rect_fast((EpdRect){x,y,width,1},gray,fb);}
static int ui_content_width(void){return 612;}
static void draw_vertical_heading(uint8_t *fb,EpdRect r){ui_fill_rect_fast(r,71,fb);}
static void ui_reader_background_paint(uint8_t *fb,EpdRect r,bool overlay){(void)fb;(void)r;assert(overlay);}
static void draw_reader_images(uint8_t *fb,size_t page,EpdRect r){(void)page;ui_fill_rect_fast(r,85,fb);}
static char note_text[601];static const char *s_text=note_text;static size_t s_text_len=600,s_note_count=1;
static int s_footnote_note=-1;static EpdRect s_footnote_marker,note_card;
typedef struct{size_t text_start,text_len;char *owned_text;} html_note_t;
static html_note_t note={.text_start=0,.text_len=600},*s_notes=&note;
static int note_lines,note_x[24],note_y[24];
static int ttf_text_width_px(int px,const char *text){(void)text;return px/2;}
static int ttf_ascender_px(int px){return px*3/4;}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inverted){(void)fb;(void)x;(void)y;(void)px;(void)text;(void)align;(void)inverted;}
static void ttf_draw_text_px(uint8_t *fb,int x,int y,int px,const char *text,int align,int ink,int paper){
 (void)px;(void)text;(void)align;(void)ink;(void)paper;assert(note_lines<24);
 note_x[note_lines]=x;note_y[note_lines++]=y;fb[y*684+x]=13;
}
static void draw_reader_metadata(uint8_t *fb,size_t page){(void)page;ui_fill_rect_fast(ui_reader_footer_for(s_reader_fullscreen,immersive),238,fb);}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int color){if(radius==16)note_card=r;ui_fill_rect_fast(r,color,fb);}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inverse){(void)fb;(void)x;(void)y;(void)px;(void)text;(void)align;(void)inverse;}
static bool reader_chapter_undo_visible(void){return false;}
static EpdRect reader_chapter_undo_rect(void){return(EpdRect){0,0,20,20};}
static void ui_draw_control_frame(uint8_t *fb,EpdRect r,int radius,int color){(void)fb;(void)r;(void)radius;(void)color;}
static void ui_text_box(uint8_t *fb,EpdRect r,int px,const char *text,int align,bool inverse){(void)fb;(void)r;(void)px;(void)text;(void)align;(void)inverse;}
static void draw_reader_panel(uint8_t *fb){
 if(s_reader_panel==READER_PANEL_SETTINGS)ui_fill_rect_fast((EpdRect){0,580,684,636},221,fb);
 else if(!s_reader_fullscreen)ui_fill_rect_fast((EpdRect){0,1096,684,120},204,fb);
}
typedef struct {uint8_t *gray;int width,height;} reader_image_t;
static const reader_image_t *reader_zoom_image(void);
static void book_layout_draw_page(uint8_t *fb,size_t page,EpdRect r,int px){(void)page;(void)px;ui_fill_rect_fast(r,17,fb);}
'''
for name in ('reader_area', 'body_rect_for_tracking', 'body_rect', 'draw_reader_header', 'draw_reader_footnote_card', 'draw_reader_outer', 'draw_reader'):
    unit += function(name, book) + '\n'
unit += r'''
static uint8_t pixel,zoom_buffer;static reader_image_t picture={&pixel,16,16};
static bool s_image_zoom_bw,zoom_memory;static int64_t s_image_zoom_draw_ms;
static int s_image_zoom=2000,s_image_zoom_cx=342,s_image_zoom_cy=608;
static uint8_t *s_zoom_bits=&zoom_buffer;static EpdRect pushed;
static const reader_image_t *reader_zoom_image(void){return &picture;}
static void reader_zoom_reset(void){assert(false);}
static EpdRect book_layout_image_rect(void){return body_rect();}
static void reader_logical_to_phys(int x,int y,int *px,int *py){*px=x;*py=y;}
static void reader_image_base_size(int *w,int *h){EpdRect r=body_rect();*w=r.width;*h=r.height;}
static bool zoom_bits_ready(EpdRect r){assert(r.height>0);return zoom_memory;}
static void draw_zoomed_image(uint8_t *fb,EpdRect r,int scale,int x,int y,bool bw){(void)scale;(void)x;(void)y;assert(bw);ui_fill_rect_fast(r,42,fb);}
static void rails_keepalive(void){}
static void epd_poweron(void){}
static void guard_draw_result(void *hl,int err){(void)hl;assert(err==0);}
static int display_main_gray_waveform(void){return 1;}
static int update_display_area_diff_with(void *hl,int wave,int mode,EpdRect r){(void)hl;assert(wave==1&&mode==MODE_GL16);pushed=r;return 0;}
static int64_t esp_timer_get_time(void){return 100;}
static void zoom_bits_build(uint8_t *bits,EpdRect phys,EpdRect band,int iw,int ih,int w,int h,int left,int top){(void)bits;(void)phys;(void)band;(void)iw;(void)ih;(void)left;(void)top;assert(w==body_rect().width*2&&h==body_rect().height*2);}
static void zoom_blit_bits(uint8_t *fb,const uint8_t *bits,EpdRect r){(void)bits;ui_fill_rect_fast(r,42,fb);}
'''
unit += function('zoom_frame', book) + '\n'
unit += r'''
int main(void){
 static uint8_t fb[684*1216],original[sizeof(fb)],standard[sizeof(fb)];app_ctx_t ctx={.fb=fb};
 for(int style=0;style<2;++style)for(int v=0;v<2;++v){
  immersive=style;vertical=v;s_reader_panel=READER_PANEL_NONE;s_reader_fullscreen=true;
  draw_reader(fb,s_page);memcpy(original,fb,sizeof(fb));EpdRect body=body_rect();
  s_reader_fullscreen=false;draw_reader(fb,s_page);memcpy(standard,fb,sizeof(fb));
  assert(body_rect().y==body.y&&body_rect().height==body.height);
  assert(fb[100*684+300]==UI_NAV_BACKGROUND_GRAY&&fb[40*684+342]==0&&fb[114*684+620]==0);
  assert(fb[1060*684+300]==238&&fb[1150*684+300]==204);
  for(int y=152;y<176;++y)for(int x=0;x<684;++x)assert(fb[y*684+x]==UI_GRAY_WHITE);
  assert(!memcmp(fb+176*684,original+176*684,872*684));
  s_reader_panel=READER_PANEL_SETTINGS;draw_reader(fb,s_page);
  assert(!memcmp(fb+176*684,original+176*684,(580-176)*684)&&fb[700*684+300]==221);
  s_reader_panel=READER_PANEL_NONE;draw_reader(fb,s_page);assert(!memcmp(fb,standard,sizeof(fb)));
  for(int memory=0;memory<2;++memory){
   memcpy(fb,standard,sizeof(fb));zoom_memory=memory;
   assert(zoom_frame(&ctx)==APP_REDRAW_DONE&&pushed.y==176&&pushed.height==872);
   assert(!memcmp(fb,standard,176*684)&&!memcmp(fb+1048*684,standard+1048*684,(1216-1048)*684));
   assert(fb[500*684+300]==42);
  }
  s_reader_fullscreen=true;draw_reader(fb,s_page);assert(!memcmp(fb,original,sizeof(fb)));
 }
 // Use the actual footnote renderer through the final compositing pass, including markers
 // hidden by newly opened chrome and long cards in both text orientations.
 memset(note_text,'a',600);note_text[600]=0;s_footnote_note=0;
 const int marker_y[]={24,180,500,1020,1180};
 for(int full=0;full<2;++full)for(int style=0;style<2;++style)for(int v=0;v<2;++v)
 for(int px=36;px<=72;px+=12)for(int margin=0;margin<=48;margin+=24)
 for(unsigned at=0;at<sizeof(marker_y)/sizeof(marker_y[0]);++at)for(int long_note=0;long_note<2;++long_note){
  s_reader_fullscreen=full;immersive=style;vertical=v;s_px=px;s_margin=margin;
  s_reader_panel=READER_PANEL_NONE;note.text_len=long_note?600:12;
  s_footnote_marker=(EpdRect){320,marker_y[at],px,px};note_lines=0;note_card=(EpdRect){0};
  EpdRect body=body_rect(),visible=reader_area();draw_reader(fb,s_page);
  assert(note_lines>0&&note_card.y>=visible.y&&note_card.y+note_card.height<=visible.y+visible.height);
  assert(note_card.x>=body.x&&note_card.x+note_card.width<=body.x+body.width);
  for(int line=0;line<note_lines;++line){
   assert(note_y[line]>=visible.y&&note_y[line]<visible.y+visible.height);
   assert(fb[note_y[line]*684+note_x[line]]==13);
  }
  EpdRect after=body_rect();assert(!memcmp(&body,&after,sizeof(body)));
 }
 puts("PASS: actual short/long footnotes remain visible after final chrome composition, including offscreen markers, font sizes, margins, orientations and immersive styles");
 puts("PASS: actual draw order masks image/text below header/tools; settings leave exposed body intact; closing restores identical frame; zoom and OOM fallback preserve chrome and stable image scale");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-overlay-') as temporary:
    p = Path(temporary)
    (p / 'epdiy.h').write_text('#pragma once\ntypedef struct{int x,y,width,height;}EpdRect;\n')
    (p / 'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(p),'-I'+str(root/'main/ui'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

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
#include "book_chapter_lead.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_NAV_BACKGROUND_GRAY 0xe0
#define UI_GRAY_LIGHT 192
#define READER_TOOL_ICON_PX 40
#define UI_CLICK_BACK_SMALL 77
#define UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT 78
enum EpdFontFlags {EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_LEFT};
#define TTF_FONT_SLOT_SYSTEM 0
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
static app_book_heading_style_t heading_style;
static app_book_heading_align_t heading_align;
static unsigned heading_size=100;
static unsigned app_settings_book_heading_size(void){return heading_size;}
static app_book_heading_align_t app_settings_book_heading_align(void){return heading_align;}
static app_book_heading_style_t app_settings_book_heading_style(void){return heading_style;}
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
static int ui_text_title_width_px(int px,const char*t,const char*s){
 (void)s;
 int half_cells=0;
 for (const unsigned char *p=(const unsigned char *)t;*p;++p) {
  if (*p<0x80) ++half_cells;
  else if ((*p&0xc0)!=0x80) half_cells+=2;
 }
 return px*half_cells/2;
}
static int ui_text_title_fit(char *text,int px,int width,const char *sample){
 while (*text&&ui_text_title_width_px(px,text,sample)>width) {
  size_t end=strlen(text)-1;
  while (end&&((unsigned char)text[end]&0xc0)==0x80) --end;
  text[end]=0;
 }
 return px;
}
static int chapter_number_x, chapter_number_px, chapter_title_x, chapter_title_px;
static bool record_heading;
static char heading_texts[2][128];
static unsigned heading_count;
static int heading_x[2],heading_y[2],heading_px[2],heading_alignment[2];
static int first_heading_y;
static void capture_heading_text(const char *text,int y) {
 if (!record_heading) return;
 if (!heading_count) first_heading_y=y;
 assert(heading_count<2);
 snprintf(heading_texts[heading_count++],sizeof(heading_texts[0]),"%s",text);
}
static void ui_text_title_vc(uint8_t *fb,int x,int y,int px,const char *text,const char *sample,int align){
 (void)sample;(void)align;
 if(!*text)return;
 if(record_heading){heading_x[heading_count]=x;heading_y[heading_count]=y;heading_px[heading_count]=px;heading_alignment[heading_count]=align;}
 capture_heading_text(text,y);
 if(!strcmp(text,"12")){chapter_number_x=x;chapter_number_px=px;}
 if(!strcmp(text,"Title")){chapter_title_x=x;chapter_title_px=px;}
 ui_fill_rect_fast((EpdRect){x,y,1,1},0,fb);
}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){(void)px;(void)icon;(void)gray;fb[y*684+x]=0;}
static EpdRect last_hairline;
static void ui_hairline(uint8_t *fb,int y,int x,int width,int gray){
 last_hairline=(EpdRect){x,y,width,1};ui_fill_rect_fast(last_hairline,gray,fb);
}
static void ttf_font_select(int slot){assert(slot==TTF_FONT_SLOT_SYSTEM);}
static int ttf_vertical_text_height_px(int px,const char *text,int track){(void)track;return px*(int)strlen(text);}
static void ttf_draw_text_px_vertical(uint8_t *fb,int x,int y,int px,const char *text,int track,int ink,int paper){
 (void)track;(void)ink;(void)paper;
 if(!*text)return;
 if(record_heading){heading_x[heading_count]=x;heading_y[heading_count]=y;heading_px[heading_count]=px;}
 capture_heading_text(text,y);
 if(!strcmp(text,"12"))chapter_number_px=px;
 if(!strcmp(text,"Title"))chapter_title_px=px;
 ui_fill_rect_fast((EpdRect){x,y,1,1},0,fb);
}
static void ui_reader_background_paint(uint8_t *fb,EpdRect r,bool overlay){(void)fb;(void)r;assert(overlay);}
static void draw_reader_images(uint8_t *fb,size_t page,EpdRect r){if(page)ui_fill_rect_fast(r,85,fb);}
static char note_text[601];static const char *s_text=note_text;static size_t s_text_len=600,s_note_count=1;
static int s_footnote_note=-1;static EpdRect s_footnote_marker,note_card;
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
static void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,int color){(void)fb;(void)r;(void)radius;(void)color;}
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
static EpdRect body_rect(void);
static EpdRect reader_area(void);
static int chapter_view_height,chapter_view_padding;
static void book_layout_set_chapter_viewport(int height,int padding){
 chapter_view_height=height;chapter_view_padding=padding;
}
static int book_layout_page_content_height(size_t page){assert(!page);return chapter_view_height;}
static int book_layout_page_top_padding(size_t page){assert(!page);return chapter_view_padding;}
static void book_layout_draw_page(uint8_t *fb,size_t page,EpdRect r,int px){(void)page;(void)px;ui_fill_rect_fast(r,17,fb);}
'''
header=(root / 'main/settings.h').read_text()
start=header.rindex('typedef enum {',0,header.index('    APP_BOOK_HEADING_RULE,'))
end=header.index('app_book_heading_style_t app_settings_book_heading_style(void);',start)
unit=header[start:end]+unit
unit += (book.parent / 'book_reader_heading.inc').read_text() + '\n'
for name in ('reader_area', 'body_rect_for_tracking', 'body_rect', 'draw_reader_header', 'draw_reader_footnote_card', 'draw_vertical_heading', 'draw_reader_outer', 'draw_reader'):
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
 static uint8_t heading_frame[APP_BOOK_HEADING_COUNT][sizeof(fb)];
 for(int v=0;v<2;++v)for(int full=0;full<2;++full)for(int im=0;im<2;++im){
  vertical=v;s_reader_fullscreen=full;immersive=im;heading_size=150;
  EpdRect body=body_rect(),visible=reader_area();
  int top=body.y>visible.y?body.y:visible.y;
  int bottom=body.y+body.height<visible.y+visible.height?body.y+body.height:visible.y+visible.height;
  for(int style=0;style<APP_BOOK_HEADING_COUNT;++style)for(int align=0;align<3;++align){
   heading_style=style;heading_align=align;heading_count=0;record_heading=true;ui_clear_page(fb);
   const char *text="Volume I: Homeward";draw_reader_flow_heading(fb,body,text,strlen(text),v,true);
   record_heading=false;assert(heading_count==2);
   if(v){
    for(int i=0;i<2;++i){int height=ttf_vertical_text_height_px(heading_px[i],heading_texts[i],0);
     assert(abs(2*heading_y[i]+height-top-bottom)<=1);}
    assert(abs(2*heading_x[0]+heading_px[0]+heading_px[1]+52*150/100/3-2*body.x-body.width)<=1);
   }else{
    assert(heading_x[0]==body.x+body.width/2&&heading_x[1]==heading_x[0]);
    assert(heading_alignment[0]==EPD_DRAW_ALIGN_CENTER&&heading_alignment[1]==EPD_DRAW_ALIGN_CENTER);
    assert(abs((heading_y[0]-heading_px[0]/2)+(heading_y[1]+heading_px[1]/2)-top-bottom)<=2);
   }
   if(!style&&!align)memcpy(original,fb,sizeof(fb));else assert(!memcmp(original,fb,sizeof(fb)));
  }
 }
 puts("PASS: volume dividers center horizontally/vertically in every visible viewport, independently of chapter style/alignment");
 vertical=false;immersive=false;heading_size=100;heading_align=APP_BOOK_HEADING_ALIGN_CENTER;
 for(int v=0;v<2;++v)for(int has_label=0;has_label<2;++has_label){
  vertical=v;s_reader_fullscreen=true;EpdRect body=body_rect();
  strcpy(s_chapter_heading_title,has_label?"Homeward":"12");
  strcpy(s_chapter_heading_label,has_label?"Chapter 12":"");
  s_chapter_lead_height=v?0:214;
  s_chapter_lead_width=v?reader_heading_metrics(s_chapter_heading_title,s_chapter_heading_label,body).width:0;
  for(int style=0;style<APP_BOOK_HEADING_COUNT;++style){
   heading_style=style;
   ui_clear_page(fb);
   const char *inline_heading=has_label?"Chapter 12: Homeward":"12";
   draw_reader_flow_heading(fb,body,inline_heading,strlen(inline_heading),v,false);
   for(int y=body.y;y<body.y+body.height;++y)for(int x=body.x;x<body.x+body.width;++x){
    if(fb[y*684+x]==UI_GRAY_WHITE)continue;
    assert(fb[y*684+x]==UI_GRAY_BLACK);
    if(v)assert(x>=body.x+body.width-s_chapter_lead_width);
    else assert(y<body.y+(int)s_chapter_lead_height);
   }
   for(int prior=0;prior<style;++prior)assert(memcmp(fb,heading_frame[prior],sizeof(fb)));
   memcpy(heading_frame[style],fb,sizeof(fb));
  }
 }
 EpdRect preview={246,756,377,104};
 for(int style=0;style<APP_BOOK_HEADING_COUNT;++style){
  ui_clear_page(fb);
  draw_horizontal_heading_text(fb,preview,"Homeward","Chapter 12",style,APP_BOOK_HEADING_ALIGN_CENTER);
  int ink=0;
  for(int y=0;y<1216;++y)for(int x=0;x<684;++x){
   if(fb[y*684+x]==UI_GRAY_WHITE)continue;
   ++ink;assert(fb[y*684+x]==UI_GRAY_BLACK&&x>=preview.x&&x<preview.x+preview.width&&
                y>=preview.y&&y<preview.y+preview.height);
  }
  assert(ink>30);
  for(int prior=0;prior<style;++prior)assert(memcmp(fb,heading_frame[prior],sizeof(fb)));
  memcpy(heading_frame[style],fb,sizeof(fb));
 }
 s_chapter_heading_title[0]=s_chapter_heading_label[0]=0;
 s_chapter_lead_height=s_chapter_lead_width=0;
 EpdRect rule_band={120,300,440,184};
 for(int align=0;align<APP_BOOK_HEADING_ALIGN_COUNT;++align){
  heading_align=align;
  ui_clear_page(fb);
  draw_horizontal_heading_text(fb,rule_band,"Title","12",APP_BOOK_HEADING_RULE,align);
  assert(chapter_number_px==chapter_title_px*4/5);
  int rail=-1;
  for(int x=rule_band.x;x<rule_band.x+rule_band.width;++x){
   if(fb[(rule_band.y+55)*684+x]==UI_GRAY_BLACK){rail=x;break;}
  }
  assert(rail>=0);
  if(align==APP_BOOK_HEADING_ALIGN_RIGHT){
   assert(rail==rule_band.x+rule_band.width-7);
   assert(rail>chapter_number_x&&rail>chapter_title_x);
  }else{
   assert(rail<chapter_number_x&&rail<chapter_title_x);
   if(align==APP_BOOK_HEADING_ALIGN_LEFT) assert(rail==rule_band.x);
  }
  draw_horizontal_heading_text(fb,rule_band,"Title","12",APP_BOOK_HEADING_FLYLEAF,align);
  int measured=ui_text_title_width_px(chapter_title_px,"Title","Title");
  assert(last_hairline.width==measured&&last_hairline.x==chapter_title_x);
  assert(last_hairline.x==rule_band.x+heading_offset(align,rule_band.width-measured));
  draw_horizontal_heading_text(fb,rule_band,"Title longer","12",APP_BOOK_HEADING_FLYLEAF,align);
  assert(last_hairline.width>measured);
 }
 heading_align=APP_BOOK_HEADING_ALIGN_RIGHT;
 for (int style=0;style<APP_BOOK_HEADING_COUNT;++style) {
  heading_style=style;
  int last_horizontal_px=0,last_preview_px=0,last_vertical_px=0;
  for (heading_size=80;heading_size<=200;heading_size+=10) {
   rule_band.height=reader_heading_band_height(heading_size);
   ui_clear_page(fb);
   draw_horizontal_heading_text(fb,rule_band,"Title","12",style,heading_align);
   assert(chapter_title_px>last_horizontal_px);
   int number_px=style==APP_BOOK_HEADING_RULE?chapter_title_px*4/5:chapter_title_px;
   assert(chapter_number_px==number_px);
   last_horizontal_px=chapter_title_px;
   draw_horizontal_heading_text(fb,preview,"Title","12",style,heading_align);
   assert(chapter_title_px>=last_preview_px);
   last_preview_px=chapter_title_px;
   EpdRect vertical_band={18,176,204,872};
   reader_heading_t metrics=reader_heading_metrics("Title","12",vertical_band);
   assert(metrics.width==28+2*metrics.px);
   draw_vertical_heading_style(fb,vertical_band,metrics,"Title","12");
   assert(chapter_title_px>last_vertical_px);
   assert(chapter_title_px<=metrics.px);
   last_vertical_px=chapter_title_px;
  }
 }
 heading_size=100;
 // 四款题头的实际底边参与预留；单行不能继续占用双行设计框。
 // Reserve the actual lower edge for every style; single rows cannot retain a two-row canvas.
 record_heading=true;
 for(int style=0;style<APP_BOOK_HEADING_COUNT;++style)for(int align=0;align<APP_BOOK_HEADING_ALIGN_COUNT;++align){
  heading_style=style;heading_align=align;
  for(heading_size=80;heading_size<=200;heading_size+=10)for(int paired=0;paired<2;++paired){
   EpdRect canvas={36,200,612,reader_heading_band_height(heading_size)};
   heading_count=0;ui_clear_page(fb);
   int extent=horizontal_heading_at_size(NULL,canvas,"Homeward",paired?"Chapter 12":"",style,align,heading_size);
   assert(measure_reader_flow_heading(canvas,"12",2)>0);
   assert(extent>0&&extent<canvas.height&&!heading_count);
   for(size_t i=0;i<sizeof(fb);++i)assert(fb[i]==UI_GRAY_WHITE);
   draw_horizontal_heading_text(fb,canvas,"Homeward",paired?"Chapter 12":"",style,align);
   int bottom=canvas.y;
   for(unsigned i=0;i<heading_count;++i){int end=heading_y[i]+heading_px[i]/2;if(end>bottom)bottom=end;}
   for(int y=canvas.y;y<canvas.y+canvas.height;++y)for(int x=canvas.x;x<canvas.x+canvas.width;++x)
    if(fb[y*684+x]!=UI_GRAY_WHITE&&y+1>bottom)bottom=y+1;
   assert(abs(extent-(bottom-canvas.y))<=1);
  }
 }
 record_heading=false;heading_size=100;
 s_heading_layout_size=200;
 assert(measure_reader_flow_heading(rule_band,"12",2)==horizontal_heading_at_size(NULL,rule_band,"12","",heading_style,heading_align,200));
 s_heading_layout_size=0;
 strcpy(s_book_title,"TXT Book Name");
 const struct {
  const char *line, *number, *title;
 } txt_headings[]={
  {"第53章 公主的朋友","第53章","公主的朋友"},
  {"第 53 章公主的朋友","第 53 章","公主的朋友"},
  {"53. 公主的朋友","53","公主的朋友"},
  {"第53章",NULL,"第53章"},
  {"53",NULL,"53"},
  {"序章",NULL,"序章"},
 };
 record_heading=true;
 for (int v=0;v<2;++v) {
  EpdRect band={36,200,v?112:612,v?870:184};
  for (int style=0;style<APP_BOOK_HEADING_COUNT;++style) {
   heading_style=style;
   for (size_t i=0;i<sizeof(txt_headings)/sizeof(txt_headings[0]);++i) {
    heading_count=0;
    draw_reader_flow_heading(fb,band,txt_headings[i].line,strlen(txt_headings[i].line),v,false);
    assert(heading_count==(txt_headings[i].number?2u:1u));
    assert(first_heading_y>=band.y && first_heading_y<=band.y+(v?27:33));
    if (txt_headings[i].number) assert(!strcmp(heading_texts[0],txt_headings[i].number));
    assert(!strcmp(heading_texts[heading_count-1],txt_headings[i].title));
   }
  }
 }
 record_heading=false;
 // Chapter leads keep identical full-body geometry when temporary chrome is shown or hidden.
 strcpy(s_chapter_heading_title,"Homeward");
 for(int v=0;v<2;++v)for(int full=0;full<2;++full)for(int style=0;style<APP_BOOK_HEADING_COUNT;++style){
  vertical=v;s_reader_fullscreen=full;heading_style=style;heading_align=APP_BOOK_HEADING_ALIGN_RIGHT;
  s_chapter_lead_height=v?0:214;s_chapter_lead_width=v?64:0;
  reader_heading_viewport();
  ui_clear_page(fb);draw_reader_outer(fb,0);
  int top=body_rect().y;
  assert(chapter_view_height==body_rect().height&&chapter_view_padding==0);
  int ink_row=-1;
  for(int y=top;ink_row<0&&y<top+100;++y)for(int x=24;x<660;++x)
   if(fb[y*684+x]==UI_GRAY_BLACK){ink_row=y;break;}
  if(full)assert(ink_row>=top&&ink_row<=top+(v?27:33));
 }
 s_chapter_heading_title[0]=0;s_chapter_lead_height=s_chapter_lead_width=0;
 strcpy(s_book_title,"Title");
 puts("PASS: four distinct headings, reserved horizontal and vertical bands, and mirrored right alignment");
 puts("PASS: heading sizes from 80 to 200 percent in previews and both reading orientations");
 puts("PASS: TXT renders actual chapter names and number-only headings in all four styles, horizontally and vertically");
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
    (p / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n')
    (p / 'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(p),'-I'+str(root/'main/ui'),'-I'+str(root/'main/book'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

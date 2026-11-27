#!/usr/bin/env python3
"""中文：实际阅读插图函数的缓存、失败回退及灰阶边界回归。
English: Regress actual reader image caching, failure recovery and grayscale bounds.
SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
view = root / '.kiiko-source-view.json'
pico = '--pico' in sys.argv or (view.is_file() and json.loads(view.read_text())['board'] == 'pico')
book_app = root / ('boards/pico/src/main/apps/app_book.c' if pico else 'main/apps/app_book.c')
source = book_app.read_text()

def function(name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    cleaned = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), source, flags=re.S)
    at, depth, quote, escaped = match.end(), 1, None, False
    while depth:
        c = cleaned[at]
        if quote:
            if escaped: escaped = False
            elif c == '\\': escaped = True
            elif c == quote: quote = None
        elif c in "\"'": quote = c
        elif c == '{': depth += 1
        elif c == '}': depth -= 1
        at += 1
    return source[match.start():at]

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "html_text.h"
#include "book_chapter_lead.h"
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool touched;} fixture_touch_t;
typedef struct {
 int leaf;
 bool consumed,request_app,request_return,request_menu;
 fixture_touch_t *touch;
 int64_t now_ms;
} app_ctx_t;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define UI_PX_CAPTION 20
#define EPD_DRAW_ALIGN_CENTER 0
#define BOOK_KIND_EPUB 1
#define TAG "test"
static void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_FAIL -1
enum {READING,SHELF};
typedef struct {uint8_t *gray;int width,height;} reader_image_t;
static reader_image_t *s_page_images;
static int s_page_image_count;
static size_t s_part_base, s_part_begin, s_part_end;
static bool s_part_more;
static size_t s_page_images_for=SIZE_MAX;
static uint32_t s_page_images_generation;
static size_t s_page,s_chapter=3,s_image_count;
static char **s_images;
static bool hidden,locked;
static int alloc_fail_after=-1,read_failure=-1,decode_failure=-1;
static unsigned reads,decodes,pixels,notices,invalidations;
static uint8_t *capture;
enum { CAPTURE_WIDTH = 200, CAPTURE_HEIGHT = 220 };
static bool vary_image_pixels;
static EpdRect body={40,20,100,200};
// 插图可用区域：默认等于正文栏，通栏用例把它换成更宽的一条带。
// Illustration region: the body column by default; the full-bleed case widens the band.
static EpdRect image_band={40,20,100,200};
static EpdRect reader_area_rect={40,20,100,200};
static int pixel_min_x=1<<30,pixel_max_x=-1,pixel_min_y=1<<30,pixel_max_y=-1;
static struct {int image,y,width,height;} slots[40];
static int slot_count;
static int whole_image=-1;
static size_t fixture_image_page=SIZE_MAX;
typedef struct {size_t chapter;char **images;size_t count;const html_note_t *notes;size_t note_count;} reader_dims_ctx_t;
static reader_dims_ctx_t s_reader_dims_ctx;
static bool (*dims_fn)(void*,int,int*,int*);
static void *dims_ctx;
static size_t measured_chapter;
static uint32_t generation=1;
static bool app_settings_reader_hide_images(void){return hidden;}
static EpdRect body_rect(void){return body;}
static int reader_image_top(void){return reader_area_rect.y;}
static EpdRect reader_area(void){return reader_area_rect;}
static EpdRect book_layout_image_rect(void){return image_band;}
static size_t free_block=4u*1024u*1024u;
static size_t heap_caps_get_largest_free_block(int caps){(void)caps;return free_block;}
static size_t heap_caps_get_free_size(int caps){(void)caps;return free_block;}
static unsigned uxTaskGetStackHighWaterMark(void *task){(void)task;return 4096;}
// 竖排题头：本套用例只覆盖排版侧，几何一律留零。
// Vertical heading: this suite only covers the layout side, so the geometry stays zero.
typedef struct {int width,title_px,title_x,label_px,label_x,text_height;} reader_heading_t;
static reader_heading_t reader_heading_metrics(const char *title,const char *label,EpdRect area){
 (void)title;(void)label;(void)area;return (reader_heading_t){0};
}
static bool app_settings_book_vertical(void){return false;}
static uint32_t book_layout_generation(void){return generation;}
static bool fixture_page_has_images(size_t page){
 return page==(fixture_image_page==SIZE_MAX?s_page:fixture_image_page);
}
static int book_layout_page_image_count(size_t page){return fixture_page_has_images(page)?slot_count:0;}
static int book_layout_page_image(size_t page){return fixture_page_has_images(page)?whole_image:-1;}
static void book_layout_set_image_dims(bool (*fn)(void*,int,int*,int*),void *ctx){dims_fn=fn;dims_ctx=ctx;}
static void draw_reader_flow_heading(uint8_t *fb,EpdRect area,const char *text,size_t len,bool vertical,bool volume){
 (void)fb;(void)area;(void)text;(void)len;(void)vertical;(void)volume;
}
static void book_layout_set_heading_draw(void (*draw)(uint8_t *,EpdRect,const char *,size_t,bool,bool)){
 assert(draw==draw_reader_flow_heading);
}
static bool book_layout_page_image_at(size_t page,int i,int *image,int *y,int *w,int *h){
 if(!fixture_page_has_images(page)||i<0||i>=slot_count)return false;
 if(image)*image=slots[i].image;
 if(y)*y=slots[i].y;
 if(w)*w=slots[i].width;
 if(h)*h=slots[i].height;
 return true;
}
static bool book_layout_page_image_rect(size_t page,int i,int *image,int *x,int *y,int *w,int *h){
 if(x)*x=-1;
 return book_layout_page_image_at(page,i,image,y,w,h);
}
static bool fail_alloc(void){if(!alloc_fail_after)return true;if(alloc_fail_after>0)--alloc_fail_after;return false;}
static void *heap_caps_malloc(size_t n,int c){(void)c;if(n>free_block)return NULL;return fail_alloc()?NULL:malloc(n);}
static void *heap_caps_calloc(size_t n,size_t size,int c){(void)c;return fail_alloc()?NULL:calloc(n,size);}
static void invalidate_prep(void){++invalidations;}
static void reset_prefetch(void){++invalidations;}
static void release_image_headroom(void){}
static uint8_t *s_next_fb;
static void reader_page_cache_clear(void){s_next_fb=NULL;}
static void copy_text(char *out,size_t size,const char *in){snprintf(out,size,"%s",in);}
static void fit_text(char *out,int px,int max){(void)px;if(max<40)out[0]=0;}
static const char *esp_err_to_name(int err){(void)err;return "error";}
static void vTaskDelay(unsigned ticks){(void)ticks;sched_yield();}
static esp_err_t book_chapter_image(size_t chapter,const char *path,uint8_t **out,size_t *size,bool *png){
 assert(path);if(locked)measured_chapter=chapter;else assert(chapter==s_chapter);++reads;
 int image=atoi(path);
 if(image==read_failure)return ESP_FAIL;
 *out=malloc(1);assert(*out);**out=(uint8_t)image;*size=1;*png=false;return ESP_OK;
}
static esp_err_t book_chapter_image_dimensions(size_t chapter,const char *path,unsigned *w,unsigned *h){
 assert(path);if(locked)measured_chapter=chapter;else assert(chapter==s_chapter);
 if(atoi(path)==read_failure)return ESP_FAIL;*w=60;*h=20;return ESP_OK;
}
static bool book_image_dimensions(const uint8_t *data,size_t len,bool png,unsigned *w,unsigned *h){
 (void)png;assert(data&&len);*w=60;*h=20;return true;
}
static void fixture_image_pixels(const uint8_t *data,unsigned w,unsigned h,uint8_t *gray){
 if(vary_image_pixels){
  for(unsigned y=0;y<h;++y)
   for(unsigned x=0;x<w;++x)
    gray[y*w+x]=(uint8_t)(27*x+13*y+81*data[0]);
 }else memset(gray,128,(size_t)w*h);
}
static bool book_image_grayscale(const uint8_t *data,size_t len,bool png,unsigned w,unsigned h,uint8_t *gray){
 (void)png;assert(data&&len==1&&w&&h&&gray);++decodes;
 if(data[0]==decode_failure)return false;
 fixture_image_pixels(data,w,h,gray);return true;
}
#include "ui_image_dither.h"
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){
 (void)fb;
 assert(x>=image_band.x&&x<image_band.x+image_band.width);++pixels;
 if(!vary_image_pixels)assert(gray==119||gray==136);
 assert(y>=reader_area_rect.y&&y<reader_area_rect.y+reader_area_rect.height);
 if(capture){
  assert(x>=0&&x<CAPTURE_WIDTH&&y>=0&&y<CAPTURE_HEIGHT);
  capture[y*CAPTURE_WIDTH+x]=gray;
 }
 if(x<pixel_min_x)pixel_min_x=x;
 if(x>pixel_max_x)pixel_max_x=x;
 if(y<pixel_min_y)pixel_min_y=y;
 if(y>pixel_max_y)pixel_max_y=y;
}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inv){
 (void)fb;(void)align;(void)inv;assert(x>=body.x&&x<=body.x+body.width&&y-px/2>=body.y&&y+px/2<=body.y+body.height&&text);++notices;
}
static char s_path[128]="test";
static void book_excerpt_offer(const char *path,uint32_t chapter,const html_text_t *text){(void)path;(void)chapter;(void)text;}
static char *s_text;
static blk_t *s_blocks;
static html_run_t *s_runs;
static size_t s_run_count;
static html_note_t *s_notes;
static size_t s_note_count;
static int s_footnote_note = -1;
static const html_note_t *inline_notes;
static size_t inline_count;
static void book_layout_set_inline_images(const html_note_t *notes,size_t count){inline_notes=notes;inline_count=count;}
static void book_layout_set_runs(const html_run_t *runs,size_t count){(void)runs;(void)count;}
static size_t s_text_len,s_block_count,s_selected_toc=2,s_jump_offset,s_jump_page;
static struct {bool valid;} s_chapter_jump_undo;
static size_t s_chapter_lead_skip;
static unsigned s_chapter_lead_height,s_chapter_lead_row_height;
static unsigned s_chapter_lead_width;
static int s_px=10,s_view=READING;
static char s_message[128],s_chapter_heading_title[128],s_chapter_heading_label[32],s_title[128],s_font_path[128];
static int load_error,reflow_failures,reflows;
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static int fixture_book_kind;
static int book_kind(void){return fixture_book_kind;}
static const char *ttf_font_path(void){return "font";}
static esp_err_t book_chapter_title(size_t chapter,char *out,size_t cap){snprintf(out,cap,"chapter%zu",chapter);return ESP_OK;}
void html_notes_free(html_note_t *notes, size_t count){
 for(size_t i=0;i<count;++i)free(notes[i].owned_text);
 free(notes);
}
void html_text_free(html_text_t *text){
 free(text->utf8);free(text->blocks);free(text->runs);html_notes_free(text->notes,text->note_count);for(size_t i=0;i<text->image_count;++i)free(text->images[i]);free(text->images);memset(text,0,sizeof(*text));
}
static esp_err_t book_chapter_load_part(size_t chapter,size_t offset,bool last,const char *anchor,size_t source,size_t *off,html_text_t *out){
 (void)chapter;(void)last;assert(!locked);if(load_error)return ESP_FAIL;*off=anchor||source!=SIZE_MAX?4:offset;
 out->utf8=strdup("new\nIMG");out->len=7;out->blocks=calloc(2,sizeof(blk_t));out->count=2;
 out->blocks[0]=(blk_t){.offset=0,.len=3,.image=-1};out->blocks[1]=(blk_t){.offset=4,.len=3,.image=0};
 out->images=calloc(1,sizeof(char*));out->images[0]=strdup("0");out->image_count=1;out->notes=calloc(1,sizeof(html_note_t));out->notes[0].image=-1;out->note_count=1;return ESP_OK;
}
static void book_layout_set_chapter_lead(size_t skip,unsigned height){(void)skip;(void)height;}
static void book_layout_set_chapter_lead_width(unsigned px){(void)px;}
static void apply_chapter_lead(const char *title,const char *label,size_t skip,unsigned height){
 (void)height;
 (void)title;(void)label;s_chapter_lead_skip=skip;s_chapter_lead_height=0;s_chapter_lead_width=0;
 book_layout_set_chapter_lead(skip,0);
}
static bool book_layout_build_blocks(const char *text,size_t len,const blk_t *blocks,size_t count,EpdRect r,int px){
 (void)text;(void)len;(void)blocks;(void)count;(void)r;(void)px;assert(locked);++reflows;++generation;
 if(dims_fn){assert(inline_notes==((reader_dims_ctx_t*)dims_ctx)->notes&&inline_count==((reader_dims_ctx_t*)dims_ctx)->note_count);int w=0,h=0;assert(dims_fn(dims_ctx,0,&w,&h)&&w==60&&h==20);}
 if(reflow_failures){--reflow_failures;return false;}return true;
}
static size_t book_layout_page_count(void){return 2;}
static unsigned percent(size_t page){(void)page;return 34;}
static size_t book_layout_page_for_offset(size_t off){return off>=4?1:0;}
static void release_page_images(void);
static void free_book(void){
 release_page_images();free(s_text);free(s_blocks);free(s_runs);for(size_t i=0;i<s_image_count;++i)free(s_images[i]);free(s_images);
 s_text=NULL;s_blocks=NULL;s_runs=NULL;s_run_count=0;s_images=NULL;s_image_count=0;
 book_layout_set_image_dims(NULL,NULL);book_layout_set_inline_images(NULL,0);free(s_notes);s_notes=NULL;s_note_count=0;s_reader_dims_ctx=(reader_dims_ctx_t){0};
}
'''
unit+=r"""
#define READER_IMAGE_ZOOM_MIN 1000
static bool s_zoom_active,s_image_pan_active,s_image_zoom_bw;
static int s_zoom_slot,s_image_zoom,s_image_zoom_cx,s_image_zoom_cy;
static size_t s_image_zoom_page,s_zoom_bits_bytes;
static uint32_t s_image_zoom_start_distance;
static uint8_t *s_zoom_bits;
static struct {bool pending;}s_reader_tap;
static void continuous_du_reset(void){}
static bool reader_single_image_page(void){return whole_image>=0;}
static void draw_zoomed_image(uint8_t *fb,EpdRect band,int scale,int cx,int cy,bool bw){
 (void)fb;(void)band;(void)scale;(void)cx;(void)cy;(void)bw;assert(false);
}
"""
if pico:
    unit += r'''
#define TEST_PICO 1
#define BOOK_STORE_PATH_MAX 288
#define UI_LOCK_WIDTH CAPTURE_WIDTH
#define UI_LOCK_HEIGHT CAPTURE_HEIGHT
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
enum { READER_PANEL_NONE, READER_PANEL_BACKGROUND_PREVIEW };
static int s_reader_panel;
static bool s_reader_fullscreen=true;
static char s_reader_notice[64];
static uint8_t paper_gray=0xdd;
static void ui_text_set_system_scale(bool enabled){(void)enabled;}
static void ui_clear_page(uint8_t *fb){memset(fb,255,CAPTURE_WIDTH*CAPTURE_HEIGHT);}
static int book_layout_page_top_padding(size_t page){(void)page;return 0;}
static int book_layout_page_content_height(size_t page){(void)page;return 200;}
static void draw_vertical_heading(uint8_t *fb,EpdRect area){(void)fb;(void)area;assert(false);}
static void draw_horizontal_heading(uint8_t *fb,EpdRect area){(void)fb;(void)area;assert(false);}
static void ui_reader_background_paint(uint8_t *fb,EpdRect area,bool light){
 assert(light&&area.x==0&&area.y==0&&area.width==CAPTURE_WIDTH&&area.height==CAPTURE_HEIGHT);
 for(int i=0;i<CAPTURE_WIDTH*CAPTURE_HEIGHT;++i)
  if(fb[i]==255)fb[i]=paper_gray;
}
static void draw_reader_footnote_card(uint8_t *fb,size_t page,EpdRect area){(void)fb;(void)page;(void)area;}
static void draw_reader_header(uint8_t *fb){(void)fb;}
static void draw_reader_metadata(uint8_t *fb,size_t page){(void)fb;(void)page;}
static bool reader_chapter_undo_visible(void){return false;}
static EpdRect reader_chapter_undo_rect(void){return (EpdRect){0};}
static void ui_fill_round_rect(uint8_t *fb,EpdRect area,int radius,int gray){
 (void)fb;(void)area;(void)radius;(void)gray;assert(false);
}
static void ui_draw_round_rect(uint8_t *fb,EpdRect area,int radius,int gray){
 (void)fb;(void)area;(void)radius;(void)gray;assert(false);
}
static void ui_draw_control_frame(uint8_t *fb,EpdRect area,int radius,int gray){
 (void)fb;(void)area;(void)radius;(void)gray;assert(false);
}
static void ui_text_box(uint8_t *fb,EpdRect area,int size,const char *text,int align,bool bold){
 (void)fb;(void)area;(void)size;(void)text;(void)align;(void)bold;assert(false);
}
static void draw_reader_panel(uint8_t *fb){(void)fb;}
static const reader_image_t *reader_zoom_image(void){return NULL;}
static void book_layout_draw_page(uint8_t *fb,size_t page,EpdRect area,int px){
 (void)page;(void)area;(void)px;
 fb[199*CAPTURE_WIDTH+42]=0x33;
}

typedef pthread_t TaskHandle_t;
#define pdPASS 1
#define pdFAIL 0
static bool fixture_worker_live, fixture_task_fail;
static TaskHandle_t fixture_worker;
static void (*fixture_task)(void *);
static void *fixture_task_arg;
static void *fixture_thread(void *unused){
 (void)unused;
 fixture_task(fixture_task_arg);
 return NULL;
}
static int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,
                                 void *arg,unsigned priority,TaskHandle_t *handle,int core){
 (void)name;(void)stack;(void)priority;(void)core;
 assert(!fixture_worker_live);
 if(fixture_task_fail)return pdFAIL;
 fixture_task=fn;
 fixture_task_arg=arg;
 assert(pthread_create(&fixture_worker,NULL,fixture_thread,NULL)==0);
 fixture_worker_live=true;
 if(handle)*handle=fixture_worker;
 return pdPASS;
}
static void vTaskDelete(void *task){assert(!task);pthread_exit(NULL);}
static void fixture_join_worker(void){
 if(!fixture_worker_live)return;
 assert(pthread_join(fixture_worker,NULL)==0);
 fixture_worker_live=false;
}

typedef struct zip_stream {
 size_t offset;
 uint8_t resource;
} zip_stream_t;
enum { FIXTURE_ENCODED_SIZE = 64 * 1024 };
static unsigned stream_opens,stream_reads,stream_closes;
static atomic_uint speculative_decodes;
static int stream_failure=-1,progressive_resource=-1;
static unsigned s_back_turns;
static bool s_clear_confirm,s_chapter_display_pending;
#define s_toolbar (s_reader_panel != READER_PANEL_NONE)
static bool queued_touch;
static bool app_touch_input_pending(app_ctx_t *ctx){assert(ctx);return queued_touch;}
static atomic_bool fixture_scanning;
static pthread_mutex_t fixture_gate_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixture_gate_signal=PTHREAD_COND_INITIALIZER;
static bool fixture_hold_decoder,fixture_decoder_entered;
static bool fixture_release_decoder;
static bool book_image_speculative_dimensions(const uint8_t *data,size_t len,bool *png,
                                             unsigned *w,unsigned *h){
 assert(data&&len==FIXTURE_ENCODED_SIZE);
 if(data[0]==progressive_resource)return false;
 *png=(data[0]&1)==0;
 *w=60;
 *h=20;
 return true;
}
static bool book_image_grayscale_speculative(const uint8_t *data,size_t len,bool png,
                                            unsigned w,unsigned h,uint8_t *gray,
                                            bool (*checkpoint)(void *),void *ctx){
 assert(data&&len==FIXTURE_ENCODED_SIZE&&w&&h&&gray);
 assert(png==((data[0]&1)==0));
 ++speculative_decodes;
 assert(pthread_mutex_lock(&fixture_gate_lock)==0);
 if(fixture_hold_decoder){
  fixture_decoder_entered=true;
  assert(pthread_cond_broadcast(&fixture_gate_signal)==0);
  while(!fixture_release_decoder)
   assert(pthread_cond_wait(&fixture_gate_signal,&fixture_gate_lock)==0);
 }
 assert(pthread_mutex_unlock(&fixture_gate_lock)==0);
 if(!checkpoint(ctx)||data[0]==decode_failure)return false;
 assert(!fixture_scanning);
 fixture_image_pixels(data,w,h,gray);
 return true;
}
static esp_err_t book_chapter_image_stream(size_t chapter,const char *path,size_t budget,
                                          zip_stream_t **stream,size_t *size){
 assert(chapter==s_chapter&&path);
 assert(!fixture_scanning);
 if(budget<FIXTURE_ENCODED_SIZE)return ESP_FAIL;
 *stream=calloc(1,sizeof(**stream));
 assert(*stream);
 (*stream)->resource=(uint8_t)atoi(path);
 *size=FIXTURE_ENCODED_SIZE;
 ++stream_opens;
 return ESP_OK;
}
static esp_err_t zip_stream_read_idle(zip_stream_t *stream,void *dst,size_t cap,size_t *received){
 assert(!fixture_scanning);
 assert(stream&&cap&&cap<=32*1024&&stream->offset<=FIXTURE_ENCODED_SIZE);
 ++stream_reads;
 if(stream->resource==stream_failure)return ESP_FAIL;
 *received=FIXTURE_ENCODED_SIZE-stream->offset;
 if(*received>cap)*received=cap;
 memset(dst,stream->resource,*received);
 stream->offset+=*received;
 return ESP_OK;
}
static void zip_stream_close(zip_stream_t *stream){
 if(!stream)return;
 ++stream_closes;
 free(stream);
}
'''
unit+='\n'+function('reader_zoom_reset')
for name in ('inline_ink_gray', 'reader_image_slots', 'reader_image_slot', 'reader_image_dims', 'alloc_page_bitmap', 'release_page_images'):
    unit += '\n' + function(name)
if pico:
    unit += source[source.index('static void reader_image_fit('):
                   source.index('// 渐进式插图要', source.index('static void reader_image_fit('))]
for name in ('prepare_inline_image', 'draw_reader_images', 'reader_footer_progress', 'load_chapter_at'):
    unit += '\n' + function(name)
if pico:
    unit += '\n' + function('draw_reader_outer') + '\n' + function('draw_reader')
unit += r'''
static void setup_images(int count){
 release_page_images();for(size_t i=0;i<s_image_count;++i)free(s_images[i]);free(s_images);
 s_images=calloc((size_t)count,sizeof(char*));s_image_count=(size_t)count;slot_count=count;
 for(int i=0;i<count;++i){char path[16];snprintf(path,sizeof(path),"%d",i);s_images[i]=strdup(path);slots[i].image=i;slots[i].y=i*12;slots[i].width=20;slots[i].height=10;}
 ++generation;alloc_fail_after=-1;hidden=false;read_failure=decode_failure=-1;reads=decodes=pixels=notices=0;whole_image=-1;
}
#ifdef TEST_PICO
static void fixture_begin_prefetch(void){
 reader_image_prep_cancel();
 fixture_join_worker();
 free_book();
 setup_images(2);
 s_view=READING;
 s_text=strdup("reading");
 assert(s_text);
 fixture_book_kind=BOOK_KIND_EPUB;
 strcpy(s_path,"sample.epub");
 s_chapter=3;
 s_page=0;
 fixture_image_page=1;
 slots[0].width=47;
 slots[1].width=53;
 slots[0].height=16;
 slots[1].height=18;
 slots[1].y=64;
 vary_image_pixels=true;
 speculative_decodes=stream_opens=stream_reads=stream_closes=0;
 progressive_resource=stream_failure=-1;
 fixture_hold_decoder=fixture_decoder_entered=fixture_release_decoder=fixture_task_fail=false;
}

static void fixture_stage(app_ctx_t *ctx){
 for(unsigned i=0;i<40;++i){
  unsigned io=stream_opens+stream_reads;
  reader_image_prep_idle(ctx);
  assert(stream_opens+stream_reads<=io+1);
  if(s_image_prep && atomic_load(&s_image_prep->state)!=IMAGE_PREP_STAGING)return;
 }
 assert(false);
}

static void fixture_render(uint8_t *frame){
 capture=frame;
 draw_reader(frame,s_page);
 capture=NULL;
 assert(frame[199*CAPTURE_WIDTH+42]==0x33);
 assert(frame[0]==paper_gray);
}

static void fixture_wait_decoder(void){
 assert(pthread_mutex_lock(&fixture_gate_lock)==0);
 while(!fixture_decoder_entered)
  assert(pthread_cond_wait(&fixture_gate_signal,&fixture_gate_lock)==0);
 assert(pthread_mutex_unlock(&fixture_gate_lock)==0);
}

static void fixture_unblock_decoder(void){
 assert(pthread_mutex_lock(&fixture_gate_lock)==0);
 fixture_release_decoder=true;
 assert(pthread_cond_broadcast(&fixture_gate_signal)==0);
 assert(pthread_mutex_unlock(&fixture_gate_lock)==0);
}

static void *fixture_scan_begin(void *unused){
 (void)unused;
 epd_image_scan_hook(true);
 fixture_scanning=true;
 return NULL;
}

static void *fixture_turn_to_image(void *unused){
 (void)unused;
 s_page=1;
 prepare_inline_image();
 return NULL;
}

static void test_image_prefetch(void){
 app_ctx_t ctx={0};
 uint8_t cached[CAPTURE_WIDTH*CAPTURE_HEIGHT],cold[sizeof(cached)];
 fixture_begin_prefetch();
 fixture_stage(&ctx);
 fixture_join_worker();
 assert(stream_opens==2&&stream_reads==4&&stream_closes==2);
 assert(speculative_decodes==2&&decodes==0&&reads==0);
 s_page=1;
 paper_gray=0xee;  // Change the paper after decoding, before using the result.
 prepare_inline_image();
 assert(!s_image_prep&&s_page_image_count==2&&reads==0&&decodes==0);
 assert(s_page_images[0].gray&&s_page_images[1].gray);
 fixture_render(cached);
 assert(pixels==(unsigned)(47*15+53*17)&&notices==0);
 paper_gray=0xbb;
 fixture_render(cached);
 assert(reads==0&&decodes==0);
 s_page=0;
 prepare_inline_image();
 assert(!s_page_images);
 s_page=1;
 prepare_inline_image();
 assert(reads==2&&decodes==2);
 fixture_render(cold);
 assert(memcmp(cached,cold,sizeof(cached))==0);

 // Same geometry/count on a different target still has to take the normal path.
 enum { BOOK_CHANGED, CHAPTER_CHANGED, PAGE_CHANGED, RESOURCE_CHANGED, WIDTH_CHANGED, HEIGHT_CHANGED,
        LAYOUT_CHANGED, EXPLICIT_CANCEL, LAST_MISS=EXPLICIT_CANCEL };
 for(int miss=BOOK_CHANGED;miss<=LAST_MISS;++miss){
  fixture_begin_prefetch();
  fixture_stage(&ctx);
  fixture_join_worker();
  assert(atomic_load(&s_image_prep->state)==IMAGE_PREP_READY);
  s_page=1;
  switch(miss){
   case BOOK_CHANGED:strcpy(s_path,"another.epub");break;
   case CHAPTER_CHANGED:++s_chapter;break;
   case PAGE_CHANGED:s_page=0;fixture_image_page=0;break;
   case RESOURCE_CHANGED:slots[1].image=0;break;
   case WIDTH_CHANGED:--slots[1].width;break;
   case HEIGHT_CHANGED:--slots[1].height;break;
   case LAYOUT_CHANGED:++generation;break;
   case EXPLICIT_CANCEL:reader_image_prep_cancel();break;
  }
  prepare_inline_image();
  assert(!s_image_prep&&reads==2&&decodes==2);
  assert(s_page_image_count==2&&s_page_images[0].gray&&s_page_images[1].gray);
 }

 // The first image may decode successfully before a later unsupported/failed slot.
 enum { STREAM_FAILURE, PROGRESSIVE, ALLOCATION_FAILURE, TASK_FAILURE };
 for(int failure=STREAM_FAILURE;failure<=TASK_FAILURE;++failure){
  fixture_begin_prefetch();
  if(failure==STREAM_FAILURE)stream_failure=1;
  if(failure==PROGRESSIVE)progressive_resource=1;
  if(failure==ALLOCATION_FAILURE)alloc_fail_after=2;
  if(failure==TASK_FAILURE)fixture_task_fail=true;
  fixture_stage(&ctx);
  fixture_join_worker();
  assert(s_image_prep&&atomic_load(&s_image_prep->state)==IMAGE_PREP_FAILED);
  assert(!s_image_prep->images&&stream_opens==stream_closes);
  assert(speculative_decodes==(failure==PROGRESSIVE?1u:0u));
  alloc_fail_after=-1;
  s_page=1;
  prepare_inline_image();
  assert(!s_image_prep&&reads==2&&decodes==2);
  assert(s_page_images[0].gray&&s_page_images[1].gray);
 }

 // Pending input or navigation pauses staging at the same ZIP position.
 fixture_begin_prefetch();
 fixture_touch_t touch={.touched=true};
 ctx.touch=&touch;
 reader_image_prep_idle(&ctx);
 assert(!stream_opens&&!s_image_prep);
 touch.touched=false;
 reader_image_prep_idle(&ctx);
 assert(s_image_prep_stream&&stream_opens==1&&!stream_reads);
 s_reader_tap.pending=true;
 reader_image_prep_idle(&ctx);
 s_reader_tap.pending=false;
 queued_touch=true;
 reader_image_prep_idle(&ctx);
 queued_touch=false;
 ctx.request_return=true;
 reader_image_prep_idle(&ctx);
 ctx.request_return=false;
 assert(!stream_reads);
 reader_image_prep_cancel();
 assert(!s_image_prep_stream&&stream_closes==1);
 ctx.touch=NULL;

 // A turn cancels at the next decode checkpoint and reclaims the worker's scratch
 // before allocating the foreground bitmaps.
 fixture_begin_prefetch();
 fixture_hold_decoder=true;
 fixture_stage(&ctx);
 fixture_wait_decoder();
 reader_image_prep_t *inflight=s_image_prep;
 pthread_t turner;
 assert(pthread_create(&turner,NULL,fixture_turn_to_image,NULL)==0);
 while(!atomic_load(&inflight->cancelled))sched_yield();
 fixture_unblock_decoder();
 assert(pthread_join(turner,NULL)==0);
 assert(!s_image_prep&&reads==2&&decodes==2);
 fixture_render(cached);
 fixture_join_worker();
 assert(speculative_decodes==1);
 fixture_render(cold);
 assert(memcmp(cached,cold,sizeof(cached))==0);

 // The real scan hook waits for the active decode checkpoint, then prevents
 // further worker progress until scan end.
 fixture_begin_prefetch();
 fixture_hold_decoder=true;
 fixture_stage(&ctx);
 fixture_wait_decoder();
 pthread_t scanner;
 assert(pthread_create(&scanner,NULL,fixture_scan_begin,NULL)==0);
 while(!(atomic_load(&s_image_prep_scan)&IMAGE_PREP_SCAN))sched_yield();
 assert(!fixture_scanning&&(atomic_load(&s_image_prep_scan)&IMAGE_PREP_BUSY));
 fixture_unblock_decoder();
 assert(pthread_join(scanner,NULL)==0);
 assert(fixture_scanning&&speculative_decodes==1);
 assert(atomic_load(&s_image_prep->state)==IMAGE_PREP_RUNNING);
 fixture_scanning=false;
 epd_image_scan_hook(false);
 fixture_join_worker();
 assert(atomic_load(&s_image_prep->state)==IMAGE_PREP_READY);
 reader_image_prep_cancel();
 free_book();
 puts("PASS: Pico next-image cache ownership, pixel parity, stale keys, fallback, input and concurrent scan guards");
}
#endif
int main(void){
 uint8_t fb=0;
 // Text-to-text within one layout keeps prefetch; image pages and reflows still invalidate it.
 char footer[64];
 reader_footer_progress(footer,sizeof(footer),0);assert(!strcmp(footer,"1/2  34%"));
 s_part_more=true;
 reader_footer_progress(footer,sizeof(footer),0);assert(!strcmp(footer,"1/2 段内  34%"));
 s_part_more=false;
 setup_images(0);prepare_inline_image();unsigned prior_invalidations=invalidations;
 ++s_page;prepare_inline_image();assert(invalidations==prior_invalidations);
 ++generation;prepare_inline_image();assert(invalidations==prior_invalidations+1);
 setup_images(12);prepare_inline_image();assert(reads==12&&decodes==12&&s_page_image_count==12);
 draw_reader_images(&fb,s_page,body);assert(pixels==12*20*6&&!notices);
 prepare_inline_image();assert(reads==12);
 unsigned old_pixels=pixels;draw_reader_images(&fb,s_page+1,body);assert(pixels==old_pixels);
 ++generation;draw_reader_images(&fb,s_page,body);assert(pixels==old_pixels);
 prepare_inline_image();assert(reads==24);draw_reader_images(&fb,s_page,body);assert(pixels==24*20*6);
 hidden=true;prepare_inline_image();assert(!s_page_images&&!s_page_image_count);draw_reader_images(&fb,s_page,body);assert(pixels==24*20*6);
 hidden=false;prepare_inline_image();assert(reads==36&&s_page_image_count==12);
 // 未知尺寸槽仍解码，保持原始比例和灰阶；失败提示留在自己的位置。
 // Unknown-size slots still decode proportionally in grayscale; failures stay in their own slots.
 setup_images(1);slots[0].width=100;slots[0].height=200;
 prepare_inline_image();assert(s_page_images[0].width==60&&s_page_images[0].height==20);
 draw_reader_images(&fb,s_page,body);assert(pixels==1200);
 // PR9 整页回退没有混排记录，调用侧仍须显示图片。/ PR9's full-page fallback has no inline record but must still draw.
 // Multiple images are decoded on entry, once per page and generation.
 setup_images(2);prepare_inline_image();
 assert(reads==2&&decodes==2&&s_page_images[0].gray&&s_page_images[1].gray);
 prepare_inline_image();assert(reads==2);
 setup_images(1);slot_count=0;whole_image=0;prepare_inline_image();
 assert(s_page_image_count==1&&s_page_images[0].gray&&reads==1);
 draw_reader_images(&fb,s_page,body);assert(pixels==1200);
 setup_images(3);for(int i=0;i<3;++i){slots[i].width=100;slots[i].height=40;slots[i].y=i*50;}
 read_failure=0;decode_failure=1;prepare_inline_image();draw_reader_images(&fb,s_page,body);
 assert(reads==3&&decodes==2&&notices==2&&pixels==60*20);
 // 数组及位图分配失败都安全释放，下一次布局代次可恢复。
 // Array and bitmap allocation failures release safely and recover on a new layout generation.
 setup_images(1);slots[0].width=100;slots[0].height=200;
 alloc_fail_after=0;prepare_inline_image();assert(!s_page_images&&!reads);draw_reader_images(&fb,s_page,body);assert(notices==1);
 alloc_fail_after=-1;++generation;prepare_inline_image();assert(s_page_images[0].gray);
 setup_images(1);slots[0].width=100;slots[0].height=200;
 alloc_fail_after=1;prepare_inline_image();assert(s_page_images&&!s_page_images[0].gray);draw_reader_images(&fb,s_page,body);assert(notices==1);
 alloc_fail_after=-1;++generation;prepare_inline_image();assert(s_page_images[0].gray);
 // 新章节加载/重排失败保留原章、图名、页码、目录选择及题头。
 // Failed chapter load/layout preserves the old chapter, images, page, selection and header.
 s_text=strdup("old\nIMG");s_text_len=7;s_blocks=calloc(2,sizeof(blk_t));s_block_count=2;
 s_blocks[0]=(blk_t){.offset=0,.len=3,.image=-1};s_blocks[1]=(blk_t){.offset=4,.len=3,.image=0};
 s_chapter=3;s_page=1;s_selected_toc=2;s_chapter_lead_skip=2;s_chapter_lead_height=12;
 strcpy(s_chapter_heading_title,"old title");char *old_text=s_text;char **old_images=s_images;app_ctx_t ctx={0};
 s_notes=calloc(1,sizeof(html_note_t));s_notes[0].image=-1;s_note_count=1;html_note_t *old_notes=s_notes;book_layout_set_inline_images(s_notes,s_note_count);
 s_reader_dims_ctx=(reader_dims_ctx_t){s_chapter,s_images,s_image_count,s_notes,s_note_count};book_layout_set_image_dims(reader_image_dims,&s_reader_dims_ctx);
 load_error=1;assert(!load_chapter_at(&ctx,4,0,false,NULL,SIZE_MAX));
 assert(s_text==old_text&&s_images==old_images&&s_chapter==3&&s_page==1&&s_selected_toc==2&&!locked);
 load_error=0;reflow_failures=1;assert(!load_chapter_at(&ctx,4,0,false,NULL,SIZE_MAX));
 assert(reflows==2&&s_text==old_text&&s_images==old_images&&s_chapter==3&&s_page==1&&s_selected_toc==2&&!locked);
 assert(s_chapter_lead_skip==2&&s_chapter_lead_height==12&&!strcmp(s_chapter_heading_title,"old title"));
 assert(measured_chapter==3&&s_reader_dims_ctx.images==old_images);
 assert(s_notes==old_notes&&s_note_count==1&&inline_notes==old_notes&&inline_count==1);
 assert(load_chapter_at(&ctx,4,0,false,NULL,SIZE_MAX));
 assert(s_chapter==4&&!strcmp(s_text,"new\nIMG")&&s_page==0&&s_selected_toc==SIZE_MAX&&!locked);
 assert(measured_chapter==4&&s_reader_dims_ctx.images==s_images);
 assert(s_notes&&s_notes!=old_notes&&s_note_count==1&&inline_notes==s_notes&&s_footnote_note==-1);
 reflow_failures=2;assert(!load_chapter_at(&ctx,5,0,false,NULL,SIZE_MAX));assert(!s_text&&s_view==SHELF&&!locked);
 assert(!dims_fn&&!s_reader_dims_ctx.images);
 free_book();release_page_images();
 // 通栏位图放不进最大空闲块时按块大小缩一档，而不是丢掉整页插图。
 // A full-bleed bitmap that does not fit the largest free block steps down instead of dropping.
 setup_images(1);slots[0].width=100;slots[0].height=200;
 free_block=600;prepare_inline_image();
 assert(s_page_images[0].gray&&s_page_images[0].width==39&&s_page_images[0].height==12);
 pixel_min_x=1<<30;pixel_max_x=-1;
 draw_reader_images(&fb,s_page,body);assert(pixels==468&&pixel_min_x==70&&pixel_max_x==108);
 free_block=4u*1024u*1024u;release_page_images();
 // 页首插图贴到阅读区顶端，正文上方的呼吸空间只留给正文。
 // A page-start illustration sits at the top of the reading area; the inset above body text
 // stays with the text.
 setup_images(1);slots[0].width=100;slots[0].height=100;slots[0].y=0;
 reader_area_rect=(EpdRect){40,0,100,200};
 prepare_inline_image();assert(s_page_images[0].width==60&&s_page_images[0].height==20);
 pixel_min_y=1<<30;pixel_max_y=-1;
 draw_reader_images(&fb,s_page,body);assert(pixel_min_y==0&&pixel_max_y==19);
 reader_area_rect=body;release_page_images();
 // 通栏：插图在整屏宽的一条带里居中，正文栏的左右边距不再限制它。
 // Full bleed: the illustration centers in the panel-wide band and the body margins no longer
 // bound it.
 setup_images(1);slots[0].width=60;slots[0].height=20;
 image_band=(EpdRect){0,body.y,200,body.height};
 pixel_min_x=1<<30;pixel_max_x=-1;
 prepare_inline_image();assert(s_page_images[0].width==60&&s_page_images[0].height==20);
 draw_reader_images(&fb,s_page,body);
 assert(pixels==1200&&pixel_min_x==70&&pixel_max_x==129);
 image_band=body;
 release_page_images();
#ifdef TEST_PICO
 test_image_prefetch();
#endif
 puts("PASS: images load automatically once per page, cache generation, OOM recovery, full bleed and chapter rollback");
}
'''
with tempfile.TemporaryDirectory() as folder:
    c, binary = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-pthread', '-I'+str(root/'main/book'),
                    '-I'+str(root/'tools/html_text_stubs'), '-I'+str(root/'main/ui'),
                    str(c), str(root/'main/ui/ui_image_dither.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)

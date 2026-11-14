#!/usr/bin/env python3
"""中文：真实书架整页合并异步封面结果、失败退避重试，触摸和切页优先。/ English: Actual shelf batches cover reveals, retries transient failures, and prioritizes navigation.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1];book=ROOT/'main/apps/app_book.c'
source = book.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "book_cover_jobs.h"
typedef struct{int x,y,width,height;}EpdRect;
enum{SHELF,MANAGE,BULK};
typedef struct{bool touched;}touch_t;
typedef struct{int leaf;bool request_app,request_return,request_menu;int64_t now_ms;touch_t *touch;uint8_t *fb;}app_ctx_t;
#define BOOK_ROWS 9
#define UI_LOCK_WIDTH 684
#define SHELF_FAST_COVER_STRIDE 21
#define TAG "test"
#define ESP_LOGW(...) ((void)0)
static int s_view=SHELF,s_visible_count=9,rows=9,nav_stop=-1;
static bool queued_touch __attribute__((unused));
static unsigned no_artwork_mask,s_shelf_no_artwork_mask;
static unsigned ram_mask;
static bool shelf_cover_ram_hit(int index,int row,bool load);
static void shelf_record_cover_result(int index,int state,const book_cover_result_t *result){
 (void)index;(void)state;(void)result;
}
static bool s_category_menu,s_sort_menu,s_clear_confirm,s_scan_pending;
static unsigned s_cover_generation,s_cover_pending_mask,s_cover_decode_mask,s_cover_dirty_mask,s_cover_present_mask;
static unsigned ready_mask,failed_mask,nav_polls,queued,warm_calls,s_cover_warm_mask;
static int64_t s_cover_warm_after_ms;
static uint8_t s_cover_retry_count[9];
static int64_t s_cover_retry_ms[9];
static bool fast,s_cover_batch_present_pending;
static __attribute__((unused)) bool read_pico_sd_io_ready(void){return true;}

static struct{int index;uint8_t *gray,*fast_bits;unsigned width,height,fast_contrast,fast_phase;bool fast_white_edge,generated;}s_covers[9];
static bool shelf_cover_ram_hit(int index,int row,bool load){
 assert(index==row&&load);
 const unsigned bit=1u<<row;
 if (!(ram_mask & bit)) return false;
 s_covers[row].gray=calloc(1,1);
 assert(s_covers[row].gray);
 s_cover_dirty_mask|=bit;
 return true;
}
static struct{char path[32],name[32],author[32];bool removed,favorite,has_progress;int pct;unsigned size;}s_shelf[9];
static int shelf_rows(void){return rows;}
static int app_settings_shelf_style(void){return 2;}
static bool app_settings_main_fast_refresh(void){return fast;}
static unsigned app_settings_system_contrast(void){return 100;}
static EpdRect shelf_cover_image(int row){return(EpdRect){20,192+row*282,164,214};}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static bool app_touch_input_pending(app_ctx_t *ctx){return ctx->request_app||queued_touch;}
static bool pico_boot_asset_allowed(const char *path){return path!=NULL;}
unsigned book_cover_jobs_begin(void){return 1;}
bool book_cover_jobs_submit(unsigned generation,unsigned slot,const book_cover_job_t *q){
 assert(generation==1&&slot<9&&q->fast==fast);queued|=1u<<slot;
 return true;
}
int book_cover_jobs_take(unsigned generation,unsigned slot,book_cover_result_t *result){assert(generation==1);
 if (no_artwork_mask & (1u<<slot)) return BOOK_COVER_JOB_NO_ARTWORK;
 if(failed_mask&(1u<<slot))return -1;if(!(ready_mask&(1u<<slot)))return 0;
 *result=(book_cover_result_t){.pixels=malloc(1),.width=164,.height=214,.fast=fast};assert(result->pixels);return 1;}
'''
unit+=r'''
enum{BOOK_SHELF_PAGE_MISS,BOOK_SHELF_PAGE_WAIT,BOOK_SHELF_PAGE_HIT};
typedef int book_shelf_page_state_t;
static bool s_shelf_page_loading,s_shelf_page_cached;
static book_shelf_page_state_t book_shelf_page_poll(void){return BOOK_SHELF_PAGE_MISS;}
static void prepare_covers(app_ctx_t *ctx){(void)ctx;assert(false);}
'''
unit += function('shelf_page_mask', book) + '\n'
functions=['prepare_cover_batch','shelf_cover_frame_ready','shelf_cover_reveal_mask']
for name in functions:
 unit+=function(name,book)+'\n'
present=function('present',book)
ack=present[present.index('    if ((s_view==SHELF || s_view==MANAGE || s_view==BULK) && err==EPD_DRAW_SUCCESS) {'):present.index('    if (err==EPD_DRAW_SUCCESS && reader_status_visible()')]
unit += r'''enum {EPD_DRAW_SUCCESS, EPD_DRAW_FAULT};
typedef enum {APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL, APP_REDRAW_NONE} app_redraw_t;
enum {MODE_DU, MODE_GL16, EPD_DRAW_ALIGN_CENTER, SHELF_LIST_BOUNCE_PX = 2, SHELF_BOOK_LIFT_PX = 6};
static char s_message[128];
static bool s_shelf_feedback_pending;
static bool s_shelf_page_cache_allowed, center_hint, shelf_page_turn;
static bool s_shelf_read_ready __attribute__((unused));
static int s_mode, s_pressed_control = -1;
static EpdRect s_area;
static unsigned art_drawn, slots_drawn;
static EpdRect clip_refresh_rect(EpdRect rect){return rect;}
static EpdRect row_rect(int row){return (EpdRect){36+(row%3)*208,192+(row/3)*282,196,260};}
static void ui_text_fixed_vc(uint8_t *fb,int x,int y,int px,const char *label,int align,bool invert){
 (void)fb;(void)x;(void)px;(void)label;(void)align;(void)invert;assert(y==610);center_hint=true;
}
static bool book_shelf_page_restore(uint8_t *fb){(void)fb;return false;}
static bool book_shelf_page_stage_covers(uint8_t *fb){(void)fb;return false;}
static bool book_shelf_page_capture(uint8_t *fb,unsigned edges,int64_t now){(void)fb;(void)edges;(void)now;return false;}
static bool shelf_cover_white_edge(int row,EpdRect rect){(void)row;(void)rect;return false;}
static void draw_shelf_cover(uint8_t *fb,EpdRect rect,int row,const char *name,bool favorite){
 (void)fb;(void)rect;(void)name;(void)favorite;slots_drawn|=1u<<row;
 if(shelf_cover_frame_ready()&&(s_covers[row].gray||s_covers[row].fast_bits))art_drawn|=1u<<row;
}
static void draw_shelf_furniture(uint8_t *fb){(void)fb;}
static void draw_shelf_cover_favorite(uint8_t *fb,int row,EpdRect rect,bool favorite){
 (void)fb;(void)row;(void)rect;(void)favorite;
}
static void draw_list_row(uint8_t *fb,int row,void *entry){(void)fb;(void)row;(void)entry;}
static __attribute__((unused)) void draw_cover_progress(uint8_t *fb,EpdRect rect,int progress){(void)fb;(void)rect;(void)progress;}
'''
for name in ['shelf_title_card','prepare_shelf_cover_slots','draw_shelf_page_artwork']:
    unit += function(name, book) + '\n'
if 'shelf_loading_hint_active(' in source:
    unit += function('shelf_loading_hint_active', book) + '\n'
    unit += function('shelf_loading_hint_update', book) + '\n'
unit += function('ui_rect_union', ROOT/'main/ui/ui_kit.c') + '\n'
unit += r'''
static void render(app_ctx_t *ctx,uint8_t *fb){
 center_hint=false;art_drawn=slots_drawn=0;
 draw_shelf_page_artwork(ctx,fb);
}
'''
unit += function('shelf_cover_tick', book) + '\n'
unit += 'static void ack_present(int redraw,int err){'+ack+'}\n'
unit+=r'''
static void reset(app_ctx_t *ctx){for(unsigned i=0;i<9;++i){free(s_covers[i].gray);free(s_covers[i].fast_bits);memset(&s_covers[i],0,sizeof(s_covers[i]));s_covers[i].index=i;snprintf(s_shelf[i].path,32,"book%u",i);}
 s_cover_generation=0;s_cover_pending_mask=511;s_cover_decode_mask=s_cover_dirty_mask=s_cover_present_mask=0;
 memset(s_cover_retry_count,0,sizeof(s_cover_retry_count));memset(s_cover_retry_ms,0,sizeof(s_cover_retry_ms));
 ready_mask=failed_mask=queued=nav_polls=0;ctx->request_app=false;ctx->now_ms=0;nav_stop=-1;
 s_message[0]=0;s_scan_pending=s_shelf_page_loading=false;
 queued_touch=false;no_artwork_mask=s_shelf_no_artwork_mask=ram_mask=0;
}
int main(void){touch_t touch={0};app_ctx_t ctx={.touch=&touch};
 for(int mode=0;mode<3;++mode){fast=mode==1;reset(&ctx);
  assert(!prepare_cover_batch(&ctx,true)&&queued==511&&!shelf_cover_frame_ready());
  ready_mask=7;assert(prepare_cover_batch(&ctx,false)==7&&s_cover_dirty_mask==7);
  assert(shelf_cover_reveal_mask()==0);
  s_cover_batch_present_pending=false;ack_present(APP_REDRAW_PAGE,EPD_DRAW_SUCCESS);
  assert(s_cover_dirty_mask==7);
  s_cover_dirty_mask=7; // Reproduce a failed partial submission carrying all three groups.
  ready_mask=511;assert(prepare_cover_batch(&ctx,false)==504&&shelf_cover_reveal_mask()==511);
  s_cover_batch_present_pending=true;s_cover_present_mask=511;ack_present(APP_REDRAW_AREA,EPD_DRAW_FAULT);assert(s_cover_dirty_mask==511);
  s_cover_present_mask=511;ack_present(APP_REDRAW_AREA,EPD_DRAW_SUCCESS);assert(!s_cover_dirty_mask);
 }
 reset(&ctx);touch.touched=true;assert(!prepare_cover_batch(&ctx,false)&&!queued);touch.touched=false;
 // 失败不再永久丢弃：先退避重试两次，第三次才留占位并在稍后再试。
 // A failure is not dropped forever: retry twice with backoff, then leave a placeholder.
 reset(&ctx);ready_mask=511;failed_mask=4;
 assert(prepare_cover_batch(&ctx,true)==507);
 assert(!shelf_cover_frame_ready());
 assert(!s_covers[2].gray&&!s_covers[2].fast_bits);
 assert(s_cover_retry_count[2]==1&&s_cover_retry_ms[2]>ctx.now_ms);
 assert(s_cover_retry_ms[2]==60&&!shelf_cover_reveal_mask());
 assert(prepare_cover_batch(&ctx,true)==0);assert(s_cover_retry_count[2]==1);
 ctx.now_ms=s_cover_retry_ms[2];assert(prepare_cover_batch(&ctx,true)==0&&s_cover_retry_count[2]==2);
 assert(shelf_cover_frame_ready());
 assert(!s_covers[2].gray&&!s_covers[2].fast_bits);
 reset(&ctx);s_scan_pending=true;s_visible_count=0;
 render(&ctx,ctx.fb);assert(center_hint&&!slots_drawn);
 s_scan_pending=false;s_visible_count=4;
 s_covers[0].index=-1;s_cover_pending_mask=0;
 strcpy(s_shelf[0].path,"cold.epub");
 strcpy(s_shelf[1].path,"cached.epub");
 strcpy(s_shelf[2].path,"title.TXT");
 strcpy(s_shelf[3].path,"no-cover.epub");
 prepare_shelf_cover_slots(&ctx);
 assert(s_cover_pending_mask==11&&s_covers[2].index==2);
 render(&ctx,ctx.fb);assert(!center_hint&&slots_drawn==15&&!art_drawn);
 ready_mask=2;
 queued_touch=true;
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE&&queued==0);
 queued_touch=false;
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE&&queued==11&&s_cover_dirty_mask==2);
 render(&ctx,ctx.fb);assert(!art_drawn);
 ack_present(APP_REDRAW_PAGE,EPD_DRAW_SUCCESS);assert(s_cover_dirty_mask==2);
 no_artwork_mask=8;
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE&&s_shelf_no_artwork_mask==8);
 assert(s_cover_retry_count[3]==0);
 ready_mask=3;
 queued_touch=true;
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE&&s_cover_dirty_mask==2);
 queued_touch=false;
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_AREA&&s_cover_present_mask==3&&art_drawn==3);
 assert(s_area.y==192&&s_area.y+s_area.height==452&&!s_message[0]);
 ack_present(APP_REDRAW_AREA,EPD_DRAW_FAULT);
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_AREA&&s_cover_present_mask==3);
 ack_present(APP_REDRAW_AREA,EPD_DRAW_SUCCESS);
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE);

 reset(&ctx);s_visible_count=9;ready_mask=7;
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE&&s_cover_dirty_mask==7);
 ready_mask=63;
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE&&s_cover_dirty_mask==63);
 ready_mask=511;
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_AREA&&s_cover_present_mask==511&&!s_message[0]);
 ack_present(APP_REDRAW_AREA,EPD_DRAW_SUCCESS);
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE);
 reset(&ctx);s_visible_count=9;
 s_covers[0].index=-1;s_cover_pending_mask=0;ram_mask=511;shelf_page_turn=true;
 prepare_shelf_cover_slots(&ctx);
 render(&ctx,ctx.fb);
 assert(art_drawn==511&&s_cover_pending_mask==0&&queued==0);
 ack_present(APP_REDRAW_AREA,EPD_DRAW_SUCCESS);
 assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE&&queued==0);
 shelf_page_turn=false;
 puts("PASS: shelf holds partial results, skips TXT and known missing artwork, and reveals a completed page in one retry-safe update");
 (void)warm_calls;(void)s_cover_warm_mask;(void)s_cover_warm_after_ms;
 for(unsigned i=0;i<9;++i){free(s_covers[i].gray);free(s_covers[i].fast_bits);}
 puts("PASS: queue without IO, reveal ready cells, retain dirty covers through failed displays, retry with backoff and prioritize touch");
}
'''
assert 'const bool artwork = shelf_cover_frame_ready();' in function('draw_shelf_cover', book)
assert 'shelf_cover_tick(ctx)' in function('on_tick', book)
assert 's_cover_batch_present_pending = true;' in function('shelf_cover_tick', book)
with tempfile.TemporaryDirectory() as d:
 c=Path(d)/'test.c';exe=Path(d)/'test';c.write_text(unit)
 subprocess.run(['cc','-Dshelf_display_style=app_settings_shelf_style','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'main/book'),str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

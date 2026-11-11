#!/usr/bin/env python3
"""中文：真实书架逐本合并异步封面结果、失败退避重试，触摸和切页优先。/ English: Actual shelf reveals asynchronous covers per cell, retries transient failures with backoff, and prioritizes navigation.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1];book=ROOT/'main/apps/app_book.c'
retry_covers = 's_cover_retry_count' in book.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover_jobs.h"
typedef struct{int x,y,width,height;}EpdRect;
enum{SHELF,MANAGE,BULK};
typedef struct{bool touched;}touch_t;
typedef struct{int leaf;bool request_app,request_return,request_menu;int64_t now_ms;touch_t *touch;}app_ctx_t;
#define BOOK_ROWS 9
#define UI_LOCK_WIDTH 684
#define SHELF_FAST_COVER_STRIDE 21
#define TAG "test"
#define ESP_LOGW(...) ((void)0)
static int s_view=SHELF,s_visible_count=9,rows=9,nav_stop=-1;
static unsigned s_cover_generation,s_cover_pending_mask,s_cover_decode_mask,s_cover_dirty_mask,s_cover_present_mask;
static unsigned ready_mask,failed_mask,nav_polls,queued,warm_calls,s_cover_warm_mask;
static int64_t s_cover_warm_after_ms;
static uint8_t s_cover_retry_count[9];
static int64_t s_cover_retry_ms[9];
static bool fast,s_clear_confirm,s_batch_confirm,s_cover_batch_present_pending;
static int lock_style=2,s_presented_view=-1;
static bool s_shelf_page_pending;
static int64_t fake_us;static unsigned ready_after;
static int64_t esp_timer_get_time(void){return fake_us;}
static void vTaskDelay(unsigned ticks){assert(ticks==1);fake_us+=1000;if(ready_after&&fake_us>=(int64_t)ready_after*1000)ready_mask=511;}
static __attribute__((unused)) bool read_pico_sd_io_ready(void){return true;}

static struct{int index;uint8_t *gray,*fast_bits;unsigned width,height,fast_contrast,fast_phase;bool fast_white_edge;}s_covers[9];
static struct{char path[32],name[32],author[32];bool removed;}s_shelf[9];
static int shelf_rows(void){return rows;}
static int app_settings_shelf_style(void){return 2;}
static bool app_settings_main_fast_refresh(void){return fast;}
static unsigned app_settings_system_contrast(void){return 100;}
static int app_settings_lock_style(void){return lock_style;}
static bool book_lock_collage_warm_cover(const char *path){assert(path);++warm_calls;return true;}
static EpdRect shelf_cover_image(int row){return(EpdRect){20,192+row*282,164,214};}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static bool app_poll_main_navigation(app_ctx_t *ctx){++nav_polls;if(nav_stop>=0&&(int)nav_polls>=nav_stop){ctx->request_app=true;return true;}return false;}
unsigned book_cover_jobs_begin(void){return 1;}
bool book_cover_jobs_submit(unsigned generation,unsigned slot,const book_cover_job_t *q){assert(generation==1&&slot<9&&q->fast==fast);queued|=1u<<slot;return true;}
int book_cover_jobs_take(unsigned generation,unsigned slot,book_cover_result_t *result){assert(generation==1);
 if(failed_mask&(1u<<slot))return -1;if(!(ready_mask&(1u<<slot)))return 0;
 *result=(book_cover_result_t){.pixels=malloc(1),.width=164,.height=214,.fast=fast};assert(result->pixels);return 1;}
'''
unit+=r'''
enum{BOOK_SHELF_PAGE_MISS,BOOK_SHELF_PAGE_WAIT,BOOK_SHELF_PAGE_HIT};
typedef int book_shelf_page_state_t;
static bool s_shelf_page_loading,s_shelf_page_cached;
static book_shelf_page_state_t book_shelf_page_poll(void){return BOOK_SHELF_PAGE_MISS;}
static unsigned shelf_page_mask(app_ctx_t *ctx){(void)ctx;return 511;}
static void prepare_covers(app_ctx_t *ctx){(void)ctx;assert(false);}
'''
for name in ['prepare_cover_batch','shelf_cover_frame_ready','shelf_cover_reveal_mask','warm_shelf_lock_cover','gather_cover_entry']:
 unit+=function(name,book)+'\n'
present=function('present',book)
ack=present[present.index('    if ((s_view==SHELF || s_view==MANAGE || s_view==BULK) && err==EPD_DRAW_SUCCESS) {'):present.index('    if (err==EPD_DRAW_SUCCESS && reader_status_visible()')]
unit+='enum{EPD_DRAW_SUCCESS,EPD_DRAW_FAULT};enum{APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL};\nstatic void ack_present(int redraw,int err){'+ack+'}\n'
unit+=r'''
static void reset(app_ctx_t *ctx){for(unsigned i=0;i<9;++i){free(s_covers[i].gray);free(s_covers[i].fast_bits);memset(&s_covers[i],0,sizeof(s_covers[i]));s_covers[i].index=i;snprintf(s_shelf[i].path,32,"book%u",i);}
 s_cover_generation=0;s_cover_pending_mask=511;s_cover_decode_mask=s_cover_dirty_mask=s_cover_present_mask=0;
 memset(s_cover_retry_count,0,sizeof(s_cover_retry_count));memset(s_cover_retry_ms,0,sizeof(s_cover_retry_ms));
 ready_mask=failed_mask=queued=nav_polls=0;ctx->request_app=false;ctx->now_ms=0;nav_stop=-1;
}
int main(void){touch_t touch={0};app_ctx_t ctx={.touch=&touch};
 for(int mode=0;mode<3;++mode){fast=mode==1;reset(&ctx);
  assert(!prepare_cover_batch(&ctx,true)&&queued==511&&!shelf_cover_frame_ready());
  ready_mask=7;assert(prepare_cover_batch(&ctx,false)==7&&s_cover_dirty_mask==7&&shelf_cover_reveal_mask()==(TEST_COVER_RETRIES?7:0));
  s_cover_batch_present_pending=false;ack_present(APP_REDRAW_PAGE,EPD_DRAW_SUCCESS);assert(s_cover_dirty_mask==7);
  ready_mask=511;assert(prepare_cover_batch(&ctx,false)==504&&shelf_cover_reveal_mask()==511);
  s_cover_batch_present_pending=true;s_cover_present_mask=511;ack_present(APP_REDRAW_AREA,EPD_DRAW_FAULT);assert(s_cover_dirty_mask==511);
  s_cover_present_mask=511;ack_present(APP_REDRAW_AREA,EPD_DRAW_SUCCESS);assert(!s_cover_dirty_mask);
 }
 reset(&ctx);fake_us=0;ready_after=12;gather_cover_entry(&ctx);assert(shelf_cover_frame_ready()&&shelf_cover_reveal_mask()==511&&fake_us==12000);
 reset(&ctx);fake_us=0;ready_after=0;gather_cover_entry(&ctx);assert(!shelf_cover_frame_ready()&&fake_us==60000);
 reset(&ctx);fake_us=0;nav_stop=1;gather_cover_entry(&ctx);assert(ctx.request_app&&fake_us==0);
 reset(&ctx);fake_us=0;s_presented_view=SHELF;gather_cover_entry(&ctx);assert(fake_us==0&&!nav_polls);s_presented_view=-1;
 reset(&ctx);touch.touched=true;assert(!prepare_cover_batch(&ctx,false)&&!queued);touch.touched=false;
 nav_stop=3;assert(!prepare_cover_batch(&ctx,true)&&ctx.request_app&&queued==3);
 // 失败不再永久丢弃：先退避重试两次，第三次才留占位并在稍后再试。
 // A failure is not dropped forever: retry twice with backoff, then leave a placeholder.
 reset(&ctx);ready_mask=511;failed_mask=4;assert(prepare_cover_batch(&ctx,true)==507);
#if TEST_COVER_RETRIES
 assert(!shelf_cover_frame_ready());
 assert(!s_covers[2].gray&&!s_covers[2].fast_bits);
 assert(s_cover_retry_count[2]==1&&s_cover_retry_ms[2]>ctx.now_ms);
 assert(prepare_cover_batch(&ctx,true)==0);assert(s_cover_retry_count[2]==1);
 ctx.now_ms=s_cover_retry_ms[2];assert(prepare_cover_batch(&ctx,true)==0&&s_cover_retry_count[2]==2);
 ctx.now_ms=s_cover_retry_ms[2];assert(prepare_cover_batch(&ctx,true)==0&&s_cover_retry_count[2]==3&&s_cover_pending_mask==0);
 #endif
 assert(shelf_cover_frame_ready());
 assert(!s_covers[2].gray&&!s_covers[2].fast_bits);
 s_cover_warm_mask=3;ctx.now_ms=2000;warm_shelf_lock_cover(&ctx);assert(warm_calls==1&&s_cover_warm_mask==2);
 for(unsigned i=0;i<9;++i){free(s_covers[i].gray);free(s_covers[i].fast_bits);}
 puts("PASS: all3 modes queue without IO, gather warm hits within 60ms with immediate nav cancellation, reveal per ready cell, retain dirty covers through placeholder/failed displays, retry transient failures with backoff, prioritize touch/navigation and degrade missing/OOM slots safely");
}
'''
artwork = ('s_view != SHELF || !(s_cover_pending_mask & (1u << row))'
           if retry_covers else 'shelf_cover_frame_ready()')
assert 'const bool artwork = '+artwork+';' in function('draw_shelf_cover',book)
assert 's_cover_batch_present_pending = true;' in function('on_tick',book)
with tempfile.TemporaryDirectory() as d:
 c=Path(d)/'test.c';exe=Path(d)/'test';c.write_text(unit)
 subprocess.run(['cc',f'-DTEST_COVER_RETRIES={int(retry_covers)}','-Dshelf_display_style=app_settings_shelf_style','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'main/book'),str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

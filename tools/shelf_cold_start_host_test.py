#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute visible-only metadata, bounded idle warming and foreground queue ownership."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1];source=R/'main/apps/app_book.c'
for name in ('scan_shelf','shelf_backfill_read_books','refresh_cached_progress'):
    assert 'book_cached_chapter_count(' not in function(name,source),name
u=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "book_cover_jobs.h"
#define SHELF_FAST_COVER_STRIDE 21
enum {SHELF,READING};
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool touched;} touch_t;
typedef struct {int leaf;int64_t now_ms;bool consumed,request_app,request_menu,request_return;touch_t *touch;} app_ctx_t;
typedef struct {char path[288],name[256],author[128];uint32_t size;uint16_t chapters;bool chapters_checked,removed;} shelf_entry_t;
static shelf_entry_t s_shelf[25];static int s_visible_count=25,s_view=SHELF;
static unsigned s_cover_pending_mask,s_chapters_pending,s_cover_generation;
static bool s_chapters_started,s_chapters_changed,s_prefetch_active,s_clear_confirm,s_batch_confirm;
static int s_prefetch_step,s_prefetch_index;static int64_t s_prefetch_after;
static int style=5,submitted,begins,canceled;static bool idle=true,fail_submit;
static unsigned generation;static bool complete[9],queued[9];static book_cover_job_t requests[9];
static int app_settings_shelf_style(void){return style;}
static int shelf_rows(void){return style==5?8:9;}
static bool app_settings_main_fast_refresh(void){return false;}
static unsigned app_settings_system_contrast(void){return 100;}
static bool pico_boot_asset_allowed(const char*p){return p&&*p;}
static void copy_text(char*d,size_t n,const char*s){snprintf(d,n,"%s",s?s:"");}
static EpdRect shelf_cover_image(int row){return(EpdRect){48,197+row*96,51,70};}
unsigned book_cover_jobs_begin(void){++begins;memset(queued,0,sizeof(queued));memset(complete,0,sizeof(complete));return ++generation;}
bool book_cover_jobs_submit(unsigned g,unsigned slot,const book_cover_job_t*q){assert(g==generation&&slot<9);if(fail_submit)return false;assert(!queued[slot]);queued[slot]=true;requests[slot]=*q;++submitted;return true;}
int book_cover_jobs_take(unsigned g,unsigned slot,book_cover_result_t*r){if(g!=generation||!queued[slot])return -1;if(!complete[slot])return 0;queued[slot]=false;*r=(book_cover_result_t){.metadata=true,.chapters=slot==2?0:37,.size=100};return 1;}
void book_cover_jobs_cancel(unsigned g){if(g==generation){++canceled;++generation;}}
bool book_cover_jobs_idle(void){return idle;}
"""
for name in ('shelf_chapters_tick','shelf_prefetch_tick'):
    u+=function(name,source)+'\n'
u+='static unsigned s_home_shelf_generation;static bool s_home_shelf_pending;\n'
for name in ('app_book_home_shelf_warm','app_book_home_shelf_warm_stop'):
    u+=function(name,source)+'\n'
u+=r"""
int main(void){
 for(int i=0;i<25;++i){snprintf(s_shelf[i].path,sizeof(s_shelf[i].path),"book%d",i);s_shelf[i].size=100;}
 app_ctx_t ctx={0};s_cover_pending_mask=1;assert(!shelf_chapters_tick(&ctx)&&!submitted);s_cover_pending_mask=0;
 assert(!shelf_chapters_tick(&ctx)&&submitted==8&&s_chapters_pending==255);
 for(int i=0;i<8;++i)assert(requests[i].metadata_only&&!requests[i].with_chapters&&!requests[i].width);
 complete[0]=true;assert(!shelf_chapters_tick(&ctx)&&s_shelf[0].chapters==37);
 for(int i=1;i<8;++i)complete[i]=true;
 assert(shelf_chapters_tick(&ctx)&&!s_chapters_pending);assert(!s_shelf[2].chapters&&s_shelf[2].chapters_checked);
 assert(!shelf_chapters_tick(&ctx)&&submitted==8);for(int i=8;i<25;++i)assert(!s_shelf[i].chapters_checked);
 // Page-cache hits still reach this path; next page first, one active job, input prevents new work.
 shelf_prefetch_tick(&ctx);assert(!s_prefetch_active);ctx.now_ms=799;shelf_prefetch_tick(&ctx);assert(!s_prefetch_active);
 ctx.now_ms=800;ctx.consumed=true;shelf_prefetch_tick(&ctx);assert(!s_prefetch_active);ctx.consumed=false;
 shelf_prefetch_tick(&ctx);assert(s_prefetch_active&&submitted==9&&!strcmp(requests[0].path,"book8")&&requests[0].width==51&&requests[0].height==70&&requests[0].with_chapters);
 shelf_prefetch_tick(&ctx);assert(submitted==9);complete[0]=true;shelf_prefetch_tick(&ctx);assert(!s_prefetch_active&&s_shelf[8].chapters_checked);
 for(int step=0;step<40;++step){ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);complete[0]=true;shelf_prefetch_tick(&ctx);}
 assert(s_prefetch_step==16&&submitted==16);for(int i=16;i<25;++i)assert(!s_shelf[i].chapters_checked);
 // Metadata submit failure is marked checked once and never loops through the full library.
 s_chapters_started=false;for(int i=0;i<8;++i)s_shelf[i].chapters_checked=false;fail_submit=true;
 assert(!shelf_chapters_tick(&ctx)&&!s_chapters_pending);assert(!shelf_chapters_tick(&ctx));fail_submit=false;
 // A background Home job cannot take ownership while foreground results are outstanding.
 idle=false;int old_begins=begins;assert(!app_book_home_shelf_warm("recent","title","author")&&begins==old_begins);
 idle=true;assert(!app_book_home_shelf_warm("recent","title","author")&&requests[0].list&&requests[0].with_chapters);
 assert(!app_book_home_shelf_warm("recent","title","author"));complete[0]=true;assert(app_book_home_shelf_warm("recent","title","author"));
 assert(!app_book_home_shelf_warm("recent","title","author"));app_book_home_shelf_warm_stop();assert(canceled==1);
 assert(!app_book_home_shelf_warm("recent","title","author"));book_cover_jobs_begin();app_book_home_shelf_warm_stop();assert(canceled==1);
 puts("PASS: zero library-wide chapter reads; visible8 asynchronous count batch, unknown/failure bounds, input priority, next/previous-page-only warming and Home queue ownership");
}
"""
with tempfile.TemporaryDirectory(prefix='kiiko-cold-start-') as d:
    p=Path(d);(p/'test.c').write_text(u)
    subprocess.run(['cc','-Dshelf_display_style=app_settings_shelf_style','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'main/book'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

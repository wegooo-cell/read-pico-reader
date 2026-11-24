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
typedef struct {char path[288],name[256],author[128];uint32_t size;int64_t modified;uint16_t chapters;bool chapters_checked,removed,is_dir,cover_identity_known;} shelf_entry_t;
static shelf_entry_t s_shelf[64];static int s_visible_count=25,s_view=SHELF;
static unsigned s_cover_pending_mask,s_chapters_pending,s_cover_generation;
static bool s_chapters_started,s_chapters_changed,s_prefetch_active,s_clear_confirm,s_batch_confirm;
static bool s_shelf_page_pending __attribute__((unused));
static bool s_category_menu __attribute__((unused)), s_sort_menu __attribute__((unused));
static bool pico_online_busy(void){return false;}
static bool shelf_is_txt(const char *p){return strstr(p,".txt")!=NULL;}
static bool ram_hit[64];
static bool shelf_cover_ram_hit(int index,int row,bool load){(void)row;assert(!load);return ram_hit[index];}
static uint32_t s_shelf_catalog_epoch;static uint32_t progress_revision;static int s_category_selected;static char s_query[8];
static uint32_t book_progress_revision(void){return progress_revision;}
static unsigned app_settings_shelf_order(void){return 0;}
static int epd_get_rotation(void){return 0;}
static uint64_t book_auto_cover_font_key(void){return 0;}
static bool sd_ready=true;static bool read_pico_sd_io_ready(void){return sd_ready;}
static void shelf_record_cover_result(int index,int state,const book_cover_result_t *result){
 if(state>0&&result->metadata){s_shelf[index].chapters=result->chapters;s_shelf[index].chapters_checked=true;}
}
static unsigned s_store_revision;
static unsigned book_store_revision(void){return s_store_revision;}
static int s_prefetch_step,s_prefetch_index;static int64_t s_prefetch_after;
static int style=5,submitted,begins;static bool idle=true,fail_submit;
static unsigned generation;static bool complete[9],queued[9];static book_cover_job_t requests[9];
static int app_settings_shelf_style(void){return style;}
static int shelf_rows(void){return style==5?8:9;}
static bool app_settings_main_fast_refresh(void){return false;}
static unsigned app_settings_system_contrast(void){return 100;}
static bool covers_off;
static bool app_settings_shelf_covers(void){return !covers_off;}
static bool pico_boot_asset_allowed(const char*p){return p&&*p;}
static void copy_text(char*d,size_t n,const char*s){snprintf(d,n,"%s",s?s:"");}
static EpdRect shelf_cover_image(int row){return(EpdRect){48,197+row*96,51,70};}
unsigned book_cover_jobs_begin(void){++begins;memset(queued,0,sizeof(queued));memset(complete,0,sizeof(complete));return ++generation;}
bool book_cover_jobs_submit(unsigned g,unsigned slot,const book_cover_job_t*q){assert(g==generation&&slot<9);if(fail_submit)return false;assert(!queued[slot]);queued[slot]=true;requests[slot]=*q;++submitted;return true;}
int book_cover_jobs_take(unsigned g,unsigned slot,book_cover_result_t*r){if(g!=generation||!queued[slot])return -1;if(!complete[slot])return 0;queued[slot]=false;*r=(book_cover_result_t){.metadata=true,.chapters=slot==2?0:37,.size=100,.modified=777};return 1;}
int book_cover_jobs_take_ready(unsigned g,unsigned slot,book_cover_result_t*r){return book_cover_jobs_take(g,slot,r);}
void book_cover_jobs_cancel(unsigned g){if(g==generation)++generation;}
bool book_cover_jobs_idle(void){return idle;}
static int sweeps;void book_cover_jobs_sweep_legacy(void){++sweeps;}
"""
# The shared source has conditional implementations; select the S31 path.
u += '#define KIIKO_BOARD_METALIO47 1\n'
text=source.read_text()
# 近页窗口随板型不同（S31提前四页、Pico三页），全书库预热从窗口之后开始。
# The nearby window differs per board (S31 four pages ahead, Pico three); library warming starts after it.
u += '#define WARM_FIRST %d\n' % (40 if 'shelf_library_warm_submit(ctx->leaf * rows, rows * 5' in text else 32)
state=text.index('static struct {\n    uint64_t key;\n    int next, index, walked;')
u += text[state:text.index('} s_library_warm;',state)+len('} s_library_warm;')] + '\n'
u += 'static int shelf_library_warm_take(unsigned generation);\n'
for name in ('shelf_covers_hidden','shelf_library_warm_abort','shelf_chapters_tick', 'shelf_record_prefetch_result', 'shelf_library_warm_key',
             'shelf_library_warm_submit', 'shelf_library_warm_take', 'shelf_prefetch_tick'):
    u += function(name, source) + '\n'
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
 shelf_prefetch_tick(&ctx);assert(!s_prefetch_active);ctx.now_ms=299;shelf_prefetch_tick(&ctx);assert(!s_prefetch_active);
 ctx.now_ms=300;ctx.consumed=true;shelf_prefetch_tick(&ctx);assert(!s_prefetch_active);ctx.consumed=false;
 shelf_prefetch_tick(&ctx);assert(s_prefetch_active&&submitted==9&&!strcmp(requests[0].path,"book8")&&requests[0].width==51&&requests[0].height==70&&requests[0].with_chapters);
 shelf_prefetch_tick(&ctx);assert(submitted==9);complete[0]=true;shelf_prefetch_tick(&ctx);assert(!s_prefetch_active&&s_shelf[8].chapters_checked);
 for(int step=0;step<40;++step){ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);complete[0]=true;shelf_prefetch_tick(&ctx);}
 assert(s_prefetch_step==17&&submitted==25&&s_shelf[24].chapters_checked);
 // Metadata submit failure is marked checked once and never loops through the full library.
 s_chapters_started=false;for(int i=0;i<8;++i)s_shelf[i].chapters_checked=false;fail_submit=true;
 assert(!shelf_chapters_tick(&ctx)&&!s_chapters_pending);assert(!shelf_chapters_tick(&ctx));fail_submit=false;
 // 近页预热之后：逐本为整个书库只写卡（跳过当前页及相邻四页与TXT），按格位取尺寸。
 // After nearby warming: card-only warming walks the rest of the library (skipping the visible page,
 // the four pages ahead and TXT), sized by each book's cell.
 for(int i=25;i<64;++i){snprintf(s_shelf[i].path,sizeof(s_shelf[i].path),i==45?"book%d.txt":"book%d",i);s_shelf[i].size=100;}
 // 预热未完成前不迁移旧封面目录。/ No old cover folder migration before the pass completes.
 sweeps=0;
 s_visible_count=64;ram_hit[41]=true;s_prefetch_active=false;int warmed[64]={0},order[64],count=0;
 for(int step=0;step<80;++step){
  ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);
  if(s_prefetch_active&&s_library_warm.active){
   assert(!sweeps);
   const book_cover_job_t *q=&requests[0];int index=atoi(q->path+4);
   assert(q->disk_only&&q->artwork_only&&!q->with_chapters&&!q->metadata_only&&index>=WARM_FIRST&&index!=41&&index!=45);
   assert(q->width==51&&q->height==70);++warmed[index];order[count++]=index;
  }
  complete[0]=true;shelf_prefetch_tick(&ctx);
 }
 int expected=0;for(int i=WARM_FIRST;i<64;++i)if(i!=41&&i!=45){assert(order[expected]==i);++expected;}
 assert(count==expected);for(int i=WARM_FIRST;i<64;++i)assert(warmed[i]==(i!=41&&i!=45));
 assert(s_shelf[40].cover_identity_known&&s_shelf[40].modified==777&&!s_shelf[41].cover_identity_known);
 // 一轮完成后不再重复；书目变化（键变化）后重来一轮。/ A finished pass stays idle until the catalog key changes.
 int before=submitted;for(int step=0;step<10;++step){ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);}assert(submitted==before&&sweeps>0);
 // 进行中被翻页打断：回退游标，下一轮重新提交同一本。/ An interrupted book rewinds the cursor and is submitted again.
 char first[16];snprintf(first,sizeof(first),"book%d",WARM_FIRST);
 ++s_store_revision;ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);assert(s_prefetch_active&&!strcmp(requests[0].path,first));
 s_prefetch_active=false;shelf_library_warm_abort();assert(!s_library_warm.active&&s_library_warm.next==WARM_FIRST);
 ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);assert(s_prefetch_active&&!strcmp(requests[0].path,first));
 // TF 卡挂载交接中：保留游标稍后再试。/ During a mount handoff keep the cursor and retry later.
 int media=WARM_FIRST+1;while(media==41||media==45)++media;char media_path[32];snprintf(media_path,sizeof(media_path),"/sdcard/book%d",media);
 complete[0]=true;shelf_prefetch_tick(&ctx);strcpy(s_shelf[media].path,media_path);sd_ready=false;before=submitted;
 ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);assert(!s_prefetch_active&&submitted==before&&s_library_warm.next==media);
 sd_ready=true;ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);assert(s_prefetch_active&&!strcmp(requests[0].path,media_path));
 // 窗口在书库中段：从窗口之后开始，到末尾后绕回开头，跳过窗口本身。
 // A mid-library window: start just past it, wrap to the start, and skip the window itself.
 complete[0]=true;shelf_prefetch_tick(&ctx);s_prefetch_active=false;shelf_library_warm_abort();
 ++s_store_revision;{unsigned g=0;int seen[64],n=0;
  while(shelf_library_warm_submit(8,48,&g)){seen[n++]=atoi(requests[0].path+(requests[0].path[0]=='/'?12:4));s_library_warm.active=false;}
  int want[64],m=0;for(int i=56;i<64;++i)want[m++]=i;for(int i=0;i<8;++i)want[m++]=i;
  assert(n==m);for(int i=0;i<n;++i)assert(seen[i]==want[i]);
  assert(!shelf_library_warm_submit(8,48,&g));}
 // 关闭封面：不再预热相邻页或整个书库。/ Covers off: no neighbour or whole-library warming.
 complete[0]=true;shelf_prefetch_tick(&ctx);s_prefetch_active=false;shelf_library_warm_abort();
 covers_off=true;++s_store_revision;before=submitted;unsigned warm_generation=0;
 assert(!shelf_library_warm_submit(0,8,&warm_generation)&&!s_library_warm.active);
 for(int step=0;step<10;++step){ctx.now_ms+=1000;shelf_prefetch_tick(&ctx);}
 assert(submitted==before&&!s_prefetch_active);covers_off=false;
 puts("PASS: switched-off shelf covers skip neighbour and whole-library warming");
 puts("PASS: visible metadata first, sequential forward shelf warming with input priority, then one card-only pass over the remaining library with resume, rewind and media-handoff retry");
}
"""
with tempfile.TemporaryDirectory(prefix='kiiko-cold-start-') as d:
    p=Path(d)
    (p/'test.c').write_text(u)
    subprocess.run(['cc','-Dshelf_display_style=app_settings_shelf_style','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'main/book'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：阅读百分比、退出保存和首页实际读取一致，读错不回零。
English: Exercise the real reader percentage/save and Home reload across forward/backward progress and read failures.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
reader = ROOT/'boards/pico/src/main/apps/app_book.c'
home = ROOT/'boards/pico/src/main/apps/app_dashboard.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "book_policy.h"
typedef int esp_err_t;
enum {ESP_OK,ESP_ERR_NVS_NOT_FOUND,ESP_FAIL};
typedef struct {uint32_t file_size;uint16_t chapter;uint32_t byte_off;uint8_t px,pct;uint32_t last_open_s;} book_progress_t;
typedef struct pending_progress {char path[288];book_progress_t value;bool dirty,progress_saved;struct pending_progress *next;}pending_progress_t;
static pending_progress_t pending,*s_pending=&pending;
static const char *s_text="body",*s_path="/book.txt";
static size_t s_page,s_chapter,s_text_len=1000,s_part_base,s_part_begin,s_part_end;
static size_t s_jump_offset=SIZE_MAX,s_jump_page;
static bool s_part_more,s_save_failed,s_chapter_display_pending;
static int s_px=48;
static unsigned s_unsaved;
static bool s_save_due;static uint8_t s_save_due_turns;
static uint32_t s_file_size=1000;
static book_progress_t stored;
static int load_error,stat_error;
static unsigned loads;
static size_t book_layout_page_start_offset(size_t page){return page*10;}
static size_t book_layout_page_count(void){return 100;}
static size_t book_chapter_count(void){return 1;}
static uint32_t book_total_bytes(void){return 1000;}
static uint32_t book_chapter_byte_offset(size_t chapter){(void)chapter;return 0;}
static void reader_current_chapter(char *out,size_t cap){snprintf(out,cap,"Chapter");}
static void book_ticket_note_position(const char *path,const book_progress_t *p,const char *chapter,bool saved){(void)path;(void)chapter;assert(saved&&p->pct==stored.pct);}
static bool pending_flush(pending_progress_t *p){stored=p->value;p->dirty=false;p->progress_saved=true;return true;}
static void invalidate_prep(void){}
typedef struct {char path[288];bool has_progress;book_progress_t progress;} home_book_t;
static home_book_t s_current,s_recent[6];
static home_book_t *book_at(int index){return index?&s_recent[index-1]:&s_current;}
static int test_stat(const char *path,struct stat *out){assert(!strcmp(path,s_path));if(stat_error){errno=stat_error;return -1;}memset(out,0,sizeof(*out));out->st_mode=S_IFREG;out->st_size=1000;return 0;}
#define stat(path,out) test_stat(path,out)
static esp_err_t book_progress_load_checked(const char *path,uint32_t size,book_progress_t *out){assert(!strcmp(path,s_path)&&size==1000);++loads;if(load_error)return load_error;*out=stored;return ESP_OK;}
'''
for name in ['chapter_end','reader_page_offset','reader_saved_offset','percent','pending_find','save_progress','reader_footer_progress']:
    unit += function(name,reader)+'\n'
unit += function('refresh_progress',home)+'\n'
unit += r'''
int main(void){
 snprintf(pending.path,sizeof(pending.path),"%s",s_path);snprintf(s_current.path,sizeof(s_current.path),"%s",s_path);
 const unsigned pages[]={42,82,17,99,0};
 for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);++i){
  s_save_due=true;s_save_due_turns=3;
  s_page=pages[i];save_progress();assert(!s_save_failed&&!pending.dirty&&stored.pct==percent(s_page));
  assert(!s_save_due&&!s_save_due_turns);
  s_current.has_progress=true;s_current.progress.pct=91;assert(refresh_progress()&&s_current.has_progress);
  assert(s_current.progress.pct==percent(s_page));
  char footer[80],expected[80];reader_footer_progress(footer,sizeof(footer),s_page);
  snprintf(expected,sizeof(expected),"%u/100  %u%%",pages[i]+1,s_current.progress.pct);assert(!strcmp(footer,expected));
 }
 stored.pct=82;assert(refresh_progress()&&s_current.progress.pct==82);
 load_error=ESP_FAIL;assert(!refresh_progress()&&s_current.has_progress&&s_current.progress.pct==82);
 stored.pct=83;load_error=0;assert(refresh_progress()&&s_current.progress.pct==83);
 load_error=ESP_ERR_NVS_NOT_FOUND;assert(refresh_progress()&&!s_current.has_progress&&!s_current.progress.pct);
 load_error=0;assert(refresh_progress());stat_error=EIO;
 assert(!refresh_progress()&&s_current.has_progress&&s_current.progress.pct==83);
 stat_error=ENOENT;assert(refresh_progress()&&!s_current.path[0]&&!s_current.has_progress);
 assert(loads>=10);
 puts("PASS: actual reader footer percentage -> saved checkpoint -> Home value, forward/backward/last/first page, unchanged revision reload, transient read/stat preservation, retry and true deletion");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-home-progress-') as directory:
    temp=Path(directory);(temp/'test.c').write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(ROOT/'main/book'),str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)

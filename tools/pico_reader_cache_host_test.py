#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：执行PR115/116真实缓存助手，覆盖内存压力及排版变化。
English: Execute the production PR115/116 cache helpers under memory pressure and layout changes."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
source=root/'boards/pico/src/main/apps/app_book.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BOOK_PREP_RESERVE (1024*1024)
typedef struct{uint8_t*fb;int page;uint32_t generation;}reader_keep_t;
static reader_keep_t s_keep_cur={NULL,-1,0},s_keep_prev={NULL,-1,0};
static int s_next_page=-1,s_prep_page=-1;
static uint32_t s_next_generation;
static struct{int generation;}s_prep_cursor;
static uint8_t*s_next_fb;
static unsigned generation=1,allocations,fail_allocation;
static size_t free_heap=4*1024*1024;
static bool next_ok=true;
static size_t fb_bytes(void){return 684*1216/2;}
static size_t heap_caps_get_free_size(int caps){(void)caps;return free_heap;}
static void*heap_caps_aligned_alloc(size_t a,size_t n,int caps){(void)a;(void)caps;if(++allocations==fail_allocation)return NULL;return malloc(n);}
static bool reader_prefetch_buffer(void){if(!next_ok)return false;if(!s_next_fb)s_next_fb=malloc(fb_bytes());return s_next_fb!=NULL;}
static uint32_t book_layout_generation(void){return generation;}
static size_t s_page=3,page_count=10;
static unsigned s_back_turns;
static int image_page=-1;
static size_t book_layout_page_count(void){return page_count;}
static unsigned book_layout_page_image_count(size_t p){return (int)p==image_page;}
static const char*background="";
static const char*app_settings_book_background_path(void){return background;}
'''
for name in ['reader_keep_free','reset_prefetch','invalidate_prep','reader_keep_buffers','reader_keep_holds','reader_prefetch_target']:
    unit+=function(name,source)+'\n'
unit+=r'''
int main(void){
 assert(reader_keep_buffers()&&s_keep_cur.fb&&s_keep_prev.fb&&s_next_fb);
 s_keep_cur.page=3;s_keep_cur.generation=generation;assert(reader_keep_holds(&s_keep_cur,3));
 assert(!reader_keep_holds(&s_keep_cur,4));++generation;assert(!reader_keep_holds(&s_keep_cur,3));
 s_keep_cur.page=2;s_keep_prev.page=1;s_next_page=4;s_prep_page=5;s_next_generation=generation;
 invalidate_prep();assert(s_keep_cur.page==-1&&s_keep_prev.page==-1&&s_next_page==-1&&s_prep_page==-1&&!s_next_generation);
 reader_keep_free();reader_keep_free();assert(!s_keep_cur.fb&&!s_keep_prev.fb);
 free_heap=BOOK_PREP_RESERVE+fb_bytes();assert(!reader_keep_buffers()&&!s_keep_cur.fb&&!s_keep_prev.fb);
 free_heap=4*1024*1024;next_ok=false;assert(!reader_keep_buffers());next_ok=true;
 for(unsigned failure=1;failure<=2;++failure){fail_allocation=allocations+failure;assert(!reader_keep_buffers()&&!s_keep_cur.fb&&!s_keep_prev.fb);}
 fail_allocation=0;assert(reader_keep_buffers());
 image_page=3;assert(reader_prefetch_target()==4);image_page=4;assert(reader_prefetch_target()==-1);image_page=-1;
 s_back_turns=1;assert(reader_prefetch_target()==2);background="paper";assert(reader_prefetch_target()==-1);background="";
 s_page=0;s_back_turns=2;assert(reader_prefetch_target()==1);s_page=9;s_back_turns=0;assert(reader_prefetch_target()==-1);
 reader_keep_free();free(s_next_fb);
 puts("PASS: real retained-page/image-to-text/backward helpers, allocation failures, decode reserve, layout-generation invalidation and book boundaries");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-second-batch-cache-') as directory:
    p=Path(directory);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

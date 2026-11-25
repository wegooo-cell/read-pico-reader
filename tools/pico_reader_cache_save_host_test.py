#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：执行实际缓存/预绘/空闲保存助手，覆盖内存不足和失效边界。/ Exercise real cache, prefetch and idle-save helpers under memory pressure and invalidation."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
source = root / 'boards/pico/src/main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BOOK_PREP_RESERVE (1024*1024)
#define BOOK_SAVE_IDLE_MS 1200
enum {READING=1};
typedef struct {bool touched;} Touch;
typedef struct {bool consumed,request_app,request_menu,request_return;Touch *touch;int64_t now_ms;} app_ctx_t;
typedef struct {uint8_t *fb;int page;uint32_t generation;} reader_keep_t;
static reader_keep_t s_keep_cur={NULL,-1,0},s_keep_prev={NULL,-1,0};
static int s_next_page=-1,s_prep_page=-1;
static uint32_t s_next_generation;
static struct {int generation;} s_prep_cursor;
static uint8_t *s_next_fb;
static unsigned generation=1,allocations,fail_allocation;
static size_t free_heap=4*1024*1024;
static bool next_ok=true;
static size_t fb_bytes(void){return 684*1216/2;}
static size_t heap_caps_get_free_size(int caps){(void)caps;return free_heap;}
static void *heap_caps_aligned_alloc(size_t a,size_t n,int caps){(void)a;(void)caps;if(++allocations==fail_allocation)return NULL;return malloc(n);}
static bool reader_prefetch_buffer(void){if(!next_ok)return false;if(!s_next_fb)s_next_fb=malloc(fb_bytes());return s_next_fb!=NULL;}
static uint32_t book_layout_generation(void){return generation;}
static size_t s_page=3,page_count=10;
static unsigned s_back_turns;
static int image_page=-1;
static size_t book_layout_page_count(void){return page_count;}
static unsigned book_layout_page_image_count(size_t p){return (int)p==image_page;}
static const char *background="";
static const char *app_settings_book_background_path(void){return background;}
static bool s_save_due,s_save_failed,s_toolbar,s_clear_confirm;
static int s_view=READING;
static const char *s_text="book";
static int64_t s_last_turn_ms=100;
static unsigned lock_depth,saves;
static bool save_error;
static void lock_draw(void){assert(!lock_depth);++lock_depth;}
static void unlock_draw(void){assert(lock_depth==1);--lock_depth;}
static void save_progress(void){assert(lock_depth==1);++saves;s_save_failed=save_error;}
'''
for name in ('reader_keep_free', 'reset_prefetch', 'invalidate_prep', 'reader_keep_buffers',
             'reader_keep_holds', 'reader_prefetch_target', 'reader_save_idle'):
    unit += function(name, source) + '\n'
unit += r'''
int main(void){
 assert(reader_keep_buffers()&&s_keep_cur.fb&&s_keep_prev.fb&&s_next_fb);
 s_keep_cur.page=3;s_keep_cur.generation=generation;
 assert(reader_keep_holds(&s_keep_cur,3));++generation;assert(!reader_keep_holds(&s_keep_cur,3));
 s_keep_cur.page=2;s_keep_prev.page=1;s_next_page=4;s_prep_page=5;invalidate_prep();
 assert(s_keep_cur.page==-1&&s_keep_prev.page==-1&&s_next_page==-1&&s_prep_page==-1);
 reader_keep_free();reader_keep_free();assert(!s_keep_cur.fb&&!s_keep_prev.fb);
 free_heap=BOOK_PREP_RESERVE+fb_bytes();assert(!reader_keep_buffers()&&!s_keep_cur.fb&&!s_keep_prev.fb);
 free_heap=4*1024*1024;fail_allocation=allocations+2;
 assert(!reader_keep_buffers()&&!s_keep_cur.fb&&!s_keep_prev.fb);
 fail_allocation=0;assert(reader_keep_buffers());
 image_page=3;assert(reader_prefetch_target()==4); // 当前图页也能预绘文字。/ Image-to-text prefetch.
 image_page=4;assert(reader_prefetch_target()==-1);image_page=-1;
 s_back_turns=1;assert(reader_prefetch_target()==2);
 background="paper";assert(reader_prefetch_target()==-1);background="";
 s_page=0;s_back_turns=2;assert(reader_prefetch_target()==1);
 s_page=9;s_back_turns=0;assert(reader_prefetch_target()==-1);
 app_ctx_t ctx={.now_ms=1299};s_save_due=true;reader_save_idle(&ctx);assert(!saves&&s_save_due);
 ctx.now_ms=1300;Touch t={.touched=true};ctx.touch=&t;reader_save_idle(&ctx);assert(!saves&&s_save_due);
 t.touched=false;ctx.request_return=true;reader_save_idle(&ctx);assert(!saves&&s_save_due);ctx.request_return=false;
 reader_save_idle(&ctx);assert(saves==1&&!s_save_due&&!lock_depth);
 s_save_due=true;save_error=true;reader_save_idle(&ctx);assert(saves==2&&s_save_failed&&!s_save_due);
 reader_save_idle(&ctx);assert(saves==2); // 失败交给原重试器，避免反复写。/ Existing retry loop owns failed writes.
 reader_keep_free();free(s_next_fb);
 puts("PASS: allocation failures and reserve, generation/layout invalidation, image-to-text/backward prefetch, idle deadline, touch/exit guards and failed-save ownership");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-cache-save-') as folder:
    path = Path(folder)
    (path/'test.c').write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)

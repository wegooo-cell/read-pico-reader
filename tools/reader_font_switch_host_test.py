#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实阅读字体切换事务：加载/排版/保存失败，位置、配置、锁及回退。"""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
from metalio_font_job_host_support import FIXTURE,implementation,completion_wrapper
from board_source_view import select_source
root=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TTF_FONT_PATH_MAX 160
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define APP_REDRAW_NONE 0
#define APP_REDRAW_PAGE 1
#define SHELF 0
#define READING 1
typedef int app_redraw_t;typedef int esp_err_t;typedef struct {int unused;} app_ctx_t;
typedef struct {char path[160];} ttf_font_item_t;
static char active[160],saved[160],s_font_path[160],s_reader_notice[64],s_message[160];
static const char *s_text="text";static size_t s_text_len=4,s_block_count,s_page=7;
static int s_blocks,s_px=40,s_view=READING;static int64_t s_reader_notice_until;
static bool locked,invalidated,load_fails,layout_fails,persist_fails,restore_fails,freed;
static unsigned layout_calls,progress_saves,setting_writes,opens;
static bool s_font_choice_pending;static size_t expected_offset=1234;
static void copy_text(char *to,size_t n,const char *from){snprintf(to,n,"%s",from);}
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static void invalidate_prep(void){assert(locked);invalidated=true;}
static const char *ttf_font_path(void){return active;}
static bool ttf_font_ready(void){return true;}
static const char *app_settings_font_path(void);
static int ttf_font_open(const char *path){assert(locked&&invalidated);if(!strcmp(path,"new.ttf")&&load_fails)return -1;
 if(!strcmp(path,"old.ttf")&&restore_fails){strcpy(active,"builtin");return -1;}strcpy(active,path);return ESP_OK;}
static size_t book_layout_page_start_offset(size_t p){assert(locked&&p==7);return expected_offset;}
static int body_rect(void){return 1;}
static bool book_layout_build_blocks(const char *t,size_t n,int b,size_t c,int r,int px){
 (void)t;(void)n;(void)b;(void)c;(void)r;(void)px;assert(locked);++layout_calls;
 return !(!strcmp(active,"new.ttf")&&layout_fails)&&!restore_fails;}
static size_t book_layout_page_for_offset(size_t off){assert(locked&&off==expected_offset);return 3;}
static void app_settings_set_font_path(const char *path){assert(locked);++setting_writes;if(!persist_fails)strcpy(saved,path);}
static const char *app_settings_font_path(void){return saved;}
static void reader_font_choice_begin(const char *p){(void)p;}
static void reader_font_choice_finish(void){}
static bool ttf_font_path_is_builtin(const char *p){return !p||!*p||!strcmp(p,"builtin");}
static void save_progress(void){assert(!locked);++progress_saves;}
static void free_book(void){assert(locked);freed=true;s_text=NULL;}
static int64_t esp_timer_get_time(void){return 1000000;}
'''
unit+='#define FONT_STAGE_PUBLISHED() ((void)0)\n'+FIXTURE
source=root/'main/apps/app_book.c'
selected=select_source(source.read_text(),'metalio47')
with tempfile.TemporaryDirectory() as temp:
 f=Path(temp)/'book.c';f.write_text(selected);unit+=implementation(f)
unit+=completion_wrapper()
unit+=r'''
static void reset(void){strcpy(active,"old.ttf");strcpy(saved,"old.ttf");strcpy(s_font_path,"old.ttf");
 s_reader_notice[0]=s_message[0]=0;s_view=READING;s_page=7;s_text="text";
 locked=invalidated=load_fails=layout_fails=persist_fails=restore_fails=freed=false;
 layout_calls=progress_saves=setting_writes=0;}
int main(void){app_ctx_t ctx={0};ttf_font_item_t item={"new.ttf"};
 reset();create_fails=true;assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_NONE&&!s_font_job&&!strcmp(active,"old.ttf"));create_fails=false;
 reset();assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE&&s_font_job&&!invalidated&&!strcmp(active,"old.ttf"));
 assert(reader_font_reload_tick(&ctx)==APP_REDRAW_NONE);assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_NONE);
 ttf_font_item_t latest={"latest.ttf"};assert(select_reading_font_item(&ctx,&latest)==APP_REDRAW_PAGE&&s_font_job&&!strcmp(active,"old.ttf"));
 run_worker();reader_font_reload_cancel();assert(!s_font_job&&!strcmp(active,"old.ttf")&&!strcmp(saved,"old.ttf")&&!progress_saves);
 reset();assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);s_view=SHELF;run_worker();
 assert(reader_font_reload_tick(&ctx)==APP_REDRAW_NONE&&!s_font_job&&!strcmp(active,"old.ttf")&&!progress_saves);
 reset();assert(select_reading_font_item(&ctx,NULL)==APP_REDRAW_NONE&&!locked&&!invalidated);
 reset();ttf_font_item_t same={"old.ttf"};assert(select_reading_font_item(&ctx,&same)==APP_REDRAW_NONE&&!invalidated);
 reset();assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);
 assert(!strcmp(saved,"new.ttf")&&!strcmp(active,saved)&&!strcmp(s_font_path,saved)&&s_page==3&&progress_saves==1&&!locked);
 reset();load_fails=true;assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);
 assert(!strcmp(saved,"old.ttf")&&!strcmp(active,saved)&&setting_writes==0&&progress_saves==0&&s_reader_notice[0]&&!locked);
 reset();layout_fails=true;assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);
 assert(!strcmp(saved,"old.ttf")&&!strcmp(active,saved)&&layout_calls==2&&setting_writes==0&&s_page==3&&!locked);
 reset();persist_fails=true;assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);
 assert(!strcmp(saved,"old.ttf")&&!strcmp(active,saved)&&layout_calls==2&&setting_writes==1&&!progress_saves&&s_reader_notice[0]&&!locked);
 reset();layout_fails=restore_fails=true;assert(select_reading_font_item(&ctx,&item)==APP_REDRAW_PAGE);
 assert(freed&&s_view==SHELF&&s_message[0]&&!strcmp(saved,"old.ttf")&&!locked);
 puts("PASS: real reader staged font transaction retains the old face during loading, holds draw lock only on publication, invalidates prepared pages, preserves offset, commits only successful selection and safely handles load/layout/NVS/rollback failures");}
'''
(root/'build').mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=root/'build') as folder:
 p=Path(folder);split=unit.index(' reset();assert(select_reading_font_item(&ctx,NULL)');unit=unit[:split]+unit[split:].replace('assert(select_reading_font_item(', 'assert(select_and_complete(');(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute both readers' resume and per-book durability decisions."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
for source in [R/'main/apps/app_book.c',R/'boards/pico/src/main/apps/app_book.c']:
 unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
enum {READING,SHELF,MANAGE,BULK,TOC};
static const char *s_text="text",*s_path="/sdcard/books/book.txt";
static int s_view=READING;static bool s_reader_fullscreen;
typedef struct {bool progress_saved;} pending_progress_t;
static pending_progress_t saved;static bool exists;
static pending_progress_t *pending_find(const char *p){assert(!strcmp(p,s_path));return exists?&saved:NULL;}
static void copy_text(char *out,size_t cap,const char *in){snprintf(out,cap,"%s",in);}
'''
 unit+=function('app_book_resume_context',source)+'\n'+function('app_book_checkpoint_ready',source)
 unit+=r'''
int main(void){char path[288];bool fullscreen=false;
 assert(app_book_resume_context(path,sizeof(path),&fullscreen)&&!strcmp(path,s_path));
 assert(!app_book_checkpoint_ready());exists=true;assert(!app_book_checkpoint_ready());
 saved.progress_saved=true;assert(app_book_checkpoint_ready());
 s_reader_fullscreen=true;s_view=TOC;
 assert(app_book_resume_context(path,sizeof(path),&fullscreen)&&fullscreen);
 s_view=SHELF;assert(!app_book_resume_context(path,sizeof(path),&fullscreen));
 s_text=NULL;assert(app_book_checkpoint_ready());
 puts("PASS: current reader context survives unrelated failures; power-off needs this book's durable position; reader/TOC/fullscreen retained");
}
'''
 with tempfile.TemporaryDirectory(prefix='reader-resume-') as d:
  p=Path(d);(p/'test.c').write_text(unit)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

# Execute both actual main-loop power actions: a failed checkpoint must not power down.
for source in [R/'main/app/app_loop.c',R/'boards/pico/src/main/app/app_loop.c']:
 unit=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define KIIKO_BOARD_METALIO47 0
#define PICO_BOOT_TAB_COUNT 6
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static int64_t esp_timer_get_time(void){return 0;}
#define APP_REDRAW_PAGE 1
#define APP_SLEEP_OFF 2
typedef int esp_err_t;
typedef struct {void *hl;uint8_t *fb;} app_ctx_t;
typedef struct {void (*on_before_lock)(app_ctx_t*);} app_desc_t;
typedef struct {uint8_t tab,reader,fullscreen;char path[288];} pico_resume_t;
static bool position_ok,checkpoint_ok,checkpoint_live,checkpoint_saved,deep_ready,reading=true;
static unsigned before_calls,power_calls,present_calls;
static void before(app_ctx_t *ctx){(void)ctx;++before_calls;}
static const app_desc_t app_book={before},other={before};
static void app_book_shelf_prefetch_cancel(void){}
static void app_lock_feedback(void *hl){(void)hl;}
static int app_index_of(const app_desc_t *page){return page==&app_book?1:5;}
static bool app_book_resume_context(char *path,size_t cap,bool *fullscreen){snprintf(path,cap,"/sdcard/books/test.txt");*fullscreen=true;return reading;}
static bool app_book_checkpoint_ready(void){return position_ok;}
static esp_err_t pico_boot_save_resume(const pico_resume_t *resume){assert(resume->tab<6);checkpoint_live=checkpoint_ok;checkpoint_saved|=checkpoint_ok;return checkpoint_ok?ESP_OK:ESP_FAIL;}
static void app_lock_set_resume_ready(bool ready){deep_ready=ready;}
static void app_present(app_ctx_t *ctx,const app_desc_t *page,int mode){(void)ctx;(void)page;(void)mode;++present_calls;}
static void ui_power_final_draw(uint8_t *fb,bool restart){(void)fb;(void)restart;}
static void guard_draw_result(void *hl,int result){(void)hl;(void)result;}
static int update_display_full(void *hl){(void)hl;return 0;}
static void app_lock_wait_key_idle(unsigned ms){assert(ms==800);}
static void epd_poweroff(void){}
// 电源交接前已保存过检查点、再按PR112清掉恢复记录，冷启动回主页。/ The checkpoint was saved, then cleared before the handoff (PR112) so the next boot is cold.
static void app_restart_host(void){assert(checkpoint_saved&&!checkpoint_live&&deep_ready);}
static esp_err_t app_enter_host_sleep(int mode){assert(mode==APP_SLEEP_OFF&&checkpoint_saved&&!checkpoint_live&&deep_ready);checkpoint_saved=false;++power_calls;return ESP_FAIL;}
static void pico_boot_clear_resume(void){checkpoint_live=false;}
"""
 unit+=function('prepare_lock',source)+'\n'+function('run_power_action',source)
 unit+=r"""
int main(void){app_ctx_t ctx={0};
 position_ok=checkpoint_ok=true;run_power_action(&ctx,&app_book,false);assert(power_calls==1&&before_calls==1&&!checkpoint_live);
 position_ok=false;run_power_action(&ctx,&app_book,false);assert(power_calls==1&&!deep_ready&&before_calls==2);
 position_ok=true;checkpoint_ok=false;run_power_action(&ctx,&app_book,false);assert(power_calls==1&&!deep_ready&&before_calls==3);
 checkpoint_ok=true;position_ok=false;run_power_action(&ctx,&other,false);assert(power_calls==2&&deep_ready&&before_calls==4);
 puts("PASS: both-board power actions flush first, refuse failed current-position/checkpoint writes and clear the resume record before the hardware handoff");
}
"""
 with tempfile.TemporaryDirectory(prefix='reader-power-') as d:
  p=Path(d);(p/'test.c').write_text(unit)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

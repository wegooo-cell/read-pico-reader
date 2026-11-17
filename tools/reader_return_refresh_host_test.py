#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check that both readers refresh the destination once on exit."""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function


ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
typedef struct {int64_t now_ms;} app_ctx_t;
typedef struct app_desc_t {
    bool persistent_navigation, enter_full;
    void (*on_exit)(app_ctx_t*);
    void (*on_enter)(app_ctx_t*);
    const char *title;
} app_desc_t;
enum {READING,SHELF,READER_PANEL_NONE};
static int s_view,s_reader_panel;
static const char *s_text;
static bool s_reader_return_home;
static int progress, tickets, shelf_restore, requested_tab, presented, armed;
static app_redraw_t last;
static void exit_book(app_ctx_t *ctx){(void)ctx;s_text=NULL;s_view=SHELF;}
static const app_desc_t app_book={.persistent_navigation=true,.on_exit=exit_book,.title="Book"};
static const app_desc_t home={.persistent_navigation=true,.title="Home"};
static const app_desc_t library={.persistent_navigation=true,.title="Other root"};
static int app_index_of(const app_desc_t *app){return app==&home?0:app==&app_book?1:2;}
static const app_desc_t *app_at(int index){assert(index==0);return &home;}
static bool main_page_ready(app_ctx_t *ctx,const app_desc_t *app){
    (void)ctx;
    return app!=&app_book||s_view==SHELF;
}
static bool navigation_page_ready(app_ctx_t *ctx,const app_desc_t *app){return main_page_ready(ctx,app);}
static void app_book_shelf_prefetch_cancel(void){}
static void display_main_transition_cancel(void){}
static void display_main_transition_arm(const app_desc_t *app,bool shelf,bool changed){
    (void)app;(void)shelf;assert(changed);++armed;
}
static void display_main_transition_disarm(void){}
static void app_font_activate_system(void){}
static void app_present(app_ctx_t *ctx,const app_desc_t *dest,app_redraw_t mode){
    (void)ctx;assert(dest);last=mode;++presented;
}
static void app_book_ticket_sample(int64_t n,bool exposed){assert(n==100&&!exposed);++tickets;}
static void save_progress(void){++progress;}
static void ui_nav_request(app_ctx_t *ctx,int index){(void)ctx;requested_tab=index;}
static void free_book(void){s_text=NULL;}
static void return_to_cached_shelf(app_ctx_t *ctx){(void)ctx;++shelf_restore;}
#if KIIKO_BOARD_METALIO47
static int64_t esp_timer_get_time(void){return 0;}
static int app_settings_main_refresh_mode(void){return 0;}
#endif
static void mute_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define TAG "return"
#define ESP_LOGI mute_log
'''
CHECKS = r'''
int main(void){
    app_ctx_t ctx={.now_ms=100};
    const app_desc_t *current=&app_book;
    s_view=READING;s_text="book";s_reader_panel=2;s_reader_return_home=true;
    assert(reader_return(&ctx)==APP_REDRAW_NONE&&requested_tab==0&&shelf_restore==0);
    assert(progress==1&&tickets==1&&s_text&&app_book_reading_active());
    switch_to(&ctx,&current,&home);
    assert(current==&home&&presented==1&&last==APP_REDRAW_FULL&&!armed);
    assert(!app_book_reading_active());

    current=&app_book;s_view=READING;s_text="book";s_reader_return_home=false;
    assert(reader_return(&ctx)==APP_REDRAW_FULL&&s_view==SHELF&&shelf_restore==1);
    assert(progress==2&&tickets==2&&s_reader_panel==READER_PANEL_NONE);
    switch_to(&ctx,&current,&home);
    assert(presented==2&&last==APP_REDRAW_PAGE&&armed==1);
    current=&app_book;s_view=READING;s_text="book";
    switch_to(&ctx,&current,&library);
    assert(presented==3&&last==APP_REDRAW_PAGE&&armed==1);
    puts("PASS: reading to Home or the originating shelf selects one full refresh; other root switching stays short");
}
'''

with tempfile.TemporaryDirectory() as temp:
    for s31 in (False, True):
        sources = ROOT / ('main' if s31 else 'boards/pico/src/main')
        code = STUBS + '\n'.join((
            function('app_book_reading_active', sources / 'apps/app_book.c'),
            function('reader_return', sources / 'apps/app_book.c'),
            function('switch_to', sources / 'app/app_loop.c'),
        )) + CHECKS
        c = Path(temp) / 'return.c'
        executable = c.with_suffix('')
        c.write_text(code)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        f'-DKIIKO_BOARD_METALIO47={int(s31)}', '-fsanitize=address,undefined',
                        str(c), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

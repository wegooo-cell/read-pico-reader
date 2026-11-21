#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bottom circle always returns management to the real shelf on both boards."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
for source in [R/'main/apps/app_book.c',R/'boards/pico/src/main/apps/app_book.c']:
 unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
enum {SHELF,BULK,APP_REDRAW_PAGE};typedef int app_redraw_t;typedef struct{int leaf;}app_ctx_t;
static bool s_manage_books,s_import_candidates,s_bulk_selecting,s_sort_menu,s_batch_confirm,s_rescan_prompt;
static int s_count,s_visible_count,s_view,clears,scans,cancels,refreshes,sorts;static char s_query[65],s_batch_message[128];
static int library_cancel_candidates(app_ctx_t*c){(void)c;++cancels;s_import_candidates=false;s_count=s_visible_count=0;return APP_REDRAW_PAGE;}
static void clear_selection(void){++clears;}
static void library_scan_again(app_ctx_t*c){assert(!s_manage_books&&!s_count&&!s_visible_count);++scans;c->leaf=0;}
static void refresh_search_matches(void){assert(!s_query[0]);++refreshes;}
static void sort_shelf(app_ctx_t*c){++sorts;c->leaf=0;}
'''+function('bulk_return_shelf',source)+r'''
int main(void){app_ctx_t c={9};for(int d=0;d<2;++d)for(int i=0;i<2;++i){
 s_manage_books=d;s_import_candidates=i;s_bulk_selecting=s_sort_menu=s_batch_confirm=s_rescan_prompt=true;s_view=BULK;s_count=s_visible_count=30;s_query[0]=s_batch_message[0]='x';
 assert(bulk_return_shelf(&c)==APP_REDRAW_PAGE&&s_view==SHELF&&!s_manage_books&&!s_import_candidates&&!s_bulk_selecting&&!s_sort_menu&&!s_batch_confirm&&!s_rescan_prompt&&!s_query[0]&&!s_batch_message[0]&&!c.leaf);
 if(d||i)assert(!s_count&&!s_visible_count);
 }assert(clears==4&&scans==2&&cancels==2&&sorts==2&&refreshes==2);puts("PASS: circle returns shelf in one action from selections, source folders, modal and import; cancels without enrollment and never paints directory rows as shelf");}
'''
 handler=function('on_key',source);assert handler.index('return bulk_return_shelf(ctx)')<handler.index('s_library_setup_pending')
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'test.c').write_text(unit);subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)

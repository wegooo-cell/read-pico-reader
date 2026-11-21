#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute production management actions and press/release hit regions across pages."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ui_book_manage_layout.h"
#include "book_import.h"
enum {SHELF,BULK,EDIT,SEARCH,BATCH_DELETE,BATCH_UNSHELF,BATCH_CLEAR,APP_REDRAW_NONE,APP_REDRAW_PAGE};
typedef int app_redraw_t;
typedef struct{int id;} app_desc_t;
static const app_desc_t app_files={1};
typedef struct{int leaf;const app_desc_t *request_app;}app_ctx_t;
typedef struct{char path[288];bool selected,shelved,is_dir;}entry_t;
typedef entry_t shelf_entry_t;
static entry_t s_shelf[50],s_managed;
static int s_view=BULK,s_visible_count=44,s_count=50,s_filter,s_category_selected,s_batch_kind;
static bool s_sort_menu,s_scan_pending,s_batch_confirm,s_import_candidates,s_manage_books,s_bulk_grid,s_bulk_selecting,s_rescan_prompt,s_editor_from_bulk,s_manage_files_return;
static char s_batch_message[128],s_query[65],s_manage_dir[288];
static unsigned directory_opens;
static bool books_directory_is_root(void){return true;}
static app_redraw_t books_directory_up(app_ctx_t*ctx){(void)ctx;return APP_REDRAW_PAGE;}
static app_redraw_t books_directory_open(app_ctx_t*ctx,const char*p){(void)ctx;(void)p;++directory_opens;return APP_REDRAW_PAGE;}
static unsigned scans,imports,cancels,editors,searches,styles,invalidations,file_requests,cover_requests;
static bool file_ok=true,file_move;static char requested[288];
static unsigned selected_count(void){if(s_import_candidates)return book_import_choice_count();unsigned n=0;for(int i=0;i<s_count;++i)n+=s_shelf[i].selected;return n;}
static void clear_selection(void){for(int i=0;i<s_count;++i)s_shelf[i].selected=false;}
static void copy_text(char *out,size_t cap,const char *in){snprintf(out,cap,"%s",in);}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static EpdRect bulk_sort_rect(int i){return (EpdRect){60,338+i*92,564,84};}
static int shelf_order_mode(void){return 0;}
static bool shelf_order_desc(void){return false;}
static bool shelf_order_apply(app_ctx_t *ctx,int mode,bool desc){(void)ctx;(void)mode;(void)desc;return true;}
static app_redraw_t bulk_cover_start(app_ctx_t *ctx){(void)ctx;++cover_requests;return APP_REDRAW_PAGE;}
static app_redraw_t batch_action(app_ctx_t *ctx,int x,int y){(void)ctx;(void)x;(void)y;return APP_REDRAW_PAGE;}
static app_redraw_t library_import_selected(app_ctx_t *ctx){(void)ctx;++imports;return APP_REDRAW_PAGE;}
static app_redraw_t library_cancel_candidates(app_ctx_t *ctx){(void)ctx;++cancels;book_import_reset();clear_selection();s_import_candidates=false;return APP_REDRAW_PAGE;}
static unsigned restarts;static app_redraw_t library_restart_import(app_ctx_t *ctx){(void)ctx;++restarts;return APP_REDRAW_PAGE;}
static void library_scan_again(app_ctx_t *ctx){++scans;ctx->leaf=0;s_scan_pending=true;}
static void invalidate_covers(void){++invalidations;}
static int shelf_rows(void){return ui_book_manage_rows(s_bulk_grid);}
static EpdRect row_rect(int row){return s_import_candidates?ui_book_import_entry(s_bulk_grid,row):ui_book_manage_entry(s_bulk_grid,row);}
static EpdRect batch_rect(int i){return ui_book_manage_action(i);}
static int bulk_nav_hit(int x,int y){return y>=1006&&y<1060?(x<342?0:1):-1;}
static int leaves(void){return (s_visible_count+shelf_rows()-1)/shelf_rows();}
static int ui_pager_target(int nav,int page,int count){(void)count;return page+(nav?1:-1);}
static app_redraw_t bulk_turn_page(app_ctx_t *ctx,int d){if(ctx->leaf+d>=0&&ctx->leaf+d<leaves())ctx->leaf+=d;return APP_REDRAW_PAGE;}
static void search_begin(void){++searches;}
static bool app_files_request_book_action(const char *path,bool move){++file_requests;strcpy(requested,path);file_move=move;return file_ok;}
static void editor_start(void){++editors;s_view=EDIT;}
'''
unit+=function('library_restore_import_selection',R/'main/apps/book_library_ui.inc')+'\n'+function('library_page_selection',R/'main/apps/book_library_ui.inc')+'\n'
for name in ['toggle_selection','bulk_select_row','bulk_action']:
    unit+=function(name,R/'main/apps/app_book.c')+'\n'
unit+=r'''
static void click(app_ctx_t *ctx,EpdRect r){assert(r.width&&r.height);assert(bulk_action(ctx,r.x+r.width/2,r.y+r.height/2)==APP_REDRAW_PAGE);}
int main(void){
 app_ctx_t ctx={.leaf=2};for(int i=0;i<50;++i){snprintf(s_shelf[i].path,288,"/sdcard/books/%d.txt",i);s_shelf[i].shelved=i==0||i==20;}
 click(&ctx,ui_book_manage_get_cover());assert(cover_requests==1);
 s_shelf[1].selected=true;
 click(&ctx,ui_book_manage_select());assert(selected_count()==14&&!s_shelf[44].selected&&!s_shelf[25].selected&&s_shelf[26].selected&&s_shelf[38].selected);
 click(&ctx,ui_book_manage_cancel());assert(selected_count()==1&&s_shelf[1].selected);
 click(&ctx,ui_book_manage_layout());assert(s_bulk_grid&&ctx.leaf==2&&selected_count()==1);
 click(&ctx,ui_book_manage_select());assert(selected_count()==13&&s_shelf[24].selected&&s_shelf[35].selected&&!s_shelf[36].selected);
 click(&ctx,ui_book_manage_cancel());assert(selected_count()==1);
 clear_selection();s_import_candidates=true;s_bulk_grid=false;ctx.leaf=3;s_shelf[40].shelved=true;
 assert(book_import_select(s_shelf[2].path,false,true)==ESP_OK);library_restore_import_selection();
 click(&ctx,ui_book_import_select());assert(selected_count()==5&&!s_shelf[40].selected&&s_shelf[39].selected&&s_shelf[43].selected&&!s_shelf[44].selected);
 click(&ctx,ui_book_import_cancel());assert(selected_count()==1&&s_shelf[2].selected);s_shelf[40].shelved=false;
 ctx.leaf=0;click(&ctx,row_rect(0));assert(selected_count()==1);click(&ctx,row_rect(1));assert(selected_count()==2);
 click(&ctx,(EpdRect){250,1124,398,64});assert(imports==1);
 s_scan_pending=true;click(&ctx,(EpdRect){36,1124,196,64});assert(cancels==1);s_scan_pending=false;
 s_view=BULK;clear_selection();s_manage_books=false;ctx.leaf=2;
 click(&ctx,batch_rect(6));assert(restarts==1&&!s_rescan_prompt&&ctx.leaf==2);
 click(&ctx,ui_book_manage_tab(0));assert(scans==1&&s_manage_books&&s_scan_pending&&ctx.leaf==0);
 s_scan_pending=false;assert(bulk_action(&ctx,ui_book_manage_sort().x+10,ui_book_manage_sort().y+10)==APP_REDRAW_NONE&&!s_sort_menu);

 s_scan_pending=false;s_count=50;s_visible_count=44;click(&ctx,row_rect(1));assert(selected_count()==1);
 // Native rename/move is requested for exactly the selected full path.
 assert(bulk_action(&ctx,50,1140)==APP_REDRAW_NONE&&file_requests==1&&!file_move&&s_manage_files_return&&ctx.request_app==&app_files&&!strcmp(requested,s_shelf[1].path));
 s_manage_files_return=false;ctx.request_app=NULL;
 assert(bulk_action(&ctx,400,1140)==APP_REDRAW_NONE&&file_move&&file_requests==2);
 ctx.request_app=NULL;s_manage_files_return=false;file_ok=false;
 assert(bulk_action(&ctx,400,1140)==APP_REDRAW_PAGE&&!ctx.request_app&&s_batch_message[0]);file_ok=true;
 click(&ctx,batch_rect(4));assert(s_batch_confirm&&s_batch_kind==BATCH_DELETE);s_batch_confirm=false;
 click(&ctx,batch_rect(6));assert(imports==2);
 click(&ctx,ui_book_manage_tab(1));assert(!s_manage_books&&scans==2);
 click(&ctx,ui_book_manage_sort());assert(s_sort_menu);s_sort_menu=false;s_scan_pending=false;s_count=50;s_visible_count=44;
 clear_selection();s_shelf[2].selected=true;click(&ctx,batch_rect(3));assert(editors==1&&s_editor_from_bulk&&s_view==EDIT);
 s_view=BULK;click(&ctx,batch_rect(4));assert(s_batch_confirm&&s_batch_kind==BATCH_UNSHELF);s_batch_confirm=false;
 click(&ctx,batch_rect(5));assert(s_batch_confirm&&s_batch_kind==BATCH_CLEAR);s_batch_confirm=false;
 click(&ctx,ui_book_manage_search());assert(searches==1);strcpy(s_query,"夏天");click(&ctx,ui_book_manage_search());assert(searches==2&&s_query[0]&&styles==0);
 s_view=BULK;s_manage_books=true;s_import_candidates=false;s_scan_pending=s_sort_menu=s_batch_confirm=false;s_count=s_visible_count=3;ctx.leaf=0;clear_selection();s_shelf[0].is_dir=true;
 click(&ctx,ui_book_manage_select());assert(selected_count()==2&&!s_shelf[0].selected);
 click(&ctx,row_rect(0));assert(directory_opens==1&&!s_shelf[0].selected);
 assert(bulk_select_row(&ctx,0)==APP_REDRAW_PAGE&&directory_opens==2&&!s_shelf[0].selected);
 s_import_candidates=true;s_manage_books=false;s_shelf[0].shelved=false;book_import_reset();library_restore_import_selection();
 EpdRect folder=row_rect(0);assert(bulk_action(&ctx,48,folder.y+folder.height/2)==APP_REDRAW_PAGE&&book_import_choice_count()==1);
 click(&ctx,row_rect(0));assert(directory_opens==3&&book_import_choice_count()==1);
 assert(bulk_select_row(&ctx,0)==APP_REDRAW_PAGE&&!book_import_choice_count());
 click(&ctx,ui_book_import_layout());assert(s_bulk_grid);click(&ctx,ui_book_import_search());assert(searches==3);
 book_import_reset();
 puts("PASS: production 13-row/12-cover actions; all/select-none across filtered pages, disabled members, view/source changes, native rename/move full paths and failures, shelf-only remove/progress/rescan, import and cancellation");
}
'''
unit+='\nint book_library_begin_snapshot(void){return ESP_OK;}\nint book_library_add(const char*p){(void)p;return ESP_OK;}\nint book_library_commit(void){return ESP_OK;}\nvoid book_library_abort(void){}\n'
with tempfile.TemporaryDirectory(prefix='library-management-') as directory:
    work=Path(directory)
    (work/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\nenum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_SIZE,ESP_ERR_NOT_FOUND,ESP_ERR_NOT_FINISHED};\n')
    (work/'epdiy.h').write_text('#pragma once\ntypedef struct{int x,y,width,height;}EpdRect;\n')
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-D_DEFAULT_SOURCE','-I'+str(work),'-I'+str(R/'main/ui'),'-I'+str(R/'main/book'),str(work/'test.c'),str(R/'main/book/book_import.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)

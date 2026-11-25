#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute shared import UI transactions; walker filesystem behavior has its own real-storage test."""
from pathlib import Path
from home_cover_cache_host_test import function
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "book_import.h"
enum{BOOK_LIBRARY_UNSET,BOOK_LIBRARY_SELECTED,BOOK_LIBRARY_ALL};
enum{SHELF,BULK,APP_REDRAW_NONE,APP_REDRAW_PAGE,UI_GRAY_BLACK,UI_GRAY_WHITE,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_LEFT,UI_ICON_BOOK_OPEN,UI_ICON_X};
typedef int app_redraw_t;typedef struct{int leaf;}app_ctx_t;typedef struct{int x,y,width,height;}EpdRect;
typedef struct{char path[40];bool selected,shelved,is_dir;}shelf_entry_t;
static shelf_entry_t s_shelf[8];static int s_count,s_view,s_filter,s_poll_ms,s_visible_count,s_category_selected,s_rescan_leaf;
static bool s_manage_books,s_rescan_prompt,s_import_all_pending,s_import_return_bulk,s_import_candidates,s_library_setup_pending,s_shelf_cache_valid,s_scan_pending,s_bulk_selecting,s_bulk_grid,s_sort_menu;
static bool s_editor_from_bulk;static char s_requested_open[288];
static char s_query[64],s_shelf_warning[128],s_batch_message[128],s_message[128],s_manage_dir[288];
static int mode=BOOK_LIBRARY_UNSET,error,write_error,invalidations,aborts,commits,begins,sorts,cover_invalidations;
static bool members[8],staged[8],hidden[8],choices[8],running;static unsigned unhide_failed;
static bool (*is_hidden_cb)(const char*),(*unhide_cb)(const char*);
static int book_library_mode(void){return mode;}static int book_library_error(void){return error;}
static bool book_library_contains(const char*p){return mode==BOOK_LIBRARY_ALL||members[p[0]-'0'];}
static void book_library_invalidate(void){++invalidations;}
static int book_library_begin(bool all){++begins;memcpy(staged,members,sizeof(staged));if(all)memset(staged,1,sizeof(staged));return write_error;}
static int book_library_begin_snapshot(void){int r=book_library_begin(false);if(mode==BOOK_LIBRARY_ALL)memset(staged,0,sizeof(staged));return r;}
static int book_library_add(const char*p){staged[p[0]-'0']=true;return write_error;}
static int book_library_remove(const char*p){staged[p[0]-'0']=false;return write_error;}
static int book_library_commit(void){if(write_error)return write_error;memcpy(members,staged,sizeof(members));mode=BOOK_LIBRARY_SELECTED;++commits;return ESP_OK;}
static void book_library_abort(void){++aborts;}
static int book_library_reset(void){if(write_error)return write_error;memset(members,0,sizeof(members));memset(staged,0,sizeof(staged));mode=BOOK_LIBRARY_UNSET;++commits;return ESP_OK;}
static bool shelf_hidden_load(const char*p){return hidden[p[0]-'0'];}
static bool shelf_hidden_save(const char*p,bool value){hidden[p[0]-'0']=value;return true;}
book_import_selection_t book_import_selection(const char*p){return choices[p[0]-'0']?BOOK_IMPORT_EXPLICIT:BOOK_IMPORT_NONE;}
esp_err_t book_import_select(const char*p,bool dir,bool value){(void)dir;choices[p[0]-'0']=value;return ESP_OK;}
unsigned book_import_choice_count(void){unsigned n=0;for(int i=0;i<8;++i)n+=choices[i];return n;}
void book_import_cancel(void){if(running)book_library_abort();running=false;}
void book_import_reset(void){book_import_cancel();memset(choices,0,sizeof(choices));}
bool book_import_running(void){return running;}
esp_err_t book_import_begin(bool(*hidden_fn)(const char*),bool(*unhide_fn)(const char*)){int r=book_library_begin_snapshot();if(r==ESP_OK){running=true;is_hidden_cb=hidden_fn;unhide_cb=unhide_fn;}return r;}
esp_err_t book_import_step(unsigned budget){assert(budget==32&&running);int r=ESP_OK;char path[20];
 for(int i=0;i<8&&r==ESP_OK;++i)if(choices[i]){snprintf(path,sizeof(path),"%d.txt",i);r=book_library_add(path);}
 if(r==ESP_OK)r=book_library_commit();if(r!=ESP_OK)book_library_abort();
 unhide_failed=0;if(r==ESP_OK)for(int i=0;i<8;++i)if(choices[i]){snprintf(path,sizeof(path),"%d.txt",i);if(is_hidden_cb(path)&&!unhide_cb(path))++unhide_failed;}
 running=false;return r;}
unsigned book_import_unhide_failures(void){return unhide_failed;}
static const char *app_settings_books_dir(void){return "0.txt";}
static int shelf_rows(void){return 13;}
static void sort_shelf(app_ctx_t*c){(void)c;s_visible_count=s_count;++sorts;}
static void invalidate_covers(void){++cover_invalidations;}
static unsigned selected_count(void){if(s_import_candidates)return book_import_choice_count();unsigned n=0;for(int i=0;i<s_count;++i)n+=s_shelf[i].selected;return n;}
static void clear_selection(void){for(int i=0;i<s_count;++i)s_shelf[i].selected=false;}
static void copy_text(char*d,size_t n,const char*s){snprintf(d,n,"%s",s);}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void ui_clear_page(uint8_t*f){(void)f;}static void ui_nav_status(uint8_t*f){(void)f;}static void ui_nav_draw(uint8_t*f,int t){(void)f;(void)t;}
static void ui_text_box(uint8_t*f,EpdRect r,int px,const char*t,int a,bool i){(void)f;(void)px;(void)t;(void)a;(void)i;assert(r.x>=0&&r.y>=0&&r.x+r.width<=684&&r.y+r.height<=1096);}
static void ui_draw_control_frame(uint8_t*f,EpdRect r,int radius,int g){(void)f;(void)radius;(void)g;assert(r.y>=0&&r.y+r.height<1096);}
static void draw_bulk(uint8_t*f,int l){(void)f;(void)l;}
static void ui_draw_icon(uint8_t*f,int x,int y,int n,int i,int g){(void)f;(void)x;(void)y;(void)n;(void)i;(void)g;}
static void ui_fill_round_rect(uint8_t*f,EpdRect r,int n,int g){(void)f;(void)r;(void)n;(void)g;}
static void bulk_button(uint8_t*f,EpdRect r,const char*t,bool a,int px){(void)f;(void)r;(void)t;(void)a;(void)px;}
static void ui_text_vc(uint8_t*f,int x,int y,int p,const char*t,int a,bool i){(void)f;(void)x;(void)y;(void)p;(void)t;(void)a;(void)i;}
'''
unit+=(R/'main/apps/book_library_ui.inc').read_text()
for suffix,source in [('s31',R/'main/apps/app_book.c'),('pico',R/'boards/pico/src/main/apps/app_book.c')]:
    enter=function('on_enter',source)
    tail=enter[enter.rindex('    if(s_view==SHELF && !s_requested_open[0]){'):]
    unit+='static void return_'+suffix+'(app_ctx_t *ctx){\n'+tail+'\n'
    leave=function('book_on_exit',source)
    prefix=leave[:leave.index('    s_lock_reader_path[0]')]
    unit+=prefix.replace('book_on_exit','leave_'+suffix,1)+'    (void)ctx;\n}\n'
unit+=r'''
static void populate(void){s_count=s_visible_count=4;for(int i=0;i<4;++i){snprintf(s_shelf[i].path,40,"%d.txt",i);s_shelf[i].selected=false;s_shelf[i].shelved=false;}}
int main(void){app_ctx_t ctx={20};uint8_t fb=0;
 // 切页和关闭都不完成导入；下一次真实on_enter重新显示弹窗。
 // Navigation and dismissal retain enrollment; the actual entry branch restores the prompt.
 for(int board=0;board<2;++board)for(int page=0;page<3;++page){
  s_view=SHELF;s_library_setup_pending=true;library_draw_setup(&fb);
  if(page!=0){assert(library_setup_action(&ctx,601,460)==APP_REDRAW_PAGE&&!s_library_setup_pending);}
  assert(mode==BOOK_LIBRARY_UNSET&&!begins&&!commits);
  if(board)leave_pico(&ctx);else leave_s31(&ctx);
  assert(!s_library_setup_pending&&mode==BOOK_LIBRARY_UNSET);
  if(board)return_pico(&ctx);else return_s31(&ctx);
  assert(s_library_setup_pending&&!s_scan_pending&&!begins&&!commits);
 }
 // 手动选择后不确认就切走，返回也应重新给出导入入口。
 // Leaving manual selection without committing must return to the import choices.
 for(int board=0;board<2;++board){
  library_open_candidates(&ctx);book_import_select("1.txt",false,true);
  if(board)leave_pico(&ctx);else leave_s31(&ctx);
  s_view=SHELF;if(board)return_pico(&ctx);else return_s31(&ctx);
  assert(s_library_setup_pending&&!s_import_candidates&&!book_import_choice_count()&&!commits);
 }
 // 已完成导入的空书架和读取失败，均不冒充首次待导入。
 // An enrolled empty shelf or read error must not be treated as a fresh library.
 mode=BOOK_LIBRARY_SELECTED;s_library_setup_pending=false;return_s31(&ctx);return_pico(&ctx);assert(!s_library_setup_pending);
 mode=BOOK_LIBRARY_UNSET;error=ESP_FAIL;return_s31(&ctx);return_pico(&ctx);assert(!s_library_setup_pending);error=0;
 s_library_setup_pending=true;library_draw_setup(&fb);
 assert(library_setup_action(&ctx,100,680)==APP_REDRAW_PAGE&&s_import_candidates&&s_view==BULK&&s_scan_pending&&!s_library_setup_pending&&!ctx.leaf&&!begins&&!commits);
 populate();library_page_selection(&ctx,true);assert(book_import_choice_count()==4);library_page_selection(&ctx,false);assert(!book_import_choice_count());
 assert(library_cancel_candidates(&ctx)==APP_REDRAW_PAGE&&!commits&&!s_count&&s_library_setup_pending);
 library_open_candidates(&ctx);populate();book_import_select("1.txt",false,true);book_import_select("3.txt",false,true);
 assert(library_import_selected(&ctx)==APP_REDRAW_PAGE&&running&&!commits&&!s_scan_pending);
 assert(library_import_tick(&ctx)==APP_REDRAW_PAGE&&commits==1&&!s_import_candidates&&s_view==SHELF&&!s_library_setup_pending&&!s_count&&members[1]&&members[3]&&!members[0]);
 library_open_candidates(&ctx);populate();assert(library_import_selected(&ctx)==APP_REDRAW_PAGE&&!running&&s_batch_message[0]);
 book_import_select("0.txt",false,true);assert(library_import_selected(&ctx)==APP_REDRAW_PAGE&&running);write_error=ESP_FAIL;
 assert(library_import_tick(&ctx)==APP_REDRAW_PAGE&&!running&&commits==1&&members[1]&&members[3]&&s_import_candidates&&s_batch_message[0]);write_error=0;
 error=ESP_FAIL;int before=s_count;library_filter_loaded(&ctx);assert(s_count==before);error=0;
 assert(library_import_all(&ctx)==APP_REDRAW_PAGE&&running&&s_import_all_pending);assert(library_cancel_candidates(&ctx)==APP_REDRAW_PAGE&&!running&&commits==1&&!s_import_all_pending);
 s_view=BULK;s_manage_books=true;s_count=1;strcpy(s_shelf[0].path,"1.txt");s_shelf[0].selected=true;hidden[1]=true;
 assert(library_import_selected(&ctx)==APP_REDRAW_PAGE&&commits==2&&!hidden[1]&&s_manage_books&&s_view==BULK);
 s_rescan_prompt=true;library_draw_setup(&fb);assert(library_setup_action(&ctx,601,460)==APP_REDRAW_PAGE&&!s_rescan_prompt&&members[1]&&members[3]);
 s_manage_books=false;library_scan_again(&ctx);assert(s_scan_pending&&!s_shelf_cache_valid&&!ctx.leaf);
 s_count=2;strcpy(s_shelf[0].path,"1.txt");strcpy(s_shelf[1].path,"3.txt");s_shelf[0].selected=true;s_shelf[1].selected=false;
 write_error=ESP_FAIL;assert(!library_remove_selected()&&members[1]&&members[3]);write_error=0;assert(library_remove_selected()&&!members[1]&&members[3]);

 // 重扫先清空持久成员；失败保留原列表，取消及切页不恢复旧成员。
 // Rescan resets membership first; failures preserve it and cancellation cannot restore it.
 s_view=BULK;s_manage_books=false;s_count=s_visible_count=2;s_library_setup_pending=false;
 s_shelf[0].selected=true;s_sort_menu=true;ctx.leaf=4;strcpy(s_query,"old");
 int before_commits=commits;write_error=ESP_FAIL;
 assert(library_restart_import(&ctx)==APP_REDRAW_PAGE&&s_view==BULK&&s_count==2&&members[3]&&commits==before_commits&&!s_library_setup_pending&&s_batch_message[0]);
 write_error=0;assert(library_restart_import(&ctx)==APP_REDRAW_PAGE&&mode==BOOK_LIBRARY_UNSET&&s_view==SHELF&&s_library_setup_pending&&!s_scan_pending&&!s_count&&!s_visible_count&&!ctx.leaf&&!s_query[0]&&!s_sort_menu&&commits==before_commits+1);
 for(int i=0;i<8;++i)assert(!members[i]);
 assert(library_setup_action(&ctx,601,460)==APP_REDRAW_PAGE&&!s_library_setup_pending);
 leave_s31(&ctx);return_s31(&ctx);assert(s_library_setup_pending&&!members[3]);
 library_open_candidates(&ctx);book_import_select("0.txt",false,true);library_cancel_candidates(&ctx);
 assert(mode==BOOK_LIBRARY_UNSET&&s_library_setup_pending&&!members[3]&&!members[0]);
 leave_pico(&ctx);return_pico(&ctx);assert(s_library_setup_pending&&!s_scan_pending);
 library_open_candidates(&ctx);book_import_select("0.txt",false,true);library_import_selected(&ctx);write_error=ESP_FAIL;
 library_import_tick(&ctx);write_error=0;library_cancel_candidates(&ctx);assert(mode==BOOK_LIBRARY_UNSET&&!members[0]&&!members[3]&&s_library_setup_pending);
 library_open_candidates(&ctx);book_import_select("0.txt",false,true);library_import_selected(&ctx);library_import_tick(&ctx);
 assert(mode==BOOK_LIBRARY_SELECTED&&members[0]&&!members[1]&&!members[3]&&!s_library_setup_pending);
 puts("PASS: actual shared dialog/async-import lifecycle, page choices, cancellation, checked failure/retry, additive shelf preservation, hidden reimport and atomic removal");}
'''
with tempfile.TemporaryDirectory(prefix='library-ui-') as d:
 p=Path(d);(p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\nenum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NOT_FINISHED=1,ESP_ERR_NOT_FOUND};\n');(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-I'+str(p),'-I'+str(R/'main/book'),str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
for source in [R/'main/apps/app_book.c',R/'boards/pico/src/main/apps/app_book.c']:
 text=source.read_text();assert '#include "apps/book_library_ui.inc"' in text
 assert 'if (complete && !s_import_candidates&&!s_manage_books) shelf_cache_save();' in text
 assert 'if(!s_import_candidates&&!s_manage_books)shelf_added_load();' in text
 assert 's_import_candidates&&book_import_running()' in text

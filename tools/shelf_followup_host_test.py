#!/usr/bin/env python3
"""中文：运行固件实际排序、菜单命中、保存失败和管理页样式往返逻辑。
English: Exercise real sorting/menu/commit-failure and scoped management-style return code.
"""
from pathlib import Path
import subprocess,tempfile,sys
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'tools'))
try:from home_cover_cache_host_test import function
except ImportError:
 sys.path.insert(0,'/Users/weihongli/Documents/New project/read-pico-reader/build/multiboard-20261018/system-ripple-nav-fix-20261020/source/tools');from home_cover_cache_host_test import function
book=R/'main/apps/app_book.c';settings=R/'main/apps/app_device_settings.c'
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef int app_desc_t;
typedef struct{int leaf;const app_desc_t*request_app;bool request_return;}app_ctx_t;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_PAGE}app_redraw_t;
enum{SHELF,BULK,MANAGE,READING,TOC,SETTINGS_MAIN,SETTINGS_SHELF_STYLE,READER_PANEL_NONE,READER_PANEL_TOUCH_SETTINGS,READER_PANEL_TOUCH_MORE,READER_PANEL_KEY_SETTINGS};
enum{UI_GESTURE_TAP,UI_GESTURE_SWIPE_U};
typedef struct{unsigned type;int x0,y0,x,y;}ui_gesture_event_t;
typedef struct{char name[32],path[64];bool favorite,has_progress,search_match,selected;unsigned pct,recent,added,size,chapter;bool removed,chapters_checked;}shelf_entry_t;
static shelf_entry_t items[]={{"Z","/z",false,true,true,true,100,5,9,1,0,false,false},{"A","/a",false,false,true,false,99,0,1,1,0,false,false},{"B","/b",true,true,true,false,10,4,2,1,0,false,false},{"C","/c",false,true,true,false,50,3,3,1,0,false,false},{"D","/d",false,true,false,true,60,2,4,1,0,false,false}};
static shelf_entry_t*s_shelf=items;static int s_count=5,s_visible_count,s_view=SHELF;
static uint8_t s_shelf_order,persisted_order;static bool s_recent_sort,s_sort_menu,fail_save;
static int invalidations,remembered;static char s_message[128],s_batch_message[128];
static bool shelf_matches(const shelf_entry_t*i){return i->search_match;}
static void copy_text(char*d,size_t cap,const char*s){snprintf(d,cap,"%s",s);}
static void invalidate_covers(void){++invalidations;}
static void shelf_category_remember(app_ctx_t*c){(void)c;++remembered;}
static bool app_settings_try_shelf_order(uint8_t order){if(fail_save)return false;persisted_order=order;return true;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
'''
for n in ['shelf_order_mode','shelf_order_desc','compare_books','sort_shelf','shelf_order_apply','shelf_sort_menu_rect','shelf_sort_gesture','bulk_sort_rect']:u+=function(n,book)+'\n'
f=function('bulk_action',book);a=f.index('    if(s_sort_menu){');b=f.index('    if(s_batch_confirm)',a)
u+='static app_redraw_t bulk_sort_click(app_ctx_t*ctx,int x,int y){'+f[a:b]+'return APP_REDRAW_NONE;}\n'
u+=r'''
static uint32_t s_progress_revision,progress_revision=1;
static bool s_chapters_started;
static char s_latest_path[64];
#define BOOK_STORE_PATH_MAX 64
typedef struct{unsigned last_open_s,pct,chapter;}book_progress_t;
static uint32_t book_progress_revision(void){return progress_revision;}
static bool shelf_backfill_read_books(void*p){(void)p;return false;}
static void shelf_added_load(void){}
static bool book_progress_last_path(char*p,size_t cap){copy_text(p,cap,"/c");return true;}
static bool book_progress_load(const char*p,unsigned size,book_progress_t*out){(void)size;assert(!strcmp(p,"/c"));*out=(book_progress_t){3,100,2};return true;}
static void shelf_categories_recount(void){}
static void shelf_category_restore(app_ctx_t*c){(void)c;}
'''
u+=function('refresh_cached_progress',book)+'\n'
u+=r'''
static bool s_manage_style_restore,s_shelf_cache_valid=true,s_cache_sd_present=true,s_cache_sd_mounted=true,s_scan_pending;
static bool sd_present=true,sd_mounted=true;
static int s_presented_view,s_pressed_control,s_cache_shelf_style,fallback;
static unsigned s_store_revision=9;
typedef struct{bool present,mounted;}read_pico_sd_info_t;
#define ESP_OK 0
static int read_pico_sd_get_info(read_pico_sd_info_t*out){*out=(read_pico_sd_info_t){sd_present,sd_mounted};return 0;}
static unsigned book_store_revision(void){return 9;}
static unsigned app_settings_shelf_style(void){return 6;}
'''
u+=function('app_book_return_from_shelf_style',book)+'\n'
f=function('on_enter',book);u+=f[:f.index('    reader_indent_hold_cancel();')].replace('on_enter','return_enter',1)+'    (void)ctx;++fallback;\n}\n'
u+=r'''
static int s_page=SETTINGS_SHELF_STYLE;static bool s_shelf_style_requested,s_shelf_style_from_manage;
'''
u+=function('app_device_settings_request_shelf_style',settings)+'\n'+function('shelf_style_finish',settings)+'\n'
u+=r'''
static int s_reader_panel;static const char*s_text="body";static bool s_reader_fullscreen,immersive;
static bool app_settings_reader_immersive(void){return immersive;}
'''
u+=function('app_book_status_bar_visible',book)+'\n'+function('reader_status_visible',book)+'\n'
u+=r'''
int main(void){
 app_ctx_t ctx={.leaf=3};
 for(unsigned order=0;order<8;++order){s_shelf_order=order;s_recent_sort=order==1||order==3;
  for(int i=0;i<5;++i)for(int j=0;j<5;++j){int a=compare_books(&items[i],&items[j]),b=compare_books(&items[j],&items[i]);assert((a>0)-(a<0)==-((b>0)-(b<0)));}
 }
 s_shelf_order=7;s_recent_sort=false;sort_shelf(&ctx);assert(s_visible_count==4&&!strcmp(items[0].path,"/b")&&!strcmp(items[1].path,"/z")&&!strcmp(items[2].path,"/c")&&!strcmp(items[3].path,"/a"));
 s_shelf_order=6;sort_shelf(&ctx);assert(!strcmp(items[0].path,"/b")&&!strcmp(items[1].path,"/a")&&!strcmp(items[2].path,"/c")&&!strcmp(items[3].path,"/z"));
 bool selected_z=false,selected_d=false;for(int i=0;i<5;++i){if(!strcmp(items[i].path,"/z"))selected_z=items[i].selected;if(!strcmp(items[i].path,"/d"))selected_d=items[i].selected;}assert(selected_z&&selected_d);
 refresh_cached_progress(&ctx);assert(!strcmp(items[0].path,"/b")&&!strcmp(items[1].path,"/a")&&!strcmp(items[2].path,"/c")&&items[2].pct==100);
 s_shelf_order=0;s_recent_sort=false;s_sort_menu=true;const unsigned expected[]={1,5,0,7};
 for(int i=0;i<4;++i){EpdRect r=shelf_sort_menu_rect(i);ui_gesture_event_t e={UI_GESTURE_TAP,r.x+20,r.y+20,r.x+20,r.y+20};assert(shelf_sort_gesture(&ctx,&e)==APP_REDRAW_PAGE&&s_sort_menu&&s_shelf_order==expected[i]&&persisted_order==expected[i]);}
 ui_gesture_event_t direction={UI_GESTURE_TAP,400,500,400,500};shelf_sort_gesture(&ctx,&direction);assert(s_shelf_order==6);
 direction.x0=direction.x=550;shelf_sort_gesture(&ctx,&direction);assert(s_shelf_order==7);
 fail_save=true;ctx.leaf=3;int before=invalidations;direction.x0=direction.x=400;shelf_sort_gesture(&ctx,&direction);assert(s_shelf_order==7&&persisted_order==7&&ctx.leaf==3&&invalidations==before&&s_message[0]);fail_save=false;
 direction.x0=400;direction.x=20;shelf_sort_gesture(&ctx,&direction);assert(!s_sort_menu&&s_shelf_order==7);
 s_view=BULK;s_sort_menu=true;EpdRect r=bulk_sort_rect(4);bulk_sort_click(&ctx,r.x+20,r.y+20);assert(s_shelf_order==6);r=bulk_sort_rect(5);bulk_sort_click(&ctx,r.x+20,r.y+20);assert(s_shelf_order==7);r=bulk_sort_rect(6);bulk_sort_click(&ctx,r.x+20,r.y+20);assert(!s_sort_menu);
 app_device_settings_request_shelf_style();assert(s_shelf_style_requested);s_shelf_style_from_manage=true;
 ctx.leaf=2;assert(shelf_style_finish(&ctx)==APP_REDRAW_NONE&&ctx.request_return&&s_manage_style_restore);
 selected_z=false;for(int i=0;i<5;++i)if(!strcmp(items[i].path,"/z"))selected_z=items[i].selected;
 return_enter(&ctx);assert(!fallback&&!s_manage_style_restore&&ctx.leaf==2&&s_cache_shelf_style==6&&selected_z);
 s_shelf_style_from_manage=false;ctx.request_return=false;assert(shelf_style_finish(&ctx)==APP_REDRAW_PAGE&&s_page==SETTINGS_MAIN&&!ctx.request_return);
 app_book_return_from_shelf_style();sd_mounted=false;return_enter(&ctx);assert(fallback==1&&!s_manage_style_restore);
 s_view=READING;s_reader_panel=READER_PANEL_TOUCH_SETTINGS;assert(!app_book_status_bar_visible()&&!reader_status_visible());s_reader_panel=READER_PANEL_TOUCH_MORE;assert(!app_book_status_bar_visible()&&!reader_status_visible());
 s_reader_panel=READER_PANEL_KEY_SETTINGS;assert(app_book_status_bar_visible()&&reader_status_visible());s_reader_fullscreen=immersive=true;assert(!reader_status_visible());
 puts("PASS: eight real sort orders and comparator symmetry, progress/unread/favorites/filter/selection, root+bulk directions, failed commit, scoped native chooser return, full-screen touch status exclusion");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(u);subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute real directory discovery/navigation on nested filesystem data."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#define BOOK_STORE_PATH_MAX 288
#define BOOK_SCAN_DEPTH_LIMIT 12
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
typedef int nvs_handle_t;typedef int app_redraw_t;
enum{APP_REDRAW_NONE,APP_REDRAW_PAGE};
typedef struct {int leaf;}app_ctx_t;
typedef struct {DIR*dir;char path[288];}shelf_scan_frame_t;
typedef struct {char path[288],name[256],author[128];bool is_dir,search_match,is_flash,cover_identity_known,chapters_checked,favorite,shelved,selected,has_progress;unsigned size,chapters,pct,chapter,recent;int64_t modified;}shelf_entry_t;
static shelf_entry_t s_shelf[64];static int s_count,s_visible_count;static bool s_manage_books=true,s_import_candidates,s_bulk_selecting;
static char s_manage_dir[288],s_query[65],s_batch_message[128],root[288];static unsigned scans;
static const char *app_settings_books_dir(void){return root;}
static void copy_text(char*d,size_t cap,const char*s){snprintf(d,cap,"%s",s);}
static void clear_selection(void){for(int i=0;i<s_count;++i)s_shelf[i].selected=false;}
static void library_scan_again(app_ctx_t*c){++scans;c->leaf=0;}
static void *heap_caps_calloc(size_t n,size_t s,int caps){assert(caps==3);return calloc(n,s);}
static bool shelf_scan_interrupted(app_ctx_t*c){(void)c;return false;}
static bool shelf_reserve(void){return s_count<64;}
static bool book_file_path_supported(const char*p){const char*e=strrchr(p,'.');return e&&(!strcasecmp(e,".txt")||!strcasecmp(e,".epub"));}
static bool book_library_contains(const char*p){(void)p;return false;}
static bool shelf_hidden_read_handle(nvs_handle_t h,const char*p){(void)h;(void)p;return false;}
static int shelf_category_add(const char*p){(void)p;return 2;}
static bool book_name_from_path(const char*p,char*n,size_t cap,char*a,size_t ac){(void)p;(void)n;(void)cap;(void)a;(void)ac;return false;}
static bool book_display_name_from_path(const char*p,char*n,size_t cap,char*a,size_t ac){return book_name_from_path(p,n,cap,a,ac);}
static void clean_filename(char*d,size_t n,const char*s){copy_text(d,n,s);}
static bool favorite_read_handle(nvs_handle_t h,const char*p){(void)h;(void)p;return false;}
static __attribute__((unused)) int book_epub_metadata_cached_for_file(const char*p,uint64_t s,int64_t m,char*t,size_t tc,char*a,size_t ac){(void)p;(void)s;(void)m;(void)t;(void)tc;(void)a;(void)ac;return 1;}
static __attribute__((unused)) int book_epub_metadata_cached(const char*p,char*t,size_t tc,char*a,size_t ac){return book_epub_metadata_cached_for_file(p,0,0,t,tc,a,ac);}
typedef struct{unsigned pct,chapter,last_open_s;}book_progress_t;
static __attribute__((unused)) bool book_progress_load(const char*p,unsigned size,book_progress_t*v){(void)p;(void)size;(void)v;return false;}
static bool book_author_clean_display(char*p,size_t c){(void)p;(void)c;return false;}
static bool read_pico_search_match(const char*s,const char*q){return !*q||strstr(s,q)!=NULL;}
'''
unit+=(R/'main/apps/book_directory_ui.inc').read_text()+'\n'
unit+=function('scan_shelf_dir',R/'main/apps/app_book.c')+'\n'
unit+=r'''
static void scan(app_ctx_t*c){bool t=false,u=false,k=false;s_count=0;scan_shelf_dir(c,books_directory_current(),false,0,0,&t,&u,&k);assert(!t&&!u&&!k);}
static bool found(const char*base,bool folder){for(int i=0;i<s_count;++i){const char*p=strrchr(s_shelf[i].path,'/');if(p&&!strcmp(p+1,base)&&s_shelf[i].is_dir==folder)return true;}return false;}
int main(int argc,char**argv){assert(argc==2);copy_text(root,sizeof(root),argv[1]);app_ctx_t c={.leaf=7};
 scan(&c);assert(s_count==3&&found("A",true)&&found("Empty",true)&&found("root.txt",false)&&!found("inside.txt",false));assert(books_directory_is_root());
 char path[288];snprintf(path,sizeof(path),"%s/A",root);assert(books_directory_open(&c,path)==APP_REDRAW_PAGE&&scans==1&&!c.leaf&&!s_count&&!books_directory_is_root());
 assert(!strcmp(books_directory_name(),"A"));scan(&c);assert(s_count==2&&found("Deep",true)&&found("inside.txt",false)&&!found("root.txt",false));
 snprintf(path,sizeof(path),"%s/A/Deep",root);assert(books_directory_open(&c,path)==APP_REDRAW_PAGE);scan(&c);assert(s_count==1&&found("deep.epub",false));
 assert(books_directory_up(&c)==APP_REDRAW_PAGE&&!strcmp(books_directory_name(),"A"));assert(books_directory_up(&c)==APP_REDRAW_PAGE&&books_directory_is_root());assert(books_directory_up(&c)==APP_REDRAW_NONE);
 snprintf(path,sizeof(path),"%s2",root);assert(books_directory_open(&c,path)==APP_REDRAW_NONE);
 snprintf(path,sizeof(path),"%s/Empty",root);assert(books_directory_open(&c,path)==APP_REDRAW_PAGE);scan(&c);assert(!s_count);
 s_manage_books=false;s_import_candidates=true;s_manage_dir[0]=0;scan(&c);assert(s_count==3&&found("root.txt",false)&&found("A",true)&&found("Empty",true)&&!found("inside.txt",false));
 puts("PASS: real immediate folders/files, empty and nested folders, bounded parent navigation, root prefix guard; manual import browses the same real hierarchy");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);tree=p/'books';(tree/'A/Deep').mkdir(parents=True);(tree/'Empty').mkdir();(tree/'.hidden').mkdir()
 for f in ['root.txt','A/inside.txt','A/Deep/deep.epub','ignore.png','.hidden/hidden.txt']:(tree/f).write_text('book')
 (p/'test.c').write_text(unit);subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test'),str(tree)],check=True)

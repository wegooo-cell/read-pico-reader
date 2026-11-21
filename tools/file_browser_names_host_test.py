#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute file scanning/open/copy with real long UTF-8 names, independently of UI label length."""
from pathlib import Path
import json
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'main/apps/app_files.c'
text = source.read_text()
item_type = re.search(r'typedef struct \{[^{}]*\} file_item_t;', text).group()
unit = r'''
#include <assert.h>
#include "read_pico_search.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
typedef int esp_err_t;
typedef int app_redraw_t;
#define APP_REDRAW_PAGE 1
enum {ESP_OK=0,ESP_ERR_INVALID_SIZE=-1,ESP_ERR_INVALID_ARG=-2,ESP_FAIL=-3,ESP_ERR_INVALID_STATE=-4};
typedef struct {int id;} app_desc_t;
typedef struct {const app_desc_t *request_app;} app_ctx_t;
const app_desc_t app_book={1},app_image={2};
typedef struct {bool mounted;uint64_t free_bytes;} read_pico_sd_info_t;
#define BOOK_STORE_PATH_MAX 288
#define BOOK_PROGRESS_MOVE_CONFLICT 0x6b01
static int s_book_move_error;
static char mock_last_path[288];
static bool book_progress_last_path(char *out,size_t cap){snprintf(out,cap,"%s",mock_last_path);return mock_last_path[0]!=0;}
static esp_err_t book_progress_set_last_path(const char *path){snprintf(mock_last_path,sizeof(mock_last_path),"%s",path);return ESP_OK;}
#define FILE_MAX 96
enum {FILE_VIEW_LIST,FILE_VIEW_MOVE};
'''+item_type+r'''
static file_item_t *s_files,s_selected;
static int s_count,s_page,s_view,s_counts[3],s_file_sort;
static bool s_file_desc;
static char s_file_query[65];
static unsigned recursive_starts;
static void file_search_start(void){++recursive_starts;}
static void file_search_stop(void){}
static void file_selection_restore(void){}
static char s_message[96],s_dir[288],root[80],opened[288],copied_to[288];
static read_pico_sd_info_t s_sd;
static bool card=true,alloc_ok=true,migration_ok=true;
static unsigned changed,book_opens,image_opens,font_opens,haptics;
static bool files_alloc(void){if(!alloc_ok)return false;if(!s_files)s_files=calloc(FILE_MAX,sizeof(*s_files));return s_files!=NULL;}
static const char *folder_root(int folder){(void)folder;return root;}
static void read_pico_sd_get_info(read_pico_sd_info_t *info){info->mounted=card;info->free_bytes=1024*1024;}
static void book_store_notify_changed(void){++changed;}
static void check_payload(const char *path){FILE *f=fopen(path,"rb");assert(f);char b[8]={0};assert(fread(b,1,7,f)==7&&!strcmp(b,"payload"));assert(!fclose(f));}
static bool app_book_request_open(const char *path){check_payload(path);strcpy(opened,path);++book_opens;return true;}
static bool app_image_request_open(const char *path){check_payload(path);strcpy(opened,path);++image_opens;return true;}
static int ttf_font_open(const char *path){check_payload(path);strcpy(opened,path);++font_opens;return ESP_OK;}
static void app_settings_set_system_font_path(const char *path){assert(!strcmp(path,opened));}
static void app_font_activate_system(void){}
static bool moved_settings_fit(const char *from,const char *to){assert(!strcmp(from,s_selected.path)&&strcmp(from,to));return true;}
static void sync_moved_settings(const char *from,const char *to){assert(!strcmp(from,s_selected.path));check_payload(to);}
static bool notify_moved_tree(const char *from,const char *to,unsigned depth){assert(!depth&&!strcmp(from,s_selected.path));check_payload(to);if(!migration_ok)snprintf(mock_last_path,sizeof(mock_last_path),"%s",to);return migration_ok;}
static void leave_move(bool changed_ok){assert(changed_ok);}
static int file_tree_move(const char *from,const char *to){check_payload(from);return rename(from,to)?ESP_FAIL:ESP_OK;}
#if KIIKO_BOARD_METALIO47
static int metalio_haptic_click(void){++haptics;return 0;}
#endif
// Real FS at the operation boundary verifies that the selected original name is used intact.
static int file_tree_copy(const char *from,const char *to,uint64_t free_bytes){
 assert(free_bytes==1024*1024);check_payload(from);FILE *f=fopen(to,"wb");assert(f);assert(fwrite("payload",1,7,f)==7);assert(!fclose(f));strcpy(copied_to,to);return ESP_OK;
}
'''
for name in ['file_item_name', 'copy_utf8', 'sibling_path', 'copy_selected_item', 'move_selected',
             'supported', 'compare_files', 'scan_folder', 'open_selected']:
    unit += function(name, source)+'\n'
unit += r'''
static void name_repeat(char *out,const char *glyph,unsigned n,const char *tail){
 out[0]=0;for(unsigned i=0;i<n;++i)strcat(out,glyph);strcat(out,tail);
}
static void make_file(const char *name){char path[512];assert(snprintf(path,sizeof(path),"%s/%s",root,name)<(int)sizeof(path));FILE *f=fopen(path,"wb");assert(f);assert(fwrite("payload",1,7,f)==7);assert(!fclose(f));}
static int find(const char *name){for(int i=0;i<s_count;++i)if(!strcmp(file_item_name(&s_files[i]),name))return i;return -1;}
static void clear_root(void){DIR *d=opendir(root);assert(d);struct dirent *e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[512];snprintf(p,sizeof(p),"%s/%s",root,e->d_name);struct stat st;assert(!stat(p,&st));assert(S_ISDIR(st.st_mode)?!rmdir(p):!unlink(p));}assert(!closedir(d));}
static void verify_name(const char *name){
 make_file(name);s_counts[0]=777;scan_folder(0);assert(s_count==1&&s_counts[0]==777);
 int n=find(name);assert(n==0&&!s_files[n].is_dir);s_selected=s_files[n];
 char expected[288];assert(snprintf(expected,sizeof(expected),"%s/%s",root,name)<(int)sizeof(expected));assert(!strcmp(s_selected.path,expected));
 char preview[128];copy_utf8(preview,sizeof(preview),file_item_name(&s_selected));assert(strlen(preview)<sizeof(preview));assert(!memcmp(name,preview,strlen(preview)));assert(((unsigned char)name[strlen(preview)]&0xc0)!=0x80);
 // A selected item retains its identity after the list is reused by a rescan.
 memset(s_files,0,FILE_MAX*sizeof(*s_files));app_ctx_t ctx={0};open_selected(&ctx);assert(ctx.request_app==&app_book&&!strcmp(opened,expected));
 clear_root();
}
int main(void){
 strcpy(root,"/tmp/kfn-XXXXXX");assert(mkdtemp(root));
 char name[288];
 for(unsigned chars=40;chars<=44;++chars){name_repeat(name,"中",chars,".epub");verify_name(name);}
 name_repeat(name,"文",80,".EPUB");verify_name(name);
 for(unsigned chars=121;chars<=130;++chars){name_repeat(name,"a",chars,".epub");verify_name(name);}
 name_repeat(name,"a",245,".epub");verify_name(name);
 name_repeat(name,"😀",38,".epub");verify_name(name);
 // Names with identical shortened labels stay distinct for sorting, selection and copy.
 char a[288],b[288];name_repeat(a,"中",44,"甲.epub");name_repeat(b,"中",44,"乙.epub");make_file(a);make_file(b);
 scan_folder(0);assert(s_count==2&&find(a)>=0&&find(b)>=0&&compare_files(&s_files[0],&s_files[1])<0);
 int n=find(a);s_selected=s_files[n];char label[128];assert(copy_selected_item(label)==ESP_OK&&changed==1);
 char copy[288],expected[288];name_repeat(copy,"中",44,"甲 副本.epub");snprintf(expected,sizeof(expected),"%s/%s",root,copy);assert(!strcmp(copied_to,expected));check_payload(expected);
 assert(copy_selected_item(label)==ESP_OK&&changed==2);name_repeat(copy,"中",44,"甲 副本 2.epub");snprintf(expected,sizeof(expected),"%s/%s",root,copy);assert(!strcmp(copied_to,expected));
 snprintf(s_dir,sizeof(s_dir),"%s/move",root);assert(!mkdir(s_dir,0700));
 snprintf(mock_last_path,sizeof(mock_last_path),"%s",s_selected.path);
 migration_ok=false;assert(move_selected()==APP_REDRAW_PAGE&&changed==2&&s_message[0]);check_payload(s_selected.path);assert(!strcmp(mock_last_path,s_selected.path));migration_ok=true;
 assert(move_selected()==APP_REDRAW_PAGE&&changed==3);snprintf(expected,sizeof(expected),"%s/%s",s_dir,a);check_payload(expected);assert(access(s_selected.path,F_OK));assert(!unlink(expected)&&!rmdir(s_dir));clear_root();
 // General directory and type-specific filters retain long directory, image and font names.
 name_repeat(name,"册",44,"");char path[512];snprintf(path,sizeof(path),"%s/%s",root,name);assert(!mkdir(path,0700));
 scan_folder(3);assert(s_count==1&&s_files[0].is_dir&&!strcmp(file_item_name(&s_files[0]),name));
 scan_folder(0);assert(s_count==1&&s_files[0].is_dir&&!strcmp(file_item_name(&s_files[0]),name));
 s_view=FILE_VIEW_MOVE;scan_folder(3);assert(s_count==1);s_view=FILE_VIEW_LIST;assert(!rmdir(path));
 const char *suffix[]={".png",".ttf",".otf"};for(unsigned i=0;i<3;++i){name_repeat(name,"名",44,suffix[i]);make_file(name);scan_folder(i?2:1);assert(s_count==1);s_selected=s_files[0];app_ctx_t ctx={0};open_selected(&ctx);assert((!i&&ctx.request_app==&app_image)||(i&&!ctx.request_app));clear_root();}
 // Books retain visible nested folders; enter each ordinary directory and open its original book path.
 char original_root[80];strcpy(original_root,root);make_file("root.epub");char level1[288],level2[288];
 snprintf(level1,sizeof(level1),"%s/Collection",root);assert(!mkdir(level1,0700));scan_folder(0);assert(s_count==2&&s_files[0].is_dir&&find("Collection")==0);
 snprintf(root,sizeof(root),"%s/Collection",original_root);snprintf(level2,sizeof(level2),"%s/Series",root);assert(!mkdir(level2,0700));scan_folder(3);assert(s_count==1&&s_files[0].is_dir&&!strcmp(file_item_name(&s_files[0]),"Series"));
 snprintf(root,sizeof(root),"%s/Collection/Series",original_root);make_file("nested.epub");scan_folder(3);assert(s_count==1&&!s_files[0].is_dir);s_selected=s_files[0];app_ctx_t nested={0};open_selected(&nested);assert(nested.request_app==&app_book&&strstr(opened,"Collection/Series/nested.epub"));clear_root();
 assert(!rmdir(level2)&&!rmdir(level1));strcpy(root,original_root);clear_root();
 // Existing hidden/type filters and media failures remain; no path is truncated into another target.
 make_file("short.epub");make_file(".hidden.epub");make_file("ignored.zip");scan_folder(0);assert(s_count==1&&find("short.epub")==0);
 card=false;scan_folder(0);assert(!s_count&&s_message[0]);card=true;scan_folder(3);assert(s_count==2);clear_root();
 // The existing complete-path bound still rejects overflow instead of targeting a truncated sibling.
 char saved_root[80];strcpy(saved_root,root);strcat(root,"/nested-directory-long");assert(!mkdir(root,0700));
 name_repeat(name,"a",245,".epub");make_file(name);scan_folder(0);assert(!s_count);clear_root();assert(!rmdir(root));strcpy(root,saved_root);
 assert(haptics==(KIIKO_BOARD_METALIO47?book_opens:0));assert(image_opens==1&&font_opens==2);
 strcpy(s_file_query,"book");scan_folder(0);assert(recursive_starts==1);
 s_view=FILE_VIEW_MOVE;scan_folder(3);assert(recursive_starts==1);
 free(s_files);assert(!rmdir(root));
 puts("PASS: actual browser scan/open/copy/move preserves 40-44/80 Chinese, 121-250-byte ASCII, emoji, long folders/images/fonts, same-prefix identities, UTF-8 label bounds and SD/type guards");
}
'''
with tempfile.TemporaryDirectory(prefix='file-browser-names-') as folder:
    work = Path(folder)
    (work/'test.c').write_text(unit)
    marker = ROOT / '.kiiko-source-view.json'
    boards = (int(json.loads(marker.read_text())['board'] == 'metalio47'),) if marker.exists() else (0, 1)
    for board in boards:
        output = work/f'board-{board}'
        subprocess.run(['cc', '-std=gnu11', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', f'-DKIIKO_BOARD_METALIO47={board}',
                        '-I'+str(ROOT/'components/read_pico_search/include'),str(ROOT/'components/read_pico_search/read_pico_search.c'), str(work/'test.c'), '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True)

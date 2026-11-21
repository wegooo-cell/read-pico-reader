#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实批量调度配合临时文件系统，只替换平台边界。/ English: Real batch scheduling and a temporary filesystem, with platform boundaries replaced."""
from pathlib import Path
import re, subprocess, tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
source=root/'main/apps/app_files.c'
item=re.search(r'typedef struct \{[^{}]*\} file_item_t;',source.read_text())[0]
unit=r'''
#include <assert.h>
#include "read_pico_search.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file_tree.h"
#define FILE_MAX 96
#define UI_PAGER_NONE -1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int unused;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
typedef struct {bool mounted;} read_pico_sd_info_t;
enum {FILE_VIEW_LIST,FILE_VIEW_MOVE};
'''+item+r'''
static file_item_t *s_files,*s_batch;
static unsigned s_batch_count,s_batch_index,s_batch_ok,s_batch_failed;
static bool s_file_selecting,s_batch_running,s_batch_move,s_scan_pending;
static bool s_pager_paint_pending;
static void *s_file_search;
static void file_search_stop(void){s_file_search=NULL;}
static int s_pager_pressed,s_count,s_folder,s_view,s_file_sort,s_move_origin_folder;
static bool s_file_desc;
static char s_message[96],s_dir[288],s_move_origin_dir[288],s_batch_target[288],root_path[120];
static read_pico_sd_info_t s_sd;
static bool mounted=true,alloc_fail,quiescent=true;
static unsigned moves,deletes,notifications,migrations,repairs;
static void *heap_caps_calloc(size_t n,size_t size,int caps){assert(caps==3);return alloc_fail?NULL:calloc(n,size);}
static bool book_cover_jobs_quiesce(unsigned timeout){assert(timeout==2000);return quiescent;}
static bool ui_avatar_quiesce(unsigned timeout){assert(timeout==2000);return quiescent;}
static void read_pico_sd_get_info(read_pico_sd_info_t *out){out->mounted=mounted;}
static unsigned book_count_invalidations;
static void file_book_count_invalidate(void){++book_count_invalidations;}
static void book_store_notify_changed(void){++notifications;}
static void repair_deleted_directories(void){assert(mounted);++repairs;}
static bool moved_settings_fit(const char *a,const char *b){assert(a&&b);return true;}
static void sync_moved_settings(const char*a,const char*b){assert(a&&b);++migrations;}
static bool notify_moved_tree(const char*a,const char*b,unsigned depth){assert(a&&b&&!depth);return true;}
static void file_deleted(const char *path,bool dir,void *ctx){(void)dir;(void)ctx;assert(strncmp(path,root_path,strlen(root_path))==0);}
static void copy_utf8(char *out,size_t size,const char *in){assert(strlen(in)<size);snprintf(out,size,"%s",in);}
static void host_path(const char *virtual_path,char out[512]){
 assert(file_tree_same_or_below(virtual_path,"/sdcard"));
 assert(snprintf(out,512,"%s%s",root_path,virtual_path+7)<512);
}
extern esp_err_t real_move(const char *,const char *);
extern esp_err_t real_delete(const char *,file_tree_deleted_cb_t,void *);
esp_err_t file_tree_move(const char*a,const char*b){char x[512],y[512];host_path(a,x);host_path(b,y);++moves;return real_move(x,y);}
esp_err_t file_tree_delete(const char*a,file_tree_deleted_cb_t callback,void *ctx){char x[512];host_path(a,x);++deletes;return real_delete(x,callback,ctx);}
static void rescan_current(void){
 char path[512];host_path(s_dir,path);DIR *dir=opendir(path);assert(dir);s_count=0;
 struct dirent *entry;while((entry=readdir(dir))){
  if(entry->d_name[0]=='.')continue;assert(s_count<FILE_MAX);
  file_item_t *f=&s_files[s_count++];memset(f,0,sizeof(*f));
  assert(snprintf(f->path,sizeof(f->path),"%s/%s",s_dir,entry->d_name)<(int)sizeof(f->path));
 }assert(!closedir(dir));
}
static void leave_move(bool changed){assert(changed);s_view=FILE_VIEW_LIST;strcpy(s_dir,s_move_origin_dir);rescan_current();}
'''
unit+='\n'.join(line for line in (root/'main/ui/ui_files_layout.h').read_text().splitlines() if not line.startswith(('#include','#pragma'))) + '\n'
for name in ['file_item_name','file_selected_count','file_select_rect','file_sort_button','file_batch_button','file_selection_restore','compare_files','file_batch_prepare','file_batch_begin','file_batch_tick','files_before_lock']:
 unit+=function(name,source)+'\n'
unit+=r'''
static void make_file(const char *name){char path[512];host_path(name,path);FILE *file=fopen(path,"wb");assert(file);assert(fwrite("payload",1,7,file)==7);assert(!fclose(file));}
static bool exists(const char *name){char path[512];host_path(name,path);return access(path,F_OK)==0;}
static int find(const char *name){for(int i=0;i<s_count;++i)if(!strcmp(s_files[i].path,name))return i;return -1;}
static void select_path(const char *name){int i=find(name);assert(i>=0);s_files[i].selected=true;}
int main(int argc,char **argv){
 assert(argc==2);snprintf(root_path,sizeof(root_path),"%s",argv[1]);
 s_files=calloc(FILE_MAX,sizeof(*s_files));assert(s_files);s_file_selecting=true;strcpy(s_dir,"/sdcard");
 char target[512];host_path("/sdcard/target",target);assert(!mkdir(target,0700));
 make_file("/sdcard/a.txt");make_file("/sdcard/b.txt");make_file("/sdcard/target/b.txt");
 rescan_current();select_path("/sdcard/a.txt");select_path("/sdcard/b.txt");
 s_files[find("/sdcard/a.txt")].size=99;s_files[find("/sdcard/b.txt")].size=1;
 for(int order=0;order<3;++order)for(int desc=0;desc<2;++desc){s_file_sort=order;s_file_desc=desc;qsort(s_files,s_count,sizeof(*s_files),compare_files);assert(file_selected_count()==2&&s_files[find("/sdcard/a.txt")].selected);}
 alloc_fail=true;assert(!file_batch_prepare()&&!s_batch);alloc_fail=false;assert(file_batch_prepare());
 strcpy(s_move_origin_dir,s_dir);strcpy(s_dir,"/sdcard/target");
 quiescent=false;assert(!file_batch_begin(true)&&!s_batch_running&&!moves);quiescent=true;
 assert(file_batch_begin(true));assert(file_batch_tick()==APP_REDRAW_PAGE&&s_batch_index==1&&moves==1&&s_batch_running);
 while(s_batch_running)file_batch_tick();
 assert(s_batch_ok==1&&s_batch_failed==1&&s_file_selecting&&migrations==1);
 assert(exists("/sdcard/target/a.txt")&&!exists("/sdcard/a.txt")&&exists("/sdcard/b.txt")&&exists("/sdcard/target/b.txt"));
 assert(file_selected_count()==1&&s_files[find("/sdcard/b.txt")].selected);
 // 失败项目保持原身份可删除，已完成项不重复处理。/ Retain failed identities for retry without processing completed entries again.
 assert(file_batch_prepare()&&s_batch_count==1&&file_batch_begin(false));file_batch_tick();
 assert(s_batch_ok==1&&!s_batch_failed&&!s_file_selecting&&!exists("/sdcard/b.txt")&&deletes==1&&repairs==1);
 s_file_selecting=true;s_count=1;s_files[0]=(file_item_t){.path="/sdcard/target",.is_dir=true,.selected=true};assert(file_batch_prepare());
 strcpy(s_dir,"/sdcard/target/sub");assert(!file_batch_begin(true));
 // 卡不可用时不执行文件操作；锁屏取消后也不自动继续。/ Unavailable media prevents I/O; locking cancels without automatic continuation.
 s_count=3;s_files[0]=(file_item_t){.path="/sdcard/target",.is_dir=true,.selected=true};
 s_files[1]=(file_item_t){.path="/sdcard/target/a.txt",.selected=true};
 s_files[2]=(file_item_t){.path="/sdcard/target/sub/deep.txt",.selected=true};
 assert(file_batch_prepare()&&s_batch_count==1&&!strcmp(s_batch[0].path,"/sdcard/target"));
 s_count=1;s_files[0]=(file_item_t){.path="/sdcard/target/a.txt",.selected=true};assert(file_batch_prepare());
 mounted=false;assert(!file_batch_begin(false));mounted=true;strcpy(s_dir,"/sdcard/target");assert(file_batch_begin(false));
 s_file_search=(void*)1;unsigned old_deletes=deletes;files_before_lock(NULL);assert(!s_file_search&&!s_batch_running&&s_scan_pending&&deletes==old_deletes);
 s_scan_pending=false;assert(file_batch_begin(false));mounted=false;file_batch_tick();assert(deletes==old_deletes&&!s_batch_ok&&s_batch_failed==1&&repairs==1);mounted=true;
 assert(exists("/sdcard/target/a.txt"));
 // 来源根目录本身不能成为删除目标；按钮顺序与触摸区域保持一致。/ Never delete the media root; preserve visible/hit button order.
 s_batch[0]=(file_item_t){.path="/sdcard",.selected=true};s_batch_count=1;s_batch_index=0;s_batch_running=true;s_batch_move=false;strcpy(s_dir,"/sdcard");file_batch_tick();assert(deletes==old_deletes&&s_batch_failed>0);
 for(int folder=-1;folder<2;++folder){s_folder=folder;EpdRect name=file_sort_button(),select=file_select_rect();assert(select.x>=name.x+name.width&&select.y==name.y&&select.x+select.width<=684);}
 assert(notifications>=3);free(s_batch);free(s_files);
 puts("PASS batch: full-path selection across sorts, one operation per tick, real move conflict/source preservation, failed-only retry, allocation/card/lock guards and button geometry");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-file-batch-') as temp:
 work=Path(temp);(work/'test.c').write_text(unit);(work/'card').mkdir()
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-g','-fsanitize=address,undefined','-I'+str(root/'tools/file_tree_stubs'),'-I'+str(root/'main/apps'),'-I'+str(root/'components/read_pico_search/include')]
 subprocess.run(flags+['-Dfile_tree_move=real_move','-Dfile_tree_delete=real_delete','-c',str(root/'main/apps/file_tree.c'),'-o',str(work/'tree.o')],check=True)
 subprocess.run(flags+[str(work/'test.c'),str(work/'tree.o'),str(root/'components/read_pico_search/read_pico_search.c'),'-o',str(work/'test')],check=True)
 subprocess.run([str(work/'test'),str(work/'card')],check=True)

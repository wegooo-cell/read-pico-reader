# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：真实文件列表扫描/显示/删除长名；原路径和UTF-8扩展名保留。
# English: Actual file-list scan/display/delete with long names, retaining raw paths and UTF-8 extensions.
from pathlib import Path
import subprocess,tempfile,sys
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
s=root/'main/apps/app_files.c'
unit=r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_title.h"
#include "file_tree.h"
#define FILE_MAX 96
#define FILE_VIEW_MOVE 2
#define FILE_VIEW_LIST 1
typedef struct {char path[288];off_t size;int64_t modified;bool is_dir,selected;}file_item_t;
static file_item_t s_files[FILE_MAX],s_selected;
static int s_count,s_page,s_view=FILE_VIEW_LIST,s_counts[3],s_file_sort;
static bool s_file_desc;
static char s_file_query[65];
static void file_search_start(void){assert(!s_file_query[0]);}
static void file_search_stop(void){}
static void file_selection_restore(void){}
static char s_message[128],test_root[288];
static struct{bool mounted;uint64_t free_bytes;}s_sd;
static bool files_alloc(void){return true;}
static const char *folder_root(int n){(void)n;return test_root;}
static void read_pico_sd_get_info(void*p){(void)p;s_sd.mounted=true;s_sd.free_bytes=1024*1024;}
static void book_store_notify_changed(void){}
'''
for name in ['file_item_name','copy_utf8','is_book_path','file_display_name','sibling_path','copy_selected_item','supported','compare_files','scan_folder']:
    unit+=function(name,s)+'\n'

book=root/'main/apps/app_book.c'
text=book.read_text()
shelf_type=text[text.index('typedef struct {\n    char name[256];'):text.index('} shelf_entry_t;')+len('} shelf_entry_t;')]
unit+=r'''
#define BOOK_STORE_PATH_MAX 288
#define BOOK_SCAN_DEPTH_LIMIT 12
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define SHELF 0
typedef int nvs_handle_t;
typedef struct{int unused;}app_ctx_t;
typedef struct{uint32_t file_size,last_open_s;uint16_t chapter;uint8_t pct;}book_progress_t;
typedef struct{DIR*dir;char path[288];}shelf_scan_frame_t;
static void *heap_caps_calloc(size_t n,size_t z,int caps){assert(caps==3);return calloc(n,z);}
static bool app_poll_main_navigation(app_ctx_t*c){(void)c;return false;}
static const char*app_settings_books_dir(void){return test_root;}
static void shelf_category_add(const char*p){(void)p;}
static bool book_file_path_supported(const char*p){return is_book_path(p);}
static bool shelf_hidden_read_handle(int h,const char*p){(void)h;(void)p;return false;}
static bool favorite_read_handle(int h,const char*p){(void)h;(void)p;return false;}
static bool pico_boot_asset_allowed(const char*p){(void)p;return true;}
static bool shelf_reserve(void){return true;}
static bool book_progress_load(const char*p,uint32_t z,book_progress_t*out){(void)p;(void)z;(void)out;return false;}
static char s_query[16];
static bool read_pico_search_match(const char*t,const char*q){(void)t;(void)q;return true;}
static unsigned metadata_calls;static bool have_metadata;static const char *cached_author="Cached author";
static int book_epub_metadata_cached(const char*p,char*t,size_t n,char*a,size_t z){(void)p;(void)n;++metadata_calls;t[0]=0;snprintf(a,z,"%s",have_metadata?cached_author:"");return have_metadata?0:-1;}
typedef struct{int favorites,hidden;bool truncated;}shelf_backfill_t;
'''+shelf_type+'\nstatic shelf_entry_t s_shelf[8];static int shelf_count,shelf_view;\n'
if 'book_progress_load(' not in function('scan_shelf_dir',book):
    unit=unit.replace('static bool book_progress_load(const char*p,uint32_t z,book_progress_t*out){(void)p;(void)z;(void)out;return false;}','')
if 'book_epub_metadata_cached_for_file' in book.read_text():
    unit+='static int book_epub_metadata_cached_for_file(const char*p,uint64_t size,int64_t modified,char*t,size_t n,char*a,size_t z){(void)size;(void)modified;return book_epub_metadata_cached(p,t,n,a,z); }\n'
unit+='enum{EXPECTED_SCAN_STAT='+('4' if 'book_epub_metadata_cached_for_file' in function('scan_shelf_dir',book) else '7')+'};\n'
unit+='static int shelf_stat_calls;static int shelf_stat(const char*p,struct stat*s){++shelf_stat_calls;return stat(p,s); }\n#define stat(p,s) shelf_stat(p,s)\n'
for name in ['copy_text','clean_filename','scan_shelf_dir','shelf_backfill_visit']:
    unit+=function(name,book).replace('s_count','shelf_count').replace('s_view','shelf_view')+'\n'
unit+='\n#undef stat\n'
home=root/'main/apps/app_dashboard.c'
unit+='typedef struct{char path[288],title[256],author[128];}home_book_t;\n'+function('copy_title',home)+'\n'+function('title_for',home)+'\n'
unit+=r'''
int main(void){
 assert(supported(1,"picture.webp")&&supported(1,"picture.WEBP"));
 assert(!supported(0,"picture.webp")&&!supported(2,"picture.webp"));
 assert(!supported(1,"._picture.webp")&&!supported(1,".picture.webp"));
 char folder[]="/tmp/kn-XXXXXX";assert(mkdtemp(folder));strcpy(test_root,folder);
 const char*names[]={"沉睡的人 (乔治·佩雷克 Georges Perec) (z-library.sk, 1lib.sk, z-lib.sk).epub",
 "活山 ([英]娜恩·谢泼德 [[英]娜恩·谢泼德]) (z-library.sk, 1lib.sk, z-lib.sk).epub",
 "我们为何迷恋真实 = This Is Not Who I Am Our Authenticity Obsession ([英] 埃米莉・布特尔 (Emily Bootle) 著  马雅 译) (z-library.sk, 1lib.sk, z-lib.sk).epub"};
 for(unsigned i=0;i<3;++i){char path[288];int n=snprintf(path,sizeof(path),"%s/%s",folder,names[i]);assert(n>0&&n<(int)sizeof(path));FILE*f=fopen(path,"wb");assert(f);fputs("payload",f);fclose(f);}
 char longest[256];memset(longest,'x',250);strcpy(longest+250,".epub");
 char path[288];snprintf(path,sizeof(path),"%s/%s",folder,longest);FILE*f=fopen(path,"wb");assert(f);fclose(f);
 scan_folder(3);assert(s_count==4);
 const char*ignored[]={"cover.png","notes.pdf","archive.zip"};
 for(unsigned i=0;i<3;i++){snprintf(path,sizeof(path),"%s/%s",folder,ignored[i]);f=fopen(path,"wb");assert(f);fclose(f);}
 bool truncated=false,unreadable=false,skipped=false;app_ctx_t ctx={0};
 scan_shelf_dir(&ctx,folder,false,0,0,&truncated,&unreadable,&skipped);
 assert(shelf_count==4&&!truncated&&!unreadable&&!skipped&&metadata_calls==4);
 assert(shelf_stat_calls==EXPECTED_SCAN_STAT);
 for(unsigned i=0;i<3;i++){snprintf(path,sizeof(path),"%s/%s",folder,ignored[i]);assert(!unlink(path));}
 printf("PASS:4 books plus3 non-books: %d directory stat calls (Pico fast path4, retained S31 path7)\n",shelf_stat_calls);

 for(int i=0;i<shelf_count;++i)if(strstr(s_shelf[i].path,"活山"))assert(!strcmp(s_shelf[i].name,"活山")&&!strcmp(s_shelf[i].author,"娜恩·谢泼德"));
 shelf_count=0;shelf_backfill_t backfill={0};book_progress_t progress={.file_size=7};
 snprintf(path,sizeof(path),"%s/%s",folder,names[0]);
 assert(shelf_backfill_visit(path,&progress,&backfill)&&shelf_count==1&&!strcmp(s_shelf[0].name,"沉睡的人")&&!strcmp(s_shelf[0].author,"乔治·佩雷克"));
 shelf_count=0;have_metadata=true;assert(shelf_backfill_visit(path,&progress,&backfill)&&!strcmp(s_shelf[0].author,"乔治·佩雷克"));
 shelf_count=0;cached_author="【英】乔治·佩雷克 (Georges Perec)";
 assert(shelf_backfill_visit(path,&progress,&backfill)&&!strcmp(s_shelf[0].author,"乔治·佩雷克"));
 home_book_t current={0};snprintf(current.path,sizeof(current.path),"%s/%s",folder,names[1]);title_for(&current);assert(!strcmp(current.title,"活山")&&!strcmp(current.author,"娜恩·谢泼德"));

 bool saw_long=false;
 for(int i=0;i<s_count;++i){
  file_item_t item=s_files[i];char before[288],title[128],author[128];strcpy(before,item.path);
  file_display_name(&item,title,author);assert(!strcmp(before,item.path));
  assert(!strcmp(file_item_name(&item),strrchr(before,'/')+1));
  if(strstr(item.path,"活山"))assert(!strcmp(title,"活山")&&!strcmp(author,"娜恩·谢泼德"));
  if(strstr(item.path,"我们为何"))assert(!strcmp(title,"我们为何迷恋真实")&&!strcmp(author,"埃米莉·布特尔"));
  if(strlen(file_item_name(&item))==255){saw_long=true;assert(strlen(title)==127);}
  struct stat st;assert(!stat(item.path,&st));
  assert(file_tree_delete(item.path,NULL,NULL)==0);assert(stat(item.path,&st)!=0);
 }
 assert(saw_long);
 char utf_name[256];for(unsigned i=0;i<80;++i)memcpy(utf_name+3*i,"书",3);strcpy(utf_name+240,".epub");
 snprintf(s_selected.path,sizeof(s_selected.path),"%s/%s",folder,utf_name);s_selected.is_dir=false;
 f=fopen(s_selected.path,"wb");assert(f);fputs("payload",f);fclose(f);
 char copied[128];assert(copy_selected_item(copied)==0);
 assert(strlen(copied)<sizeof(copied));
 // 显示提示不能用来重建真实复制目标；遍历目录核对完整副本名。/ Never rebuild a copy path from its clipped UI label.
 char clone[288]={0};DIR *copies=opendir(folder);assert(copies);struct dirent *entry;unsigned matches=0;
 while((entry=readdir(copies))){if(!strstr(entry->d_name," 副本.epub"))continue;
  const char *suffix=strstr(entry->d_name," 副本.epub");assert((suffix-entry->d_name)%3==0&&suffix-entry->d_name>=99);
  int n=snprintf(clone,sizeof(clone),"%s/%s",folder,entry->d_name);assert(n>0&&(size_t)n<sizeof(clone));++matches;
 }
 closedir(copies);assert(matches==1);assert(!file_tree_delete(clone,NULL,NULL));assert(!file_tree_delete(s_selected.path,NULL,NULL));
 assert(!rmdir(folder));
 puts("PASS: actual file/shelf scans, history backfill, cached-author precedence and Home labels; 3 supplied names and 255-byte basename retain real paths and delete successfully");
}
'''
with tempfile.TemporaryDirectory() as folder:
    c=Path(folder)/'test.c';b=Path(folder)/'test';c.write_text(unit)
    (Path(folder)/'esp_err.h').write_text((root/'tools/book_storage_stubs/esp_err.h').read_text()+'\n#define ESP_ERR_INVALID_SIZE 6\n#define ESP_ERR_NOT_SUPPORTED 7\n#define ESP_ERR_NOT_FOUND 8\n#define ESP_ERR_NO_MEM 9\n')
    (Path(folder)/'nvs.h').write_text((root/'tools/book_storage_stubs/nvs.h').read_text())
    gc=['-Wl,-dead_strip']if sys.platform=='darwin'else['-Wl,--gc-sections']
    subprocess.run(['cc','-std=gnu11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections',*gc,'-I'+folder,'-Imain/book','-Imain/apps','-Itools/book_storage_stubs','-Itools/file_tree_stubs',str(c),'main/book/book_title.c','main/apps/file_tree.c','-o',str(b)],cwd=root,check=True)
    subprocess.run([str(b)],check=True)
    subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections',*gc,'-Imain/book','-Itools/book_storage_stubs','tools/book_name_host_test.c','main/book/book_title.c','-o',str(b)],cwd=root,check=True)
    subprocess.run([str(b)],check=True)

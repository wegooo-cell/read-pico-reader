/* SPDX-License-Identifier: Apache-2.0
 * Bounded selection and checked recursive enrollment. Never edit source books/progress.
 */
#include "book_import.h"
#include "book_library.h"
#include "book_store.h"
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif
#define IMPORT_LIMIT 8192u
#define IMPORT_DEPTH 12
#define IMPORT_NODES 131072u
typedef struct {char *path;bool directory;} choice_t;
typedef struct {DIR *dir;char path[BOOK_STORE_PATH_MAX];} frame_t;
static choice_t *s_choices;
static unsigned s_count,s_capacity,s_cursor,s_nodes,s_books,s_failed;
static char **s_hidden;
static unsigned s_hidden_count,s_hidden_capacity;
static frame_t *s_frames;
static int s_depth=-1;
static bool s_running;
static bool (*s_is_hidden)(const char *),(*s_unhide)(const char *);
static void *import_resize(void *old,size_t size){
#ifdef ESP_PLATFORM
 return heap_caps_realloc(old,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 return realloc(old,size);
#endif
}
static char *import_copy(const char *path){
 size_t bytes=strlen(path)+1;char *copy=import_resize(NULL,bytes);if(copy)memcpy(copy,path,bytes);return copy;
}
static unsigned choice_at(const char *path){
 unsigned lo=0,hi=s_count;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(strcmp(s_choices[mid].path,path)<0)lo=mid+1;else hi=mid;}
 return lo;
}
static bool choice_exact(const char *path,unsigned *index){
 unsigned at=choice_at(path);if(index)*index=at;return at<s_count&&!strcmp(s_choices[at].path,path);
}
book_import_selection_t book_import_selection(const char *path){
 if(!path||!*path)return BOOK_IMPORT_NONE;
 char parent[BOOK_STORE_PATH_MAX];size_t n=strlen(path);if(n>=sizeof(parent))return BOOK_IMPORT_NONE;
 memcpy(parent,path,n+1);
 for(char *slash=strrchr(parent,'/');slash&&slash!=parent;slash=strrchr(parent,'/')){
  *slash=0;unsigned at;if(choice_exact(parent,&at)&&s_choices[at].directory)return BOOK_IMPORT_COVERED;
 }
 return choice_exact(path,NULL)?BOOK_IMPORT_EXPLICIT:BOOK_IMPORT_NONE;
}
esp_err_t book_import_select(const char *path,bool directory,bool selected){
 if(s_running)return ESP_ERR_INVALID_STATE;
 if(!path||!*path||strlen(path)>=BOOK_STORE_PATH_MAX)return ESP_ERR_INVALID_ARG;
 unsigned at;bool found=choice_exact(path,&at);
 if(!selected){if(found){free(s_choices[at].path);memmove(s_choices+at,s_choices+at+1,(s_count-at-1)*sizeof(*s_choices));--s_count;}return ESP_OK;}
 if(found||book_import_selection(path)==BOOK_IMPORT_COVERED)return ESP_OK;
 if(s_count==IMPORT_LIMIT)return ESP_ERR_NO_MEM;
 char *copy=import_copy(path);if(!copy)return ESP_ERR_NO_MEM;
 if(s_count==s_capacity){unsigned cap=s_capacity?s_capacity+32:16;if(cap>IMPORT_LIMIT)cap=IMPORT_LIMIT;
  void *next=import_resize(s_choices,cap*sizeof(*s_choices));if(!next){free(copy);return ESP_ERR_NO_MEM;}s_choices=next;s_capacity=cap;
 }
 memmove(s_choices+at+1,s_choices+at,(s_count-at)*sizeof(*s_choices));s_choices[at]=(choice_t){copy,directory};++s_count;return ESP_OK;
}
unsigned book_import_choice_count(void){return s_count;}
static void walk_close(void){
 for(int i=0;i<=s_depth;++i)if(s_frames[i].dir)closedir(s_frames[i].dir);
 free(s_frames);s_frames=NULL;s_depth=-1;
 for(unsigned i=0;i<s_hidden_count;++i)free(s_hidden[i]);
 free(s_hidden);s_hidden=NULL;s_hidden_count=s_hidden_capacity=0;
}
void book_import_cancel(void){if(s_running)book_library_abort();s_running=false;walk_close();}
void book_import_reset(void){
 book_import_cancel();
 for(unsigned i=0;i<s_count;++i)free(s_choices[i].path);
 free(s_choices);s_choices=NULL;s_count=s_capacity=0;s_books=s_failed=0;
}
bool book_import_running(void){return s_running;}
unsigned book_import_result_count(void){return s_books;}
unsigned book_import_unhide_failures(void){return s_failed;}
esp_err_t book_import_begin(bool (*hidden)(const char *),bool (*unhide)(const char *)){
 if(s_running||!s_count)return ESP_ERR_INVALID_STATE;
 s_frames=import_resize(NULL,(IMPORT_DEPTH+1)*sizeof(*s_frames));if(!s_frames)return ESP_ERR_NO_MEM;
 memset(s_frames,0,(IMPORT_DEPTH+1)*sizeof(*s_frames));s_depth=-1;
 esp_err_t error=book_library_begin_snapshot();if(error!=ESP_OK){walk_close();return error;}
 s_running=true;s_cursor=s_nodes=s_books=s_failed=0;s_is_hidden=hidden;s_unhide=unhide;return ESP_OK;
}
static esp_err_t remember_hidden(const char *path){
 if(!s_is_hidden||!s_is_hidden(path))return ESP_OK;
 if(s_hidden_count==s_hidden_capacity){unsigned cap=s_hidden_capacity?s_hidden_capacity+32:16;
  void *next=import_resize(s_hidden,cap*sizeof(*s_hidden));if(!next)return ESP_ERR_NO_MEM;s_hidden=next;s_hidden_capacity=cap;
 }
 char *copy=import_copy(path);if(!copy)return ESP_ERR_NO_MEM;s_hidden[s_hidden_count++]=copy;return ESP_OK;
}
static int import_stat(const char *path,struct stat *st){
#ifdef ESP_PLATFORM
 return stat(path,st);
#else
 return lstat(path,st);
#endif
}
static esp_err_t visit_path(const char *path,bool directory,bool verify_type){
 struct stat st;if(import_stat(path,&st))return ESP_FAIL;
 if(verify_type&&directory!=(bool)S_ISDIR(st.st_mode))return ESP_ERR_INVALID_STATE;
 if(S_ISDIR(st.st_mode)){
  if(s_depth==IMPORT_DEPTH)return ESP_ERR_INVALID_SIZE;
  DIR *dir=opendir(path);if(!dir)return ESP_FAIL;
  frame_t *frame=&s_frames[++s_depth];frame->dir=dir;memcpy(frame->path,path,strlen(path)+1);return ESP_OK;
 }
 if(!S_ISREG(st.st_mode))return ESP_OK;
 const char *ext=strrchr(path,'.');if(!ext||(strcasecmp(ext,".txt")&&strcasecmp(ext,".epub")))return ESP_OK;
 esp_err_t error=book_library_add(path);if(error==ESP_OK)error=remember_hidden(path);if(error==ESP_OK)++s_books;return error;
}
esp_err_t book_import_step(unsigned budget){
 if(!s_running||!budget)return ESP_ERR_INVALID_STATE;
#ifdef ESP_PLATFORM
 int64_t deadline=esp_timer_get_time()+16000;
#endif
 esp_err_t error=ESP_OK;
 while(budget--&&error==ESP_OK){
  if(s_depth<0){
   if(s_cursor==s_count){
    error=s_books?book_library_commit():ESP_ERR_NOT_FOUND;
    if(error==ESP_OK){for(unsigned i=0;i<s_hidden_count;++i)if(!s_unhide||!s_unhide(s_hidden[i]))++s_failed;}
    else book_library_abort();
    s_running=false;walk_close();return error;
   }
   choice_t *choice=&s_choices[s_cursor++];
   if(book_import_selection(choice->path)==BOOK_IMPORT_COVERED)continue;
   error=visit_path(choice->path,choice->directory,true);
  }else{
   errno=0;struct dirent *entry=readdir(s_frames[s_depth].dir);
   if(!entry){if(errno)error=ESP_FAIL;if(closedir(s_frames[s_depth].dir))error=ESP_FAIL;s_frames[s_depth--].dir=NULL;}
   else if(entry->d_name[0]!='.'){
    if(++s_nodes>IMPORT_NODES){error=ESP_ERR_INVALID_SIZE;break;}
    char path[BOOK_STORE_PATH_MAX];int n=snprintf(path,sizeof(path),"%s/%s",s_frames[s_depth].path,entry->d_name);
    error=n<0||(size_t)n>=sizeof(path)?ESP_ERR_INVALID_SIZE:visit_path(path,false,false);
   }
  }
#ifdef ESP_PLATFORM
  if(esp_timer_get_time()>=deadline)break;
#endif
 }
 if(error!=ESP_OK){book_import_cancel();return error;}
 return ESP_ERR_NOT_FINISHED;
}

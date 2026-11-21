/* SPDX-License-Identifier: Apache-2.0
 * Checked, portable bookshelf membership, with a previous generation and bounded RAM.
 * An explicit save never deletes books and never formats or remounts a medium.
 */
#include "book_library.h"
#include "book_store.h"
#include "setup_state.h"
#include "read_pico_sd.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#define LIBRARY_LIMIT 8192u
#define LIBRARY_PATH_MAX BOOK_STORE_PATH_MAX
#ifndef LIBRARY_SD_ROOT
#define LIBRARY_SD_ROOT "/sdcard"
#endif
#ifndef LIBRARY_FLASH_ROOT
#define LIBRARY_FLASH_ROOT "/flash"
#endif
typedef struct {uint32_t hash;char path[LIBRARY_PATH_MAX];} library_entry_t;
static library_entry_t *s_entries;
#define LOOKUP_SLOTS (LIBRARY_LIMIT * 2u)
static uint16_t *s_lookup;
static unsigned s_count,s_capacity;
static book_library_mode_t s_mode;
static esp_err_t s_error;
static bool s_loaded,s_transaction,s_fresh;
static char s_medium[LIBRARY_PATH_MAX],s_file[LIBRARY_PATH_MAX+64];
static uint32_t hash_bytes(const void *data,size_t bytes){
 const uint8_t *p=data;uint32_t h=UINT32_C(2166136261);
 for(size_t i=0;i<bytes;++i)h=(h^p[i])*UINT32_C(16777619);
 return h;
}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool valid_path(const char *path){
 if(!path||strnlen(path,LIBRARY_PATH_MAX)==LIBRARY_PATH_MAX)return false;
 if(strncmp(path,LIBRARY_SD_ROOT "/",sizeof(LIBRARY_SD_ROOT))&&strncmp(path,LIBRARY_FLASH_ROOT "/books/",sizeof(LIBRARY_FLASH_ROOT)+6))return false;
 for(const char *p=path;*p;++p)if((unsigned char)*p<32||*p=='\\')return false;
 for(const char *part=path+1;*part;){
  const char *end=strchr(part,'/');size_t len=end?(size_t)(end-part):strlen(part);
  if(!len||(len==1&&part[0]=='.')||(len==2&&part[0]=='.'&&part[1]=='.'))return false;
  if(!end)break;
  part=end+1;
  if(!*part)return false;
 }
 return true;
}
static bool reserve(unsigned n){
 if(n>s_capacity){
  unsigned cap=s_capacity?(s_capacity<1024?s_capacity*2:s_capacity+128):32;while(cap<n)cap=cap<1024?cap*2:cap+128;if(cap>LIBRARY_LIMIT)cap=LIBRARY_LIMIT;
  if(n>cap)return false;
#ifdef ESP_PLATFORM
  void *next=heap_caps_realloc(s_entries,(size_t)cap*sizeof(*s_entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  void *next=realloc(s_entries,(size_t)cap*sizeof(*s_entries));
#endif
  if(!next)return false;
  s_entries=next;s_capacity=cap;
 }
 return true;
}
static bool lookup_prepare(void){
 if(s_lookup)return true;
#ifdef ESP_PLATFORM
 s_lookup=heap_caps_calloc(LOOKUP_SLOTS,sizeof(*s_lookup),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 s_lookup=calloc(LOOKUP_SLOTS,sizeof(*s_lookup));
#endif
 return s_lookup!=NULL;
}
static unsigned lookup_slot(const char *path,uint32_t hash){
 unsigned at=hash&(LOOKUP_SLOTS-1);
 while(s_lookup[at]){
  const library_entry_t *entry=&s_entries[s_lookup[at]-1];
  if(entry->hash==hash&&!strcmp(entry->path,path))break;
  at=(at+1)&(LOOKUP_SLOTS-1);
 }
 return at;
}
static bool lookup_build(void){
 if(!s_count){if(s_lookup)memset(s_lookup,0,LOOKUP_SLOTS*sizeof(*s_lookup));return true;}
 if(!lookup_prepare())return false;
 memset(s_lookup,0,LOOKUP_SLOTS*sizeof(*s_lookup));
 for(unsigned i=0;i<s_count;++i)s_lookup[lookup_slot(s_entries[i].path,s_entries[i].hash)]=(uint16_t)(i+1);
 return true;
}
static bool compact_entries(void){
 unsigned kept=0;
 for(unsigned i=0;i<s_count;++i)if(s_entries[i].path[0]){if(kept!=i)s_entries[kept]=s_entries[i];++kept;}
 s_count=kept;return lookup_build();
}
void book_library_invalidate(void){free(s_entries);free(s_lookup);s_entries=NULL;s_lookup=NULL;s_count=s_capacity=0;s_loaded=s_transaction=s_fresh=false;s_mode=BOOK_LIBRARY_UNSET;s_error=ESP_OK;}
static esp_err_t read_index(const char *path,bool admit){
 FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT?ESP_ERR_NOT_FOUND:ESP_FAIL;
 uint8_t head[20];esp_err_t err=ESP_ERR_INVALID_RESPONSE;
 if(fread(head,1,sizeof(head),f)!=sizeof(head)||memcmp(head,"KLIB01\0",8)||head[8]>2||head[9]||head[10]||head[11]||get32(head+16)!=hash_bytes(head,16))goto done;
 uint32_t count=get32(head+12);if(count>LIBRARY_LIMIT||(head[8]==BOOK_LIBRARY_UNSET&&count))goto done;
 if(admit){s_count=0;s_mode=(book_library_mode_t)head[8];if(s_mode==BOOK_LIBRARY_SELECTED&&!reserve(count)){err=ESP_ERR_NO_MEM;goto done;}}
 for(uint32_t i=0;i<count;++i){
  uint8_t size[2],seal[4];char value[LIBRARY_PATH_MAX];
  if(fread(size,1,2,f)!=2)goto done;
  unsigned len=(unsigned)size[0]|(unsigned)size[1]<<8;
  if(len<2||len>sizeof(value)||fread(value,1,len,f)!=len||value[len-1]||strnlen(value,len)!=len-1||!valid_path(value)||fread(seal,1,4,f)!=4||get32(seal)!=hash_bytes(value,len))goto done;
  if(admit&&s_mode==BOOK_LIBRARY_SELECTED){s_entries[s_count].hash=hash_bytes(value,len-1);memcpy(s_entries[s_count++].path,value,len);}
 }
 if(fgetc(f)!=EOF||ferror(f))goto done;
 err=admit&&s_mode==BOOK_LIBRARY_SELECTED&&!lookup_build()?ESP_ERR_NO_MEM:ESP_OK;
done: if(fclose(f)&&err==ESP_OK)err=ESP_FAIL;return err;
}
static esp_err_t load(void){
 read_pico_sd_info_t sd={0};esp_err_t status=read_pico_sd_get_info(&sd);
 if(status==ESP_ERR_NOT_FINISHED || (sd.present&&!sd.mounted))return s_error=status==ESP_OK?ESP_ERR_INVALID_STATE:status;
 const char *root=sd.mounted?LIBRARY_SD_ROOT:LIBRARY_FLASH_ROOT;
 bool fresh=app_setup_library_pending();
 if(s_loaded&&s_fresh==fresh&&!strcmp(s_medium,root))return s_error=ESP_OK;
 book_library_invalidate();snprintf(s_medium,sizeof(s_medium),"%s",root);
 if(!sd.mounted){status=book_store_prepare_flash();if(status!=ESP_OK)return s_error=status;}
 snprintf(s_file,sizeof(s_file),"%s/.kiikoread/bookshelf-v1",root);
 s_fresh=fresh;
 if(fresh){s_loaded=true;return s_error=ESP_OK;}
 esp_err_t first=read_index(s_file,true);
 if(first!=ESP_OK){
  char previous[sizeof(s_file)+16];snprintf(previous,sizeof(previous),"%s.previous",s_file);
  esp_err_t second=read_index(previous,true);
  if(second!=ESP_OK){
   s_count=0;s_mode=BOOK_LIBRARY_UNSET;
   if(first!=ESP_ERR_NOT_FOUND||second!=ESP_ERR_NOT_FOUND)return s_error=first==ESP_ERR_NO_MEM?first:ESP_ERR_INVALID_RESPONSE;
   // Existing users already have an added-order journal. Retain their established all-books library.
   char legacy[LIBRARY_PATH_MAX+32];struct stat st;
   const char *markers[]={".kiiko-added-v1",".kiikoread/shelf.idx"};
   for(unsigned i=0;i<2;++i){snprintf(legacy,sizeof(legacy),"%s/%s",root,markers[i]);
    if(!stat(legacy,&st)&&S_ISREG(st.st_mode)&&st.st_size>0)s_mode=BOOK_LIBRARY_ALL;
   }
  }
 }
 s_loaded=true;return s_error=ESP_OK;
}
book_library_mode_t book_library_mode(void){(void)load();return s_mode;}
esp_err_t book_library_error(void){return s_error;}
bool book_library_contains(const char *path){
 if(!valid_path(path)||load()!=ESP_OK)return false;
 if(s_mode==BOOK_LIBRARY_ALL)return true;
 if(!s_count||!s_lookup)return false;
 uint32_t h=hash_bytes(path,strlen(path));
 return s_lookup[lookup_slot(path,h)]!=0;
}
esp_err_t book_library_begin(bool all){
 esp_err_t err=load();if(err!=ESP_OK)return err;
 if(all){s_mode=BOOK_LIBRARY_ALL;s_count=0;}
 else if(s_mode!=BOOK_LIBRARY_ALL)s_mode=BOOK_LIBRARY_SELECTED;
 s_transaction=true;return ESP_OK;
}
esp_err_t book_library_begin_snapshot(void){
 esp_err_t err=book_library_begin(false);if(err!=ESP_OK)return err;
 if(s_mode==BOOK_LIBRARY_ALL){s_mode=BOOK_LIBRARY_SELECTED;s_count=0;free(s_lookup);s_lookup=NULL;}
 return ESP_OK;
}
esp_err_t book_library_add(const char *path){
 if(!s_transaction||!valid_path(path))return ESP_ERR_INVALID_ARG;
 if(s_mode==BOOK_LIBRARY_ALL||book_library_contains(path))return ESP_OK;
 if(s_count>=LIBRARY_LIMIT&&!compact_entries())return ESP_ERR_NO_MEM;
 if(!reserve(s_count+1)||!lookup_prepare())return ESP_ERR_NO_MEM;
 uint32_t hash=hash_bytes(path,strlen(path));unsigned at=lookup_slot(path,hash);
 s_entries[s_count].hash=hash;snprintf(s_entries[s_count].path,LIBRARY_PATH_MAX,"%s",path);
 s_lookup[at]=(uint16_t)(++s_count);return ESP_OK;
}
esp_err_t book_library_commit(void){
 if(!s_transaction)return ESP_ERR_INVALID_STATE;
 if(!compact_entries())return ESP_ERR_NO_MEM;
 char folder[LIBRARY_PATH_MAX+16];snprintf(folder,sizeof(folder),"%s/.kiikoread",s_medium);
 if(mkdir(folder,0777)&&errno!=EEXIST)return ESP_FAIL;
 char temp[sizeof(s_file)+16],previous[sizeof(s_file)+16];snprintf(temp,sizeof(temp),"%s.tmp",s_file);snprintf(previous,sizeof(previous),"%s.previous",s_file);
 FILE *f=fopen(temp,"wb");if(!f)return ESP_FAIL;
 uint8_t head[20]={0};memcpy(head,"KLIB01\0",8);head[8]=(uint8_t)s_mode;put32(head+12,s_count);put32(head+16,hash_bytes(head,16));
 bool ok=fwrite(head,1,sizeof(head),f)==sizeof(head);
 for(unsigned i=0;ok&&i<s_count;++i){
  unsigned len=strlen(s_entries[i].path)+1;uint8_t size[2]={(uint8_t)len,(uint8_t)(len>>8)},seal[4];put32(seal,hash_bytes(s_entries[i].path,len));
  ok=fwrite(size,1,2,f)==2&&fwrite(s_entries[i].path,1,len,f)==len&&fwrite(seal,1,4,f)==4;
 }
 if(ok)ok=fflush(f)==0&&fsync(fileno(f))==0;
 if(fclose(f))ok=false;
 if(!ok||read_index(temp,false)!=ESP_OK){unlink(temp);return ESP_FAIL;}
 bool rotated=false;
 if(read_index(s_file,false)==ESP_OK){
  if(unlink(previous)&&errno!=ENOENT)return ESP_FAIL;
  if(rename(s_file,previous))return ESP_FAIL;
  rotated=true;
 }else if(unlink(s_file)&&errno!=ENOENT)return ESP_FAIL;
 if(rename(temp,s_file)){if(rotated)(void)rename(previous,s_file);return ESP_FAIL;}
 s_transaction=false;
 if(s_mode==BOOK_LIBRARY_ALL){free(s_entries);free(s_lookup);s_entries=NULL;s_lookup=NULL;s_capacity=0;}
 book_store_notify_changed();
 if(s_fresh&&s_mode!=BOOK_LIBRARY_UNSET){esp_err_t err=app_setup_library_complete();if(err!=ESP_OK){book_library_invalidate();return err;}s_fresh=false;}
 return ESP_OK;
}
void book_library_abort(void){book_library_invalidate();}
esp_err_t book_library_reset(void){
 esp_err_t err=book_library_begin(false);if(err!=ESP_OK)return err;
 s_mode=BOOK_LIBRARY_UNSET;s_count=0;
 err=book_library_commit();if(err!=ESP_OK)book_library_abort();
 return err;
}
esp_err_t book_library_include(const char *path){
 if(book_library_contains(path))return ESP_OK;
 esp_err_t err=book_library_begin(false);if(err==ESP_OK)err=book_library_add(path);if(err==ESP_OK)err=book_library_commit();
 if(err!=ESP_OK)book_library_abort();
 return err;
}

esp_err_t book_library_list(book_library_visit_fn visit,void *context){
 if(!visit)return ESP_ERR_INVALID_ARG;
 esp_err_t error=load();if(error!=ESP_OK)return error;
 if(s_mode!=BOOK_LIBRARY_SELECTED)return ESP_OK;
 for(unsigned i=0;i<s_count;++i){
  char path[LIBRARY_PATH_MAX];snprintf(path,sizeof(path),"%s",s_entries[i].path);
  if(path[0]&&!visit(path,context))break;
  if(!s_loaded)return ESP_ERR_NOT_FINISHED;
 }
 return ESP_OK;
}

esp_err_t book_library_remove(const char *path){
 if(!s_transaction||!valid_path(path)||s_mode!=BOOK_LIBRARY_SELECTED)return ESP_ERR_INVALID_ARG;
 if(!book_library_contains(path))return s_error;
 unsigned index=s_lookup[lookup_slot(path,hash_bytes(path,strlen(path)))]-1;
 // Keep the hash-chain occupied until the one-pass commit compacts all removals.
 s_entries[index].path[0]=0;return ESP_OK;
}
esp_err_t book_library_move(const char *old_path,const char *new_path){
 if(!valid_path(old_path)||!valid_path(new_path))return ESP_ERR_INVALID_ARG;
 if(!strcmp(old_path,new_path))return ESP_OK;
 if(!book_library_contains(old_path))return s_error;
 if(s_mode==BOOK_LIBRARY_ALL)return ESP_OK;
 esp_err_t error=book_library_begin(false);
 if(error==ESP_OK){
  unsigned index=s_lookup[lookup_slot(old_path,hash_bytes(old_path,strlen(old_path)))]-1;
  if(book_library_contains(new_path))s_entries[index].path[0]=0;
  else{snprintf(s_entries[index].path,LIBRARY_PATH_MAX,"%s",new_path);s_entries[index].hash=hash_bytes(new_path,strlen(new_path));}
  error=book_library_commit();
 }
 if(error!=ESP_OK)book_library_abort();
 return error;
}

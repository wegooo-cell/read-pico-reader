/* SPDX-License-Identifier: Apache-2.0 */
/* Actual settings/history/progress codecs and durable stores, with injected NVS failures. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "nvs.h"
#include <errno.h>
#include <unistd.h>
static int rename_calls,fail_rename;
static int test_card_rename(const char *from,const char *to);
#define rename test_card_rename
#include SETTINGS_SOURCE
#undef rename
// ESP-IDF forwards rename to FatFs, which returns FR_EXIST for an existing destination.
static int test_card_rename(const char *from,const char *to){
    if(++rename_calls==fail_rename){fail_rename=0;errno=EIO;return -1;}
    struct stat st;if(!stat(to,&st)){errno=EEXIST;return -1;}return rename(from,to);
}
#include "book/book_progress.h"
#include "book/book_store.h"

typedef struct {int ns;nvs_type_t type;char key[16];size_t len;uint8_t data[4096];} test_record_t;
static test_record_t db[1024],saved[1024];static unsigned count,saved_count;
static char ns_names[16][16];static unsigned ns_count;
static int writes,commits,fail_write,fail_commit;static bool mounted=true,primary,persistent_commit_failure;
struct nvs_iterator_opaque {int ns,index;nvs_type_t type;};
static int ns_index(const char *name,bool create){
    for(unsigned i=0;i<ns_count;++i)if(!strcmp(name,ns_names[i]))return (int)i;
    if(!create)return -1;assert(ns_count<16);strcpy(ns_names[ns_count],name);return (int)ns_count++;
}
static test_record_t *lookup(int ns,const char *key){
    for(unsigned i=0;i<count;++i)if(db[i].ns==ns&&!strcmp(db[i].key,key))return db+i;return NULL;
}
static esp_err_t put_value(nvs_handle_t h,const char *key,nvs_type_t type,const void *value,size_t len){
    if(++writes==fail_write){fail_write=0;return ESP_ERR_NVS_NOT_ENOUGH_SPACE;}
    test_record_t *r=lookup(h-1,key);if(!r){assert(count<1024);r=db+count++;memset(r,0,sizeof(*r));r->ns=h-1;strcpy(r->key,key);}
    assert(len<=sizeof(r->data));r->type=type;r->len=len;memcpy(r->data,value,len);return ESP_OK;
}
static esp_err_t get_value(nvs_handle_t h,const char *key,nvs_type_t type,void *value,size_t *len){
    test_record_t *r=lookup(h-1,key);if(!r||r->type!=type)return ESP_ERR_NVS_NOT_FOUND;
    if(value&&*len<r->len)return ESP_ERR_INVALID_SIZE;if(value)memcpy(value,r->data,r->len);*len=r->len;return ESP_OK;
}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){int id=ns_index(name,mode==NVS_READWRITE);if(id<0)return ESP_ERR_NVS_NOT_FOUND;*h=id+1;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t nvs_flash_init(void){return ESP_OK;}
void app_setup_init(void){}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;if(persistent_commit_failure)return ESP_FAIL;if(++commits==fail_commit){fail_commit=0;return ESP_FAIL;}return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){
    test_record_t *r=lookup(h-1,key);if(!r)return ESP_ERR_NVS_NOT_FOUND;
    memmove(r,r+1,(size_t)(db+--count-r)*sizeof(*r));return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t h){(void)h;assert(!"configuration restore must not erase namespaces");return ESP_FAIL;}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v){size_t n=1;return get_value(h,key,NVS_TYPE_U8,v,&n);}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v){return put_value(h,key,NVS_TYPE_U8,&v,1);}
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *v){size_t n=4;return get_value(h,key,NVS_TYPE_U32,v,&n);}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t v){return put_value(h,key,NVS_TYPE_U32,&v,4);}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *v,size_t *n){return get_value(h,key,NVS_TYPE_STR,v,n);}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *v){return put_value(h,key,NVS_TYPE_STR,v,strlen(v)+1);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *v,size_t *n){return get_value(h,key,NVS_TYPE_BLOB,v,n);}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *v,size_t n){return put_value(h,key,NVS_TYPE_BLOB,v,n);}
static esp_err_t iterator_seek(nvs_iterator_t *it,int start){
    for(unsigned i=(unsigned)start;i<count;++i)if(db[i].ns==(*it)->ns&&((*it)->type==NVS_TYPE_ANY||db[i].type==(*it)->type)){(*it)->index=(int)i;return ESP_OK;}
    free(*it);*it=NULL;return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_find_in_handle(nvs_handle_t h,nvs_type_t type,nvs_iterator_t *it){*it=malloc(sizeof(**it));assert(*it);**it=(struct nvs_iterator_opaque){h-1,0,type};return iterator_seek(it,0);}
esp_err_t nvs_entry_find(const char *part,const char *ns,nvs_type_t type,nvs_iterator_t *it){(void)part;int id=ns_index(ns,false);if(id<0){*it=NULL;return ESP_ERR_NVS_NOT_FOUND;}return nvs_entry_find_in_handle(id+1,type,it);}
esp_err_t nvs_entry_next(nvs_iterator_t *it){return iterator_seek(it,(*it)->index+1);}
esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t *out){memset(out,0,sizeof(*out));strcpy(out->key,db[it->index].key);strcpy(out->namespace_name,ns_names[it->ns]);out->type=db[it->index].type;return ESP_OK;}
void nvs_release_iterator(nvs_iterator_t it){free(it);}
esp_err_t nvs_get_stats(const char *partition,nvs_stats_t *stats){(void)partition;stats->available_entries=512;return ESP_OK;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info){info->mounted=mounted;return ESP_OK;}
bool book_store_card_ready(void){return mounted;}
bool book_store_flash_ready(void){return primary;}
esp_err_t book_store_prepare_flash(void){return primary?ESP_OK:ESP_FAIL;}

static nvs_handle_t wifi_handle(void){nvs_handle_t h;assert(nvs_open(KIIKO_BOARD_METALIO47?"metalio_wifi":"rp_wifi",NVS_READWRITE,&h)==ESP_OK);return h;}
esp_err_t read_pico_transfer_export_wifi_backup(read_pico_transfer_wifi_backup_t *out){memset(out,0,sizeof(*out));size_t n=sizeof(*out);esp_err_t err=nvs_get_blob(wifi_handle(),"creds",out,&n);return err==ESP_ERR_NVS_NOT_FOUND?ESP_OK:err;}
bool read_pico_transfer_wifi_backup_valid(const read_pico_transfer_wifi_backup_t *b){return b&&b->configured<=1&&strnlen(b->ssid,sizeof(b->ssid))<sizeof(b->ssid)&&strnlen(b->password,sizeof(b->password))<sizeof(b->password);}
esp_err_t read_pico_transfer_import_wifi_backup(const read_pico_transfer_wifi_backup_t *b){if(!read_pico_transfer_wifi_backup_valid(b))return ESP_ERR_INVALID_ARG;esp_err_t err=nvs_set_blob(wifi_handle(),"creds",b,sizeof(*b));return err==ESP_OK?nvs_commit(wifi_handle()):err;}

static const char *a="/sdcard/books/legacy.epub",*b="/sdcard/books/unmentioned.txt";
static void save_progress_at(const char *path,unsigned percent){book_progress_t p={.file_size=10000,.byte_off=percent*100,.pct=percent,.px=40};assert(book_progress_save(path,&p)==ESP_OK);}
static void assert_progress(const char *path,unsigned percent){book_progress_t p;esp_err_t err=book_progress_load_checked(path,10000,&p);if(err!=ESP_OK||p.pct!=percent)fprintf(stderr,"%s: expected %u, got %u err=%d\n",path,percent,p.pct,err);assert(err==ESP_OK&&p.pct==percent);}
static void assert_database_saved(void){
    assert(count==saved_count);for(unsigned i=0;i<saved_count;++i){test_record_t *r=lookup(saved[i].ns,saved[i].key);assert(r&&r->type==saved[i].type&&r->len==saved[i].len&&!memcmp(r->data,saved[i].data,r->len));}
}
static uint8_t *load_bytes(const char *path,size_t *len){FILE *f=fopen(path,"rb");assert(f&&!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0&&!fseek(f,0,SEEK_SET));uint8_t *data=malloc((size_t)n);assert(data&&fread(data,1,(size_t)n,f)==(size_t)n&&!fclose(f));*len=(size_t)n;return data;}
int main(int argc,char **argv){
    assert(argc>=3);app_settings_init();assert(!app_settings_low_ghost());
    app_settings_set_low_ghost(true);app_settings_init();assert(app_settings_low_ghost());
    app_settings_set_low_ghost(false);app_settings_init();assert(!app_settings_low_ghost());app_settings_set_book_px(120);app_settings_set_book_line_spacing(300);app_settings_set_book_paragraph_spacing(-100);
    app_settings_set_clock_12h(true);app_settings_set_low_ghost(false);assert(app_settings_set_shelf_covers(false));
    assert(app_settings_try_book_layout(120,24,300,-100,18));save_progress_at(a,82);
    nvs_handle_t favorite_handle,stats_handle;
    assert(nvs_open(KIIKO_BOARD_METALIO47?"mt_favs":"rp_favs",NVS_READWRITE,&favorite_handle)==ESP_OK);
    assert(nvs_set_u8(favorite_handle,"f_deadbeef",1)==ESP_OK);
    assert(nvs_open(KIIKO_BOARD_METALIO47?"mt_ticket":"rp_ticket",NVS_READWRITE,&stats_handle)==ESP_OK);
    uint8_t statistics[32]={0};uint32_t magic=UINT32_C(0x52505431);memcpy(statistics,&magic,4);statistics[8]=77;
    assert(nvs_set_blob(stats_handle,"stats",statistics,sizeof(statistics))==ESP_OK);
    read_pico_transfer_wifi_backup_t wifi={.configured=1,.ssid="Home",.password="password123"};assert(read_pico_transfer_import_wifi_backup(&wifi)==ESP_OK);
    if(!strcmp(argv[1],"export")){
        uint8_t *data=NULL;size_t n=0;assert(app_settings_sync_export(&data,&n)==ESP_OK);
        FILE *file=fopen(argv[2],"wb");assert(file&&fwrite(data,1,n,file)==n&&!fclose(file));free(data);return 0;
    }
    size_t bytes=0;uint8_t *wire=load_bytes(argv[2],&bytes);
    app_settings_set_book_px(44);save_progress_at(a,91);save_progress_at(b,64);
    assert(nvs_set_u8(favorite_handle,"f_cafebabe",1)==ESP_OK);
    // Every failed write/commit must preserve configuration, progress, WiFi and unrelated records.
    saved_count=count;memcpy(saved,db,sizeof(db));unsigned failures=0;
    int base_writes=writes,base_commits=commits;
    assert(app_settings_sync_import(wire,bytes)==ESP_OK);int applied_writes=writes-base_writes,applied_commits=commits-base_commits;
    assert_progress(a,82);assert_progress(b,64);
    uint8_t *exported=NULL;size_t exported_len=0;assert(app_settings_sync_export(&exported,&exported_len)==ESP_OK);
    const char *history=memmem(wire,bytes,"RPHIST1",8);assert(history);
    for(size_t i=0;i<(size_t)(history-(const char*)wire)&&i<exported_len;++i)if(exported[i]!=wire[i]){fprintf(stderr,"roundtrip differs at %zu: %u != %u\n",i,exported[i],wire[i]);break;}
    assert(exported_len>=(size_t)(history-(const char*)wire)&&!memcmp(exported,wire,(size_t)(history-(const char*)wire)));free(exported);
    base_writes=writes;base_commits=commits;int base_renames=rename_calls;assert(app_settings_sync_import(wire,bytes)==ESP_OK);
    applied_writes=writes-base_writes;applied_commits=commits-base_commits;int applied_renames=rename_calls-base_renames;
    app_settings_set_book_px(44);app_settings_set_clock_12h(false);app_settings_set_low_ghost(true);save_progress_at(a,91);
    saved_count=count;memcpy(saved,db,sizeof(db));
    for(int mode=0;mode<3;++mode)for(int at=1;at<=(mode==2?applied_renames:mode?applied_commits:applied_writes);++at){
        if(mode==2)fail_rename=rename_calls+at;else if(mode)fail_commit=commits+at;else fail_write=writes+at;
        esp_err_t err=app_settings_sync_import(wire,bytes);fail_write=fail_commit=fail_rename=0;
        if(err==ESP_OK){fprintf(stderr,"missed fault mode=%d at=%d writes=%d commits=%d\n",mode,at,applied_writes,applied_commits);abort();}
        assert_database_saved();assert(app_settings_book_px()==44&&!app_settings_clock_12h()&&app_settings_low_ghost());assert_progress(a,91);assert_progress(b,64);++failures;
    }
    // A persistent NVS failure must retain its undo journal; a later retry restores every namespace.
    persistent_commit_failure=true;assert(app_settings_sync_import(wire,bytes)!=ESP_OK);persistent_commit_failure=false;
    FILE *pending=fopen(RESTORE_PENDING,"rb");assert(pending&&!fclose(pending));
    assert(app_settings_restore_recover()==ESP_OK);assert_database_saved();assert_progress(a,91);assert_progress(b,64);
    // Simulate a process reset after both settings and the progress pointer changed, before commit marker.
    uint32_t generation=book_progress_restore_generation();assert(restore_checkpoint_begin()==ESP_OK);assert(book_progress_restore_begin()==ESP_OK);
    FILE *interrupted=fmemopen(wire,bytes,"rb");assert(interrupted);
    assert(backup_apply_stream_raw(interrupted,true)==ESP_OK&&!fclose(interrupted));assert(book_progress_restore_commit()==ESP_OK);assert_progress(a,82);
    assert(app_settings_restore_recover()==ESP_OK);app_settings_init();assert(book_progress_restore_generation()==generation);
    assert_database_saved();assert_progress(a,91);assert_progress(b,64);
    // A corrupt checkpoint is retained and refused before changing any current record.
    assert(restore_checkpoint_begin()==ESP_OK);FILE *bad=fopen(RESTORE_PENDING,"r+b");assert(bad&&fputc(0,bad)!=EOF&&!fclose(bad));
    assert(app_settings_restore_recover()==ESP_ERR_INVALID_RESPONSE);assert_database_saved();assert(!remove(RESTORE_PENDING));
    mounted=false;assert(app_settings_sync_import(wire,bytes)==ESP_ERR_INVALID_STATE);assert_database_saved();mounted=true;
    // Native v28 plus all historical streams are applied by actual ESP restore code.
    for(int i=2;i<argc;++i){
        size_t n=0;uint8_t *data=load_bytes(argv[i],&n);
        bool combined=strstr(argv[i],"v28-combined")!=NULL;
        if(combined){app_settings_set_low_ghost(true);assert(app_settings_set_shelf_covers(true));}
        esp_err_t err=app_settings_sync_import(data,n);
        if(err!=ESP_OK)fprintf(stderr,"failed %s err=%d\n",argv[i],err);
        assert(err==ESP_OK);assert_progress(b,64);
        if(combined)assert(app_settings_low_ghost()&&!app_settings_shelf_covers()&&app_settings_book_margin_y()==18);
        free(data);
    }

    // Public card path and a damaged paired companion must still accept a valid older interchange.
    assert(app_settings_backup_save()==ESP_OK&&app_settings_backup_save()==ESP_OK);FILE *bad_full=fopen(BACKUP_FULL,"r+b");assert(bad_full&&fputc(0,bad_full)!=EOF&&!fclose(bad_full));
    assert(app_settings_backup_restore()==ESP_OK);assert_progress(b,64);
    free(wire);printf("PASS board=%d: %d historical streams, %u injected write/commit/rename failures, exact multi-namespace NVS/WiFi rollback, persistent-error journal retry, interrupted-commit recovery, corrupt-journal protection, unrelated progress and FAT no-overwrite rename, repeat save/restore and public card fallback\n",KIIKO_BOARD_METALIO47,argc-2,failures);
}

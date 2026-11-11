#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：生产云导入的模拟网络/真文件/线程取消回归，无真实账号。
English: Exercise production cloud import with mocked HTTP, real files and joined workers.
"""
from pathlib import Path
import json
import argparse, re, subprocess, tempfile, os, io, zipfile, base64, struct, zlib, shlex, sys
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cjson',type=Path);p.add_argument('--image-deps',type=Path);p.add_argument('--png-prefix',type=Path);p.add_argument('--board',choices=['pico','metalio47'],default='pico');args=p.parse_args()
cjson=args.cjson
if not cjson:
    candidates=[ROOT/'managed_components/espressif__cjson/cJSON',Path('/private/tmp/kiiko-reader-mode-20261101/firmware/pico/source/managed_components/espressif__cjson/cJSON')]
    cjson=next((x for x in candidates if (x/'cJSON.c').is_file()),None)
assert cjson,'Pass --cjson path/to/cJSON'
source=(ROOT/'main/book/book_xteink_sync.c').read_text()
# 只替代硬件加密边界；协议、状态机、解析、下载和存储执行真实实现。
# Mock the hardware crypto boundary only; execute real protocol/state/download/storage code.
a=source.index('static bool sha256_bytes(');b=source.index('// ---- 状态文件',a)
source=source[:a]+r'''
static bool sha256_bytes(const uint8_t*i,size_t n,uint8_t out[32]){(void)i;(void)n;memset(out,1,32);return true;}
static bool hmac_sha256_hex(const char*k,const char*m,char out[65]){(void)k;(void)m;memset(out,'a',64);out[64]=0;return true;}
static bool aes_gcm_seal(const uint8_t k[32],const uint8_t iv[12],const char*p,uint8_t*out,size_t cap,size_t*len){(void)k;(void)iv;assert(strstr(p,"\"device_type\":\"" XTEINK_SYNC_DEVICE_TYPE "\""));*len=strlen(p)+16;assert(cap>=*len);memcpy(out,p,strlen(p));memset(out+strlen(p),2,16);return true;}
'''+source[b:]
source=source.replace('#define XTEINK_PICTURES_DIR "/sdcard/Pictures"','#define XTEINK_PICTURES_DIR pictures_path')
source=re.sub(r'^#include ["<].*[">]\n','',source,flags=re.M)
header=(ROOT/'main/book/book_xteink_sync.h').read_text();header=re.sub(r'^#include .*\n|^#pragma once\n','',header,flags=re.M)
stubs=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>
#include <pthread.h>
#include "cJSON.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NOT_FOUND -4
#define ESP_ERR_INVALID_ARG -5
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT 0x8006
#define HTTP_ADDR_TYPE_INET 2
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
#define BOOK_STORE_PATH_MAX 288
static atomic_int allocations;static size_t fail_once_bytes;
static void *heap_caps_malloc(size_t n,int c){(void)c;if(fail_once_bytes==n){fail_once_bytes=0;return NULL;}void*p=malloc(n);if(p)++allocations;return p;}
static void *heap_caps_calloc(size_t n,size_t z,int c){(void)c;void*p=calloc(n,z);if(p)++allocations;return p;}
static void heap_caps_free(void*p){if(p)--allocations;free(p);}
typedef struct{pthread_mutex_t lock;pthread_cond_t cond;int count;}sem_t;
typedef sem_t* SemaphoreHandle_t;
static sem_t sems[2];static int sem_used;
static SemaphoreHandle_t new_sem(int count){assert(sem_used<2);sem_t*s=&sems[sem_used++];pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->cond,NULL);s->count=count;return s;}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return new_sem(1);}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return new_sem(0);}
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
static int xSemaphoreTake(sem_t*s,unsigned wait){pthread_mutex_lock(&s->lock);while(!s->count){if(!wait){pthread_mutex_unlock(&s->lock);return 0;}assert(wait==portMAX_DELAY);pthread_cond_wait(&s->cond,&s->lock);}--s->count;pthread_mutex_unlock(&s->lock);return 1;}
static int xSemaphoreGive(sem_t*s){pthread_mutex_lock(&s->lock);assert(s->count==0);++s->count;pthread_cond_signal(&s->cond);pthread_mutex_unlock(&s->lock);return 1;}
typedef void* TaskHandle_t;typedef int BaseType_t;
static pthread_t worker_thread;static bool thread_joinable;
static void(*task_fn)(void*);static void*task_arg;
static void*run_task(void*p){(void)p;task_fn(task_arg);return NULL;}
static int xTaskCreate(void(*fn)(void*),const char*n,int stack,void*a,int priority,TaskHandle_t*out){(void)n;assert(stack>=24576);(void)priority;assert(!thread_joinable);task_fn=fn;task_arg=a;*out=(void*)1;thread_joinable=true;assert(!pthread_create(&worker_thread,NULL,run_task,NULL));return pdPASS;}
static void vTaskDelete(void*p){assert(!p);}
static atomic_llong fake_time;static atomic_int async_spins;static int64_t esp_timer_get_time(void){return fake_time;}
static void vTaskDelay(unsigned ms){fake_time+=(int64_t)ms*1000;usleep(1000);}
static uint8_t mac_tail=6;
#define ESP_MAC_WIFI_STA 0
#define ESP_MAC_BT 1
static int esp_read_mac(uint8_t*out,int type){(void)type;for(int i=0;i<6;i++)out[i]=(uint8_t)(i+1);out[5]=mac_tail;return ESP_OK;}
static void esp_fill_random(void*p,size_t n){memset(p,7,n);}
typedef struct{char version[32];}esp_app_desc_t;
static const esp_app_desc_t *esp_app_get_description(void){static esp_app_desc_t d={"host"};return &d;}
static const char*esp_err_to_name(int e){(void)e;return "mock";}
typedef struct{char path[288];}book_store_root_t;static char root_path[288],fonts_path[320],pictures_path[320];
static bool card_ready=true;static bool read_pico_sd_io_ready(void){return card_ready;}
#define BOOK_IMAGE_FILE_MAX (50u*1024u*1024u)
bool book_image_file_dimensions(const char*,bool,unsigned*,unsigned*);
bool book_image_file_grayscale(const char*,bool,unsigned,unsigned,uint8_t*);
bool read_pico_webp_rgb565(const uint8_t*d,size_t s,size_t m,uint8_t**o,unsigned*w,unsigned*h){(void)d;(void)s;(void)m;(void)o;(void)w;(void)h;return false;}
static int book_store_upload_root(book_store_root_t*r){strcpy(r->path,root_path);return ESP_OK;}
static const char*app_settings_fonts_dir(void){return fonts_path;}
#define READ_PICO_TRANSFER_STOPPED 0
#define READ_PICO_TRANSFER_MODE_STA 1
typedef struct{bool network_ready;int state;}read_pico_transfer_status_t;
typedef struct{int mode;bool network_only;}read_pico_transfer_cfg_t;
static bool network_ready=true;static int starts,stops;
static void read_pico_transfer_get_status(read_pico_transfer_status_t*s){s->network_ready=network_ready;s->state=0;}
static int read_pico_transfer_get_saved_wifi(char ssid[33],bool*c){strcpy(ssid,"mock");*c=true;return 0;}
static void read_pico_transfer_stop(void){++stops;network_ready=false;}
static int read_pico_transfer_start(const read_pico_transfer_cfg_t*c){assert(c->network_only&&c->mode==1);++starts;network_ready=true;return 0;}
static void read_pico_transfer_service_poll(void){}
static int online_syncs,online_error;static uint32_t online_utc=1800000000u;
static int read_pico_transfer_sync_time_online(uint32_t*t){assert(network_ready);++online_syncs;*t=online_utc;return online_error;}
typedef struct zip_reader zip_reader_t;
int zip_open(const char*p,zip_reader_t**r);
int zip_find(const zip_reader_t*r,const char*n);
int zip_extract(zip_reader_t*r,int i,void*p,size_t n);
void zip_close(zip_reader_t*r);
typedef struct esp_http_client_event{int event_id;void*user_data;int data_len;char*data;void*client;}esp_http_client_event_t;
typedef struct{const char*url;int timeout_ms;void*crt_bundle_attach;int buffer_size,buffer_size_tx;bool is_async;int addr_type;int(*event_handler)(esp_http_client_event_t*);void*user_data;}esp_http_client_config_t;
typedef struct{esp_http_client_config_t cfg;int status;int64_t length;const char*body;}client_t;
typedef client_t* esp_http_client_handle_t;
#define HTTP_EVENT_ON_DATA 1
#define HTTP_EVENT_ON_CONNECTED 2
#define HTTP_EVENT_ON_HEADER 3
#define HTTP_METHOD_POST 1
#define HTTP_METHOD_GET 0
static void*esp_crt_bundle_attach=(void*)1;
static int token_status=200,pull_status=200,download_status=200,request_count,complete_count;
static bool waiting,block_download,truncated,block_token,transport_fail,cert_fail,redirect_body,download_transport_fail,async_forever,body_trickle;static atomic_bool http_started;
static char tasks_json[16000],completed_body[256],last_download_url[8192];static size_t download_length;static const char*download_body="a simple novel\nsecond line\n";
static int esp_http_client_get_and_clear_last_tls_error(client_t*c,int*code,int*flags){(void)c;*code=cert_fail?-9984:0;*flags=cert_fail?8:0;return cert_fail?0x8010:ESP_OK;}
static client_t*esp_http_client_init(const esp_http_client_config_t*c){assert(c->crt_bundle_attach);if(c->is_async)assert(c->timeout_ms==30000&&c->addr_type==HTTP_ADDR_TYPE_INET);else assert(c->timeout_ms==5000);client_t*p=calloc(1,sizeof(*p));assert(p);p->cfg=*c;return p;}
static void esp_http_client_set_header(client_t*c,const char*k,const char*v){(void)c;assert(k&&v);}
static void esp_http_client_set_method(client_t*c,int m){(void)c;(void)m;}
static void esp_http_client_set_post_field(client_t*c,const char*b,int n){(void)n;c->body=b;}
static void esp_http_client_set_user_data(client_t*c,void*p){c->cfg.user_data=p;}
static int esp_http_client_get_status_code(client_t*c){return c->status;}
static int64_t esp_http_client_get_content_length(client_t*c){return c->length;}
static void esp_http_client_cleanup(client_t*c){free(c);}
static int esp_http_client_perform(client_t*c);
'''
tests=r'''
static int esp_http_client_perform(client_t*c){
 ++request_count;const char*response;bool is_download=false;
 if(strstr(c->cfg.url,"/token")){assert(s_status.qr[0]&&online_syncs&&s_bind_utc==online_utc);cJSON*sent=cJSON_Parse(c->body);assert(sent);cJSON*stamp=cJSON_GetObjectItem(sent,"timestamp");assert(cJSON_IsNumber(stamp)&&stamp->valuedouble>=online_utc&&stamp->valuedouble<online_utc+1000);cJSON_Delete(sent);if(block_token){http_started=true;while(!s_cancel)usleep(1000);return ESP_FAIL;}if(transport_fail){c->status=0;return ESP_FAIL;}c->status=token_status;response=waiting?"{\"poll_interval\":2}":"{\"access_token\":\"host-token\",\"expires_in\":7200}";}
 else if(strstr(c->cfg.url,"/pull")){c->status=pull_status;response=tasks_json;}
 else if(strstr(c->cfg.url,"/complete")){++complete_count;snprintf(completed_body,sizeof(completed_body),"%s",c->body);c->status=200;response="{}";}
 else {is_download=true;assert(c->cfg.is_async&&c->cfg.addr_type==HTTP_ADDR_TYPE_INET);if(async_forever){++async_spins;fake_time+=1000000;return ESP_ERR_HTTP_EAGAIN;}assert(!strncmp(c->cfg.url,"https://",8));snprintf(last_download_url,sizeof(last_download_url),"%s",c->cfg.url);c->status=download_status;response=download_body;if(download_transport_fail)return ESP_FAIL;if(redirect_body){c->status=302;esp_http_client_event_t e={HTTP_EVENT_ON_DATA,c->cfg.user_data,17,"redirect html !!!",c};assert(c->cfg.event_handler(&e)==ESP_OK);c->status=download_status;}if(block_download){http_started=true;while(!s_cancel)usleep(1000);return ESP_FAIL;}}
 if(is_download&&body_trickle) {
  ++async_spins;fake_time+=10000000;
  if(async_spins<8){c->status=200;esp_http_client_event_t e={HTTP_EVENT_ON_DATA,c->cfg.user_data,1,"a",c};assert(c->cfg.event_handler(&e)==ESP_OK);return ESP_ERR_HTTP_EAGAIN;}
 }
 size_t length=is_download&&download_length?download_length:strlen(response);
 c->length=(int64_t)length+(truncated?7:0)+(is_download&&body_trickle?7:0);
 esp_http_client_event_t e={HTTP_EVENT_ON_DATA,c->cfg.user_data,(int)length,(char*)response,c};
 return c->cfg.event_handler(&e);
}
static void join_worker(void){assert(thread_joinable);pthread_join(worker_thread,NULL);thread_joinable=false;}
static void run_refresh(void){assert(xteink_sync_start()==ESP_OK);join_worker();}
static bool exists(const char*p){struct stat st;return !stat(p,&st);}
static void init_tasks(void){
 strcpy(tasks_json,"{\"tasks\":[");
 for(int i=0;i<12;i++){char row[300];snprintf(row,sizeof(row),"%s{\"task_id\":\"task-%d\",\"file_url\":\"https://cdn.example.test/book%d.txt\",\"save_path\":\"book%d.txt\",\"size\":%zu}",i?",":"",i,i,i,strlen(download_body));strcat(tasks_json,row);}
 strcat(tasks_json,"]}");
}
int main(void){
 char directory[]="/tmp/cloud-import-host-XXXXXX";assert(mkdtemp(directory));strcpy(root_path,directory);snprintf(fonts_path,sizeof(fonts_path),"%s/fonts",directory);snprintf(pictures_path,sizeof(pictures_path),"%s/Pictures",directory);
 char *prepared=download_url_prepare("http://cdn.example.test:80/a%20b.epub?sign=a%2Bb#anchor");assert(prepared&&!strcmp(prepared,"https://cdn.example.test/a%20b.epub?sign=a%2Bb"));heap_caps_free(prepared);
 char out[1024];assert(xteink_base64url_encode((const uint8_t*)"f",1,out,sizeof(out))==2&&!strcmp(out,"Zg"));assert(xteink_base64url_encode((const uint8_t*)"fo",2,out,sizeof(out))==3&&!strcmp(out,"Zm8"));assert(xteink_base64url_encode((const uint8_t*)"foo",3,out,sizeof(out))==4&&!strcmp(out,"Zm9v"));assert(xteink_url_encode_path("/书 名%20.txt",out,sizeof(out))&&!strcmp(out,"/%E4%B9%A6%20%E5%90%8D%20.txt"));
 init_once();identity_init();assert(!strcmp(s_state.id.firmware_name,"kiikoread")&&!strcmp(s_state.id.device_type,"pico"));char envelope[1024];assert(build_bind_envelope(1800000000u,envelope,sizeof(envelope))&&strstr(envelope,"\"firmware_name\":\"kiikoread\""));strcpy(s_state.root,root_path);strcpy(s_state.access_token,"saved-one");assert(state_save());char path[400];assert(state_file_path(path,sizeof(path),""));assert(exists(path));strcpy(s_state.access_token,"saved-two");assert(state_save());FILE*f=fopen(path,"wb");assert(f);fputs("corrupted",f);fclose(f);s_state_loaded=false;s_state.access_token[0]=0;assert(state_load()&&!strcmp(s_state.access_token,"saved-one"));
 xteink_sync_close();mac_tail=9;identity_init();strcpy(s_state.root,root_path);assert(!state_load()&&!s_state.access_token[0]);xteink_sync_close();network_ready=false;init_tasks();
 run_refresh();assert(network_ready&&starts==1&&stops==0);assert(xteink_sync_bound()&&xteink_sync_task_count()==12);xteink_task_t item;assert(xteink_sync_task_get(11,&item)&&!strcmp(item.task_id,"task-11"));assert(!xteink_sync_task_get(12,&item));
 int calls=request_count;for(int i=0;i<10;i++){xteink_sync_status_t st;xteink_sync_get_status(&st);assert(st.bound&&!st.busy);}assert(request_count==calls);
 assert(xteink_sync_download(11)==0);join_worker();assert(network_ready&&starts==1&&stops==0);snprintf(out,sizeof(out),"%s/book11.txt",root_path);assert(exists(out));assert(complete_count==1&&strstr(completed_body,"\"success\":true"));assert(xteink_sync_import_count()==1&&xteink_sync_task_get(11,&item)&&item.imported);
 truncated=true;assert(xteink_sync_download(1)==0);join_worker();xteink_sync_cancel_join();truncated=false;snprintf(out,sizeof(out),"%s/book1.txt",root_path);assert(!exists(out));assert(xteink_sync_import_count()==1&&strstr(completed_body,"\"success\":false"));
 download_body="invalid";xSemaphoreTake(s_lock,portMAX_DELAY);strcpy(s_tasks[2].save_path,"font.ttf");s_tasks[2].is_font=true;xSemaphoreGive(s_lock);assert(xteink_sync_download(2)==0);join_worker();xteink_sync_cancel_join();snprintf(out,sizeof(out),"%s/font.ttf",fonts_path);assert(!exists(out)&&xteink_sync_import_count()==1);
 block_download=true;assert(xteink_sync_download(3)==0);while(!http_started)usleep(1000);xteink_sync_cancel_join();join_worker();block_download=false;assert(!xteink_sync_busy());snprintf(out,sizeof(out),"%s/book3.txt",root_path);assert(!exists(out));snprintf(out,sizeof(out),"%s/.readpico/cloud/incoming/download.part",root_path);assert(!exists(out));

 download_body="a simple novel\nsecond line\n";redirect_body=true;
 assert(xteink_sync_download(4)==ESP_OK);join_worker();xteink_sync_task_get(4,&item);assert(item.imported);redirect_body=false;
 snprintf(out,sizeof(out),"%s/book4.txt",root_path);FILE *check=fopen(out,"rb");assert(check);char content[100]={0};assert(fread(content,1,sizeof(content),check)==strlen(download_body));fclose(check);assert(!strcmp(content,download_body));
 char long_url[1400];strcpy(long_url,"http://cdn.example.test/书%20名.txt?signature=a%2Bb%3D&key=");memset(long_url+strlen(long_url),'q',1000);long_url[strlen("http://cdn.example.test/书%20名.txt?signature=a%2Bb%3D&key=")+1000]=0;
 xSemaphoreTake(s_lock,portMAX_DELAY);strcpy(s_tasks[5].file_url,long_url);xSemaphoreGive(s_lock);
 assert(xteink_sync_download(5)==ESP_OK);join_worker();assert(!strncmp(last_download_url,"https://cdn.example.test/%E4%B9%A6%20%E5%90%8D.txt?signature=a%2Bb%3D&key=",strlen("https://cdn.example.test/%E4%B9%A6%20%E5%90%8D.txt?signature=a%2Bb%3D&key=")));assert(strlen(last_download_url)>1000);xteink_sync_task_get(5,&item);assert(item.imported);
 download_status=403;assert(xteink_sync_download(6)==ESP_OK);join_worker();xteink_sync_task_get(6,&item);assert(item.failed&&strstr(item.error,"HTTP 403"));snprintf(out,sizeof(out),"%s/book6.txt",root_path);assert(!exists(out));download_status=200;
 download_transport_fail=true;cert_fail=true;assert(xteink_sync_download(7)==ESP_OK);join_worker();xteink_sync_task_get(7,&item);assert(item.failed&&strstr(item.error,"证书"));download_transport_fail=false;cert_fail=false;
 download_body=(const char*)epub_payload;download_length=sizeof(epub_payload);xSemaphoreTake(s_lock,portMAX_DELAY);strcpy(s_tasks[8].save_path,"novel.epub");xSemaphoreGive(s_lock);assert(xteink_sync_download(8)==ESP_OK);join_worker();xteink_sync_task_get(8,&item);assert(item.imported&&!item.failed);snprintf(out,sizeof(out),"%s/novel.epub",root_path);assert(verify_epub(out));download_length=0;download_body="a simple novel\nsecond line\n";
 xteink_sync_cancel_join();assert(!network_ready&&stops>0);


 async_forever=true;async_spins=0;assert(xteink_sync_download(9)==ESP_OK);join_worker();xteink_sync_task_get(9,&item);assert(item.failed&&strstr(item.error,"0x8006")&&strstr(item.error,"cdn.example.test"));assert(async_spins>=29&&async_spins<=31);async_forever=false;
 async_forever=true;async_spins=0;assert(xteink_sync_download(10)==ESP_OK);while(async_spins<2)usleep(1000);xteink_sync_cancel_join();join_worker();assert(async_spins<10&&!xteink_sync_busy());async_forever=false;
 body_trickle=true;async_spins=0;assert(xteink_sync_download(10)==ESP_OK);join_worker();xteink_sync_task_get(10,&item);assert(item.imported&&async_spins==8);body_trickle=false;
 char bad_cache[400],saved_bad[400];snprintf(bad_cache,sizeof(bad_cache),"%s/no-parent/cache",root_path);s_download_error[0]=0;assert(download_file("https://cdn.example.test/book.epub","book.epub",bad_cache,saved_bad,sizeof(saved_bad))==3);assert(strstr(s_download_error,"TF卡"));
 snprintf(tasks_json,sizeof(tasks_json),"{\"tasks\":[{\"task_id\":\"long-link\",\"file_url\":\"%s\",\"size\":10}]}",long_url);run_refresh();assert(xteink_sync_task_get(0,&item)&&!strcmp(item.file_url,long_url)&&!strcmp(item.save_path,"书%20名.txt"));
 char excessive[2300];strcpy(excessive,"https://cdn.example.test/a.epub?sign=");size_t base=strlen(excessive);memset(excessive+base,'a',2100);excessive[base+2100]=0;snprintf(tasks_json,sizeof(tasks_json),"{\"tasks\":[{\"task_id\":\"too-long\",\"file_url\":\"%s\",\"save_path\":\"a.epub\"}]}",excessive);run_refresh();assert(xteink_sync_task_get(0,&item)&&item.failed&&!item.file_url[0]&&strstr(item.error,"链接过长"));

 snprintf(tasks_json,sizeof(tasks_json),"{\"tasks\":[{\"task_id\":\"wall\",\"file_url\":\"https://cdn.example.test/wall.PNG?sig=x\",\"save_path\":\"/untrusted/../wall.PNG\"},{\"task_id\":\"jpg\",\"file_url\":\"https://cdn.example.test/wall.JPEG\",\"save_path\":\"wall.JPEG\"},{\"task_id\":\"fake\",\"file_url\":\"https://cdn.example.test/font.ttf.png\",\"save_path\":\"font.ttf.png\"}]}");
 run_refresh();assert(xteink_sync_task_get(0,&item)&&item.is_image&&!item.is_font);assert(xteink_sync_task_get(1,&item)&&item.is_image);assert(xteink_sync_task_get(2,&item)&&item.is_image&&!item.is_font);
 download_body=(const char*)png_payload;download_length=sizeof(png_payload);assert(xteink_sync_download(0)==ESP_OK);join_worker();assert(xteink_sync_task_get(0,&item)&&item.imported&&!item.failed);snprintf(out,sizeof(out),"%s/wall.PNG",pictures_path);assert(verify_image(out,true));xteink_sync_status_t picture_status;xteink_sync_get_status(&picture_status);assert(strstr(picture_status.message,"Pictures"));
 char original_picture[1024];strcpy(original_picture,out);assert(xteink_sync_download(0)==ESP_OK);join_worker();snprintf(out,sizeof(out),"%s/wall (2).PNG",pictures_path);assert(verify_image(out,true)&&verify_image(original_picture,true));snprintf(out,sizeof(out),"%s/wall.PNG",root_path);assert(!exists(out));
 download_body=(const char*)jpeg_payload;download_length=sizeof(jpeg_payload);assert(xteink_sync_download(1)==ESP_OK);join_worker();snprintf(out,sizeof(out),"%s/wall.JPEG",pictures_path);assert(verify_image(out,false));
 // JPG内容不能冒充PNG；损坏/截断/HTTP失败不提交。/ Reject mismatched signatures, corrupt/truncated payloads and failed HTTP.
 assert(xteink_sync_download(2)==ESP_OK);join_worker();assert(xteink_sync_task_get(2,&item)&&item.failed&&!item.imported);
 download_body=(const char*)png_payload;download_length=sizeof(png_payload)/2;assert(xteink_sync_download(2)==ESP_OK);join_worker();assert(xteink_sync_task_get(2,&item)&&item.failed&&!item.imported);
 download_body="<html>not a wallpaper</html>";download_length=0;assert(xteink_sync_download(2)==ESP_OK);join_worker();assert(xteink_sync_task_get(2,&item)&&item.failed&&!item.imported);snprintf(out,sizeof(out),"%s/font.ttf.png",pictures_path);assert(!exists(out));snprintf(out,sizeof(out),"%s/.readpico/cloud/incoming/download.part",root_path);assert(!exists(out));

 FILE*font_file=fopen(TEST_FONT_PATH,"rb");assert(font_file&&!fseek(font_file,0,SEEK_END));long font_size=ftell(font_file);assert(font_size>0&&!fseek(font_file,0,SEEK_SET));char *font_data=malloc((size_t)font_size);assert(font_data&&fread(font_data,1,(size_t)font_size,font_file)==(size_t)font_size);fclose(font_file);download_body=font_data;download_length=(size_t)font_size;
 xSemaphoreTake(s_lock,portMAX_DELAY);strcpy(s_tasks[2].save_path,"cloud-font.ttf");s_tasks[2].is_image=false;s_tasks[2].is_font=true;xSemaphoreGive(s_lock);assert(xteink_sync_download(2)==ESP_OK);join_worker();snprintf(out,sizeof(out),"%s/cloud-font.ttf",fonts_path);assert(verify_font(out)&&xteink_sync_task_get(2,&item)&&item.imported);free(font_data);
 char traversal_saved[512],traversal_incoming[512];snprintf(traversal_incoming,sizeof(traversal_incoming),"%s/.readpico/cloud/incoming",root_path);int prior_traversal=request_count;assert(download_file("https://cdn.example.test/a.png","..\\escape.png",traversal_incoming,traversal_saved,sizeof(traversal_saved))!=0&&request_count==prior_traversal);

 int before_no_card=request_count;card_ready=false;assert(xteink_sync_download(0)==ESP_OK);join_worker();assert(xteink_sync_task_get(0,&item)&&item.failed&&strstr(item.error,"TF卡"));assert(request_count==before_no_card+1);card_ready=true;
 // Explicit batches use one serial worker; valid selections and cancellation never touch later items.
 download_body="batch book\nsecond line\n";download_length=0;init_tasks();run_refresh();
 assert(xteink_sync_pending_mask()==4095);
 assert(xteink_sync_download_selected(0)==ESP_ERR_INVALID_ARG&&xteink_sync_download_selected(1u<<24)==ESP_ERR_INVALID_ARG);
 int before_oom_requests=request_count,live_allocations=allocations;fail_once_bytes=sizeof(xteink_task_t);
 assert(xteink_sync_download(0)==ESP_OK);join_worker();xteink_sync_status_t oom_status;xteink_sync_get_status(&oom_status);
 assert(oom_status.state==XTEINK_SYNC_FAILED&&!oom_status.busy&&!fail_once_bytes&&request_count==before_oom_requests&&allocations==live_allocations);
 assert(xteink_sync_task_get(0,&item)&&!item.downloading&&!item.imported);
 unsigned batch_imports=xteink_sync_import_count();int batch_completed=complete_count;
 assert(xteink_sync_download_selected((1u<<1)|(1u<<5))==ESP_OK);join_worker();
 assert(xteink_sync_import_count()==batch_imports+2&&complete_count==batch_completed+2);
 assert(xteink_sync_task_get(1,&item)&&item.imported&&xteink_sync_task_get(5,&item)&&item.imported);
 assert(xteink_sync_task_get(0,&item)&&!item.imported&&xteink_sync_download_selected(1u<<1)==ESP_ERR_INVALID_ARG);
 xSemaphoreTake(s_lock,portMAX_DELAY);s_tasks[0].file_url[0]=0;strcpy(s_tasks[0].error,"invalid fixture URL");xSemaphoreGive(s_lock);
 assert(xteink_sync_download_selected(1u|(1u<<2))==ESP_OK);join_worker();
 assert(xteink_sync_task_get(0,&item)&&item.failed&&!item.imported&&xteink_sync_task_get(2,&item)&&item.imported);
 xteink_sync_status_t batch_status;xteink_sync_get_status(&batch_status);assert(batch_status.state==XTEINK_SYNC_FAILED&&strstr(batch_status.message,"失败1项"));
 block_download=true;http_started=false;assert(xteink_sync_download_selected((1u<<3)|(1u<<4))==ESP_OK);while(!http_started)usleep(1000);
 assert(xteink_sync_download_selected(1u<<4)==ESP_ERR_INVALID_STATE);xteink_sync_cancel_join();join_worker();block_download=false;
 assert(xteink_sync_task_get(4,&item)&&!item.imported&&!item.downloading&&!item.received&&!xteink_sync_busy());
 run_refresh();batch_imports=xteink_sync_import_count();assert(xteink_sync_download_selected(xteink_sync_pending_mask())==ESP_OK);join_worker();
 assert(!xteink_sync_pending_mask()&&xteink_sync_import_count()==batch_imports+12);
 puts("PASS:24KiB worker admission and PSRAM snapshot OOM fails without HTTP/file/heap leak; selected/all-pending cloud batches are explicit,sequential,bounded,skip imported,reject empty/invalid/busy,retain per-item failures,continue others,and cancel before the next file");

 assert(file_kind("wall.bmp")==XTEINK_FILE_OTHER&&file_kind("wall.PNG.exe")==XTEINK_FILE_OTHER);
 download_body="a simple novel\nsecond line\n";download_length=0;
 xteink_sync_cancel_join();network_ready=true;int stopped_before=stops;run_refresh();xteink_sync_close();assert(network_ready&&stops==stopped_before);
 xteink_sync_unbind();online_error=ESP_FAIL;int before_time_failure=request_count;run_refresh();xteink_sync_status_t status;xteink_sync_get_status(&status);assert(status.state==XTEINK_SYNC_FAILED&&!status.qr[0]&&!status.bound&&strstr(status.message,"校时")&&request_count==before_time_failure);online_error=0;
 online_utc=0;run_refresh();xteink_sync_get_status(&status);assert(!status.qr[0]&&!status.bound&&request_count==before_time_failure);online_utc=1800000000u;
 token_status=400;run_refresh();xteink_sync_get_status(&status);assert(status.state==XTEINK_SYNC_FAILED&&status.qr[0]&&!status.bound&&strstr(status.message,"HTTP 400"));
 int64_t old_clock=s_bind_clock_us;fake_time=old_clock+15000000;assert(current_time_or_zero()==online_utc+15);fake_time=old_clock;
 token_status=403;run_refresh();xteink_sync_get_status(&status);assert(status.state==XTEINK_SYNC_FAILED&&!status.busy&&!status.bound&&status.qr[0]&&strstr(status.message,"HTTP 403"));assert(!allocations||allocations==1);
 token_status=404;run_refresh();xteink_sync_get_status(&status);assert(status.qr[0]&&strstr(status.message,"HTTP 404")&&!status.bound);
 transport_fail=true;cert_fail=true;run_refresh();xteink_sync_get_status(&status);assert(status.qr[0]&&strstr(status.message,"HTTPS")&&strstr(status.message,"0x8")&&!status.bound);cert_fail=false;run_refresh();xteink_sync_get_status(&status);assert(status.qr[0]&&strstr(status.message,"网络连接失败"));transport_fail=false;
 block_token=true;http_started=false;assert(xteink_sync_start()==ESP_OK);while(!http_started)usleep(1000);xteink_sync_get_status(&status);assert(status.qr[0]&&status.busy&&!status.bound);xteink_sync_cancel_join();join_worker();block_token=false;
 token_status=200;waiting=true;assert(xteink_sync_start()==0);for(int i=0;i<3000;i++){xteink_sync_get_status(&status);if(status.qr[0])break;usleep(1000);}assert(status.qr[0]&&status.state==XTEINK_SYNC_WAITING);xteink_sync_cancel_join();join_worker();assert(!xteink_sync_busy());
 xteink_sync_close();assert(allocations==0);assert(!network_ready||stops<=starts);for(int i=0;i<sem_used;i++){pthread_mutex_destroy(&sems[i].lock);pthread_cond_destroy(&sems[i].cond);}char command[700];snprintf(command,sizeof(command),"rm -rf '%s'",directory);assert(system(command)==0);
 puts("PASS: kiikoread bind name; real streaming PNG/JPEG download to SD-root Pictures, uppercase types, safe basename, collisions, mismatched/truncated/HTML rejection, no-card guard; fresh UTC despite plausible stale RTC; failed/invalid time sends no QR/request; monotonic15s token timestamp; HTTP400 stays failed/unbound; QR precedes blocked/failed HTTP,403/404/TLS details preserved and no false binding; long signed URL/HTTPS upgrade/redirect-body filter/HTTP403/TLS details/real ZIP EPUB import/retained STA/IPv4 async download/30s idle bound/prompt pending cancellation/80s active transfer; production cloud protocol/list12/state backup/device isolation/TXT import/truncated rejection/font rejection/cancel+join/no temp residue/no token retry loop/no heap leaks; HTTP/crypto mocked, no real account used");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-cloud-test-') as t:
    payload=io.BytesIO()
    with zipfile.ZipFile(payload,'w') as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',compress_type=zipfile.ZIP_DEFLATED)
        z.writestr('OEBPS/content.opf','<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="id" version="2.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">cloud-test</dc:identifier><dc:title>Cloud import test</dc:title><dc:language>zh-CN</dc:language></metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/><item id="toc" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="toc"><itemref idref="chapter"/></spine></package>',compress_type=zipfile.ZIP_DEFLATED)
        z.writestr('OEBPS/chapter.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Test</title></head><body><p>云导入测试。</p></body></html>',compress_type=zipfile.ZIP_DEFLATED)
        z.writestr('OEBPS/toc.ncx','<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>Cloud import test</text></docTitle><navMap><navPoint id="c1" playOrder="1"><navLabel><text>Test</text></navLabel><content src="chapter.xhtml"/></navPoint></navMap></ncx>',compress_type=zipfile.ZIP_DEFLATED)
    fixture='static const unsigned char epub_payload[]={'+','.join(map(str,payload.getvalue()))+'};\n'
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',72,96,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([70,110,150])*72)*96))+chunk(b'IEND',b'')
    jpeg=base64.b64decode('/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCABgAEgDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDGooor2DywooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAP/2Q==')
    for name,data in [('png',png),('jpeg',jpeg)]:fixture+='static const unsigned char '+name+'_payload[]={'+','.join(map(str,data))+'};\n'
    fixture+='#define TEST_FONT_PATH '+json.dumps(str(ROOT/'main/assets/builtin.ttf'))+'\n'
    unit=Path(t)/'cloud.c';unit.write_text(stubs+'\n'+header+'\n'+source+'\n'+fixture+tests)
    exe=Path(t)/'cloud'
    deps=args.image_deps or cjson.parent.parent
    png_prefix=args.png_prefix
    png_flags=['-I'+str(png_prefix/'include/libpng16')] if png_prefix else shlex.split(subprocess.check_output(['pkg-config','--cflags','libpng'],text=True))
    png_link=[str(png_prefix/'lib/libpng16.a'),'-lz'] if png_prefix else shlex.split(subprocess.check_output(['pkg-config','--libs','libpng'],text=True))
    work=Path(t)
    (work/'sdkconfig.h').write_text('#define CONFIG_JD_SZBUF 512\n#define CONFIG_JD_FORMAT 0\n#define CONFIG_JD_USE_SCALE 1\n#define CONFIG_JD_TBLCLIP 1\n#define CONFIG_JD_FASTDECODE 0\n')
    jpeg_dir=deps/'espressif__esp_jpeg'
    assert (jpeg_dir/'tjpgd/tjpgd.c').is_file(),'Pass --image-deps managed_components/'
    flags=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections']
    includes=['-I'+str(ROOT/'tools/book_epub_stubs'),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'components/jpegdec/src'),'-I'+str(ROOT/'components/read_pico_webp/include'),'-I'+str(jpeg_dir/'tjpgd'),'-I'+str(jpeg_dir/'include'),'-I'+str(work)]+png_flags
    image_objects=[]
    for i,path in enumerate([ROOT/'main/book/book_cover.c',ROOT/'main/book/book_image_header.c',jpeg_dir/'tjpgd/tjpgd.c',ROOT/'components/jpegdec/src/JPEGDEC.cpp',ROOT/'components/jpegdec/src/jpegdec_shim.cpp']):
        cpp=path.suffix=='.cpp';obj=work/f'image{i}.o';extra=['-fno-sanitize=alignment,shift'] if path.name=='JPEGDEC.cpp' else []
        subprocess.run(['c++' if cpp else 'cc','-std=c++17' if cpp else '-std=c11',*flags,*extra,*includes,'-c',str(path),'-o',str(obj)],check=True);image_objects.append(str(obj))
    # 云服务自身仍用严格警告；其他源文件独立编译，使用其现有stub。/ Keep strict warnings for the service, compiling codec sources separately.
    obj=work/'cloud.o'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-format-truncation','-Wno-deprecated-declarations','-DKIIKO_BOARD_METALIO47='+str(int(args.board=='metalio47')),*flags,'-pthread','-I'+str(cjson),'-I'+str(ROOT/'tools/zip_host_stubs'),'-I'+str(ROOT/'main/book'),'-c',str(unit),'-o',str(obj)],check=True)
    gc='-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections'
    subprocess.run(['c++',*flags,gc,'-pthread','-I'+str(cjson),'-I'+str(ROOT/'tools/zip_host_stubs'),'-I'+str(ROOT/'main/book'),str(obj),*image_objects,'-x','c',str(cjson/'cJSON.c'),str(ROOT/'main/book/zip_reader.c'),'-x','none',*png_link,'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=40)
    print("PASS: binding model payload",args.board)

/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实 OTA 任务的断流、校验、取消、内存不足及与屏幕扫描的并发互斥。
 * English: Real OTA interruption, verification, cancellation, memory exhaustion and display/flash exclusion. */
#include "common.h"
#include "cJSON.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include "../main/ota_online.c"
#if KIIKO_BOARD_METALIO47
#define TEST_SLOT_SIZE 0x500000
#define TEST_SLOT_1 0x510000
#define TEST_OTA_DATA 0xd000
#define TEST_CHIP_ID 32
#else
#define TEST_SLOT_SIZE 0x400000
#define TEST_SLOT_1 0x910000
#define TEST_OTA_DATA 0xd10000
#define TEST_CHIP_ID 9
#endif
static const esp_partition_t running={0x10000,TEST_SLOT_SIZE},inactive={TEST_SLOT_1,TEST_SLOT_SIZE},metadata={TEST_OTA_DATA,0x2000};
#ifndef MINDRESET_TEST_RUNNING_VERSION
#define MINDRESET_TEST_RUNNING_VERSION "0.3.3-alpha.2"
#endif
#ifndef MINDRESET_TEST_IMAGE_SIZE
#define MINDRESET_TEST_IMAGE_SIZE 4096
#endif
static const esp_app_desc_t current={.version=MINDRESET_TEST_RUNNING_VERSION,.project_name=PICO_OTA_PROJECT};
static uint8_t firmware[MINDRESET_TEST_IMAGE_SIZE],written[MINDRESET_TEST_IMAGE_SIZE];static char feed[1600];
static size_t written_size,limit,drop_at;
static int boot_calls,abort_calls,begin_calls,end_error,fail_task;
static bool bad_layout,same_slot,cancel_read,again;
static int drops,open_failures,range_requests,http_opens;
static bool wrong_range,ignored_range;
static char github_feed[1800];static int github_status=404;static unsigned github_queries,github_data;
static int query_status=200,grant_status=302,bad_redirect,signed_expirations;
static unsigned query_requests,grant_requests,data_requests,http_alive;
static bool expire_on_drop,cancel_grant,fail_post,duplicate_range;
static wifi_ps_type_t wifi_ps;
static int ps_disabled,ps_restored;
static int offline_polls;
static bool cancel_reconnect;
static void (*pending)(void*);
static int64_t now;
static size_t internal_free=131072,internal_block=65536,external_free=1048576,external_block=524288;
static size_t failed_allocation;
static pthread_mutex_t flash_display_mutex;
static _Thread_local unsigned lock_depth;
#if KIIKO_BOARD_METALIO47
static pthread_once_t sdk_lock_once = PTHREAD_ONCE_INIT;
static void sdk_lock_init(void) {(void)xSemaphoreCreateRecursiveMutex();}
void spi_flash_op_lock(void) {assert(!pthread_once(&sdk_lock_once,sdk_lock_init));assert(xSemaphoreTakeRecursive((void*)3,portMAX_DELAY));}
void spi_flash_op_unlock(void) {assert(xSemaphoreGiveRecursive((void*)3));}
#endif
static atomic_bool display_active;
static void assert_flash_locked(void) {assert(lock_depth && !atomic_load(&display_active));}

enum HttpKind {QUERY,GRANT,DATA};
#ifndef MINDRESET_LIVE_HOST
struct FakeHttp {const uint8_t *data;size_t size,at,offset,post_size,post_at;enum HttpKind kind;int code;esp_http_client_method_t method;char url[PICO_MINDRESET_CDN_URL_MAX],post[512];int (*event_handler)(esp_http_client_event_t*);void *user_data;};
#endif
void *heap_caps_malloc(size_t n,int c){(void)c;return n==failed_allocation?NULL:malloc(n);}
void *heap_caps_calloc(size_t n,size_t s,int c){(void)c;return n*s==failed_allocation?NULL:calloc(n,s);}
void heap_caps_free(void *p){free(p);}
size_t heap_caps_get_free_size(unsigned c){return c&MALLOC_CAP_INTERNAL?internal_free:external_free;}
size_t heap_caps_get_largest_free_block(unsigned c){return c&MALLOC_CAP_INTERNAL?internal_block:external_block;}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void){
 pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));
 assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
 assert(!pthread_mutex_init(&flash_display_mutex,&attr));assert(!pthread_mutexattr_destroy(&attr));return (void*)3;
}
int xSemaphoreTakeRecursive(SemaphoreHandle_t s,uint32_t wait){assert(s==(void*)3&&wait==portMAX_DELAY);assert(!pthread_mutex_lock(&flash_display_mutex));++lock_depth;return pdTRUE;}
int xSemaphoreGiveRecursive(SemaphoreHandle_t s){assert(s==(void*)3&&lock_depth);--lock_depth;assert(!pthread_mutex_unlock(&flash_display_mutex));return pdTRUE;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
SemaphoreHandle_t xSemaphoreCreateBinary(void){return (void*)2;}
static void run(void){assert(pending);void(*f)(void*)=pending;pending=NULL;f(NULL);}
int xSemaphoreTake(SemaphoreHandle_t s,uint32_t wait){(void)wait;if(s==(void*)2&&pending)run();return 1;}
int xSemaphoreGive(SemaphoreHandle_t s){(void)s;return 1;}
int xTaskCreate(void(*f)(void*),const char*n,unsigned stack,void*a,unsigned p,void*t){(void)n;(void)a;(void)p;(void)t;assert(stack==12288);if(fail_task)return 0;pending=f;return pdPASS;}
void vTaskDelete(void *p){(void)p;}
void vTaskDelay(unsigned ms){now+=(int64_t)ms*1000;}
int64_t esp_timer_get_time(void){return now+=1000;}
const esp_app_desc_t *esp_app_get_description(void){return &current;}
const esp_partition_t *esp_partition_find_first(int type,int sub,const char *label){(void)label;if(bad_layout)return NULL;if(type==1)return &metadata;return sub==16?&running:&inactive;}
const esp_partition_t *esp_ota_get_running_partition(void){return &running;}
const esp_partition_t *esp_ota_get_next_update_partition(void*p){(void)p;return same_slot?&running:&inactive;}
esp_err_t esp_ota_begin(const esp_partition_t*p,size_t n,esp_ota_handle_t*h){assert_flash_locked();assert(p==&inactive&&n==OTA_WITH_SEQUENTIAL_WRITES);++begin_calls;written_size=0;*h=7;return ESP_OK;}
esp_err_t esp_ota_write(esp_ota_handle_t h,const void *p,size_t n){assert_flash_locked();sched_yield();assert(!atomic_load(&display_active));assert(h==7&&written_size+n<=sizeof(written));memcpy(written+written_size,p,n);written_size+=n;return ESP_OK;}
esp_err_t esp_ota_abort(esp_ota_handle_t h){assert_flash_locked();assert(h==7);++abort_calls;return ESP_OK;}
esp_err_t esp_ota_end(esp_ota_handle_t h){assert_flash_locked();assert(h==7);return end_error;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p){assert_flash_locked();assert(p==&inactive);++boot_calls;return ESP_OK;}
void read_pico_transfer_get_status(read_pico_transfer_status_t*s){*s=(read_pico_transfer_status_t){.mode=1,.network_ready=offline_polls==0};}
esp_err_t read_pico_transfer_get_saved_wifi(char*s,bool*b){strcpy(s,"test");*b=true;return ESP_OK;}
void read_pico_transfer_stop(void){}
esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t*c){(void)c;return ESP_OK;}
void read_pico_transfer_service_poll(void){if(offline_polls){--offline_polls;if(cancel_reconnect)atomic_store(&s_cancel,true);}}
esp_err_t esp_wifi_get_ps(wifi_ps_type_t*p){*p=wifi_ps;return ESP_OK;}
esp_err_t esp_wifi_set_ps(wifi_ps_type_t p){wifi_ps=p;if(p==WIFI_PS_NONE)++ps_disabled;else ++ps_restored;return ESP_OK;}
esp_err_t read_pico_transfer_sync_time_online(uint32_t*p){*p=1791200000;return ESP_OK;}
int esp_crt_bundle_attach(void*p){(void)p;return ESP_OK;}

#ifndef MINDRESET_LIVE_HOST
static void header(esp_http_client_handle_t h,const char *key,const char *value){
 esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_HEADER,.user_data=h->user_data,.header_key=(char*)key,.header_value=(char*)value};
 assert(h->event_handler(&event)==ESP_OK);
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*c){
 assert(c->disable_auto_redirect&&c->crt_bundle_attach&&c->timeout_ms==15000);
 assert(strlen(c->url)<PICO_MINDRESET_CDN_URL_MAX);
 struct FakeHttp*h=calloc(1,sizeof(*h));assert(h);strcpy(h->url,c->url);++http_alive;
 if(!strcmp(c->url,PICO_MINDRESET_QUERY_URL)){h->kind=QUERY;++query_requests;h->data=(const uint8_t*)feed;h->size=strlen(feed);h->code=query_status;}
 else if(!strcmp(c->url,pico_github_feed_url(current.version))){h->kind=99;++github_queries;h->data=(const uint8_t*)github_feed;h->size=strlen(github_feed);h->code=github_status;}
 else if(pico_mindreset_url_valid(c->url)){h->kind=GRANT;++grant_requests;h->code=grant_status;}
 else {assert(pico_mindreset_cdn_url_valid(c->url)||pico_github_url_valid(c->url));if(pico_github_url_valid(c->url))++github_data;h->kind=DATA;++data_requests;h->data=firmware;h->size=sizeof(firmware);h->code=200;
  char signature[40];snprintf(signature,sizeof(signature),"sign=%u",grant_requests);if(!pico_github_url_valid(c->url))assert(strstr(c->url,signature));
 }
 h->event_handler=c->event_handler;h->user_data=c->user_data;return h;
}
esp_err_t esp_http_client_set_method(esp_http_client_handle_t h,esp_http_client_method_t method){h->method=method;return ESP_OK;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char*k,const char*v){
 assert(strcmp(k,"Authorization")&&strcmp(k,"Cookie"));
 if(!strcmp(k,"Range")){assert(h->kind==DATA);unsigned long at=0;assert(sscanf(v,"bytes=%lu-",&at)==1);h->offset=at;h->at=ignored_range?0:at;++range_requests;}
 return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){
 ++http_opens;assert(wifi_ps==WIFI_PS_NONE);h->post_size=n;
 assert(h->method==(h->kind==QUERY?HTTP_METHOD_POST:HTTP_METHOD_GET));
 if(h->kind==QUERY)assert(n>0&&n<(int)sizeof(h->post));else assert(n==0);
 if(h->kind==DATA&&open_failures&&!pico_github_url_valid(h->url)){--open_failures;return ESP_FAIL;}
 if(h->kind==DATA&&signed_expirations){--signed_expirations;h->code=403;h->size=0;}
 return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t h,const char *p,int n){
 assert(h->kind==QUERY&&h->post_at+(size_t)n<=h->post_size);
 if(fail_post)return -1;
 if(n>31) n=31;
 memcpy(h->post+h->post_at,p,n);
 h->post_at+=n;
 return n;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){
 if(h->kind==QUERY){
  assert(h->post_at==h->post_size);h->post[h->post_at]=0;cJSON *json=cJSON_Parse(h->post);assert(json);
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(json,"currentVersion")->valuestring,current.version));
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(json,"firmwareId")->valuestring,PICO_MINDRESET_FIRMWARE_ID));
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(json,"status")->valuestring,"ALPHA"));assert(cJSON_GetArraySize(json)==6);cJSON_Delete(json);
 }
 if(h->kind==GRANT&&h->code==302){
  char location[PICO_MINDRESET_CDN_URL_MAX+32];
  snprintf(location,sizeof(location),"https://cdn.mindreset.tech/firmware.bin?sign=%u",grant_requests);
  if(bad_redirect==1)strcpy(location,"http://cdn.mindreset.tech/a");
  if(bad_redirect==2)strcpy(location,"https://cdn.mindreset.tech.evil/a");
  if(bad_redirect==3)strcpy(location,"https://cdn.mindreset.tech@evil/a");
  if(bad_redirect==4){memset(location,'a',sizeof(location)-1);location[sizeof(location)-1]=0;}
  if(bad_redirect==7)strcpy(location,"https://dot.mindreset.tech/download/Loop");
  if(bad_redirect!=5)header(h,"Location",location);
  if(bad_redirect==6)header(h,"Location",location);
  if(cancel_grant)atomic_store(&s_cancel,true);
 }
 if(h->kind==DATA&&h->code==200&&h->offset&&!ignored_range){
  char value[96];snprintf(value,sizeof(value),"bytes %lu-%lu/%lu",(unsigned long)(h->offset+(wrong_range?1:0)),(unsigned long)h->size-1,(unsigned long)h->size);
  header(h,"Content-Range",value);if(duplicate_range)header(h,"Content-Range",value);h->code=206;
 }
 return h->size-(h->code==206?h->offset:0);
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){return h->code;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int ms){(void)h;(void)ms;return ESP_OK;}
int esp_http_client_read(esp_http_client_handle_t h,char*p,int cap){
 if(again){again=false;return -ESP_ERR_HTTP_EAGAIN;}
 if(h->kind==DATA&&drops&&h->at>=drop_at&&!pico_github_url_valid(h->url)){--drops;offline_polls=5;if(expire_on_drop)signed_expirations=1;return -1;}
 size_t end=h->kind==DATA?limit:h->size;if(h->at>=end)return 0;size_t n=end-h->at;if(n>37)n=37;if(n>(size_t)cap)n=cap;
 memcpy(p,h->data+h->at,n);h->at+=n;if(cancel_read&&h->kind==DATA&&h->at>500)atomic_store(&s_cancel,true);return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){return h->at==h->size;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){assert(http_alive);--http_alive;free(h);return ESP_OK;}
#else
#include "ota_mindreset_curl_transport.h"
#endif
int psa_crypto_init(void){return PSA_SUCCESS;}
#ifndef MINDRESET_LIVE_HOST
int psa_hash_setup(psa_hash_operation_t*h,int alg){assert(alg==PSA_ALG_SHA_256);h->bytes=0;return PSA_SUCCESS;}
int psa_hash_update(psa_hash_operation_t*h,const void*p,size_t n){(void)p;h->bytes+=n;return PSA_SUCCESS;}
int psa_hash_finish(psa_hash_operation_t*h,uint8_t*p,size_t cap,size_t*n){assert(cap==32&&h->bytes==4096);memset(p,0,32);*n=32;return PSA_SUCCESS;}
int psa_hash_abort(psa_hash_operation_t*h){(void)h;return PSA_SUCCESS;}
#else
static CC_SHA256_CTX live_hash;
int psa_hash_setup(psa_hash_operation_t*h,int alg){assert(alg==PSA_ALG_SHA_256);h->bytes=0;assert(CC_SHA256_Init(&live_hash));return PSA_SUCCESS;}
int psa_hash_update(psa_hash_operation_t*h,const void*p,size_t n){assert(CC_SHA256_Update(&live_hash,p,(CC_LONG)n));h->bytes+=n;return PSA_SUCCESS;}
int psa_hash_finish(psa_hash_operation_t*h,uint8_t*p,size_t cap,size_t*n){assert(cap==32&&h->bytes==written_size);assert(CC_SHA256_Final(p,&live_hash));*n=32;return PSA_SUCCESS;}
int psa_hash_abort(psa_hash_operation_t*h){(void)h;memset(&live_hash,0,sizeof(live_hash));return PSA_SUCCESS;}
#endif

static void fixture(void){
 pico_online_cancel_join();assert(!http_alive);internal_free=131072;internal_block=65536;external_free=1048576;external_block=524288;failed_allocation=0;
 memset(firmware,0,sizeof(firmware));esp_image_header_t header={.magic=0xe9,.chip_id=TEST_CHIP_ID};memcpy(firmware,&header,sizeof(header));
 esp_app_desc_t desc={.magic_word=ESP_APP_DESC_MAGIC_WORD,.version="0.3.3-alpha.3",.project_name=PICO_OTA_PROJECT};memcpy(firmware+32,&desc,sizeof(desc));
 snprintf(feed,sizeof(feed),"{\"series\":\"read\",\"model\":\"read_0\",\"edition\":1,\"firmwareId\":\"%s\",\"status\":\"ALPHA\",\"version\":\"0.3.3-alpha.3\",\"updateVersion\":\"0.3.3-alpha.3\",\"needUpdate\":true,\"ota\":{\"downloadUrl\":\"https://dot.mindreset.tech/download/Example_123\",\"size\":4096,\"sha256\":\"%064d\"}}",PICO_MINDRESET_FIRMWARE_ID,0);
 limit=4096;bad_layout=same_slot=cancel_read=again=false;written_size=0;boot_calls=abort_calls=begin_calls=end_error=fail_task=0;
 drops=open_failures=range_requests=0;query_status=200;grant_status=302;bad_redirect=signed_expirations=0;
 github_queries=github_data=0;github_status=404;
 snprintf(github_feed,sizeof(github_feed),"{\"schema\":1,\"version\":\"0.3.3-alpha.3\",\"project\":\"%s\",\"board\":\"%s\",\"layout\":\"%s\",\"minimum_base_version\":\"0.3.3-alpha.0\",\"url\":\"https://wegooo-cell.github.io/read-pico-reader/Pico-update-0.3.3-alpha.3.bin\",\"sha256\":\"%064d\",\"notes\":\"test\",\"size\":4096}",PICO_OTA_PROJECT,PICO_OTA_BOARD,PICO_OTA_LAYOUT,0);
 query_requests=grant_requests=data_requests=0;expire_on_drop=cancel_grant=fail_post=duplicate_range=wrong_range=ignored_range=false;
 offline_polls=0;cancel_reconnect=false;
}
static void check_download(void){wifi_ps=WIFI_PS_MIN_MODEM;ps_disabled=ps_restored=0;assert(pico_online_check()==ESP_OK);assert(pico_online_busy());run();assert(s_status->state==PICO_UPDATE_AVAILABLE&&!pico_online_busy());assert(wifi_ps==WIFI_PS_MIN_MODEM&&ps_disabled==1&&ps_restored==1);assert(pico_online_download()==ESP_OK);run();assert(wifi_ps==WIFI_PS_MIN_MODEM&&ps_disabled==2&&ps_restored==2);}
// 两个真实线程交错写闪存与推屏，重复验证锁和递归路径。
// Interleave flash writes and display scans on real threads, including recursive acquisition.
static void *display_thread(void *arg){(void)arg;for(unsigned i=0;i<1000;++i){
 bool held=pico_online_display_begin();assert(held&&lock_depth==1);
 bool nested=pico_online_display_begin();assert(nested&&lock_depth==2);
 atomic_store(&display_active,true);sched_yield();atomic_store(&display_active,false);
 pico_online_display_end(nested);pico_online_display_end(held);assert(!lock_depth);
}return NULL;}
static void *flash_thread(void *arg){(void)arg;for(unsigned i=0;i<1000;++i){assert(flash_write(7,firmware,0)==ESP_OK);assert(!lock_depth);}return NULL;}

#ifndef MINDRESET_LIVE_HOST
int main(void){
 fixture();drop_at=1000;drops=20;github_status=200;check_download();assert(s_status->state==PICO_UPDATE_READY&&github_queries==1&&github_data==1&&range_requests==4&&begin_calls==1&&!memcmp(firmware,written,4096));
 fixture();query_status=500;github_status=200;assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_AVAILABLE&&github_queries==1&&pico_github_url_valid(s_status->release.url));
 fixture();query_status=404;github_status=200;assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_AVAILABLE&&github_queries==1);
 fixture();github_status=200;grant_status=503;check_download();assert(s_status->state==PICO_UPDATE_READY&&github_queries==1&&github_data==1&&written_size==sizeof(firmware));
 fixture();github_status=200;grant_status=503;strstr(github_feed,"0000000000")[0]='1';check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!boot_calls&&!begin_calls&&github_queries==1);
 _Static_assert(sizeof(esp_image_header_t)==24&&sizeof(esp_app_desc_t)==256,"real header sizes");
 assert(!strcmp(pico_mindreset_channel(current.version),"ALPHA")&&!strstr(PICO_MINDRESET_QUERY_URL,"github"));
 fixture();query_status=500;assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_FAILED&&query_requests==2&&!begin_calls&&!grant_requests);
 fixture();query_status=404;assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_FAILED&&github_queries==1&&!begin_calls&&!grant_requests);
 fixture();query_status=302;assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_FAILED&&!grant_requests);
 fixture();fail_post=true;assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_FAILED&&!grant_requests);
 fixture();cJSON *root=cJSON_Parse(feed);cJSON_ReplaceItemInObjectCaseSensitive(root,"status",cJSON_CreateString("RELEASED"));char *text=cJSON_PrintUnformatted(root);strcpy(feed,text);free(text);cJSON_Delete(root);
 assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_FAILED&&!grant_requests);
 fixture();root=cJSON_Parse(feed);cJSON_ReplaceItemInObjectCaseSensitive(root,"ota",cJSON_CreateNull());text=cJSON_PrintUnformatted(root);strcpy(feed,text);free(text);cJSON_Delete(root);
 assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_FAILED&&!grant_requests);
 fixture();again=true;check_download();assert(s_status->state==PICO_UPDATE_READY&&written_size==4096&&!memcmp(firmware,written,4096)&&!boot_calls&&grant_requests==1);assert(pico_online_commit()==ESP_OK&&boot_calls==1);
 fixture();signed_expirations=1;check_download();assert(s_status->state==PICO_UPDATE_READY&&grant_requests==2&&data_requests==2&&begin_calls==1);
 fixture();drop_at=1000;drops=1;expire_on_drop=true;check_download();assert(s_status->state==PICO_UPDATE_READY&&grant_requests==3&&range_requests==2&&begin_calls==1&&!memcmp(firmware,written,4096));
 fixture();drop_at=100;drops=1;check_download();assert(s_status->state==PICO_UPDATE_READY&&begin_calls==1&&range_requests==1);
 fixture();drop_at=1000;drops=2;check_download();assert(s_status->state==PICO_UPDATE_READY&&begin_calls==1&&range_requests==2);
 fixture();open_failures=2;check_download();assert(s_status->state==PICO_UPDATE_READY&&grant_requests==3);
 fixture();limit=1024;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&abort_calls==1&&!boot_calls&&range_requests==3);
 for(int i=1;i<=7;++i){fixture();bad_redirect=i;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls&&!boot_calls&&grant_requests==4&&!data_requests);}
 fixture();grant_status=404;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls&&!data_requests);
 fixture();cancel_grant=true;check_download();assert(s_status->state==PICO_UPDATE_CANCELLED&&!begin_calls&&!boot_calls);
 fixture();signed_expirations=20;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&grant_requests==4&&!begin_calls);
 fixture();drop_at=1000;drops=1;wrong_range=true;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&abort_calls==1&&!boot_calls);
 fixture();drop_at=1000;drops=1;ignored_range=true;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&abort_calls==1&&!boot_calls);
 fixture();drop_at=1000;drops=1;duplicate_range=true;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&abort_calls==1&&!boot_calls);
 fixture();drop_at=1000;drops=20;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&abort_calls==1&&!boot_calls&&range_requests==3);
 fixture();drop_at=1000;drops=1;cancel_reconnect=true;check_download();assert(s_status->state==PICO_UPDATE_CANCELLED&&abort_calls==1&&!boot_calls);
 fixture();cancel_read=true;check_download();assert(s_status->state==PICO_UPDATE_CANCELLED&&abort_calls==1&&!boot_calls);
 fixture();check_download();pico_online_cancel_join();assert(pico_online_commit()!=ESP_OK&&!boot_calls);
 fixture();char *sha=strstr(feed,"0000000000");assert(sha);sha[0]='1';check_download();assert(s_status->state==PICO_UPDATE_FAILED&&abort_calls==1&&!boot_calls);
 fixture();firmware[80]='X';check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls&&!boot_calls);
 fixture();firmware[48]='X';check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls&&!boot_calls);
 fixture();firmware[12]=32;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls&&!boot_calls);
 fixture();same_slot=true;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls);
 fixture();end_error=ESP_FAIL;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!boot_calls);
 fixture();bad_layout=true;assert(pico_online_check()==ESP_OK);run();assert(s_status->state==PICO_UPDATE_FAILED&&!query_requests&&!begin_calls);
 fixture();assert(pico_online_check()==ESP_OK);pico_online_cancel_join();assert(!pico_online_busy()&&s_status->state==PICO_UPDATE_CANCELLED);
 fixture();fail_task=1;assert(pico_online_check()==ESP_ERR_NO_MEM&&!pico_online_busy());
 fixture();internal_free=49151;assert(pico_online_check()==ESP_ERR_NO_MEM&&!pending&&!begin_calls);
 fixture();internal_block=16383;assert(pico_online_check()==ESP_ERR_NO_MEM&&!pending&&!begin_calls);
 fixture();external_free=524287;assert(pico_online_check()==ESP_ERR_NO_MEM&&!pending&&!begin_calls);
 fixture();external_block=32767;assert(pico_online_check()==ESP_ERR_NO_MEM&&!pending&&!begin_calls);
 fixture();assert(pico_online_check()==ESP_OK);internal_free=32767;int opens=http_opens;run();assert(s_status->state==PICO_UPDATE_FAILED&&http_opens==opens&&!begin_calls&&wifi_ps==WIFI_PS_MIN_MODEM);
 fixture();failed_allocation=4096;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls&&!boot_calls);
 fixture();failed_allocation=288;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls&&!boot_calls);
 fixture();failed_allocation=PICO_MINDRESET_CDN_URL_MAX;check_download();assert(s_status->state==PICO_UPDATE_FAILED&&!begin_calls&&!boot_calls);
 fixture();pthread_t display_job,flash_job;assert(!pthread_create(&display_job,NULL,display_thread,NULL));assert(!pthread_create(&flash_job,NULL,flash_thread,NULL));assert(!pthread_join(display_job,NULL));assert(!pthread_join(flash_job,NULL));assert(!lock_depth&&!http_alive);assert(!pthread_mutex_destroy(&flash_display_mutex));free(s_status);s_status=NULL;
 puts("PASS: MindReset primary/GitHub fallback with same-image Range resume; strict channel identity; 302 host/length/duplicate guards; renewed signatures after 403 and midstream drops; exact Range; cancellation; image identity/hash; inactive-slot commit; low memory; 2000 concurrent display/flash operations");
}
#endif

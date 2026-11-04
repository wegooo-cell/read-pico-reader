# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Deterministic scheduler and font-API fixtures for the real reader reload transaction."""
from home_cover_cache_host_test import function
from board_source_view import select_source
FIXTURE=r'''
#include <stdlib.h>
#include <stdatomic.h>
#define pdPASS 1
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif
typedef struct {char path[160];}ttf_font_prepared_t;
typedef struct {char path[160];atomic_bool cancel,done;ttf_font_prepared_t *prepared;esp_err_t error;}reader_font_job_t;
static reader_font_job_t *s_font_job;
static void (*queued_worker)(void *);static void *queued_arg;
static bool create_fails;
static unsigned retired;
static int xTaskCreate(void(*fn)(void*),const char*name,unsigned stack,void*arg,int priority,void*handle){
 (void)name;(void)stack;(void)priority;(void)handle;if(create_fails)return 0;assert(!queued_worker);queued_worker=fn;queued_arg=arg;return pdPASS;}
static void run_worker(void){if(queued_worker){void(*fn)(void*)=queued_worker;void*arg=queued_arg;queued_worker=NULL;fn(arg);}}
static void vTaskDelete(void*p){assert(!p);}
static void vTaskDelay(int n){assert(n==1);run_worker();}
static ttf_font_prepared_t *ttf_font_prepare(const char *path,const atomic_bool *cancel,esp_err_t *error){
 ++opens;*error=-1;if(load_fails||atomic_load(cancel))return NULL;
 ttf_font_prepared_t *p=calloc(1,sizeof(*p));assert(p);snprintf(p->path,sizeof(p->path),"%s",path);*error=ESP_OK;return p;}
static void ttf_font_publish(ttf_font_prepared_t *p){assert(locked);char old[160];strcpy(old,active);strcpy(active,p->path);strcpy(p->path,old);FONT_STAGE_PUBLISHED();}
static void ttf_font_prepared_free(ttf_font_prepared_t*p){if(p){++retired;free(p);}}
'''
def implementation(source):
    return '\n'.join(function(n,source) for n in ['reader_font_worker','reader_font_reload_cancel','select_reading_font_item','reader_font_reload_tick'])
def completion_wrapper():
    return r'''
static app_redraw_t select_and_complete(app_ctx_t *ctx,const ttf_font_item_t *item){
 app_redraw_t result=select_reading_font_item(ctx,item);
 if(s_font_job){assert(!locked);run_worker();result=reader_font_reload_tick(ctx);}
 return result;
}
'''

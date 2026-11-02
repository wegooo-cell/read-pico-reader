#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实双缓冲写入与上传提交：并发、短包、OOM、断网和写卡错误。
English: Execute real two-slot upload/commit with concurrency, short packets, OOM, disconnects and storage faults.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
unit=r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define KIIKO_BOARD_METALIO47 1
#define ESP_PLATFORM 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define portMAX_DELAY -1
#define pdPASS 1
#define ESP_LOGI(...) ((void)0)
#define TRANSFER_COMMITTED_BACKUP_RETAINED 299
static int allocations,fail_alloc,fail_task,threads,sem_live;
static pthread_t thread;
static atomic_uint write_calls;
static atomic_bool writing;
static unsigned short_write,overlaps;static bool close_error;
static size_t *live=NULL;
static void *heap_caps_malloc(size_t n,int caps){(void)caps;if(++allocations==fail_alloc)return NULL;size_t*p=malloc(sizeof(size_t)+n);assert(p);*p=n;return p+1;}
static void *heap_caps_calloc(size_t n,size_t bytes,int caps){void*p=heap_caps_malloc(n*bytes,caps);if(p)memset(p,0,n*bytes);return p;}
static void heap_caps_free(void*p){if(p)free((size_t*)p-1);}
static int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
typedef struct{pthread_mutex_t mutex;pthread_cond_t cond;unsigned count;}*SemaphoreHandle_t;
static SemaphoreHandle_t xSemaphoreCreateBinary(void){if(++allocations==fail_alloc)return NULL;SemaphoreHandle_t s=calloc(1,sizeof(*s));assert(s);pthread_mutex_init(&s->mutex,NULL);pthread_cond_init(&s->cond,NULL);++sem_live;return s;}
static void xSemaphoreTake(SemaphoreHandle_t s,int timeout){assert(timeout==portMAX_DELAY);pthread_mutex_lock(&s->mutex);while(!s->count)pthread_cond_wait(&s->cond,&s->mutex);--s->count;pthread_mutex_unlock(&s->mutex);}
static void xSemaphoreGive(SemaphoreHandle_t s){pthread_mutex_lock(&s->mutex);assert(!s->count);++s->count;pthread_cond_signal(&s->cond);pthread_mutex_unlock(&s->mutex);}
static void vSemaphoreDelete(SemaphoreHandle_t s){assert(!pthread_mutex_destroy(&s->mutex));assert(!pthread_cond_destroy(&s->cond));free(s);--sem_live;}
static struct{void(*fn)(void*);void*arg;} job;
static void *runner(void *unused){(void)unused;job.fn(job.arg);return NULL;}
static int xTaskCreate(void(*fn)(void*),const char*name,unsigned stack,void*arg,unsigned priority,void*handle){assert(!strcmp(name,"s31_sd_write")&&stack==4096&&priority==4&&!handle);if(fail_task)return 0;job.fn=fn;job.arg=arg;assert(!pthread_create(&thread,NULL,runner,NULL));++threads;return pdPASS;}
static void vTaskDelete(void *t){assert(!t);}
static size_t write_fault(const void *bytes,size_t size,size_t count,FILE *file){
 assert(size==1);unsigned call=atomic_fetch_add(&write_calls,1)+1;atomic_store(&writing,true);usleep(2000);
 size_t wrote=fwrite(bytes,size,short_write==call?count-1:count,file);atomic_store(&writing,false);return wrote;
}
static int close_fault(FILE*f){int r=fclose(f);return close_error?EOF:r;}
#define fclose close_fault
#define fwrite write_fault
#include "transfer_stream_writer.h"
static struct{unsigned bytes,elapsed_ms,receive_ms,write_ms,writes,block,queue_ms;int error;}s_receive_metrics;
typedef int(*receive_cb_t)(void*,char*,size_t);typedef void(*progress_cb_t)(size_t);
static unsigned access_depth;static bool deny_access;
typedef struct{bool allowed,active;}read_pico_sd_access_scope_t;
static read_pico_sd_access_scope_t read_pico_sd_access_scope(const char *path){(void)path;if(deny_access)return (read_pico_sd_access_scope_t){0};++access_depth;return (read_pico_sd_access_scope_t){true,true};}
static void read_pico_sd_access_scope_end(read_pico_sd_access_scope_t *scope){if(scope->active){assert(access_depth);--access_depth;}}
static int commit_file(const char*part,const char*path){assert(access_depth);return rename(part,path);}
'''
unit+=function('receive_file',root/'components/read_pico_transfer/read_pico_transfer.c')
unit+=r'''
#undef fwrite
#undef fclose
static unsigned char input[300001],output[300001];
static size_t received,receive_calls,bad_receive,progress_bytes,packet;
static bool oversize;
static int receive_test(void*ctx,char*buf,size_t n){(void)ctx;if(++receive_calls==bad_receive)return oversize?(int)n+1:-1;if(atomic_load(&writing))++overlaps;usleep(100);size_t got=n<packet?n:packet;memcpy(buf,input+received,got);received+=got;return (int)got;}
static void progress_test(size_t n){assert(n>=progress_bytes);progress_bytes=n;}
static void reset(void){allocations=fail_alloc=fail_task=threads=sem_live=0;atomic_store(&write_calls,0);atomic_store(&writing,false);short_write=overlaps=received=receive_calls=bad_receive=progress_bytes=0;packet=1371;oversize=false;}
int main(void){char buf[32768];for(size_t i=0;i<sizeof(input);++i)input[i]=(unsigned char)(i*73+(i>>9));
 for(unsigned test=0;test<25;++test){reset();close_error=false;if(test>=17)fail_alloc=test-9;if(test>=1&&test<=7)fail_alloc=test;if(test==8)fail_task=1;if(test==9)bad_receive=10;if(test==10){bad_receive=10;oversize=true;}if(test>=11&&test<=13)short_write=test-10;if(test==14)packet=32768;if(test==15)packet=1;
  FILE*f=fopen("target.epub","wb");assert(f);assert(fwrite("old",1,3,f)==3);assert(!fclose(f));
  close_error=test==16;size_t total=test==15?127:sizeof(input);int result=receive_file("target.epub","target.part",total,buf,sizeof(buf),receive_test,NULL,progress_test);
  if(threads)assert(!pthread_join(thread,NULL));assert(!sem_live && !access_depth);
  if((test>=9&&test<=13)||test==16){assert(result==(test<=10?408:507));f=fopen("target.epub","rb");assert(f&&fread(output,1,sizeof(output),f)==3&&!memcmp(output,"old",3));}
  else {assert(!result&&received==total&&progress_bytes==total);f=fopen("target.epub","rb");assert(f&&fread(output,1,sizeof(output),f)==total&&!memcmp(input,output,total));if(!test)assert(overlaps&&threads==1&&s_receive_metrics.writes==10);}
  assert(!fclose(f));assert(access("target.part",F_OK));assert(!remove("target.epub"));
 }
 deny_access=true;assert(receive_file("denied.epub","denied.part",10,buf,sizeof(buf),receive_test,NULL,progress_test)==507);assert(access("denied.part",F_OK));
 puts("PASS: actual receive/write overlap, byte-exact ordered commit, small/fragmented/large packets, seven allocation faults and task failure fall back, receive overrun/disconnect/write faults join before cleanup and preserve old file");
}
'''
with tempfile.TemporaryDirectory(prefix='s31-stream-') as folder:
 p=Path(folder);(p/'freertos').mkdir()
 for h in ['FreeRTOS.h','task.h','semphr.h']:(p/'freertos'/h).write_text('')
 (p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-fsanitize=address,undefined','-pthread','-I'+str(p),'-I'+str(root/'components/read_pico_transfer'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True,timeout=45)

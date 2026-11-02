#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""S31 SD VFS admission: actual wrappers, lifetime/drain races and failed-close recovery."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
STUB=r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) assert(pthread_mutex_lock(p)==0)
#define portEXIT_CRITICAL(p) assert(pthread_mutex_unlock(p)==0)
#define configASSERT assert
#define pdMS_TO_TICKS(x) (x)
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void*)pthread_self();}
static TickType_t xTaskGetTickCount(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000+t.tv_nsec/1000000;}
static void vTaskDelay(unsigned n){usleep(n*1000);}
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
#define ESP_ERR_INVALID_STATE 0x103
struct _reent {int _errno;};
'''
UNIT=r'''
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include "access_stub.h"
#include "@SOURCE@"
static int next_fd=30,close_error,open_error;
static atomic_int entered,allow_stat,transition_ready,release_transition;
int __real__open_r(struct _reent *r,const char *p,int f,int m){(void)r;(void)p;(void)f;(void)m;return open_error?-1:next_fd++;}
int __real__close_r(struct _reent *r,int f){(void)r;(void)f;return close_error;}
DIR *__real_opendir(const char *p){(void)p;return (DIR*)malloc(8);}
int __real_closedir(DIR *d){free(d);return close_error;}
int __real__stat_r(struct _reent *r,const char *p,struct stat *s){
 (void)r;(void)s;
 if(strstr(p,"blocked")){
  atomic_store(&entered,1);while(!atomic_load(&allow_stat))usleep(1000);
  // 已接纳操作在卸载等待期间仍可嵌套完成访问。/ Admitted operations may finish nested access while transitions drain.
  int f=__wrap__open_r(NULL,"/sdcard/nested",0,0);assert(f>=0);assert(__wrap__close_r(NULL,f)==0);
 }
 return 0;
}
int __real__unlink_r(struct _reent *r,const char *p){(void)r;(void)p;return 0;}
int __real__rename_r(struct _reent *r,const char *a,const char *b){(void)r;(void)a;(void)b;return 0;}
int __real__link_r(struct _reent *r,const char *a,const char *b){(void)r;(void)a;(void)b;return 0;}
int __real_mkdir(const char *p,mode_t m){(void)p;(void)m;return 0;}
int __real_rmdir(const char *p){(void)p;return 0;}
int __real_access(const char *p,int m){(void)p;(void)m;return 0;}
int __real_utime(const char *p,const struct utimbuf *t){(void)p;(void)t;return 0;}
int __real_truncate(const char *p,off_t n){(void)p;(void)n;return 0;}
int __real_esp_vfs_fat_info(const char *p,uint64_t *a,uint64_t *b){(void)p;*a=2048;*b=1024;return 0;}
static void *stat_worker(void *p){(void)p;struct stat s;assert(__wrap__stat_r(NULL,"/sdcard/blocked",&s)==0);return NULL;}
static void *transition_worker(void *p){
 (void)p;assert(read_pico_sd_access_transition_begin(2000));atomic_store(&transition_ready,1);
 while(!atomic_load(&release_transition))usleep(1000);
 read_pico_sd_access_set_mounted(false);read_pico_sd_access_transition_end();return NULL;
}
static void *other_task(void *p){
 (void)p;errno=0;assert(__wrap__open_r(NULL,"/sdcard/new",0,0)<0&&errno==EBUSY);
 struct stat st;assert(__wrap__stat_r(NULL,"/sdcard/x",&st)<0);
 assert(__wrap__rename_r(NULL,"/flash/a","/sdcard/a")<0);
 assert(__wrap__open_r(NULL,"/flash/x",0,0)>=0);
 return NULL;
}
static int guarded_commit(bool early){
 read_pico_sd_access_scope_t scope __attribute__((cleanup(read_pico_sd_access_scope_end)))=read_pico_sd_access_scope("/sdcard");
 assert(scope.allowed);
 int f=__wrap__open_r(NULL,"/sdcard/temp",0,0);assert(f>=0);assert(__wrap__close_r(NULL,f)==0);
 assert(!read_pico_sd_access_transition_begin(0));
 if(early)return -1;
 assert(__wrap__rename_r(NULL,"/sdcard/temp","/sdcard/final")==0);return 0;
}
int main(void){
 assert(__wrap__open_r(NULL,"/sdcard/a",0,0)<0);
 assert(__wrap__open_r(NULL,"/sdcardish/a",0,0)>=0);
 int flash=__wrap__open_r(NULL,"/flash/a",0,0);assert(flash>=0);
 assert(read_pico_sd_access_transition_begin(0));
 pthread_t t;assert(!pthread_create(&t,NULL,other_task,NULL));assert(!pthread_join(t,NULL));
 read_pico_sd_access_set_mounted(true);read_pico_sd_access_transition_end();
 int f=__wrap__open_r(NULL,"/sdcard/a",0,0);assert(f>=0);
 assert(!read_pico_sd_access_transition_begin(0));
 close_error=-1;assert(__wrap__close_r(NULL,f)==-1);close_error=0;
 assert(read_pico_sd_access_transition_begin(0));assert(read_pico_sd_access_transition_begin(0));
 read_pico_sd_access_transition_end();read_pico_sd_access_transition_end();
 DIR *d=__wrap_opendir("/sdcard");assert(d);
 assert(!read_pico_sd_access_transition_begin(0));close_error=-1;assert(__wrap_closedir(d)==-1);close_error=0;
 assert(read_pico_sd_access_transition_begin(0));read_pico_sd_access_transition_end();
 open_error=1;assert(__wrap__open_r(NULL,"/sdcard/fail",0,0)<0);open_error=0;
 assert(read_pico_sd_access_transition_begin(0));read_pico_sd_access_transition_end();
 assert(read_pico_sd_access_begin());assert(!read_pico_sd_access_transition_begin(0));read_pico_sd_access_end();
 assert(guarded_commit(true)==-1 && guarded_commit(false)==0);
 assert(read_pico_sd_access_transition_begin(0));read_pico_sd_access_transition_end();
 int files[48];for(int i=0;i<48;i++){files[i]=__wrap__open_r(NULL,"/sdcard/many",0,0);assert(files[i]>=0);}
 assert(__wrap__open_r(NULL,"/sdcard/full",0,0)<0&&errno==EMFILE);
 for(int i=0;i<48;i++)assert(__wrap__close_r(NULL,files[i])==0);
 assert(__wrap__close_r(NULL,flash)==0);
 pthread_t a,b;assert(!pthread_create(&a,NULL,stat_worker,NULL));while(!atomic_load(&entered))usleep(1000);
 assert(!pthread_create(&b,NULL,transition_worker,NULL));
 // 在途stat尚未结束，过渡不得提前成功。/ The transition cannot finish before the admitted stat drains.
 usleep(20000);assert(!atomic_load(&transition_ready));
 assert(!pthread_create(&t,NULL,other_task,NULL));assert(!pthread_join(t,NULL));
 atomic_store(&allow_stat,1);assert(!pthread_join(a,NULL));while(!atomic_load(&transition_ready))usleep(1000);
 uint64_t total,available;assert(__wrap_esp_vfs_fat_info("/sdcard",&total,&available)==ESP_ERR_INVALID_STATE);
 assert(__wrap_esp_vfs_fat_info("/flash",&total,&available)==0);
 atomic_store(&release_transition,1);assert(!pthread_join(b,NULL));
 assert(__wrap__open_r(NULL,"/sdcard/after",0,0)<0);
 puts("PASS: actual SD-only VFS wrappers: file/dir lifetimes, failed closes, exhaustion, nested draining, owner exclusion, timeout retention and flash isolation");
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'access_stub.h').write_text(STUB)
 for name in ('sys/reent.h','esp_log.h','esp_vfs_fat.h','freertos/FreeRTOS.h','freertos/task.h'):
  x=p/name;x.parent.mkdir(parents=True,exist_ok=True);x.write_text('#include "access_stub.h"\n')
 (p/'test.c').write_text(UNIT.replace('@SOURCE@',str(ROOT/'components/read_pico/read_pico_sd_access.c')))
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-g','-I'+str(p),'-I'+str(ROOT/'components/read_pico/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=15)

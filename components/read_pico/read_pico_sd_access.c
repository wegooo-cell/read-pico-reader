/*
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * S31 TF的统一VFS访问和生命周期保护；内部Flash不参与。/ S31 SD VFS admission and lifecycle protection; internal flash is independent.
 */
#include "read_pico_sd_access.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/reent.h>
#include <sys/stat.h>
#include <dirent.h>
#include <utime.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 数量有界；槽位耗尽时拒绝新访问，不执行无法追踪的open。/ Bounded slots; exhaustion rejects opens before creating untracked handles.
#define SD_HANDLES 48
#define SD_TASKS 16
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_mounted;
static TaskHandle_t s_owner;
static unsigned s_owner_depth, s_pending;
static struct {TaskHandle_t task; unsigned depth;} s_tasks[SD_TASKS];
static struct {bool used, directory, closing; int fd; DIR *dir;} s_handles[SD_HANDLES];

static bool sd_path(const char *path) {
    return path && !strncmp(path,"/sdcard",7) && (!path[7] || path[7]=='/');
}
bool read_pico_sd_access_begin(void) {
    TaskHandle_t task=xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_lock);
    int slot=-1,empty=-1;
    for(int i=0;i<SD_TASKS;i++) {
        if(s_tasks[i].depth && s_tasks[i].task==task)slot=i;
        if(!s_tasks[i].depth)empty=i;
    }
    bool admitted=s_owner==task || (s_mounted && (!s_owner || slot>=0));
    if(slot<0)slot=empty;
    if(admitted && slot>=0) {
        s_tasks[slot].task=task;++s_tasks[slot].depth;++s_pending;
    } else admitted=false;
    portEXIT_CRITICAL(&s_lock);
    return admitted;
}
read_pico_sd_access_scope_t read_pico_sd_access_scope(const char *path) {
    if(!sd_path(path))return (read_pico_sd_access_scope_t){.allowed=true};
    bool admitted=read_pico_sd_access_begin();
    return (read_pico_sd_access_scope_t){.allowed=admitted,.active=admitted};
}
void read_pico_sd_access_end(void) {
    TaskHandle_t task=xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_lock);
    for(int i=0;i<SD_TASKS;i++)if(s_tasks[i].depth && s_tasks[i].task==task) {
        --s_tasks[i].depth;--s_pending;portEXIT_CRITICAL(&s_lock);return;
    }
    portEXIT_CRITICAL(&s_lock);
    configASSERT(false);
}
bool read_pico_sd_access_transition_begin(uint32_t timeout_ms) {
    TaskHandle_t task=xTaskGetCurrentTaskHandle();
    TickType_t start=xTaskGetTickCount();
    bool claimed=false;
    for(;;) {
        portENTER_CRITICAL(&s_lock);
        // 不允许事务等待自己，避免回调升级造成死锁。/ Never upgrade an admitted operation and wait for itself.
        for(int i=0;i<SD_TASKS;i++)if(s_tasks[i].depth && s_tasks[i].task==task) {
            portEXIT_CRITICAL(&s_lock);return false;
        }
        if(!claimed && s_owner==task) {
            ++s_owner_depth;portEXIT_CRITICAL(&s_lock);return true;
        }
        if(!s_owner){s_owner=task;s_owner_depth=1;claimed=true;}
        bool idle=claimed && !s_pending;
        for(int i=0;idle && i<SD_HANDLES;i++)if(s_handles[i].used)idle=false;
        portEXIT_CRITICAL(&s_lock);
        if(idle)return true;
        if(xTaskGetTickCount()-start>=pdMS_TO_TICKS(timeout_ms))break;
        vTaskDelay(1);
    }
    portENTER_CRITICAL(&s_lock);
    if(claimed){s_owner=NULL;s_owner_depth=0;}
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW("sd_access","SD transition busy; retain filesystem and handles");
    return false;
}
void read_pico_sd_access_transition_end(void) {
    portENTER_CRITICAL(&s_lock);
    configASSERT(s_owner==xTaskGetCurrentTaskHandle() && s_owner_depth);
    if(!--s_owner_depth)s_owner=NULL;
    portEXIT_CRITICAL(&s_lock);
}
void read_pico_sd_access_set_mounted(bool mounted) {
    portENTER_CRITICAL(&s_lock);s_mounted=mounted;portEXIT_CRITICAL(&s_lock);
}
static int reserve_handle(bool directory) {
    portENTER_CRITICAL(&s_lock);
    for(int i=0;i<SD_HANDLES;i++)if(!s_handles[i].used) {
        s_handles[i].used=true;s_handles[i].directory=directory;
        s_handles[i].closing=false;s_handles[i].fd=-1;s_handles[i].dir=NULL;
        portEXIT_CRITICAL(&s_lock);return i;
    }
    portEXIT_CRITICAL(&s_lock);errno=EMFILE;return -1;
}
static int closing_handle(int fd,DIR *dir,bool directory) {
    portENTER_CRITICAL(&s_lock);
    for(int i=0;i<SD_HANDLES;i++)if(s_handles[i].used && !s_handles[i].closing &&
        s_handles[i].directory==directory && (directory?s_handles[i].dir==dir:s_handles[i].fd==fd)) {
        s_handles[i].closing=true;portEXIT_CRITICAL(&s_lock);return i;
    }
    portEXIT_CRITICAL(&s_lock);return -1;
}

int __real__open_r(struct _reent*,const char*,int,int);
int __wrap__open_r(struct _reent *r,const char *path,int flags,int mode) {
    if(!sd_path(path))return __real__open_r(r,path,flags,mode);
    if(!read_pico_sd_access_begin()){errno=EBUSY;return -1;}
    int slot=reserve_handle(false),fd=-1;
    if(slot>=0) {
        fd=__real__open_r(r,path,flags,mode);
        portENTER_CRITICAL(&s_lock);
        if(fd<0)s_handles[slot].used=false;else s_handles[slot].fd=fd;
        portEXIT_CRITICAL(&s_lock);
    }
    read_pico_sd_access_end();return fd;
}
int __real__close_r(struct _reent*,int);
int __wrap__close_r(struct _reent *r,int fd) {
    int slot=closing_handle(fd,NULL,false);
    int result=__real__close_r(r,fd);
    // SDK的VFS即使f_close出错也回收描述符；不得让拔卡永久占住计数。
    // SDK VFS recycles descriptors even when f_close fails; removal must not leak admission counts.
    if(slot>=0){portENTER_CRITICAL(&s_lock);s_handles[slot].used=false;portEXIT_CRITICAL(&s_lock);}
    return result;
}
DIR *__real_opendir(const char*);
DIR *__wrap_opendir(const char *path) {
    if(!sd_path(path))return __real_opendir(path);
    if(!read_pico_sd_access_begin()){errno=EBUSY;return NULL;}
    int slot=reserve_handle(true);DIR *dir=NULL;
    if(slot>=0) {
        dir=__real_opendir(path);
        portENTER_CRITICAL(&s_lock);
        if(!dir)s_handles[slot].used=false;else s_handles[slot].dir=dir;
        portEXIT_CRITICAL(&s_lock);
    }
    read_pico_sd_access_end();return dir;
}
int __real_closedir(DIR*);
int __wrap_closedir(DIR *dir) {
    int slot=closing_handle(-1,dir,true),result=__real_closedir(dir);
    if(slot>=0){portENTER_CRITICAL(&s_lock);s_handles[slot].used=false;portEXIT_CRITICAL(&s_lock);}
    return result;
}
#define SD_PATH_CALL(name,params,path,args) \
    int __real_##name params; \
    int __wrap_##name params { \
        if(!sd_path(path))return __real_##name args; \
        if(!read_pico_sd_access_begin()){errno=EBUSY;return -1;} \
        int result=__real_##name args;read_pico_sd_access_end();return result; \
    }
SD_PATH_CALL(_stat_r,(struct _reent *r,const char *p,struct stat *s),p,(r,p,s))
SD_PATH_CALL(_unlink_r,(struct _reent *r,const char *p),p,(r,p))
SD_PATH_CALL(_rename_r,(struct _reent *r,const char *a,const char *b),(sd_path(a)?a:b),(r,a,b))
SD_PATH_CALL(_link_r,(struct _reent *r,const char *a,const char *b),(sd_path(a)?a:b),(r,a,b))
SD_PATH_CALL(mkdir,(const char *p,mode_t m),p,(p,m))
SD_PATH_CALL(rmdir,(const char *p),p,(p))
SD_PATH_CALL(access,(const char *p,int m),p,(p,m))
SD_PATH_CALL(utime,(const char *p,const struct utimbuf *t),p,(p,t))
SD_PATH_CALL(truncate,(const char *p,off_t n),p,(p,n))
#undef SD_PATH_CALL
esp_err_t __real_esp_vfs_fat_info(const char*,uint64_t*,uint64_t*);
esp_err_t __wrap_esp_vfs_fat_info(const char *p,uint64_t *total,uint64_t *free_bytes) {
    if(!sd_path(p))return __real_esp_vfs_fat_info(p,total,free_bytes);
    if(!read_pico_sd_access_begin())return ESP_ERR_INVALID_STATE;
    esp_err_t result=__real_esp_vfs_fat_info(p,total,free_bytes);
    read_pico_sd_access_end();return result;
}

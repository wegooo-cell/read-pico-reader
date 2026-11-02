/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：S31大文件最多四槽顺序写入；接收方只填空槽，收齐工作线程后才能关闭或提交文件。
 * English: S31 bounded four-slot sequential writes; receive into free slots and join before closing or committing.
 */
#pragma once
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

enum {TRANSFER_WRITE_SLOTS=4};
typedef struct {
    FILE *file;
    unsigned slots;
    char *buffer[TRANSFER_WRITE_SLOTS];
    size_t bytes[TRANSFER_WRITE_SLOTS];
    SemaphoreHandle_t ready[TRANSFER_WRITE_SLOTS], free[TRANSFER_WRITE_SLOTS], joined;
    atomic_int error;
    int64_t write_us;
    unsigned writes;
} transfer_writer_t;

static void transfer_writer_task(void *opaque) {
    transfer_writer_t *w=opaque;
    for(unsigned slot=0;;slot=(slot+1)%w->slots) {
        xSemaphoreTake(w->ready[slot],portMAX_DELAY);
        size_t bytes=w->bytes[slot];
        if(!bytes)break;
        if(!atomic_load(&w->error)) {
            int64_t start=esp_timer_get_time();
            if(fwrite(w->buffer[slot],1,bytes,w->file)!=bytes)atomic_store(&w->error,507);
            w->write_us+=esp_timer_get_time()-start;
            ++w->writes;
        }
        xSemaphoreGive(w->free[slot]);
    }
    // 发出完成后不再访问调用方资源；不能强删正在写卡的任务。
    // Touch no caller resources after completion; never force-delete a task inside card I/O.
    xSemaphoreGive(w->joined);
    vTaskDelete(NULL);
}

static void transfer_writer_destroy(transfer_writer_t *w) {
    if(!w)return;
    for(unsigned i=0;i<TRANSFER_WRITE_SLOTS;++i) {
        if(w->ready[i])vSemaphoreDelete(w->ready[i]);
        if(w->free[i])vSemaphoreDelete(w->free[i]);
    }
    if(w->joined)vSemaphoreDelete(w->joined);
    for(unsigned i=1;i<TRANSFER_WRITE_SLOTS;i++)heap_caps_free(w->buffer[i]);
    heap_caps_free(w);
}

static transfer_writer_t *transfer_writer_start(FILE *file,char *first,size_t cap,size_t total) {
    // 最多四槽吸收短暂写卡停顿；PSRAM不足回到双槽，再不足沿用同步写入。
    // Up to four slots absorb card stalls; PSRAM pressure falls back to two slots, then synchronous writes.
    if(!cap || total/cap<4)return NULL;
    transfer_writer_t *w=heap_caps_calloc(1,sizeof(*w),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!w)return NULL;
    atomic_init(&w->error,0);
    w->file=file;w->buffer[0]=first;w->slots=TRANSFER_WRITE_SLOTS;
    for(unsigned i=1;i<TRANSFER_WRITE_SLOTS;i++) {
        w->buffer[i]=heap_caps_malloc(cap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!w->buffer[i]) {
            if(i==1)goto fail;
            for(unsigned j=2;j<TRANSFER_WRITE_SLOTS;j++){heap_caps_free(w->buffer[j]);w->buffer[j]=NULL;}
            w->slots=2;break;
        }
    }
    for(unsigned i=0;i<w->slots;++i) {
        w->ready[i]=xSemaphoreCreateBinary();w->free[i]=xSemaphoreCreateBinary();
        if(!w->ready[i] || !w->free[i])goto fail;
        xSemaphoreGive(w->free[i]);
    }
    w->joined=xSemaphoreCreateBinary();
    if(!w->joined)goto fail;
    if(xTaskCreate(transfer_writer_task,"s31_sd_write",4096,w,4,NULL)!=pdPASS)goto fail;
    return w;
fail:
    transfer_writer_destroy(w);
    return NULL;
}

static char *transfer_writer_acquire(transfer_writer_t *w,unsigned slot) {
    xSemaphoreTake(w->free[slot],portMAX_DELAY);
    return w->buffer[slot];
}

static void transfer_writer_submit(transfer_writer_t *w,unsigned slot,size_t bytes) {
    w->bytes[slot]=bytes;
    xSemaphoreGive(w->ready[slot]);
}

static int transfer_writer_finish(transfer_writer_t *w,unsigned owned_slot,int64_t *write_us,unsigned *writes) {
    // 结束符排在已提交的块之后；失败和断网也先归还所有缓冲，再删临时文件。
    // The terminal slot follows submitted blocks; faults/disconnects join all buffers before temporary-file removal.
    transfer_writer_submit(w,owned_slot,0);
    xSemaphoreTake(w->joined,portMAX_DELAY);
    int error=atomic_load(&w->error);
    *write_us=w->write_us;*writes=w->writes;
    transfer_writer_destroy(w);
    return error;
}

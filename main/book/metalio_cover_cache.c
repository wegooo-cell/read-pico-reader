/* SPDX-License-Identifier: Apache-2.0
 * 中文：板级封面像素预算；低内存时回收最久未使用项并保留前台余量。
 * English: Honor the board cover budget and foreground memory reserve with LRU eviction.
 */
#include "metalio_cover_cache.h"
#include "metalio_memory.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static portMUX_TYPE init_lock=portMUX_INITIALIZER_UNLOCKED;
static void lock(void) {
    portENTER_CRITICAL(&init_lock);
    if(!mutex) mutex=xSemaphoreCreateMutexStatic(&mutex_storage);
    portEXIT_CRITICAL(&init_lock);
    xSemaphoreTake(mutex,portMAX_DELAY);
}
static void unlock(void) { xSemaphoreGive(mutex); }
#else
#include <pthread.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static void lock(void) { pthread_mutex_lock(&mutex); }
static void unlock(void) { pthread_mutex_unlock(&mutex); }
#endif
// 后台全书库预热只写卡：在该任务内暂停收录，避免挤掉当前页与相邻页的封面。
// Whole-library warming writes the card only: suspend admission for that task so it cannot
// evict the visible and neighboring pages' covers.
#ifdef ESP_PLATFORM
static TaskHandle_t s_hold_task;
static bool admission_held(void) { return s_hold_task && s_hold_task==xTaskGetCurrentTaskHandle(); }
void metalio_cover_cache_hold_admission(bool hold) { lock(); s_hold_task=hold?xTaskGetCurrentTaskHandle():NULL; unlock(); }
#else
static pthread_t s_hold_thread;
static bool s_hold;
static bool admission_held(void) { return s_hold && pthread_equal(s_hold_thread,pthread_self()); }
void metalio_cover_cache_hold_admission(bool hold) { lock(); s_hold=hold; if(hold)s_hold_thread=pthread_self(); unlock(); }
#endif
#define CACHE_SLOTS KIIKO_BOARD_COVER_RAM_SLOTS
#define CACHE_BUDGET (KIIKO_BOARD_COVER_RAM_BUDGET_KB * 1024u)
typedef struct {
    metalio_cover_key_t key;
    metalio_cover_meta_t meta;
    uint8_t *pixels;
    size_t bytes;
    uint64_t used;
} cache_entry_t;
static cache_entry_t *entries;
static metalio_cover_stats_t stats;
static uint64_t stamp;
static void discard(unsigned i) {
    if(!entries[i].pixels) return;
    free(entries[i].pixels); stats.bytes-=entries[i].bytes; --stats.entries;
    memset(&entries[i],0,sizeof(entries[i])); ++stats.evictions;
}
static bool evict(void) {
    if(!entries || !stats.entries) return false;
    unsigned oldest=0; uint64_t age=UINT64_MAX;
    for(unsigned i=0;i<CACHE_SLOTS;i++) if(entries[i].pixels && entries[i].used<age) {
        oldest=i; age=entries[i].used;
    }
    discard(oldest); return true;
}
bool metalio_cover_cache_get(const metalio_cover_key_t *key,void *out,size_t capacity,metalio_cover_meta_t *meta) {
    if(!key || (!out && !meta)) return false;
    lock();
    if(entries) for(unsigned i=0;i<CACHE_SLOTS;i++) {
        cache_entry_t *e=&entries[i];
        if(e->pixels && !memcmp(&e->key,key,sizeof(*key)) && (!out || capacity>=e->bytes)) {
            if(out) memcpy(out,e->pixels,e->bytes);
            if(meta) *meta=e->meta;
            e->used=++stamp; ++stats.hits; unlock(); return true;
        }
    }
    ++stats.misses; unlock(); return false;
}
static bool put_rows(const metalio_cover_key_t *key,const void *pixels,size_t stride,size_t span,size_t rows,metalio_cover_meta_t meta) {
    if(!rows||!span||span>stride||rows>SIZE_MAX/span||rows>SIZE_MAX/stride)return false;
    size_t bytes=span*rows;
    if(!key || !pixels || !bytes || bytes>CACHE_BUDGET) return false;
    lock();
    if(admission_held()) { unlock(); return false; }
    if(!entries) entries=heap_caps_calloc(CACHE_SLOTS,sizeof(*entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!entries) { unlock(); return false; }
    // 同一文件版本/光栅只存一份；书籍修改时移除旧版本。/ Replace same raster or stale file versions.
    for(unsigned i=0;i<CACHE_SLOTS;i++) if(entries[i].pixels &&
       (!memcmp(&entries[i].key,key,sizeof(*key)) ||
        (entries[i].key.field[0]==key->field[0] &&
         (entries[i].key.field[2]!=key->field[2] || entries[i].key.field[3]!=key->field[3] ||
          (key->field[4]==3 && entries[i].key.field[4]<=2))))) discard(i);
    while(stats.entries && (stats.bytes+bytes>metalio_cache_budget(stats.bytes,CACHE_BUDGET) ||
          stats.entries==CACHE_SLOTS || !metalio_cache_can_grow(bytes))) evict();
    if(!metalio_cache_can_grow(bytes)) { unlock(); return false; }
    uint8_t *copy=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    while(!copy && evict()) copy=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!copy) { unlock(); return false; }
    for(unsigned i=0;i<CACHE_SLOTS;i++) if(!entries[i].pixels) {
        for(size_t y=0;y<rows;++y)memcpy(copy+y*span,(const uint8_t*)pixels+y*stride,span);
        entries[i]=(cache_entry_t){.key=*key,.meta=meta,.pixels=copy,.bytes=bytes,.used=++stamp};
        stats.bytes+=bytes; ++stats.entries; unlock(); return true;
    }
    free(copy); unlock(); return false;
}
bool metalio_cover_cache_put(const metalio_cover_key_t *key,const void *pixels,size_t bytes,metalio_cover_meta_t meta) {
    return put_rows(key,pixels,bytes,bytes,1,meta);
}
bool metalio_cover_cache_put_rows(const metalio_cover_key_t *key,const void *pixels,size_t stride,size_t span,size_t rows) {
    return put_rows(key,pixels,stride,span,rows,(metalio_cover_meta_t){0});
}
bool metalio_cover_cache_get_rows(const metalio_cover_key_t *key,void *out,size_t stride,size_t span,size_t rows) {
    if(!key||!out||!rows||!span||span>stride||rows>SIZE_MAX/span||rows>SIZE_MAX/stride)return false;
    lock();
    if(entries)for(unsigned i=0;i<CACHE_SLOTS;++i) {
        cache_entry_t *e=&entries[i];
        if(e->pixels&&e->bytes==span*rows&&!memcmp(&e->key,key,sizeof(*key))) {
            for(size_t y=0;y<rows;++y)memcpy((uint8_t*)out+y*stride,e->pixels+y*span,span);
            e->used=++stamp;++stats.hits;unlock();return true;
        }
    }
    ++stats.misses;unlock();return false;
}
void metalio_cover_cache_trim(size_t budget) {
    lock(); while(stats.bytes>budget && evict()) {} unlock();
}
size_t metalio_cover_cache_yield(size_t bytes) {
    lock();
    const size_t before=stats.bytes;
    // 只写卡的预热不为远处书目让出当前页的内存封面。/ Card-only warming never evicts visible covers for distant books.
    if(!admission_held()) while(before-stats.bytes<bytes && evict()) {}
    const size_t freed=before-stats.bytes;
    unlock(); return freed;
}
void metalio_cover_cache_clear(void) {
    lock(); while(evict()) {} free(entries); entries=NULL; unlock();
}
metalio_cover_stats_t metalio_cover_cache_stats(void) { lock(); metalio_cover_stats_t s=stats; unlock(); return s; }

/* SPDX-License-Identifier: Apache-2.0
 * 中文：一个有界低优先级封面工作队列；UI只收取结果，旧代次不写入新页面。
 * English: One bounded low-priority cover queue; the UI takes results and stale generations never alter a new page.
 */
#include "book_cover_jobs.h"
#include "book_cover_auto.h"
#include "book_title.h"
#include "book_source.h"
#include "ui_image_dither.h"
#include "read_pico_sd.h"
#if KIIKO_BOARD_METALIO47
#include "metalio_jpeg.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef ESP_PLATFORM
#include "freertos/semphr.h"
#endif

enum { EMPTY,QUEUED,RUNNING,AUTO,READY,FAILED,TAKEN,NO_ARTWORK };
typedef struct { book_cover_job_t request;book_cover_result_t result;int state;bool auto_cache;uint64_t automatic_font_key;uint8_t *automatic; } cover_job_slot_t;
static cover_job_slot_t *s_jobs;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned s_generation;
static bool s_running,s_paused;
// 旧封面目录迁移：请求后在队列空闲时逐个处理，处理完本次开机不再做。/ Old cover folder migration: requested, run one file at a time while the queue is empty, done once per boot.
static bool s_sweep_requested,s_sweep_done,s_sweeping;
static uint8_t contrast_gray(uint8_t value,unsigned contrast) {
    int v=128+((int)value-128)*(int)contrast/100;
    return (uint8_t)(v<0?0:v>255?255:v)&0xf0;
}
static bool white_edge(const uint8_t *gray,unsigned sw,unsigned sh,book_cover_job_t *q) {
    book_crop_t crop=book_cover_crop(sw,sh,q->width,q->height);unsigned sum=0,count=0;
    for(unsigned y=9;y<39;y+=5)for(unsigned x=11;x<35;x+=4){
        unsigned sx=crop.x+(uint64_t)x*crop.width/q->width,sy=crop.y+(uint64_t)y*crop.height/q->height;
        if(sx>=sw)sx=sw-1;
        if(sy>=sh)sy=sh-1;
        sum+=contrast_gray(gray[(size_t)sy*sw+sx],q->contrast);++count;
    }
    return sum<count*145;
}
// 快刷点阵先交给界面，等队列里没有待做的封面再写卡：冷封面写卡耗时最多，攒到当前页都显示后再写，不挡显示也不挡下一本。
// 只有后台任务读写这些待写项，同一时刻至多一个后台任务。
// Hand fast rasters to the UI first and write them to the card once no cover is queued: the write is the
// largest part of a cold cover, so it waits until the page is shown instead of delaying this cover or the next.
// Only the worker touches the pending writes, and at most one worker runs at a time.
typedef struct {
    char path[288],title[256],author[128];
    book_fast_cover_key_t key;bool edge;int64_t size,modified;size_t bytes;uint8_t bits[];
} fast_cache_write_t;
static fast_cache_write_t *s_fast_writes[BOOK_COVER_JOB_SLOTS];
static unsigned s_fast_write_count;
static void flush_fast_cache_write(bool store) {
    if(!s_fast_write_count)return;
    fast_cache_write_t *w=s_fast_writes[0];
    --s_fast_write_count;memmove(s_fast_writes,s_fast_writes+1,s_fast_write_count*sizeof(*s_fast_writes));
    // 写入前仍按原书大小与修改时间核对，书改过就不落盘。/ The write still checks the source size and mtime.
    if(store)book_cover_fast_cache_write_checked(w->path,w->title,w->author,w->key,w->bits,w->edge,w->size,w->modified);
    free(w);
}
static void defer_fast_cache_write(const book_cover_job_t *q,const uint8_t *bits,size_t bytes,book_fast_cover_key_t key,bool edge,int64_t size,int64_t modified) {
    if(s_fast_write_count==BOOK_COVER_JOB_SLOTS)flush_fast_cache_write(true);
    fast_cache_write_t *w=heap_caps_malloc(sizeof(*w)+bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!w)return;
    memcpy(w->path,q->path,sizeof(w->path));memcpy(w->title,q->title,sizeof(w->title));memcpy(w->author,q->author,sizeof(w->author));
    w->key=key;w->edge=edge;w->size=size;w->modified=modified;w->bytes=bytes;memcpy(w->bits,bits,bytes);
    s_fast_writes[s_fast_write_count++]=w;
}
#ifdef ESP_PLATFORM
/* ---- 扫屏期间让路 / Yield to panel scans ---- */
// 用户2026-11-24：200本书实测，后台解码或转换封面时整页扫屏偶发供数欠载（line buffer underrun），随后整屏清白重建
// 约1.5秒并把像素时钟永久降到12MHz；扫屏期间挂起封面后台后，同样的冷封面翻页不再出现。Pico 扫屏开始时挂起、
// 结束时恢复；扫屏线程只等渲染线程与中断，后台挂起时持有的文件锁不会卡住扫屏。S31 扫屏中会写日志，未接此钩子。
// User2026-11-24: in 200-book tests, cover decoding or conversion during a page scan occasionally starved the
// line queue, forcing a ~1.5 s white rebuild and a permanent drop to 12 MHz; suspending the cover worker during
// scans removed it in the same cold turns. Pico suspends at scan start and resumes at its end: the scanning
// thread waits only on render threads and interrupts, so file locks held by the suspended worker cannot stall
// it. S31 logs inside its scans and does not install this hook.
static TaskHandle_t s_worker_task;
static SemaphoreHandle_t s_worker_task_lock;
#if !KIIKO_BOARD_METALIO47
__attribute__((weak)) void epd_image_scan_hook(bool active) { (void)active; }
#endif
void epd_scan_hook(bool active) {
#if !KIIKO_BOARD_METALIO47
    if(active)epd_image_scan_hook(true);
#endif
    if(s_worker_task_lock){
        xSemaphoreTake(s_worker_task_lock,portMAX_DELAY);
        if(s_worker_task){if(active)vTaskSuspend(s_worker_task);else vTaskResume(s_worker_task);}
        xSemaphoreGive(s_worker_task_lock);
    }
#if !KIIKO_BOARD_METALIO47
    if(!active)epd_image_scan_hook(false);
#endif
}
// 后台任务自己登记与注销，扫屏钩子不会拿到已删除任务的句柄。/ The worker registers and unregisters itself, so the hook never sees a deleted task.
static void worker_register(bool live) {
    if(!s_worker_task_lock)return;
    xSemaphoreTake(s_worker_task_lock,portMAX_DELAY);
    if(live)s_worker_task=xTaskGetCurrentTaskHandle();
    else if(s_worker_task==xTaskGetCurrentTaskHandle())s_worker_task=NULL;
    xSemaphoreGive(s_worker_task_lock);
}
#else
static void worker_register(bool live) {(void)live;}
#endif
static bool current(unsigned generation) {
    portENTER_CRITICAL(&s_lock);bool ok=!s_paused&&generation==s_generation;portEXIT_CRITICAL(&s_lock);return ok;
}
static void prepare(cover_job_slot_t *job,unsigned generation) {
    book_cover_job_t *q=&job->request;unsigned sw=BOOK_COVER_W,sh=BOOK_COVER_H;
    job->result.generated=false;
    const bool compact=q->list&&!q->natural&&q->width&&q->height&&q->width<=BOOK_COVER_W&&q->height<=BOOK_COVER_H;
    struct stat st;if(!current(generation)||stat(q->path,&st)||!S_ISREG(st.st_mode))return;
    if (job->automatic &&
        (job->result.size != st.st_size || job->result.modified != st.st_mtime ||
         job->automatic_font_key != book_auto_cover_font_key())) return;
    job->result.size=st.st_size;job->result.modified=st.st_mtime;
    if((q->metadata_only||q->with_chapters)&&!job->result.metadata){
        job->result.chapters=st.st_size>=0&&(uint64_t)st.st_size<=UINT32_MAX?book_cached_chapter_count(q->path,(uint32_t)st.st_size,(int64_t)st.st_mtime):0;
        job->result.metadata=true;if(q->metadata_only)goto ready;
    }
    if(!job->automatic&&q->fast&&(!q->natural||book_cover_list_cache_dimensions(q->path,q->title,q->author,&sw,&sh))){
        if(q->natural){q->width=sw;q->height=sh;q->stride=(sw+7)/8;}
        size_t bytes=(size_t)q->stride*q->height;
        uint8_t *bits=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);bool edge=false;
        book_fast_cover_key_t key={q->width,q->height,q->stride,q->contrast,q->phase};
        if(bits&&book_cover_fast_cache_read(q->path,q->title,q->author,key,bits,&edge)){
            job->result.pixels=bits;job->result.fast=true;job->result.white_edge=edge;goto ready;
        }free(bits);
    }
    if(!current(generation))return;
    bool from_auto=job->automatic!=NULL;
    // TXT 模板可按字体复用；其他生成图保留真实封面的重试机会。/ Cache TXT templates by face; leave other generated art eligible for later artwork retries.
    job->result.generated=from_auto;
    uint8_t *gray=job->automatic;job->automatic=NULL;
    if(gray&&compact){
        uint8_t *small=heap_caps_malloc((size_t)q->width*q->height,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!small){free(gray);return;}
        book_crop_t crop=book_cover_crop(BOOK_COVER_W,BOOK_COVER_H,q->width,q->height);
        for(unsigned y=0;y<q->height;++y)for(unsigned x=0;x<q->width;++x)
            small[(size_t)y*q->width+x]=gray[(crop.y+(uint64_t)y*crop.height/q->height)*BOOK_COVER_W+crop.x+(uint64_t)x*crop.width/q->width];
        free(gray);gray=small;sw=q->width;sh=q->height;
        // 生成封面只用于本帧绘制，不写回精简列表缓存（否则失败会被当成真封面固化）。
        // Generated art is painted for this frame only and never persisted into the sized list
        // cache, which would otherwise freeze a decode failure as a real cover.
    }else if(gray&&job->auto_cache){book_cover_store_generated(q->path,q->title,q->author,gray,q->list,st.st_size,st.st_mtime);}
    if(!gray) {
        const size_t need=compact?(size_t)q->width*q->height:BOOK_COVER_W*BOOK_COVER_H;
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<need+1536u*1024u)return;
        gray=heap_caps_malloc(need,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!gray)return;
        int result;
        if (compact) { sw = q->width; sh = q->height; }
        if(compact)result=book_cover_load_sized_io(q->path,q->title,q->author,gray,sw,sh,&job->auto_cache);
        else result=book_cover_load_io(q->path,q->title,q->author,gray,q->list,&sw,&sh,&job->auto_cache);
        if(result<0){free(gray);return;}
        if(!current(generation)){free(gray);return;}
        if(!result){
            if(q->artwork_only) {
                if (job->auto_cache) {
                    struct stat after;
                    if (!stat(q->path, &after) && st.st_size == after.st_size && st.st_mtime == after.st_mtime)
                        job->state = NO_ARTWORK;
                }
                free(gray);
                return;
            }
#ifdef ESP_PLATFORM
            ESP_LOGW("cover", "auto pending %s", q->path);
#endif
            if(compact){free(gray);gray=heap_caps_malloc(BOOK_COVER_W*BOOK_COVER_H,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!gray)return;}
            job->result.pixels=gray;job->state=AUTO;return;
        }
    }
    if(!q->fast){job->result.pixels=gray;q->width=sw;q->height=sh;goto ready;}
    if(q->natural){q->width=sw;q->height=sh;q->stride=(sw+7)/8;}
    uint8_t *bits=heap_caps_calloc(q->height,q->stride,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!bits){free(gray);return;}
    book_crop_t crop=book_cover_crop(sw,sh,q->width,q->height);
    // Crop and contrast depend only on the source column and gray value.
    // Compute them once per cover instead of dividing in the inner pixel loop.
    unsigned source_x[BOOK_COVER_W];
    uint8_t tones[256];
    const bool sample_columns = q->width <= BOOK_COVER_W;
    if (sample_columns)
        for (unsigned x = 0; x < q->width; ++x)
            source_x[x] = crop.x + (uint64_t)x * crop.width / q->width;
    for (unsigned i = 0; i < 256; ++i) {
        const uint8_t tone = contrast_gray(i, q->contrast);
        tones[i] = q->natural ? tone : (tone >> 4) * 17u;
    }
    for(unsigned y=0;y<q->height;++y){
        if(!(y&15)&&!current(generation)){free(bits);free(gray);return;}
        unsigned sy=crop.y+(uint64_t)y*crop.height/q->height;
        const uint8_t *source_row = gray + (size_t)sy * sw;
        uint8_t *packed_row = bits + (size_t)y * q->stride;
        for(unsigned x=0;x<q->width;++x){
            unsigned sx = sample_columns ? source_x[x] : crop.x + (uint64_t)x * crop.width / q->width;
            const uint8_t tone = tones[source_row[sx]];
            bool white=q->natural?ui_image_dither_bw(tone,x,y):ui_image_dither_cover_bw(tone,q->x+x,q->y+y);
            if(white)packed_row[x/8]|=1u<<(x&7);
        }
    }
    job->result.white_edge=q->natural?false:white_edge(gray,sw,sh,q);free(gray);
    if(!current(generation)){free(bits);return;}
    book_fast_cover_key_t key={q->width,q->height,q->stride,q->contrast,q->phase};
    // 只有真封面才落盘快刷缓存；默认封面留在内存，避免把失败固化。
    // Persist fast rasters only for real covers; generated defaults stay in RAM.
    if(!from_auto)defer_fast_cache_write(q,bits,(size_t)q->stride*q->height,key,job->result.white_edge,st.st_size,st.st_mtime);
    job->result.pixels=bits;job->result.fast=true;
ready:
    job->result.width=q->width;job->result.height=q->height;
    struct stat after;
    if (!current(generation) || stat(q->path, &after) || after.st_size != st.st_size ||
        after.st_mtime != st.st_mtime) {
        free(job->result.pixels);
        job->result.pixels = NULL;
        job->result.metadata = false;
        return;
    }
    const char *ext = strrchr(q->path, '.');
    if (job->result.pixels && job->result.generated && !q->natural && ext && !strcasecmp(ext, ".txt")) {
        const book_fast_cover_key_t raster = {q->width, q->height, q->stride, q->contrast, q->phase};
        book_cover_shelf_ram_store(q->path, q->title, q->author, st.st_size, st.st_mtime,
                                   job->automatic_font_key, q->list, q->fast, raster, job->result.pixels, job->result.white_edge);
    }
}
static void worker(void *unused) {
    (void)unused;
    worker_register(true);
    for(;;){
        cover_job_slot_t job={0};int slot=-1;unsigned generation;
        portENTER_CRITICAL(&s_lock);
        if(!s_paused&&s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i)if(s_jobs[i].state==QUEUED){slot=(int)i;break;}
        if(slot<0&&s_fast_write_count){
            // 队列空了再逐个写卡，每写一个就回头看是否有新封面要先做；暂停时（拔卡、睡眠）直接丢弃，之后可重建。
            // Write one raster at a time once the queue is empty, checking for new covers in between; while
            // paused (card removal, sleep) drop them instead, since they can be rebuilt.
            const bool store=!s_paused;portEXIT_CRITICAL(&s_lock);flush_fast_cache_write(store);continue;
        }
        if(slot<0&&s_sweep_requested&&!s_paused){
            // 每步之间回头看队列，新封面总是先做；暂停（翻页取消、拔卡、睡眠）即停，空闲时由书架再请求。
            // Check the queue between steps so new covers always go first; stop on pause (turn cancel,
            // card removal, sleep) and let the shelf request it again when idle.
            s_sweeping=true;portEXIT_CRITICAL(&s_lock);
            const int more=read_pico_sd_io_ready()?book_cover_legacy_sweep_step():-1;
            portENTER_CRITICAL(&s_lock);
            if(more<=0){s_sweep_requested=false;if(!more)s_sweep_done=true;}
            portEXIT_CRITICAL(&s_lock);vTaskDelay(pdMS_TO_TICKS(20));continue;
        }
        s_sweeping=false;
        if(slot<0){s_running=false;portEXIT_CRITICAL(&s_lock);break;}
        job=s_jobs[slot];s_jobs[slot].automatic=NULL;s_jobs[slot].state=RUNNING;generation=s_generation;
        portEXIT_CRITICAL(&s_lock);
        // 只写卡的预热不收录到内存缓存，当前页的封面不会被远处书目挤掉。
        // Card-only warming skips RAM admission so distant books never evict the visible page.
        if(job.request.disk_only)metalio_cover_cache_hold_admission(true);
        prepare(&job,generation);
        if(job.request.disk_only)metalio_cover_cache_hold_admission(false);
        portENTER_CRITICAL(&s_lock);
        if(!s_paused&&generation==s_generation){
            s_jobs[slot].auto_cache = job.auto_cache;
            s_jobs[slot].result = job.result;
            if (job.result.pixels) s_jobs[slot].state = job.state == AUTO ? AUTO : READY;
            else if (job.state == NO_ARTWORK) s_jobs[slot].state = NO_ARTWORK;
            else s_jobs[slot].state = job.result.metadata ? READY : FAILED;
            job.result.pixels=NULL;
        }
        portEXIT_CRITICAL(&s_lock);
        free(job.result.pixels);free(job.automatic);vTaskDelay(1);
    }
    worker_register(false);
    vTaskDelete(NULL);
}
static void start_worker(void) {
    portENTER_CRITICAL(&s_lock);bool queued=false;
    if(s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i)queued|=s_jobs[i].state==QUEUED;
    bool start=(queued||s_sweep_requested)&&!s_running&&!s_paused;if(start)s_running=true;portEXIT_CRITICAL(&s_lock);
#ifdef ESP_PLATFORM
    // 建锁失败时后台照常工作，只是扫屏不挂起它。/ Without the lock the worker still runs; scans just do not suspend it.
    if(start&&!s_worker_task_lock)s_worker_task_lock=xSemaphoreCreateMutex();
#endif
    if(start&&xTaskCreate(worker,"covers",12288,NULL,1,NULL)!=pdPASS){
        uint8_t *old[BOOK_COVER_JOB_SLOTS]={0};
        portENTER_CRITICAL(&s_lock);s_running=false;
        for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i)if(s_jobs[i].state==QUEUED){
            old[i]=s_jobs[i].automatic;s_jobs[i].automatic=NULL;s_jobs[i].state=FAILED;
        }
        portEXIT_CRITICAL(&s_lock);
        for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i)free(old[i]);
    }
}
unsigned book_cover_jobs_begin(void) {
    if(!s_jobs)s_jobs=heap_caps_calloc(BOOK_COVER_JOB_SLOTS,sizeof(*s_jobs),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *old[BOOK_COVER_JOB_SLOTS*2]={0};
    portENTER_CRITICAL(&s_lock);unsigned generation=++s_generation;s_paused=false;
#if KIIKO_BOARD_METALIO47
    bool cancel=s_running;
#endif
    if(s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i){old[i*2]=s_jobs[i].result.pixels;old[i*2+1]=s_jobs[i].automatic;memset(&s_jobs[i],0,sizeof(s_jobs[i]));}
    portEXIT_CRITICAL(&s_lock);
#if KIIKO_BOARD_METALIO47
    if(cancel)metalio_jpeg_cancel();
#endif
    for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS*2;++i)free(old[i]);
    return generation;
}
static bool same_request(const book_cover_job_t *a,const book_cover_job_t *b) {
    return !strcmp(a->path,b->path)&&!strcmp(a->title,b->title)&&!strcmp(a->author,b->author)&&
        a->metadata_only==b->metadata_only&&a->disk_only==b->disk_only&&a->with_chapters==b->with_chapters&&a->list==b->list&&a->fast==b->fast&&a->natural==b->natural&&a->artwork_only==b->artwork_only&&a->width==b->width&&a->height==b->height&&
        a->stride==b->stride&&a->contrast==b->contrast&&a->phase==b->phase&&a->x==b->x&&a->y==b->y;
}
bool book_cover_jobs_submit(unsigned generation,unsigned slot,const book_cover_job_t *request) {
    // 挂载交接期间不重新打开旧卡封面；恢复后由书架扫描重排队。
    // Never reopen old-card cover files during mount handoff; shelf scans requeue after recovery.
    if(request&&!strncmp(request->path,"/sdcard/",8)&&!read_pico_sd_io_ready())return false;
    if(!s_jobs||!request||slot>=BOOK_COVER_JOB_SLOTS||!request->path[0])return false;
    portENTER_CRITICAL(&s_lock);bool ok=generation==s_generation&&!s_paused;
    if(ok&&s_jobs[slot].state!=EMPTY&&!same_request(&s_jobs[slot].request,request))ok=false;
    if(ok&&s_jobs[slot].state==EMPTY){s_jobs[slot].request=*request;s_jobs[slot].state=QUEUED;}
    portEXIT_CRITICAL(&s_lock);if(ok)start_worker();return ok;
}
static int take_result(unsigned generation, unsigned slot, book_cover_result_t *result, bool render_auto) {
    if(!s_jobs||slot>=BOOK_COVER_JOB_SLOTS||!result)return -1;
    cover_job_slot_t automatic={0};int status=0;
    portENTER_CRITICAL(&s_lock);
    if(generation!=s_generation||s_paused)status=-1;
    else if(s_jobs[slot].state==READY){*result=s_jobs[slot].result;s_jobs[slot].result.pixels=NULL;s_jobs[slot].state=TAKEN;status=1;}
    else if(render_auto && s_jobs[slot].state==AUTO){automatic=s_jobs[slot];s_jobs[slot].result.pixels=NULL;s_jobs[slot].state=RUNNING;}
    else if (s_jobs[slot].state == NO_ARTWORK) {
        *result = s_jobs[slot].result;
        s_jobs[slot].state = TAKEN;
        status = BOOK_COVER_JOB_NO_ARTWORK;
    }
    else if(s_jobs[slot].state==FAILED||s_jobs[slot].state==TAKEN) {
        // A consumed failure can be retried after the caller's backoff.
        if (s_jobs[slot].state == FAILED) s_jobs[slot].state = EMPTY;
        status = -1;
    }
    portEXIT_CRITICAL(&s_lock);
    if(automatic.result.pixels){
        char canonical[256];const char *title=automatic.request.title;
        if(book_title_from_path(automatic.request.path,canonical,sizeof(canonical)))title=canonical;
        const uint64_t font_key = book_auto_cover_font_key();
        bool ok=book_auto_cover_render(title,automatic.request.author,BOOK_COVER_W,BOOK_COVER_H,automatic.result.pixels);
        portENTER_CRITICAL(&s_lock);
        if(generation==s_generation&&!s_paused){
            s_jobs[slot].state=ok?QUEUED:FAILED;
            if(ok){
                s_jobs[slot].automatic = automatic.result.pixels;
                s_jobs[slot].automatic_font_key = font_key;
                automatic.result.pixels = NULL;
            }
        }
        portEXIT_CRITICAL(&s_lock);free(automatic.result.pixels);start_worker();
    }
    return status;
}
int book_cover_jobs_take(unsigned generation, unsigned slot, book_cover_result_t *result) {
    return take_result(generation, slot, result, true);
}

int book_cover_jobs_take_ready(unsigned generation, unsigned slot, book_cover_result_t *result) {
    return take_result(generation, slot, result, false);
}

void book_cover_jobs_cancel(unsigned generation) {
#if KIIKO_BOARD_METALIO47
    bool cancel=false;
#endif
    uint8_t *old[BOOK_COVER_JOB_SLOTS*2]={0};
    portENTER_CRITICAL(&s_lock);
    if(generation==s_generation){
#if KIIKO_BOARD_METALIO47
        cancel=s_running;
#endif
        ++s_generation;s_paused=true;
        if(s_jobs)for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS;++i){
            old[i*2]=s_jobs[i].result.pixels;old[i*2+1]=s_jobs[i].automatic;
            memset(&s_jobs[i],0,sizeof(s_jobs[i]));
        }
    }
    portEXIT_CRITICAL(&s_lock);
#if KIIKO_BOARD_METALIO47
    if(cancel)metalio_jpeg_cancel();
#endif
    for(unsigned i=0;i<BOOK_COVER_JOB_SLOTS*2;++i)free(old[i]);
}
bool book_cover_jobs_quiesce(unsigned timeout_ms) {
    book_cover_jobs_cancel(s_generation);
    int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    for(;;){portENTER_CRITICAL(&s_lock);bool active=s_running;portEXIT_CRITICAL(&s_lock);
        if(!active)return true;
        if(esp_timer_get_time()>=deadline)return false;
        vTaskDelay(pdMS_TO_TICKS(10));}
}

void book_cover_jobs_sweep_legacy(void) {
    portENTER_CRITICAL(&s_lock);const bool request=!s_sweep_done;if(request)s_sweep_requested=true;portEXIT_CRITICAL(&s_lock);
    if(request)start_worker();
}
// 只在迁移旧目录时仍算空闲，书架照常提交封面，工作线程会先做它们。
// Migration alone still counts as idle so the shelf keeps submitting covers, which the worker runs first.
bool book_cover_jobs_idle(void) {
    portENTER_CRITICAL(&s_lock);bool idle=!s_running||s_sweeping;
    if(s_jobs)for(unsigned i=0;idle&&i<BOOK_COVER_JOB_SLOTS;++i)
        if(s_jobs[i].state!=EMPTY&&s_jobs[i].state!=TAKEN&&s_jobs[i].state!=FAILED)idle=false;
    portEXIT_CRITICAL(&s_lock);return idle;
}

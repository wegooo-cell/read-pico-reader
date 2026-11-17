#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the S31 LCD prefill loop with real producer threads and an emulated scanner."""
from pathlib import Path
import re
import subprocess
import tempfile

from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
board = root / 'boards/metalio47/src/components/epdiy/src'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>

#define IRAM_ATTR
#define ESP_LOGI(tag, ...) do { char log[120]; (void)tag; (void)snprintf(log,sizeof(log),__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, ...) ESP_LOGI(tag,__VA_ARGS__)
#define NUM_RENDER_THREADS 2
#define EPD_MIN_PHASE_US 1800
#define MODE_PACKING_1PPB_DIFFERENCE 256
#define CONFIG_IDF_TARGET_ESP32S31 1
#define EPD_DRAW_EMPTY_LINE_QUEUE 1024
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((ms)/5)
#define portMAX_DELAY (-1)
#define portYIELD_FROM_ISR() ((void)0)
#define int_min(a,b) ((a)<(b)?(a):(b))
typedef int BaseType_t;
typedef int TickType_t;
typedef struct { int x,y,width,height; } EpdRect;
enum { WIDTH=16, HEIGHT=64, QUEUE_SIZE=24, FIRST=3, PHASES=7 };
static int s_prefill_lines=32;
static int last_phase_average,last_phase_minimum,last_phase_maximum;
static int64_t esp_timer_get_time(void) {
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
 return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static void vTaskDelay(int ticks){(void)ticks;sched_yield();}
static int xPortGetCoreID(void){return 0;}
typedef struct { pthread_mutex_t mutex; pthread_cond_t cv; int count,capacity; } Sem;
typedef Sem* SemaphoreHandle_t;
static void sem_init(Sem *s,int capacity){
 pthread_mutex_init(&s->mutex,NULL);pthread_cond_init(&s->cv,NULL);s->count=0;s->capacity=capacity;
}
static int xSemaphoreTake(Sem* s,int ms){
 pthread_mutex_lock(&s->mutex);
 if(ms==portMAX_DELAY){
  while(!s->count)pthread_cond_wait(&s->cv,&s->mutex);
 }else if(ms>0){
  struct timespec t;clock_gettime(CLOCK_REALTIME,&t);
  t.tv_sec+=ms/1000;t.tv_nsec+=ms%1000*1000000;
  if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
  while(!s->count&&pthread_cond_timedwait(&s->cv,&s->mutex,&t)==0){}
 }
 const bool got=s->count!=0;
 if(got)--s->count;
 pthread_mutex_unlock(&s->mutex);
 return got;
}
static void xSemaphoreGive(Sem* s){
 pthread_mutex_lock(&s->mutex);
 if(s->count<s->capacity)++s->count;
 pthread_cond_signal(&s->cv);
 pthread_mutex_unlock(&s->mutex);
}
static void xSemaphoreGiveFromISR(Sem* s,BaseType_t* awake){(void)awake;xSemaphoreGive(s);}
static void xTaskNotifyGive(Sem* s){xSemaphoreGive(s);}
typedef struct{int size;atomic_int current,last;uint8_t** bufs;size_t element_size;} LineQueue_t;
typedef struct {
 EpdRect area; int mode,cycle_frames,current_frame,lines_total,display_width,display_height;
 volatile int lines_consumed;
 atomic_int error;
 int underrun_row,underrun_reason;
 bool phase_prefill_requested,phase_prefill_active;
 SemaphoreHandle_t phase_prefill_ready,phase_feed_resume[2],feed_tasks[2],feed_done_smphr[2],frame_done;
 uint8_t *feed_line_buffers[2], *line_ready, *line_threads, *line_mask, *conversion_lut;
 LineQueue_t line_queues[2];
 const bool *drawn_lines; const uint8_t *data_ptr; const int *phase_times;
 int col_band_n;const uint8_t*const*phase_luts;
 const int *col_band_x0,*col_band_x1;
 const int8_t *col_band_phase,*line_phase;
 void (*lut_lookup_func)(const uint32_t*,uint8_t*,const uint8_t*,uint32_t);
} RenderContext_t;
static RenderContext_t ctx;
static bool (*line_source)(void*,uint8_t*);
static void (*frame_done)(void*);
static void *line_arg,*frame_arg;
static atomic_bool stopping,scan_active,future_while_scanning,abort_scan;
static atomic_int scanning_phase;
static int fault_phase,scans;
static bool attempt_active,fault_observed;
static pthread_t scan_thread;
static void epd_lcd_line_source_cb(bool(*fn)(void*,uint8_t*),void *arg){line_source=fn;line_arg=arg;}
static void epd_lcd_frame_done_cb(void(*fn)(void*),void* arg){frame_done=fn;frame_arg=arg;}
static void epd_set_mode(int value){attempt_active=value!=0;}
static void epd_apply_line_mask_VE(uint8_t*a,const uint8_t*b,int n){(void)a;(void)b;(void)n;}
static void get_buffer_params(RenderContext_t*c,int*stride,const uint8_t**data,int*top,int*end,int*ppb){
 *stride=c->display_width;*data=c->data_ptr;*top=0;*end=c->display_height;*ppb=1;
}
static void prepare_context_phase_lut(RenderContext_t*c){c->conversion_lut[0]=(uint8_t)c->current_frame;}
static void lookup(const uint32_t*in,uint8_t*out,const uint8_t*lut,uint32_t width){
 if(atomic_load(&scan_active)&&lut[0]>atomic_load(&scanning_phase))atomic_store(&future_while_scanning,true);
 memset(out,(((const uint8_t*)in)[0]+lut[0]*17)&255,width/4);
}
static void scanner_stop(void){if(scans)assert(!pthread_join(scan_thread,NULL));}
static struct {bool(*read)(void*,uint8_t*);void(*done)(void*);void* line;void* frame;int phase;} job;
static void *scanner(void *unused){
 (void)unused;
 uint8_t out[WIDTH/4];
 for(int row=0;row<HEIGHT;++row){
  if(atomic_load(&abort_scan))return NULL;
  if(job.phase==fault_phase&&row==49){fault_observed=true;break;}
  // Let host scheduling resume a producer before emulating the next DMA read.
  // The injected short frame below still exercises actual recovery.
  LineQueue_t *q=&ctx.line_queues[row%NUM_RENDER_THREADS];
  const int64_t deadline=esp_timer_get_time()+200000;
  while(atomic_load(&q->current)==atomic_load(&q->last)){
   if(atomic_load(&abort_scan))return NULL;
   assert(esp_timer_get_time()<deadline);
   sched_yield();
  }
  job.read(job.line,out);
  for(unsigned i=0;i<sizeof(out);++i)assert(out[i]==(uint8_t)(row+job.phase*17));
  nanosleep(&(struct timespec){.tv_nsec=60000},NULL);
 }
 nanosleep(&(struct timespec){.tv_nsec=3000000},NULL);
 atomic_store(&scan_active,false);
 if(!atomic_load(&abort_scan))job.done(job.frame);
 return NULL;
}
static void epd_lcd_start_frame(void){
 scanner_stop();
 assert(attempt_active&&line_source&&frame_done&&ctx.lines_consumed==0);
 assert(ctx.current_frame==FIRST+scans);
 job.read=line_source;job.done=frame_done;job.line=line_arg;job.frame=frame_arg;job.phase=ctx.current_frame;
 atomic_store(&scanning_phase,job.phase);atomic_store(&scan_active,true);atomic_store(&abort_scan,false);
 assert(!pthread_create(&scan_thread,NULL,scanner,NULL));++scans;
}
static void epd_lcd_abort_frame(void){atomic_store(&abort_scan,true);atomic_store(&scan_active,false);}
'''
with tempfile.TemporaryDirectory() as td:
    tmp=Path(td)
    for file,names in (
        (root/'components/epdiy/src/output_common/line_queue.c', ('lq_current','lq_commit','lq_read','lq_reset')),
        (board/'output_lcd/render_lcd.c', ('retrieve_line_isr','handle_lcd_frame_done','pace_phase',
           'retrieve_prefilled_line','phase_prefill_available','lcd_do_update_prefilled','lcd_calculate_frame')),
    ):
        view=tmp/file.name
        source = re.sub(r'__attribute__\(\(optimize\("O3"\)\)\)', '', file.read_text())
        view.write_text('\n'.join(line.lstrip() for line in source.splitlines()))
        for name in names:
            unit += function(name,view)+'\n'
    unit += r'''
static void* feeder(void *arg){
 int id=(int)(intptr_t)arg;
 for(;;){
  xSemaphoreTake(ctx.feed_tasks[id],portMAX_DELAY);
  if(atomic_load(&stopping))return NULL;
  lcd_calculate_frame(&ctx,id);
  xSemaphoreGive(ctx.feed_done_smphr[id]);
 }
}
int main(void){
 Sem done,ready,notify[2],end[2],resume[2];
 sem_init(&done,1);sem_init(&ready,2);
 ctx.frame_done=&done;ctx.phase_prefill_ready=&ready;
 static _Alignas(16) uint8_t data[WIDTH*HEIGHT];
 uint8_t buffers[2][QUEUE_SIZE][WIDTH/4],*pointers[2][QUEUE_SIZE],feed[2][WIDTH],
         thread[HEIGHT],line_ready[HEIGHT],lut[1],mask[WIDTH/4];
 for(int row=0;row<HEIGHT;++row)memset(data+row*WIDTH,row,WIDTH);
 ctx.area=(EpdRect){0,0,WIDTH,HEIGHT};ctx.display_width=WIDTH;ctx.display_height=HEIGHT;
 ctx.lines_total=HEIGHT;ctx.mode=MODE_PACKING_1PPB_DIFFERENCE;
 ctx.line_ready=line_ready;ctx.line_threads=thread;ctx.line_mask=mask;ctx.data_ptr=data;
 ctx.conversion_lut=lut;ctx.lut_lookup_func=lookup;ctx.phase_prefill_requested=true;
 pthread_t feeders[2];
 for(int i=0;i<2;++i){
  sem_init(&notify[i],1);sem_init(&end[i],1);sem_init(&resume[i],1);
  for(int j=0;j<QUEUE_SIZE;++j)pointers[i][j]=buffers[i][j];
  ctx.line_queues[i]=(LineQueue_t){.size=QUEUE_SIZE,.element_size=WIDTH/4,.bufs=pointers[i]};
  ctx.feed_tasks[i]=&notify[i];ctx.feed_done_smphr[i]=&end[i];ctx.phase_feed_resume[i]=&resume[i];
  ctx.feed_line_buffers[i]=feed[i];
  assert(!pthread_create(&feeders[i],NULL,feeder,(void*)(intptr_t)i));
 }
 for(int loop=0;loop<3;++loop){
  scans=0;fault_phase=loop==1?FIRST+2:-1;fault_observed=false;
  ctx.error=0;ctx.current_frame=FIRST;ctx.cycle_frames=PHASES;
  atomic_store(&future_while_scanning,false);
  assert(phase_prefill_available(&ctx));
  lcd_do_update_prefilled(&ctx);scanner_stop();
  assert(!ctx.phase_prefill_active&&!attempt_active);
  if(loop==1){
   assert(fault_observed&&ctx.error==EPD_DRAW_EMPTY_LINE_QUEUE&&scans==3);
  }else{
   assert(!ctx.error&&scans==PHASES&&ctx.current_frame==FIRST+PHASES);
   assert(atomic_load(&future_while_scanning)&&last_phase_minimum>=EPD_MIN_PHASE_US);
  }
  for(int i=0;i<2;++i)
   assert(atomic_load(&ctx.line_queues[i].current)==atomic_load(&ctx.line_queues[i].last));
 }
 ctx.phase_prefill_ready=NULL;assert(!phase_prefill_available(&ctx));
 atomic_store(&stopping,true);
 for(int i=0;i<2;++i){xTaskNotifyGive(&notify[i]);assert(!pthread_join(feeders[i],NULL));}
 puts("PASS: next-phase rows prepared during scan, exact ordered bytes and skipped prefix, cadence floor, underrun unwind and clean retry");
}
'''
    c=tmp/'test.c'
    binary=tmp/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-pthread',
                    '-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=10)

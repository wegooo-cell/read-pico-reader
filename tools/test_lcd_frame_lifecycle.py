#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 提取真实相位调度函数，模拟 DMA 结束和任务汇合边界。/ Exercise production phase scheduling with mocked DMA and task joins.
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
source_path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'components/epdiy/src/output_lcd/render_lcd.c'
source = source_path.read_text()
start = source.index('static void IRAM_ATTR handle_lcd_frame_done(')
# 弱扫描钩子可能先于调度函数；从调度函数之后查找结尾助手，避免截断被测代码。
# A weak scan hook may precede the scheduler; search for its trailing helper after the scheduler starts.
end = source.index('__attribute__', source.index('void lcd_do_update(', start))
production = source[start:end]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define IRAM_ATTR
#define NUM_RENDER_THREADS 2
#define EPD_DRAW_EMPTY_LINE_QUEUE 0x400
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define portMAX_DELAY -1
#define portYIELD_FROM_ISR() ((void)0)
typedef int BaseType_t;
typedef void (*frame_done_func_t)(void*);
typedef struct { int current, last; } LineQueue_t;
typedef struct {
 int lines_consumed, lines_total, error, current_frame, cycle_frames,underrun_row,underrun_reason;
 int frame_done, feed_done_smphr[2], feed_tasks[2];
 LineQueue_t line_queues[2];
} RenderContext_t;
static RenderContext_t *active;
static frame_done_func_t done_cb;
static int notifications, joins, phases, mode, failure,aborts;
static int s_prefill_lines=128;
static int64_t clock_us;
static int64_t esp_timer_get_time(void){clock_us+=500;return clock_us;}
static void epd_lcd_abort_frame(void){++aborts;}

static void epd_lcd_frame_done_cb(frame_done_func_t cb, void *ctx) {
 done_cb=cb; if (ctx) active=ctx;
}
static void epd_lcd_line_source_cb(void *cb, void *ctx) {(void)cb; (void)ctx;}
static void xSemaphoreGiveFromISR(int sem, int *awoken) {(void)sem; (void)awoken;}
static void epd_set_mode(int m) {mode=m;}
static int xPortGetCoreID(void) {return 0;}
static void prepare_context_for_next_frame(RenderContext_t *ctx) {
 phases++; joins=0; ctx->lines_consumed=0;
}
static void xTaskNotifyGive(int task) {(void)task; notifications++;}
static int xSemaphoreTake(int sem, int timeout) {
 (void)timeout;
 if (sem == 10) {
  if(!timeout)return pdFALSE;
  if(failure==3)return pdFALSE;
  active->lines_consumed = failure == 1 ? 676 : 688;
  if (failure == 2) active->error = EPD_DRAW_EMPTY_LINE_QUEUE;
  active->line_queues[0]=(LineQueue_t){11,12};
  active->line_queues[1]=(LineQueue_t){39,40};
  if (!failure) active->line_queues[0]=active->line_queues[1]=(LineQueue_t){5,5};
  assert(done_cb); done_cb(active);
 } else { joins++;if(failure==4&&sem==11)return pdFALSE; }
 return pdTRUE;
}
static void lq_reset(LineQueue_t *q) {
 assert(joins==2); q->current=q->last=0;
}
static void vTaskDelay(int ticks) {(void)ticks;}
'''
s31 = 'EPD_MIN_PHASE_US' in production
harness += f'\n#define HAS_S31_TIMEOUTS {int(s31)}\n'
tests = r'''
int main(void) {
 for (failure=0; failure<(HAS_S31_TIMEOUTS?5:3); failure++) {
  RenderContext_t ctx={.lines_total=688,.cycle_frames=36,.frame_done=10,
   .feed_done_smphr={11,12},.feed_tasks={0,1}};
  notifications=joins=phases=aborts=0;
  lcd_do_update(&ctx);
  assert(mode==0 && !done_cb);
  assert(aborts==(failure==3));
  if (failure) {
   assert(ctx.error & EPD_DRAW_EMPTY_LINE_QUEUE);
   assert(phases==1 && notifications==2 && joins==2);
   assert(ctx.line_queues[0].current==ctx.line_queues[0].last);
   assert(ctx.line_queues[1].current==ctx.line_queues[1].last);
  } else assert(!ctx.error && phases==36 && ctx.current_frame==36);
 }
 puts("LCD lifecycle PASS: incomplete scan, underrun, join-before-reset, normal phases and S31 DMA/feed timeouts");
}
'''
out = root / 'build/book-tests'
out.mkdir(parents=True, exist_ok=True)
cfile = out / 'lcd_frame_lifecycle.c'
cfile.write_text(harness + production + tests)
binary = out / 'lcd_frame_lifecycle'
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-variable',
                '-fsanitize=address,undefined', '-g', str(cfile), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)

#include <stdint.h>
#include <string.h>

#include "../output_common/render_method.h"

#ifdef RENDER_METHOD_LCD

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/cache.h>

#include "../epd_internals.h"
#include "../output_common/line_queue.h"
#include "../output_common/lut.h"
#include "../output_common/render_context.h"
#include "epd_board.h"
#include "epdiy.h"
#include "lcd_driver.h"
#include "render_lcd.h"

// S31 C 查表需要更长的准备窗口；Pico 上层档位不应覆盖它。
// S31 C lookup needs its own prefill margin, independent of Pico profiles.
static int s_prefill_lines = 192;
#ifdef KIIKO_LOCAL_LATENCY
#include "esp_private/spi_flash_os.h"
#include "esp_rom_sys.h"
static int s_cache_probe_remaining,s_cache_probe_completed;
void epd_lcd_cache_probe_arm(int phases) {
    __atomic_store_n(&s_cache_probe_completed,0,__ATOMIC_RELAXED);
    __atomic_store_n(&s_cache_probe_remaining,phases,__ATOMIC_RELAXED);
}
int epd_lcd_cache_probe_completed(void) { return __atomic_load_n(&s_cache_probe_completed,__ATOMIC_RELAXED); }
static void IRAM_ATTR __attribute__((noinline)) cache_probe(void) {
    const spi_flash_guard_funcs_t *guard=spi_flash_guard_get();
    guard->start();esp_rom_delay_us(200);guard->end();
    __atomic_add_fetch(&s_cache_probe_completed,1,__ATOMIC_RELAXED);
    __atomic_sub_fetch(&s_cache_probe_remaining,1,__ATOMIC_RELAXED);
}
#endif

// declare vector optimized line mask application.
void epd_apply_line_mask_VE(uint8_t* line, const uint8_t* mask, int mask_len);

__attribute__((optimize("O3"))) static bool IRAM_ATTR
retrieve_line_isr(void* payload, uint8_t* buf) {
    RenderContext_t* ctx = payload;
    if (ctx->lines_consumed >= ctx->lines_total) {
        return false;
    }
    int thread = ctx->line_threads[ctx->lines_consumed];
    BaseType_t awoken = pdFALSE;

    if (!__atomic_load_n(&ctx->line_ready[ctx->lines_consumed],__ATOMIC_ACQUIRE) ||
        thread >= NUM_RENDER_THREADS) {
        if (ctx->underrun_row<0) {
            ctx->underrun_row=ctx->lines_consumed;
            ctx->underrun_reason=thread>=NUM_RENDER_THREADS?2:1;
        }
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
        memset(buf, 0x00, ctx->display_width / 4);
        ctx->lines_consumed += 1;
        return awoken;
    }

    LineQueue_t* lq = &ctx->line_queues[thread];

    if (lq_read(lq, buf) != 0) {
        if (ctx->underrun_row<0) { ctx->underrun_row=ctx->lines_consumed; ctx->underrun_reason=3; }
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
        memset(buf, 0x00, ctx->display_width / 4);
    }

    if (ctx->lines_consumed >= ctx->display_height) {
        memset(buf, 0x00, ctx->display_width / 4);
    }
    ctx->lines_consumed += 1;
    return awoken;
}

/// start the next frame in the current update cycle
static void IRAM_ATTR handle_lcd_frame_done(void* payload) {
    RenderContext_t* ctx = payload;
    epd_lcd_frame_done_cb(NULL, NULL);
    epd_lcd_line_source_cb(NULL, NULL);

    // 扫描先于供数结束时置错，禁止残留行进入下一相位
    if (ctx->lines_consumed < ctx->lines_total) {
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
    }

    BaseType_t task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(ctx->frame_done, &task_awoken);

    portYIELD_FROM_ISR();
}

// 纯扫约 11ms；TB8 相太短 DU/GL 易残影。流式路径保留，仅垫相周期下限。
#ifndef EPD_MIN_PHASE_US
#define EPD_MIN_PHASE_US 12000
#endif

static int last_phase_average, last_phase_minimum, last_phase_maximum;
void epd_lcd_get_timing(int* average,int* minimum,int* maximum) {
    *average=__atomic_load_n(&last_phase_average,__ATOMIC_RELAXED);
    *minimum=__atomic_load_n(&last_phase_minimum,__ATOMIC_RELAXED);
    *maximum=__atomic_load_n(&last_phase_maximum,__ATOMIC_RELAXED);
}

static void pace_phase(int64_t phase_start_us) {
    for (;;) {
        if (esp_timer_get_time() - phase_start_us >= EPD_MIN_PHASE_US) {
            break;
        }
        vTaskDelay(0);
    }
}

// Each feeder appends every one of its rows in order. Once both feeders return,
// old-phase rows still in the FIFO can drain while the next rows are appended.
static bool IRAM_ATTR retrieve_prefilled_line(void* payload, uint8_t* buf) {
    RenderContext_t* ctx = payload;
    const int row = ctx->lines_consumed;
    if (row >= ctx->lines_total) return false;
    if (lq_read(&ctx->line_queues[row % NUM_RENDER_THREADS], buf) != 0) {
        if (ctx->underrun_row < 0) {
            ctx->underrun_row = row;
            ctx->underrun_reason = 3;
        }
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
        memset(buf, 0, ctx->display_width / 4);
    }
    ctx->lines_consumed = row + 1;
    return false;
}

static bool phase_prefill_available(const RenderContext_t* ctx) {
    if (!ctx->phase_prefill_requested || ctx->cycle_frames < 2 || ctx->phase_times ||
        ctx->lines_total <= s_prefill_lines + NUM_RENDER_THREADS || !ctx->phase_prefill_ready)
        return false;
    for (int i = 0; i < NUM_RENDER_THREADS; ++i)
        if (!ctx->phase_feed_resume[i] || ctx->line_queues[i].size <= s_prefill_lines / NUM_RENDER_THREADS)
            return false;
    return true;
}

static void lcd_do_update_prefilled(RenderContext_t* ctx) {
    const TickType_t timeout = pdMS_TO_TICKS(2000);
    const int64_t started = esp_timer_get_time();
    int64_t previous = 0, periods_total = 0;
    int period_min = INT32_MAX, period_max = 0, completed = 0;
    const int local = xPortGetCoreID();
    unsigned feeding = 0;
    bool scanning = false;
    ctx->phase_prefill_active = true;
    while (xSemaphoreTake(ctx->phase_prefill_ready, 0) == pdTRUE) {}
    for (int i = 0; i < NUM_RENDER_THREADS; ++i)
        (void)xSemaphoreTake(ctx->phase_feed_resume[i], 0);
    epd_set_mode(1);

    for (int phase = 0; phase < ctx->cycle_frames; ++phase) {
        // The prior producers are done. The scanner reads the committed FIFO
        // rows only and does not use the conversion table or producer indices.
        prepare_context_phase_lut(ctx);
        feeding = (1u << NUM_RENDER_THREADS) - 1u;
        // Waking the local high-priority feeder can preempt the update task.
        xTaskNotifyGive(ctx->feed_tasks[!local]);
        xTaskNotifyGive(ctx->feed_tasks[local]);
        for (int i = 0; i < NUM_RENDER_THREADS; ++i)
            if (ctx->error || xSemaphoreTake(ctx->phase_prefill_ready, timeout) != pdTRUE) goto failed;
        if (scanning) {
            if (xSemaphoreTake(ctx->frame_done, timeout) != pdTRUE) goto failed;
            scanning = false;
            if (ctx->error) goto failed;
            ++completed;
        }
        if (ctx->error) goto failed;
        if (previous) pace_phase(previous);
        ctx->lines_consumed = 0;
        ctx->underrun_row = -1;
        ctx->underrun_reason = 0;
        epd_lcd_frame_done_cb(handle_lcd_frame_done, ctx);
        epd_lcd_line_source_cb(retrieve_prefilled_line, ctx);
        scanning = true;
        epd_lcd_start_frame();
        const int64_t now = esp_timer_get_time();
        if (previous) {
            const int period = (int)(now - previous);
            periods_total += period;
            if (period < period_min) period_min = period;
            if (period > period_max) period_max = period;
        }
        previous = now;
        xSemaphoreGive(ctx->phase_feed_resume[!local]);
        xSemaphoreGive(ctx->phase_feed_resume[local]);
        for (int i = 0; i < NUM_RENDER_THREADS; ++i) {
            if (xSemaphoreTake(ctx->feed_done_smphr[i], timeout) != pdTRUE) goto failed;
            feeding &= ~(1u << i);
        }
        if (ctx->error) goto failed;
        ++ctx->current_frame;
    }
    if (xSemaphoreTake(ctx->frame_done, timeout) != pdTRUE) goto failed;
    scanning = false;
    if (ctx->error) goto failed;
    ++completed;
    pace_phase(previous);
    goto done;

failed:
    ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
    epd_lcd_frame_done_cb(NULL, NULL);
    epd_lcd_line_source_cb(NULL, NULL);
    if (scanning) epd_lcd_abort_frame();
    (void)xSemaphoreTake(ctx->frame_done, 0);
    for (int i = 0; i < NUM_RENDER_THREADS; ++i) {
        if (feeding & (1u << i)) {
            xSemaphoreGive(ctx->phase_feed_resume[i]);
            xSemaphoreTake(ctx->feed_done_smphr[i], portMAX_DELAY);
        }
        lq_reset(&ctx->line_queues[i]);
    }
    ESP_LOGW("epd_lcd", "phase prefill failed completed=%d row=%d reason=%d",
             completed, ctx->underrun_row, ctx->underrun_reason);

done:
    epd_lcd_line_source_cb(NULL, NULL);
    epd_lcd_frame_done_cb(NULL, NULL);
    epd_set_mode(0);
    ctx->phase_prefill_active = false;
    if (completed == ctx->cycle_frames && completed > 1) {
        const int avg = (int)(periods_total / (completed - 1));
        __atomic_store_n(&last_phase_average, avg, __ATOMIC_RELAXED);
        __atomic_store_n(&last_phase_minimum, period_min, __ATOMIC_RELAXED);
        __atomic_store_n(&last_phase_maximum, period_max, __ATOMIC_RELAXED);
        ESP_LOGI("EpdTiming", "phase prefill us: avg=%d min=%d max=%d frames=%d cycle=%dms",
                 avg, period_min, period_max, completed, (int)((esp_timer_get_time() - started) / 1000));
    }
}

void lcd_do_update(RenderContext_t* ctx) {
    if (phase_prefill_available(ctx)) {
        lcd_do_update_prefilled(ctx);
        return;
    }
    const int64_t cycle_start=esp_timer_get_time();
    int64_t previous_phase_start=0, period_total=0;
    int period_min=INT32_MAX, period_max=0, periods=0;
    epd_set_mode(1);
    ESP_LOGD("epd_lcd", "update %d frames, lines=%d (stream pace=%dus)", ctx->cycle_frames,
             ctx->lines_total, EPD_MIN_PHASE_US);

    for (uint8_t k = 0; k < ctx->cycle_frames; k++) {
        const int64_t phase_start=esp_timer_get_time();
        if (previous_phase_start) {
            const int period=(int)(phase_start-previous_phase_start);
            if (period<period_min) period_min=period;
            if (period>period_max) period_max=period;
            period_total+=period; ++periods;
        }
        previous_phase_start=phase_start;
        epd_lcd_frame_done_cb(handle_lcd_frame_done, ctx);
        prepare_context_for_next_frame(ctx);
        ctx->underrun_row=-1; ctx->underrun_reason=0;

        const int64_t t0 = esp_timer_get_time();

        // start both feeder tasks；开扫由 feeder 预填够行后触发
        xTaskNotifyGive(ctx->feed_tasks[!xPortGetCoreID()]);
        xTaskNotifyGive(ctx->feed_tasks[xPortGetCoreID()]);
#ifdef KIIKO_LOCAL_LATENCY
        // 只有整帧行已在内部队列时，主调用者才递归测试缓存停顿；不饿住生成任务。/ The owning caller tests a recursive cache stall only after all rows are in internal queues, without starving producers.
        extern bool epd_lcd_frame_active(void);
        if(__atomic_load_n(&s_cache_probe_remaining,__ATOMIC_RELAXED)>0&&
           __atomic_load_n(&ctx->line_ready[ctx->lines_total-1],__ATOMIC_ACQUIRE)&&
           __atomic_load_n(&ctx->line_ready[ctx->lines_total-2],__ATOMIC_ACQUIRE)&&epd_lcd_frame_active())cache_probe();
#endif

        if (xSemaphoreTake(ctx->frame_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGE("epd_lcd", "LCD frame %d/%d timeout", k + 1, ctx->cycle_frames);
            ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
            epd_lcd_line_source_cb(NULL, NULL);
            epd_lcd_abort_frame();
            xSemaphoreTake(ctx->frame_done, 0);
        }

        for (int i = 0; i < NUM_RENDER_THREADS; i++) {
            if (xSemaphoreTake(ctx->feed_done_smphr[i], pdMS_TO_TICKS(2000)) != pdTRUE) {
                ESP_LOGE("epd_lcd", "feed %d timeout @frame %d", i, k);
                ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
            }
        }

        pace_phase(phase_start);

        const int64_t period_us = esp_timer_get_time() - t0;
        if ((k & 3) == 0 || k + 1 == ctx->cycle_frames) {
            ESP_LOGD("epd_lcd", "phase %d/%d period=%lldus", k + 1, ctx->cycle_frames,
                     (long long)period_us);
        }

        if (ctx->error) {
#ifdef KIIKO_LOCAL_LATENCY
            extern void epd_lcd_dump_dma(void);
            epd_lcd_dump_dma();
#endif
            ESP_LOGE("epd_lcd","phase=%d prefill=%d first_missing_row=%d reason=%d consumed=%d/%d",
                     k,s_prefill_lines,ctx->underrun_row,ctx->underrun_reason,ctx->lines_consumed,ctx->lines_total);
            for (int i = 0; i < NUM_RENDER_THREADS; i++) {
                lq_reset(&ctx->line_queues[i]);
            }
            break;
        }

        ctx->current_frame++;
    }

    epd_lcd_line_source_cb(NULL, NULL);
    epd_lcd_frame_done_cb(NULL, NULL);

    epd_set_mode(0);
    if (periods) {
        __atomic_store_n(&last_phase_average,(int)(period_total/periods),__ATOMIC_RELAXED);
        __atomic_store_n(&last_phase_minimum,period_min,__ATOMIC_RELAXED);
        __atomic_store_n(&last_phase_maximum,period_max,__ATOMIC_RELAXED);
        ESP_LOGI("EpdTiming","phase cadence us: avg=%d min=%d max=%d frames=%d cycle=%dms",
        (int)(period_total/periods),period_min,period_max,ctx->cycle_frames,
        (int)((esp_timer_get_time()-cycle_start)/1000));
    }
}

__attribute__((optimize("O3"))) static bool IRAM_ATTR
push_pixels_isr(void* payload, uint8_t* buf) {
    RenderContext_t* ctx = payload;
    // Output no-op outside of drawn area
    if (ctx->lines_consumed < ctx->area.y) {
        memset(buf, 0, ctx->display_width / 4);
    } else if (ctx->lines_consumed >= ctx->area.y + ctx->area.height) {
        memset(buf, 0, ctx->display_width / 4);
    } else {
        memcpy(buf, ctx->static_line_buffer, ctx->display_width / 4);
    }
    ctx->lines_consumed += 1;
    return pdFALSE;
}

/**
 * Populate the line mask for use in epd_push_pixels.
 */
static void push_pixels_populate_line(RenderContext_t* ctx, int color) {
    // Select fill pattern by draw color
    int fill_byte = 0;
    switch (color) {
        case 0:
            fill_byte = DARK_BYTE;
            break;
        case 1:
            fill_byte = CLEAR_BYTE;
            break;
        default:
            fill_byte = 0x00;
    }

    // Compute a line mask based on the drawn area
    uint8_t* dirtyness = malloc(ctx->display_width / 2);
    assert(dirtyness != NULL);

    memset(dirtyness, 0, ctx->display_width / 2);

    for (int i = 0; i < ctx->display_width; i++) {
        if ((i >= ctx->area.x) && (i < ctx->area.x + ctx->area.width)) {
            dirtyness[i / 2] |= i % 2 ? 0xF0 : 0x0F;
        }
    }
    epd_populate_line_mask(ctx->line_mask, dirtyness, ctx->display_width / 4);

    // mask the line pattern with the populated mask
    memset(ctx->static_line_buffer, fill_byte, ctx->display_width / 4);
    epd_apply_line_mask(ctx->static_line_buffer, ctx->line_mask, ctx->display_width / 4);

    free(dirtyness);
}

void epd_push_pixels_lcd(RenderContext_t* ctx, short time, int color) {
    ctx->current_frame = 0;
    ctx->lines_total = ctx->display_height;
    ctx->lines_consumed = 0;
    ctx->static_line_buffer = malloc(ctx->display_width / 4);
    assert(ctx->static_line_buffer != NULL);

    push_pixels_populate_line(ctx, color);
    epd_lcd_frame_done_cb(handle_lcd_frame_done, ctx);
    epd_lcd_line_source_cb(&push_pixels_isr, ctx);

    epd_set_mode(1);
    epd_lcd_start_frame();
    if (xSemaphoreTake(ctx->frame_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
        epd_lcd_line_source_cb(NULL, NULL);
        epd_lcd_frame_done_cb(NULL, NULL);
        epd_lcd_abort_frame();
        ESP_LOGE("epd_lcd", "clear scan timed out");
    }
    epd_set_mode(0);

    free(ctx->static_line_buffer);
    ctx->static_line_buffer = NULL;
}

#define int_min(a, b) (((a) < (b)) ? (a) : (b))

// 预填够 bounce 再开扫；太少易欠载，太多则短队列在开扫前会堵死
// S31 uses C LUT feeding instead of Pico's Xtensa SIMD. The 64-line profile
// underruns during concurrent audio. Alternate producers to prefill 192 rows
// without exceeding either existing 127-slot queue.

__attribute__((optimize("O3"))) void IRAM_ATTR
lcd_calculate_frame(RenderContext_t* ctx, int thread_id) {
    assert(ctx->lut_lookup_func != NULL);
    uint8_t* input_line = ctx->feed_line_buffers[thread_id];

    LineQueue_t* lq = &ctx->line_queues[thread_id];
    int l = 0;

    // 另一生产者可能已启动并报告欠载；不得再次启动同一帧
    if (ctx->error) {
        return;
    }

    // line must be able to hold 2-pixel-per-byte or 1-pixel-per-byte data
    memset(input_line, 0x00, ctx->display_width);

    EpdRect area = ctx->area;
    int min_y, max_y, bytes_per_line, _ppB;
    const uint8_t* ptr_start;
    get_buffer_params(ctx, &bytes_per_line, &ptr_start, &min_y, &max_y, &_ppB);

    assert(area.width == ctx->display_width && area.x == 0);

    const int trigger_line = int_min(s_prefill_lines, ctx->lines_total-1);
    const bool phase_prefill = ctx->phase_prefill_active;

    // Fixed ownership bounds each producer to half the prefill. With dynamic
    // claims one core can fill its 127-slot queue before a larger prefill starts.
    for (l=thread_id; l<ctx->lines_total; l+=NUM_RENDER_THREADS) {
        if (ctx->error) return;
        if (!phase_prefill) ctx->line_threads[l] = (uint8_t)thread_id;

        // 预填够行后开扫，随后边扫边喂
        if (phase_prefill && l == s_prefill_lines + thread_id) {
            xSemaphoreGive(ctx->phase_prefill_ready);
            xSemaphoreTake(ctx->phase_feed_resume[thread_id], portMAX_DELAY);
            if (ctx->error) return;
        } else if (!phase_prefill && l == trigger_line) {
            // Claimed rows are not necessarily committed: the other core may
            // still own an early row. Start scanning only when the entire
            // prefill prefix is ready, not when a producer claims row 127.
            const int64_t ready_deadline=esp_timer_get_time()+2000000;
            for (int row=0;row<l;++row) {
                while (!__atomic_load_n(&ctx->line_ready[row],__ATOMIC_ACQUIRE)) {
                    if (ctx->error || esp_timer_get_time()>ready_deadline) {
                        ctx->error|=EPD_DRAW_EMPTY_LINE_QUEUE; return;
                    }
                    vTaskDelay(0);
                }
            }
            epd_lcd_line_source_cb(&retrieve_line_isr, ctx);
            epd_lcd_start_frame();
        }

        if (l < min_y || l >= max_y
            || (ctx->drawn_lines != NULL && !ctx->drawn_lines[l - area.y])) {
            uint8_t* buf = NULL;
            int wait_iters = 0;
            while (buf == NULL) {
                if (ctx->error & EPD_DRAW_EMPTY_LINE_QUEUE) {
                    if (!phase_prefill) lq_reset(lq);
                    return;
                }
                buf = lq_current(lq);
                if (buf != NULL) {
                    break;
                }
                vTaskDelay(0);
                if (++wait_iters > 30000) {
                    ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
                    if (!phase_prefill) lq_reset(lq);
                    return;
                }
            }
            memset(buf, 0x00, lq->element_size);
            lq_commit(lq);
            if (!phase_prefill) __atomic_store_n(&ctx->line_ready[l],1,__ATOMIC_RELEASE);
            continue;
        }

        const uint8_t* ptr = ptr_start + bytes_per_line * (l - min_y);
#if !CONFIG_IDF_TARGET_ESP32S31
        Cache_Start_DCache_Preload((uint32_t)ptr, ctx->display_width, 0);
#endif
        uint32_t* lp = (uint32_t*)ptr;

        uint8_t* buf = NULL;
        int wait_iters = 0;
        while (buf == NULL) {
            if (ctx->error & EPD_DRAW_EMPTY_LINE_QUEUE) {
                if (!phase_prefill) lq_reset(lq);
                return;
            }
            buf = lq_current(lq);
            if (buf != NULL) {
                break;
            }
            vTaskDelay(0);
            if (++wait_iters > 30000) {
                ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
                if (!phase_prefill) lq_reset(lq);
                return;
            }
        }

        if (ctx->col_band_n > 0 && ctx->phase_luts && ctx->col_band_x0 &&
            ctx->col_band_x1 && ctx->col_band_phase) {
            // 未启动的条带保持零电压；活跃条带使用自己的 GL16 相位。
            // Inactive bands hold at zero voltage; active bands use their GL16 phase.
            memset(buf, 0x00, ctx->display_width / 4);
            for (int band = 0; band < ctx->col_band_n; ++band) {
                int8_t phase = ctx->col_band_phase[band];
                if (phase < 0) continue;
                int x0 = ctx->col_band_x0[band];
                int x1 = ctx->col_band_x1[band];
                if (x0 < 0 || x1 > ctx->display_width || x0 >= x1 ||
                    (x0 & 15) || (x1 & 15)) continue;
                ctx->lut_lookup_func((const uint32_t*)(ptr + x0), buf + x0 / 4,
                                     ctx->phase_luts[phase], (uint32_t)(x1 - x0));
            }
        } else {
            const uint8_t* lut = ctx->conversion_lut;
            if (ctx->line_phase && ctx->phase_luts && l < ctx->display_height) {
                int8_t phase = ctx->line_phase[l];
                if (phase >= 0) lut = ctx->phase_luts[phase];
            }
            ctx->lut_lookup_func(lp, buf, lut, ctx->display_width);
        }

        epd_apply_line_mask_VE(buf, ctx->line_mask, ctx->display_width / 4);
        lq_commit(lq);
        if (!phase_prefill) __atomic_store_n(&ctx->line_ready[l],1,__ATOMIC_RELEASE);
    }
}

#endif

// 空闲根页可请求128行，两生产者各64行；其他调用保持192行以保护音频/传输余量。
// Idle roots may request128 rows,64 per producer; other callers retain192 to protect audio/transfer margins.
void epd_lcd_set_prefill_lines(int lines) { s_prefill_lines = lines == 128 || lines == 160 ? lines : 192; }
int epd_lcd_prefill_lines(void) { return s_prefill_lines; }

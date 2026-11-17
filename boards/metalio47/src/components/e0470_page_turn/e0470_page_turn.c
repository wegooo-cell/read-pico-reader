/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 基于 MindReset Read Pico 官方 E0470 波形与刷新路径实现。
 * Built on the MindReset Read Pico E0470 waveform and refresh path.
 * 中文：S31阅读与主页用8带一拍一带：默认完整48相共55拍（裁剪表37相为44拍），
 *       保留原始48相表及扫描/节拍下限。
 * English: S31 uses eight bands, one per tick: 55 ticks with the default full 48
 *          phases (44 for the trimmed 37), retaining scan timing and minimum pacing.
 * 普通文字单带整轮提交；阅读专用水波纹用8带，尾部两带同时启动，22相共28拍。
 * Ordinary text batches a single band; reader-only ripple uses eight bands with the final pair launched together, totaling28 ticks for22 phases.
 * 保留每个像素的完整有效驱动、准备相与尾相；主页及侧滑仍按原节拍逐带启动。
 * Retain every pixel's complete active, setup and tail phases; main pages and edge-back retain their original stagger schedule.
 */

#include "e0470_page_turn.h"

#include <stdbool.h>
#include <string.h>

#include "e0470_epaper_waveform.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static const char* TAG = "e0470_turn";

#define TURN_DEFAULT_BANDS E0470_TURN_FAST_BANDS
#define TURN_BANDS_MAX 32
#define TURN_PHASE_CAP 64
#define TURN_LINE_MAX 2048

static uint8_t (*s_lut)[1024];
static const uint8_t* s_lut_ptr[TURN_PHASE_CAP];
static int s_band0[TURN_BANDS_MAX];
static int s_band1[TURN_BANDS_MAX];
static int8_t s_band_phase[TURN_BANDS_MAX];
static int8_t s_line_phase[TURN_LINE_MAX] DRAM_ATTR;
static const EpdWaveformPhases* s_lut_src;
static int s_lut_n;
static int s_tick_us = E0470_TURN_DEFAULT_TICK_US;
static uint8_t s_first_phase[256];
static const EpdWaveformPhases* s_first_source;

void e0470_page_turn_release(void) {
    heap_caps_free(s_lut);
    s_lut = NULL;
    s_lut_src = NULL;
    s_lut_n = 0;
    s_first_source = NULL;
}

const char* e0470_turn_dir_name(e0470_turn_dir_t dir) {
    switch (dir) {
        case E0470_TURN_LTR: return "ltr";
        case E0470_TURN_RTL: return "rtl";
        case E0470_TURN_TTB: return "ttb";
        case E0470_TURN_BTT: return "btt";
        default: return "?";
    }
}

void e0470_page_turn_set_tick_us(int us) {
    if (us < 0) us = 0;
    s_tick_us = us;
}

int e0470_page_turn_tick_us(void) {
    return s_tick_us;
}

static void clip_rect(EpdRect* r, int w, int h) {
    if (r->x < 0) {
        r->width += r->x;
        r->x = 0;
    }
    if (r->y < 0) {
        r->height += r->y;
        r->y = 0;
    }
    if (r->x + r->width > w) r->width = w - r->x;
    if (r->y + r->height > h) r->height = h - r->y;
}

static EpdRect rotate_to_fb(EpdRect rect) {
    const int pw = epd_width();
    const int ph = epd_height();
    const int lx0 = rect.x;
    const int ly0 = rect.y;
    const int lx1 = rect.x + rect.width - 1;
    const int ly1 = rect.y + rect.height - 1;
    int ax, ay, bx, by;

    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE:
            ax = lx0;
            ay = ly0;
            bx = lx1;
            by = ly1;
            break;
        case EPD_ROT_PORTRAIT:
            ax = pw - ly0 - 1;
            ay = lx0;
            bx = pw - ly1 - 1;
            by = lx1;
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            ax = pw - lx0 - 1;
            ay = ph - ly0 - 1;
            bx = pw - lx1 - 1;
            by = ph - ly1 - 1;
            break;
        default:
            ax = ly0;
            ay = ph - lx0 - 1;
            bx = ly1;
            by = ph - lx1 - 1;
            break;
    }

    const int x0 = ax < bx ? ax : bx;
    const int x1 = ax < bx ? bx : ax;
    const int y0 = ay < by ? ay : by;
    const int y1 = ay < by ? by : ay;
    return (EpdRect){ .x = x0, .y = y0, .width = x1 - x0 + 1, .height = y1 - y0 + 1 };
}

static void phys_to_logical(int px, int py, int* lx, int* ly) {
    const int pw = epd_width();
    const int ph = epd_height();
    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE:
            *lx = px;
            *ly = py;
            break;
        case EPD_ROT_PORTRAIT:
            *lx = py;
            *ly = pw - px - 1;
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            *lx = pw - px - 1;
            *ly = ph - py - 1;
            break;
        default:
            *lx = ph - py - 1;
            *ly = px;
            break;
    }
}

static bool dir_is_lr(e0470_turn_dir_t dir) {
    return dir == E0470_TURN_LTR || dir == E0470_TURN_RTL;
}

static bool dir_is_reverse(e0470_turn_dir_t dir) {
    return dir == E0470_TURN_RTL || dir == E0470_TURN_BTT;
}

// 错相轴若落在物理 y 上，整行共用一个相位；落在物理 x 上则一行内分段。
// If the stagger axis is physical y, a whole line shares one phase; physical x needs column bands.
static bool uses_line_phase(e0470_turn_dir_t dir) {
    const bool lr = dir_is_lr(dir);
    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE:
        case EPD_ROT_INVERTED_LANDSCAPE:
            return !lr;
        default:
            return lr;
    }
}

static int band_of(int coord, int origin, int span, int bands, bool reverse) {
    int band = (coord - origin) * bands / span;
    if (band < 0) band = 0;
    if (band >= bands) band = bands - 1;
    return reverse ? bands - 1 - band : band;
}

static void clear_bands(int* b0, int* b1, int n) {
    for (int i = 0; i < n; i++) {
        b0[i] = 0;
        b1[i] = 0;
    }
}

static void assign_bands(
    int* b0,
    int* b1,
    int bands,
    int from,
    int to,
    int step,
    bool along_x,
    int origin,
    int span,
    bool lr,
    bool reverse
) {
    clear_bands(b0, b1, bands);
    int cur = -1;
    int run = from;
    for (int p = from; p < to; p += step) {
        int lx, ly;
        if (along_x) phys_to_logical(p, 0, &lx, &ly);
        else phys_to_logical(0, p, &lx, &ly);
        const int coord = lr ? lx : ly;
        const int band = band_of(coord, origin, span, bands, reverse);
        if (band != cur) {
            if (cur >= 0) {
                b0[cur] = run;
                b1[cur] = p;
            }
            cur = band;
            run = p;
        }
    }
    if (cur >= 0) {
        b0[cur] = run;
        b1[cur] = to;
    }
}

static bool build_luts(const EpdWaveformPhases* gl) {
    // 37 KiB 相位表按需放入 PSRAM；避免长期占用 WiFi 和 USB 需要的内部内存。
    // Allocate the 48 KiB phase table in PSRAM on demand, preserving internal RAM for WiFi and USB.
    if (!s_lut) {
        s_lut = heap_caps_aligned_alloc(16, sizeof(uint8_t[TURN_PHASE_CAP][1024]),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_lut) return false;
    }
    if (s_lut_src == gl && s_lut_n == gl->phases) return true;
    const int n = gl->phases < TURN_PHASE_CAP ? gl->phases : TURN_PHASE_CAP;
    for (int phase = 0; phase < n; phase++) {
        epd_build_1ppB_lut_1k(s_lut[phase], gl, phase);
        s_lut_ptr[phase] = s_lut[phase];
    }
    s_lut_src = gl;
    s_lut_n = gl->phases;
    return true;
}

static void copy_front_to_back(EpdiyHighlevelState* hl, EpdRect phys) {
    const int w = epd_width();
    const int h = epd_height();
    clip_rect(&phys, w, h);
    if (phys.width <= 0 || phys.height <= 0) return;

    for (int y = phys.y; y < phys.y + phys.height; y++) {
        uint8_t* src = hl->front_fb + y * w / 2;
        uint8_t* dst = hl->back_fb + y * w / 2;
        int x = phys.x;
        int x_last = phys.x + phys.width - 1;
        if (x & 1) {
            dst[x / 2] = (uint8_t)((src[x / 2] & 0xF0) | (dst[x / 2] & 0x0F));
            x++;
        }
        if ((x_last & 1) == 0) {
            dst[x_last / 2] = (uint8_t)((src[x_last / 2] & 0x0F) | (dst[x_last / 2] & 0xF0));
            x_last--;
        }
        if (x_last >= x) memcpy(dst + x / 2, src + x / 2, (size_t)((x_last - x + 1) / 2));
    }
}

// 只读实际目标与真实旧帧，不以脏矩形或目标灰阶猜测可跳相数。
// Read actual target/history pairs rather than inferring skip counts from dirty bounds or target gray alone.
static int text_leading_holds(const EpdWaveformPhases* gl, const EpdiyHighlevelState* hl, EpdRect phys) {
    if (s_first_source != gl) {
        for (int pair = 0; pair < 256; ++pair) {
            int from = pair & 15, to = pair >> 4, first = gl->phases;
            for (int phase = 0; phase < gl->phases; ++phase) {
                const uint8_t v = gl->luts[(phase * 16 + to) * 4 + from / 4];
                if ((v >> (6 - 2 * (from % 4))) & 3) { first = phase; break; }
            }
            s_first_phase[pair] = (uint8_t)first;
        }
        s_first_source = gl;
    }
    int first = gl->phases;
    const int stride = epd_width() / 2;
    // 行掩码以字节为界，邻接半字节也计入，保证结果保守。
    // Include neighboring nibbles covered by byte masks so the result stays conservative.
    const int x0 = phys.x / 2, x1 = (phys.x + phys.width + 1) / 2;
    for (int y = phys.y; y < phys.y + phys.height; ++y) {
        const uint8_t* to = hl->front_fb + (size_t)y * stride;
        const uint8_t* from = hl->back_fb + (size_t)y * stride;
        int previous_to = -1, previous_from = -1;
        for (int x = x0; x < x1; ++x) {
            const unsigned t = to[x], f = from[x];
            if ((int)t == previous_to && (int)f == previous_from) continue;
            previous_to = (int)t; previous_from = (int)f;
            int lo = s_first_phase[((t & 15) << 4) | (f & 15)];
            int hi = s_first_phase[(t & 0xf0) | (f >> 4)];
            if (lo < first) first = lo;
            if (hi < first) first = hi;
            if (first == 0) return 0;
        }
    }
    if (first >= gl->phases) return gl->phases - 1;
    return first > 0 ? first - 1 : 0;
}

static enum EpdDrawError page_turn_run(EpdiyHighlevelState* hl, EpdRect area,
    e0470_turn_dir_t dir, const EpdWaveform* waveform, unsigned band_count,
    bool compact, bool text_skip, bool paired_tail) {
    if (hl == NULL) return EPD_DRAW_NO_PHASES_AVAILABLE;
    if (band_count < (compact ? 1u : 2u) || band_count > TURN_BANDS_MAX) return EPD_DRAW_INVALID_CROP;
    if (dir > E0470_TURN_BTT) dir = E0470_TURN_RTL;

    const EpdWaveformPhases* gl = e0470_waveform_phases(waveform, MODE_GL16);
    if (gl == NULL || gl->luts == NULL || gl->phases <= 0 || gl->phases > TURN_PHASE_CAP || gl->phase_times) {
        return EPD_DRAW_NO_PHASES_AVAILABLE;
    }

    const int bands = (int)band_count;
    const int nphase = gl->phases;
    // 紧凑模式供阅读和快速主页共用；仅提前后续条带的启动，不跳过每个像素的任何相位。
    // Reading and fast main pages share compact launches; start later bands sooner without skipping any pixel phase.
    const int launch_step = compact ? 1 : (nphase + bands - 2) / (bands - 1);
    // 阅读末两带共用启动时刻，避免最后一窄带独自拖尾；每带仍输出全部相位。
    // Reader's final pair shares a launch, avoiding an isolated narrow tail while retaining every phase per band.
    const int last_launch = bands - 1 - (paired_tail && bands > 1 ? 1 : 0);
    const int ticks = last_launch * launch_step + nphase;
    const int fb_w = epd_width();
    const int fb_h = epd_height();
    if (fb_h > TURN_LINE_MAX) return EPD_DRAW_INVALID_CROP;

    EpdRect phys = rotate_to_fb(area);
    clip_rect(&phys, fb_w, fb_h);
    if (phys.width <= 0 || phys.height <= 0) return EPD_DRAW_SUCCESS;

    const int y0 = phys.y;
    const int y1 = phys.y + phys.height;
    const bool lr = dir_is_lr(dir);
    const bool reverse = dir_is_reverse(dir);
    const bool line_ph = uses_line_phase(dir);
    const int origin = lr ? area.x : area.y;
    const int span = lr ? (area.width > 0 ? area.width : 1) : (area.height > 0 ? area.height : 1);

    if (line_ph) {
        assign_bands(
            s_band0, s_band1, bands, y0, y1, 1, false, origin, span, lr, reverse
        );
    } else {
        const int x_lo = (phys.x + 15) & ~15;
        const int x_hi = (phys.x + phys.width) & ~15;
        assign_bands(
            s_band0, s_band1, bands, x_lo, x_hi, 16, true, origin, span, lr, reverse
        );
    }
    int assigned = 0;
    for (int band = 0; band < bands; ++band) assigned += s_band0[band] < s_band1[band];
    if (assigned != bands) return EPD_DRAW_INVALID_CROP;

    if (!build_luts(gl)) return EPD_DRAW_NO_PHASES_AVAILABLE;
    epd_poweron();

    const int64_t t_all = esp_timer_get_time();
    const int64_t t_gen = esp_timer_get_time();
    epd_difference_image_cropped(
        hl->front_fb, hl->back_fb, phys, hl->difference_fb, hl->dirty_lines, hl->dirty_columns
    );
    const int skip = text_skip ? text_leading_holds(gl, hl, phys) : 0;
    const int64_t gen_us = esp_timer_get_time() - t_gen;

    int64_t draw_us = 0;
    enum EpdDrawError err = EPD_DRAW_SUCCESS;
    for (int tick = skip; tick < ticks; tick++) {
        const int64_t t_tick = esp_timer_get_time();
        memset(hl->dirty_lines, 0, sizeof(bool) * (size_t)fb_h);
        memset(hl->dirty_columns, 0, (size_t)fb_w / 2);
        memset(s_band_phase, -1, (size_t)bands);
        if (line_ph) memset(s_line_phase, -1, (size_t)fb_h);

        bool any = false;
        for (int band = 0; band < bands; band++) {
            const int launch = band > last_launch ? last_launch : band;
            const int phase = tick - launch * launch_step;
            if (phase < 0 || phase >= nphase) continue;
            const int a = s_band0[band];
            const int b = s_band1[band];
            if (a >= b) continue;
            s_band_phase[band] = (int8_t)phase;
            if (line_ph) {
                memset(hl->dirty_lines + a, 1, (size_t)(b - a));
                memset(s_line_phase + a, (int)(int8_t)phase, (size_t)(b - a));
            } else {
                memset(hl->dirty_columns + a / 2, 0xFF, (size_t)((b - a) / 2));
            }
            any = true;
        }
        if (!any) continue;

        if (line_ph) {
            int xs = phys.x & ~1;
            int xe = (phys.x + phys.width + 1) & ~1;
            if (xe > xs) {
                memset(hl->dirty_columns + xs / 2, 0xFF, (size_t)((xe - xs) / 2));
            }
            epd_set_line_phase_luts(s_lut_ptr, s_line_phase);
        } else {
            memset(hl->dirty_lines + y0, 1, (size_t)(y1 - y0));
            epd_set_col_phase_luts(s_lut_ptr, s_band0, s_band1, s_band_phase, bands);
        }

        const int64_t t_draw = esp_timer_get_time();
        err = epd_draw_base(
            epd_full_screen(),
            hl->difference_fb,
            epd_full_screen(),
            MODE_PACKING_1PPB_DIFFERENCE | MODE_DU,
            25,
            hl->dirty_lines,
            hl->dirty_columns,
            &E0470_APPLY_WAVEFORM
        );
        epd_clear_phase_luts();
        draw_us += esp_timer_get_time() - t_draw;
        if (err != EPD_DRAW_SUCCESS) break;
        const int64_t used = esp_timer_get_time() - t_tick;
        if (s_tick_us > 0 && used < s_tick_us) {
            esp_rom_delay_us((uint32_t)(s_tick_us - used));
        }
    }

    // 扫描失败时屏幕可能停在中间相位，保留旧帧基准给调用方恢复。
    // A failed scan may leave intermediate pixels; keep the old baseline for caller recovery.
    if (err == EPD_DRAW_SUCCESS) copy_front_to_back(hl, phys);
    ESP_LOGI(
        TAG,
        "dir=%s ticks=%d gen=%d ms draw=%d ms scan_avg=%d us wall=%d ms err=%d",
        e0470_turn_dir_name(dir),
        ticks - skip,
        (int)(gen_us / 1000),
        (int)(draw_us / 1000),
        ticks > skip ? (int)(draw_us / (ticks - skip)) : 0,
        (int)((esp_timer_get_time() - t_all) / 1000),
        (int)err
    );
    if (text_skip) ESP_LOGI(TAG, "text phases=%d hold_skip=%d", nphase, skip);
    return err;
}

enum EpdDrawError e0470_page_turn(EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir) {
    return page_turn_run(hl, area, dir, &E0470_WAVEFORM, TURN_DEFAULT_BANDS, true, false, false);
}

enum EpdDrawError e0470_page_turn_with_waveform(EpdiyHighlevelState* hl, EpdRect area,
    e0470_turn_dir_t dir, const EpdWaveform* waveform, unsigned band_count) {
    return page_turn_run(hl, area, dir, waveform, band_count, false, false, false);
}

enum EpdDrawError e0470_page_turn_with_waveform_compact(EpdiyHighlevelState* hl, EpdRect area,
    e0470_turn_dir_t dir, const EpdWaveform* waveform, unsigned band_count) {
    return page_turn_run(hl, area, dir, waveform, band_count, true, false, false);
}

enum EpdDrawError e0470_reader_ripple_with_waveform(EpdiyHighlevelState* hl, EpdRect area,
    e0470_turn_dir_t dir, const EpdWaveform* waveform, unsigned band_count) {
    return page_turn_run(hl, area, dir, waveform, band_count, true, false, true);
}

static enum EpdDrawError text_turn(EpdiyHighlevelState* hl, EpdRect area,
    const EpdWaveform* waveform, bool prefill) {
    if(!hl)return EPD_DRAW_NO_PHASES_AVAILABLE;
    const EpdWaveformPhases* phases=e0470_waveform_phases(waveform,MODE_GL16);
    if(!phases||!phases->luts||phases->phases<=0||phases->phases>TURN_PHASE_CAP||phases->phase_times)
        return EPD_DRAW_NO_PHASES_AVAILABLE;
    EpdRect phys=rotate_to_fb(area);clip_rect(&phys,epd_width(),epd_height());
    if(phys.width<=0||phys.height<=0)return EPD_DRAW_SUCCESS;
    const int64_t start=esp_timer_get_time();
    epd_difference_image_cropped(hl->front_fb,hl->back_fb,phys,hl->difference_fb,
        hl->dirty_lines,hl->dirty_columns);
    const int first=text_leading_holds(phases,hl,phys);
    // 与原单带相同：区域内含等白补推，边界相邻半字节由真实差分保持。
    // Match the old single band: include white maintenance; real differences guard adjacent boundary nibbles.
    memset(hl->dirty_lines,0,(size_t)epd_height()*sizeof(bool));
    memset(hl->dirty_lines+phys.y,1,(size_t)phys.height*sizeof(bool));
    memset(hl->dirty_columns,0,(size_t)epd_width()/2);
    int xs=phys.x&~1,xe=(phys.x+phys.width+1)&~1;
    memset(hl->dirty_columns+xs/2,0xff,(size_t)(xe-xs)/2);
    // 等灰重建波形不能靠边界等值保持保护相邻像素，半字节掩码必须精确到裁剪边缘。
    // Equal-gray rebuilding cannot rely on held boundary pairs; mask neighboring nibbles exactly outside the crop.
    if(phys.x&1)hl->dirty_columns[xs/2]&=0xf0;
    if((phys.x+phys.width)&1)hl->dirty_columns[(xe-1)/2]&=0x0f;
    const int64_t generated=esp_timer_get_time();
    epd_clear_phase_luts();epd_poweron();
    // 一次调用复用LCD多相循环与内部1KiB LUT，避免逐相配置及64KiB外部相位表。
    // Reuse the LCD multi-phase loop and internal1KiB LUT in one call, avoiding per-phase setup and a64KiB external phase cache.
    enum EpdDrawMode mode = MODE_PACKING_1PPB_DIFFERENCE | MODE_GL16;
    if (prefill) mode |= MODE_PHASE_PREFILL;
    enum EpdDrawError result=epd_draw_base_from_phase(epd_full_screen(),hl->difference_fb,
        epd_full_screen(),mode,25,
        hl->dirty_lines,hl->dirty_columns,waveform,first);
    if(result==EPD_DRAW_SUCCESS)copy_front_to_back(hl,phys);
    ESP_LOGI(TAG,"text batch phases=%d hold_skip=%d gen=%dms draw=%dms err=%d",
        phases->phases-first,first,(int)((generated-start)/1000),
        (int)((esp_timer_get_time()-generated)/1000),(int)result);
    return result;
}

enum EpdDrawError e0470_text_turn_with_waveform(EpdiyHighlevelState* hl, EpdRect area,
    e0470_turn_dir_t dir, const EpdWaveform* waveform) {
    (void)dir;
    return text_turn(hl, area, waveform, false);
}

enum EpdDrawError e0470_text_turn_with_prefill(EpdiyHighlevelState* hl, EpdRect area,
    const EpdWaveform* waveform) {
    return text_turn(hl, area, waveform, true);
}

enum EpdDrawError e0470_update_regions(EpdiyHighlevelState* hl, EpdRect area,
    EpdRect strong_area, const EpdWaveform* soft_waveform,
    const EpdWaveform* strong_waveform, enum EpdDrawMode strong_mode) {
    const int w = epd_width(), h = epd_height();
    if (!hl || !hl->front_fb || !hl->back_fb || !hl->difference_fb ||
        !hl->dirty_lines || !hl->dirty_columns) return EPD_DRAW_NO_PHASES_AVAILABLE;
    if (epd_get_rotation() != EPD_ROT_INVERTED_PORTRAIT || area.x || area.width != h ||
        area.y < 0 || area.height <= 0 || area.y > w || area.height > w - area.y ||
        (area.y & 1) || (area.height & 1) || strong_area.x || strong_area.width != h ||
        strong_area.y < area.y || strong_area.height <= 0 || strong_area.y > area.y + area.height ||
        strong_area.height > area.y + area.height - strong_area.y ||
        (strong_area.y & 31) || (strong_area.height & 31)) return EPD_DRAW_INVALID_CROP;
    if (strong_mode != MODE_GL16 && strong_mode != MODE_GC16) return EPD_DRAW_NO_PHASES_AVAILABLE;
    const EpdWaveformPhases* soft = e0470_waveform_phases(soft_waveform, MODE_GL16);
    if(!soft)soft=e0470_waveform_phases(soft_waveform,MODE_DU);
    const EpdWaveformPhases* strong = e0470_waveform_phases(strong_waveform, strong_mode);
    if (!soft || !strong || !soft->luts || !strong->luts ||
        soft->phases <= 0 || strong->phases <= 0 || soft->phases > TURN_PHASE_CAP ||
        strong->phases > TURN_PHASE_CAP || soft->phase_times || strong->phase_times)
        return EPD_DRAW_NO_PHASES_AVAILABLE;
    // 只保留当前相位的两张1K表，下一相开始前上一帧的生产者/扫描均已结束。
    // Keep only two current 1 KiB LUTs; producers/scanning finish before the next phase overwrites them.
    uint8_t (*luts)[1024] = heap_caps_aligned_alloc(16, 2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!luts) return EPD_DRAW_NO_PHASES_AVAILABLE;
    const uint8_t* lut_ptrs[] = {luts[0], luts[1]};
    const int starts[] = {area.y&~31, strong_area.y, strong_area.y + strong_area.height};
    const int ends[] = {strong_area.y, strong_area.y + strong_area.height, (area.y+area.height+31)&~31};
    const EpdRect phys = rotate_to_fb(area);
    epd_difference_image_cropped(hl->front_fb, hl->back_fb, phys, hl->difference_fb,
                                  hl->dirty_lines, hl->dirty_columns);
    // 校准区包含等灰像素；外侧等灰是否保持由原柔和表决定，不伪造前一帧。
    // Include equal grays for art calibration; the original soft table holds equal chrome without falsifying the prior frame.
    memset(hl->dirty_lines, 1, (size_t)h * sizeof(bool));
    memset(hl->dirty_columns, 0, (size_t)w / 2);
    memset(hl->dirty_columns + area.y / 2, 0xff, (size_t)area.height / 2);
    const int phases = soft->phases > strong->phases ? soft->phases : strong->phases;
    epd_poweron();
    enum EpdDrawError error = EPD_DRAW_SUCCESS;
    for (int phase = 0; phase < phases; ++phase) {
        if (phase < soft->phases) epd_build_1ppB_lut_1k(luts[0], soft, phase);
        else memset(luts[0], 0, 1024);
        if (phase < strong->phases) epd_build_1ppB_lut_1k(luts[1], strong, phase);
        else memset(luts[1], 0, 1024);
        const int8_t selected[] = {0, 1, 0};
        epd_set_col_phase_luts(lut_ptrs, starts, ends, selected, 3);
        error = epd_draw_base(epd_full_screen(), hl->difference_fb, epd_full_screen(),
            MODE_PACKING_1PPB_DIFFERENCE | MODE_DU, 25,
            hl->dirty_lines, hl->dirty_columns, &E0470_APPLY_WAVEFORM);
        epd_clear_phase_luts();
        if (error != EPD_DRAW_SUCCESS) break;
    }
    heap_caps_free(luts);
    if (error == EPD_DRAW_SUCCESS) copy_front_to_back(hl, phys);
    return error;
}

enum EpdDrawError e0470_update_native_area(EpdiyHighlevelState* hl,EpdRect area,const EpdWaveform* waveform) {
    const int w=epd_width(),h=epd_height();
    if(!hl||!hl->front_fb||!hl->back_fb||!hl->difference_fb||!hl->dirty_lines||!hl->dirty_columns)
        return EPD_DRAW_NO_PHASES_AVAILABLE;
    if(epd_get_rotation()!=EPD_ROT_INVERTED_PORTRAIT||area.x||area.width!=h||area.y<0||
       area.height<=0||area.y>w||area.height>w-area.y)return EPD_DRAW_INVALID_CROP;
    const EpdRect phys=rotate_to_fb(area);
    epd_difference_image_cropped(hl->front_fb,hl->back_fb,phys,hl->difference_fb,
        hl->dirty_lines,hl->dirty_columns);
    // GC会驱动等值像素，必须用精确半字节掩码保护裁剪边缘，而不能靠等灰保持。
    // GC drives equal pixels, so exact nibble masks protect the crop rather than equal-gray holds.
    memset(hl->dirty_lines,1,(size_t)h*sizeof(bool));
    memset(hl->dirty_columns,0,(size_t)w/2);
    const int xs=phys.x&~1,xe=(phys.x+phys.width+1)&~1;
    memset(hl->dirty_columns+xs/2,0xff,(size_t)(xe-xs)/2);
    if(phys.x&1)hl->dirty_columns[xs/2]&=0xf0;
    if((phys.x+phys.width)&1)hl->dirty_columns[(xe-1)/2]&=0x0f;
    epd_clear_phase_luts();epd_poweron();
    enum EpdDrawError result=epd_draw_base(epd_full_screen(),hl->difference_fb,epd_full_screen(),
        MODE_PACKING_1PPB_DIFFERENCE|MODE_GC16,25,hl->dirty_lines,hl->dirty_columns,waveform);
    if(result==EPD_DRAW_SUCCESS)copy_front_to_back(hl,phys);
    return result;
}

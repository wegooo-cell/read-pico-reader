/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：S31 阅读专用 GL16：37 相文字/面板、水波纹及收起归一。
 * English: Reader-only S31 GL16 profiles: 37-phase text/sheets, ripple and dismissal normalization.
 * 用户2026-11-19修订：水波纹等黑/灰保持，白色目标在尾部三拍补白，最后一拍保持；非白目标及其他表保持。
 * User2026-11-19 revision: ripple holds equal black/grays, reinforces white targets in three tail phases and holds the final phase; retain nonwhite targets and other profiles.
 * 用户2026-11-18：普通文字去掉剥除擦白后遗留的前导空等，只前移整段有效序列；相数、脉冲顺序/数量及变白列不改。
 * User2026-11-18: ordinary text advances active sequences past erase-stripped leading holds; retain phase count, pulse order/counts and the white-target column.
 * 冻结：只派生原厂 GL48，不修改共享 LUT、主页、系统列表或永久标定。
 * Frozen: derive from vendor GL48 without changing shared LUTs, main pages, system lists or permanent calibration.
 */
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include "e0470_waveform_trim.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define READER_GRAY_RAM EXT_RAM_BSS_ATTR
#else
#define READER_GRAY_RAM
#endif

enum { GRAY_PHASES = 37, GRAY_CAPACITY = 48, GRAY_HOLD = 3, BLACK_PUSHES = 18 };
extern const EpdWaveformPhases my_gl_25_0;
extern const EpdWaveformPhases my_du_25_0;
typedef struct {
    uint8_t data[GRAY_CAPACITY][16][4];
    EpdWaveformPhases phases;
    const EpdWaveformPhases *ranges[1];
    EpdWaveformMode mode;
    const EpdWaveformMode *modes[1];
    EpdWaveform waveform;
} reader_gray_table_t;
static READER_GRAY_RAM reader_gray_table_t s_gray[DISPLAY_READER_GRAY_COUNT];

static int gray_get(const uint8_t (*data)[16][4], int phase, int to, int from) {
    return (data[phase][to][from / 4] >> (6 - 2 * (from % 4))) & 3;
}
static void gray_set(uint8_t (*data)[16][4], int phase, int to, int from, int action) {
    const int shift = 6 - 2 * (from % 4);
    uint8_t *cell = &data[phase][to][from / 4];
    *cell = (uint8_t)((*cell & ~(3u << shift)) | ((unsigned)action << shift));
}
static void gray_boost_black(uint8_t (*data)[16][4], int phases) {
    const int limit = phases - 1 - GRAY_HOLD;
    for (int from = 1; from < 16; ++from) {
        int first = -1, black = 0, erase = 0;
        bool ordered = true;
        for (int f = 0; f < phases; ++f) {
            const int action = gray_get(data, f, 0, from);
            if (!action) continue;
            if (first < 0) first = f;
            if (action == 1) ++black;
            else if (action == 2 && !black) ++erase;
            else ordered = false;
        }
        if (!ordered || !black || black >= BLACK_PUSHES) continue;
        const int span = erase + BLACK_PUSHES;
        int start = first;
        if (start + span - 1 > limit) start = limit - span + 1;
        if (start < 0) continue;
        for (int f = 0; f < phases; ++f) gray_set(data, f, 0, from, 0);
        for (int i = 0; i < erase; ++i) gray_set(data, start + i, 0, from, 2);
        for (int i = 0; i < BLACK_PUSHES; ++i) gray_set(data, start + erase + i, 0, from, 1);
    }
}
static int gray_base(uint8_t (*data)[16][4]) {
    const e0470_trim_t trim = {11, 5, 0, GRAY_HOLD};
    if (my_gl_25_0.phases != GRAY_CAPACITY || !my_gl_25_0.luts || my_gl_25_0.phase_times) return 0;
    const int phases = e0470_waveform_trim(&my_gl_25_0, &trim, data);
    if (phases != GRAY_PHASES) return 0;
    // 白到白只沿既有白相补一拍；差分路径不额外驱动整片白底。
    // Place one white-to-white tick on an existing white phase; differential routes never add a whole-background pass.
    int tick = -1;
    for (int f = phases - 1; f >= 0 && tick < 0; --f)
        for (int from = 0; from < 15; ++from)
            if (gray_get(data, f, 15, from) == 2) { tick = f; break; }
    gray_set(data, tick >= 0 ? tick : phases - 3, 15, 15, 2);
    return phases;
}
static void gray_white_column(uint8_t (*data)[16][4]) {
    // 普通文字/面板使用18拍原厂DU白推加六拍补白，未变白保持。
    // Ordinary text/sheets use the native 18 white DU pushes plus six extra pushes, holding unchanged white.
    for (int from = 0; from < 16; ++from)
        for (int f = 0; f < GRAY_PHASES; ++f) {
            const int action = from == 15 ? 0 : f < 20
                ? e0470_phase_action(&my_du_25_0, f, 15, from) : f < 26 ? 2 : 0;
            gray_set(data, f, 15, from, action);
        }
}
static void gray_text(uint8_t (*data)[16][4]) {
    // 每对序列左对齐、同灰保持；非白目标去掉前导擦白。
    // Left-align each pair, hold equal grays, and omit leading erase for nonwhite targets.
    for (int to = 0; to < 16; ++to) for (int from = 0; from < 16; ++from) {
        uint8_t seq[GRAY_PHASES];
        for (int f = 0; f < GRAY_PHASES; ++f) seq[f] = (uint8_t)gray_get(data, f, to, from);
        int first = 0, last = GRAY_PHASES;
        while (first < last && !seq[first]) ++first;
        while (last > first && !seq[last - 1]) --last;
        for (int f = 0; f < GRAY_PHASES; ++f)
            gray_set(data, f, to, from, from != to && f < last - first ? seq[first + f] : 0);
        if (to < 15) for (int f = 0; f < GRAY_PHASES && gray_get(data, f, to, from) == 2; ++f)
            gray_set(data, f, to, from, 0);
    }
    for (int to = 1; to < 15; ++to) {
        int first_white = GRAY_PHASES, last_black = -1, prefix_black = 0, tail_white = 0;
        for (int f = 0; f < GRAY_PHASES; ++f) {
            const int action = gray_get(data, f, to, 15);
            if (action == 2 && first_white == GRAY_PHASES) first_white = f;
            if (action == 1) last_black = f;
        }
        for (int f = 0; f < first_white; ++f) prefix_black += gray_get(data, f, to, 15) == 1;
        for (int f = last_black + 1; f < GRAY_PHASES; ++f) tail_white += gray_get(data, f, to, 15) == 2;
        const int moves = tail_white >= 4 && prefix_black >= 6 ? 2 : tail_white >= 2 && prefix_black >= 4 ? 1 : 0;
        int rank = 0, moved = 0, white = last_black + 1;
        int target = prefix_black - (moves == 2 ? 5 : 3);
        for (int f = 0; f < first_white && moved < moves; ++f) {
            if (gray_get(data, f, to, 15) != 1 || rank++ != target) continue;
            while (white < GRAY_PHASES && gray_get(data, white, to, 15) != 2) ++white;
            if (white == GRAY_PHASES) break;
            gray_set(data, f, to, 15, 2); gray_set(data, white++, to, 15, 1);
            ++moved; target += 2;
        }
    }
}

static void gray_text_start(uint8_t (*data)[16][4]) {
    for (int to = 0; to < 15; ++to) for (int from = 0; from < 16; ++from) {
        int first = 0;
        while (first < GRAY_PHASES && !gray_get(data, first, to, from)) ++first;
        if (!first || first == GRAY_PHASES) continue;
        // 低位相向前读未改动的后续相；灰阶尾中的保持与反向脉冲完整保留。
        // Read untouched later phases while moving forward; retain internal holds and reverse settling pulses.
        for (int f = 0; f < GRAY_PHASES; ++f)
            gray_set(data, f, to, from, f + first < GRAY_PHASES ? gray_get(data, f + first, to, from) : 0);
    }
}

static int gray_derivative(uint8_t (*data)[16][4], bool image) {
    const EpdWaveform *water = display_reader_gray_waveform(DISPLAY_READER_GRAY_WATER);
    const EpdWaveformPhases *base = e0470_waveform_phases(water, MODE_GL16);
    if (!base || !base->luts || base->phases != GRAY_PHASES || base->phase_times) return 0;
    const e0470_trim_t trim = {image ? 19 : 7, 4, 3, GRAY_HOLD};
    const int frames = e0470_waveform_trim(base, &trim, data);
    if (!frames) return 0;
    gray_boost_black(data, frames);
    if (!image) for (int to = 0; to <= 7; ++to) for (int from = 0; from <= 7; ++from) {
        if (from == to) continue;
        int first = 0, last = frames;
        while (first < frames && !gray_get(data, first, to, from)) ++first;
        if (first >= frames || gray_get(data, first, to, from) != 2) continue;
        while (last > first && !gray_get(data, last - 1, to, from)) --last;
        int end = first;
        while (end < last && gray_get(data, end, to, from) == 2) ++end;
        int len = last - end, start = frames - GRAY_HOLD - len;
        if (start < first) { start = first; len = frames - GRAY_HOLD - start; end = last - len; }
        if (len <= 0) continue;
        uint8_t kept[GRAY_PHASES];
        for (int i = 0; i < len; ++i) kept[i] = (uint8_t)gray_get(data, end + i, to, from);
        for (int f = 0; f < frames; ++f) gray_set(data, f, to, from, 0);
        for (int i = 0; i < len; ++i) gray_set(data, start + i, to, from, kept[i]);
    }
    for (int g = 0; g < 15; ++g) for (int f = 0; f < frames; ++f) gray_set(data, f, g, g, 0);
    const int limit = frames - GRAY_HOLD;
    for (int from = 0; from < 15; ++from) {
        int first = -1, last = -1;
        for (int f = 0; f < GRAY_PHASES; ++f) if (e0470_phase_action(base, f, 15, from)) {
            if (first < 0) first = f;
            last = f;
        }
        if (first < 0) continue;
        for (int f = 0; f < frames; ++f) gray_set(data, f, 15, from, 0);
        const int span = last - first + 1;
        if (span <= limit) {
            for (int i = 0; i < span; ++i) gray_set(data, limit - span + i, 15, from, e0470_phase_action(base, first + i, 15, from));
        } else {
            uint8_t seq[GRAY_PHASES]; int n = 0;
            for (int f = first; f <= last; ++f) {
                const int action = e0470_phase_action(base, f, 15, from);
                if (action) seq[n++] = (uint8_t)action;
            }
            const int skip = n > limit ? n - limit : 0, start = limit - (n - skip);
            for (int i = skip; i < n; ++i) gray_set(data, start + i - skip, 15, from, seq[i]);
        }
    }
    return frames;
}

static bool gray_water_tail(uint8_t (*data)[16][4]) {
    const EpdWaveform *wave = display_reader_gray_waveform(DISPLAY_READER_GRAY_WATER);
    const EpdWaveformPhases *base = e0470_waveform_phases(wave, MODE_GL16);
    if (!base || base->phases != GRAY_PHASES || !base->luts || base->phase_times) return false;
    memcpy(data, base->luts, GRAY_PHASES * 64);
    // 不擦白两页重合的字芯/灰边，避免短暂出现旧字的白色轮廓。
    // Hold overlapping cores/gray edges instead of briefly exposing an erased old-text outline.
    for (int gray = 0; gray < 16; ++gray)
        for (int phase = 0; phase < GRAY_PHASES; ++phase) gray_set(data, phase, gray, gray, 0);
    // 白底维护移到尾部：既有末白相加两拍补白，白字旧影和空白区域一起整理，末相仍保持。
    // Move maintenance to the tail: extend the existing final white phase by two pushes for old-ink and blank white targets,then hold the last phase.
    enum { TAIL_WHITE_PHASES = 3, TAIL_HOLD_PHASES = 1 };
    const int end = GRAY_PHASES - TAIL_HOLD_PHASES;
    for (int from = 0; from < 16; ++from)
        for (int phase = end - TAIL_WHITE_PHASES; phase < end; ++phase)
            gray_set(data, phase, 15, from, 2);
    return true;
}

const EpdWaveform *display_reader_gray_waveform(display_reader_gray_profile_t profile) {
    if ((unsigned)profile >= DISPLAY_READER_GRAY_COUNT) return NULL;
    reader_gray_table_t *table = &s_gray[profile];
    if (table->phases.luts) return &table->waveform;
    int frames = GRAY_PHASES;
    if (profile == DISPLAY_READER_GRAY_WATER_TAIL) {
        if (!gray_water_tail(table->data)) return NULL;
    } else if (profile == DISPLAY_READER_GRAY_BACKGROUND || profile == DISPLAY_READER_GRAY_IMAGE) {
        frames = gray_derivative(table->data, profile == DISPLAY_READER_GRAY_IMAGE);
    } else if (profile == DISPLAY_READER_GRAY_NORMALIZE || profile == DISPLAY_READER_GRAY_NORMALIZE_GRAY) {
        const EpdWaveform *ink = display_reader_gray_waveform(DISPLAY_READER_GRAY_PAGE);
        const EpdWaveformPhases *base = e0470_waveform_phases(ink, MODE_GL16);
        if (!base || base->phases != GRAY_PHASES) return NULL;
        memcpy(table->data, base->luts, GRAY_PHASES * 64);
        if (profile == DISPLAY_READER_GRAY_NORMALIZE_GRAY)
            for (int g = 1; g < 16; ++g) for (int f = 0; f < frames; ++f) gray_set(table->data, f, g, g, 0);
        // 同一轮统一字芯18白+18黑，保留37相长度与非黑灰阶尾段。
        // Normalize black cores with 18 white and 18 black phases in one 37-phase pass, preserving nonblack tails.
        for (int from = 0; from < 16; ++from) for (int f = 0; f < frames; ++f)
            gray_set(table->data, f, 0, from, f < 18 ? 2 : f < 36 ? 1 : 0);
    } else {
        frames = gray_base(table->data);
        if (frames && profile == DISPLAY_READER_GRAY_TEXT) gray_text(table->data);
        if (frames && profile != DISPLAY_READER_GRAY_WATER) gray_white_column(table->data);
        if (frames) gray_boost_black(table->data, frames);
        if (frames && profile == DISPLAY_READER_GRAY_TEXT) gray_text_start(table->data);
    }
    if (!frames) return NULL;
    table->phases = (EpdWaveformPhases){frames, (const uint8_t *)table->data, NULL};
    table->ranges[0] = &table->phases;
    table->mode = (EpdWaveformMode){MODE_GL16, 1, table->ranges};
    table->modes[0] = &table->mode;
    table->waveform = (EpdWaveform){.num_modes = 1, .num_temp_ranges = 1,
        .mode_data = table->modes, .temp_intervals = E0470_FULL_WAVEFORM.temp_intervals};
    return &table->waveform;
}

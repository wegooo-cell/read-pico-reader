/* SPDX-License-Identifier: Apache-2.0
 * 中文：文字水波纹的柔和灰边，保留抗锯齿目标、驱动计数和周期清残影。
 * English: Soften gray-edge transients on text ripple turns, preserving antialiased targets,
 * pulse counts and periodic cleanup.
 * 冻结：仅白底到中灰提前最多两相回浅；黑/白端点、其他迁移、原表及帧时序不改。
 * 相同驱动计数不保证光学灰度相同；此调序需实屏对比深浅与残影。
 * Frozen: Advance at most two settling phases only for white-to-midgray pairs. Keep endpoints,
 * all other pairs, original tables and frame timing. Equal pulse counts do not guarantee equal
 * optical grays; validate this ordering on the panel against tone and ghosting.
 * User revision2026-10-29: the reader-ripple wrapper inserts four changed-white
 * phases before the original three settle holds; the underlying37-phase function stays unchanged.
 */
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include <stdbool.h>
#include <string.h>

static uint8_t s_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_data, NULL};
static const EpdWaveformPhases* s_ranges[] = {&s_phases};
static const EpdWaveformMode s_mode = {5, 1, s_ranges};
static const EpdWaveformMode* s_modes[] = {&s_mode};
static EpdWaveform s_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_modes};
static bool s_ready;

static void set_action(int phase, int to, int action) {
    // from=15 占该字节最低两位；其余三对不变。/ from=15 occupies only the lowest two bits.
    s_data[phase][to][3] = (uint8_t)((s_data[phase][to][3] & ~3u) | (unsigned)action);
}

const EpdWaveform* display_reader_text_waveform(void) {
    if (s_ready) return &s_waveform;
    const EpdWaveformPhases* original = e0470_waveform_phases(&E0470_TEXTTURN_WAVEFORM, MODE_GL16);
    if (!original || !original->luts || original->phases != E0470_GL16_FRAMES || original->phase_times)
        return &E0470_TEXTTURN_WAVEFORM;
    memcpy(s_data, original->luts, sizeof(s_data));
    for (int to = 1; to < 15; ++to) {
        int first_white = original->phases, last_black = -1;
        for (int f = 0; f < original->phases; ++f) {
            int action = e0470_phase_action(original, f, to, 15);
            if (action == 2 && first_white == original->phases) first_white = f;
            if (action == 1) last_black = f;
        }
        int prefix_black = 0, tail_white = 0;
        for (int f = 0; f < first_white; ++f)
            prefix_black += e0470_phase_action(original, f, to, 15) == 1;
        for (int f = last_black + 1; f < original->phases; ++f)
            tail_white += e0470_phase_action(original, f, to, 15) == 2;
        int moves = tail_white >= 4 && prefix_black >= 6 ? 2 :
                    tail_white >= 2 && prefix_black >= 4 ? 1 : 0;
        if (!moves) continue;
        // 交换动作而非删相；保留起点、末尾回浅及所有保持相，不增加第二次推屏。
        // Swap pulses instead of dropping phases; retain the start, final settling and holds.
        int rank = 0, moved = 0, white = last_black + 1;
        int target_rank = prefix_black - (moves == 2 ? 5 : 3);
        for (int f = 0; f < first_white && moved < moves; ++f) {
            if (e0470_phase_action(original, f, to, 15) != 1) continue;
            if (rank++ != target_rank) continue;
            while (white < original->phases && e0470_phase_action(original, white, to, 15) != 2) ++white;
            if (white == original->phases) break;
            set_action(f, to, 2); set_action(white++, to, 1);
            ++moved; target_rank += 2;
        }
    }
    s_waveform.temp_intervals = E0470_TEXTTURN_WAVEFORM.temp_intervals;
    s_ready = true;
    return &s_waveform;
}

#define READER_SOFT_TURN_SETTLE_EXTRA 12
#define READER_SOFT_TURN_BLACK_EXTRA 4
#define READER_SOFT_TURN_HOLD_PHASES 3
static uint8_t s_soft_data[64][16][4];
static EpdWaveformPhases s_soft_phases = {
    48, (const uint8_t*)s_soft_data, NULL
};
static const EpdWaveformPhases* s_soft_ranges[] = {&s_soft_phases};
static const EpdWaveformMode s_soft_mode = {MODE_GL16, 1, s_soft_ranges};
static const EpdWaveformMode* s_soft_modes[] = {&s_soft_mode};
static EpdWaveform s_soft_waveform = {
    .num_modes = 1, .num_temp_ranges = 1, .mode_data = s_soft_modes
};
static bool s_soft_ready;

static void soft_set_action(int phase, int to, int from, int action) {
    const int shift = 6 - 2 * (from % 4);
    s_soft_data[phase][to][from / 4] = (uint8_t)(
        (s_soft_data[phase][to][from / 4] & ~(3u << shift)) | ((unsigned)action << shift)
    );
}

// 官方阅读翻页时序：整块全像素走厂商 GL16（右对齐、擦除+饱和+灰阶尾+定稿保持）。
// 改动：等值“黑→黑”去掉擦白相只保留压黑相；并在厂商 3 相定稿保持之前插入 12 相
// “全屏擦白”（三轮，所有目标为白的像素，含本来也白的空白区），翻页后白底擦得更狠；
// 擦白之后再加 4 相“重推黑”（所有目标为黑的像素），抵消整屏白推对黑字的冲淡；
// 其余所有迁移（灰阶、变黑、变白）完全按厂商原序列。
// Official reader turn timing: full-pixel vendor GL16. Equal black-on-black drops the erase
// phases and keeps the darken phases; twelve extra white phases (three full erase rounds) are
// inserted before the vendor's three settle holds so every white target (including already-white
// blanks) is erased harder after a page turn, then four extra black re-drive phases darken every
// black target to compensate for the whole-panel white push. All other transitions stay
// vendor-original.
const EpdWaveform* display_reader_soft_turn_waveform(void) {
    if (s_soft_ready) return &s_soft_waveform;
    const EpdWaveformPhases* gl = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
    if (!gl || !gl->luts || gl->phase_times ||
        gl->phases + READER_SOFT_TURN_SETTLE_EXTRA + READER_SOFT_TURN_BLACK_EXTRA > 64)
        return &E0470_WAVEFORM;
    int hold = 0;
    while (hold < gl->phases) {
        const int f = gl->phases - 1 - hold;
        int nonzero = 0;
        for (int to = 0; to < 16 && !nonzero; ++to)
            for (int from = 0; from < 16; ++from)
                if (e0470_phase_action(gl, f, to, from)) { nonzero = 1; break; }
        if (nonzero) break;
        ++hold;
    }
    if (hold < 1) return &E0470_WAVEFORM;
    const int drive = gl->phases - hold;
    const int extra = READER_SOFT_TURN_SETTLE_EXTRA;
    const int black = READER_SOFT_TURN_BLACK_EXTRA;
    memcpy(s_soft_data, gl->luts, (size_t)drive * 16 * 4);
    memset(s_soft_data + drive, 0, (size_t)(extra + black) * 16 * 4);
    memcpy(s_soft_data + drive + extra + black,
           (const uint8_t*)gl->luts + (size_t)drive * 16 * 4, (size_t)hold * 16 * 4);
    for (int f = 0; f < gl->phases; ++f)
        if (e0470_phase_action(gl, f, 0, 0) == 2) soft_set_action(f, 0, 0, 0);
    for (int i = 0; i < extra; ++i)
        for (int from = 0; from < 16; ++from) soft_set_action(drive + i, 15, from, 2);
    for (int i = 0; i < black; ++i)
        for (int from = 0; from < 16; ++from) soft_set_action(drive + extra + i, 0, from, 1);
    s_soft_phases = (EpdWaveformPhases){gl->phases + extra + black, (const uint8_t*)s_soft_data, NULL};
    s_soft_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_soft_ready = true;
    return &s_soft_waveform;
}

// 复用Pico原厂完整DU，含末尾保持相；不改表、不调脉宽或冒用其他板型的扫描。
// Reuse Pico's complete vendor DU including trailing holds; retain tables, pulse widths and board scan timing.
const EpdWaveform* display_reader_fast_text_waveform(void) {
    const EpdWaveformPhases* original = e0470_waveform_phases(&E0470_FULL_WAVEFORM, MODE_DU);
    if (!original || !original->luts || original->phase_times ||
        original->phases != E0470_FULL_DU_FRAMES) return NULL;
    return &E0470_FULL_WAVEFORM;
}

bool display_reader_fast_text_prepare(EpdiyHighlevelState* hl, EpdRect area) {
    if (!hl || !hl->front_fb || !hl->back_fb || hl->front_fb == hl->back_fb ||
        area.width <= 0 || area.height <= 0 || !display_reader_fast_text_waveform()) return false;
    const int pw = epd_width(), ph = epd_height();
    if (pw <= 0 || ph <= 0 || (pw & 1)) return false;
    enum EpdRotation rotation = epd_get_rotation();
    bool portrait = rotation == EPD_ROT_PORTRAIT || rotation == EPD_ROT_INVERTED_PORTRAIT;
    int lw = portrait ? ph : pw, lh = portrait ? pw : ph;
    int64_t right = (int64_t)area.x + area.width, bottom = (int64_t)area.y + area.height;
    int x = area.x < 0 ? 0 : area.x, y = area.y < 0 ? 0 : area.y;
    if (right > lw) right = lw;
    if (bottom > lh) bottom = lh;
    if (right <= x || bottom <= y) return false;
    int w = (int)right - x, h = (int)bottom - y;
    EpdRect phys;
    switch (rotation) {
        case EPD_ROT_LANDSCAPE: phys = (EpdRect){x, y, w, h}; break;
        case EPD_ROT_PORTRAIT: phys = (EpdRect){pw-y-h, x, h, w}; break;
        case EPD_ROT_INVERTED_LANDSCAPE: phys = (EpdRect){pw-x-w, ph-y-h, w, h}; break;
        case EPD_ROT_INVERTED_PORTRAIT: phys = (EpdRect){y, ph-x-w, h, w}; break;
        default: return false;
    }
    // 只对本轮正文/页脚目标做50%阈值；真实旧灰阶及区域外的目标完全不改。
    // Threshold only this body/footer target at 50%; retain real old grays and all outside target pixels.
    for (int row = phys.y; row < phys.y + phys.height; ++row) {
        uint8_t* p = hl->front_fb + (size_t)row * (pw / 2);
        int col = phys.x, end = col + phys.width;
        if (col & 1) { uint8_t v = p[col/2]; p[col++/2] = (uint8_t)((v & 15) | ((v & 0x80) ? 0xf0 : 0)); }
        for (; col + 1 < end; col += 2) {
            uint8_t v = p[col/2]; p[col/2] = (uint8_t)(((v & 8) ? 15 : 0) | ((v & 0x80) ? 0xf0 : 0));
        }
        if (col < end) { uint8_t v = p[col/2]; p[col/2] = (uint8_t)((v & 0xf0) | ((v & 8) ? 15 : 0)); }
    }
    return true;
}

// User revision 2026-10-29: retain every reader-ripple gray phase and insert
// four changed-white drives before the existing three settle holds.
#define READER_WATER_WHITE 4
#define READER_WATER_HOLDS 3
#define READER_WATER_PHASES (E0470_GL16_FRAMES + READER_WATER_WHITE)
static uint8_t s_water_data[READER_WATER_PHASES][16][4];
static const EpdWaveformPhases s_water_phases = {
    READER_WATER_PHASES, (const uint8_t*)s_water_data, NULL
};
static const EpdWaveformPhases* s_water_ranges[] = {&s_water_phases};
static const EpdWaveformMode s_water_mode = {MODE_GL16, 1, s_water_ranges};
static const EpdWaveformMode* s_water_modes[] = {&s_water_mode};
static EpdWaveform s_water_waveform = {
    .num_modes=1, .num_temp_ranges=1, .mode_data=s_water_modes
};
static bool s_water_ready;

const EpdWaveform* display_reader_water_waveform(void) {
    if (s_water_ready) return &s_water_waveform;
    const EpdWaveform* source=display_reader_text_waveform();
    const EpdWaveformPhases* phases=e0470_waveform_phases(source,MODE_GL16);
    if (!phases || !phases->luts || phases->phase_times ||
        phases->phases!=E0470_GL16_FRAMES) return source;
    const int drive=phases->phases-READER_WATER_HOLDS;
    for (int f=drive;f<phases->phases;++f)
        for(int i=0;i<64;++i) if(phases->luts[f*64+i]) return source;
    memcpy(s_water_data,phases->luts,(size_t)drive*64);
    memset(s_water_data+drive,0,READER_WATER_WHITE*64);
    memcpy(s_water_data+drive+READER_WATER_WHITE,phases->luts+(size_t)drive*64,
           READER_WATER_HOLDS*64);
    for(int f=drive;f<drive+READER_WATER_WHITE;++f)
        for(int from=0;from<15;++from)
            s_water_data[f][15][from/4]|=(uint8_t)(2u<<(6-2*(from%4)));
    s_water_waveform.temp_intervals=source->temp_intervals;
    s_water_ready=true;
    return &s_water_waveform;
}

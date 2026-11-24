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
#include "settings.h"
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

// 用户2026-11-24：每页只推 2 相白，15 页累计 30 相，正好由一次 30 相净负偏置的轻量 DC 恢复抵消。
// User2026-11-24: two white phases per turn, so fifteen turns accumulate thirty frames, cancelled
// by one light DC-recovery pass with thirty frames of net negative bias.
#define READER_SOFT_TURN_SETTLE_EXTRA 2
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
static bool s_soft_low;

static void set_action_in(uint8_t (*data)[16][4], int phase, int to, int from, int action) {
    const int shift = 6 - 2 * (from % 4);
    data[phase][to][from / 4] = (uint8_t)(
        (data[phase][to][from / 4] & ~(3u << shift)) | ((unsigned)action << shift)
    );
}

// 官方阅读翻页时序：整块全像素走厂商 GL16（右对齐、擦除+饱和+灰阶尾+定稿保持）。
// 改动：等值“黑→黑”去掉擦白相只保留压黑相；并在厂商 3 相定稿保持之后追加 12 相
// “全屏擦白”（三轮，所有目标为白的像素，含本来也白的空白区），翻页后白底擦得更狠；
// 擦白之后再加 4 相“重推黑”（所有目标为黑的像素），抵消整屏白推对黑字的冲淡；
// 其余所有迁移（灰阶、变黑、变白）完全按厂商原序列。
// Official reader turn timing: full-pixel vendor GL16. Equal black-on-black drops the erase
// phases and keeps the darken phases; twelve extra white phases (three full erase rounds) are
// appended after the vendor's three settle holds so every white target (including already-white
// blanks) is erased harder after a page turn, then four extra black re-drive phases darken every
// black target to compensate for the whole-panel white push. All other transitions stay
// vendor-original.
// 上面的构造抽成通用函数，供普通翻页（GL16 37 相）与快速灰阶（GRAY8 30 相）共用；普通翻页输出逐相不变。
// The construction above, shared by ordinary (GL16 37) and fast-gray (GRAY8 30) turns; ordinary output is unchanged phase for phase.
static bool build_turn_phases(const EpdWaveformPhases* gl, int extra, int black,
                              uint8_t (*data)[16][4], EpdWaveformPhases* out) {
    if (!gl || !gl->luts || gl->phase_times || gl->phases + extra + black > 64) return false;
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
    if (hold < 1) return false;
    const int drive = gl->phases - hold;
    // 定稿保持相先走完，再整屏擦白，最后以重推黑收尾（与上游 PR96 顺序一致）。
    // Vendor settle holds first, then the whole-panel white erase, finishing on the black re-drive (upstream PR96 order).
    memcpy(data, gl->luts, (size_t)drive * 16 * 4);
    memcpy(data + drive, (const uint8_t*)gl->luts + (size_t)drive * 16 * 4, (size_t)hold * 16 * 4);
    memset(data + drive + hold, 0, (size_t)(extra + black) * 16 * 4);
    for (int f = 0; f < gl->phases; ++f)
        if (e0470_phase_action(gl, f, 0, 0) == 2) set_action_in(data, f, 0, 0, 0);
    for (int i = 0; i < extra; ++i)
        for (int from = 0; from < 16; ++from) set_action_in(data, drive + hold + i, 15, from, 2);
    for (int i = 0; i < black; ++i)
        for (int from = 0; from < 16; ++from) set_action_in(data, drive + hold + extra + i, 0, from, 1);
    *out = (EpdWaveformPhases){gl->phases + extra + black, (const uint8_t*)data, NULL};
    return true;
}

const EpdWaveform* display_reader_soft_turn_waveform(void) {
    // 低残影模式保留额外擦白/重推黑；关闭时去掉这些白刷，回到 DC 平衡的厂商序列。
    // Low-ghost keeps the extra white and black pulses; disabling drops the extra white push so
    // the sequence returns to the DC-balanced vendor table.
    const bool low = app_settings_low_ghost();
    if (s_soft_ready && s_soft_low == low) return &s_soft_waveform;
    s_soft_ready = false;
    if (!build_turn_phases(e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16),
                           low ? READER_SOFT_TURN_SETTLE_EXTRA : 0,
                           low ? READER_SOFT_TURN_BLACK_EXTRA : 0,
                           s_soft_data, &s_soft_phases))
        return &E0470_WAVEFORM;
    s_soft_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_soft_low = low;
    s_soft_ready = true;
    return &s_soft_waveform;
}

// 快速灰阶翻页：用官方 GRAY8 的 GL16（30 相，8 级可分辨灰）代替默认 GL16（37 相），同样去掉“黑→黑”的擦白。
// 实屏验收：不加额外相时白底残影明显，追加 4 相整屏擦白后可接受；不加重推黑。两个宏可在编译时覆盖。
// 只给纯文字翻页用，封面/插图/周期全刷仍走原路径。
// Fast-gray turns: official GRAY8 GL16 (30 phases, 8 distinguishable grays) replaces the default GL16 (37),
// with the same equal black-on-black erase removal. On the panel, zero extra phases ghosted visibly and four
// whole-panel white phases were acceptable; no black re-drive. Both macros are compile-time tunable.
// Text-only turns; covers, images and periodic cleanup keep their paths.
#ifndef READER_GRAY8_TURN_SETTLE_EXTRA
#define READER_GRAY8_TURN_SETTLE_EXTRA 4
#endif
#ifndef READER_GRAY8_TURN_BLACK_EXTRA
#define READER_GRAY8_TURN_BLACK_EXTRA 0
#endif
static uint8_t s_gray8_turn_data[64][16][4];
static EpdWaveformPhases s_gray8_turn_phases = {30, (const uint8_t*)s_gray8_turn_data, NULL};
static const EpdWaveformPhases* s_gray8_turn_ranges[] = {&s_gray8_turn_phases};
static const EpdWaveformMode s_gray8_turn_mode = {MODE_GL16, 1, s_gray8_turn_ranges};
static const EpdWaveformMode* s_gray8_turn_modes[] = {&s_gray8_turn_mode};
static EpdWaveform s_gray8_turn_waveform = {
    .num_modes = 1, .num_temp_ranges = 1, .mode_data = s_gray8_turn_modes
};
static bool s_gray8_turn_ready;
static bool s_gray8_low;

const EpdWaveform* display_reader_gray8_turn_waveform(void) {
    const bool low = app_settings_low_ghost();
    if (s_gray8_turn_ready && s_gray8_low == low) return &s_gray8_turn_waveform;
    s_gray8_turn_ready = false;
    if (!build_turn_phases(e0470_gray8_gl16_phases_only(),
                           low ? READER_GRAY8_TURN_SETTLE_EXTRA : 0,
                           low ? READER_GRAY8_TURN_BLACK_EXTRA : 0,
                           s_gray8_turn_data, &s_gray8_turn_phases))
        return display_reader_soft_turn_waveform();
    s_gray8_turn_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_gray8_low = low;
    s_gray8_turn_ready = true;
    return &s_gray8_turn_waveform;
}

// 只驱白：所有目标为白的像素从任意状态再补白，其他目标完全保持，不动未变化的正文/文字。
// White-only drive: push white targets from any state and hold everything else, without
// re-driving unchanged text or graphics.
#define READER_WHITE_PUSH_PHASES 4
static uint8_t s_white_data[READER_WHITE_PUSH_PHASES][16][4];
static const EpdWaveformPhases s_white_phases = {
    READER_WHITE_PUSH_PHASES, (const uint8_t*)s_white_data, NULL
};
static const EpdWaveformPhases* s_white_ranges[] = {&s_white_phases};
static const EpdWaveformMode s_white_mode = {MODE_GL16, 1, s_white_ranges};
static const EpdWaveformMode* s_white_modes[] = {&s_white_mode};
static EpdWaveform s_white_waveform = {
    .num_modes=1, .num_temp_ranges=1, .mode_data=s_white_modes
};
static bool s_white_ready;

const EpdWaveform* display_reader_white_push_waveform(void) {
    if (s_white_ready) return &s_white_waveform;
    memset(s_white_data, 0, sizeof(s_white_data));
    for (int f = 0; f < READER_WHITE_PUSH_PHASES; ++f)
        for (int from = 0; from < 16; ++from)
            s_white_data[f][15][from / 4] |= (uint8_t)(2u << (6 - 2 * (from % 4)));
    s_white_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_white_ready = true;
    return &s_white_waveform;
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
    // 关闭低残影模式时水波纹直接用原文字表：没有额外白刷，DC 平衡。
    // With low-ghost off the ripple uses the plain text table: no extra white push, DC balanced.
    if (!app_settings_low_ghost()) return display_reader_text_waveform();
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

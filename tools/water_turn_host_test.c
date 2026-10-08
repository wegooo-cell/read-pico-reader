/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：验证真实水波纹组件的 16 带、37 相、四向映射和失败后旧帧基准。
 * English: Exercise the real water-turn engine's 16 bands, 37 phases, four directions and failure baseline.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "e0470_page_turn.h"
#include "e0470_epaper_waveform.h"

#define WIDTH 1216
#define HEIGHT 684
#define FB_BYTES ((WIDTH * HEIGHT) / 2)
static uint8_t front[FB_BYTES], back[FB_BYTES], diff[WIDTH * HEIGHT];
static bool dirty_lines[HEIGHT];
static uint8_t dirty_columns[WIDTH / 2];
static const EpdWaveformPhases gl = {.phases = 37, .luts = (const uint8_t*)"x"};
const EpdWaveform E0470_WAVEFORM = {.id = 1};
const EpdWaveform E0470_APPLY_WAVEFORM = {.id = 2};
static enum EpdRotation rotation;
static e0470_turn_dir_t direction;
static bool check_direction = true;
static const uint8_t* const* staged_luts;
static const int8_t* staged_lines;
static const int *staged_x0, *staged_x1;
static const int8_t* staged_bands;
static int staged_count;
static int scans, fail_at, differences, powerons;
static int scan_time_us = 7000;
static uint32_t trace_hash;
static int64_t now_us;
int water_test_alloc_fail;

int epd_width(void) { return WIDTH; }
int epd_height(void) { return HEIGHT; }
enum EpdRotation epd_get_rotation(void) { return rotation; }
EpdRect epd_full_screen(void) { return (EpdRect){0, 0, WIDTH, HEIGHT}; }
void epd_poweron(void) { ++powerons; }
int64_t esp_timer_get_time(void) { return now_us; }
void esp_rom_delay_us(uint32_t us) { now_us += us; }
const EpdWaveformPhases* e0470_waveform_phases(const EpdWaveform* waveform, int mode) {
    assert(waveform == &E0470_WAVEFORM && mode == MODE_GL16);
    return &gl;
}
void epd_build_1ppB_lut_1k(uint8_t* lut, const EpdWaveformPhases* phases, int frame) {
    assert(phases == &gl && frame >= 0 && frame < 37);
    memset(lut, frame, 1024);
}
EpdRect epd_difference_image_cropped(const uint8_t* to, const uint8_t* from, EpdRect area,
                                      uint8_t* difference, bool* lines, uint8_t* columns) {
    assert(to == front && from == back && difference == diff && lines == dirty_lines && columns == dirty_columns);
    assert(area.x >= 0 && area.y >= 0 && area.x + area.width <= WIDTH &&
           area.y + area.height <= HEIGHT && area.width > 0 && area.height > 0);
    ++differences;
    return area;
}
void epd_clear_phase_luts(void) {
    staged_luts = NULL;
    staged_lines = NULL;
    staged_x0 = staged_x1 = NULL;
    staged_bands = NULL;
    staged_count = 0;
}
void epd_set_line_phase_luts(const uint8_t* const* luts, const int8_t* lines) {
    epd_clear_phase_luts();
    staged_luts = luts;
    staged_lines = lines;
}
void epd_set_col_phase_luts(const uint8_t* const* luts, const int* x0, const int* x1,
                            const int8_t* phases, int count) {
    epd_clear_phase_luts();
    staged_luts = luts;
    staged_x0 = x0;
    staged_x1 = x1;
    staged_bands = phases;
    staged_count = count;
}
static void physical_to_logical(int px, int py, int* lx, int* ly) {
    switch (rotation) {
        case EPD_ROT_LANDSCAPE: *lx = px; *ly = py; break;
        case EPD_ROT_PORTRAIT: *lx = py; *ly = WIDTH - px - 1; break;
        case EPD_ROT_INVERTED_LANDSCAPE: *lx = WIDTH - px - 1; *ly = HEIGHT - py - 1; break;
        case EPD_ROT_INVERTED_PORTRAIT: *lx = HEIGHT - py - 1; *ly = px; break;
    }
}
enum EpdDrawError epd_draw_base(EpdRect area, const uint8_t* data, EpdRect crop,
                                 enum EpdDrawMode mode, int temperature, const bool* lines,
                                 const uint8_t* columns, const EpdWaveform* waveform) {
    assert(area.width == WIDTH && crop.height == HEIGHT && data == diff);
    assert(mode == (MODE_PACKING_1PPB_DIFFERENCE | MODE_DU) && temperature == 25);
    assert(lines == dirty_lines && columns == dirty_columns && waveform == &E0470_APPLY_WAVEFORM);
    assert(staged_luts && (staged_lines != NULL) != (staged_bands != NULL));
    int tick = scans++;
    int active = 0;
    bool phase_seen[37] = {0};
    int px = -1, py = -1;
    if (staged_lines) {
        for (int y = 0; y < HEIGHT; ++y) {
            int p = staged_lines[y];
            if (p < 0) continue;
            assert(p < 37 && staged_luts[p][0] == p);
            if (!phase_seen[p]) { phase_seen[p] = true; ++active; }
            if (px < 0) { px = WIDTH / 2; py = y; }
        }
    } else {
        assert(staged_count == 16);
        for (int b = 0; b < staged_count; ++b) {
            int p = staged_bands[b];
            if (p < 0) continue;
            assert(p < 37 && staged_luts[p][0] == p);
            assert(staged_x0[b] >= 0 && staged_x1[b] <= WIDTH && staged_x1[b] > staged_x0[b]);
            if (!phase_seen[p]) { phase_seen[p] = true; ++active; }
            if (px < 0) { px = (staged_x0[b] + staged_x1[b]) / 2; py = HEIGHT / 2; }
        }
    }
    int expected = 0;
    for (int b = 0; b < 16; ++b) if (tick - b >= 0 && tick - b < 37) ++expected;
    assert(active == expected);
    if (tick == 0 && check_direction) {
        int lx, ly;
        physical_to_logical(px, py, &lx, &ly);
        int coord = direction <= E0470_TURN_RTL ? lx : ly;
        int span = direction <= E0470_TURN_RTL ?
            ((rotation & 1) ? HEIGHT : WIDTH) : ((rotation & 1) ? WIDTH : HEIGHT);
        if (direction == E0470_TURN_LTR || direction == E0470_TURN_TTB) assert(coord < span / 4);
        else assert(coord > span * 3 / 4);
    }
    // 两档输出的相位、覆盖范围必须逐拍一致，时钟不计入指纹。
    // Both speeds must submit identical phases and masks; timing is excluded from the fingerprint.
    for (int y = 0; y < HEIGHT; ++y) {
        trace_hash = (trace_hash ^ (uint8_t)lines[y]) * 16777619u;
        if (staged_lines) trace_hash = (trace_hash ^ (uint8_t)staged_lines[y]) * 16777619u;
    }
    for (int x = 0; x < WIDTH / 2; ++x) trace_hash = (trace_hash ^ columns[x]) * 16777619u;
    for (int b = 0; b < staged_count; ++b)
        trace_hash = (trace_hash ^ (uint8_t)staged_bands[b]) * 16777619u;
    now_us += scan_time_us;
    return fail_at == scans ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS;
}

int main(void) {
    EpdiyHighlevelState hl = {front, back, diff, dirty_lines, dirty_columns};
    uint32_t original_traces[4][4];
    assert(e0470_page_turn_tick_us() == E0470_TURN_DEFAULT_TICK_US);
    for (int speed = 0; speed < 2; ++speed) {
    int target_us = speed ? E0470_TURN_FAST_TICK_US : E0470_TURN_DEFAULT_TICK_US;
    e0470_page_turn_set_tick_us(target_us);
    assert(e0470_page_turn_tick_us() == target_us);
    for (int rot = 0; rot < 4; ++rot) for (int dir = 0; dir < 4; ++dir) {
        rotation = (enum EpdRotation)rot;
        direction = (e0470_turn_dir_t)dir;
        memset(front, 0x24, sizeof(front));
        memset(back, 0xFF, sizeof(back));
        scans = differences = powerons = fail_at = 0;
        now_us = 0; trace_hash = 2166136261u;
        EpdRect logical = (rot & 1) ? (EpdRect){0, 0, HEIGHT, WIDTH} :
                                      (EpdRect){0, 0, WIDTH, HEIGHT};
        assert(e0470_page_turn(&hl, logical, direction) == EPD_DRAW_SUCCESS);
        assert(scans == 52 && differences == 1 && powerons == 1);
        assert(!memcmp(back, front, sizeof(back)) && now_us == 52 * target_us);
        if (!speed) original_traces[rot][dir] = trace_hash;
        else assert(original_traces[rot][dir] == trace_hash);
        // 真实阅读页保留页眉和底栏；裁剪区同样必须覆盖全部 16 带。
        // The actual reader leaves header and footer outside the crop; all 16 bands must still run.
        EpdRect reader = {0, 160, logical.width, logical.height - 288};
        memset(back, 0xFF, sizeof(back));
        scans = differences = 0;
        check_direction = false;
        assert(e0470_page_turn(&hl, reader, direction) == EPD_DRAW_SUCCESS);
        assert(scans == 52 && differences == 1);
        // 全屏阅读保留状态栏时从第 80 行开始，水波纹仍要覆盖整个正文。
        // Full-screen reading keeps the status row at y=80 while animating the remaining body.
        EpdRect fullscreen_reader = {0, 80, logical.width, logical.height - 80};
        memset(back, 0xFF, sizeof(back));
        scans = differences = 0;
        assert(e0470_page_turn(&hl, fullscreen_reader, direction) == EPD_DRAW_SUCCESS);
        assert(scans == 52 && differences == 1);
        check_direction = true;
        memset(back, 0xFF, sizeof(back));
        scans = differences = 0;
        fail_at = 13;
        assert(e0470_page_turn(&hl, logical, direction) == EPD_DRAW_OTHER_ERROR);
        assert(scans == 13 && differences == 1);
        for (size_t i = 0; i < sizeof(back); ++i) assert(back[i] == 0xFF);
    }
    }
    // 扫描超出目标时允许自然完成；任一拍失败都保留旧帧并清理暂存LUT。
    // Let scans exceeding the target finish; any tick failure retains history and clears staged LUTs.
    check_direction = false;
    e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
    fail_at = 0; scans = differences = 0; now_us = 0; scan_time_us = 18000;
    assert(e0470_page_turn(&hl, (EpdRect){0, 0, HEIGHT, WIDTH}, E0470_TURN_RTL) == EPD_DRAW_SUCCESS);
    assert(scans == 52 && now_us == 52 * 18000 && !staged_luts);
    for (int tick = 1; tick <= 52; ++tick) {
        scans = differences = 0; fail_at = tick;
        memset(back, 0xFF, sizeof(back));
        assert(e0470_page_turn(&hl, (EpdRect){0, 0, HEIGHT, WIDTH}, E0470_TURN_RTL) == EPD_DRAW_OTHER_ERROR);
        assert(scans == tick && differences == 1 && !staged_luts);
        for (size_t i = 0; i < sizeof(back); ++i) assert(back[i] == 0xFF);
    }
    e0470_page_turn_set_tick_us(E0470_TURN_DEFAULT_TICK_US);
    assert(e0470_page_turn_tick_us() == 21000);
    e0470_page_turn_release();
    memset(back, 0xFF, sizeof(back));
    scans = differences = 0;
    fail_at = 0;
    water_test_alloc_fail = 1;
    assert(e0470_page_turn(&hl, (EpdRect){0, 0, HEIGHT, WIDTH}, E0470_TURN_LTR) ==
           EPD_DRAW_NO_PHASES_AVAILABLE);
    assert(scans == 0 && differences == 0);
    for (size_t i = 0; i < sizeof(back); ++i) assert(back[i] == 0xFF);
    puts("water turn: both speeds, 4 rotations x 4 directions, identical phase/mask traces, 52 ticks, all-tick failure recovery; simulated 1092ms -> 728ms; slower scans never interrupted");
}

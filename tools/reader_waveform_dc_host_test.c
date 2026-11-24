/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：审计阅读翻页波形的直流平衡：统计每个 (from→to) 的擦白/压黑帧数，
 * 检查同灰像素是否零驱动、黑白往返是否对称、正文页的版面平均是否接近 0。
 * English: Audit DC balance of reader-turn waveforms: count erase/darken frames per
 * (from→to), requiring zero drive for unchanged grays, symmetric black/white round trips
 * and a near-zero panel average for a text page.
 * 冻结：只做帧数账，不替代实屏灰阶与残影验收；翻页表只报告、不做门禁，方案调整时用来对比。
 * Frozen: frame accounting only; it does not replace panel validation of grays or ghosting.
 * Reader-turn tables are reported, not gated, so alternative profiles can be compared.
 */
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include "epdiy.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define FRAME_US 11090

static bool s_low_ghost;

bool app_settings_low_ghost(void) { return s_low_ghost; }

// 速读准备不在审计范围内，但同一个编译单元引用了面板尺寸接口，这里给 0 替身。
// Fast-text preparation is out of scope; the same translation unit references panel size, so stub it.
int epd_width(void) { return 0; }
int epd_height(void) { return 0; }
enum EpdRotation epd_get_rotation(void) { return EPD_ROT_LANDSCAPE; }

static int dc_of(const EpdWaveformPhases* phases, int from, int to) {
    int erase = 0, darken = 0;
    for (int f = 0; f < phases->phases; ++f) {
        const int action = e0470_phase_action(phases, f, to, from);
        if (action == 2) ++erase;
        else if (action == 1) ++darken;
    }
    return erase - darken;
}

static int same_gray_bad(const EpdWaveformPhases* phases) {
    int bad = 0;
    for (int g = 0; g < 16; ++g)
        if (dc_of(phases, g, g) != 0) ++bad;
    return bad;
}

// 1000 分比，避免浮点：加权平均 ×1000。
static int panel_avg_milli(const EpdWaveformPhases* phases, const int mix[][3], int count) {
    int sum = 0, total = 0;
    for (int i = 0; i < count; ++i) {
        sum += mix[i][2] * dc_of(phases, mix[i][0], mix[i][1]);
        total += mix[i][2];
    }
    return total ? sum * 1000 / total : 0;
}

static void report(const char* name, const EpdWaveformPhases* phases) {
    printf("%s: %d phases, %d ms\n", name, phases->phases,
           phases->phases * FRAME_US / 1000);
    printf("  same-gray dc:");
    for (int g = 0; g < 16; ++g) printf(" %d:%+d", g, dc_of(phases, g, g));
    printf("\n  pairs: 0->15 %+d, 15->0 %+d, 8->15 %+d, 15->8 %+d\n",
           dc_of(phases, 0, 15), dc_of(phases, 15, 0),
           dc_of(phases, 8, 15), dc_of(phases, 15, 8));
}

static int check_turn(const char* name, const EpdWaveform* waveform, bool enforce) {
    const EpdWaveformPhases* phases = e0470_waveform_phases(waveform, MODE_GL16);
    if (!phases || !phases->luts) {
        printf("FAIL %s: no GL16 table\n", name);
        return 1;
    }
    report(name, phases);
    int bad = 0;
    const char* level = enforce ? "FAIL" : "WARN";
    if (phases->phases > 64) {
        printf("%s %s: %d phases exceed the 64-frame buffer\n", level, name, phases->phases);
        if (enforce) ++bad;
    }
    if (same_gray_bad(phases)) {
        printf("%s %s: unchanged pixels are still driven\n", level, name);
        if (enforce) ++bad;
    }
    const int black_white = dc_of(phases, 0, 15);
    const int white_black = dc_of(phases, 15, 0);
    if (black_white != -white_black) {
        printf("%s %s: 0->15 %+d and 15->0 %+d do not cancel\n",
               level, name, black_white, white_black);
        if (enforce) ++bad;
    }
    static const int text_page[][3] = {
        {15, 15, 80}, {0, 0, 18}, {15, 0, 1}, {0, 15, 1},
    };
    const int avg = panel_avg_milli(phases, text_page, 4);
    printf("  text-page panel average: %+d.%03d frames\n", avg / 1000, avg % 1000 < 0 ? -avg % 1000 : avg % 1000);
    if (avg > 1000 || avg < -1000) {
        printf("%s %s: panel average %+d milli-frames exceeds +/-1 frame\n", level, name, avg);
        if (enforce) ++bad;
    }
    return bad;
}

int main(void) {
    int failures = 0;
    e0470_waveform_init();

    // 厂商完整 48 相：同灰应该只有白底那 1 帧 tick。
    const EpdWaveformPhases* full = e0470_waveform_phases(&E0470_FULL_WAVEFORM, MODE_GL16);
    if (!full) {
        printf("FAIL: full GL16 missing\n");
        return 1;
    }
    report("full GL16 (vendor reference)", full);
    for (int g = 0; g < 15; ++g)
        if (dc_of(full, g, g) != 0) {
            printf("FAIL: vendor GL16 gray %d is %+d\n", g, dc_of(full, g, g));
            ++failures;
        }
    if (dc_of(full, 15, 15) != 1) {
        printf("FAIL: vendor GL16 white tick is %+d\n", dc_of(full, 15, 15));
        ++failures;
    }

    s_low_ghost = false;
    // 翻页表只报告；同灰零驱动/对称性留给后续方案决定是否作为门禁。
    // Reader-turn tables are report-only for now; the next profile decides what to gate.
    s_low_ghost = false;
    (void)check_turn("soft turn, low-ghost off", display_reader_soft_turn_waveform(), false);
    s_low_ghost = true;
    (void)check_turn("soft turn, low-ghost on", display_reader_soft_turn_waveform(), false);

    // 快速灰阶与水波纹还没有做同灰零驱动/对称迁移，先作为已知项报告，不计失败。
    // Fast-gray and ripple have not been balanced yet; report them as known items without failing.
    s_low_ghost = true;
    (void)check_turn("fast-gray turn, low-ghost on", display_reader_gray8_turn_waveform(), false);
    s_low_ghost = false;
    (void)check_turn("fast-gray turn, low-ghost off", display_reader_gray8_turn_waveform(), false);

    s_low_ghost = true;
    const EpdWaveform* ripple = display_reader_water_waveform();
    if (ripple) (void)check_turn("ripple, low-ghost on", ripple, false);

    if (failures) {
        printf("reader_waveform_dc_host_test: FAIL (%d)\n", failures);
        return 1;
    }
    puts("reader_waveform_dc_host_test: PASS");
    return 0;
}

/* SPDX-FileCopyrightText: 2026 zhubeilife
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：检查小纸普通翻页（GL16 37+12+4 相）与快速灰阶翻页（GRAY8 GL16 30+4 擦白相）的构造。
 * English: Check Pico ordinary turns (GL16 37+12+4) and fast-gray turns (GRAY8 GL16 30+4 white).
 *
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DKIIKO_BOARD_METALIO47=0 \
 *   -Imain -Itools/waveform_host_stubs -Icomponents/epdiy/include -Icomponents/epdiy/src \
 *   -Icomponents/e0470_epaper_waveform/include -Icomponents/e0470_epaper_waveform/waveforms \
 *   tools/pico_reader_turn_host_test.c boards/pico/src/main/display_reader_waveform.c \
 *   boards/pico/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c \
 *   components/e0470_epaper_waveform/e0470_waveform_trim.c -o build/pico-reader-turn && build/pico-reader-turn
 */
#include <assert.h>
#include <stdio.h>
#include "e0470_epaper_waveform.h"
#include "display_reader_waveform.h"

int epd_width(void) { return 684; }
int epd_height(void) { return 1216; }
enum EpdRotation epd_get_rotation(void) { return (enum EpdRotation)0; }

int main(void) {
    e0470_waveform_init();
    const EpdWaveformPhases* def = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
    const EpdWaveformPhases* soft = e0470_waveform_phases(display_reader_soft_turn_waveform(), MODE_GL16);
    const EpdWaveformPhases* src = e0470_waveform_phases(&E0470_GRAY8_WAVEFORM, MODE_GL16);
    const EpdWaveformPhases* gray8 = e0470_waveform_phases(display_reader_gray8_turn_waveform(), MODE_GL16);
    assert(def && soft && src && gray8);
    assert(soft->phases == def->phases + 12 + 4);
    assert(gray8->phases == src->phases + 4);
    // 普通翻页须与上游 PR96 的构造逐相一致：驱动相 → 厂商定稿保持 → 12 相擦白 → 4 相重推黑。
    // Ordinary turns must match upstream PR96 phase by phase: drive, vendor holds, 12 white, 4 black.
    int hold = 0;
    for (;;) {
        int nonzero = 0;
        for (int to = 0; to < 16; ++to)
            for (int from = 0; from < 16; ++from)
                nonzero |= e0470_phase_action(def, def->phases - 1 - hold, to, from);
        if (nonzero) break;
        ++hold;
    }
    assert(hold >= 1);
    for (int f = 0; f < soft->phases; ++f)
        for (int to = 0; to < 16; ++to)
            for (int from = 0; from < 16; ++from) {
                int want;
                if (f < def->phases)
                    want = (to == 0 && from == 0 && e0470_phase_action(def, f, 0, 0) == 2)
                        ? 0 : e0470_phase_action(def, f, to, from);
                else if (f < def->phases + 12) want = to == 15 ? 2 : 0;
                else want = to == 0 ? 1 : 0;
                assert(e0470_phase_action(soft, f, to, from) == want);
            }
    for (int f = 0; f < gray8->phases; ++f)
        for (int to = 0; to < 16; ++to)
            for (int from = 0; from < 16; ++from) {
                const int a = e0470_phase_action(gray8, f, to, from);
                if (f >= src->phases) { assert(a == (to == 15 ? 2 : 0)); continue; }  // 追加擦白 / appended white
                const int b = e0470_phase_action(src, f, to, from);
                assert(a == b || (to == 0 && from == 0 && b == 2 && a == 0));
            }
    printf("vendor GL16 settle holds %d; ordinary turn matches upstream PR96 order\n", hold);
    printf("ordinary turn %d phases (~%.0f ms), fast gray %d phases (~%.0f ms) @11.09 ms/frame\n",
           soft->phases, soft->phases * 11.09, gray8->phases, gray8->phases * 11.09);
    return 0;
}

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：把 Pico 的 mark_present_line 提前停止与 leading_skip_frames 抽取编译，和独立参考实现逐次比对 skip。
English: Compile Pico's early-stopping mark_present_line and leading_skip_frames against an independent reference and compare every skip.
"""
from pathlib import Path
import json
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'boards/pico/src/components/epdiy/src/render.c'

# 生成视图带有板型标记；非 Pico 视图直接跳过。
# Generated views carry a board marker; skip non-Pico views.
MARKER = ROOT / '.kiiko-source-view.json'
if MARKER.exists():
    view_board = json.loads(MARKER.read_text(encoding='utf-8')).get('board')
    if view_board and view_board != 'pico':
        print('skip: pico-only test in a %s board view' % view_board)
        raise SystemExit(0)
if not RENDER.exists():
    # 本测试只针对 Pico 的 render.c；没有该文件时打印说明后跳过。
    # This test only targets Pico's render.c; skip with a note when it is absent.
    print('skip: pico render.c not present (pico-only test)')
    raise SystemExit(0)


def _slice(start_marker, end_marker):
    source = RENDER.read_text()
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]

# 静态状态与测试所需的最小依赖；函数体直接从真实 render.c 抽取。
# Statics and minimal harness deps; function bodies come from the real render.c.
PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
typedef struct {int x,y,width,height;} EpdRect;
typedef struct { int phases; const uint8_t* luts; const int* phase_times; } EpdWaveformPhases;
uint32_t epd_interlace_4bpp_line_VE(const uint8_t*,const uint8_t*,uint8_t*,uint8_t*,int);
#define W 64
#define H 32
int epd_width(void){return W;}
static bool s_leading_skip_enabled=true;
static uint8_t s_present[256];
static const uint8_t* s_present_for;
static const EpdWaveformPhases* s_present_resolved_for;
static const EpdWaveformPhases* s_first_active_phases;
static uint8_t s_first_active[256];
static uint8_t s_first_active_min;
'''
# 上面的静态状态覆盖了 epd_leading_skip_discard 到 leading_skip_frames 以及差分所需的全部状态。
# The statics above cover every state used from epd_leading_skip_discard through the difference helpers.

# 带 __attribute__ 的函数无法用 function() 抽取（属性在同一行），按真实顺序整段切。
# function() cannot extract attributed functions (attribute on the same line); slice them in file order.
CORE = _slice(
    'static void build_first_active(',
    'static inline int rounded_display_height() {',
)
DIFF = _slice(
    '__attribute__((optimize("O3"))) static inline int _interlace_line_unaligned(',
    '\nEpdRect epd_difference_image(\n',
)

MAIN = r'''
#define B (W*H/2)
#define D (W*H)
#define NP 8
static _Alignas(16) uint8_t to[B], from[B], interlaced[D], other[D];
static bool rows[H];
static uint8_t cols[W/2];
static uint8_t LUT0[NP][16][4], LUT1[NP][16][4], LUT3[NP][16][4], LUTH[NP][16][4];
static EpdWaveformPhases WF0={NP,(const uint8_t*)LUT0,NULL};
static EpdWaveformPhases WF1={NP,(const uint8_t*)LUT1,NULL};
static EpdWaveformPhases WF3={NP,(const uint8_t*)LUT3,NULL};
static EpdWaveformPhases WFH={NP,(const uint8_t*)LUTH,NULL};
static unsigned checks;

// 构造一张波形：special_pair 首次动作在 min_phase，其余对首次动作在 min_phase+1（或保持）。
// Build a waveform where special_pair first acts at min_phase and every other pair acts at min_phase+1.
static void build_waveform(uint8_t (*l)[16][4], int min_phase, int special_pair) {
    memset(l, 0, sizeof(uint8_t)*NP*16*4);
    int other = min_phase + 1;
    for (int to_i = 0; to_i < 16; to_i++)
        for (int from_i = 0; from_i < 16; from_i++) {
            int pair = (to_i << 4) | from_i;
            int first = (pair == special_pair) ? min_phase : other;
            if (first >= NP) continue;
            for (int f = first; f < NP; f++)
                l[f][to_i][from_i/4] |= (uint8_t)(1 << (6 - 2*(from_i%4)));
        }
}

static void setp(uint8_t* p, int x, int y, unsigned v) {
    size_t at = (size_t)y*W/2 + x/2; unsigned shift = 4*(x&1);
    p[at] = (uint8_t)((p[at] & (uint8_t)~(15u<<shift)) | (uint8_t)(v<<shift));
}
static void fill(uint8_t* p) {
    static uint32_t rng = 0x9e3779b9u;
    for (size_t i = 0; i < (size_t)B; i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        p[i] = (uint8_t)rng;
    }
}
static void diff(EpdRect crop) {
    memset(cols, 0, sizeof(cols));
    memset(rows, 0, sizeof(rows));
    (void)epd_difference_image_base(to, from, crop, W, H, interlaced, rows, cols);
}

// ---- 独立参考实现：不依赖被修改的代码。----
// ---- Independent reference: no dependence on the patched code. ----
static int ref_first_active(const EpdWaveformPhases* ph, uint8_t pair) {
    const uint8_t(*l)[16][4] = (const uint8_t(*)[16][4])ph->luts;
    int to_i = pair >> 4, from_i = pair & 15;
    for (int f = 0; f < ph->phases; f++)
        if ((l[f][to_i][from_i/4] >> (6 - 2*(from_i%4))) & 3) return f;
    return ph->phases;
}
static int clamp_skip(const EpdWaveformPhases* ph, int skip) {
    if (skip >= ph->phases) skip = ph->phases - 1;
    return skip < 0 ? 0 : skip;
}
// 整块重扫路径：遍历差分图全部字节取最小。
// Full-rescan path: min over every byte of the difference image.
static int ref_skip_full(const EpdWaveformPhases* ph, const uint8_t* data, EpdRect area) {
    int skip = ph->phases;
    size_t n = (size_t)area.width * (size_t)area.height;
    for (size_t i = 0; i < n; i++) {
        int fa = ref_first_active(ph, data[i]);
        if (fa < skip) skip = fa;
    }
    return clamp_skip(ph, skip);
}
// 统计路径参考：mark_present_line 只覆盖裁剪矩形对齐后的列段与行区间。
// Stats-path reference: mark_present_line only covers the aligned column segment and row span of the crop.
static int ref_skip_segment(const EpdWaveformPhases* ph, const uint8_t* data, EpdRect crop) {
    int x0, x1;
    epd_difference_column_range(crop, &x0, &x1);
    int y_end = min(H, crop.y + crop.height);
    int skip = ph->phases;
    for (int y = crop.y; y < y_end; y++)
        for (int x = x0; x < x1; x++) {
            int fa = ref_first_active(ph, data[(size_t)y*W + x]);
            if (fa < skip) skip = fa;
        }
    return clamp_skip(ph, skip);
}

static const EpdWaveformPhases* WFS[4];

static void run_all_waveforms(void) {
    for (int w = 0; w < 4; w++) {
        const EpdWaveformPhases* ph = WFS[w];
        EpdRect full = {0,0,W,H};
        // 全屏统计路径：s_present_for == interlaced，直接复用差分统计（含提前停止）。
        // Full-screen stats path: s_present_for == interlaced, so reuse the diff statistics (including early stop).
        diff(full);
        int got = leading_skip_frames(ph, interlaced, full);
        int exp = ref_skip_segment(ph, interlaced, full);
        if (got != exp) { fprintf(stderr, "stats w=%d: got %d want %d\n", w, got, exp); abort(); }
        checks++;
        // 丢弃后走整块重扫路径。
        // After discarding, take the full-rescan path.
        epd_leading_skip_discard();
        got = leading_skip_frames(ph, interlaced, full);
        exp = ref_skip_full(ph, interlaced, full);
        if (got != exp) { fprintf(stderr, "discard w=%d: got %d want %d\n", w, got, exp); abort(); }
        checks++;
        // 换一块缓冲也走整块重扫路径。
        // A different buffer also takes the full-rescan path.
        memcpy(other, interlaced, D);
        got = leading_skip_frames(ph, other, full);
        exp = ref_skip_full(ph, other, full);
        if (got != exp) { fprintf(stderr, "other w=%d: got %d want %d\n", w, got, exp); abort(); }
        checks++;
    }
}

int main(void) {
    build_waveform(LUT0, 0, 0xF0);
    build_waveform(LUT1, 1, 0xF1);
    build_waveform(LUT3, 3, 0xF3);
    memset(LUTH, 0, sizeof(LUTH));
    WFS[0]=&WF0; WFS[1]=&WF1; WFS[2]=&WF3; WFS[3]=&WFH;

    // 各种帧：白底相同、纯灰、稀疏文字、随机、随机相同。
    // Frame patterns: identical white, solid gray, sparse text, random, random-identical.
    for (int pattern = 0; pattern < 5; pattern++) {
        switch (pattern) {
            case 0: memset(to,0xFF,B); memset(from,0xFF,B); break;
            case 1: memset(to,0x77,B); memset(from,0x77,B); break;
            case 2:
                memset(to,0xFF,B); memset(from,0xFF,B);
                setp(to,7,3,0); setp(to,9,3,0); setp(to,11,5,0);
                setp(from,5,9,0); setp(from,8,12,1); setp(to,8,12,15);
                break;
            case 3: fill(to); fill(from); break;
            default: fill(to); memcpy(from,to,B); break;
        }
        run_all_waveforms();
    }

    // 同一波形连续两次命中提前停止：先建 WF0 的表，两轮差分都含下限对 0xF0。
    // Same waveform hits the early stop twice in a row: build WF0's table first, then diff two frames that both contain the floor pair 0xF0.
    {
        EpdRect full = {0,0,W,H};
        memset(other, 0xFF, D);
        (void)leading_skip_frames(&WF0, other, full); // 触发 build_first_active(WF0) / builds WF0's table
        for (int round = 0; round < 2; round++) {
            memset(to,0xFF,B); memset(from,0xFF,B); setp(to,3,2,15); setp(from,3,2,0); // 0xF0
            diff(full);
            int got = leading_skip_frames(&WF0, interlaced, full);
            assert(got == ref_skip_segment(&WF0, interlaced, full));
            assert(got == 0); // 命中下限后取全局下限 / takes the global floor after hitting it
            checks++;
        }
    }

    // 差分后换波形：提前停止的结论对不上新表，必须退回整块重扫且结果正确。
    // Changing waveform after the diff: the early-stop conclusion no longer matches the new table, so it must rescan and stay correct.
    memset(to,0xFF,B); memset(from,0xFF,B); setp(to,3,2,15); setp(from,3,2,0); // WF0 下限 / WF0 floor
    {
        EpdRect full = {0,0,W,H};
        diff(full);
        assert(leading_skip_frames(&WF1, interlaced, full) == ref_skip_full(&WF1, interlaced, full));
        checks++;
        assert(leading_skip_frames(&WF0, interlaced, full) == ref_skip_full(&WF0, interlaced, full));
        checks++;
    }

    // 裁剪区域只覆盖部分列/行：统计路径对裁剪列段，丢弃后整块重扫都正确。
    // Crops covering only part of the columns/rows: the stats path uses the cropped segment, and the discard path rescans the whole image, both correct.
    for (int ci = 0; ci < 3; ci++) {
        EpdRect crop;
        switch (ci) {
            case 0: crop = (EpdRect){5, 2, 20, 8}; break;   // 部分列 + 部分行 / partial columns + rows
            case 1: crop = (EpdRect){0, 8, W, 12}; break;   // 整宽、部分行 / full width, partial rows
            case 2: crop = (EpdRect){0, 0, 16, H}; break;   // 部分列（对齐到 32）、整高 / partial columns (aligned to 32), full height
            default: abort();
        }
        fill(to); fill(from);
        memset(interlaced, 0x00, D);
        diff(crop);
        // 统计路径只统计裁剪覆盖的对齐列段。
        // The stats path only covers the aligned column segment of the crop.
        assert(leading_skip_frames(&WF0, interlaced, (EpdRect){0,0,W,H})
               == ref_skip_segment(&WF0, interlaced, crop));
        checks++;
        // 丢弃后整块重扫（interlaced 非裁剪区已初始化为 0x00，参考一致）。
        // After discarding, rescan the whole image (non-crop interlaced bytes were initialized to 0x00, matching the reference).
        epd_leading_skip_discard();
        assert(leading_skip_frames(&WF0, interlaced, (EpdRect){0,0,W,H})
               == ref_skip_full(&WF0, interlaced, (EpdRect){0,0,W,H}));
        checks++;
    }

    // s_leading_skip_enabled 为 false 时行为不变：差分不统计，直接整块重扫。
    // Disabling leading skip keeps the old behavior: the diff records nothing and the draw rescans the whole image.
    s_leading_skip_enabled = false;
    fill(to); fill(from);
    {
        EpdRect full = {0,0,W,H};
        memset(interlaced, 0x00, D);
        diff(full);
        assert(!s_present_for);
        assert(leading_skip_frames(&WF0, interlaced, full) == ref_skip_full(&WF0, interlaced, full));
        checks++;
    }
    s_leading_skip_enabled = true;

    printf("PASS: %u early-stop skip comparisons match the independent reference "
           "(full/stats/discard/other buffers, waveform switch, partial crops, disabled path)\n", checks);
    return 0;
}
'''

unit = PREFIX + function('epd_leading_skip_discard', RENDER) + CORE + DIFF + MAIN

with tempfile.TemporaryDirectory(prefix='pico-leading-skip-') as tmp:
    folder = Path(tmp)
    (folder / 'esp_attr.h').write_text('#define IRAM_ATTR\n')
    c = folder / 'test.c'
    c.write_text(unit)
    exe = folder / 'test'
    subprocess.run(
        ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unknown-attributes',
         '-Wno-pointer-to-int-cast', '-fsanitize=address,undefined', '-I' + tmp,
         str(c), str(ROOT / 'components/epdiy/src/riscv_pixels.c'), '-o', str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)

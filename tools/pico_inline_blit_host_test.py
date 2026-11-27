#!/usr/bin/env python3
"""中文：核对 Pico 插图贴图快速路径与逐像素 epd_draw_pixel 参考实现逐字节一致。
English: Verify the Pico inline-image blit fast path is byte-identical to a per-pixel epd_draw_pixel reference.
SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
view = root / '.kiiko-source-view.json'
pico = '--pico' in sys.argv or (view.is_file() and json.loads(view.read_text())['board'] == 'pico')
if not pico:
    # 本测试只覆盖 Pico 的插图贴图快速路径；非 Pico 视图直接跳过，避免硬失败。
    # This test only covers the Pico inline-image blit fast path; skip on a non-Pico view instead of failing.
    print('中文：本测试只覆盖 Pico 的插图贴图快速路径；当前视图不是 Pico，跳过。')
    print('English: this test only covers the Pico inline-image blit fast path; the current view is not Pico, skipping.')
    sys.exit(0)
book_app = root / 'boards/pico/src/main/apps/app_book.c'
if not book_app.is_file():
    raise SystemExit(f'missing {book_app}; run this test in a pico board-source view')
source = book_app.read_text()


def function(name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    cleaned = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), source, flags=re.S)
    at, depth, quote, escaped = match.end(), 1, None, False
    while depth:
        c = cleaned[at]
        if quote:
            if escaped:
                escaped = False
            elif c == '\\':
                escaped = True
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        at += 1
    return source[match.start():at]


# 桩与参考实现：epd_draw_pixel 按 epdiy 真实语义（uint16_t 回绕旋转 + 物理边界 + 半字节读改写）。
# Stubs and reference: epd_draw_pixel mirrors epdiy's real semantics (uint16_t-wrapped rotation,
# physical bounds and nibble read-modify-write).
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *gray; int width, height; } reader_image_t;
enum EpdRotation { EPD_ROT_LANDSCAPE = 0, EPD_ROT_PORTRAIT = 1,
                   EPD_ROT_INVERTED_LANDSCAPE = 2, EPD_ROT_INVERTED_PORTRAIT = 3 };

enum { PW = 1216, PH = 684 };
static int g_rotation = EPD_ROT_INVERTED_PORTRAIT;
static uint8_t fb_ref[PW * PH / 2];
static uint8_t fb_fast[PW * PH / 2];

int epd_width(void) { return PW; }
int epd_height(void) { return PH; }
enum EpdRotation epd_get_rotation(void) { return (enum EpdRotation)g_rotation; }
void epd_set_rotation(enum EpdRotation r) { g_rotation = (int)r; }
int epd_rotated_display_width(void) {
    int w = epd_width();
    if (g_rotation == EPD_ROT_PORTRAIT || g_rotation == EPD_ROT_INVERTED_PORTRAIT) w = epd_height();
    return w;
}
int epd_rotated_display_height(void) {
    int h = epd_height();
    if (g_rotation == EPD_ROT_PORTRAIT || g_rotation == EPD_ROT_INVERTED_PORTRAIT) h = epd_width();
    return h;
}

// _rotate 与 epdiy 完全一致：参数是 uint16_t，负数先回绕；结果转回 int 再做物理边界检查。
// _rotate matches epdiy exactly: uint16_t arguments wrap negatives first; the result returns to int
// for the physical bounds check.
static void rotate_px(int lx, int ly, int *out_x, int *out_y) {
    uint16_t x = (uint16_t)lx, y = (uint16_t)ly;
    switch (g_rotation) {
        case EPD_ROT_LANDSCAPE:
            break;
        case EPD_ROT_PORTRAIT: {
            uint16_t t = x; x = y; y = t;
            x = (uint16_t)(epd_width() - x - 1);
            break;
        }
        case EPD_ROT_INVERTED_LANDSCAPE:
            x = (uint16_t)(epd_width() - x - 1);
            y = (uint16_t)(epd_height() - y - 1);
            break;
        case EPD_ROT_INVERTED_PORTRAIT: {
            uint16_t t = x; x = y; y = t;
            y = (uint16_t)(epd_height() - y - 1);
            break;
        }
    }
    *out_x = (int)x;
    *out_y = (int)y;
}

void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb) {
    int px, py;
    rotate_px(x, y, &px, &py);
    if (px < 0 || px >= epd_width()) return;
    if (py < 0 || py >= epd_height()) return;
    uint8_t *b = &fb[py * epd_width() / 2 + px / 2];
    if (px % 2) *b = (uint8_t)((*b & 0x0F) | (color & 0xF0));
    else *b = (uint8_t)((*b & 0xF0) | (color >> 4));
}

#include "ui_image_dither.h"
'''
unit += '\n' + function('inline_ink_gray')
unit += '\n' + function('reader_logical_to_phys')
unit += '\n' + function('inline_ink_dither_nibble')
unit += '\n' + function('blit_inline_gray')
unit += r'''
// 参考实现：被替换掉的逐像素循环，语义与 epdiy 真实路径一致。
// Reference: the replaced per-pixel loop, matching the real epdiy path.
static void ref_blit(const reader_image_t *image, int left, int top, uint8_t *fb) {
    for (int iy = 0; iy < image->height; ++iy)
        for (int ix = 0; ix < image->width; ++ix)
            epd_draw_pixel(left + ix, top + iy,
                ui_image_dither_gray(inline_ink_gray(image->gray[(size_t)iy * image->width + ix]),
                                     left + ix, top + iy), fb);
}

static uint32_t lcg(uint32_t *s) { *s = *s * 1103515245u + 12345u; return *s >> 16; }
static uint32_t g_state = 1;

static void fill_bytes(uint8_t *buf, size_t n, uint32_t *s) {
    for (size_t i = 0; i < n; ++i) buf[i] = (uint8_t)lcg(s);
}

static void run_case(int rotation, int iw, int ih, int left, int top, uint32_t seed) {
    epd_set_rotation((enum EpdRotation)rotation);
    reader_image_t image = {0};
    image.width = iw;
    image.height = ih;
    image.gray = (uint8_t *)malloc((size_t)iw * (size_t)ih);
    assert(image.gray);
    uint32_t s = seed;
    fill_bytes(image.gray, (size_t)iw * (size_t)ih, &s);
    s = seed ^ 0x9e3779b9u;
    fill_bytes(fb_ref, sizeof(fb_ref), &s);
    memcpy(fb_fast, fb_ref, sizeof(fb_ref));
    ref_blit(&image, left, top, fb_ref);
    blit_inline_gray(&image, left, top, fb_fast);
    assert(memcmp(fb_ref, fb_fast, sizeof(fb_ref)) == 0);
    free(image.gray);
}

int main(void) {
    // 内联抖动在全部 256 灰度 × 16 相位上与 ui_image_dither_gray 逐字节一致。
    // The inline dither matches ui_image_dither_gray byte-for-byte at all 256 grays x 16 phases.
    for (int g = 0; g < 256; ++g)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                assert(ui_image_dither_gray((uint8_t)g, x, y) ==
                       (uint8_t)(inline_ink_dither_nibble((uint8_t)g, x, y) * 17));

    // 空图 / 空指针安全返回，不写帧缓冲。
    // Empty or null images return safely without touching the framebuffer.
    blit_inline_gray(NULL, 0, 0, fb_ref);
    reader_image_t bad = {NULL, 5, 5};
    blit_inline_gray(&bad, 0, 0, fb_ref);
    bad = (reader_image_t){fb_ref, 0, 5};
    blit_inline_gray(&bad, 0, 0, fb_ref);
    bad = (reader_image_t){fb_ref, 5, 0};
    blit_inline_gray(&bad, 0, 0, fb_ref);

    static const int rotations[4] = { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT,
                                      EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
    for (int r = 0; r < 4; ++r) {
        int rot = rotations[r];
        // 系统化边界：单行/单列、奇偶起点（物理 pxmin 奇偶都覆盖）、裁剪与越界。
        // Systematic edges: single row/column, odd/even starts (both physical pxmin parities),
        // clipping and out-of-bounds.
        struct { int w, h, l, t; } cases[] = {
            {1, 1, 0, 0}, {1, 1, 100, 200}, {1, 5, 0, 0}, {5, 1, 0, 0},
            {1, 7, 3, 0}, {7, 1, 0, 3}, {2, 2, 0, 0}, {2, 2, 1, 1},
            {3, 3, 0, 0}, {3, 3, 1, 1}, {5, 5, 0, 0}, {5, 5, 1, 0},
            {5, 5, 0, 1}, {5, 5, 1, 1}, {2, 5, 0, 1}, {2, 5, 1, 0},
            {7, 9, -3, -5}, {7, 9, -2, -4}, {7, 9, -1, 0}, {7, 9, 0, -1},
            {3, 3, PW - 2, PH - 2}, {4, 4, PW - 1, PH - 1}, {4, 3, PW - 2, PH - 3},
            {PW, PH + 200, 0, 0}, {PW + 200, PH, 0, 0},
            {5, 5, PW - 3, 0}, {5, 5, 0, PH - 3}, {5, 5, PW - 4, 0}, {5, 5, 0, PH - 4},
            {7, 7, -2, PH - 2}, {7, 7, PW - 2, -2},
        };
        for (size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ++ci)
            run_case(rot, cases[ci].w, cases[ci].h, cases[ci].l, cases[ci].t,
                     1000u + (uint32_t)r * 100u + (uint32_t)ci);
        // 随机灰阶图 + 随机位置，覆盖旋转分支下的任意交集。
        // Random gray images and positions, covering arbitrary intersections per rotation.
        for (int t = 0; t < 60; ++t) {
            int w = 1 + (int)(lcg(&g_state) % 80);
            int h = 1 + (int)(lcg(&g_state) % 80);
            int l = (int)(lcg(&g_state) % 140) - 60;
            int tp = (int)(lcg(&g_state) % 140) - 60;
            run_case(rot, w, h, l, tp, 5000u + (uint32_t)r * 1000u + (uint32_t)t);
        }
    }
    puts("PASS: inline image blit matches per-pixel epd_draw_pixel byte-for-byte across rotations and edges");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
    c, binary = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-I' + str(root / 'main/ui'),
                    str(c), str(root / 'main/ui/ui_image_dither.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare S31 direct framebuffer writes with the generic glyph compositor."""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
font = root / "main/font/ttf_font.c"
source = font.read_text()
a = source.index("static bool s_reader_body_aa;")
b = source.index("#endif", a)
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 30, HEIGHT = 24, BYTES = WIDTH / 2 * HEIGHT };
enum { EPD_ROT_INVERTED_PORTRAIT, GENERIC_ROTATION };
typedef struct { const uint8_t *bitmap; int width, height, left, top; } glyph_entry_t;
static uint8_t s_cover[256];
static int rotation, checked_writes;
static int epd_width(void) { return WIDTH; }
static int epd_height(void) { return HEIGHT; }
static int epd_get_rotation(void) { return rotation; }
static void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb) {
    // Both test outlets use the same physical portrait mapping to compare exact writes.
    ++checked_writes;
    if (x < 0 || x >= HEIGHT || y < 0 || y >= WIDTH) return;
    uint8_t *out = fb + (HEIGHT - 1 - x) * (WIDTH / 2) + y / 2;
    if (y & 1) *out = (*out & 15) | color;
    else *out = (*out & 240) | (color >> 4);
}
''' + source[a:b] + '\n' + function("mix_ink_aa", font) + '\n' + function("blit_upright", font) + r'''
int main(void) {
    for (unsigned alpha = 0; alpha < 256; ++alpha) s_cover[alpha] = alpha;
    reader_gray_rebuild();
    for (int alpha = 0; alpha <= 255; ++alpha) {
        int ink = 0;
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
            const int gray = reader_gray_at(alpha, x, y);
            ink += 15 - gray;
            if (alpha == 0) assert(gray == 15);
            if (alpha == 255) assert(gray == 0);
            if (alpha) assert(reader_gray_at(alpha - 1, x, y) >= gray);
        }
        assert(abs(ink * 255 - alpha * 60) <= 128);
    }
    assert(reader_gray_at(6, 0, 1) < 15);
    uint8_t bitmap[63], expected[BYTES], direct[BYTES];
    for (unsigned i = 0; i < sizeof(bitmap); ++i) bitmap[i] = (uint8_t)(i * 37);
    glyph_entry_t g = {.bitmap = bitmap, .width = 7, .height = 9, .left = -2, .top = 5};
    ttf_set_reader_body_aa(true);
    for (int x = -9; x <= HEIGHT + 8; ++x) for (int y = -8; y <= WIDTH + 10; ++y) {
        memset(expected, 0xdc, BYTES); memset(direct, 0xdc, BYTES);
        rotation = GENERIC_ROTATION;
        blit_upright(expected, x, y, &g, 0, 15, true);
        const int writes = checked_writes;
        rotation = EPD_ROT_INVERTED_PORTRAIT;
        blit_upright(direct, x, y, &g, 0, 15, true);
        assert(checked_writes == writes && !memcmp(expected, direct, BYTES));
    }
    memset(expected, 255, BYTES); memset(direct, 255, BYTES);
    blit_upright(expected, 3, 8, &g, 0, 15, false);
    ttf_set_reader_body_aa(false);
    blit_upright(direct, 3, 8, &g, 0, 15, false);
    assert(!memcmp(expected, direct, BYTES));
    puts("PASS: balanced tone and solid endpoints, clipped direct portrait raster equals generic AA, AA off unchanged");
}
'''
with tempfile.TemporaryDirectory() as directory:
    c = Path(directory) / "s31-reader-raster.c"
    executable = c.with_suffix("")
    c.write_text(unit)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                    "-DKIIKO_BOARD_METALIO47=1", "-DESP_PLATFORM",
                    "-fsanitize=address,undefined", str(c), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

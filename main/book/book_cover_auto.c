/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：无封面书籍使用深底、白线框和横排书名。
 * English: Coverless books use a dark field, fine white border and horizontal title.
 */
#include "book_cover_auto.h"

#include <stdlib.h>
#include <string.h>
#include "boot_state.h"
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_font.h"

typedef struct {
    uint8_t *fb;
    unsigned width, height, stride;
} cover_canvas_t;

static bool physical_pixel(int x, int y, int *physical_x, int *physical_y) {
    int pw = epd_width(), ph = epd_height();
    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE: *physical_x = x; *physical_y = y; break;
        case EPD_ROT_PORTRAIT: *physical_x = pw - y - 1; *physical_y = x; break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            *physical_x = pw - x - 1; *physical_y = ph - y - 1; break;
        default: *physical_x = y; *physical_y = ph - x - 1; break;
    }
    return *physical_x >= 0 && *physical_y >= 0 && *physical_x < pw && *physical_y < ph;
}

static uint8_t get(const cover_canvas_t *c, int x, int y) {
    int px, py;
    if (!physical_pixel(x, y, &px, &py)) return 15;
    uint8_t value = c->fb[(size_t)py * c->stride + (unsigned)px / 2];
    return px & 1 ? value >> 4 : value & 15;
}


enum { COVER_TEXT_CAP = 256, COVER_TITLE_LINES = 3 };

static const char *cover_font_path(void) {
    const char *system = app_settings_system_font_path();
    const char *reading = app_settings_font_path();
    return !ttf_font_path_is_builtin(system) ? system : reading;
}

uint64_t book_auto_cover_font_key(void) {
    const char *path = cover_font_path();
    if (ttf_font_path_is_builtin(path) || !pico_boot_asset_allowed(path)) return 0;
    uint64_t key = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        key = (key ^ *p) * UINT64_C(1099511628211);
    return key;
}

typedef struct {
    cover_canvas_t canvas;
    const char *title, *author;
    bool imported;
} cover_draw_t;

static const char *next_glyph(const char *text, char glyph[5]) {
    size_t n = 1;
    while (n < 4 && text[n] && ((unsigned char)text[n] & 0xc0) == 0x80) ++n;
    memcpy(glyph, text, n);
    glyph[n] = 0;
    return text + n;
}

static bool cover_glyph_uses_ttf(const cover_draw_t *cover, const char *glyph) {
    const int slot = ttf_font_selected();
    return ttf_font_slot_ready(slot) &&
        ((cover->imported && ttf_font_slot_has_text(slot, glyph)) || !ui_font_has_text(glyph));
}

static int glyph_width(const cover_draw_t *cover, int px, const char *glyph) {
    return cover_glyph_uses_ttf(cover, glyph)
        ? ttf_text_width_px(px, glyph) : ui_font_text_width_px(px, glyph);
}

static void draw_line(const cover_draw_t *cover, const char *text, int center_y, int px) {
    int width = 0, above = 0, below = 0;
    for (const char *at = text; *at;) {
        char glyph[5]; at = next_glyph(at, glyph);
        int a = 0, b = 0;
        if (cover_glyph_uses_ttf(cover, glyph)) ttf_measure_line_px(px, glyph, &a, &b);
        else ui_font_measure_line_px(px, glyph, &a, &b);
        if (a > above) above = a;
        if (b > below) below = b;
        width += glyph_width(cover, px, glyph);
    }
    int x = ((int)cover->canvas.width - width) / 2;
    const int baseline = center_y + (above - below) / 2;
    while (*text) {
        char glyph[5]; text = next_glyph(text, glyph);
        if (cover_glyph_uses_ttf(cover, glyph))
            ttf_draw_text_px(cover->canvas.fb, x, baseline, px, glyph, EPD_DRAW_ALIGN_LEFT, 0, 15);
        else
            ui_font_draw_text_px(cover->canvas.fb, x, baseline, px, glyph, EPD_DRAW_ALIGN_LEFT, 0, 15, false);
        x += glyph_width(cover, px, glyph);
    }
}

// 长标题按完整字符折行；最后一行预留省略号。/ Wrap complete characters, reserving space for an ellipsis on the last line.
static int wrap_title(const cover_draw_t *cover, const char *text, int px, int max_width,
                      char lines[][COVER_TEXT_CAP], int max_lines) {
    int line = 0;
    while (*text && line < max_lines) {
        char *out = lines[line++];
        size_t used = 0;
        int width = 0;
        while (*text) {
            char glyph[5];
            const char *next = next_glyph(text, glyph);
            const int space = glyph_width(cover, px, glyph);
            const bool last = line == max_lines && *next;
            const int reserve = last ? glyph_width(cover, px, "…") : 0;
            if (used + (size_t)(next - text) + sizeof("…") > COVER_TEXT_CAP ||
                (used && width + space + reserve > max_width)) break;
            memcpy(out + used, text, (size_t)(next - text));
            used += (size_t)(next - text);
            width += space;
            text = next;
        }
        out[used] = 0;
        if (*text && line == max_lines) memcpy(out + used, "…", sizeof("…"));
    }
    return line;
}

static bool draw_cover_text(void *context) {
    const cover_draw_t *cover = context;
    const int width = (int)cover->canvas.width, height = (int)cover->canvas.height;
    const int short_side = width < height ? width : height;
    int title_px = short_side * 13 / 100;
    // 短标题优先单行，长标题保留足够字号后折行。/ Keep short titles on one line without shrinking long titles past the minimum.
    int width_at_size = 0;
    for (const char *at = cover->title; *at;) {
        char glyph[5]; at = next_glyph(at, glyph);
        width_at_size += glyph_width(cover, title_px, glyph);
    }
    const int line_width = width * 69 / 100;
    if (width_at_size > line_width) {
        const int fitted = title_px * line_width / width_at_size;
        if (fitted >= short_side / 10) title_px = fitted;
    }
    const int author_px = short_side * 8 / 100 < 10 ? 10 : short_side * 8 / 100;
    const int line_step = title_px + short_side / 35;
    char lines[COVER_TITLE_LINES][COVER_TEXT_CAP];
    const int count = wrap_title(cover, cover->title, title_px, line_width, lines, COVER_TITLE_LINES);
    const bool has_author = cover->author && *cover->author;
    const int center_y = height * 53 / 100;
    const int author_offset = has_author ? (title_px + author_px) / 2 + height / 45 : 0;
    const int first_y = center_y - ((count - 1) * line_step + author_offset) / 2;
    const bool antialias = ttf_get_antialias();
    ttf_set_antialias(true);
    for (int i = 0; i < count; ++i) draw_line(cover, lines[i], first_y + i * line_step, title_px);
    if (has_author) {
        wrap_title(cover, cover->author, author_px, line_width, lines, 1);
        draw_line(cover, lines[0], first_y + (count - 1) * line_step + author_offset, author_px);
    }
    ttf_set_antialias(antialias);
    return true;
}

bool book_auto_cover_render(const char *title, const char *author,
                            unsigned width, unsigned height, uint8_t *out) {
    if (!out || !title || !*title || width < 96 || height < 128 ||
        width > 320 || height > 440 || width >= (unsigned)epd_rotated_display_width() ||
        height > (unsigned)epd_rotated_display_height()) return false;
    const unsigned stride = (unsigned)epd_width() / 2;
    const size_t bytes = (size_t)stride * epd_height();
    uint8_t *scratch = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scratch) return false;
    memset(scratch, 0xff, bytes);
    cover_draw_t cover = {{scratch, width, height, stride}, title, author, true};
    const char *path = cover_font_path();
    bool drawn = false;
    if (!ttf_font_path_is_builtin(path) && pico_boot_asset_allowed(path) &&
        (strncmp(path, "/sdcard/", 8) || read_pico_sd_io_ready()))
        drawn = ttf_font_draw_with_path(path, draw_cover_text, &cover);
    if (!drawn) {
        cover.imported = false;
        if (!*ttf_font_path() || !ttf_font_draw_with_path(ttf_font_path(), draw_cover_text, &cover))
            draw_cover_text(&cover);
    }

    const unsigned mx = width * 9 / 100, my = height * 11 / 100;
    const unsigned stroke = width < 150 ? 1 : 2;
    const unsigned right = width - mx, bottom = height - my;
    const unsigned background = 51;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const bool frame = x >= mx && x < right && y >= my && y < bottom &&
                (x < mx + stroke || x >= right - stroke || y < my + stroke || y >= bottom - stroke);
            const unsigned ink = 15 - get(&cover.canvas, (int)x, (int)y);
            out[(size_t)y * width + x] = frame ? 255 : (uint8_t)(background + ink * (255 - background) / 15);
        }
    free(scratch);
    return true;
}

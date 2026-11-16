/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover_auto.h"
#include "epdiy.h"

static int glyph_count(const char *text) {
    int count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if ((*p & 0xc0) != 0x80) ++count;
    return count;
}
static const char *system_font = "", *reading_font = "", *expected_font;
static bool external_face, storage_ready = true, antialias;
static unsigned ttf_draws, builtin_draws, borrowed;
const char *app_settings_system_font_path(void) { return system_font; }
const char *app_settings_font_path(void) { return reading_font; }
const char *ttf_font_path(void) { return "builtin"; }
bool ttf_font_path_is_builtin(const char *path) { return !path || !*path || !strcmp(path, "builtin"); }
bool pico_boot_asset_allowed(const char *path) { return path && strcmp(path, "/sdcard/blocked.ttf"); }
bool read_pico_sd_io_ready(void) { return storage_ready; }
bool ttf_font_draw_with_path(const char *path, bool (*draw)(void *), void *context) {
    external_face = !ttf_font_path_is_builtin(path);
    if (external_face) {
        assert(expected_font && !strcmp(path, expected_font));
        ++borrowed;
    }
    bool result = draw(context);
    external_face = false;
    return result;
}
int ttf_font_selected(void) { return external_face ? 1 : 0; }
bool ttf_font_slot_ready(int slot) { return slot == 0 || external_face; }
bool ttf_font_slot_has_text(int slot, const char *text) {
    return slot == 1 && strcmp(text, "缺");
}
bool ttf_get_antialias(void) { return antialias; }
void ttf_set_antialias(bool enabled) { antialias = enabled; }
bool ui_font_has_text(const char *text) { (void)text; return true; }
int ui_font_text_width_px(int px, const char *text) { return glyph_count(text) * px; }
int ttf_text_width_px(int px, const char *text) { return glyph_count(text) * (px - 2); }
void ui_font_measure_line_px(int px, const char *text, int *a, int *b) {
    (void)text; *a = px - 2; *b = 0;
}
void ttf_measure_line_px(int px, const char *text, int *a, int *b) {
    ui_font_measure_line_px(px, text, a, b);
}
static void glyph_rect(uint8_t *fb, int x, int baseline, int px, const char *text,
                       enum EpdFontFlags align, uint8_t fg) {
    int width = ui_font_text_width_px(px, text);
    if (align == EPD_DRAW_ALIGN_CENTER) x -= width / 2;
    else if (align == EPD_DRAW_ALIGN_RIGHT) x -= width;
    for (int g = 0; g < glyph_count(text); ++g)
        for (int y = baseline - px; y < baseline - 2; ++y)
            for (int dx = 0; dx < px - 3; ++dx) {
                int xx = x + g * px + dx;
                if (xx < 0 || xx >= epd_rotated_display_width() ||
                    y < 0 || y >= epd_rotated_display_height()) continue;
                int physical_x = y, physical_y = epd_height() - xx - 1;
                uint8_t *at = fb + (size_t)physical_y * epd_width() / 2 + physical_x / 2;
                if (physical_x & 1) *at = (*at & 0x0f) | (fg << 4);
                else *at = (*at & 0xf0) | fg;
            }
}
void ui_font_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                          enum EpdFontFlags align, uint8_t fg, uint8_t bg, bool black_white) {
    (void)bg; (void)black_white; ++builtin_draws;
    glyph_rect(fb, x, baseline, px, text, align, fg);
}
void ttf_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)bg; ++ttf_draws;
    glyph_rect(fb, x, baseline, px - 2, text, align, fg);
}

enum { W = 176, H = 240, N = W * H };
static void render_checked(const char *title, const char *author,
                           uint8_t out[N]) {
    uint8_t *guard = malloc(N + 32);
    assert(guard);
    memset(guard, 0xa5, N + 32);
    assert(book_auto_cover_render(title, author, W, H, guard + 16));
    for (int i = 0; i < 16; ++i) assert(guard[i] == 0xa5 && guard[N + 16 + i] == 0xa5);
    memcpy(out, guard + 16, N);
    free(guard);
}
int main(void) {
    uint8_t *a = malloc(N), *b = malloc(N);
    assert(a && b);
    const char *title = "重生之女将星", *author = "千山茶客";
    assert(!book_auto_cover_font_key());
    render_checked(title, author, a);
    render_checked(title, author, b);
    assert(!memcmp(a, b, N) && !ttf_draws && builtin_draws && !antialias);
    const int mx = W * 9 / 100, my = H * 11 / 100;
    assert(a[0] == 51 && a[N - 1] == 51);
    for (int x = mx; x < W - mx; ++x) {
        assert(a[my * W + x] == 255);
        assert(a[(H - my - 1) * W + x] == 255);
    }
    int title_ink = 0, author_ink = 0;
    for (int y = my + 2; y < H - my - 2; ++y)
        for (int x = mx + 2; x < W - mx - 2; ++x) {
            assert(a[y * W + x] == 51 || (y > 90 && y < 160));
            if (a[y * W + x] > 51) {
                title_ink += y < 131;
                author_ink += y >= 131;
            }
        }
    assert(title_ink > author_ink && author_ink > 100);
    expected_font = reading_font = "/sdcard/reading.ttf";
    const uint64_t reading_key = book_auto_cover_font_key();
    assert(reading_key);
    render_checked(title, author, b);
    assert(borrowed == 1 && ttf_draws && memcmp(a, b, N) && !external_face);
    expected_font = system_font = "/sdcard/system.ttf";
    assert(book_auto_cover_font_key() && book_auto_cover_font_key() != reading_key);
    builtin_draws = ttf_draws = 0;
    render_checked("测试缺字", author, b);
    assert(borrowed == 2 && builtin_draws == 1 && ttf_draws > 1);
    storage_ready = false;
    render_checked(title, author, b);
    assert(borrowed == 2 && !memcmp(a, b, N));
    storage_ready = true;
    system_font = "/sdcard/blocked.ttf";
    assert(!book_auto_cover_font_key());
    render_checked(title, author, b);
    assert(borrowed == 2 && !memcmp(a, b, N));
    render_checked("夏天、烟火和我的尸体：一个很长很长的中文书名", "三天两觉", a);
    render_checked("The Extremely Long English Title That Must Wrap Cleanly Without Escaping Its Frame", "", b);
    assert(memcmp(a, b, N));
    assert(!book_auto_cover_render("", "", W, H, b));
    assert(!book_auto_cover_render("title", "", 8, H, b));
    free(a); free(b);
    puts("PASS: only framed dark layout; imported system/reading priority, missing glyph fallback and no unavailable-card load");
    return 0;
}

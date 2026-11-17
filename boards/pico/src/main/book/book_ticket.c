/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：锁屏票根从持久进度与真实阅读计数生成；没有可靠时钟时不编造今日数据。
 * 票面上下以裁纸缺口绘制，最近阅读的封面作为灰阶背景。
 * English: Build the ticket from persisted progress and measured reading data; omit daily data without a valid clock.
 * Scalloped paper edges sit over a grayscale wallpaper made from the latest book cover.
 * 用户修订：锁屏入口统一系统字体，截短测量须与实际界面字体及字号一致。
 * User revision: lock entry selects the system face; truncation must measure the actual UI face and size.
 */
#include "book_ticket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include "book_cover.h"
#include "book_weread_cover.h"
#include "book_epub.h"
#include "book_progress.h"
#include "book_title.h"
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "read_pico_pmu.h"
#include "ttf_font.h"
#include "ui_kit.h"

#include "book/book_ticket_data.inc"

static void text_fit(char *text, int px, int width) {
    while (*text && ui_text_fixed_width_px(ui_text_effective_px(px), text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}
static uint8_t *cover_load(const char *path, const char *title, const char *author) {
    uint8_t *gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gray) return NULL;
    if (book_weread_cover_is_txt(path)) {
        if (book_weread_cover_read(path, gray, BOOK_COVER_W, BOOK_COVER_H)) {
            return gray;
        }
        free(gray);
        return NULL;
    }
    // Locks only reuse available EPUB thumbnails to avoid decoding on the power-key path.
    if (book_cover_load_gray(path, title, author, gray, false, NULL)) return gray;
    free(gray);
    return NULL;
}
static void cover_background_draw(uint8_t *fb, const uint8_t *gray) {
    if (!gray) {
        ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}, 0xb0, fb);
        return;
    }
    int crop_x = 0, crop_y = 0, crop_w = BOOK_COVER_W, crop_h = BOOK_COVER_H;
    if ((int64_t)BOOK_COVER_W * UI_LOCK_HEIGHT > (int64_t)BOOK_COVER_H * UI_LOCK_WIDTH) {
        crop_w = BOOK_COVER_H * UI_LOCK_WIDTH / UI_LOCK_HEIGHT;
        crop_x = (BOOK_COVER_W - crop_w) / 2;
    } else {
        crop_h = BOOK_COVER_W * UI_LOCK_HEIGHT / UI_LOCK_WIDTH;
        crop_y = (BOOK_COVER_H - crop_h) / 2;
    }
    for (int y = 0; y < UI_LOCK_HEIGHT; ++y) {
        const int sy = crop_y + y * crop_h / UI_LOCK_HEIGHT;
        for (int x = 0; x < UI_LOCK_WIDTH; ++x) {
            const int sx = crop_x + x * crop_w / UI_LOCK_WIDTH;
            const uint8_t tone = ui_contrast_gray(gray[sy * BOOK_COVER_W + sx]);
            // 对比度输出已经是面板4bit灰阶；再次按255/15量化会把纯白变成灰点。
            // Contrast already yields panel 4-bit levels; quantizing again by 255/15 would dither solid white into gray.
            epd_draw_pixel(x, y, tone, fb);
        }
    }
}
static int notch_depth(int x) {
    const int radius = 18, first = 80, step = 64;
    int dx = (x - first) % step;
    if (dx < 0) dx += step;
    if (dx > step / 2) dx -= step;
    if (dx < -radius || dx > radius) return 0;
    return radius - dx * dx / radius;
}
static void ticket_paper_draw(uint8_t *fb) {
    const int left = 48, right = 636, top = 58, bottom = 1158;
    for (int x = left; x < right; ++x) {
        int cut = notch_depth(x);
        int y0 = top + cut, y1 = bottom - cut;
        ui_fill_rect_fast((EpdRect){x, y0, 1, y1 - y0}, UI_GRAY_WHITE, fb);
        epd_draw_pixel(x, y0, UI_GRAY_BLACK, fb);
        epd_draw_pixel(x, y1 - 1, UI_GRAY_BLACK, fb);
    }
    ui_fill_rect_fast((EpdRect){left, top + 20, 1, bottom - top - 40}, UI_GRAY_BLACK, fb);
    ui_fill_rect_fast((EpdRect){right - 1, top + 20, 1, bottom - top - 40}, UI_GRAY_BLACK, fb);
}
static void ticket_shadow_draw(uint8_t *fb) {
    const int left = 48, right = 636, top = 58, bottom = 1158;
    for (int x = left; x < right; ++x) {
        int cut = notch_depth(x);
        int y0 = top + cut + 8, y1 = bottom - cut + 8;
        ui_fill_rect_fast((EpdRect){x + 5, y0, 1, y1 - y0}, 0x80, fb);
    }
}
static void cover_draw(uint8_t *fb, const uint8_t *gray, const char *title) {
    EpdRect box = {216, 378, 252, 344};
    ui_fill_rect_fast(box, UI_GRAY_LIGHT, fb);
    if (gray) {
        for (int y = 0; y < box.height; ++y)
            for (int x = 0; x < box.width; ++x) {
                int sx = x * BOOK_COVER_W / box.width;
                int sy = y * BOOK_COVER_H / box.height;
                const uint8_t tone = ui_contrast_gray(gray[sy * BOOK_COVER_W + sx]);
                epd_draw_pixel(box.x + x, box.y + y,
                               tone, fb);
            }
    } else {
        char short_title[96]; snprintf(short_title, sizeof(short_title), "%s", title);
        text_fit(short_title, 32, box.width - 20);
        ui_text_vc(fb, box.x + box.width / 2, box.y + box.height / 2, 32,
                   short_title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, box, 0, UI_GRAY_BLACK);
}
// Preserve exact counters, shrinking only when needed. Two lines fit long statistics.
static void ticket_stat(uint8_t *fb, int y, int height, int px, const char *text) {
    px = ui_text_effective_px(px);
    if (px > height) px = height;
    char line[128]; snprintf(line, sizeof(line), "%s", text);
    while (px > 18 && ui_text_fixed_width_px(px, line) > 520) --px;
    if (ui_text_fixed_width_px(px, line) <= 520) {
        ui_text_fixed(fb, 82, y, px, line, EPD_DRAW_ALIGN_LEFT, false);
        return;
    }
    char *sep = strstr(line, "  ·  ");
    if (sep && height >= 44) {
        *sep = 0; char *tail = sep + strlen("  ·  ");
        text_fit(line, 18, 520); text_fit(tail, 18, 520);
        ui_text_fixed(fb, 82, y, 18, line, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 82, y + height / 2, 18, tail, EPD_DRAW_ALIGN_LEFT, false);
    } else {
        text_fit(line, px, 520);
        ui_text_fixed(fb, 82, y, px, line, EPD_DRAW_ALIGN_LEFT, false);
    }
}
static void ticket_heading(uint8_t *fb,int top,int height,int px,char *text) {
    // 锁屏入口已切系统字体；标题按原生轮廓输出，独立强制AA，测量与绘制用同一字库。
    // Lock entry selects the system face; rasterize native outlines with explicit AA and measure using the same face.
    px=ui_text_effective_px(px);if(px>height-2)px=height-2;
    px=ui_text_title_fit(text,px,520,text);
    ui_text_title_vc(fb,82,top+height/2,px,text,text,EPD_DRAW_ALIGN_LEFT);
}
bool book_ticket_draw(uint8_t *fb, bool reader_background) {
    char path[288];
    if (reader_background && s_position.magic == TICKET_POSITION_MAGIC) strcpy(path, s_position.path);
    else if (!book_progress_last_path(path, sizeof(path))) return false;
    struct stat st;
    bool file_valid = stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
                      (uint64_t)st.st_size <= UINT32_MAX;
    book_progress_t progress = {0};
    const bool saved = file_valid && book_progress_load(path, st.st_size, &progress);
    char chapter[128] = {0};
    const bool progress_valid = file_valid &&
        position_for_ticket(path, st.st_size, reader_background, saved, &progress, chapter);
    char title[256] = {0}, author[128] = {0};
    if (!book_display_name_from_path(path, title, sizeof(title), author, sizeof(author)))
        snprintf(title, sizeof(title), "未命名");
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) {
        char meta_title[256] = {0},meta_author[128]={0};
        if(book_epub_metadata_cached(path,meta_title,sizeof(meta_title),meta_author,sizeof(meta_author))==ESP_OK&&meta_author[0]) {
            bool chinese=book_author_clean_display(meta_author,sizeof(meta_author));
            if(meta_author[0]&&(chinese||!book_author_clean_display(author,sizeof(author))))
                snprintf(author,sizeof(author),"%s",meta_author);
        }
    }
    ticket_stats_t stats; stats_load(&stats);
    uint32_t day = local_day();
    uint8_t *gray = cover_load(path, title, author);
    cover_background_draw(fb, gray);
    ticket_shadow_draw(fb);
    ticket_paper_draw(fb);
    ui_text(fb, 82, 98, 28, "kiikoread  /  阅读票根", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 156, 82, 520, UI_GRAY_BLACK);
    ticket_heading(fb,202,64,50,title);
    if (author[0]) ticket_heading(fb,274,44,28,author);
    cover_draw(fb, gray, title);
    free(gray);
    ui_hairline(fb, 768, 82, 520, UI_GRAY_BLACK);
    char line[200];
    if (progress_valid && chapter[0])
        snprintf(line, sizeof(line), "已读 %u%%  ·  %s", progress.pct, chapter);
    else if (progress_valid)
        snprintf(line, sizeof(line), "已读 %u%%", progress.pct);
    else
        snprintf(line, sizeof(line), "已读 —");
    ticket_stat(fb, 802, 54, 34, line);
    EpdRect track = {82, 868, 520, 10};
    ui_fill_rect_fast(track, UI_GRAY_LIGHT, fb);
    track.width = progress_valid ? track.width * progress.pct / 100 : 0;
    if (track.width) ui_fill_rect_fast(track, UI_GRAY_BLACK, fb);
    char elapsed[64];
    if (day) {
        const ticket_day_t today = stats_day(&stats, day);
        book_ticket_format_time(elapsed, sizeof(elapsed), today.milliseconds);
        snprintf(line, sizeof(line), "今日阅读 %s  ·  翻页 %u 次", elapsed, (unsigned)today.turns);
    } else {
        snprintf(line, sizeof(line), "今日阅读 —  ·  日期未同步");
    }
    ticket_stat(fb, 914, 48, 28, line);
    book_ticket_format_time(elapsed, sizeof(elapsed), stats.total_ms);
    snprintf(line, sizeof(line), "累计阅读 %s  ·  翻页 %u 次", elapsed, (unsigned)stats.total_turns);
    ticket_stat(fb, 969, 74, 28, line);
    ui_hairline(fb, 1050, 82, 520, UI_GRAY_BLACK);
    ui_text(fb, 82, 1080, 26, "在每一页里，留下时间的凭证", EPD_DRAW_ALIGN_LEFT, false);
    return true;
}

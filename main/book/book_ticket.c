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
static uint8_t *cover_load(const char *path, const char *title, const char *author,
                           unsigned *width, unsigned *height) {
    uint8_t *gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gray) return NULL;
    if (book_weread_cover_is_txt(path)) {
        if (book_weread_cover_read(path, gray, BOOK_COVER_W, BOOK_COVER_H)) {
            *width = BOOK_COVER_W; *height = BOOK_COVER_H;
            return gray;
        }
        free(gray);
        return NULL;
    }
    // Locks only reuse available EPUB thumbnails to avoid decoding on the power-key path.
    // Natural/list covers retain the complete artwork. Older shelf caches may
    // have been prepared before the author metadata became available.
    for (unsigned pass = 0; pass < 2; ++pass) {
        const char *cached_author = pass ? "" : author;
        if (pass && (!author || !*author)) break;
        if (book_cover_load_list_gray(path, title, cached_author, gray, false, NULL, width, height))
            return gray;
    }
    for (unsigned pass = 0; pass < 2; ++pass) {
        const char *cached_author = pass ? "" : author;
        if (pass && (!author || !*author)) break;
        if (book_cover_load_gray(path, title, cached_author, gray, false, NULL)) {
            *width = BOOK_COVER_W; *height = BOOK_COVER_H;
            return gray;
        }
    }
    free(gray);
    return NULL;
}
static void cover_background_draw(uint8_t *fb, const uint8_t *gray, unsigned width, unsigned height) {
    if (!gray) {
        ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}, 0xb0, fb);
        return;
    }
    book_crop_t crop = book_cover_crop(width, height, UI_LOCK_WIDTH, UI_LOCK_HEIGHT);
    for (int y = 0; y < UI_LOCK_HEIGHT; ++y) {
        const int sy = crop.y + (uint64_t)y * crop.height / UI_LOCK_HEIGHT;
        for (int x = 0; x < UI_LOCK_WIDTH; ++x) {
            const int sx = crop.x + (uint64_t)x * crop.width / UI_LOCK_WIDTH;
            const uint8_t tone = ui_contrast_gray(gray[sy * width + sx]);
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
static void cover_draw(uint8_t *fb, const uint8_t *gray, unsigned width, unsigned height, const char *title) {
    EpdRect box = {216, 378, 252, 344};
    if (gray) {
        // Fit the original aspect ratio instead of stretching a narrow book cover.
        int w = box.width, h = (uint64_t)height * w / width;
        if (h > box.height) { h = box.height; w = (uint64_t)width * h / height; }
        box.x += (box.width - w) / 2; box.y += (box.height - h) / 2;
        box.width = w; box.height = h;
        for (int y = 0; y < box.height; ++y)
            for (int x = 0; x < box.width; ++x) {
                int sx = x * width / box.width;
                int sy = y * height / box.height;
                const uint8_t tone = ui_contrast_gray(gray[sy * width + sx]);
                epd_draw_pixel(box.x + x, box.y + y,
                               tone, fb);
            }
    } else {
        ui_fill_rect_fast(box, UI_GRAY_LIGHT, fb);
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
static size_t heading_prefix(const char *text, int px, const char *sample, char line[256]) {
    size_t used = 0;
    while (text[used]) {
        size_t next = used + 1;
        while (text[next] && ((unsigned char)text[next] & 0xc0) == 0x80) ++next;
        if (next >= 256) break;
        memcpy(line, text, next); line[next] = 0;
        if (ui_text_title_width_px(px, line, sample) > 520) break;
        used = next;
    }
    memcpy(line, text, used); line[used] = 0;
    return used;
}
static int ticket_heading(uint8_t *fb,int top,int height,int px,const char *text) {
    // Keep the source title intact for the cover and cache keys. Match the exact
    // drawing face, shrink first, then use the reserved second title line.
    const int single_height = height > 64 ? 64 : height;
    px=ui_text_effective_px(px);if(px>single_height-2)px=single_height-2;
    // Preserve native-size supplemental Han titles; imported outlines retain their preferred size.
    px=ui_text_title_fit(NULL,px,520,text);
    const int minimum = height >= 96 ? 28 : 18;
    while(px>minimum && ui_text_title_width_px(px,text,text)>520)--px;
    if(ui_text_title_width_px(px,text,text)<=520) {
        ui_text_title_vc(fb,82,top+single_height/2,px,text,text,EPD_DRAW_ALIGN_LEFT);
        return 1;
    }
    char first[256],last[256]; size_t split=0;
    if(height>=96) {
        for(;px>=18;--px) {
            split=heading_prefix(text,px,text,first);
            if(split && ui_text_title_width_px(px,text+split,text)<=520)break;
        }
        if(px<18)px=18;
        split=heading_prefix(text,px,text,first);
        snprintf(last,sizeof(last),"%s",text+split);
    } else snprintf(last,sizeof(last),"%s",text);
    // Extremely long names retain an explicit ellipsis rather than silently losing characters.
    if(ui_text_title_width_px(px,last,text)>520) {
        while(*last) {
            size_t n=strlen(last)-1;
            while(n && ((unsigned char)last[n]&0xc0)==0x80)--n;
            last[n]=0;
            char candidate[260];snprintf(candidate,sizeof(candidate),"%s…",last);
            if(strlen(candidate)<sizeof(last) && ui_text_title_width_px(px,candidate,text)<=520) {
                memcpy(last,candidate,strlen(candidate)+1);break;
            }
        }
    }
    if(height>=96) {
        ui_text_title_vc(fb,82,top+height/4,px,first,text,EPD_DRAW_ALIGN_LEFT);
        ui_text_title_vc(fb,82,top+height*3/4,px,last,text,EPD_DRAW_ALIGN_LEFT);
        return 2;
    }
    ui_text_title_vc(fb,82,top+height/2,px,last,text,EPD_DRAW_ALIGN_LEFT);
    return 1;
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
    unsigned cover_width=0,cover_height=0;
    uint8_t *gray = cover_load(path, title, author, &cover_width, &cover_height);
    cover_background_draw(fb, gray, cover_width, cover_height);
    ticket_shadow_draw(fb);
    ticket_paper_draw(fb);
    ui_text(fb, 82, 98, 28, "kiikoread  /  阅读票根", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 156, 82, 520, UI_GRAY_BLACK);
    int lines=ticket_heading(fb,202,112,50,title);
    if (author[0]) ticket_heading(fb,lines>1?326:274,44,28,author);
    cover_draw(fb, gray, cover_width, cover_height, title);
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

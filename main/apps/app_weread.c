/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 微读传书：扫码、书架、含插图的离线下载。后台服务独占网络与文件写入。
 * WeRead transfer: QR login, shelf and illustrated offline downloads; worker owns writes.
 * 冻结：绘制无副作用；退出、锁屏、失卡前取消并等待；不修改云端阅读进度。
 * Frozen: pure render; cancel and join before exit/lock/media loss; no cloud-progress writes.
 */
#include "app.h"
#include "app_content_open.h"
#include "app_transfer_mode.h"
#include "book_store.h"
#include "display.h"
#include "read_pico_sd.h"
#include "ttf_font.h"
#include "weread_service.h"
#include "weread_notes.h"
#include "book_progress.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include "ui_menu.h"
#include "ui_gesture.h"
#include "ui_wifi_qr.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <limits.h>

static weread_snapshot_t *s_view_storage;
#define s_view (*s_view_storage)
static bool s_ready, s_qr_ok, s_images = true, s_logout_confirm;
static int s_selected = -1;
static unsigned s_changed;
static bool s_multiselect, s_batch_panel;
static weread_selection_t *s_selection;
static unsigned s_selection_count, s_selection_capacity;
static char s_message[96];
static int64_t s_next_tick;
static char s_qr[320];
// 划线想法浏览：视图开关、当前分类（划线/想法）、页码、任务完成后自动着陆。
// / Thoughts browser: view flag, active tab, page, and auto-landing after the fetch task.
static bool s_thoughts_view, s_thoughts_pending;
static unsigned s_thoughts_kind, s_thoughts_page;
// 整本拉取的状态行节流：条数每页都在涨，30 秒重绘一次进度行。
// / Throttle for the whole-book fetch status line: counts tick per page,
// / redraw the progress line every 30 s.
static int64_t s_notes_redraw_tick;
static unsigned s_notes_seen_done = UINT_MAX, s_notes_seen_total;
// 整本拉取完成确认：任务启动置 pending，结束后消费并弹一次性卡片。
// / Whole-book fetch confirm card: pending set at start, consumed on end.
static bool s_notes_toast, s_notes_toast_pending;
static EpdRect sync_rect(void) { return (EpdRect){36, 293, 260, 64}; }
static EpdRect logout_rect(void) { return (EpdRect){468, 293, 180, 64}; }
static EpdRect select_rect(void) { return (EpdRect){316, 293, 132, 64}; }
static EpdRect batch_rect(void) { return (EpdRect){220, 1003, 244, 66}; }
static EpdRect cancel_rect(void) { return (EpdRect){36, 740, 612, 78}; }
static EpdRect row_rect(int i) { return (EpdRect){36, 384 + i * 84, 612, 84}; }
static EpdRect prev_rect(void) { return (EpdRect){36, 1003, 164, 66}; }
static EpdRect next_rect(void) { return (EpdRect){484, 1003, 164, 66}; }
static EpdRect image_rect(void) { return (EpdRect){36, 456, 612, 84}; }
static EpdRect download_rect(void) { return (EpdRect){36, 570, 612, 78}; }
static EpdRect thoughts_rect(void) { return (EpdRect){36, 668, 612, 78}; }
static EpdRect redownload_rect(void) { return (EpdRect){36, 766, 612, 78}; }
// y766 行已下载的书拆两半：左「立即同步」右「重新下载」（设置 WiFi 仍用整宽）。
// / Downloaded books split the y766 row: sync left, re-download right (WiFi keeps full width).
static EpdRect sync_now_rect(void) { return (EpdRect){36, 766, 298, 78}; }
static EpdRect redownload_half_rect(void) { return (EpdRect){350, 766, 298, 78}; }
static EpdRect shelf_rect(void) { return (EpdRect){36, 864, 612, 78}; }
static EpdRect thought_tab_rect(int tab) { return (EpdRect){tab ? 342 : 36, 152, 306, 76}; }
static EpdRect thought_prev_rect(void) { return (EpdRect){36, 1080, 164, 70}; }
static EpdRect thought_next_rect(void) { return (EpdRect){484, 1080, 164, 70}; }
#define THOUGHT_ROWS 3
#define THOUGHT_CARD_H 244
#define THOUGHT_TEXT_CAP 768

static void card(uint8_t *fb, EpdRect r, int radius) {
    ui_fill_round_rect(fb, r, radius, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, r, radius, 0x60);
    ui_draw_round_rect(fb, (EpdRect){r.x + 1, r.y + 1, r.width - 2, r.height - 2}, radius - 1, 0x60);
}
static void button(uint8_t *fb, EpdRect r, const char *label, bool primary) {
    card(fb, r, 20);
    if (primary) ui_draw_round_rect(fb, (EpdRect){r.x + 2, r.y + 2, r.width - 4, r.height - 4}, 18, 0x30);
    ui_text_vc(fb, r.x + r.width / 2, r.y + r.height / 2, 25, label, EPD_DRAW_ALIGN_CENTER, false);
}
// 像素心形（9x8，灰度 0x30）：已拉取划线想法书籍的角标（系统字库无 ♥ 字形，位图最稳）。
// / 9x8 pixel heart (gray 0x30): badge for books with fetched notes (no ♥ in the font).
static void notes_heart(uint8_t *fb, int x, int y) {
    static const char *art[8] = {
        ".XX...XX.", "XXXX.XXXX", "XXXXXXXXX", "XXXXXXXXX",
        ".XXXXXXX.", "..XXXXX..", "...XXX...", "....X....",
    };
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 9; ++c)
            if (art[r][c] == 'X') epd_fill_rect((EpdRect){x + c, y + r, 1, 1}, 0x30, fb);
}
static void fit_text(char *dst, size_t cap, const char *src, int px, int width) {
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n); dst[n] = 0;
    while (n && ui_text_fixed_width_px(px, dst) > width) {
        --n;
        while (n && ((unsigned char)dst[n] & 0xc0) == 0x80) --n;
        dst[n] = 0;
    }
}
// 按像素宽折行绘制，超出行数在末行截断；会原地修改文本，渲染每次重新读取可放心。
// / Word-wrap by pixel width, truncating into the last line; mutates in place, safe because render refetches.
static void draw_paragraph(uint8_t *fb, int x, int y, int width, int px, char *text, int lines) {
    char *cursor = text;
    for (int row = 0; row < lines && cursor[0]; ++row) {
        const int top = y + row * (px + 16);
        // 逐字符试探：临时截断测宽再恢复，避免整条文本恒超行宽导致空行。
        // / Probe per character: truncate temporarily to measure, then restore.
        size_t keep = strlen(cursor);
        while (keep) {
            const char saved = cursor[keep];
            cursor[keep] = 0;
            const int w = ui_text_fixed_width_px(px, cursor);
            cursor[keep] = saved;
            if (w <= width) break;
            --keep;
            while (keep && ((unsigned char)cursor[keep] & 0xc0) == 0x80) --keep;
        }
        if (!keep) break;
        if (row == lines - 1) {
            cursor[keep] = 0;
            ui_text(fb, x, top, px, cursor, EPD_DRAW_ALIGN_LEFT, false);
            break;
        }
        const char next = cursor[keep];
        cursor[keep] = 0;
        ui_text(fb, x, top, px, cursor, EPD_DRAW_ALIGN_LEFT, false);
        cursor[keep] = next;
        cursor += keep;
        while (*cursor == ' ') ++cursor;
    }
}
// 跨页选择按云端书号保存，内存按需增长，退出时统一释放。
// Keep cross-page selections by remote ID; grow on demand and free on exit.
static int selection_index(const char *id) {
    for (unsigned i = 0; i < s_selection_count; ++i)
        if (!strcmp(s_selection[i].id, id)) return (int)i;
    return -1;
}
static bool toggle_selection(unsigned row) {
    if (row >= s_view.count || !s_view.books[row].id[0]) return false;
    int found = selection_index(s_view.books[row].id);
    if (found >= 0) {
        memmove(s_selection + found, s_selection + found + 1,
                (s_selection_count - (unsigned)found - 1) * sizeof(*s_selection));
        --s_selection_count; return true;
    }
    if (s_selection_count == s_selection_capacity) {
        if (s_selection_capacity >= WEREAD_BATCH_MAX) {
            snprintf(s_message, sizeof(s_message), "一次最多选择 %u 本", WEREAD_BATCH_MAX); return false;
        }
        unsigned capacity = s_selection_capacity + 32;
        weread_selection_t *next = heap_caps_calloc(capacity, sizeof(*next), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!next) { snprintf(s_message, sizeof(s_message), "选择未保存，内存不足"); return false; }
        if (s_selection_count) memcpy(next, s_selection, s_selection_count * sizeof(*next));
        heap_caps_free(s_selection); s_selection = next; s_selection_capacity = capacity;
    }
    weread_selection_t *entry = s_selection + s_selection_count++;
    snprintf(entry->id, sizeof(entry->id), "%s", s_view.books[row].id);
    entry->index = s_view.page * WEREAD_ROWS + row;
    s_message[0] = 0; return true;
}
static const char *status_text(void) {
    if (s_message[0]) return s_message;
    if (!s_view_storage) return "内存不足，请重启后重试";
    if (!s_ready) return "下载书籍需要插入 TF 卡";
    if (s_view.state == WEREAD_CONNECTING) return "正在连接 WiFi";
    if (s_view.state == WEREAD_QR) return "请用手机微信扫码确认登录";
    if (s_view.state == WEREAD_WORKING) {
        if (s_view.action == WEREAD_LOAD) return "正在读取本地书架";
        if (s_view.action == WEREAD_READ_REPORT) return "正在同步阅读进度";
        if (s_view.action == WEREAD_NOTES) {
            // 整本划线想法：章粒度 + 条粒度（服务器未知总数时只报已获取条数）。
            // / Whole-book notes: chapter progress plus thought counts (0 = unknown total).
            static char notes_status[96];
            if (s_view.notes_total)
                snprintf(notes_status, sizeof(notes_status), "划线想法 %u/%u 章 · 想法 %u/%u 条",
                         s_view.done, s_view.target, s_view.notes_done, s_view.notes_total);
            else
                snprintf(notes_status, sizeof(notes_status), "划线想法 %u/%u 章 · 已获取 %u 条想法",
                         s_view.done, s_view.target, s_view.notes_done);
            return notes_status;
        }
        if (s_view.action == WEREAD_THOUGHTS) return "正在获取划线想法";
        switch (s_view.stage) {
        case WEREAD_PREPARING: return "正在准备封面与书籍";
        case WEREAD_IMAGES: return "正在下载正文插图";
        case WEREAD_PACKAGING: return "正在生成 EPUB 文件";
        default: return (s_view.action == WEREAD_DOWNLOAD || s_view.action == WEREAD_BATCH) ?
            (s_view.target ? "正在下载书籍章节" : "正在获取书籍信息") : "正在同步书架，请稍候";
        }
    }
    if (s_view.state == WEREAD_CANCELLED) return "已取消，书架与已下载书籍保留";
    if (s_view.state == WEREAD_FAILED) {
        switch (s_view.error) {
        case 100: return "请先配置可访问互联网的 WiFi";
        case 101: return "连接失败，请检查 WiFi";
        case 102: return "对时失败，请检查网络后重试";
        case 103: case 6: return "存储失败，请检查 TF 卡与空间";
        case 7: return "内容校验失败，请重新下载";
        case 2: return "网络请求失败，请检查连接";
        case 3: case 4: return "登录失效，请重新同步扫码";
        case 8: case 11: return "此书暂不可下载，请换一本";
        case 9: return "设备时间无效，请重新联网";
        case 10: return "内存不足，请重启后重试";
        case 12: return "本书缺少目录缓存，请重新下载全书";
        default: return "操作失败，可以重新尝试";
        }
    }
    if (s_view.state == WEREAD_COMPLETE && s_view.output[0])
        return s_view.skipped_images ? "下载完成，部分插图未能获取" : "下载完成，可以离线阅读";
    return s_view.logged_in ? "选择书籍，下载后可离线阅读" : "扫码登录后，获取微信读书书架";
}
// 手动「立即同步」：把本地保存的阅读位置推到云端（纯进度包，rt=0，官方 enter 同形态）。
// 无本地进度的书直接拒绝——绝不把未知位置当 0 上传，防止云端进度被拉回开头。
// / Manual "Sync now": push the locally saved position upstream (progress-only, rt=0,
// / official enter shape). Refuse books without local progress — never upload a guessed
// / position that could drag cloud progress back to the start.
static void refresh_snapshot(void);
static void start_progress_sync(void) {
    const char *path = s_view.books[s_selected].local_path;
    struct stat st;
    book_progress_t p;
    if (!path[0] || stat(path, &st) != 0 ||
        !book_progress_load(path, (uint32_t)st.st_size, &p)) {
        snprintf(s_message, sizeof(s_message), "本书无本地进度，请先阅读");
        return;
    }
    if (!weread_start_read_report(s_view.books[s_selected].id, p.chapter, p.byte_off, p.pct, 0))
        snprintf(s_message, sizeof(s_message), "同步未启动，请稍后重试");
    refresh_snapshot();
}
static void refresh_snapshot(void) {
    if (!s_view_storage) return;
    weread_snapshot(&s_view);
    if (s_view.changed != s_changed) { s_changed = s_view.changed; book_store_notify_changed(); }
    if (strcmp(s_qr, s_view.qr)) {
        snprintf(s_qr, sizeof(s_qr), "%s", s_view.qr);
        s_qr_ok = s_qr[0] && ui_wifi_qr_prepare_weread(s_qr);
        if (!s_qr[0]) ui_wifi_qr_clear();
    }
}
static void configure(void) {
    if (!s_view_storage) s_view_storage = heap_caps_calloc(1, sizeof(*s_view_storage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_view_storage) { s_ready = false; return; }
    read_pico_sd_info_t sd;
    read_pico_sd_get_info(&sd);
    book_store_root_t root;
    s_ready = sd.mounted && book_store_upload_root(&root) == ESP_OK && !root.is_flash &&
              weread_configure("/sdcard/.readpico/weread", root.path);
    if (s_ready) (void)weread_start(WEREAD_LOAD, 0, 0);
}
static void start_action(weread_action_t action, unsigned page, unsigned index) {
    if (!s_ready || s_view.active) return;
    s_message[0] = 0;
    ttf_font_cache_clear();
    (void)weread_set_include_images(s_images);
    (void)weread_start(action, page, index);
    refresh_snapshot();
}
static void on_enter(app_ctx_t *ctx) {
    s_selected = -1; s_multiselect = s_batch_panel = false; s_selection_count = 0; s_message[0] = 0; s_qr[0] = 0; s_qr_ok = s_logout_confirm = false;
    s_thoughts_view = s_thoughts_pending = false;
    s_notes_toast = s_notes_toast_pending = false; s_notes_redraw_tick = 0;
    s_notes_seen_done = UINT_MAX; s_notes_seen_total = 0;
    s_next_tick = ctx->now_ms;
    display_set_bulk_io(true);
    ui_wifi_qr_clear();
    configure(); refresh_snapshot();
}
static void weread_on_exit(app_ctx_t *ctx) {
    (void)ctx;
    weread_stop(); refresh_snapshot(); ui_wifi_qr_clear(); s_qr[0] = 0;
    heap_caps_free(s_view_storage); s_view_storage = NULL;
    heap_caps_free(s_selection); s_selection = NULL; s_selection_count = s_selection_capacity = 0;
    s_multiselect = s_batch_panel = s_thoughts_view = s_thoughts_pending = false;
    display_set_bulk_io(false);
}
static void on_media_lost(app_ctx_t *ctx) { weread_on_exit(ctx); s_ready = false; s_selected = -1; }
static void on_media_ready(app_ctx_t *ctx) { (void)ctx; display_set_bulk_io(true); configure(); refresh_snapshot(); }
static void on_before_lock(app_ctx_t *ctx) { (void)ctx; weread_stop(); refresh_snapshot(); }
static void draw_images_option(uint8_t *fb) {
    card(fb, image_rect(), 20);
    ui_text_vc(fb, 58, 498, 25, "包含正文插图", EPD_DRAW_ALIGN_LEFT, false);
    ui_fill_round_rect(fb, (EpdRect){548, 479, 76, 38}, 19, s_images ? 0x40 : 0xb0);
    ui_fill_round_rect(fb, (EpdRect){s_images ? 587 : 551, 482, 32, 32}, 16, UI_GRAY_WHITE);
}
// 划线想法浏览页：分类 Tab + 每页三条卡片（折行文本 + 章节与热度小字）。
// / Thoughts browser: tabs, three cards per page (wrapped text + chapter/heat captions).
static void render_thoughts(uint8_t *fb) {
    const weread_book_t *book = &s_view.books[s_selected];
    ui_clear_page(fb);
    epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, 0xe0, fb);
    ui_nav_status(fb); ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "划线想法", EPD_DRAW_ALIGN_CENTER, false);
    for (int tab = 0; tab < 2; ++tab) {
        char label[48];
        snprintf(label, sizeof(label), "%s %u", tab ? "热门想法" : "热门划线",
                 weread_thoughts_count(tab ? WEREAD_THOUGHT_REVIEWS : WEREAD_THOUGHT_HIGHLIGHTS));
        button(fb, thought_tab_rect(tab), label, s_thoughts_kind == (tab ? WEREAD_THOUGHT_REVIEWS : WEREAD_THOUGHT_HIGHLIGHTS));
    }
    const unsigned kind = s_thoughts_kind ? WEREAD_THOUGHT_REVIEWS : WEREAD_THOUGHT_HIGHLIGHTS;
    const unsigned total = weread_thoughts_count(kind);
    const unsigned pages = total ? (total + THOUGHT_ROWS - 1) / THOUGHT_ROWS : 1;
    if (s_thoughts_page >= pages) s_thoughts_page = pages - 1;
    char count[64];
    snprintf(count, sizeof(count), "共 %u 条 · 第 %u/%u 页", total, s_thoughts_page + 1, pages);
    ui_text(fb, 48, 272, 21, count, EPD_DRAW_ALIGN_LEFT, false);
    if (!total) {
        card(fb, (EpdRect){36, 320, 612, 240}, 24);
        ui_text_vc(fb, 342, 400, 26, "暂无公开划线或想法", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 456, 21, "可在书籍详情页重新获取", EPD_DRAW_ALIGN_CENTER, false);
    }
    static char texts[THOUGHT_ROWS][THOUGHT_TEXT_CAP];
    static weread_thought_meta_t metas[THOUGHT_ROWS];
    for (unsigned i = 0; i < THOUGHT_ROWS; ++i) {
        const unsigned index = s_thoughts_page * THOUGHT_ROWS + i;
        if (index >= total) break;
        EpdRect r = {36, 300 + i * 250, 612, 240};
        card(fb, r, 22);
        if (weread_thoughts_get(kind, index, texts[i], THOUGHT_TEXT_CAP, &metas[i])) {
            draw_paragraph(fb, 58, r.y + 18, 566, 26, texts[i], 3);
            char who[288], chapter[128];
            fit_text(chapter, sizeof(chapter), metas[i].chapter, 20, 320);
            if (metas[i].author[0]) snprintf(who, sizeof(who), "%s · %s", chapter, metas[i].author);
            else snprintf(who, sizeof(who), "%s", chapter);
            ui_text(fb, 58, r.y + 200, 20, who, EPD_DRAW_ALIGN_LEFT, false);
            char heat[48];
            if (kind == WEREAD_THOUGHT_HIGHLIGHTS)
                snprintf(heat, sizeof(heat), "%u 人划线", (unsigned)metas[i].heat);
            else
                snprintf(heat, sizeof(heat), "%u 赞", (unsigned)metas[i].heat);
            ui_text_fixed(fb, 626, r.y + 200, 20, heat, EPD_DRAW_ALIGN_RIGHT, false);
        } else {
            ui_text_vc(fb, 342, r.y + 110, 22, "读取失败", EPD_DRAW_ALIGN_CENTER, false);
        }
    }
    button(fb, thought_prev_rect(), "上一页", s_thoughts_page == 0);
    button(fb, thought_next_rect(), "下一页", s_thoughts_page >= pages - 1);
    ui_nav_draw(fb, 2);
}
static void draw_progress(uint8_t *fb) {
    card(fb, (EpdRect){36, 384, 612, 312}, 24);
    char progress[80], title[192];
    // 划线想法任务用专属文案：突出逐章保存与断点续传，慢也不慌。
    // / Notes runs get dedicated copy highlighting per-chapter saves and resume.
    const bool notes = s_view.action == WEREAD_NOTES;
    const bool report = s_view.action == WEREAD_READ_REPORT;
    if (s_view.batch_total && s_view.batch_current) snprintf(progress, sizeof(progress), "第 %u / %u 本", s_view.batch_current, s_view.batch_total);
    else snprintf(progress, sizeof(progress), notes ? "正在获取划线想法" : report ? "正在同步阅读进度" : "正在下载");
    ui_text_vc(fb, 342, 422, 29, progress, EPD_DRAW_ALIGN_CENTER, false);
    fit_text(title, sizeof(title), s_view.batch_title, 26, 544);
    ui_text_fixed(fb, 342, 463, 26, title, EPD_DRAW_ALIGN_CENTER, false);
    if (s_view.target) snprintf(progress, sizeof(progress), notes ? "%u / %u 章" : "%u / %u", s_view.done, s_view.target);
    else snprintf(progress, sizeof(progress), "正在处理");
    ui_text_vc(fb, 342, 524, 30, progress, EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_round_rect(fb, (EpdRect){70, 575, 544, 10}, 5, 0xb0);
    if (s_view.target) {
        unsigned done = s_view.done > s_view.target ? s_view.target : s_view.done;
        int width = (int)((uint64_t)done * 544 / s_view.target);
        if (width) ui_fill_round_rect(fb, (EpdRect){70, 575, width, 10}, 5, 0x30);
    }
    if (notes) {
        // 实时想法条数随任务增长，展示出来让人有「数据在进来」的踏实感。
        // / Live thought tally gives a sense of steady progress.
        if (s_view.notes_total) snprintf(progress, sizeof(progress), "已获取 %u/%u 条想法 · 已完成章节自动保存", s_view.notes_done, s_view.notes_total);
        else snprintf(progress, sizeof(progress), "已获取 %u 条想法 · 已完成章节自动保存", s_view.notes_done);
    } else if (report) snprintf(progress, sizeof(progress), "正在上传本书的阅读位置，几秒即可完成");
    else snprintf(progress, sizeof(progress), "一次只下载一本，完成后继续下一本");
    ui_text_vc(fb, 342, 641, 22, progress, EPD_DRAW_ALIGN_CENTER, false);
    button(fb, cancel_rect(), "取消操作", true);
    ui_text(fb, 48, 864, 22, notes ? "离开或锁屏会暂停；已完成章节已保存，重新打开书籍自动续传" :
            report ? "同步的是本书最近阅读位置，失败不影响本地阅读" : "离开页面或锁屏会停止本次操作", EPD_DRAW_ALIGN_LEFT, false);
}
static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    const bool detail = s_view_storage && s_selected >= 0 && (unsigned)s_selected < s_view.count;
    if (s_thoughts_view && detail) { render_thoughts(fb); return; }
    const bool secondary = detail || s_batch_panel;
    ui_clear_page(fb);
    epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, 0xe0, fb);
    ui_nav_status(fb); ui_nav_back(fb, 36, 79);
    // 划线想法任务进行中时使用专属标题与卡片文案，让进度页成为划线想法语境。
    // / Dedicated title and card copy while a notes fetch runs, so the progress
    // / page reads as a notes-specific view.
    const bool notes_task = s_view_storage && s_view.active && s_view.action == WEREAD_NOTES;
    const bool report_task = s_view_storage && s_view.active && s_view.action == WEREAD_READ_REPORT;
    ui_text_vc(fb, 342, 107, 34, s_batch_panel ? "批量下载" : detail ? (notes_task ? "划线想法下载" : report_task ? "进度同步" : "书籍下载") : "微读传书", EPD_DRAW_ALIGN_CENTER, false);
    if (secondary) {
        card(fb, (EpdRect){36, 176, 612, detail ? 174 : 94}, 22);
        char title[192], author[96];
        if (detail) {
            fit_text(title, sizeof(title), s_view.books[s_selected].title, 30, 564);
            fit_text(author, sizeof(author), s_view.books[s_selected].author, 24, 564);
            ui_text_fixed(fb, 58, 203, 30, title, EPD_DRAW_ALIGN_LEFT, false);
            ui_text_fixed(fb, 58, 259, 24, author, EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 58, 307, 22, notes_task ? "逐章获取 · 实时保存到 TF 卡" :
                    report_task ? "正在上传本书的阅读进度" : "封面始终下载", EPD_DRAW_ALIGN_LEFT, false);
            // 已缓存划线想法：卡内右侧常驻标记（纯 SD 只读扫描，无任务时才走到这）。
            // / Cached notes marker at the card's right (read-only SD scan; no task runs here).
            unsigned n_cached, n_total, n_marks;
            if (weread_notes_book_stats(s_view.books[s_selected].id, &n_cached, &n_total, &n_marks)) {
                char badge[64];
                if (n_total)
                    snprintf(badge, sizeof(badge), "划线想法 %u/%u 章 · %u 句", n_cached, n_total, n_marks);
                else
                    snprintf(badge, sizeof(badge), "划线想法 %u 章 · %u 句", n_cached, n_marks);
                ui_text_fixed(fb, 626, 307, 22, badge, EPD_DRAW_ALIGN_RIGHT, false);
            }
        } else {
            snprintf(title, sizeof(title), "已选择 %u 本书籍", s_selection_count);
            ui_text_vc(fb, 58, 223, 29, title, EPD_DRAW_ALIGN_LEFT, false);
        }
        ui_text(fb, 48, detail ? 359 : 293, 23, status_text(), EPD_DRAW_ALIGN_LEFT, false);
        // 阅读进度同步状态（详情页空闲时）：最近一次上报尝试的结果与时刻。
        // / Read-report status (idle detail page): latest attempt result and when.
        if (detail && !s_view.active) {
            bool has, ok;
            int64_t epoch;
            weread_last_read_report(&has, &ok, &epoch);
            if (has) {
                char sync_info[48];
                const time_t now = time(NULL);
                if (!ok) snprintf(sync_info, sizeof(sync_info), "上次同步失败，可重试");
                else if (epoch > 0 && now > (time_t)epoch && now - (time_t)epoch < 86400)
                    snprintf(sync_info, sizeof(sync_info), "已同步 · %ld 分钟前",
                             (long)((now - (time_t)epoch) / 60));
                else snprintf(sync_info, sizeof(sync_info), "已同步");
                ui_text_fixed(fb, 626, 359, 22, sync_info, EPD_DRAW_ALIGN_RIGHT, false);
            }
        }
    } else {
        card(fb, (EpdRect){36, 176, 612, 94}, 22);
        ui_text(fb, 58, 187, 27, s_view_storage && s_view.logged_in ? "微信读书书架" : "微信读书", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 58, 232, 21, status_text(), EPD_DRAW_ALIGN_LEFT, false);
        if (s_view_storage && s_view.logged_in) {
            char count[48];
            snprintf(count, sizeof(count), "%u 本 · %u/%u 页", s_view.total, s_view.page + 1,
                     s_view.total ? (s_view.total + WEREAD_ROWS - 1) / WEREAD_ROWS : 1);
            ui_text(fb, 624, 191, 21, count, EPD_DRAW_ALIGN_RIGHT, false);
        }
        if (!s_view_storage || !s_view.active) {
            button(fb, sync_rect(), s_multiselect ? "全选本页" :
                   s_view_storage && s_view.logged_in ? "同步书架" : "扫码登录", true);
            button(fb, select_rect(), s_multiselect ? "清空" : "多选", false);
            button(fb, logout_rect(), s_multiselect ? "完成选择" : "退出登录", false);
        }
    }
    if (!s_ready || !s_view_storage) {
        ui_text(fb, 58, 420, 26, "请插入 TF 卡后重新打开", EPD_DRAW_ALIGN_LEFT, false);
    } else if (s_view.state == WEREAD_QR) {
        card(fb, (EpdRect){36, 384, 612, 590}, 24);
        ui_text_vc(fb, 342, 429, 27, "扫描二维码", EPD_DRAW_ALIGN_CENTER, false);
        if (s_qr_ok) ui_wifi_qr_draw(fb, (EpdRect){142, 469, 400, 400});
        else ui_text_vc(fb, 342, 639, 24, "二维码生成失败，请重新同步", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 905, 22, "手机确认后自动继续", EPD_DRAW_ALIGN_CENTER, false);
        button(fb, (EpdRect){36, 990, 612, 70}, "取消操作", true);
    } else if (s_view.active) draw_progress(fb);
    else if (secondary) {
        draw_images_option(fb);
        if (s_batch_panel) {
            char summary[96];
            snprintf(summary, sizeof(summary), "完成 %u 本 · 失败 %u 本 · 未下载 %u 本", s_view.batch_success,
                     s_view.batch_failed, s_view.batch_total > s_view.batch_success + s_view.batch_failed ?
                     s_view.batch_total - s_view.batch_success - s_view.batch_failed : 0);
            if (s_view.action == WEREAD_BATCH) ui_text(fb, 48, 390, 23, summary, EPD_DRAW_ALIGN_LEFT, false);
            button(fb, download_rect(), "开始批量下载", true);
        } else {
            button(fb, download_rect(), s_view.books[s_selected].local_path[0] ? "打开已下载书籍" : "下载到 TF 卡", true);
            button(fb, thoughts_rect(), "划线想法", true);
            if (s_view.books[s_selected].local_path[0]) {
                button(fb, sync_now_rect(), "立即同步", false);
                button(fb, redownload_half_rect(), "重新下载", false);
            }
        }
        if (s_view.state == WEREAD_FAILED && s_view.error == 100) button(fb, redownload_rect(), "设置 WiFi", false);
        button(fb, shelf_rect(), "返回微信书架", false);
        ui_text(fb, 48, 964, 21, "划线与想法为网友公开内容，只读浏览", EPD_DRAW_ALIGN_LEFT, false);
    } else if (!s_view.count) {
        card(fb, (EpdRect){36, 384, 612, 286}, 24);
        ui_text_vc(fb, 342, 445, 28, "把微信读书带到 Pico", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 515, 22, "连接 WiFi → 扫码登录 → 选择书籍", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 561, 22, "下载为 EPUB，保存后可离线阅读", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 617, 20, "需要可访问互联网的 2.4 GHz WiFi", EPD_DRAW_ALIGN_CENTER, false);
        button(fb, (EpdRect){36, 703, 612, 78}, "设置 WiFi", false);
    } else {
        card(fb, (EpdRect){36, 384, 612, s_view.count * 84}, 24);
        for (unsigned i = 0; i < s_view.count; ++i) {
            EpdRect r = row_rect(i); char title[192], author[96];
            if (i) ui_hairline(fb, r.y, 58, 566, 0x80);
            fit_text(title, sizeof(title), s_view.books[i].title, 30, s_multiselect ? 496 : 518);
            fit_text(author, sizeof(author), s_view.books[i].author, 22, 420);
            ui_text_fixed(fb, 58, r.y + 10, 30, title, EPD_DRAW_ALIGN_LEFT, false);
            ui_text_fixed(fb, 58, r.y + 51, 22, author, EPD_DRAW_ALIGN_LEFT, false);
            if (s_multiselect) {
                EpdRect check = {582, r.y + 20, 38, 38};
                ui_draw_round_rect(fb, check, 8, 0x30);
                ui_draw_round_rect(fb, (EpdRect){583, r.y + 21, 36, 36}, 7, 0x30);
                if (selection_index(s_view.books[i].id) >= 0) ui_fill_round_rect(fb, (EpdRect){589, r.y + 27, 24, 24}, 4, 0x20);
            } else ui_text_vc(fb, 620, r.y + 27, 28, "›", EPD_DRAW_ALIGN_CENTER, false);
            if (s_view.books[i].local_path[0]) ui_text_fixed(fb, 624, r.y + 51, 18, "已下载", EPD_DRAW_ALIGN_RIGHT, false);
            // 已拉取划线想法的书：作者行 ♥ + 划线句数角标（纯 SD 只读扫描）。
            // / Books with fetched notes: heart + highlight count on the author row.
            unsigned n_cached, n_total, n_marks;
            if (weread_notes_book_stats(s_view.books[i].id, &n_cached, &n_total, &n_marks)) {
                char marks[12];
                snprintf(marks, sizeof(marks), "%u", n_marks);
                ui_text_fixed(fb, 552, r.y + 51, 18, marks, EPD_DRAW_ALIGN_RIGHT, false);
                notes_heart(fb, 552 - ui_text_fixed_width_px(ui_text_effective_px(18), marks) - 13, r.y + 57);
            }
        }
        button(fb, prev_rect(), "上一页", false); button(fb, next_rect(), "下一页", false);
        if (s_multiselect) {
            char count[40]; snprintf(count, sizeof(count), "下载 %u 本", s_selection_count);
            button(fb, batch_rect(), count, s_selection_count != 0);
        } else {
            char page[40]; snprintf(page, sizeof(page), "%u / %u", s_view.page + 1, (s_view.total + WEREAD_ROWS - 1) / WEREAD_ROWS);
            ui_text_vc(fb, 342, 1036, 24, page, EPD_DRAW_ALIGN_CENTER, false);
        }
    }
    ui_nav_draw(fb, 2);
    if (s_logout_confirm) {
        card(fb, (EpdRect){58, 416, 568, 300}, 28);
        ui_text_vc(fb, 342, 477, 30, "退出微信读书登录？", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 536, 22, "已下载的书籍会继续保留", EPD_DRAW_ALIGN_CENTER, false);
        button(fb, (EpdRect){82, 604, 240, 74}, "取消", false);
        button(fb, (EpdRect){362, 604, 240, 74}, "退出登录", true);
    }
    // 整本划线想法完成确认：统计一次拉取成果，任意点击/按键关闭。
    // / Whole-book notes confirm: the run's tally; any tap/key dismisses it.
    if (s_notes_toast) {
        const bool failed = s_view.state == WEREAD_FAILED;
        card(fb, (EpdRect){58, 416, 568, 300}, 28);
        ui_text_vc(fb, 342, 477, 30, failed ? "划线想法下载失败" : "划线想法下载完成", EPD_DRAW_ALIGN_CENTER, false);
        char line[96];
        if (failed)
            snprintf(line, sizeof(line), "%s", status_text());
        else
            snprintf(line, sizeof(line), "已获取 %u 章 · %u 条想法", s_view.done, s_view.notes_done);
        ui_text_vc(fb, 342, 536, 22, line, EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 578, 18, "进入正文即可查看划线与想法", EPD_DRAW_ALIGN_CENTER, false);
        button(fb, (EpdRect){242, 604, 200, 74}, "好", true);
    }
}
static app_redraw_t on_tick(app_ctx_t *ctx) {
    if (!s_view_storage || ctx->now_ms < s_next_tick) return APP_REDRAW_NONE;
    s_next_tick = ctx->now_ms + 1200;
    unsigned revision = s_view.revision, changed = s_view.changed, count = s_view.count;
    weread_state_t state = s_view.state;
    weread_stage_t stage = s_view.stage;
    bool active = s_view.active;
    unsigned current = s_view.batch_current, failures = s_view.batch_failed;
    unsigned bucket = s_view.target ? (unsigned)((uint64_t)s_view.done * 20 / s_view.target) : 0;
    refresh_snapshot();
    unsigned next = s_view.target ? (unsigned)((uint64_t)s_view.done * 20 / s_view.target) : 0;
    // 拉取任务收尾：成功自动进入浏览视图，失败给出原因。
    // / Fetch-task landing: open the browser on success, explain on failure.
    if (s_thoughts_pending && !s_view.active && s_view.action == WEREAD_THOUGHTS) {
        s_thoughts_pending = false;
        if (s_view.state == WEREAD_COMPLETE && s_selected >= 0 &&
            (unsigned)s_selected < s_view.count &&
            weread_thoughts_open(s_view.books[s_selected].id)) {
            s_thoughts_view = true; s_thoughts_kind = WEREAD_THOUGHT_HIGHLIGHTS; s_thoughts_page = 0;
            return APP_REDRAW_PAGE;
        }
        if (s_view.state == WEREAD_FAILED)
            snprintf(s_message, sizeof(s_message), "%s", status_text());
        return APP_REDRAW_PAGE;
    }
    // 整本划线想法收尾：完成后弹一次性确认卡片（含统计）；取消不弹。
    // / Whole-book notes landing: one-shot confirm card on completion or
    // / failure (with the tally); silent on user cancel.
    if (s_view.action == WEREAD_NOTES && s_view.active) s_notes_toast_pending = true;
    if (s_notes_toast_pending && !s_view.active && s_view.action == WEREAD_NOTES) {
        s_notes_toast_pending = false;
        if (s_view.state == WEREAD_COMPLETE || s_view.state == WEREAD_FAILED) {
            s_notes_toast = true;
            return APP_REDRAW_PAGE;
        }
    }
    if (revision == s_view.revision) return APP_REDRAW_NONE;
    if (!s_view.active) return APP_REDRAW_PAGE;
    // 整本划线想法：条数每页都在涨，但状态行按 30 秒节流重绘——进度可见又不频闪；
    // 章数与条数的变化都计入「动了」的判定。
    // / Whole-book notes: counts tick per page; redraw the status line at most
    // / every 30 s — visible progress without constant e-ink flashing.
    if (s_view.action == WEREAD_NOTES) {
        const bool moved = s_view.notes_done != s_notes_seen_done ||
                           s_view.notes_total != s_notes_seen_total || bucket != next;
        s_notes_seen_done = s_view.notes_done;
        s_notes_seen_total = s_view.notes_total;
        if (!moved || ctx->now_ms < s_notes_redraw_tick) return APP_REDRAW_NONE;
        s_notes_redraw_tick = ctx->now_ms + 30000;
        return APP_REDRAW_PAGE;
    }
    return state != s_view.state || stage != s_view.stage || active != s_view.active ||
           current != s_view.batch_current || failures != s_view.batch_failed ||
           count != s_view.count || changed != s_view.changed || bucket != next ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
}
static app_redraw_t go_back(app_ctx_t *ctx) {
    if (s_notes_toast) { s_notes_toast = false; return APP_REDRAW_PAGE; }
    if (s_logout_confirm) { s_logout_confirm = false; return APP_REDRAW_PAGE; }
    if (s_thoughts_view) { s_thoughts_view = false; s_message[0] = 0; return APP_REDRAW_PAGE; }
    if (s_view_storage && s_view.active) { weread_stop(); refresh_snapshot(); }
    if (s_selected >= 0 || s_batch_panel) { s_selected = -1; s_batch_panel = false; s_message[0] = 0; return APP_REDRAW_PAGE; }
    if (s_multiselect) { s_multiselect = false; return APP_REDRAW_PAGE; }
    // 产品页明确返回文件管理，不进入示例工程的菜单返回栈。
    // Return explicitly to Files instead of the example project's menu return stack.
    extern const app_desc_t app_files;
    ctx->request_app = &app_files; return APP_REDRAW_NONE;
}
static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type != UI_GESTURE_TAP && ev->type != UI_GESTURE_SWIPE_L && ev->type != UI_GESTURE_SWIPE_R) return APP_REDRAW_NONE;
    if (s_notes_toast) { s_notes_toast = false; return APP_REDRAW_PAGE; }
    if (s_logout_confirm) {
        if (ev->type == UI_GESTURE_TAP && ui_rect_hit((EpdRect){362, 604, 240, 74}, ev->x0, ev->y0)) {
            s_logout_confirm = false; s_selected = -1; s_selection_count = 0; start_action(WEREAD_LOGOUT, 0, 0);
        } else if (ev->type == UI_GESTURE_TAP && ui_rect_hit((EpdRect){82, 604, 240, 74}, ev->x0, ev->y0)) s_logout_confirm = false;
        return APP_REDRAW_PAGE;
    }
    if (ev->type == UI_GESTURE_TAP) {
        if (ui_rect_hit((EpdRect){36, 79, 56, 56}, ev->x0, ev->y0)) return go_back(ctx);
        int nav = ui_nav_hit(ev->x0, ev->y0);
        if (nav >= 0) { ui_nav_request(ctx, nav); return APP_REDRAW_NONE; }
        // 划线想法浏览视图：Tab 切换与翻页。/ Thoughts browser: tabs and paging.
        if (s_thoughts_view) {
            const unsigned highlights = WEREAD_THOUGHT_HIGHLIGHTS, reviews = WEREAD_THOUGHT_REVIEWS;
            if (ui_rect_hit(thought_tab_rect(0), ev->x0, ev->y0)) {
                if (s_thoughts_kind != highlights) { s_thoughts_kind = highlights; s_thoughts_page = 0; }
                return APP_REDRAW_PAGE;
            }
            if (ui_rect_hit(thought_tab_rect(1), ev->x0, ev->y0)) {
                if (s_thoughts_kind != reviews) { s_thoughts_kind = reviews; s_thoughts_page = 0; }
                return APP_REDRAW_PAGE;
            }
            const unsigned total = weread_thoughts_count(s_thoughts_kind);
            const unsigned pages = total ? (total + THOUGHT_ROWS - 1) / THOUGHT_ROWS : 1;
            if (ui_rect_hit(thought_prev_rect(), ev->x0, ev->y0) && s_thoughts_page) {
                --s_thoughts_page; return APP_REDRAW_PAGE;
            }
            if (ui_rect_hit(thought_next_rect(), ev->x0, ev->y0) && s_thoughts_page < pages - 1) {
                ++s_thoughts_page; return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        if (s_view_storage && s_view.active &&
            ui_rect_hit(s_view.state == WEREAD_QR ? (EpdRect){36, 990, 612, 70} : cancel_rect(), ev->x0, ev->y0)) {
            weread_stop(); refresh_snapshot(); return APP_REDRAW_PAGE;
        }
    }
    if (!s_view_storage || s_view.active || !s_ready) return APP_REDRAW_NONE;
    const bool detail = s_selected >= 0 && (unsigned)s_selected < s_view.count;
    if (detail || s_batch_panel) {
        if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        if (ui_rect_hit(shelf_rect(), ev->x0, ev->y0)) { s_selected = -1; s_batch_panel = false; }
        else if (ui_rect_hit(image_rect(), ev->x0, ev->y0)) s_images = !s_images;
        else if (s_view.state == WEREAD_FAILED && s_view.error == 100 && ui_rect_hit(redownload_rect(), ev->x0, ev->y0)) {
            app_transfer_request_wifi_setup(); extern const app_desc_t app_transfer; ctx->request_app = &app_transfer;
        } else if (ui_rect_hit(thoughts_rect(), ev->x0, ev->y0)) {
            // 临时验证入口：触发句子级按章拉取（断点续传）；弹窗 UI 在下一阶段接入。
            // / Temporary trigger for the sentence-level per-chapter fetch; popup lands next stage.
            s_message[0] = 0; s_thoughts_pending = false; s_thoughts_view = false;
            start_action(WEREAD_NOTES, s_view.page, s_view.page * WEREAD_ROWS + s_selected);
        } else if (ui_rect_hit(download_rect(), ev->x0, ev->y0)) {
            if (s_batch_panel) {
                s_message[0] = 0; ttf_font_cache_clear(); (void)weread_set_include_images(s_images);
                if (!weread_start_batch(s_view.page, s_selection, s_selection_count))
                    snprintf(s_message, sizeof(s_message), "批量任务未启动，请重试");
                refresh_snapshot();
            } else if (s_view.books[s_selected].local_path[0]) {
                if (app_book_request_open(s_view.books[s_selected].local_path)) {
                    extern const app_desc_t app_book; ctx->request_app = &app_book;
                }
            } else start_action(WEREAD_DOWNLOAD, s_view.page, s_view.page * WEREAD_ROWS + s_selected);
        } else if (detail && s_view.books[s_selected].local_path[0] && ui_rect_hit(sync_now_rect(), ev->x0, ev->y0)) {
            // 手动「立即同步」：本地进度推云端（纯进度包）。/ Manual "Sync now": push local position upstream.
            s_message[0] = 0;
            start_progress_sync();
        } else if (detail && s_view.books[s_selected].local_path[0] && ui_rect_hit(redownload_half_rect(), ev->x0, ev->y0))
            start_action(WEREAD_DOWNLOAD, s_view.page, s_view.page * WEREAD_ROWS + s_selected);
        return ctx->request_app ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
    }
    if (ev->type == UI_GESTURE_TAP) {
        if (ui_rect_hit(sync_rect(), ev->x0, ev->y0)) {
            if (s_multiselect) {
                for (unsigned i = 0; i < s_view.count; ++i)
                    if (selection_index(s_view.books[i].id) < 0 && !toggle_selection(i)) break;
            } else { s_selection_count = 0; start_action(WEREAD_SYNC, s_view.page, 0); }
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(select_rect(), ev->x0, ev->y0)) {
            if (s_multiselect) { s_selection_count = 0; s_message[0] = 0; }
            else if (s_view.count) s_multiselect = true;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(logout_rect(), ev->x0, ev->y0)) {
            if (s_multiselect) s_multiselect = false;
            else if (s_view.logged_in) s_logout_confirm = true;
            return APP_REDRAW_PAGE;
        }
        if (s_multiselect && s_selection_count && ui_rect_hit(batch_rect(), ev->x0, ev->y0)) {
            s_batch_panel = true; return APP_REDRAW_PAGE;
        }
        if (!s_view.count && ui_rect_hit((EpdRect){36, 703, 612, 78}, ev->x0, ev->y0)) {
            app_transfer_request_wifi_setup(); extern const app_desc_t app_transfer; ctx->request_app = &app_transfer;
            return APP_REDRAW_NONE;
        }
        for (unsigned i = 0; i < s_view.count; ++i) if (ui_rect_hit(row_rect(i), ev->x0, ev->y0)) {
            if (s_multiselect) (void)toggle_selection(i); else s_selected = i;
            return APP_REDRAW_PAGE;
        }
    }
    bool previous = ev->type == UI_GESTURE_SWIPE_R || (ev->type == UI_GESTURE_TAP && ui_rect_hit(prev_rect(), ev->x0, ev->y0));
    bool next = ev->type == UI_GESTURE_SWIPE_L || (ev->type == UI_GESTURE_TAP && ui_rect_hit(next_rect(), ev->x0, ev->y0));
    if (previous && s_view.page) start_action(WEREAD_LOAD, s_view.page - 1, 0);
    else if (next && (s_view.page + 1) * WEREAD_ROWS < s_view.total) start_action(WEREAD_LOAD, s_view.page + 1, 0);
    else return APP_REDRAW_NONE;
    return APP_REDRAW_PAGE;
}
static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    if (s_notes_toast) { s_notes_toast = false; return APP_REDRAW_PAGE; }
    if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    return go_back(ctx);
}
static app_redraw_t on_key_long(app_ctx_t *ctx, int key) { (void)ctx; return key == UI_KEY_2 ? APP_REDRAW_FULL : APP_REDRAW_NONE; }
static bool menu_enabled(app_ctx_t *ctx) { (void)ctx; return false; }
const app_desc_t app_weread = {
    .title = "微读传书", .detail = "微信读书下载", .enter_full = true, .owns_keys = true,
    .menu_handle_enabled = menu_enabled, .on_enter = on_enter, .on_exit = weread_on_exit,
    .on_media_lost = on_media_lost, .on_media_ready = on_media_ready, .on_before_lock = on_before_lock,
    .render = render, .on_gesture = on_gesture, .on_key = on_key, .on_key_long = on_key_long, .on_tick = on_tick,
};

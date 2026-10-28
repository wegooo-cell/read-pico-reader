/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：云同步页：未绑定显示二维码；绑定后显示云端列表，点一个下载一个，可刷新/解绑。
 *       每行书名下是进度条，右侧是大小或「下载失败 / 校验失败 / 已导入」。
 * English: Cloud sync page: QR while unbound, then the cloud list with per-item download,
 *       refresh and unbind. Under each name is a progress bar; the right side shows size or
 *       download/verify/import state.
 *
 * 冻结：不自动下载全部条目；校验失败不落盘；解绑只清本地令牌；render 只绘制。
 * Frozen: never download every entry automatically; failed checks are never kept; unbind only
 *       clears local tokens; render paints only.
 */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_registry.h"
#include "book_store.h"
#include "book_xteink_sync.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include "ui_wifi_qr.h"

#define CLOUD_ROWS 5
#define CLOUD_ROW_TOP 220
#define CLOUD_ROW_STEP 120
#define CLOUD_ROW_HEIGHT 104
#define CLOUD_QR_AREA ((EpdRect){62, 190, 560, 560})
#define CLOUD_REFRESH_RECT ((EpdRect){36, 976, 288, 84})
#define CLOUD_UNBIND_RECT ((EpdRect){360, 976, 288, 84})

static unsigned s_seen_revision;
static unsigned s_seen_imports;
static char s_qr[1024];

static EpdRect row_rect(int index) {
    return (EpdRect){36, CLOUD_ROW_TOP + index * CLOUD_ROW_STEP, 612, CLOUD_ROW_HEIGHT};
}

static void fit_utf8(char* out, size_t cap, const char* in, size_t max_bytes) {
    if (!out || !cap) return;
    size_t bytes = 0;
    const unsigned char* p = (const unsigned char*)(in ? in : "");
    while (*p) {
        size_t length = *p < 0x80 ? 1 : (*p < 0xE0 ? 2 : (*p < 0xF0 ? 3 : 4));
        if (bytes + length > max_bytes || bytes + length + 1 >= cap) break;
        memcpy(out + bytes, p, length);
        bytes += length;
        p += length;
    }
    if (*p && bytes + 4 < cap) {
        memcpy(out + bytes, "…", 3);
        bytes += 3;
    }
    out[bytes] = 0;
}

static void format_size(char* out, size_t cap, uint32_t bytes) {
    if (bytes >= 1024u * 1024u)
        snprintf(out, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else
        snprintf(out, cap, "%u KB", (unsigned)((bytes + 1023u) / 1024u));
}

static void sync_qr(const xteink_sync_status_t* status) {
    if (status->bound) {
        if (s_qr[0]) {
            s_qr[0] = 0;
            ui_wifi_qr_clear();
        }
        return;
    }
    if (!strcmp(s_qr, status->qr)) return;
    snprintf(s_qr, sizeof(s_qr), "%s", status->qr);
    if (s_qr[0]) ui_wifi_qr_prepare_text(s_qr);
    else ui_wifi_qr_clear();
}

static void cloud_enter(app_ctx_t* ctx) {
    (void)ctx;
    s_seen_revision = 0;
    s_seen_imports = xteink_sync_import_count();
    if (!xteink_sync_busy()) xteink_sync_start();
}

static void cloud_exit(app_ctx_t* ctx) {
    (void)ctx;
    xteink_sync_cancel_join();
    ui_wifi_qr_clear();
    s_qr[0] = 0;
}

static void cloud_media_lost(app_ctx_t* ctx) {
    (void)ctx;
    xteink_sync_cancel_join();
}

static void cloud_before_lock(app_ctx_t* ctx) {
    (void)ctx;
    xteink_sync_cancel_join();
}

// 一行：书名 + 右侧状态；第二行左侧进度条或状态文字。
// One row: name plus right-side state; the second line is the bar or a state label.
static void draw_row(uint8_t* fb, int index, const xteink_task_t* task) {
    EpdRect r = row_rect(index);
    ui_fill_round_rect(fb, r, 22, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, r, 22, 0x70);

    char name[96];
    fit_utf8(name, sizeof(name), task->name, 60);
    ui_text(fb, r.x + 24, r.y + 16, 21, name, EPD_DRAW_ALIGN_LEFT, false);

    unsigned percent = 0;
    if (task->downloading && task->size)
        percent = (unsigned)((uint64_t)task->received * 100u / task->size);
    if (percent > 100) percent = 100;

    char right[32];
    if (task->downloading) {
        snprintf(right, sizeof(right), "%u%%", percent);
    } else if (task->failed) {
        snprintf(right, sizeof(right), "%s", task->error[0] ? task->error : "下载失败");
    } else if (task->imported) {
        snprintf(right, sizeof(right), "已导入");
    } else {
        format_size(right, sizeof(right), task->size);
    }
    ui_text_vc(fb, r.x + r.width - 24, r.y + 26, 20, right, EPD_DRAW_ALIGN_RIGHT, false);

    int bar_x = r.x + 24;
    int bar_y = r.y + 68;
    int bar_w = r.width - 48;
    if (task->downloading) {
        epd_draw_rect((EpdRect){bar_x, bar_y, bar_w, 14}, 0, fb);
        unsigned filled = (unsigned)((uint64_t)(bar_w - 4) * percent / 100u);
        if (filled)
            epd_fill_rect((EpdRect){bar_x + 2, bar_y + 2, (int)filled, 10}, 0, fb);
    } else if (task->failed) {
        ui_text(fb, bar_x, bar_y - 4, 19, task->error[0] ? task->error : "下载失败",
                EPD_DRAW_ALIGN_LEFT, false);
    } else if (task->imported) {
        ui_text(fb, bar_x, bar_y - 4, 19, task->is_font ? "已导入字体" : "已导入书架",
                EPD_DRAW_ALIGN_LEFT, false);
    } else {
        ui_text(fb, bar_x, bar_y - 4, 19, task->is_font ? "字体待下载" : "待下载",
                EPD_DRAW_ALIGN_LEFT, false);
    }
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    xteink_sync_status_t status;
    xteink_sync_get_status(&status);

    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "云同步", EPD_DRAW_ALIGN_CENTER, false);
    if (status.message[0])
        ui_text_vc(fb, 342, 158, 21, status.message, EPD_DRAW_ALIGN_CENTER, false);

    if (!status.bound) {
        if (s_qr[0])
            ui_wifi_qr_draw(fb, CLOUD_QR_AREA);
        else
            ui_text_vc(fb, 342, 430, 24, "正在生成绑定二维码…", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 800, 20, "请用「阅星曈」App 扫码绑定", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 858, 19, "绑定后这里会显示云端列表", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 1085, 19, "左上角返回 · 扫码中请稍候",
                   EPD_DRAW_ALIGN_CENTER, false);
        return;
    }

    int count = xteink_sync_task_count();
    if (!count) {
        ui_text_vc(fb, 342, 430, 24,
                   status.busy ? "正在拉取云端列表…" : "云端没有待下载的内容",
                   EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 500, 19, "点下方「刷新列表」重新拉取",
                   EPD_DRAW_ALIGN_CENTER, false);
    } else {
        ui_text_vc(fb, 342, 190, 18,
                   status.busy ? "同步中，请稍候" : "点选一项开始下载",
                   EPD_DRAW_ALIGN_CENTER, false);
        int shown = count > CLOUD_ROWS ? CLOUD_ROWS : count;
        for (int i = 0; i < shown; ++i) {
            xteink_task_t task;
            if (!xteink_sync_task_get(i, &task)) break;
            draw_row(fb, i, &task);
        }
        if (count > CLOUD_ROWS) {
            char more[64];
            snprintf(more, sizeof(more), "共 %d 项，仅显示前 %d 项", count, CLOUD_ROWS);
            ui_text_vc(fb, 342, 856, 18, more, EPD_DRAW_ALIGN_CENTER, false);
        }
    }

    ui_draw_button(fb, CLOUD_REFRESH_RECT, status.busy ? "同步中…" : "刷新列表", false);
    ui_draw_button(fb, CLOUD_UNBIND_RECT, "解绑", false);
    ui_text_vc(fb, 342, 1122, 18, "左上角返回 · 点一项只下载这一项",
               EPD_DRAW_ALIGN_CENTER, false);
}

static app_redraw_t action_at(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (y < 160 && x < 120) {
        ctx->request_return = true;
        return APP_REDRAW_NONE;
    }
    xteink_sync_status_t status;
    xteink_sync_get_status(&status);
    if (!status.bound) {
        if (!xteink_sync_busy() && y > 180) {
            xteink_sync_start();
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (ui_rect_hit(CLOUD_REFRESH_RECT, x, y)) {
        if (!xteink_sync_busy()) xteink_sync_start();
        return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit(CLOUD_UNBIND_RECT, x, y)) {
        xteink_sync_unbind();
        return APP_REDRAW_PAGE;
    }
    if (xteink_sync_busy()) return APP_REDRAW_NONE;
    int count = xteink_sync_task_count();
    for (int i = 0; i < count && i < CLOUD_ROWS; ++i) {
        xteink_task_t task;
        if (!xteink_sync_task_get(i, &task)) break;
        if (task.imported) continue;
        if (ui_rect_hit(row_rect(i), x, y)) {
            xteink_sync_download(i);
            return APP_REDRAW_PAGE;
        }
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t cloud_tick(app_ctx_t* ctx) {
    (void)ctx;
    xteink_sync_status_t status;
    xteink_sync_get_status(&status);
    sync_qr(&status);
    unsigned imported = xteink_sync_import_count();
    if (imported != s_seen_imports) {
        s_seen_imports = imported;
        book_store_notify_changed();
    }
    if (!status.bound && !xteink_sync_busy() && status.state != XTEINK_SYNC_BINDING &&
        status.state != XTEINK_SYNC_WAITING)
        xteink_sync_start();
    if (status.revision == s_seen_revision) return APP_REDRAW_NONE;
    s_seen_revision = status.revision;
    return APP_REDRAW_PAGE;
}

static bool cloud_edge_back(app_ctx_t* ctx) {
    (void)ctx;
    return true;
}

static app_redraw_t cloud_gesture(app_ctx_t* ctx, const ui_gesture_event_t* event) {
    if (event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    return action_at(ctx, event->x0, event->y0);
}

const app_desc_t app_cloud_sync = {
    .title = "云同步 Cloud sync",
    .detail = "星曈云端下发内容 / Xteink cloud content",
    .enter_full = true,
    .binary_ui = false,
    .edge_back_enabled = cloud_edge_back,
    .on_enter = cloud_enter,
    .on_exit = cloud_exit,
    .on_media_lost = cloud_media_lost,
    .on_before_lock = cloud_before_lock,
    .render = render,
    .on_tick = cloud_tick,
    .on_gesture = cloud_gesture,
};

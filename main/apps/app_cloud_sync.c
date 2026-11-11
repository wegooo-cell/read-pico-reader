/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户2026-11-10：每次进入先复用WiFi配网页，确认STA连接成功再申请绑定/拉列表；保留列表选择和取消清理。
 * User2026-11-10: provision WiFi on every entry and confirm a ready STA before binding/list requests; retain selection and cleanup.
 * 中文：云同步页：未绑定显示二维码；绑定后显示云端列表，单击勾选后同步所选，或一键同步全部待同步项；可刷新/解绑。
 *       每行书名下是进度条，右侧是大小或「下载失败 / 校验失败 / 已导入」。
 * English: Cloud sync page: QR while unbound, then the cloud list with selected or explicitly requested all-pending downloads,
 *       refresh and unbind. Under each name is a progress bar; the right side shows size or
 *       download/verify/import state.
 *
 * 冻结：不自动下载，用户明确点击一键同步才批量下载；校验失败不落盘；解绑只清本地令牌；render 只绘制。
 * Frozen: batch downloads require an explicit one-click sync action; failed checks are never kept; unbind only
 *       clears local tokens; render paints only.
 */
#include <stdio.h>
// 网络/页面控制按体积编译，保留Pico原4MiB应用槽；协议、校验与任务时序不变。
// Size-optimize IO/page control to retain Pico's4MiB slots without changing protocol,verification or task timing.
#if defined(ESP_PLATFORM) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("Os")
#endif
#include <string.h>

#include "app.h"
#include "app_registry.h"
#include "app_transfer_mode.h"
#include "read_pico_transfer.h"
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
#define CLOUD_SYNC_RECT ((EpdRect){232,976,232,84})
#define CLOUD_REFRESH_RECT ((EpdRect){36, 976, 288, 84})
#define CLOUD_UNBIND_RECT ((EpdRect){360, 976, 288, 84})

static int s_page;
static uint32_t s_selected;
static EpdRect refresh_rect(uint32_t pending){return pending?(EpdRect){36,976,184,84}:CLOUD_REFRESH_RECT;}
static EpdRect unbind_rect(uint32_t pending){return pending?(EpdRect){476,976,172,84}:CLOUD_UNBIND_RECT;}

extern const app_desc_t app_transfer;
static bool s_wifi_stage,s_wifi_live;
static bool s_resume_refresh;
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

// 在同一导入页内复用已有配网 UI，保留返回来源及密码键盘的原刷新/手势。
// Embed existing provisioning so returning retains the caller and keyboard refresh/gestures.
static void cloud_wifi_open(app_ctx_t* ctx) {
    s_wifi_stage=s_wifi_live=true;
    app_transfer_request_wifi_setup();
    if(app_transfer.on_enter)app_transfer.on_enter(ctx);
}
static void cloud_wifi_close(app_ctx_t* ctx) {
    if(s_wifi_live&&app_transfer.on_exit)app_transfer.on_exit(ctx);
    s_wifi_live=false;
}
static bool cloud_navigation_visible(app_ctx_t* ctx) {(void)ctx;return s_wifi_stage;}
static bool cloud_present(app_ctx_t* ctx,app_redraw_t redraw) {
    return s_wifi_live&&app_transfer.present&&app_transfer.present(ctx,redraw);
}
static bool cloud_back_swipe_area(app_ctx_t* ctx,const ui_gesture_event_t* event) {
    return !s_wifi_live||!app_transfer.back_swipe_area||app_transfer.back_swipe_area(ctx,event);
}
static app_redraw_t cloud_key(app_ctx_t* ctx,int key) {
    if(s_wifi_live&&app_transfer.on_key)return app_transfer.on_key(ctx,key);
    ctx->request_return=true;return APP_REDRAW_NONE;
}

static void cloud_enter(app_ctx_t* ctx) {
    (void)ctx;
    s_seen_revision = 0;s_page=0;s_selected=0;s_resume_refresh=false;
    s_seen_imports = xteink_sync_import_count();
    cloud_wifi_open(ctx);
}

static void cloud_exit(app_ctx_t* ctx) {
    cloud_wifi_close(ctx);s_wifi_stage=false;
    if(xteink_sync_import_count()!=s_seen_imports)book_store_notify_changed();
    s_selected=0;xteink_sync_close();
    ui_wifi_qr_clear();
    s_qr[0] = 0;
}

static void cloud_media_lost(app_ctx_t* ctx) {
    cloud_wifi_close(ctx);s_wifi_stage=false;s_resume_refresh=false;s_selected=0;
    xteink_sync_close();ui_wifi_qr_clear();s_qr[0]=0;
}

static void cloud_before_lock(app_ctx_t* ctx) {
    cloud_wifi_close(ctx);s_wifi_stage=true;s_resume_refresh=true;s_selected=0;
    xteink_sync_cancel_join();
}

// 一行：书名 + 右侧状态；第二行左侧进度条或状态文字。
// One row: name plus right-side state; the second line is the bar or a state label.
static void draw_row(uint8_t* fb, int index, const xteink_task_t* task, bool selected) {
    EpdRect r = row_rect(index);
    ui_fill_round_rect(fb, r, 22, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, r, 22, 0x70);

    EpdRect check={r.x+16,r.y+21,24,24};
    ui_draw_control_frame(fb,check,5,task->imported?0xc0:0x70);
    if(selected)epd_fill_rect((EpdRect){check.x+5,check.y+5,14,14},0,fb);
    char name[96];
    fit_utf8(name, sizeof(name), task->name, 60);
    ui_text_box(fb,(EpdRect){r.x+56,r.y+12,r.width-252,42},21,name,EPD_DRAW_ALIGN_LEFT,false);

    unsigned percent = 0;
    if (task->downloading && task->size)
        percent = (unsigned)((uint64_t)task->received * 100u / task->size);
    if (percent > 100) percent = 100;

    char right[32];
    if (task->downloading) {
        snprintf(right, sizeof(right), "%u%%", percent);
    } else if (task->failed) {
        snprintf(right,sizeof(right),"下载失败");
    } else if (task->imported) {
        snprintf(right, sizeof(right), "已导入");
    } else {
        format_size(right, sizeof(right), task->size);
    }
    ui_text_box(fb,(EpdRect){r.x+r.width-180,r.y+12,156,42},20,right,EPD_DRAW_ALIGN_RIGHT,false);

    int bar_x = r.x + 24;
    int bar_y = r.y + 68;
    int bar_w = r.width - 48;
    if (task->downloading) {
        epd_draw_rect((EpdRect){bar_x, bar_y, bar_w, 14}, 0, fb);
        unsigned filled = (unsigned)((uint64_t)(bar_w - 4) * percent / 100u);
        if (filled)
            epd_fill_rect((EpdRect){bar_x + 2, bar_y + 2, (int)filled, 10}, 0, fb);
    } else if (task->failed) {
        ui_text_box(fb,(EpdRect){bar_x,bar_y-9,bar_w,36},19,task->error[0]?task->error:"下载失败",EPD_DRAW_ALIGN_LEFT,false);
    } else if (task->imported) {
        ui_text(fb, bar_x, bar_y - 4, 19, task->is_image ? "已保存到 Pictures" : task->is_font ? "已导入字体" : "已导入书架",
                EPD_DRAW_ALIGN_LEFT, false);
    } else {
        ui_text(fb, bar_x, bar_y - 4, 19, task->is_image ? "图片待下载 · Pictures" : task->is_font ? "字体待下载" : "待下载",
                EPD_DRAW_ALIGN_LEFT, false);
    }
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    if(s_wifi_live){app_transfer.render(ctx,fb);return;}
    if(s_wifi_stage){ui_clear_page(fb);ui_nav_status(fb);ui_text_vc(fb,342,430,24,"正在准备 WiFi 连接…",EPD_DRAW_ALIGN_CENTER,false);return;}
    xteink_sync_status_t status;
    xteink_sync_get_status(&status);

    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "阅星瞳APP导入", EPD_DRAW_ALIGN_CENTER, false);
    if (status.message[0])
        ui_text_box(fb,(EpdRect){36,138,612,40},21,status.message,EPD_DRAW_ALIGN_CENTER,false);

    if (!status.bound) {
        if (s_qr[0])
            ui_wifi_qr_draw(fb, CLOUD_QR_AREA);
        else
            ui_text_vc(fb, 342, 430, 24, status.busy?"正在生成绑定二维码…":"请点击下方按钮重试", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 800, 20, "请用阅星瞳APP扫码绑定", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 858, 19, status.state==XTEINK_SYNC_FAILED&&s_qr[0]?"扫码后点「重试连接」获取列表":"绑定后这里会显示云端列表", EPD_DRAW_ALIGN_CENTER, false);
        ui_draw_button(fb,(EpdRect){180,976,324,84},status.busy?(s_qr[0]?"等待扫码…":"连接中…"):(s_qr[0]?"重试连接":"刷新二维码"),false);
        return;
    }

    uint32_t pending=xteink_sync_pending_mask(),selected=s_selected&pending;
    int count = xteink_sync_task_count();
    if (!count) {
        ui_text_vc(fb, 342, 430, 24,
                   status.busy ? "正在拉取云端列表…" : "云端没有待下载的内容",
                   EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 500, 19, "点下方「刷新列表」重新拉取",
                   EPD_DRAW_ALIGN_CENTER, false);
    } else {
        char hint[96];
        if(status.busy)snprintf(hint,sizeof(hint),"同步中，请稍候");
        else if(selected)snprintf(hint,sizeof(hint),"已选择%u项，点「同步所选」",(unsigned)__builtin_popcount(selected));
        else snprintf(hint,sizeof(hint),"点选单项，或一键同步全部");
        ui_text_vc(fb,342,190,18,hint,EPD_DRAW_ALIGN_CENTER,false);
        int pages=(count+CLOUD_ROWS-1)/CLOUD_ROWS;
        if(s_page>=pages)s_page=pages-1;
        int shown=count-s_page*CLOUD_ROWS;if(shown>CLOUD_ROWS)shown=CLOUD_ROWS;
        for (int i = 0; i < shown; ++i) {
            xteink_task_t task;
            if (!xteink_sync_task_get(s_page*CLOUD_ROWS+i, &task)) break;
            draw_row(fb,i,&task,(selected&(UINT32_C(1)<<(s_page*CLOUD_ROWS+i)))!=0);
        }
        char label[64];snprintf(label,sizeof(label),"%d/%d · 共%d项",s_page+1,pages,count);
        ui_pager_draw_at(fb,label,s_page,pages,false,-1,852);
    }

    ui_draw_button(fb, refresh_rect(pending), status.busy ? "同步中…" : "刷新列表", false);
    if(pending)ui_draw_button(fb,CLOUD_SYNC_RECT,status.busy?"同步中…":selected?"同步所选":"一键同步",false);
    ui_draw_button(fb, unbind_rect(pending), "解绑", false);
    ui_text_vc(fb, 342, 1122, 18, "选择书籍、字体或图片后同步",
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
        if(!status.busy&&(ui_rect_hit(CLOUD_QR_AREA,x,y)||ui_rect_hit((EpdRect){180,976,324,84},x,y))) {
            xteink_sync_start();return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    uint32_t pending=xteink_sync_pending_mask();s_selected&=pending;
    int count=xteink_sync_task_count(),pages=(count+CLOUD_ROWS-1)/CLOUD_ROWS;
    int action=ui_pager_hit_at(x,y,852);
    int target=ui_pager_target(action,s_page,pages);
    if(target>=0&&target!=s_page){s_page=target;return APP_REDRAW_PAGE;}

    if (ui_rect_hit(refresh_rect(pending), x, y)) {
        if (!xteink_sync_busy()){s_selected=0;xteink_sync_start();}
        return APP_REDRAW_PAGE;
    }
    if(pending&&ui_rect_hit(CLOUD_SYNC_RECT,x,y)) {
        if(!xteink_sync_busy())xteink_sync_download_selected(s_selected?s_selected:pending);
        return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit(unbind_rect(pending), x, y)) {
        s_selected=0;
        if(xteink_sync_unbind()==0)xteink_sync_start();
        return APP_REDRAW_PAGE;
    }
    if (xteink_sync_busy()) return APP_REDRAW_NONE;
    count=xteink_sync_task_count();
    for (int i = 0; i < CLOUD_ROWS && s_page*CLOUD_ROWS+i < count; ++i) {
        xteink_task_t task;
        if (!xteink_sync_task_get(s_page*CLOUD_ROWS+i, &task)) break;
        if (task.imported) continue;
        if (ui_rect_hit(row_rect(i), x, y)) {
            s_selected^=UINT32_C(1)<<(s_page*CLOUD_ROWS+i);
            return APP_REDRAW_PAGE;
        }
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t cloud_tick(app_ctx_t* ctx) {
    if(s_resume_refresh&&!xteink_sync_busy()) {
        s_resume_refresh=false;cloud_wifi_open(ctx);return APP_REDRAW_PAGE;
    }
    if(s_wifi_live) {
        app_redraw_t redraw=app_transfer.on_tick?app_transfer.on_tick(ctx):APP_REDRAW_NONE;
        if(ctx->request_return||ctx->request_app||ctx->request_menu)return redraw;
        read_pico_transfer_status_t network;
        read_pico_transfer_get_status(&network);
        if(!ctx->consumed&&network.network_ready&&network.mode==READ_PICO_TRANSFER_MODE_STA&&network.state==READ_PICO_TRANSFER_READY) {
            cloud_wifi_close(ctx);s_wifi_stage=false;
            xteink_sync_start();return APP_REDRAW_PAGE;
        }
        return redraw;
    }
    xteink_sync_status_t status;
    xteink_sync_get_status(&status);
    sync_qr(&status);
    s_selected&=xteink_sync_pending_mask();
    unsigned imported = xteink_sync_import_count();
    if (imported != s_seen_imports) {
        s_seen_imports = imported;
        book_store_notify_changed();
    }
    if (status.revision == s_seen_revision) return APP_REDRAW_NONE;
    s_seen_revision = status.revision;
    return APP_REDRAW_PAGE;
}

static bool cloud_edge_back(app_ctx_t* ctx) {
    if(s_wifi_live&&app_transfer.edge_back_enabled)return app_transfer.edge_back_enabled(ctx);
    (void)ctx;
    return true;
}

static app_redraw_t cloud_gesture(app_ctx_t* ctx, const ui_gesture_event_t* event) {
    if(s_wifi_live)return app_transfer.on_gesture?app_transfer.on_gesture(ctx,event):APP_REDRAW_NONE;
    if (event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    return action_at(ctx, event->x0, event->y0);
}

const app_desc_t app_cloud_sync = {
    .title = "阅星瞳APP导入",
    .detail = "阅星瞳APP下发内容 / Xteink app content",
    .enter_full = true,
    .binary_ui = false,
    .persistent_navigation=true,.navigation_visible=cloud_navigation_visible,
    .present=cloud_present,.back_swipe_area=cloud_back_swipe_area,.on_key=cloud_key,
    .edge_back_enabled = cloud_edge_back,
    .on_enter = cloud_enter,
    .on_exit = cloud_exit,
    .on_media_lost = cloud_media_lost,
    .on_before_lock = cloud_before_lock,
    .render = render,
    .on_tick = cloud_tick,
    .on_gesture = cloud_gesture,
};

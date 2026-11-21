/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：双板共享已确认的书架管理绘制；选择模式底栏图标统一黑色，只绘图，操作由所属页面回调处理。
 * English: Approved bookshelf-management painting shared by both boards; selection-footer icons stay black and actions stay in page callbacks.
 */
#pragma once
typedef enum {
    COVER_WIFI_NO_RETURN, COVER_WIFI_BACK, COVER_WIFI_CONNECTED,
} cover_wifi_result_t;

enum { COVER_ROWS = 7, COVER_ROW_HEIGHT = 88 };
typedef enum { COVER_CONNECT_IDLE, COVER_CONNECT_START, COVER_CONNECT_WAIT } cover_connection_t;
static cover_wifi_result_t s_cover_wifi_result;
static int *s_cover_entries;
static unsigned s_cover_count, s_cover_scan, s_cover_page;
static bool s_cover_follow, s_cover_filtering, s_cover_started;
static unsigned s_cover_choice = BOOK_WEREAD_NO_ROW;
static cover_connection_t s_cover_connection;
static int64_t s_cover_connect_deadline;
static unsigned s_weread_applied;
static book_weread_cover_status_t s_cover_status;
static char s_cover_error[96];
static const EpdRect cover_back = {30, 74, 70, 70};
static const EpdRect cover_prev = {36, 968, 94, 62};
static const EpdRect cover_next = {554, 968, 94, 62};
static const EpdRect cover_done = {36, 1060, 612, 74};
static const EpdRect cover_retry = {36, 1060, 294, 74};
static const EpdRect cover_exit = {346, 1060, 302, 74};
static const EpdRect cover_body = {36, 174, 612, 863};

void app_book_return_from_cover_wifi(bool connected) {
    s_cover_wifi_result = connected ? COVER_WIFI_CONNECTED : COVER_WIFI_BACK;
}

static void cover_lookup_cleanup(void) {
    book_weread_cover_cancel();
    free(s_cover_entries);
    s_cover_entries = NULL;
    s_cover_count = s_cover_scan = s_cover_page = 0;
    s_cover_filtering = s_cover_started = false;
    s_cover_choice = BOOK_WEREAD_NO_ROW;
    s_cover_connection = COVER_CONNECT_IDLE;
    s_cover_status = (book_weread_cover_status_t){0};
    s_cover_error[0] = 0;
}

static bool cover_station_ready(const read_pico_transfer_status_t *wifi) {
    return wifi->network_ready && wifi->mode == READ_PICO_TRANSFER_MODE_STA;
}

static bool cover_station_compatible(const read_pico_transfer_status_t *wifi) {
    return cover_station_ready(wifi) && (!KIIKO_BOARD_METALIO47 || wifi->cover_lookup);
}

static void cover_lookup_open(void) {
    if (s_view != COVER_LOOKUP) invalidate_covers();
    cover_lookup_cleanup();
    read_pico_transfer_status_t wifi = {0};
    read_pico_transfer_get_status(&wifi);
    if (!cover_station_compatible(&wifi)) s_cover_connection = COVER_CONNECT_START;
    s_view = COVER_LOOKUP;
    s_cover_filtering = s_cover_follow = true;
}

static app_redraw_t cover_connection_tick(app_ctx_t *ctx) {
    read_pico_transfer_status_t wifi = {0};
    read_pico_transfer_get_status(&wifi);
    if (cover_station_compatible(&wifi)) {
        s_cover_connection = COVER_CONNECT_IDLE;
        return APP_REDRAW_PAGE;
    }
    if (s_cover_connection == COVER_CONNECT_START) {
        if (!read_pico_transfer_try_stop_if_idle()) {
            copy_text(s_cover_error, sizeof(s_cover_error), "Wi‑Fi 正在使用，请稍后重试");
        } else {
            ttf_font_cache_clear();
            const read_pico_transfer_cfg_t settings = {
                .mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true, .cover_lookup = true,
            };
            if (read_pico_transfer_start(&settings) == ESP_OK) {
                s_cover_connect_deadline = esp_timer_get_time() / 1000 + 18000;
                s_cover_connection = COVER_CONNECT_WAIT;
                return APP_REDRAW_NONE;
            }
            copy_text(s_cover_error, sizeof(s_cover_error), "无法连接 Wi‑Fi，请重试");
        }
    } else if (wifi.state == READ_PICO_TRANSFER_ERROR || wifi.state == READ_PICO_TRANSFER_STOPPED ||
               ctx->now_ms >= s_cover_connect_deadline) {
        copy_text(s_cover_error, sizeof(s_cover_error), "Wi‑Fi 连接失败，请重试");
    } else {
        return APP_REDRAW_NONE;
    }
    s_cover_connection = COVER_CONNECT_IDLE;
    s_cover_filtering = false;
    return APP_REDRAW_PAGE;
}

static app_redraw_t bulk_cover_start(app_ctx_t *ctx) {
    if (book_weread_cover_status().active || pico_online_busy()) {
        copy_text(s_batch_message, sizeof(s_batch_message), "请等待当前联网任务结束");
        return APP_REDRAW_PAGE;
    }
    if (!read_pico_sd_io_ready()) {
        copy_text(s_batch_message, sizeof(s_batch_message), "请插入并挂载 TF 卡");
        return APP_REDRAW_PAGE;
    }
    s_batch_message[0] = 0;
    read_pico_transfer_status_t wifi = {0};
    read_pico_transfer_get_status(&wifi);
    if (cover_station_ready(&wifi)) {
        cover_lookup_open();
        return APP_REDRAW_PAGE;
    }
    extern const app_desc_t app_transfer;
    app_transfer_request_cover_wifi();
    ctx->request_app = &app_transfer;
    return APP_REDRAW_NONE;
}

static bool cover_lookup_resume(void) {
    const cover_wifi_result_t result = s_cover_wifi_result;
    if (result == COVER_WIFI_NO_RETURN) return false;
    s_cover_wifi_result = COVER_WIFI_NO_RETURN;
    s_pressed_control = -1;
    s_presented_view = -1;
    s_sort_menu = false;
    if (!s_shelf_cache_valid || s_store_revision != book_store_revision()) s_scan_pending = true;
    s_view = BULK;
    if (result == COVER_WIFI_BACK) return true;
    cover_lookup_open();
    return true;
}

static unsigned cover_lookup_pages(void) {
    return s_cover_count ? 1 + (s_cover_count - 1) / COVER_ROWS : 1;
}

static EpdRect cover_choice_card(unsigned option) {
    return (EpdRect){36, 278 + (int)option * 174, 612, 152};
}

static const char *cover_row_label(book_weread_cover_row_t row) {
    switch (row.state) {
        case BOOK_WEREAD_CHECKING: return "获取中";
        case BOOK_WEREAD_NOT_FOUND: return "未匹配";
        case BOOK_WEREAD_MULTIPLE: return "多个结果 ›";
        case BOOK_WEREAD_SAVING: return "正在保存";
        case BOOK_WEREAD_SAVED: return "已保存";
        case BOOK_WEREAD_FAILED: return row.error == ESP_ERR_TIMEOUT ? "连接超时" : "获取失败";
        default: return s_cover_status.running ? "待获取" : "未获取";
    }
}

static void cover_choices_draw_body(uint8_t *fb) {
    ui_fill_rect_fast(cover_body, UI_GRAY_WHITE, fb);
    if (s_cover_choice >= s_cover_count) return;
    const shelf_entry_t *entry = &s_shelf[s_cover_entries[s_cover_choice]];
    char name[256];
    copy_text(name, sizeof(name), entry->name);
    const int px = ui_text_title_fit(name, 28, 600, entry->name);
    ui_text_title_vc(fb, 342, 205, px, name, entry->name, EPD_DRAW_ALIGN_CENTER);
    const book_weread_cover_row_t row = book_weread_cover_row(s_cover_choice);
    const char *message = "点一下要保存的封面";
    if (row.state == BOOK_WEREAD_SAVING) message = "正在保存封面…";
    else if (row.state == BOOK_WEREAD_SAVED) message = "封面已保存到 TF 卡";
    else if (row.error == ESP_ERR_TIMEOUT) message = "封面下载超时，请重试";
    else if (row.error != ESP_OK) message = "保存失败，请重新选择";
    else if (s_cover_status.saving) message = "正在保存另一本书，请稍候";
    else if (!row.options) message = "获取已结束，请返回";
    ui_text_vc(fb, 342, 244, 21, message, EPD_DRAW_ALIGN_CENTER, false);

    uint8_t *preview = heap_caps_malloc(BOOK_WEREAD_PREVIEW_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (unsigned index = 0; index < row.options; ++index) {
        book_weread_cover_option_t option;
        if (!book_weread_cover_option(s_cover_choice, index, &option, preview)) break;
        const EpdRect card = cover_choice_card(index);
        const EpdRect image = {card.x + 28, card.y + 27, BOOK_WEREAD_PREVIEW_W, BOOK_WEREAD_PREVIEW_H};
        ui_draw_round_rect(fb, card, 16, 0xa0);
        if (preview && option.preview_ready) {
            for (int y = 0; y < image.height; ++y) {
                for (int x = 0; x < image.width; ++x) {
                    const uint8_t tone = ui_contrast_gray(preview[y * image.width + x]);
                    epd_draw_pixel(image.x + x, image.y + y,
                                   ui_image_dither_gray(tone, image.x + x, image.y + y), fb);
                }
            }
        } else {
            ui_fill_rect_fast(image, UI_GRAY_LIGHT, fb);
            ui_text_vc(fb, image.x + image.width / 2, image.y + image.height / 2, 16,
                       option.preview_checked ? "无预览" : "加载中", EPD_DRAW_ALIGN_CENTER, false);
        }
        copy_text(name, sizeof(name), option.title);
        const int title_px = ui_text_title_fit(name, 28, 446, option.title);
        ui_text_title_vc(fb, 164, card.y + 55, title_px, name, option.title, EPD_DRAW_ALIGN_LEFT);
        if (*option.author) {
            copy_text(name, sizeof(name), option.author);
            const int author_px = ui_text_title_fit(name, 21, 446, option.author);
            ui_text_title_vc(fb, 164, card.y + 106, author_px, name, option.author, EPD_DRAW_ALIGN_LEFT);
        }
        if (option.from_zlib)
            ui_text_vc(fb, 608, card.y + 127, 17, "Z-Library", EPD_DRAW_ALIGN_RIGHT, false);
        if (row.state == BOOK_WEREAD_MULTIPLE && !s_cover_status.saving)
            ui_text_vc(fb, 624, card.y + card.height / 2, 26, "›", EPD_DRAW_ALIGN_CENTER, false);
    }
    free(preview);
}

static void cover_lookup_draw_body(uint8_t *fb) {
    char summary[112];
    const bool working = s_cover_filtering || s_cover_status.running;
    if (s_cover_connection != COVER_CONNECT_IDLE)
        copy_text(summary, sizeof(summary), "正在准备联网获取封面…");
    else if (s_cover_filtering)
        copy_text(summary, sizeof(summary), "正在筛选缺少封面的 TXT");
    else if (s_cover_error[0])
        copy_text(summary, sizeof(summary), s_cover_error);
    else if (!s_cover_count)
        copy_text(summary, sizeof(summary), "没有缺少封面的 TXT");
    else if (working)
        snprintf(summary, sizeof(summary), "已检查 %u/%u · 已保存 %u · 待选择 %u",
                 s_cover_status.done, s_cover_count, s_cover_status.saved, s_cover_status.multiple);
    else
        snprintf(summary, sizeof(summary), "已保存 %u 本，待选择 %u 本",
                 s_cover_status.saved, s_cover_status.multiple);
    ui_fill_rect_fast(cover_body, UI_GRAY_WHITE, fb);
    ui_text_vc(fb, 342, 215, 25, summary, EPD_DRAW_ALIGN_CENTER, false);
    if (!s_cover_count || s_cover_filtering) return;
    if (s_cover_status.zlib_unavailable)
        ui_text_vc(fb, 342, 242, 17, "Z-Library 暂不可用", EPD_DRAW_ALIGN_CENTER, false);

    EpdRect bar = {36, 256, 612, 8};
    ui_fill_rect_fast(bar, UI_GRAY_LIGHT, fb);
    bar.width = bar.width * s_cover_status.done / s_cover_count;
    if (bar.width) ui_fill_rect_fast(bar, UI_GRAY_BLACK, fb);
    const unsigned start = s_cover_page * COVER_ROWS;
    for (unsigned row = 0; row < COVER_ROWS && start + row < s_cover_count; ++row) {
        const unsigned index = start + row;
        const int top = 296 + row * COVER_ROW_HEIGHT;
        const bool active = s_cover_status.running && index == s_cover_status.done;
        if (active) ui_fill_round_rect(fb, (EpdRect){36, top, 612, COVER_ROW_HEIGHT - 4}, 10, UI_GRAY_LIGHT);
        const shelf_entry_t *entry = &s_shelf[s_cover_entries[index]];
        char name[256];
        copy_text(name, sizeof(name), entry->name);
        const int px = ui_text_title_fit(name, 26, 452, entry->name);
        ui_text_title_vc(fb, 54, top + 42, px, name, entry->name, EPD_DRAW_ALIGN_LEFT);
        const book_weread_cover_row_t state = s_cover_started ? book_weread_cover_row(index) :
            (book_weread_cover_row_t){0};
        const char *label = active && state.state == BOOK_WEREAD_WAITING ? "获取中" : cover_row_label(state);
        ui_text_vc(fb, 626, top + 42, 21, label, EPD_DRAW_ALIGN_RIGHT, false);
        ui_hairline(fb, top + 85, 36, 612, UI_GRAY_LIGHT);
    }
    if (cover_lookup_pages() > 1) {
        char page[32];
        snprintf(page, sizeof(page), "%u / %u", s_cover_page + 1, cover_lookup_pages());
        ui_text_vc(fb, 342, 999, 22, page, EPD_DRAW_ALIGN_CENTER, false);
        if (s_cover_page) ui_draw_button(fb, cover_prev, "‹", false);
        if (s_cover_page + 1 < cover_lookup_pages()) ui_draw_button(fb, cover_next, "›", false);
    }
}

static bool cover_retry_enabled(void) {
    return !s_cover_filtering && !s_cover_status.running && !s_cover_status.saving &&
        (s_cover_error[0] || s_cover_status.saved < s_cover_count);
}

static void draw_cover_lookup(uint8_t *fb) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    const bool choices = s_cover_choice != BOOK_WEREAD_NO_ROW;
    ui_text_vc(fb, 342, 107, 34, choices ? "选择封面" : "获取封面", EPD_DRAW_ALIGN_CENTER, false);
    ui_hairline(fb, 168, 36, 612, 0x70);
    if (choices) cover_choices_draw_body(fb);
    else cover_lookup_draw_body(fb);
    if (choices) {
        ui_draw_button(fb, cover_done, "返回获取封面", false);
    } else {
        if (cover_retry_enabled()) ui_draw_button(fb, cover_retry, "重试", true);
        else {
            ui_draw_round_rect(fb, cover_retry, UI_BTN_RADIUS, UI_GRAY_LIGHT);
            ui_text_vc(fb, cover_retry.x + cover_retry.width / 2, cover_retry.y + cover_retry.height / 2,
                       UI_PX_BTN, "重试", EPD_DRAW_ALIGN_CENTER, false);
        }
        ui_draw_button(fb, cover_exit, s_cover_filtering || s_cover_status.running || s_cover_status.saving ?
                       "停止并返回" : "返回书架管理", false);
    }
}

static app_redraw_t cover_lookup_back(void) {
    if (s_cover_choice != BOOK_WEREAD_NO_ROW) {
        s_cover_choice = BOOK_WEREAD_NO_ROW;
        book_weread_cover_focus(BOOK_WEREAD_NO_ROW);
        return APP_REDRAW_PAGE;
    }
    cover_lookup_cleanup();
    s_view = BULK;
    return APP_REDRAW_PAGE;
}

static app_redraw_t cover_lookup_page(int direction) {
    if (s_cover_choice != BOOK_WEREAD_NO_ROW || (direction < 0 && !s_cover_page) ||
        (direction > 0 && s_cover_page + 1 >= cover_lookup_pages())) return APP_REDRAW_NONE;
    s_cover_page += direction;
    s_cover_follow = false;
    return APP_REDRAW_PAGE;
}

static app_redraw_t cover_lookup_gesture(const ui_gesture_event_t *ev) {
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (ui_rect_hit(cover_back, ev->x0, ev->y0)) return cover_lookup_back();
    if (s_cover_choice != BOOK_WEREAD_NO_ROW) {
        if (ui_rect_hit(cover_done, ev->x0, ev->y0)) return cover_lookup_back();
        const book_weread_cover_row_t row = book_weread_cover_row(s_cover_choice);
        for (unsigned i = 0; i < row.options; ++i) {
            if (ui_rect_hit(cover_choice_card(i), ev->x0, ev->y0) && book_weread_cover_choose(s_cover_choice, i)) {
                s_cover_status = book_weread_cover_status();
                return APP_REDRAW_PAGE;
            }
        }
        return APP_REDRAW_NONE;
    }
    if (ui_rect_hit(cover_exit, ev->x0, ev->y0)) return cover_lookup_back();
    if (ui_rect_hit(cover_retry, ev->x0, ev->y0) && cover_retry_enabled()) {
        cover_lookup_open();
        return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit(cover_prev, ev->x0, ev->y0)) return cover_lookup_page(-1);
    if (ui_rect_hit(cover_next, ev->x0, ev->y0)) return cover_lookup_page(1);
    if (ev->x0 >= 36 && ev->x0 < 648 && ev->y0 >= 296 && ev->y0 < 296 + COVER_ROWS * COVER_ROW_HEIGHT) {
        const unsigned index = s_cover_page * COVER_ROWS + (ev->y0 - 296) / COVER_ROW_HEIGHT;
        if (index < s_cover_count && book_weread_cover_row(index).state == BOOK_WEREAD_MULTIPLE) {
            s_cover_follow = false;
            s_cover_choice = index;
            book_weread_cover_focus(index);
            return APP_REDRAW_PAGE;
        }
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t cover_lookup_filter(app_ctx_t *ctx) {
    if (!read_pico_sd_io_ready()) {
        copy_text(s_cover_error, sizeof(s_cover_error), "TF 卡不可用，请重新插入");
    } else if (s_scan_pending) {
        read_pico_sd_info_t info = {0};
        if (read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        int leaf = ctx->leaf;
        s_scan_pending = false;
        scan_shelf(ctx);
        ctx->leaf = leaf;
        return APP_REDRAW_NONE;
    } else if (s_count && !s_cover_entries) {
        s_cover_entries = heap_caps_malloc((size_t)s_count * sizeof(*s_cover_entries),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_cover_entries) copy_text(s_cover_error, sizeof(s_cover_error), "内存不足，请稍后重试");
    }

    if (!s_cover_error[0]) {
        const int64_t deadline = esp_timer_get_time() + 14000;
        while (s_cover_scan < (unsigned)s_count) {
            unsigned index = s_cover_scan++;
            const shelf_entry_t *entry = &s_shelf[index];
            if (!entry->removed && !entry->is_dir && book_weread_cover_is_txt(entry->path) &&
                !book_weread_cover_metadata(entry->path, NULL, 0, NULL, 0))
                s_cover_entries[s_cover_count++] = (int)index;
            if (esp_timer_get_time() >= deadline) return APP_REDRAW_NONE;
        }
    }
    s_cover_filtering = false;
    return APP_REDRAW_PAGE;
}

static app_redraw_t bulk_cover_tick(app_ctx_t *ctx) {
    const book_weread_cover_status_t status = book_weread_cover_status();
    if (status.active && (s_view != COVER_LOOKUP || ctx->request_app || pico_online_busy()))
        book_weread_cover_cancel();
    if (s_view != COVER_LOOKUP) {
        const unsigned revision = book_weread_cover_revision();
        if (!status.active && revision != s_weread_applied) {
            s_weread_applied = revision;
            metalio_cover_cache_clear();
            app_home_cover_mode_changed();
            book_lock_collage_invalidate();
            book_store_notify_changed();
            invalidate_covers();
            s_shelf_cache_valid = false;
            s_scan_pending = true;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (ctx->consumed || (ctx->touch && ctx->touch->touched)) return APP_REDRAW_NONE;
    if (s_cover_connection != COVER_CONNECT_IDLE) return cover_connection_tick(ctx);
    if (s_cover_filtering) return cover_lookup_filter(ctx);
    if (!s_cover_started && s_cover_count && !s_cover_error[0]) {
        if (status.active) return APP_REDRAW_NONE;
        book_weread_path_t *paths = heap_caps_malloc(s_cover_count * sizeof(*paths),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (paths) {
            for (unsigned i = 0; i < s_cover_count; ++i)
                copy_text(paths[i], sizeof(*paths), s_shelf[s_cover_entries[i]].path);
            if (book_weread_cover_start(paths, s_cover_count) == ESP_OK) {
                s_cover_started = true;
                s_cover_status = book_weread_cover_status();
                return APP_REDRAW_PAGE;
            }
            free(paths);
        }
        copy_text(s_cover_error, sizeof(s_cover_error), "无法开始获取，请重试");
        return APP_REDRAW_PAGE;
    }
    if (!s_cover_started || status.changes == s_cover_status.changes) return APP_REDRAW_NONE;
    const bool scan_finished = s_cover_status.running && !status.running;
    const bool controls_changed = status.running != s_cover_status.running || status.saving != s_cover_status.saving;
    s_cover_status = status;
    if (s_cover_follow && s_cover_count) {
        unsigned active = status.done < s_cover_count ? status.done : s_cover_count - 1;
        if (scan_finished && status.multiple) {
            for (unsigned row = 0; row < status.done; ++row) {
                if (book_weread_cover_row(row).state != BOOK_WEREAD_MULTIPLE) continue;
                active = row;
                break;
            }
        }
        s_cover_page = active / COVER_ROWS;
    }
    if (status.error != ESP_OK) {
        const char *msg = status.error == ESP_ERR_NOT_FINISHED ? "已停止" :
            status.error == ESP_ERR_TIMEOUT ? "连接封面源超时" : "获取中断";
        snprintf(s_cover_error, sizeof(s_cover_error), "%s，已保存 %u 本，待选择 %u 本",
                 msg, status.saved, status.multiple);
    }
    if (controls_changed) return APP_REDRAW_PAGE;
    if (s_cover_choice != BOOK_WEREAD_NO_ROW) cover_choices_draw_body(ctx->fb);
    else cover_lookup_draw_body(ctx->fb);
    s_area = cover_body;
    s_mode = MODE_GL16;
    return APP_REDRAW_AREA;
}

static void bulk_draw_title(uint8_t *fb,const char *name,EpdRect box,int preferred,bool grid,uint8_t background){
    char line[256];copy_text(line,sizeof(line),name);
    int px=ui_text_title_fit(line,preferred,box.width,name);
    const char *rest=name+strlen(line);
    int x=box.x+(grid?box.width/2:0);
    enum EpdFontFlags align=grid?EPD_DRAW_ALIGN_CENTER:EPD_DRAW_ALIGN_LEFT;
    ui_text_title_background_vc(fb,x,box.y+(*rest?16:box.height/2),px,line,name,align,background);
    if(*rest){
        copy_text(line,sizeof(line),rest);ui_text_title_fit(line,px,box.width,name);
        ui_text_title_background_vc(fb,x,box.y+49,px,line,name,align,background);
    }
}

static void bulk_button(uint8_t *fb,EpdRect box,const char *text,bool active,int px){
    ui_fill_round_rect(fb,box,14,active?UI_GRAY_BLACK:UI_GRAY_WHITE);
    ui_draw_control_frame(fb,box,14,UI_GRAY_BLACK);
    ui_text_box(fb,ui_inset_rect(box,6),px,text,EPD_DRAW_ALIGN_CENTER,active);
}
static void bulk_draw_footer(uint8_t *fb){
    ui_fill_rect_fast((EpdRect){0,1096,684,120},0xe0,fb);ui_hairline(fb,1096,0,684,0xa0);
    if(s_import_candidates){
        char label[48];snprintf(label,sizeof(label),book_import_running()?"正在导入（%u项）":"导入所选（%u项）",(unsigned)selected_count());
        bulk_button(fb,(EpdRect){36,1124,196,64},"取消导入",false,24);
        bulk_button(fb,(EpdRect){250,1124,398,64},label,true,25);return;
    }
    const char *file_labels[]={"重命名","删除文件","移动位置","导入书架"};
    const char *shelf_labels[]={"重命名","移出书架","清理进度","重新扫描"};
    const ui_icon_t file_icons[]={UI_ICON_TYPE,UI_ICON_TRASH,UI_ICON_FOLDER_INPUT,UI_ICON_LIBRARY_BIG};
    const ui_icon_t shelf_icons[]={UI_ICON_TYPE,UI_ICON_LIBRARY_BIG,UI_ICON_ROTATE_CW,UI_ICON_REFRESH_CW};
    for(int i=0;i<4;++i){EpdRect r=batch_rect(i+3);int cx=r.x+r.width/2;
        if(s_pressed_control==613+i)ui_fill_rect_fast(r,0xc0,fb);
        ui_draw_icon(fb,cx,1141,42,s_manage_books?file_icons[i]:shelf_icons[i],UI_GRAY_BLACK);
        ui_text_box(fb,(EpdRect){r.x,1170,r.width,30},17,s_manage_books?file_labels[i]:shelf_labels[i],EPD_DRAW_ALIGN_CENTER,false);
    }
}
// 作者限制在书名和百分比之间，按实际固定字体宽度省略。/ Fit authors between title and percent using the actual fixed-font width.
static void bulk_draw_author(uint8_t *fb,const char *source,int y){
    char author[160];copy_text(author,sizeof(author),source);
    if(ui_text_fixed_width_px(18,author)>152){
        fit_fixed_text(author,18,152-ui_text_fixed_width_px(18,"…"));
        size_t used=strlen(author);if(used+sizeof("…")<=sizeof(author))memcpy(author+used,"…",sizeof("…"));
        fit_fixed_text(author,18,152);
    }
    ui_text_fixed_ink_vc(fb,578,y,18,author,EPD_DRAW_ALIGN_RIGHT,0x60);
}
static void draw_bulk(uint8_t *fb,int leaf){
    ui_clear_page(fb);ui_nav_status(fb);
    ui_text_box(fb,(EpdRect){s_import_candidates?110:36,s_import_candidates?70:80,s_import_candidates?392:480,s_import_candidates?64:76},s_import_candidates?42:52,s_import_candidates?"选择书籍导入":"书架管理",EPD_DRAW_ALIGN_LEFT,false);
    if(s_import_candidates){
        ui_draw_icon(fb,64,102,40,UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT,0x30);
        ui_draw_icon(fb,552,112,36,s_bulk_grid?UI_ICON_LIST:UI_ICON_LAYOUT_GRID,0x30);
        ui_draw_icon(fb,616,112,34,UI_ICON_SEARCH,0x30);
        ui_draw_icon(fb,66,164,28,books_directory_is_root()?UI_ICON_FOLDER_OPEN:UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT,0x40);
        char location[288];
        if(books_directory_is_root())copy_text(location,sizeof(location),"TF卡 / books");
        else snprintf(location,sizeof(location),"TF卡 / books / %s",books_directory_name());
        fit_text(location,24,530);ui_text_box(fb,(EpdRect){96,146,530,36},24,location,EPD_DRAW_ALIGN_LEFT,false);
    }else{
        bulk_button(fb,ui_book_manage_get_cover(),"获取封面",false,21);
        ui_draw_icon(fb,552,112,34,UI_ICON_SEARCH,0x30);
        ui_draw_icon(fb,616,112,36,s_bulk_grid?UI_ICON_LIST:UI_ICON_LAYOUT_GRID,0x30);
        for(int i=0;i<2;++i){
            char label[96];
            if(!i&&s_manage_books&&!books_directory_is_root())snprintf(label,sizeof(label),"Books目录 / %s",books_directory_name());
            else copy_text(label,sizeof(label),i?"已上架书籍":"Books目录");
            fit_text(label,26,270);
            bulk_button(fb,ui_book_manage_tab(i),label,s_manage_books==(i==0),26);
        }
    }
    char count[80];snprintf(count,sizeof(count),"%u项 · 已选%u项",(unsigned)s_visible_count,(unsigned)selected_count());
    if(!s_import_candidates)snprintf(count,sizeof(count),s_manage_books?"%u项":"%u本",(unsigned)s_visible_count);
    int y=s_import_candidates?194:246;
    if(s_manage_books&&!s_import_candidates&&!books_directory_is_root())bulk_button(fb,ui_book_manage_parent(),"‹ 上一级",false,20);
    else ui_text_box(fb,(EpdRect){36,y,s_import_candidates?350:128,48},18,count,EPD_DRAW_ALIGN_LEFT,false);
    if(!s_import_candidates&&!s_manage_books)draw_shelf_order_button(fb,ui_book_manage_sort(),111);
    bulk_button(fb,s_import_candidates?ui_book_import_select():ui_book_manage_select(),"本页全选",false,21);
    bulk_button(fb,s_import_candidates?ui_book_import_cancel():ui_book_manage_cancel(),"取消本页",false,20);
    ui_hairline(fb,s_import_candidates?242:306,36,612,0xdc);
    for(int row=0;row<shelf_rows();++row){
        int index=leaf*shelf_rows()+row;if(index>=s_visible_count)break;
        const shelf_entry_t *item=&s_shelf[index];EpdRect r=row_rect(row);
        bool grid=s_bulk_grid,covered=s_import_candidates&&book_import_selection(item->path)==BOOK_IMPORT_COVERED;
        bool disabled=s_import_candidates&&(item->shelved||covered);
        bool selected=item->selected;uint8_t ink=disabled?0xa0:0x60;
        char name[288];const char *base=strrchr(item->path,'/');
        copy_text(name,sizeof(name),s_manage_books||s_import_candidates?(base?base+1:item->path):item->name);
        if(item->is_dir){
            if(grid){ui_draw_icon(fb,r.x+72,r.y+82,88,UI_ICON_FOLDER_OPEN,0x40);fit_text(name,22,144);ui_text_box(fb,(EpdRect){r.x,r.y+172,144,28},22,name,EPD_DRAW_ALIGN_CENTER,false);}
            else{
                ui_draw_icon(fb,s_import_candidates?88:49,r.y+r.height/2,25,UI_ICON_FOLDER_OPEN,0x40);
                fit_text(name,26,s_import_candidates?480:528);ui_text_box(fb,(EpdRect){s_import_candidates?110:70,r.y,s_import_candidates?480:528,r.height},26,name,EPD_DRAW_ALIGN_LEFT,false);
                ui_text_vc(fb,636,r.y+r.height/2,28,"›",EPD_DRAW_ALIGN_CENTER,false);ui_hairline(fb,r.y+r.height-1,36,612,0xe0);
            }
            if(s_import_candidates){int mx=grid?r.x+129:48,my=grid?r.y+16:r.y+r.height/2;
                if(grid)ui_fill_round_rect(fb,(EpdRect){mx-11,my-11,22,22},11,255);
                epd_draw_circle(mx,my,grid?9:10,ink,fb);if(selected)epd_fill_circle(mx,my,grid?4:5,disabled?0xb0:0,fb);
            }
            continue;
        }
        if(grid){
            EpdRect image=shelf_cover_image(row);ui_fill_rect_fast(image,UI_GRAY_WHITE,fb);
            if(s_covers[row].index==index&&s_covers[row].gray){
                book_crop_t crop=book_cover_crop(s_covers[row].width,s_covers[row].height,image.width,image.height);
                for(int cy=0;cy<image.height;++cy)for(int cx=0;cx<image.width;++cx){
                    int sx=crop.x+cx*crop.width/image.width,sy=crop.y+cy*crop.height/image.height;
                    uint8_t tone=ui_contrast_gray(s_covers[row].gray[sy*s_covers[row].width+sx]);
                    epd_draw_pixel(image.x+cx,image.y+cy,ui_image_dither_gray(tone,image.x+cx,image.y+cy),fb);
                }
            }else ui_draw_icon(fb,image.x+image.width/2,image.y+image.height/2,30,UI_ICON_BOOK_OPEN,0x70);
            ui_draw_control_frame(fb,image,0,0x50);
            if(!s_import_candidates){
            char value[16];snprintf(value,sizeof(value),"%u%%",item->has_progress?(item->pct>100?100:item->pct):0);
            int w=ui_text_fixed_width_px(22,value)+16;if(w>image.width-10)w=image.width-10;
            EpdRect badge={image.x+5,image.y+image.height-37,w,32};ui_fill_round_rect(fb,badge,16,0);
            ui_text_fixed_vc(fb,badge.x+w/2,badge.y+16,22,value,EPD_DRAW_ALIGN_CENTER,true);
            }
            fit_text(name,22,144);ui_text_box(fb,(EpdRect){r.x,r.y+172,144,28},22,name,EPD_DRAW_ALIGN_CENTER,false);
        }else{
            int width=s_import_candidates?480:s_manage_books?390:340;
            fit_text(name,26,width);
            ui_text_box(fb,(EpdRect){70,r.y,width,r.height},26,name,EPD_DRAW_ALIGN_LEFT,false);
            if(s_manage_books||s_import_candidates)ui_text_fixed_ink_vc(fb,s_import_candidates?648:578,r.y+r.height/2,18,covered?"目录已选":item->shelved?"已上架":"未上架",EPD_DRAW_ALIGN_RIGHT,disabled?0xa0:0x60);
            else bulk_draw_author(fb,item->author,r.y+r.height/2);
            if(!s_import_candidates){
                char value[16];snprintf(value,sizeof(value),"%u%%",item->has_progress?(item->pct>100?100:item->pct):0);
                ui_text_fixed_ink_vc(fb,648,r.y+r.height/2,18,value,EPD_DRAW_ALIGN_RIGHT,0x40);
            }
            ui_hairline(fb,r.y+r.height-1,36,612,0xe0);
        }
        int mx=grid?r.x+129:48,my=grid?r.y+16:r.y+r.height/2;
        if(grid)ui_fill_round_rect(fb,(EpdRect){mx-11,my-11,22,22},11,255);
        epd_draw_circle(mx,my,grid?9:10,ink,fb);if(selected||disabled)epd_fill_circle(mx,my,grid?4:5,disabled?0xb0:0,fb);
    }
    if(!s_visible_count)ui_text_box(fb,(EpdRect){52,450,580,100},26,s_scan_pending?"正在加载书籍…":s_shelf_warning[0]?s_shelf_warning:s_message[0]?s_message:"当前筛选没有图书",EPD_DRAW_ALIGN_CENTER,false);
    char page[64];snprintf(page,sizeof(page),"第%d页 / 共%d页",leaf+1,leaves());
    int pressed=s_pressed_control>=100&&s_pressed_control<=103?s_pressed_control-100:UI_PAGER_NONE;
    ui_pager_draw_at(fb,page,leaf,leaves(),false,pressed,s_import_candidates?1028:1006);
    if(s_batch_message[0])ui_text_box(fb,(EpdRect){36,1070,612,24},18,s_batch_message,EPD_DRAW_ALIGN_CENTER,false);
    bulk_draw_footer(fb);draw_batch_confirmation(fb);if(s_sort_menu)draw_bulk_sort(fb);
}

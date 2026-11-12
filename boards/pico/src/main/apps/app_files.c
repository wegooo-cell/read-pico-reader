/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 用户2026-10-20修订：文件二级页常驻灰底栏，操作与分页恢复原高度，边缘返回沿用取消与目录回退。
 * User revision2026-10-20: file children retain gray navigation with original footer/control heights; edge back retains cancellation and directory backtracking.
 *
 * 中文：TF 卡文件入口、目录浏览及 USB/Wi-Fi 传输入口。
 * English: SD file entry, directory browser and USB/Wi-Fi transfer routes.
 * 用户修订：文件管理传输区提供 WiFi、热点、USB 和阅星瞳APP导入四个入口。
 * User revision: show direct WiFi, hotspot, USB and Xteink app import routes.
 * 用户授权新输入法：文件改名使用共用九宫格/全键盘及常亮光标；离页释放候选。
 * Authorized keyboard revision: rename uses shared T9/QWERTY and a steady caret; exit frees candidates.
 * 用户修订：输入只刷新变化区域，布局切换局部灰阶，避免打字累计整屏清残影。
 * User revision: input refreshes changed regions; layout switches use local grayscale, without accumulating whole-page cleanup.
 */
#ifdef ESP_PLATFORM
#include "ui_locale_format.h"
#endif
#include "ui_avatar.h"
#include "book_cover_jobs.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "app_content_open.h"
#include "app_transfer_mode.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_title.h"
#include "book_added.h"
#include "display.h"
#include "file_tree.h"
#include "file_search.h"
#include "read_pico_sd.h"
#include "read_pico_search.h"
#include "settings.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "e0470_epaper_waveform.h"
#include "esp_timer.h"
#include "ui_nav.h"
#include "ui_files_layout.h"
#include "ui_root_style.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"

#define FILE_MAX 512
#define FILE_ROWS 9
typedef struct { char path[288]; off_t size; int64_t modified; bool is_dir, selected; } file_item_t;
// 文件操作始终用完整路径中的基名，显示名可截短但不能反过来重建路径。
// File operations use the full basename from the real path; never rebuild a path from a clipped label.
static const char *file_item_name(const file_item_t *item) {
    const char *name=strrchr(item->path,'/');return name?name+1:item->path;
}
// 文件列表放 PSRAM：内部 RAM 已被静态占用到约 90%，而 BLE 控制器只能用内部 RAM，
// 列表这类大数组没必要跟它抢。
// The file list lives in PSRAM. Internal RAM is already around 90% statically used and the BLE
// controller can only use internal RAM, so a list this big has no business holding any of it.
static file_item_t *s_files;

static bool files_alloc(void) {
    if (!s_files)
        s_files = heap_caps_calloc(FILE_MAX, sizeof(file_item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_files != NULL;
}
static int s_count, s_folder, s_page;
static int s_requested_folder = -1;
static int s_counts[3];
static file_search_t *s_book_count_search;
static int s_book_count_total;
static bool s_book_count_dirty,s_book_count_incomplete;
static bool s_scan_pending;
static int s_pager_pressed=UI_PAGER_NONE;
static bool s_pager_paint_pending;
static int64_t s_space_poll_ms;
static char s_message[96];
static char s_dir[288] = "/sdcard";
static read_pico_sd_info_t s_sd;
// 卡在、挂不上、又不是超时，多半是文件系统本机不认（exFAT 之类）。这种卡仍然格得动，
// 所以卡片本身变成可点的：第一次点只是问，第二次点才真的格。
// A card is in the slot but will not mount and it is not a timeout: most likely a filesystem this
// firmware does not read, exFAT for instance. Such a card can still be formatted, so the card
// itself becomes a tap target -- the first tap asks, the second one does it.
static bool s_sd_format_confirm;
// 格式化尝试的反馈，显示在卡片的信息行上；为空时回到提示语。
// Feedback from a format attempt, shown on the card's info line; empty falls back to the hint.
static char s_sd_notice[64];
typedef enum { FILE_VIEW_LIST, FILE_VIEW_ACTIONS, FILE_VIEW_RENAME, FILE_VIEW_MOVE, FILE_VIEW_SORT, FILE_VIEW_BATCH_DELETE, FILE_VIEW_DIR_OPTIONS, FILE_VIEW_SEARCH } file_view_t;
static file_view_t s_view;
static bool s_file_grid;
static char s_file_query[65],s_query_draft[65];
static file_search_t *s_file_search;
static char s_search_root[288];
static unsigned s_search_matches,s_search_errors;
static int64_t s_search_painted_ms;
static EpdRect file_search_rect(void){return (EpdRect){36,160,612,64};}
static EpdRect file_layout_rect(void){return (EpdRect){584,79,64,66};}
// 仅映射可见索引，不压缩原数组；筛选不会改变选中项的文件身份。
// Map visible indices without compacting the source array, preserving selected file identities.
static bool file_matches(int i){return s_view==FILE_VIEW_MOVE||!s_file_query[0]||read_pico_search_match(file_item_name(&s_files[i]),s_file_query);}
static int file_visible_count(void){int n=0;for(int i=0;i<s_count;++i)if(file_matches(i))++n;return n;}
static int file_visible_index(int n){for(int i=0;i<s_count;++i)if(file_matches(i)&&!n--)return i;return -1;}
static EpdRect file_entry_rect(int row){
    if(s_file_grid&&s_view!=FILE_VIEW_MOVE)return (EpdRect){36+(row%3)*210,UI_FILES_LIST_TOP+(row/3)*218,192,202};
    return (EpdRect){36,UI_FILES_LIST_TOP+row*UI_FILES_ROW_HEIGHT,612,UI_FILES_ROW_HEIGHT};
}
static int file_entry_hit(int x,int y){for(int row=0;row<(s_file_grid?9:UI_FILES_ROWS);++row)if(ui_rect_hit(file_entry_rect(row),x,y))return row;return -1;}

static file_item_t s_selected;
static file_item_t *s_batch;
static unsigned s_batch_count, s_batch_index, s_batch_ok, s_batch_failed;
static bool s_file_selecting, s_batch_running, s_batch_move;
static char s_batch_target[288];
static int s_file_sort;
static bool s_file_desc;
static unsigned file_selected_count(void) {
    unsigned count=0;for(int i=0;i<s_count;++i)if(s_files[i].selected)++count;return count;
}
static EpdRect file_select_rect(void) { return (EpdRect){560,244,88,46}; }
static EpdRect file_sort_button(void) { return (EpdRect){426,244,122,46}; }
static EpdRect file_batch_button(int i) { return ui_files_batch_rect(i+1); }
static EpdRect file_sort_rect(int i) {
    if(i<3)return (EpdRect){60,342+i*112,564,100};
    if(i<5)return (EpdRect){60+(i-3)*290,700,274,66};
    return (EpdRect){60,836,564,72};
}
static void file_selection_restore(void) {
    if(!s_file_selecting)return;
    for(int i=0;i<s_count;++i)for(unsigned j=0;j<s_batch_count;++j)
        if(s_batch[j].selected&&!strcmp(s_files[i].path,s_batch[j].path))s_files[i].selected=true;
}

static bool s_delete_confirm;
static int s_move_origin_folder, s_move_origin_page;
static char s_move_origin_dir[288];
static char s_editor[121], s_editor_ext[16], s_editor_notice[96];
static ui_text_edit_t s_editor_input;
static void scan_folder(int folder);
static void rescan_current(void);
static void file_search_start(void);
static void file_search_stop(void);
static void file_book_count_invalidate(void);
static app_redraw_t file_search_apply(void);
static void fit_name(char *name,int px,int max_width);
static void file_card_border(uint8_t *fb,EpdRect rect,int radius);
static void file_rule(uint8_t *fb,int y,int x0,int x1);
static bool main_page_visible(app_ctx_t *ctx);
static bool s_render_secondary;
// 紧凑按钮按实际盒子缩字，保留大字号可读性。/ Fit compact labels within their boxes at large UI scales.
static void file_management_button(uint8_t *fb,EpdRect rect,const char *label,bool selected) {
    ui_draw_choice_round_rect(fb,rect,UI_BTN_RADIUS,selected);
    ui_text_box(fb,(EpdRect){rect.x+10,rect.y+4,rect.width-20,rect.height-8},26,label,EPD_DRAW_ALIGN_CENTER,false);
}
static void files_navigation(uint8_t *fb) {
    if(s_file_selecting && s_view==FILE_VIEW_LIST){
        ui_fill_rect_fast((EpdRect){0,1096,684,120},0xe0,fb);ui_hairline(fb,1096,0,684,0xa0);
        const char *labels[]={"本页全选","移动至","删除","取消"};
        const ui_icon_t icons[]={UI_ICON_COPY,UI_ICON_FOLDER_INPUT,UI_ICON_TRASH,UI_ICON_X};
        for(int i=0;i<4;++i){EpdRect r=ui_files_batch_rect(i);int cx=r.x+r.width/2;
            if(i==0)epd_draw_circle(cx,1141,18,UI_GRAY_BLACK,fb);
            else ui_draw_icon(fb,cx,1141,44,icons[i],UI_GRAY_BLACK);
            ui_text_box(fb,(EpdRect){r.x,1170,r.width,30},17,labels[i],EPD_DRAW_ALIGN_CENTER,false);
        }
        return;
    }
    ui_nav_draw(fb,2);
}
static int files_nav_hit(uint16_t x,uint16_t y) {
    return ui_nav_hit(x,y);
}

static const char *const names[] = {"书籍", "壁纸", "Fonts", "TF 卡根目录"};
static const char *folder_root(int folder) {
    if (folder == 0) return app_settings_books_dir();
    if (folder == 1) return "/sdcard/pictures";
    if (folder == 2) return app_settings_fonts_dir();
    return s_dir;
}
static int file_pager_top(void) {
    return 1006;
}
static EpdRect file_action_rect(int index) {
    int top=875;
    return (EpdRect){index?355:36,top,293,76};
}
static int file_message_top(void) {
    return 978;
}
static int file_sheet_offset(void) {
    return 0;
}
static int visible_rows(void) { return s_view==FILE_VIEW_MOVE?(file_action_rect(0).y-16-214)/88:s_file_grid?9:UI_FILES_ROWS; }
static int root_rows(void) { return 6; }
static int file_pager_pages(void) {
    int rows=s_folder<0?root_rows():visible_rows();
    int count=file_visible_count();return count?1+(count-1)/rows:1;
}
static void draw_file_pager(uint8_t *fb) {
    char label[48];snprintf(label,sizeof(label),"第 %d 页 · 共 %d 页",s_page+1,file_pager_pages());
    ui_pager_draw_at(fb,label,s_page,file_pager_pages(),false,s_pager_pressed,file_pager_top());
}
static app_redraw_t file_pager_paint(app_ctx_t *ctx) {
    ui_fill_rect_fast(ui_pager_area_at(file_pager_top()),UI_GRAY_WHITE,ctx->fb);
    draw_file_pager(ctx->fb);s_pager_paint_pending=true;
    return APP_REDRAW_AREA;
}
// 四键共用命中和目标计算；只在抬手提交，同一按钮外移/长按/取消只恢复箭头。
// Share hits/targets across four controls; commit on release, while leaving/holding/cancelling only restores arrows.
static bool file_pager_event(app_ctx_t *ctx,const ui_gesture_event_t *ev,app_redraw_t *redraw) {
    if((s_view!=FILE_VIEW_LIST&&s_view!=FILE_VIEW_MOVE)||(s_view==FILE_VIEW_LIST&&s_folder<0))return false;
    int start=ui_pager_hit_at(ev->x0,ev->y0,file_pager_top()),end=ui_pager_hit_at(ev->x,ev->y,file_pager_top());
    if(start<0&&s_pager_pressed<0)return false;
    *redraw=APP_REDRAW_NONE;
    if(s_scan_pending)return true;
    if(ev->type==UI_GESTURE_PRESS){s_pager_pressed=start;*redraw=file_pager_paint(ctx);return true;}
    if(ev->type==UI_GESTURE_MOVE&&start==end)return true;
    bool decorated=s_pager_pressed>=0;s_pager_pressed=UI_PAGER_NONE;
    if(ev->type==UI_GESTURE_TAP&&start>=0&&start==end){
        int target=ui_pager_target(start,s_page,file_pager_pages());
        if(target!=s_page){s_page=target;*redraw=APP_REDRAW_PAGE;return true;}
    }
    if(decorated)*redraw=file_pager_paint(ctx);
    return true;
}
static EpdRect home_transfer_rect(int index) { return ui_files_transfer_rect(index); }

// 存储卡卡片的位置，绘制与命中测试共用一份，免得两处各写一个坐标。
// The storage card's rect, shared by drawing and hit testing so the two cannot drift apart.
static EpdRect storage_card_rect(void) { return ui_files_storage_rect(); }

// 需要格式化：卡在、没挂上、而且不是超时。超时是接触或供电问题，格式化解决不了，那种情况下
// 说"格式化可能可以解决"会把用户带偏。
// Needs formatting: the card is present, will not mount, and the failure is not a timeout. A
// timeout is a contact or power problem that formatting cannot fix, so offering it there would
// point the user at the wrong thing.
static bool sd_needs_format(void) { return s_sd.present && !s_sd.mounted && s_sd.needs_format; }

static void copy_utf8(char *dst, size_t cap, const char *src) {
#ifdef ESP_PLATFORM
    src=ui_tr(src);
#endif

    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n); dst[n] = 0;
}

static bool is_book_path(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt"));
}

// 列表只取简短书名和作者，非书籍与详情继续用原基名；没有额外TF读取。
// List rows use short book titles/authors; other files and details keep the real basename, with no extra SD IO.
static void file_display_name(const file_item_t *item,char title[128],char author[128]) {
    author[0]=0;
    if(!item->is_dir&&is_book_path(item->path)&&book_name_from_path(item->path,title,128,author,128))return;
    copy_utf8(title,128,file_item_name(item));
}

static bool sibling_path(const char *name, char out[288]) {
    const char *slash = strrchr(s_selected.path, '/');
    if (!slash || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':')) return false;
    size_t parent = (size_t)(slash - s_selected.path);
    if (!name[0] || name[0] == '.' || parent + 1 + strlen(name) >= 288) return false;
    memcpy(out, s_selected.path, parent);
    out[parent] = '/';
    strcpy(out + parent + 1, name);
    return true;
}

static void migrate_book_state(const char *old_path, const char *new_path, uint32_t size) {
    if (!is_book_path(old_path)) return;
    book_added_migrate(old_path,new_path);
    book_progress_t progress = {0};
    bool had_progress = book_progress_load(old_path, size, &progress);
    char last[BOOK_STORE_PATH_MAX] = {0};
    bool was_last = book_progress_last_path(last, sizeof(last)) && !strcmp(last, old_path);
    if (had_progress) (void)book_progress_save(new_path, &progress);
    (void)book_progress_forget(old_path);
    if (was_last) (void)book_progress_set_last_path(new_path);
    (void)book_title_clear(old_path);
}

static esp_err_t copy_selected_item(char out_name[128]) {
    const char *name = file_item_name(&s_selected);
    const char *dot = s_selected.is_dir ? NULL : strrchr(name, '.');
    size_t stem_len = dot && dot != name ? (size_t)(dot - name) : strlen(name);
    char stem[288], candidate[288], destination[288];
    memcpy(stem, name, stem_len); stem[stem_len] = 0;
    const char *ext = dot && dot != name ? dot : "";
    for (int number = 1; number < 100; ++number) {
        int used = number == 1 ? snprintf(candidate, sizeof(candidate), "%s 副本%s", stem, ext) :
            snprintf(candidate, sizeof(candidate), "%s 副本 %d%s", stem, number, ext);
        if (used < 0 || used >= (int)sizeof(candidate)) return ESP_ERR_INVALID_SIZE;
        if (!sibling_path(candidate, destination)) return ESP_ERR_INVALID_ARG;
        struct stat exists;
        if (stat(destination, &exists) != 0) {
            if (errno == ENOENT) break;
            return ESP_FAIL;
        }
        if (number == 99) return ESP_ERR_INVALID_SIZE;
    }
    read_pico_sd_get_info(&s_sd);
    if (!s_sd.mounted) return ESP_ERR_INVALID_STATE;
    esp_err_t err = file_tree_copy(s_selected.path, destination, s_sd.free_bytes);
    if (err != ESP_OK) return err;
    copy_utf8(out_name, 128, candidate);
    book_store_notify_changed();
    return ESP_OK;
}

static bool rebased_path(const char *path, const char *old_path, const char *new_path,
                         char *out, size_t cap) {
    if (!file_tree_same_or_below(path, old_path)) return false;
    int n = snprintf(out, cap, "%s%s", new_path, path + strlen(old_path));
    return n >= 0 && (size_t)n < cap;
}

static bool moved_settings_fit(const char *old_path, const char *new_path) {
    char updated[288];
    const char *paths[] = {app_settings_books_dir(),app_settings_fonts_dir(),app_settings_system_font_path(),
        app_settings_font_path(), app_settings_wallpaper_path()};
    const size_t limits[] = {96,96,160,160,288};
    for (int i = 0; i < 5; ++i) {
        if (!file_tree_same_or_below(paths[i], old_path)) continue;
        if (!rebased_path(paths[i], old_path, new_path, updated, sizeof(updated)) ||
            strlen(updated) >= limits[i]) return false;
    }
    return true;
}

static void sync_moved_settings(const char *old_path, const char *new_path) {
    char updated[288];
    if(rebased_path(app_settings_books_dir(),old_path,new_path,updated,sizeof(updated)))
        (void)app_settings_set_books_dir(updated);
    if(rebased_path(app_settings_fonts_dir(),old_path,new_path,updated,sizeof(updated))){
        (void)app_settings_set_fonts_dir(updated);ttf_font_scan();
    }
    if (rebased_path(app_settings_system_font_path(), old_path, new_path, updated, sizeof(updated))) {
        app_settings_set_system_font_path(updated);
        app_font_activate_system();
    }
    if (rebased_path(app_settings_font_path(), old_path, new_path, updated, sizeof(updated)))
        app_settings_set_font_path(updated);
    if (rebased_path(app_settings_wallpaper_path(), old_path, new_path, updated, sizeof(updated)))
        app_settings_set_wallpaper_path(updated);
}

static void notify_moved_tree(const char *old_path, const char *new_path, unsigned depth) {
    if (depth > 8) return;
    struct stat st;
    if (stat(new_path, &st)) return;
    if (S_ISREG(st.st_mode)) {
        migrate_book_state(old_path, new_path,
            st.st_size >= 0 && (uint64_t)st.st_size <= UINT32_MAX ? (uint32_t)st.st_size : 0);
        return;
    }
    if (!S_ISDIR(st.st_mode)) return;
    DIR *dir = opendir(new_path);
    if (!dir) return;
    char *paths = heap_caps_malloc(576, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!paths) { closedir(dir); return; }
    char *old_child = paths, *new_child = paths + 288;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(old_child, 288, "%s/%s", old_path, entry->d_name) >= 288 ||
            snprintf(new_child, 288, "%s/%s", new_path, entry->d_name) >= 288) continue;
        notify_moved_tree(old_child, new_child, depth + 1);
    }
    free(paths);
    closedir(dir);
}

static void file_deleted(const char *path, bool directory, void *ctx) {
    (void)ctx;
    if (directory) return;
    if (is_book_path(path)) { (void)book_progress_forget(path); book_added_forget(path); }
    (void)book_title_clear(path);
    if (!strcmp(app_settings_system_font_path(), path)) {
        app_settings_set_system_font_path("");
        app_font_activate_system();
    }
    if (!strcmp(app_settings_font_path(), path)) app_settings_set_font_path("");
    if (!strcmp(app_settings_book_background_path(), path)) (void)app_settings_set_book_background_path("");
    if (!strcmp(app_settings_wallpaper_path(), path)) {
        app_settings_set_wallpaper_path("");
        app_settings_set_lock_style(0);
    }
}

static void repair_deleted_directories(void) {
    struct stat st;
    if (stat(app_settings_books_dir(), &st) || !S_ISDIR(st.st_mode)) {
        (void)mkdir("/sdcard/books", 0775);
        (void)app_settings_set_books_dir("/sdcard/books");
    }
    if (stat(app_settings_fonts_dir(), &st) || !S_ISDIR(st.st_mode)) {
        (void)mkdir("/sdcard/fonts", 0775);
        (void)app_settings_set_fonts_dir("/sdcard/fonts");
        ttf_font_scan();
    }
}

static void start_move(void) {
    if(!s_file_selecting)s_batch_count=0;
    s_move_origin_folder = s_folder;
    s_move_origin_page = s_page;
    copy_utf8(s_move_origin_dir, sizeof(s_move_origin_dir), s_dir);
    copy_utf8(s_dir, sizeof(s_dir), "/sdcard");
    s_folder = 3;
    s_view = FILE_VIEW_MOVE;
    scan_folder(3);
}

static void leave_move(bool changed) {
    s_folder = s_move_origin_folder;
    copy_utf8(s_dir, sizeof(s_dir), s_move_origin_dir);
    s_view = FILE_VIEW_LIST;
    scan_folder(s_folder >= 0 ? s_folder : 3);
    if (!changed) s_page = s_move_origin_page;
}

static app_redraw_t move_selected(void) {
    read_pico_sd_get_info(&s_sd);
    if (!s_sd.mounted) {
        snprintf(s_message, sizeof(s_message), "TF 卡不可用"); return APP_REDRAW_PAGE;
    }
    char destination[288];
    int path_len=snprintf(destination,sizeof(destination),"%s/%s",s_dir,file_item_name(&s_selected));
    if (path_len<0||(size_t)path_len>=sizeof(destination)) {
        snprintf(s_message, sizeof(s_message), "目标路径过长"); return APP_REDRAW_PAGE;
    }
    if (!strcmp(destination, s_selected.path)) {
        snprintf(s_message, sizeof(s_message), "已在此文件夹"); return APP_REDRAW_PAGE;
    }
    if (!moved_settings_fit(s_selected.path, destination)) {
        snprintf(s_message, sizeof(s_message), "设置路径过长，无法移动"); return APP_REDRAW_PAGE;
    }
    esp_err_t err = file_tree_move(s_selected.path, destination);
    if (err != ESP_OK) {
        snprintf(s_message, sizeof(s_message), err == ESP_ERR_INVALID_STATE ? "目标已有同名项目" :
                 err == ESP_ERR_INVALID_ARG ? "不能移入自身文件夹" : "移动失败，请检查 TF 卡");
        return APP_REDRAW_PAGE;
    }
    sync_moved_settings(s_selected.path, destination);
    notify_moved_tree(s_selected.path, destination, 0);
    book_store_notify_changed();
    leave_move(true);
    snprintf(s_message, sizeof(s_message), "移动完成");
    return APP_REDRAW_PAGE;
}

static bool file_batch_prepare(void) {
    if(!file_selected_count()) { snprintf(s_message,sizeof(s_message),"请先选择文件");return false; }
    if(!s_batch)s_batch=heap_caps_calloc(FILE_MAX,sizeof(*s_batch),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_batch){snprintf(s_message,sizeof(s_message),"内存不足，请减少选择后重试");return false;}
    s_batch_count=s_batch_index=s_batch_ok=s_batch_failed=0;
    for(int i=0;i<s_count;++i)if(s_files[i].selected){
        bool nested=false;for(int j=0;j<s_count&&!nested;++j)if(i!=j&&s_files[j].selected&&s_files[j].is_dir&&file_tree_same_or_below(s_files[i].path,s_files[j].path))nested=true;
        if(!nested)s_batch[s_batch_count++]=s_files[i];
    }
    return true;
}
static bool file_batch_begin(bool move) {
    file_search_stop();
    read_pico_sd_get_info(&s_sd);
    if(!s_sd.mounted||!s_batch_count){snprintf(s_message,sizeof(s_message),"TF 卡不可用或未选择文件");return false;}
    if(!book_cover_jobs_quiesce(2000)||!ui_avatar_quiesce(2000)){
        snprintf(s_message,sizeof(s_message),"资源正在使用，请稍后重试");return false;
    }
    if(move){
        for(unsigned i=0;i<s_batch_count;++i)if(s_batch[i].is_dir&&file_tree_same_or_below(s_dir,s_batch[i].path)){
            snprintf(s_message,sizeof(s_message),"不能移入自身文件夹");return false;
        }
        copy_utf8(s_batch_target,sizeof(s_batch_target),s_dir);
    }
    s_batch_move=move;s_batch_index=s_batch_ok=s_batch_failed=0;s_batch_running=true;
    s_view=move?FILE_VIEW_MOVE:FILE_VIEW_LIST;
    return true;
}
static app_redraw_t file_batch_tick(void) {
    if(s_batch_index<s_batch_count){
        file_item_t *item=&s_batch[s_batch_index++];
        esp_err_t err=ESP_ERR_INVALID_STATE;read_pico_sd_get_info(&s_sd);
        if(s_sd.mounted&&file_tree_same_or_below(item->path,"/sdcard")&&strcmp(item->path,"/sdcard")){
            if(s_batch_move){
                char target[288];int n=snprintf(target,sizeof(target),"%s/%s",s_batch_target,file_item_name(item));
                if(n<0||(size_t)n>=sizeof(target))err=ESP_ERR_INVALID_SIZE;
                else if(!moved_settings_fit(item->path,target))err=ESP_ERR_INVALID_SIZE;
                else{
                    err=file_tree_move(item->path,target);
                    if(err==ESP_OK){sync_moved_settings(item->path,target);notify_moved_tree(item->path,target,0);}
                }
            }else err=file_tree_delete(item->path,file_deleted,NULL);
        }
        if(err==ESP_OK){item->selected=false;++s_batch_ok;}else ++s_batch_failed;
        snprintf(s_message,sizeof(s_message),"%s %u/%u",s_batch_move?"正在移动":"正在删除",s_batch_index,s_batch_count);
        if(s_batch_index<s_batch_count)return APP_REDRAW_PAGE;
    }
    s_batch_running=false;
    if(!s_batch_move&&s_sd.mounted&&s_batch_ok)repair_deleted_directories();
    book_store_notify_changed();
    s_file_selecting=s_batch_failed!=0;
    if(s_batch_move)leave_move(true);else {s_view=FILE_VIEW_LIST;rescan_current();}
    file_selection_restore();
    snprintf(s_message,sizeof(s_message),"完成 %u 项%s%u 项",s_batch_ok,s_batch_failed?" · 失败 ":" · 剩余 ",s_batch_failed);
    return APP_REDRAW_PAGE;
}
static void render_file_batch_confirmation(uint8_t *fb) {
    EpdRect panel={36,300,612,580};ui_fill_round_rect(fb,panel,26,UI_GRAY_WHITE);file_card_border(fb,panel,26);
    char title[64];snprintf(title,sizeof(title),"删除所选 %u 项？",file_selected_count());
    ui_text_vc(fb,64,352,31,title,EPD_DRAW_ALIGN_LEFT,false);
    ui_text_vc(fb,64,412,20,"文件将从存储卡删除，无法撤销",EPD_DRAW_ALIGN_LEFT,false);
    unsigned row=0;for(int i=0;i<s_count&&row<3;++i)if(s_files[i].selected){
        char name[128];copy_utf8(name,sizeof(name),file_item_name(&s_files[i]));fit_name(name,23,530);
        ui_text_vc(fb,64,478+(int)row++*62,23,name,EPD_DRAW_ALIGN_LEFT,false);
    }
    ui_draw_button(fb,(EpdRect){60,760,270,76},"取消",false);
    ui_draw_button(fb,(EpdRect){354,760,270,76},"确认删除",true);
}
static void render_file_sort(uint8_t *fb) {
    EpdRect panel={36,230,612,710};ui_fill_round_rect(fb,panel,26,UI_GRAY_WHITE);file_card_border(fb,panel,26);
    ui_text_vc(fb,64,286,31,"排列文件",EPD_DRAW_ALIGN_LEFT,false);
    const char *labels[]={"名称","文件时间","文件大小"};
    for(int i=0;i<3;++i){EpdRect r=file_sort_rect(i);ui_text_vc(fb,r.x+8,r.y+45,28,labels[i],EPD_DRAW_ALIGN_LEFT,false);
        epd_draw_circle(r.x+r.width-27,r.y+44,13,0x80,fb);if(s_file_sort==i)epd_fill_circle(r.x+r.width-27,r.y+44,7,0x30,fb);
        file_rule(fb,r.x,r.y+r.height,r.width);}
    ui_draw_button(fb,file_sort_rect(3),"↑ 升序",!s_file_desc);ui_draw_button(fb,file_sort_rect(4),"↓ 降序",s_file_desc);
    ui_text_vc(fb,342,802,19,"文件夹始终优先",EPD_DRAW_ALIGN_CENTER,false);
    ui_draw_button(fb,file_sort_rect(5),"完成",true);
}
void app_files_request_folder(int folder) {
    if (folder >= 0 && folder < 3) s_requested_folder = folder;
}

static bool supported(int folder, const char *name) {
    if (!name[0] || name[0] == '.' || !strncmp(name, "._", 2)) return false;
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    if (folder == 0) return !strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt");
    if (folder == 1) return !strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".png") || !strcasecmp(ext, ".webp") || !strcasecmp(ext, ".xth");
    return !strcasecmp(ext, ".ttf") || !strcasecmp(ext, ".otf");
}

static int compare_files(const void *left, const void *right) {
    const file_item_t *a=left,*b=right;
    if(a->is_dir!=b->is_dir)return a->is_dir?-1:1;
    int order=0;
    if(s_file_sort==1&&a->modified!=b->modified)order=a->modified<b->modified?-1:1;
    else if(s_file_sort==2&&a->size!=b->size)order=a->size<b->size?-1:1;
    else order=strcasecmp(file_item_name(a),file_item_name(b));
    if(!order)order=strcmp(a->path,b->path);
    return s_file_desc?-order:order;
}

static void scan_folder(int folder) {
    if(s_file_query[0]&&s_view!=FILE_VIEW_MOVE){file_search_start();return;}
    file_search_stop();
    if (!files_alloc()) return;
    s_count = s_page = 0;
    s_message[0] = 0;
    read_pico_sd_get_info(&s_sd);
    if (!s_sd.mounted) { snprintf(s_message, sizeof(s_message), "TF 卡尚未挂载"); return; }
    const char *root = folder_root(folder);
    DIR *dir = opendir(root);
    if (!dir) { snprintf(s_message, sizeof(s_message), "目录不存在：%.60s", root); return; }
    struct dirent *entry;
    while (s_count < FILE_MAX && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !strncmp(entry->d_name, "._", 2)) continue;
        file_item_t *item = &s_files[s_count];
        item->selected=false;item->modified=0;
        int path_n=snprintf(item->path,sizeof(item->path),"%s/%s",root,entry->d_name);
        if(path_n<0||(size_t)path_n>=sizeof(item->path))continue;
        struct stat st;
        bool is_dir = false, is_file = false;
        if (!stat(item->path, &st)) {
            is_dir = S_ISDIR(st.st_mode);
            is_file = S_ISREG(st.st_mode);
            item->size = st.st_size;
            item->modified = (int64_t)st.st_mtime;
        } else {
            DIR *probe = opendir(item->path);
            if (probe) { is_dir = true; item->size = 0; closedir(probe); }
        }
        if (!is_dir && !is_file) continue;
        if (s_view == FILE_VIEW_MOVE && !is_dir) continue;
        // 书籍分类保留子文件夹，复用目录浏览进入任意层级。/ Keep book subfolders and reuse directory navigation at any depth.
        if (folder != 3 && !(folder == 0 && is_dir) && (!is_file || !supported(folder, entry->d_name))) continue;
        item->is_dir = is_dir;
        ++s_count;
    }
    closedir(dir);
    qsort(s_files, s_count, sizeof(s_files[0]), compare_files);
    file_selection_restore();
    if (folder > 0 && folder < 3) s_counts[folder] = s_count;
}

static void file_search_stop(void){file_search_close(s_file_search);s_file_search=NULL;file_book_count_invalidate();}
static void file_search_found(const char *path,const struct stat *info,void *ctx){
    (void)ctx;if(s_count>=FILE_MAX)return;
    file_item_t *item=&s_files[s_count++];memset(item,0,sizeof(*item));
    copy_utf8(item->path,sizeof(item->path),path);item->size=info->st_size;item->modified=info->st_mtime;item->is_dir=S_ISDIR(info->st_mode);
    if(s_file_selecting)for(unsigned j=0;j<s_batch_count;++j)
        if(s_batch[j].selected&&!strcmp(item->path,s_batch[j].path)){item->selected=true;break;}
}
static void file_search_notice(bool done){
    if(s_search_matches>FILE_MAX)snprintf(s_message,sizeof(s_message),"找到 %u 项，仅显示前 %u 项",s_search_matches,(unsigned)FILE_MAX);
    else if(s_search_errors)snprintf(s_message,sizeof(s_message),"部分目录未能读取，已找到 %u 项",s_search_matches);
    else if(done)snprintf(s_message,sizeof(s_message),"子文件夹搜索完成，共 %u 项",s_search_matches);
    else snprintf(s_message,sizeof(s_message),"正在搜索子文件夹，已找到 %u 项",s_search_matches);
}
static void file_search_start(void){
    file_search_stop();if(!files_alloc())return;
    if(s_file_selecting&&file_selected_count()&&!file_batch_prepare())return;
    s_scan_pending=false;
    s_count=s_page=0;s_search_matches=s_search_errors=0;
    if(!s_search_root[0])copy_utf8(s_search_root,sizeof(s_search_root),folder_root(s_folder));
    s_file_search=file_search_begin(s_search_root,s_file_query,file_search_found,NULL);
    if(!s_file_search)snprintf(s_message,sizeof(s_message),"无法搜索当前文件夹");else file_search_notice(false);
}
static app_redraw_t file_search_apply(void){
    ui_keyboard_end();s_view=FILE_VIEW_LIST;s_page=0;
    copy_utf8(s_file_query,sizeof(s_file_query),s_query_draft);
    copy_utf8(s_search_root,sizeof(s_search_root),folder_root(s_folder));
    scan_folder(s_folder);return APP_REDRAW_FULL;
}
static app_redraw_t file_search_clear(void){
    file_search_stop();s_file_query[0]=s_search_root[0]=0;scan_folder(s_folder);return APP_REDRAW_PAGE;
}
static void rescan_current(void) {
    scan_folder(s_folder >= 0 ? s_folder : 3);
}

// 书籍总数独立于当前目录/列表上限；空闲分片遍历，离开/写操作前关闭目录。
// Count books independently of list bounds; slice traversal when idle and close before leaving/writes.
static void file_book_count_invalidate(void){
    file_search_close(s_book_count_search);s_book_count_search=NULL;s_book_count_dirty=true;
}
static void file_book_count_visit(const char *path,const struct stat *info,void *ctx){
    (void)ctx;if(!S_ISREG(info->st_mode))return;
    const char *relative=path+strlen(folder_root(0));
    for(const char *p=relative;*p;++p)if(*p=='/'&&p[1]=='.')return;
    const char *name=strrchr(path,'/');
    if(supported(0,name?name+1:path))++s_book_count_total;
}
static bool file_book_count_tick(void){
    if(s_book_count_dirty){
        s_book_count_dirty=false;s_book_count_total=0;s_book_count_incomplete=false;
        s_book_count_search=file_search_begin_all(folder_root(0),file_book_count_visit,NULL);
        if(!s_book_count_search){s_counts[0]=0;s_book_count_incomplete=true;return true;}
    }
    if(!s_book_count_search)return false;
    file_search_step(s_book_count_search,24);
    file_search_stats_t stats=file_search_stats(s_book_count_search);
    if(!stats.done)return false;
    s_counts[0]=s_book_count_total;s_book_count_incomplete=stats.errors!=0;
    file_search_close(s_book_count_search);s_book_count_search=NULL;return true;
}
static void file_count_label(int folder,char *out,size_t size){
    if(folder==0&&(s_book_count_dirty||s_book_count_search))snprintf(out,size,"统计中");
    else snprintf(out,size,"%d%s %s",s_counts[folder],folder==0&&s_book_count_incomplete?"+":"",folder==0?"本":folder==1?"张":"款");
}
static int count_root(int folder) {
    if (!s_sd.mounted) return 0;
    if(folder==0){file_book_count_invalidate();return s_counts[0];}
    DIR *dir = opendir(folder_root(folder));
    if (!dir) return 0;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) if (supported(folder, entry->d_name)) ++count;
    closedir(dir);
    return count;
}

// 列表行的文件夹 / 文档图标，来自 Lucide；盒子 46 像素，中心对到原来的图形中心。
// Lucide folder / file marks for list rows; a 46 px box centred on the old artwork.
#define FILE_ICON_PX 46

// 图标来自既有Lucide资源；类型判断不改变打开文件的支持范围。/ Use existing Lucide assets without expanding file-open capabilities.
static ui_icon_t file_item_icon(const file_item_t *item){
    if(item->is_dir)return UI_ICON_FOLDER;
    const char *ext=strrchr(file_item_name(item),'.');if(!ext)return UI_ICON_FILE;
    if(!strcasecmp(ext,".epub")||!strcasecmp(ext,".txt")||!strcasecmp(ext,".pdf")||!strcasecmp(ext,".mobi")||!strcasecmp(ext,".azw3")||!strcasecmp(ext,".fb2"))return UI_ICON_BOOK_OPEN;
    if(!strcasecmp(ext,".ttf")||!strcasecmp(ext,".otf")||!strcasecmp(ext,".ttc")||!strcasecmp(ext,".woff")||!strcasecmp(ext,".woff2"))return UI_ICON_TYPE;
    if(!strcasecmp(ext,".png")||!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg")||!strcasecmp(ext,".gif")||!strcasecmp(ext,".bmp")||!strcasecmp(ext,".webp")||!strcasecmp(ext,".xth")||!strcasecmp(ext,".svg"))return UI_ICON_FILE_IMAGE;
    return UI_ICON_FILE;
}
static void file_icon(uint8_t *fb,int x,int y,const file_item_t *item){ui_draw_icon(fb,x+21,y+21,FILE_ICON_PX,file_item_icon(item),UI_GRAY_BLACK);}

static void file_detail(char *dst, size_t cap, const file_item_t *item) {
    if (item->is_dir) snprintf(dst, cap, "文件夹");
    else if (item->size >= 1024 * 1024) snprintf(dst, cap, "%.1f MB", item->size / 1048576.0);
    else if (item->size >= 1024) snprintf(dst, cap, "%.1f KB", item->size / 1024.0);
    else snprintf(dst, cap, "%lld B", (long long)item->size);
}

// 文件列表的分隔线保持两像素，墨水屏上比浅色发丝线更容易辨认。
// Two-pixel file dividers stay legible on the panel without changing the layout.
static void file_rule(uint8_t *fb, int x, int y, int width) {
    ui_draw_separator(fb, y, x, width, 0x60);
}

// 大卡片用双层描边，避免单像素浅灰线在低对比度屏上消失。
// Double-stroke large cards so their outlines survive low-contrast E-ink rendering.
static void file_card_border(uint8_t *fb, EpdRect rect, int radius) {
    ui_draw_control_frame(fb, rect, radius, 0x50);
}

static void fit_name(char *name, int px, int width) {
    while (*name && ttf_text_width_px(ui_text_effective_px(px), name) > width) {
        size_t n = strlen(name) - 1;
        while (n && ((unsigned char)name[n] & 0xc0) == 0x80) --n;
        name[n] = 0;
    }
}



static void editor_start(void) {
    if (strlen(file_item_name(&s_selected)) >= sizeof(s_editor)) {
        snprintf(s_message, sizeof(s_message), "名称过长，无法在设备上编辑");
        return;
    }
    copy_utf8(s_editor, sizeof(s_editor), file_item_name(&s_selected));
    s_editor_ext[0] = 0;
    char *dot = strrchr(s_editor, '.');
    if (!s_selected.is_dir && dot && dot != s_editor) {
        copy_utf8(s_editor_ext, sizeof(s_editor_ext), dot);
        *dot = 0;
    }
    ui_text_edit_init(&s_editor_input, s_editor, sizeof(s_editor));
    s_editor_notice[0] = 0;
    ui_keyboard_begin(&s_editor_input, false);
    s_view = FILE_VIEW_RENAME;
}







static void render_actions(uint8_t *fb) {
    const int offset=file_sheet_offset();
    // S03：保留当前目录作背景，只叠加文件操作底部面板。/ S03: keep the directory visible and overlay a file-action sheet.
    EpdRect sheet = {20, 430+offset, 644, 626};
    ui_fill_round_rect(fb, sheet, 28, UI_GRAY_WHITE);
    file_card_border(fb, sheet, 28);
    ui_fill_round_rect(fb, (EpdRect){604, 454+offset, 38, 38}, 19, 0xe0);
    ui_text_vc(fb, 623, 473+offset, 24, "×", EPD_DRAW_ALIGN_CENTER, false);
    char name[128]; copy_utf8(name, sizeof(name), file_item_name(&s_selected)); fit_name(name, 28, 540);
    file_icon(fb, 48, 467+offset, &s_selected);
    ui_text(fb, 102, 462+offset, 28, name, EPD_DRAW_ALIGN_LEFT, false);
    char detail[48]; file_detail(detail, sizeof(detail), &s_selected);
    ui_text(fb, 102, 507+offset, 20, detail, EPD_DRAW_ALIGN_LEFT, false);
    char path[128]; copy_utf8(path, sizeof(path), s_selected.path); fit_name(path, 19, 585);
    ui_text(fb, 50, 552+offset, 19, path, EPD_DRAW_ALIGN_LEFT, false);
    file_rule(fb, 42, 590+offset, 600);
    if (s_message[0]) ui_text(fb, 50, 608+offset, 20, s_message, EPD_DRAW_ALIGN_LEFT, false);
    if (s_delete_confirm) {
        ui_text_vc(fb, 342, 712+offset, s_selected.is_dir ? 23 : 28,
                   s_selected.is_dir ? "将删除文件夹内所有内容，无法恢复" : "删除后无法恢复，是否继续？",
                   EPD_DRAW_ALIGN_CENTER, false);
        ui_draw_button(fb, (EpdRect){50, 788+offset, 278, 76}, "取消", false);
        ui_draw_button(fb, (EpdRect){356, 788+offset, 278, 76}, "确认删除", true);
    } else {
        static const char *labels[] = {"重命名", "复制", "移动", "删除"};
        for (int i = 0; i < 4; ++i) {
            EpdRect button = {40 + (i % 2) * 312, 648 + (i / 2) * 122, 292, 100};
            ui_fill_round_rect(fb, button, 20, 0xe8);
            ui_draw_control_frame(fb, button, 20, 0x60);
            ui_text_vc(fb, button.x + button.width / 2, button.y + 50, 25,
                       labels[i], EPD_DRAW_ALIGN_CENTER, false);
        }
        ui_text_vc(fb, 342, 900+offset, 20, "长按文件或文件夹打开操作", EPD_DRAW_ALIGN_CENTER, false);
        ui_fill_round_rect(fb, (EpdRect){40, 930+offset, 604, 76}, 26, 0xe0);
        ui_text_vc(fb, 342, 968+offset, 23, "取消", EPD_DRAW_ALIGN_CENTER, false);
    }
}

static void render_rename(uint8_t *fb) {
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "重命名", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_vc(fb, 646, 107, 25, "完成", EPD_DRAW_ALIGN_RIGHT, false);
    file_rule(fb, 36, 157, 612);
    ui_text(fb, 36, 202, 22, "文件名", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect field = {36, 242, 612, 82};
    ui_text_input_draw(fb, &s_editor_input, field, 29, false, s_editor_ext);
    if (s_editor_notice[0]) ui_text_fixed_vc(fb, 36, 510, 22, s_editor_notice, EPD_DRAW_ALIGN_LEFT, false);
    ui_keyboard_draw(fb, UI_SYSTEM_KEYBOARD_TOP);
}

static bool storage_space_valid(const read_pico_sd_info_t *info) {
    return info->mounted && info->space_valid && info->filesystem_bytes &&
           info->free_bytes<=info->filesystem_bytes && info->filesystem_bytes<=info->capacity_bytes;
}
static void storage_amount(char *text,size_t size,uint64_t bytes) {
    if (bytes>=1073741824ULL) snprintf(text,size,"%.2f GB",bytes/1073741824.0);
    else if (bytes>=1048576ULL) snprintf(text,size,"%.1f MB",bytes/1048576.0);
    else if (bytes) snprintf(text,size,"%.1f KB",bytes/1024.0);
    else snprintf(text,size,"0 MB");
}
static void storage_usage(const read_pico_sd_info_t *info,char *total,size_t total_size,char *usage,size_t usage_size) {
    storage_amount(total,total_size,info->space_valid?info->filesystem_bytes:info->capacity_bytes);
    if (!storage_space_valid(info)) { snprintf(usage,usage_size,info->space_error==ESP_ERR_NOT_FINISHED?
        "正在统计空间，文件可正常使用":"空间统计暂不可用，请重新进入此页"); return; }
    char used[24],available[24];
    storage_amount(used,sizeof(used),info->filesystem_bytes-info->free_bytes);
    storage_amount(available,sizeof(available),info->free_bytes);
    snprintf(usage,usage_size,"已用 %s · 可用 %s",used,available);
}
static void render(app_ctx_t *ctx,uint8_t *fb);
static void render_content(app_ctx_t *ctx, uint8_t *fb) {
    if(s_view==FILE_VIEW_SORT||s_view==FILE_VIEW_BATCH_DELETE){
        file_view_t previous=s_view;s_view=FILE_VIEW_LIST;render(ctx,fb);s_view=previous;
        if(previous==FILE_VIEW_SORT)render_file_sort(fb);else render_file_batch_confirmation(fb);return;
    }

    if (s_view == FILE_VIEW_ACTIONS) {
        file_view_t restore = s_view;
        s_view = FILE_VIEW_LIST;
        render(ctx, fb);
        s_view = restore;
        render_actions(fb);
        return;
    }
    (void)ctx;
    ui_clear_page(fb);
    if(main_page_visible(ctx))ui_root_background(fb);
    ui_nav_status(fb);
    if (s_view == FILE_VIEW_RENAME || s_view == FILE_VIEW_SEARCH) {
        if(s_view==FILE_VIEW_SEARCH){
            ui_nav_back(fb,36,79);ui_text_box(fb,(EpdRect){120,79,380,66},34,"搜索文件",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){530,79,118,66},26,"应用",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_input_draw(fb,&s_editor_input,(EpdRect){36,242,612,82},29,false,NULL);
            ui_text_box(fb,(EpdRect){36,352,612,84},22,"可输入中文、完整拼音、首字母或英文",EPD_DRAW_ALIGN_LEFT,false);
            ui_draw_button(fb,(EpdRect){36,458,180,66},"清空",false);ui_keyboard_draw(fb,UI_SYSTEM_KEYBOARD_TOP);
        }else render_rename(fb);
        files_navigation(fb);
        return;
    }
    if(s_view==FILE_VIEW_DIR_OPTIONS){
        s_view=FILE_VIEW_LIST;render(ctx,fb);s_view=FILE_VIEW_DIR_OPTIONS;
        EpdRect panel={36,370,612,440};ui_fill_round_rect(fb,panel,26,UI_GRAY_WHITE);file_card_border(fb,panel,26);
        ui_text_box(fb,(EpdRect){60,392,564,70},29,"当前文件夹用途",EPD_DRAW_ALIGN_CENTER,false);
        const char *options[]={"设为书籍目录","设为字体目录","取消"};
        for(int i=0;i<3;++i)file_management_button(fb,(EpdRect){60,490+i*100,564,76},options[i],false);
        return;
    }
    if (s_folder >= 0) {
        ui_nav_back(fb,36,79);
        const char *title=s_folder==3?(!strcmp(s_dir,"/sdcard")?"TF 卡根目录":strrchr(s_dir,'/')+1):names[s_folder];
        if(!strcmp(title,"fonts"))title="Fonts";
        ui_text_box(fb,(EpdRect){112,79,s_view==FILE_VIEW_MOVE?450:456,66},34,s_view==FILE_VIEW_MOVE?"选择目标文件夹":title,EPD_DRAW_ALIGN_CENTER,false);
        char count[64];snprintf(count,sizeof(count),s_file_selecting?"已选 %u 项":"共 %u 项",s_file_selecting?file_selected_count():(unsigned)file_visible_count());
        ui_text_box(fb,(EpdRect){36,s_view==FILE_VIEW_MOVE?171:244,370,s_view==FILE_VIEW_MOVE?38:46},21,count,EPD_DRAW_ALIGN_LEFT,false);
        int top=s_view==FILE_VIEW_MOVE?214:UI_FILES_LIST_TOP;
        ui_hairline(fb,top,36,612,0xdc);
        for(int row=0;row<visible_rows();++row){
            int index=file_visible_index(s_page*visible_rows()+row);if(index<0)break;
            file_item_t *item=&s_files[index];EpdRect r=s_view==FILE_VIEW_MOVE?(EpdRect){36,214+row*88,612,88}:file_entry_rect(row);
            bool grid=s_file_grid&&s_view!=FILE_VIEW_MOVE;
            if(item->selected)ui_fill_round_rect(fb,r,10,0xee);
            if(grid)ui_draw_icon(fb,r.x+r.width/2,r.y+53,58,file_item_icon(item),0x30);
            else file_icon(fb,48,r.y+25,item);
            char name[128],author[128],detail[192],size[32];file_display_name(item,name,author);
            int width=grid?r.width-20:472;fit_name(name,grid?23:26,width);
            ui_text_box(fb,(EpdRect){grid?r.x+10:112,r.y+(grid?96:12),width,grid?54:42},grid?23:26,name,grid?EPD_DRAW_ALIGN_CENTER:EPD_DRAW_ALIGN_LEFT,false);
            file_detail(size,sizeof(size),item);
            if(s_file_query[0]){char parent[288];copy_utf8(parent,sizeof(parent),item->path);char *slash=strrchr(parent,'/');if(slash)*slash=0;
                const char *relative=parent;if(!strncmp(parent,s_search_root,strlen(s_search_root))){relative=parent+strlen(s_search_root);if(*relative=='/')++relative;}
                int used=snprintf(detail,sizeof(detail),"%s · ",size);
                if(used>=0&&(size_t)used<sizeof(detail))copy_utf8(detail+used,sizeof(detail)-(size_t)used,*relative?relative:"当前文件夹");
            }else if(author[0]&&!grid)snprintf(detail,sizeof(detail),"%s · %s",author,size);else copy_utf8(detail,sizeof(detail),size);
            fit_name(detail,18,width);ui_text_box(fb,(EpdRect){grid?r.x+10:112,r.y+(grid?163:57),width,28},18,detail,grid?EPD_DRAW_ALIGN_CENTER:EPD_DRAW_ALIGN_LEFT,false);
            int mx=grid?r.x+r.width-22:628,my=r.y+(grid?22:r.height/2);
            if(s_file_selecting){epd_draw_circle(mx,my,15,0x60,fb);if(item->selected)epd_fill_circle(mx,my,4,UI_GRAY_BLACK,fb);}
            else if(!grid)ui_draw_icon(fb,mx,my,23,UI_ICON_CHEVRON_RIGHT,0x30);
            if(!grid)ui_hairline(fb,r.y+r.height-1,112,536,0xdc);
        }
        if(!file_visible_count())ui_text_box(fb,(EpdRect){52,450,580,100},26,s_message[0]?s_message:"此目录还没有文件",EPD_DRAW_ALIGN_CENTER,false);
        if(s_view==FILE_VIEW_MOVE){
            file_management_button(fb,file_action_rect(0),"取消移动",false);file_management_button(fb,file_action_rect(1),"移动到此处",false);
            if(s_message[0])ui_text_box(fb,(EpdRect){36,960,612,32},20,s_message,EPD_DRAW_ALIGN_CENTER,false);
        }
    } else {
        ui_text_box(fb,(EpdRect){36,80,612,76},52,"文件",EPD_DRAW_ALIGN_LEFT,false);
        EpdRect card=storage_card_rect();ui_fill_round_rect(fb,card,24,UI_GRAY_WHITE);file_card_border(fb,card,24);
        ui_draw_icon(fb,74,213,28,UI_ICON_HARD_DRIVE,0x30);
        ui_text_box(fb,(EpdRect){100,192,200,44},27,"存储卡",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){380,192,236,44},21,"查看卡内文件夹",EPD_DRAW_ALIGN_RIGHT,false);ui_draw_icon(fb,627,214,22,UI_ICON_CHEVRON_RIGHT,0x30);
        char total[32],available[32],quota[80],usage[80];
        if(s_sd.mounted){
            storage_usage(&s_sd,total,sizeof(total),usage,sizeof(usage));
            if(storage_space_valid(&s_sd)){char used[32];storage_amount(used,sizeof(used),s_sd.filesystem_bytes-s_sd.free_bytes);snprintf(usage,sizeof(usage),"已用 %s",used);}
            if(storage_space_valid(&s_sd))storage_amount(available,sizeof(available),s_sd.free_bytes);else snprintf(available,sizeof(available),"统计中");
            snprintf(quota,sizeof(quota),"%s 可用",available);
        }else{snprintf(total,sizeof(total),"—");snprintf(quota,sizeof(quota),"%s",sd_needs_format()?"需要格式化":"未挂载");snprintf(usage,sizeof(usage),"%s",s_sd_notice[0]?s_sd_notice:s_sd_format_confirm?"将清空卡内全部数据，再点确认":"请检查 TF 卡");}
        ui_text_box(fb,(EpdRect){60,244,564,62},42,quota,EPD_DRAW_ALIGN_LEFT,false);
        char capacity[80];snprintf(capacity,sizeof(capacity),"总容量 %s",total);
        ui_text_box(fb,(EpdRect){60,308,290,30},20,capacity,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){350,308,274,30},18,usage,EPD_DRAW_ALIGN_RIGHT,false);
        EpdRect meter={60,352,564,6};ui_fill_round_rect(fb,meter,3,0xd8);
        if(storage_space_valid(&s_sd)){meter.width=(int)((s_sd.filesystem_bytes-s_sd.free_bytes)*564/s_sd.filesystem_bytes);if(meter.width)ui_fill_round_rect(fb,meter,3,0x58);}
        const int folders[]={0,2,1};const char *labels[]={"书籍","字体","壁纸"};const ui_icon_t icons[]={UI_ICON_BOOK_OPEN,UI_ICON_TYPE,UI_ICON_FILE_IMAGE};
        for(int i=0;i<3;++i){EpdRect r=ui_files_category_rect(i);ui_fill_round_rect(fb,r,22,UI_GRAY_WHITE);file_card_border(fb,r,22);ui_draw_icon(fb,r.x+r.width/2,r.y+43,42,icons[i],0x20);
            ui_text_box(fb,(EpdRect){r.x+10,r.y+81,r.width-20,40},28,labels[i],EPD_DRAW_ALIGN_CENTER,false);
            char count[32];file_count_label(folders[i],count,sizeof(count));ui_text_box(fb,(EpdRect){r.x+10,r.y+125,r.width-20,28},18,count,EPD_DRAW_ALIGN_CENTER,false);}
        ui_text_box(fb,(EpdRect){36,606,612,38},26,"传输",EPD_DRAW_ALIGN_LEFT,false);
        EpdRect transfer={36,662,612,408};ui_fill_round_rect(fb,transfer,24,UI_GRAY_WHITE);file_card_border(fb,transfer,24);
        const char *methods[]={"Wi-Fi 传输","热点传输","U 盘模式","阅星瞳APP导入"};const char *details[]={"同一网络，手机或电脑传入","连接阅读器热点，无需路由器","用 USB 数据线连接电脑","扫码绑定，下载书籍、字体与壁纸"};const ui_icon_t method_icons[]={UI_ICON_WIFI,UI_ICON_RADIO_TOWER,UI_ICON_USB,UI_ICON_DOWNLOAD};
        for(int i=0;i<4;++i){EpdRect r=home_transfer_rect(i);ui_fill_round_rect(fb,(EpdRect){60,r.y+24,54,54},15,0xf0);ui_draw_icon(fb,87,r.y+51,33,method_icons[i],0x20);
            ui_text_box(fb,(EpdRect){134,r.y+14,450,43},27,methods[i],EPD_DRAW_ALIGN_LEFT,false);ui_text_box(fb,(EpdRect){134,r.y+58,450,28},18,details[i],EPD_DRAW_ALIGN_LEFT,false);ui_draw_icon(fb,612,r.y+51,24,UI_ICON_CHEVRON_RIGHT,0x30);if(i)ui_draw_separator(fb,r.y,60,564,0x90);}
    }
    if(s_view==FILE_VIEW_LIST&&s_folder>=0){
        ui_draw_icon(fb,616,112,36,s_file_grid?UI_ICON_LIST:UI_ICON_LAYOUT_GRID,0x30);
        EpdRect search=file_search_rect();ui_fill_round_rect(fb,search,18,0xf0);ui_draw_control_frame(fb,search,18,0xc0);
        ui_draw_icon(fb,66,192,28,UI_ICON_SEARCH,0x60);
        ui_text_box(fb,(EpdRect){96,168,492,48},24,s_file_query[0]?s_file_query:"搜索文件",EPD_DRAW_ALIGN_LEFT,false);
        if(s_file_query[0])ui_draw_icon(fb,620,192,24,UI_ICON_X,0x60);
        char label[48];snprintf(label,sizeof(label),"%s %s",s_file_sort==0?"名称":s_file_sort==1?"时间":"大小",s_file_desc?"↓":"↑");
        file_management_button(fb,file_sort_button(),label,false);file_management_button(fb,file_select_rect(),s_file_selecting?"取消":"选择",false);
        if(s_message[0])ui_text_box(fb,(EpdRect){36,984,612,22},17,s_message,EPD_DRAW_ALIGN_CENTER,false);
    }
    if(s_folder>=0)draw_file_pager(fb);
    files_navigation(fb);
}

static void render(app_ctx_t *ctx,uint8_t *fb) {
    bool previous=s_render_secondary;
    s_render_secondary=previous || !main_page_visible(ctx);
    render_content(ctx,fb);
    s_render_secondary=previous;
}

static void on_enter(app_ctx_t *ctx) {
    file_search_stop();s_file_query[0]=s_search_root[0]=0;
    s_file_selecting=s_batch_running=false;s_batch_count=0;
    (void)ctx;
    s_pager_pressed=UI_PAGER_NONE;s_pager_paint_pending=false;
    s_folder = s_requested_folder; s_requested_folder = -1;
    s_view = FILE_VIEW_LIST;
    s_delete_confirm = false;
    snprintf(s_dir, sizeof(s_dir), "/sdcard");
    s_page = 0; s_scan_pending = true;
    read_pico_sd_start_probe();
    const esp_err_t info_status=read_pico_sd_get_info(&s_sd);
    s_space_poll_ms=ctx->now_ms;
    s_sd_format_confirm = false;
    s_sd_notice[0] = 0;
    for (int i = 0; i < 3; ++i) s_counts[i] = count_root(i);
    if (s_folder >= 0) scan_folder(s_folder);
    else scan_folder(3);
    // 已完成首帧目录扫描时不在下一拍重复统计并推整页；挂载/分配未完成仍沿原重试路径。
    // Do not repeat a completed first-frame scan and whole-page present on the next tick; retain mount/allocation retries.
    if(info_status!=ESP_ERR_NOT_FINISHED&&s_files)s_scan_pending=false;
}
static void on_media_lost(app_ctx_t *ctx) {
    file_search_stop();s_file_query[0]=s_search_root[0]=0;
    s_batch_running=false;
    s_pager_pressed=UI_PAGER_NONE;s_pager_paint_pending=false;
    (void)ctx; ui_keyboard_end(); s_folder = -1; s_count = 0; s_scan_pending = false;
    s_view = FILE_VIEW_LIST; s_delete_confirm = false;
    memset(s_counts, 0, sizeof(s_counts)); read_pico_sd_get_info(&s_sd);
}
static void on_media_ready(app_ctx_t *ctx) {
    (void)ctx;
    s_scan_pending = true;
}
static app_redraw_t rename_paint(app_ctx_t *ctx, bool field);
static app_redraw_t on_tick(app_ctx_t *ctx) {
    if(s_batch_running)return file_batch_tick();
    if (s_view == FILE_VIEW_RENAME || s_view == FILE_VIEW_SEARCH) {
        if (ctx->consumed) return APP_REDRAW_NONE;
        bool held = !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
        if (ui_keyboard_hold_tick(held, ctx->touch ? ctx->touch->x : 0,
            ctx->touch ? ctx->touch->y : 0, ctx->now_ms) == UI_KEYBOARD_CHANGED) return rename_paint(ctx, false);
        return ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) == UI_KEYBOARD_CHANGED
            ? rename_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if(s_file_search&&s_view==FILE_VIEW_LIST&&!ctx->consumed&&!(ctx->touch&&ctx->touch->touched)){
        file_search_step(s_file_search,24);file_search_stats_t stats=file_search_stats(s_file_search);
        s_search_matches=stats.matched;s_search_errors=stats.errors;file_search_notice(stats.done);
        if(stats.done){file_search_stop();qsort(s_files,s_count,sizeof(*s_files),compare_files);}
        if(stats.done||ctx->now_ms-s_search_painted_ms>=500){s_search_painted_ms=ctx->now_ms;return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if(!s_scan_pending&&s_view==FILE_VIEW_LIST&&s_folder<0&&s_sd.mounted&&!ctx->consumed&&
       !(ctx->touch&&ctx->touch->touched)&&(s_book_count_dirty||s_book_count_search)){
        return file_book_count_tick()?APP_REDRAW_PAGE:APP_REDRAW_NONE;
    }
    if (!s_scan_pending) {
        if (s_folder<0 && s_sd.mounted && !(ctx->touch && ctx->touch->touched) && ctx->now_ms-s_space_poll_ms>=
            (s_sd.space_valid?15000:500)
            ) {
            s_space_poll_ms=ctx->now_ms;
            read_pico_sd_info_t previous=s_sd;
            (void)read_pico_sd_start_space_refresh();
            read_pico_sd_get_info(&s_sd);
            if (previous.space_valid!=s_sd.space_valid || previous.filesystem_bytes!=s_sd.filesystem_bytes || previous.free_bytes!=s_sd.free_bytes)
                return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (read_pico_sd_get_info(&s_sd) == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
    s_scan_pending = false;
    s_space_poll_ms=ctx->now_ms;
    for (int i = 0; i < 3; ++i) s_counts[i] = count_root(i);
    if (s_folder >= 0) scan_folder(s_folder);
    else scan_folder(3);
    return APP_REDRAW_PAGE;
}

static void open_selected(app_ctx_t *ctx) {
    const char *ext = strrchr(file_item_name(&s_selected), '.');
    if (!ext) { snprintf(s_message, sizeof(s_message), "没有可用的打开方式"); return; }
    if (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt")) {
        extern const app_desc_t app_book;
        if (app_book_request_open(s_selected.path)) ctx->request_app = &app_book;
    } else if (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".png") || !strcasecmp(ext, ".webp") || !strcasecmp(ext, ".xth")) {
        extern const app_desc_t app_image;
        if (app_image_request_open(s_selected.path)) ctx->request_app = &app_image;
    } else if (!strcasecmp(ext, ".ttf") || !strcasecmp(ext, ".otf")) {
        if (ttf_font_open(s_selected.path) == ESP_OK) {
            app_settings_set_system_font_path(s_selected.path);
            app_font_activate_system();
            snprintf(s_message, sizeof(s_message), "已设为系统字体");
        } else snprintf(s_message, sizeof(s_message), "字体无法加载");
    } else snprintf(s_message, sizeof(s_message), "没有可用的打开方式");
}

static void delete_selected(void) {
    file_search_stop();
    char old_path[288]; copy_utf8(old_path, sizeof(old_path), s_selected.path);
    esp_err_t err = file_tree_delete(old_path, file_deleted, NULL);
    if (err != ESP_OK) {
        s_delete_confirm = false;
        repair_deleted_directories();
        book_store_notify_changed();
        rescan_current();
        snprintf(s_message, sizeof(s_message), "删除未完成，请检查 TF 卡");
        return;
    }
    repair_deleted_directories();
    book_store_notify_changed();
    s_view = FILE_VIEW_LIST; s_delete_confirm = false;
    rescan_current();
    snprintf(s_message, sizeof(s_message), s_selected.is_dir ? "文件夹已删除" : "文件已删除");
}

static app_redraw_t save_rename(void) {
    if (ui_keyboard_pending()) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "请先选择候选字");
        return APP_REDRAW_PAGE;
    }
    char *start = s_editor;
    while (*start == ' ') ++start;
    if (start != s_editor) memmove(s_editor, start, strlen(start) + 1);
    size_t len = strlen(s_editor);
    while (len && (s_editor[len - 1] == ' ' || s_editor[len - 1] == '.')) s_editor[--len] = 0;
    if (!len || strchr(s_editor, '/') || strchr(s_editor, '\\') || strchr(s_editor, ':')) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "请输入有效文件名");
        return APP_REDRAW_PAGE;
    }
    char name[128], destination[288];
    if (snprintf(name, sizeof(name), "%s%s", s_editor, s_editor_ext) >= sizeof(name) ||
        !sibling_path(name, destination)) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "文件名过长");
        return APP_REDRAW_PAGE;
    }
    if (!strcmp(destination, s_selected.path)) { s_view = FILE_VIEW_ACTIONS; return APP_REDRAW_PAGE; }
    struct stat existing;
    if (stat(destination, &existing) == 0) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "同名文件已经存在");
        return APP_REDRAW_PAGE;
    }
    char old_path[288]; copy_utf8(old_path, sizeof(old_path), s_selected.path);
    if (!moved_settings_fit(old_path, destination)) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "设置路径过长");
        return APP_REDRAW_PAGE;
    }
    file_search_stop();
    if (file_tree_move(old_path, destination) != ESP_OK) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "重命名失败，请检查 TF 卡");
        return APP_REDRAW_PAGE;
    }
    sync_moved_settings(old_path, destination);
    notify_moved_tree(old_path, destination, 0);
    book_store_notify_changed();
    copy_utf8(s_selected.path, sizeof(s_selected.path), destination);
    s_view = FILE_VIEW_ACTIONS;
    ui_keyboard_end();
    snprintf(s_message, sizeof(s_message), "重命名完成");
    return APP_REDRAW_PAGE;
}

static app_redraw_t action_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    const int y=(int)ev->y0-file_sheet_offset();
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (y < 430 || (ev->x0 >= 596 && y < 510) || y >= 918) {
        s_view = FILE_VIEW_LIST; s_delete_confirm = false; rescan_current(); return APP_REDRAW_PAGE;
    }
    if (s_delete_confirm) {
        if (y >= 788 && y < 864) {
            if (ev->x0 < 342) s_delete_confirm = false;
            else delete_selected();
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (y >= 648 && y < 870) {
        int action = (y >= 770 ? 2 : 0) + (ev->x0 >= 352 ? 1 : 0);
        if (action == 0) editor_start();
        else if (action == 1) {
            char copied[128] = {0};
            esp_err_t err = copy_selected_item(copied);
            if (err == ESP_OK) snprintf(s_message, sizeof(s_message), "已复制：%.74s", copied);
            else snprintf(s_message, sizeof(s_message), err == ESP_ERR_NO_MEM ? "内存不足，复制失败" : "复制失败，请检查空间");
        } else if (action == 2) start_move();
        else { s_delete_confirm = true; s_message[0] = 0; }
        return ctx->request_app ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static EpdRect s_input_area;
static bool s_input_layout, s_input_settle;
static app_redraw_t rename_paint(app_ctx_t *ctx, bool field) {
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, UI_SYSTEM_KEYBOARD_TOP, (EpdRect){36, 242, 612, 82}, 29, false, s_editor_ext, field);
    s_input_area = update.area; s_input_layout = update.layout; s_input_settle = update.settle;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static bool files_present(app_ctx_t *ctx, app_redraw_t redraw) {
    bool pager=s_pager_paint_pending;s_pager_paint_pending=false;
    if(pager&&redraw==APP_REDRAW_AREA&&!ctx->request_app&&
        (s_view==FILE_VIEW_LIST||s_view==FILE_VIEW_MOVE)){
        guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,ui_pager_area_at(file_pager_top())));
        return true;
    }
    if (redraw != APP_REDRAW_AREA || (s_view != FILE_VIEW_RENAME && s_view != FILE_VIEW_SEARCH)) return false;
    if (s_input_settle) {
        guard_draw_result(ctx->hl, update_display_area_full_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, s_input_area));
        s_input_settle = false;
        return true;
    }
    guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl,
        s_input_layout ? &E0470_WAVEFORM : &E0470_FOLLOW_WAVEFORM,
        s_input_layout ? MODE_GL16 : MODE_DU, s_input_area));
    return true;
}
static app_redraw_t rename_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type == UI_GESTURE_PRESS)
        return ui_keyboard_press(ev->x0, ev->y0, UI_SYSTEM_KEYBOARD_TOP, ctx->now_ms) ? rename_paint(ctx, false) : APP_REDRAW_NONE;
    bool feedback = ev->type != UI_GESTURE_LONG_PRESS && ui_keyboard_release();
    if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R) {
        bool page = ui_keyboard_page(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1);
        return (page || feedback) ? rename_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (ev->type != UI_GESTURE_TAP) return feedback ? rename_paint(ctx, false) : APP_REDRAW_NONE;
    int x = ev->x0, y = ev->y0;
    if (y < 160 && x < 160) { ui_keyboard_end(); s_view = FILE_VIEW_ACTIONS; return APP_REDRAW_PAGE; }
    if (y < 160 && x > 510) return save_rename();
    if (ui_text_input_tap(&s_editor_input, (EpdRect){36, 242, 612, 82}, 29,
                          false, s_editor_ext, x, y)) return rename_paint(ctx, true);
    ui_keyboard_result_t result = ui_keyboard_tap(x, y, UI_SYSTEM_KEYBOARD_TOP, esp_timer_get_time() / 1000);
    if (result == UI_KEYBOARD_DONE) return save_rename();
    return result == UI_KEYBOARD_CHANGED ? rename_paint(ctx, false) : APP_REDRAW_NONE;
}

static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if(s_batch_running)return APP_REDRAW_NONE;
    if(s_view==FILE_VIEW_SEARCH){
        if(ev->type==UI_GESTURE_TAP&&ev->y0<160){
            if(ev->x0>510)return file_search_apply();
            ui_keyboard_end();s_view=FILE_VIEW_LIST;return APP_REDRAW_FULL;
        }
        if(ev->type==UI_GESTURE_TAP&&ui_rect_hit((EpdRect){36,458,180,66},ev->x0,ev->y0)){
            s_query_draft[0]=0;ui_text_edit_init(&s_editor_input,s_query_draft,sizeof(s_query_draft));return APP_REDRAW_FULL;
        }
        if(ev->type==UI_GESTURE_TAP&&ev->y0>=160&&ev->y0<UI_SYSTEM_KEYBOARD_TOP){
            if(ui_text_input_tap(&s_editor_input,(EpdRect){36,242,612,82},29,false,NULL,ev->x0,ev->y0))return rename_paint(ctx,true);
            return APP_REDRAW_NONE;
        }
        if(ev->type==UI_GESTURE_TAP){
            bool feedback=ui_keyboard_release();ui_keyboard_result_t result=ui_keyboard_tap(ev->x0,ev->y0,UI_SYSTEM_KEYBOARD_TOP,ctx->now_ms);
            if(result==UI_KEYBOARD_DONE)return file_search_apply();
            return result==UI_KEYBOARD_CHANGED||feedback?rename_paint(ctx,false):APP_REDRAW_NONE;
        }
        return rename_gesture(ctx,ev);
    }
    if(s_view==FILE_VIEW_DIR_OPTIONS){
        if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
        for(int i=0;i<3;++i)if(ui_rect_hit((EpdRect){60,490+i*100,564,76},ev->x0,ev->y0)){
            if(i<2){
                if(!strcmp(s_dir,"/sdcard"))snprintf(s_message,sizeof(s_message),"请先进入 TF 卡内的文件夹");
                else if(i==0){if(app_settings_set_books_dir(s_dir)){book_store_notify_changed();snprintf(s_message,sizeof(s_message),"已设为书籍目录");}else snprintf(s_message,sizeof(s_message),"保存失败，请重试");}
                else {if(app_settings_set_fonts_dir(s_dir)){ttf_font_scan();snprintf(s_message,sizeof(s_message),"已设为字体目录");}else snprintf(s_message,sizeof(s_message),"保存失败，请重试");}
                s_counts[0]=count_root(0);s_counts[2]=count_root(2);
            }
            s_view=FILE_VIEW_LIST;return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(s_view==FILE_VIEW_SORT){
        if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
        if(!ui_rect_hit((EpdRect){36,230,612,710},ev->x0,ev->y0)||ui_rect_hit(file_sort_rect(5),ev->x0,ev->y0))s_view=FILE_VIEW_LIST;
        else for(int i=0;i<5;++i)if(ui_rect_hit(file_sort_rect(i),ev->x0,ev->y0)){
            if(i<3)s_file_sort=i;else s_file_desc=i==4;
            qsort(s_files,s_count,sizeof(*s_files),compare_files);s_page=0;break;
        }
        return APP_REDRAW_PAGE;
    }
    if(s_view==FILE_VIEW_BATCH_DELETE){
        if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
        if(ui_rect_hit((EpdRect){354,760,270,76},ev->x0,ev->y0)){
            if(file_batch_prepare())(void)file_batch_begin(false);
            if(!s_batch_running)s_view=FILE_VIEW_LIST;
        }else s_view=FILE_VIEW_LIST;
        return APP_REDRAW_PAGE;
    }
    if(s_view==FILE_VIEW_LIST&&s_folder>=0&&ev->type==UI_GESTURE_TAP){
        if(ui_rect_hit(file_layout_rect(),ev->x0,ev->y0)){s_file_grid=!s_file_grid;s_page=0;return APP_REDRAW_FULL;}
        if(ui_rect_hit(file_search_rect(),ev->x0,ev->y0)){
            if(s_file_query[0]&&ev->x0>=592)return file_search_clear();
            copy_utf8(s_query_draft,sizeof(s_query_draft),s_file_query);s_editor_ext[0]=0;
            ui_text_edit_init(&s_editor_input,s_query_draft,sizeof(s_query_draft));ui_keyboard_begin(&s_editor_input,false);
            s_view=FILE_VIEW_SEARCH;return APP_REDRAW_FULL;
        }
        if(ui_rect_hit(file_select_rect(),ev->x0,ev->y0)){
            s_file_selecting=!s_file_selecting;s_batch_count=0;
            for(int i=0;i<s_count;++i)s_files[i].selected=false;
            return APP_REDRAW_FULL;
        }
        if(ui_rect_hit(file_sort_button(),ev->x0,ev->y0)){s_view=FILE_VIEW_SORT;return APP_REDRAW_PAGE;}
        if(s_file_selecting&&ev->y0>=1096){
            if(ui_rect_hit(ui_files_batch_rect(3),ev->x0,ev->y0)){s_file_selecting=false;s_batch_count=0;for(int i=0;i<s_count;++i)s_files[i].selected=false;return APP_REDRAW_FULL;}
            if(ui_rect_hit(ui_files_batch_rect(0),ev->x0,ev->y0)){
                int rows=s_folder<0?root_rows():visible_rows(),start=s_page*rows;bool all=true;
                for(int n=start;n<file_visible_count()&&n<start+rows;++n){int i=file_visible_index(n);if(!s_files[i].selected)all=false;}
                for(int n=start;n<file_visible_count()&&n<start+rows;++n)s_files[file_visible_index(n)].selected=!all;
                s_batch_count=0;return APP_REDRAW_PAGE;
            }

            if(ui_rect_hit(file_batch_button(0),ev->x0,ev->y0)&&file_batch_prepare()){
                s_batch_move=true;start_move();
            }else if(ui_rect_hit(file_batch_button(1),ev->x0,ev->y0)&&file_selected_count())s_view=FILE_VIEW_BATCH_DELETE;
            return APP_REDRAW_PAGE;
        }
    }

    app_redraw_t pager;
    if(file_pager_event(ctx,ev,&pager))return pager;
    if (s_view == FILE_VIEW_ACTIONS) return action_gesture(ctx, ev);
    if (s_view == FILE_VIEW_RENAME) return rename_gesture(ctx, ev);
    if (s_view == FILE_VIEW_MOVE) {
        if (ev->type == UI_GESTURE_SWIPE_L && (s_page + 1) * visible_rows() < file_visible_count()) {
            ++s_page; return APP_REDRAW_PAGE;
        }
        if (ev->type == UI_GESTURE_SWIPE_R && s_page > 0) { --s_page; return APP_REDRAW_PAGE; }
        if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        if (ev->y0 < 185) {
            if (strcmp(s_dir, "/sdcard")) {
                char *slash = strrchr(s_dir, '/'); if (slash) *slash = 0;
                scan_folder(3);
            } else leave_move(false);
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(file_action_rect(0),ev->x0,ev->y0) || ui_rect_hit(file_action_rect(1),ev->x0,ev->y0)) {
            if (ev->x0 < 342) leave_move(false);
            else if(s_batch_count&&s_batch_move){(void)file_batch_begin(true);return APP_REDRAW_PAGE;}
            else return move_selected();
            return APP_REDRAW_PAGE;
        }
        int row = (ev->y0 - 214) / 88;
        int index = s_page * visible_rows() + row;
        if (ev->y0 >= 214 && row >= 0 && row < visible_rows() && index >= 0 && index < s_count) {
            copy_utf8(s_dir, sizeof(s_dir), s_files[index].path);
            scan_folder(3);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_SWIPE_L && s_folder >= 0 && (s_page + 1) * visible_rows() < file_visible_count()) { ++s_page; return APP_REDRAW_PAGE; }
    if (ev->type == UI_GESTURE_SWIPE_R && s_folder >= 0 && s_page > 0) { --s_page; return APP_REDRAW_PAGE; }
    if (ev->type != UI_GESTURE_TAP && ev->type != UI_GESTURE_LONG_PRESS) return APP_REDRAW_NONE;
    bool long_press = ev->type == UI_GESTURE_LONG_PRESS;
    if(long_press&&s_folder==3&&ev->y0>=79&&ev->y0<145){s_view=FILE_VIEW_DIR_OPTIONS;return APP_REDRAW_PAGE;}
    if (long_press && ev->y0 < (s_folder >= 0 ? UI_FILES_LIST_TOP : UI_LOCK_HEIGHT)) return APP_REDRAW_NONE;
    int tab = s_file_selecting ? -1 : files_nav_hit(ev->x0, ev->y0);
    if (!long_press && tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    if (s_folder >= 0) {
        if (!long_press && ev->y0 < 185) {
            if(s_file_query[0]&&!s_file_selecting)return file_search_clear();
            if(s_file_selecting){s_file_selecting=false;s_batch_count=0;for(int i=0;i<s_count;++i)s_files[i].selected=false;return APP_REDRAW_FULL;}
            if (s_folder == 3 && strcmp(s_dir, "/sdcard")) {
                char *slash = strrchr(s_dir, '/'); if (slash) *slash = 0;
                scan_folder(3);
            } else s_folder = -1;
            return APP_REDRAW_PAGE;
        }
        int row = file_entry_hit(ev->x0,ev->y0);
        int index = row<0?-1:file_visible_index(s_page * visible_rows() + row);
        if (ev->y0 >= UI_FILES_LIST_TOP && row >= 0 && row < visible_rows() && index >= 0 && index < s_count) {
            if(long_press){
                if(s_file_selecting&&s_files[index].selected&&file_selected_count()==1){s_selected=s_files[index];s_view=FILE_VIEW_ACTIONS;s_delete_confirm=false;s_message[0]=0;return APP_REDRAW_PAGE;}
                s_file_selecting=true;s_batch_count=0;s_files[index].selected=true;return APP_REDRAW_FULL;
            }
            if(s_file_selecting){s_files[index].selected=!s_files[index].selected;s_batch_count=0;return APP_REDRAW_PAGE;}
            if (s_files[index].is_dir) {
                file_search_stop();s_file_query[0]=s_search_root[0]=0;s_folder=3;
                if (strlen(s_files[index].path) >= sizeof(s_dir)) {
                    snprintf(s_message, sizeof(s_message), "目录路径过长"); return APP_REDRAW_PAGE;
                }
                snprintf(s_dir, sizeof(s_dir), "%s", s_files[index].path);
                scan_folder(3);
                return APP_REDRAW_PAGE;
            }
            s_selected = s_files[index];
            open_selected(ctx);
            return ctx->request_app ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    // 存储卡卡片：只有"卡在但读不了"时响应，点第一次问、点第二次格。
    // The storage card answers only when the card is present but unreadable: the first tap asks,
    // the second one formats.
    if (!long_press && sd_needs_format() && ui_rect_hit(storage_card_rect(), ev->x0, ev->y0)) {
        if (!s_sd_format_confirm) {
            s_sd_format_confirm = true;
            s_sd_notice[0] = 0;
        } else {
            s_sd_format_confirm = false;
            snprintf(s_sd_notice, sizeof(s_sd_notice), "正在格式化…");
            esp_err_t err = (book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000)) ? read_pico_sd_format() : ESP_ERR_NOT_FINISHED;
            read_pico_sd_get_info(&s_sd);
            if (err == ESP_OK) {
                s_sd_notice[0] = 0;
                s_scan_pending = true;
            } else {
                snprintf(s_sd_notice, sizeof(s_sd_notice), "格式化失败：%s", esp_err_to_name(err));
            }
        }
        return APP_REDRAW_PAGE;
    }
    // 点别处就撤销确认，免得提示一直停在"再点一次"。
    // A tap elsewhere cancels the confirmation, so the prompt does not stay armed.
    bool format_cancelled = !long_press && s_sd_format_confirm;
    if (format_cancelled) {
        s_sd_format_confirm = false;
        s_sd_notice[0] = 0;
    }
    for (int i = 0; !long_press && i < 4; ++i) if (ui_rect_hit(home_transfer_rect(i), ev->x0, ev->y0)) {
        if(i==3){
            extern const app_desc_t app_cloud_sync;
            ctx->request_app=&app_cloud_sync;return APP_REDRAW_NONE;
        }
        extern const app_desc_t app_transfer;
        if (i == 0) app_transfer_request_wifi_upload();
        else if (i == 1) app_transfer_request_hotspot_start();
        else app_transfer_request_usb_start();
        ctx->request_app = &app_transfer;
        return APP_REDRAW_NONE;
    }
    if(!long_press&&ui_rect_hit(storage_card_rect(),ev->x0,ev->y0)&&s_sd.mounted){s_folder=3;copy_utf8(s_dir,sizeof(s_dir),"/sdcard");scan_folder(3);return APP_REDRAW_PAGE;}
    for(int i=0;!long_press&&i<3;++i)if(ui_rect_hit(ui_files_category_rect(i),ev->x0,ev->y0)){
        const int folders[]={0,2,1};s_folder=folders[i];s_file_selecting=false;s_batch_count=0;scan_folder(s_folder);return APP_REDRAW_PAGE;
    }
    return format_cancelled ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
}

static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    if(s_batch_running)return APP_REDRAW_NONE;
    if(s_view==FILE_VIEW_SEARCH){ui_keyboard_end();s_view=FILE_VIEW_LIST;return APP_REDRAW_FULL;}
    if(s_view==FILE_VIEW_SORT||s_view==FILE_VIEW_BATCH_DELETE){s_view=FILE_VIEW_LIST;return APP_REDRAW_PAGE;}
    if(s_file_selecting&&s_view==FILE_VIEW_LIST){s_file_selecting=false;s_batch_count=0;for(int i=0;i<s_count;++i)s_files[i].selected=false;return APP_REDRAW_FULL;}

    s_sd_format_confirm=false;
    if (s_view == FILE_VIEW_MOVE) { leave_move(false); return APP_REDRAW_PAGE; }
    if (key == 1) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_view == FILE_VIEW_RENAME || s_view == FILE_VIEW_SEARCH) { ui_keyboard_end(); s_view = FILE_VIEW_ACTIONS; return APP_REDRAW_PAGE; }
    if (s_view == FILE_VIEW_ACTIONS) {
        s_view = FILE_VIEW_LIST; s_delete_confirm = false; rescan_current(); return APP_REDRAW_PAGE;
    }
    if (s_folder >= 0) {
        if(s_file_query[0])return file_search_clear();
        if (s_folder == 3 && strcmp(s_dir, "/sdcard")) {
            char *slash = strrchr(s_dir, '/'); if (slash) *slash = 0;
            scan_folder(3);
        } else s_folder = -1;
        return APP_REDRAW_PAGE;
    }
    ui_nav_request(ctx, 0);
    return APP_REDRAW_NONE;
}
static bool no_menu_handle(app_ctx_t *ctx) { (void)ctx; return false; }
static bool files_navigation_visible(app_ctx_t *ctx){(void)ctx;return !(s_file_selecting&&s_view==FILE_VIEW_LIST);}
#if KIIKO_BOARD_METALIO47
// 自有操作栏保留与主底栏相同的灰底，但刷新不再裁掉这块区域。/ Preserve gray action-bar fill while allowing the whole bottom to refresh.
static bool files_custom_footer_gray(app_ctx_t *ctx){(void)ctx;return main_page_visible(ctx)||s_file_grid||(s_file_selecting&&s_view==FILE_VIEW_LIST);}
#endif
static bool files_navigation_enabled(app_ctx_t *ctx){(void)ctx;return !s_file_selecting&&!s_batch_running&&s_view==FILE_VIEW_LIST;}
static bool main_page_visible(app_ctx_t *ctx) {
    (void)ctx;
    return !s_file_selecting && s_view == FILE_VIEW_LIST && s_folder < 0 && !strcmp(s_dir, "/sdcard") && !s_delete_confirm;
}
static bool files_edge_back(app_ctx_t *ctx) { return !main_page_visible(ctx); }
static bool files_back_swipe_area(app_ctx_t *ctx,const ui_gesture_event_t *event) {
    (void)ctx;
    return (s_view!=FILE_VIEW_RENAME&&s_view!=FILE_VIEW_SEARCH) || event->y0<UI_SYSTEM_KEYBOARD_TOP;
}
static void files_before_lock(app_ctx_t *ctx) {
    file_book_count_invalidate();
    if(s_file_search){file_search_stop();s_scan_pending=true;}
    (void)ctx;s_pager_pressed=UI_PAGER_NONE;s_pager_paint_pending=false;
    if(s_batch_running){
        s_batch_running=false;
        if(s_batch_move){s_folder=s_move_origin_folder;copy_utf8(s_dir,sizeof(s_dir),s_move_origin_dir);}
        s_view=FILE_VIEW_LIST;s_file_selecting=true;s_scan_pending=true;
    }
}
static void files_exit(app_ctx_t *ctx) { files_before_lock(ctx);file_search_stop(); ui_keyboard_end();s_batch_running=s_file_selecting=false;s_batch_count=0;free(s_batch);s_batch=NULL;free(s_files);s_files=NULL; }

const app_desc_t app_files = {
    .binary_ui = false,
    .title = "文件管理 Files", .detail = "TF 卡文件与传输", .enter_full = false,
    .owns_keys = true, .menu_handle_enabled = no_menu_handle,
    .main_page_visible = main_page_visible, .edge_back_enabled = files_edge_back,
    .back_swipe_area=files_back_swipe_area,
    .persistent_navigation=true,.white_child_transition=true,.navigation_enabled=files_navigation_enabled,.navigation_visible=files_navigation_visible,
    .on_enter = on_enter, .on_media_lost = on_media_lost, .on_media_ready = on_media_ready,
    .on_exit = files_exit, .on_before_lock=files_before_lock, .render = render, .on_tick = on_tick,
    .on_gesture = on_gesture, .on_key = on_key, .present = files_present,
};

#if defined(KIIKO_LOCAL_LATENCY) || KIIKO_BOARD_METALIO47
#if KIIKO_BOARD_METALIO47
#ifdef ESP_PLATFORM
#include "ui_locale_format.h"
#endif
#include "metalio_hw.h"
#endif
#include "esp_timer.h"
#endif
#ifdef KIIKO_LOCAL_LATENCY
#include "esp_log.h"
#endif
/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：快刷只保留 1 位封面，转码后立即释放灰阶；切换模式或对比度清理旧格式。
 * User revision: fast Home retains only a 1-bit cover, releases gray after conversion, and drops stale formats on mode/contrast changes.
 * 用户修订：首页原比例点阵独立落卡，复用灰阶缩略图；普通/水波纹复用灰阶缓存，始终预留内存。
 * User revision: persist proportional Home monochrome separately and reuse gray thumbnail files; ordinary/water reuse gray caches with a memory reserve.
 * 用户2026-11-10：首页继续阅读封面固定176×240，小图等比放大、异形封面居中裁剪填满；原最近书夹第2–4本选择和封面缓存保持。
 * User2026-11-10: keep the resume cover176×240,scale small covers proportionally and centre-crop other ratios to fill;retain recent-folder ranks2–4 and existing natural-cover caches.
 * 中文：首页使用真实续读、完整缓存书摘、可选书夹/时钟与月阅读记录；绘图不读卡。
 * English: Home uses real resume data, cached complete sentences, selectable folder/clock and monthly records; painting never reads media.
 * 用户修订：缓存原位换位，只用一个条目的栈空间；保留复用速度，避免启动时复制七条占用半个主任务栈。
 * User revision: reorder the cache in place with one stack entry, retaining hot reuse without copying seven entries into half the main stack at boot.
 * 用户最新修订：续读书名复用已加载系统字体，按原40px直接生成字形；不加载阅读字体或增加内建字库。
 * Latest user revision: resume titles reuse the loaded system face at the original 40px, without loading the reader face or adding built-in fonts.
 * 用户修订：首页静止时按分钟局部更新时间，不扫描图书或重画整页；保留快刷白底。
 * User revision: locally advance the idle clock by minute without scanning books or repainting the page; retain the fast white background.
 * 用户修订：普通/水波纹续读卡有浅灰底，三模式结构边线加粗；快刷组件预先二值化且不增加补刷。
 * User revision: ordinary/water resume cards have a light-gray fill and thicker rules in all modes; fast widgets are pre-binarized without an extra refresh.
 * 用户修订：Kehai秒针保持未变化的表盘像素，避免灰底形成矩形黑闪；快刷只驱动黑白圆点差分。
 * User revision: Kehai seconds hold unchanged face pixels to avoid rectangular gray-background flashes; fast mode drives binary dot differences only.
 * 用户修订：首页题头与书架、文件和设置共用左侧起点、顶边及52px名义字号，仍使用当前系统字体。
 * User revision: Home's title matches the shelf/files/settings left edge, top and nominal 52px size in the active system face.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "app.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "app_registry.h"
#include "book_cover.h"
#if KIIKO_BOARD_METALIO47
#include "metalio_cover_cache.h"
#else
#endif
#include "book_cover_jobs.h"
#include "ui_image_dither.h"
#include "book_epub.h"
#include "book_progress.h"
#include "book_store.h"
#include "book_file_path.h"
#include "book_title.h"
#include "book_ticket.h"
#include "book_excerpt.h"
#include "ui_home_widget.h"
#include "ui_home_clock.h"
#include "ui_wallpaper.h"
#include "ui_avatar.h"
#include "read_pico_pmu.h"
#include "read_pico_crashlog.h"
#include <math.h>
#include "display.h"
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_root_style.h"
#include "ui_nav.h"

typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[128];
    char author[128];
    uint8_t *cover;
    bool cover_fast;
    uint16_t cover_width,cover_height;
    time_t modified;
    book_progress_t progress;
    bool has_progress;
} home_book_t;

// 首页冷数据不参与DMA或中断，在PSRAM保留原静态生命周期和零初始化。
// Home cold data is never used by DMA/ISRs; retain static lifetime and zero initialization in PSRAM.
static EXT_RAM_BSS_ATTR home_book_t s_current, s_recent[6];
typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[128];
    char author[128];
    off_t size;
    time_t modified;
    uint8_t *pixels;
    bool fast;
    uint16_t width,height;
} home_cover_cache_t;
static EXT_RAM_BSS_ATTR home_cover_cache_t s_cover_cache[7];
#define HOME_COVER_RESERVE (512u * 1024u)
static int s_cover_next;
static bool s_scan_pending;
static bool s_books_cache_valid;
static bool s_cached_sd_present;
static bool s_cached_sd_mounted;
static unsigned s_cached_store_revision;
static char s_cached_books_dir[128];
static char s_cached_last_path[BOOK_STORE_PATH_MAX];
static EpdRect s_area;
static bool s_clock_pending;
static int64_t s_clock_minute=-2;
static uint32_t s_reading_days[31];
static struct tm s_date;
static enum { HOME_ROOT,HOME_COMPONENT,HOME_GOAL,HOME_TIME } s_view;
static app_home_component_t s_component_browse;
static bool s_component_pending;
static EpdRect s_component_dirty;
static const app_home_component_t s_components[]={APP_HOME_MOON,APP_HOME_CLOCK,APP_HOME_KEHAI,APP_HOME_CAT,APP_HOME_DALI,APP_HOME_ASTERISK,APP_HOME_HOUSE,APP_HOME_FOLDER};
#define HOME_COMPONENT_OPTIONS (sizeof(s_components)/sizeof(s_components[0]))
static bool s_time_pending;
static struct tm s_time_date;
static int64_t s_time_stamp=-1;
static bool s_widget_pending;
static struct tm s_widget_date,s_widget_shown;
static int64_t s_widget_stamp=-1;
static unsigned s_widget_phase,s_widget_phase_shown,s_time_phase,s_time_phase_shown;
static int64_t s_motion_ms=-100;
typedef struct {unsigned start,last,steps;int64_t stamp;int view;bool valid;} clock_trail_t;
static clock_trail_t s_widget_trail,s_time_trail;
static uint8_t s_goal_draft;
static bool s_save_error,s_folder_attempted;
static uint8_t *s_folder;
static bool s_folder_fast;
static uint32_t s_folder_key;
static char s_excerpt[BOOK_EXCERPT_CAP];
static EXT_RAM_BSS_ATTR uint8_t s_avatar[60*60];
static bool s_avatar_ready,s_avatar_pending;
static char s_avatar_path[288];
static EpdRect s_pressed;
static bool s_press_pending;
static EpdRect continue_card_rect(void) { return (EpdRect){36,184,612,378}; }
static EpdRect continue_card_hit_rect(void) { return (EpdRect){24,172,636,402}; }
static bool continue_card_pressed(void) { return s_press_pending && s_pressed.height == 378; }
static EpdRect control_feedback_area(EpdRect control) {
    return control.height == 378 ? (EpdRect){56,270,184,252} : control;
}
static bool s_reading_days_valid;
static bool s_home_force_full_once;
static uint32_t s_cached_progress_revision=UINT32_MAX;
static uint8_t *s_clock_base_cache;
static bool s_clock_base_attempted;
#if KIIKO_BOARD_METALIO47
static int64_t s_component_warm_after_ms;
#endif
void app_home_release_clock_cache(void) {
    ui_home_clock_bind_base_cache(NULL,0);
    free(s_clock_base_cache);s_clock_base_cache=NULL;s_clock_base_attempted=false;
}
static void clock_base_prepare(void){
    app_home_component_t component=s_view==HOME_COMPONENT?s_component_browse:app_settings_home_component();
    bool eligible=component!=APP_HOME_FOLDER;
    if(s_clock_base_cache||s_clock_base_attempted||!eligible)return;
    s_clock_base_attempted=true;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<UI_HOME_CLOCK_BASE_CACHE_BYTES+2u*1024u*1024u)return;
    s_clock_base_cache=heap_caps_malloc(UI_HOME_CLOCK_BASE_CACHE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ui_home_clock_bind_base_cache(s_clock_base_cache,s_clock_base_cache?UI_HOME_CLOCK_BASE_CACHE_BYTES:0);
}

static home_book_t *book_at(int index);

static bool is_book(const char *name) {
    if (!name[0] || name[0] == '.' || !strncmp(name, "._", 2)) return false;
    return book_file_path_supported(name);
}
static void copy_title(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void title_for(home_book_t *item) {
    if (!book_name_from_path(item->path, item->title, sizeof(item->title),item->author,sizeof(item->author)))
        copy_title(item->title, sizeof(item->title), "未命名");
}

static void consider_book(const char *path, const struct stat *st) {
    if (s_current.path[0] && !strcmp(path, s_current.path)) return;
    book_progress_t progress = {0};
    bool has_progress = st->st_size >= 0 && (uint64_t)st->st_size <= UINT32_MAX &&
                        book_progress_load(path, (uint32_t)st->st_size, &progress);
    for (int i=0;i<6;++i) if (!strcmp(path,s_recent[i].path)) return;
    uint64_t score = has_progress ? (UINT64_C(1) << 32) + progress.last_open_s : (uint64_t)st->st_mtime;
    int slot = -1;
    for (int i = 0; i < 6; ++i) {
        uint64_t other = s_recent[i].has_progress ?
            (UINT64_C(1) << 32) + s_recent[i].progress.last_open_s : (uint64_t)s_recent[i].modified;
        if (!s_recent[i].path[0] || score > other) { slot = i; break; }
    }
    if (slot < 0) return;
    for (int i = 5; i > slot; --i) s_recent[i] = s_recent[i - 1];
    memset(&s_recent[slot], 0, sizeof(s_recent[slot]));
    snprintf(s_recent[slot].path, sizeof(s_recent[slot].path), "%s", path);
    s_recent[slot].modified = st->st_mtime;
    s_recent[slot].has_progress = has_progress;
    if (has_progress) s_recent[slot].progress = progress;
}

static void scan_root(const char *root) {
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!is_book(entry->d_name)) continue;
        char path[BOOK_STORE_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >= sizeof(path)) continue;
        struct stat st;
        if (!stat(path, &st) && S_ISREG(st.st_mode)) consider_book(path, &st);
    }
    closedir(dir);
}

static bool recent_visit(const char *path,const book_progress_t *progress,void *ctx) {
    (void)ctx;
    struct stat st;
    if (is_book(path) && !stat(path,&st) && S_ISREG(st.st_mode) && st.st_size>=0 && (uint64_t)st.st_size==progress->file_size)
        consider_book(path,&st);
    return true;
}
static void scan_books(void) {
    char last[BOOK_STORE_PATH_MAX];
    // 不支持的恢复记录只跳过，不删除其他固件的缓存和阅读位置。
    // Ignore unsupported resume records without deleting another firmware's cache or progress.
    if (book_progress_last_path(last, sizeof(last)) && book_file_path_supported(last)) {
        struct stat st;
        if (!stat(last, &st) && S_ISREG(st.st_mode) && st.st_size >= 0 && (uint64_t)st.st_size <= UINT32_MAX) {
            snprintf(s_current.path, sizeof(s_current.path), "%s", last);
            s_current.has_progress = book_progress_load(last, (uint32_t)st.st_size, &s_current.progress);
            title_for(&s_current);
        }
    }
    (void)book_progress_list(recent_visit,NULL);
    read_pico_sd_info_t sd = {0};
    read_pico_sd_get_info(&sd);
    if (sd.mounted) {
        if (!strcmp(app_settings_books_dir(), "/sdcard/books")) scan_root("/sdcard/book");
        scan_root(app_settings_books_dir());
    }
    if (book_store_flash_ready()) scan_root("/flash/books");
    for (int i=0;i<7;++i) {
        home_book_t *b=book_at(i);
        if (!b->path[0]) continue;
        title_for(b);
        char metadata_title[128],metadata_author[128];
        if (book_epub_metadata_cached(b->path,metadata_title,sizeof(metadata_title),metadata_author,sizeof(metadata_author))==ESP_OK) {
            if(metadata_title[0])copy_title(b->title,sizeof(b->title),metadata_title);
            bool chinese=book_author_clean_display(metadata_author,sizeof(metadata_author));
            if(metadata_author[0]&&(chinese||!book_author_clean_display(b->author,sizeof(b->author))))
                copy_title(b->author,sizeof(b->author),metadata_author);
        }
    }
    home_book_t *featured = s_current.path[0] ? &s_current : &s_recent[0];
    const char *ext = strrchr(featured->path, '.');
    if (ext && !strcasecmp(ext, ".epub") && pico_boot_asset_allowed(featured->path)) {
        char metadata_title[256],metadata_author[128];
        if (book_epub_metadata(featured->path, metadata_title, sizeof(metadata_title),
                               metadata_author, sizeof(metadata_author)) == ESP_OK) {
            if(metadata_title[0])copy_title(featured->title,sizeof(featured->title),metadata_title);
            bool chinese=book_author_clean_display(metadata_author,sizeof(metadata_author));
            if(metadata_author[0]&&(chinese||!book_author_clean_display(featured->author,sizeof(featured->author))))
                copy_title(featured->author,sizeof(featured->author),metadata_author);
        }
    }
}

static void refresh_progress(void) {
    for (int i = 0; i < 7; ++i) {
        home_book_t *item = book_at(i);
        struct stat st;
        if (!item->path[0] || stat(item->path, &st) || !S_ISREG(st.st_mode) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) continue;
        item->has_progress = book_progress_load(item->path, (uint32_t)st.st_size, &item->progress);
    }
}

static void remember_scan_state(const read_pico_sd_info_t *sd) {
    s_books_cache_valid = true;
    s_cached_store_revision = book_store_revision();
    s_cached_sd_present = sd->present;
    s_cached_sd_mounted = sd->mounted;
    snprintf(s_cached_books_dir, sizeof(s_cached_books_dir), "%s", app_settings_books_dir());
    s_cached_last_path[0] = 0;
    (void)book_progress_last_path(s_cached_last_path, sizeof(s_cached_last_path));
}

static home_book_t *book_at(int index) { return index ? &s_recent[index - 1] : &s_current; }
static void clear_cover_cache(void) {
    free(s_folder);s_folder=NULL;s_folder_key=0;s_folder_attempted=false;
    for (int i = 0; i < 7; ++i) {
        free(s_cover_cache[i].pixels);
        memset(&s_cover_cache[i], 0, sizeof(s_cover_cache[i]));
    }
    s_current.cover = NULL;
    for (int i = 0; i < 6; ++i) s_recent[i].cover = NULL;
}

void app_home_cover_mode_changed(void) {
    clear_cover_cache();
    s_cover_next = 0;
}
static void restore_cover_cache(void) {
    bool used[7] = {0};
    int match[7];
    for (int i = 0; i < 7; ++i) {
        home_book_t *book = book_at(i);
        struct stat st;
        book->cover = NULL;
        match[i] = -1;
        if (!book->path[0] || stat(book->path, &st) || !S_ISREG(st.st_mode)) continue;
        for (int j = 0; j < 7; ++j) {
            home_cover_cache_t *cached = &s_cover_cache[j];
            if (used[j] || !cached->pixels || strcmp(book->path, cached->path) ||
                strcmp(book->title, cached->title) || strcmp(book->author, cached->author) ||
                st.st_size != cached->size || st.st_mtime != cached->modified) continue;
            match[i] = j;
            used[j] = true;
            break;
        }
    }
    for (int j = 0; j < 7; ++j) if (!used[j]) {
        free(s_cover_cache[j].pixels);
        memset(&s_cover_cache[j], 0, sizeof(s_cover_cache[j]));
    }
    for (int i = 0; i < 7; ++i) {
        int j = match[i];
        if (j < 0) {
            for (j = i; j < 7 && s_cover_cache[j].pixels; ++j) {}
        }
        if (j < 7 && j != i) {
            home_cover_cache_t swap = s_cover_cache[i];
            s_cover_cache[i] = s_cover_cache[j];
            s_cover_cache[j] = swap;
            for (int k = i + 1; k < 7; ++k) {
                if (match[k] == i) match[k] = j;
                else if (match[k] == j) match[k] = i;
            }
        }
        book_at(i)->cover = s_cover_cache[i].pixels;
        book_at(i)->cover_fast = s_cover_cache[i].fast;
        book_at(i)->cover_width=s_cover_cache[i].width;
        book_at(i)->cover_height=s_cover_cache[i].height;
    }
}

static unsigned s_cover_generation;
static bool s_cover_waiting;
static bool load_cover(home_book_t *item,int slot) {
    s_cover_waiting=false;
    if(!pico_boot_asset_allowed(item->path))return false;
    book_cover_job_t request={.list=true,.natural=true,.fast=app_settings_main_fast_refresh(),
        .contrast=app_settings_system_contrast(),.phase=17};
    snprintf(request.path,sizeof(request.path),"%s",item->path);
    snprintf(request.title,sizeof(request.title),"%s",item->title);
    snprintf(request.author,sizeof(request.author),"%s",item->author);
    if(!s_cover_generation)s_cover_generation=book_cover_jobs_begin();
    if(!book_cover_jobs_submit(s_cover_generation,slot,&request)){
        s_cover_generation=book_cover_jobs_begin();
        if(!book_cover_jobs_submit(s_cover_generation,slot,&request))return false;
    }
    book_cover_result_t result={0};int state=book_cover_jobs_take(s_cover_generation,slot,&result);
    if(!state){s_cover_waiting=true;return false;}if(state<0)return false;
    home_cover_cache_t *cached=&s_cover_cache[slot];free(cached->pixels);
    snprintf(cached->path,sizeof(cached->path),"%s",item->path);
    snprintf(cached->title,sizeof(cached->title),"%s",item->title);
    snprintf(cached->author,sizeof(cached->author),"%s",item->author);
    cached->size=result.size;cached->modified=result.modified;cached->pixels=result.pixels;
    cached->fast=result.fast;cached->width=result.width;cached->height=result.height;
    item->cover=result.pixels;item->cover_fast=result.fast;item->cover_width=result.width;item->cover_height=result.height;
    return true;
}

static void fit(char *text,int px,int width) {
    while(*text && ui_text_fixed_width_px(px,text)>width) {
        size_t n=strlen(text)-1;while(n&&((uint8_t)text[n]&0xc0)==0x80)--n;text[n]=0;
    }
}
static void label(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray) {
    if (gray >= 0xf0) ui_text_fixed_vc(fb,x,y,px,text,align,true);
    else ui_text_fixed_ink_vc(fb,x,y,px,text,align,gray);
}
static void label_on(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray,uint8_t background) {
    ui_text_fixed_context_aa_vc(fb,x,y,px,text,text,align,gray,background);
}
// 三像素边线均向内加粗，保持点击及局部刷新边界。/ Three-pixel rules grow inward, preserving hit and dirty bounds.
static void home_frame(uint8_t *fb,EpdRect box,int radius) {
    for(int i=0;i<3;++i)ui_draw_round_rect(fb,ui_inset_rect(box,i),radius>i?radius-i:0,0x30);
}
static void circle_frame(uint8_t *fb,int x,int y,int radius) {
    for(int i=0;i<3&&i<radius;++i)epd_draw_circle(x,y,radius-i,0x30,fb);
}
static bool date_now(struct tm *out) {
    time_t now=time(NULL);
    if(now<1704067200) { const pmu_snapshot_t *pmu=read_pico_pmu_get(); if(pmu&&pmu->time_synced)now=pmu->unix_sec; }
    if(now<1704067200) {memset(out,0,sizeof(*out));return false;}
    now+=8*3600;gmtime_r(&now,out);return true;
}
static int64_t date_stamp(const struct tm *d) {return ((int64_t)d->tm_year*366+d->tm_yday)*86400+d->tm_hour*3600+d->tm_min*60+d->tm_sec;}
static unsigned clock_phase_now(struct tm *date) {
    struct timeval now;
    if(!gettimeofday(&now,NULL)&&now.tv_sec>=1704067200){time_t local=now.tv_sec+8*3600;gmtime_r(&local,date);return date->tm_sec*10+(unsigned)now.tv_usec/100000;}
    return (unsigned)date->tm_sec*10;
}
static bool clock_motion_due(app_ctx_t *ctx){return ctx->now_ms<=0||ctx->now_ms<s_motion_ms||ctx->now_ms-s_motion_ms>=100;}
static unsigned component_index(app_home_component_t component) {
    for(unsigned i=0;i<HOME_COMPONENT_OPTIONS;++i)if(s_components[i]==component)return i;
    return 0;
}
static ui_home_clock_style_t clock_style(app_home_component_t component) {
    // 浏览顺序独立于稳定设置编号，避免旧选择错配。/ Keep browsing order separate from persisted IDs.
    switch(component){
        case APP_HOME_MOON:return UI_HOME_CLOCK_MOON;
        case APP_HOME_KEHAI:return UI_HOME_CLOCK_KEHAI;
        case APP_HOME_CAT:return UI_HOME_CLOCK_CAT;
        case APP_HOME_DALI:return UI_HOME_CLOCK_DALI;
        case APP_HOME_ASTERISK:return UI_HOME_CLOCK_ASTERISK;
        case APP_HOME_HOUSE:return UI_HOME_CLOCK_HOUSE;
        default:return UI_HOME_CLOCK_PS1995;
    }
}
static void clock_draw_region(uint8_t *fb,EpdRect bounds,const struct tm *date,bool binary,ui_home_clock_style_t style,EpdRect clip,unsigned phase){
    clock_trail_t *trail=s_view==HOME_TIME?&s_time_trail:&s_widget_trail;
    int64_t stamp=date_stamp(date);
    if(style!=UI_HOME_CLOCK_KEHAI){trail->valid=false;ui_home_clock_phase_draw_region(fb,bounds,date,binary,style,clip,phase);return;}
    if(!trail->valid||trail->view!=s_view||stamp<trail->stamp){*trail=(clock_trail_t){.start=phase,.last=phase,.stamp=stamp,.view=s_view,.valid=true};}
    else {unsigned added=(phase+600-trail->last)%600;trail->steps=stamp-trail->stamp>=60?600:trail->steps+added;if(trail->steps>600)trail->steps=600;trail->last=phase;}
    // 保留同一页面上的圆环，分钟更新只重画表盘目标；离页重新开始。
    // Retain the ring on this page through minute hand updates; start anew on a view change.
    ui_home_clock_trail_draw_region(fb,bounds,date,binary,style,clip,phase,trail->start,trail->steps);
}
static const char *component_name(app_home_component_t component) {return component==APP_HOME_FOLDER?"最近阅读书夹":ui_home_clock_name(clock_style(component));}
static EpdRect time_clock_bounds(void) {return(EpdRect){122,275,460,503};}
static EpdRect component_clock_bounds(void){return s_view==HOME_COMPONENT?(EpdRect){141,294,420,460}:(EpdRect){24,640,320,350};}
static EpdRect component_clock_graphics(void){return s_view==HOME_COMPONENT?component_clock_bounds():(EpdRect){24,640,320,326};}
static bool visible_motion_clock(void){
    return(s_view==HOME_ROOT&&ui_home_clock_has_motion(clock_style(app_settings_home_component())))
#if KIIKO_BOARD_METALIO47
        ;
#else
        ||(s_view==HOME_COMPONENT&&ui_home_clock_has_motion(clock_style(s_component_browse)));
#endif
}
#if KIIKO_BOARD_METALIO47
static void component_prepare_neighbor(app_ctx_t *ctx) {
    if(s_view!=HOME_COMPONENT||s_component_pending||!s_clock_base_cache||ctx->consumed||
       (ctx->touch&&ctx->touch->touched)||ctx->now_ms<s_component_warm_after_ms)return;
    // 空闲时每轮只准备一个邻近样式；临时画布不接触正在显示的front/back。
    // Prepare at most one neighboring style per idle interval, leaving the displayed front/back untouched.
    s_component_warm_after_ms=ctx->now_ms+600;
    EpdRect bounds=component_clock_bounds();bool binary=app_settings_main_fast_refresh();
    unsigned current=component_index(s_component_browse);
    for(unsigned step=1;step<HOME_COMPONENT_OPTIONS;++step) {
        unsigned offset=(step+1)/2;
        unsigned index=(current+HOME_COMPONENT_OPTIONS+(step&1?offset:HOME_COMPONENT_OPTIONS-offset))%HOME_COMPONENT_OPTIONS;
        app_home_component_t component=s_components[index];
        if(component==APP_HOME_FOLDER)continue;
        ui_home_clock_style_t style=clock_style(component);
        if(ui_home_clock_preview_cached(bounds,binary,style))continue;
        const size_t bytes=(size_t)epd_width()*epd_height()/2;
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<bytes+2u*1024u*1024u)return;
        uint8_t *scratch=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!scratch)return;
        ui_fill_rect_fast(bounds,0xff,scratch);
        ui_home_clock_preview_draw(scratch,bounds,binary,style);
        free(scratch);return;
    }
}
#endif
static void refresh_days(void) {
    s_reading_days_valid=date_now(&s_date)&&book_ticket_month_days(s_date.tm_year+1900,s_date.tm_mon+1,s_reading_days);
    if(!s_reading_days_valid)memset(s_reading_days,0,sizeof(s_reading_days));
}
static unsigned today_seconds(void) { return s_reading_days_valid?s_reading_days[s_date.tm_mday-1]:0; }
static unsigned month_length(const struct tm *d) {
    static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned year=d->tm_year+1900;
    return days[d->tm_mon]+(d->tm_mon==1&&year%4==0&&(year%100!=0||year%400==0));
}
static void ring(uint8_t *fb,int cx,int cy,int radius,int thick,uint32_t seconds,unsigned goal) {
    const float fraction=goal?fminf(1,seconds/(float)goal):0;
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x) {
        float r=hypotf(x,y);if(r>radius||r<radius-thick)continue;
        float a=atan2f(x,-y);if(a<0)a+=6.2831853f;
        epd_draw_pixel(cx+x,cy+y,a<=fraction*6.2831853f?0x20:0xe0,fb);
    }
    if(fraction>0&&fraction<1) {
        float angle=fraction*6.2831853f;int r=radius-thick/2;
        epd_fill_circle(cx,cy-r,thick/2,0x20,fb);
        epd_fill_circle(cx+lroundf(sinf(angle)*r),cy-lroundf(cosf(angle)*r),thick/2,0x20,fb);
    }
}
static void calendar(uint8_t *fb,int x,int y,int step_x,int step_y,int px,unsigned goal) {
    static const char *const weekdays[]={"日","一","二","三","四","五","六"};
    if(!s_reading_days_valid) {label(fb,x+3*step_x,y+100,20,"请先设置日期与时间",EPD_DRAW_ALIGN_CENTER,0x60);return;}
    unsigned start=(s_date.tm_wday+7-(s_date.tm_mday-1)%7)%7;
    for(int i=0;i<7;++i)label(fb,x+i*step_x,y-step_y,20,weekdays[i],EPD_DRAW_ALIGN_CENTER,0x70);
    for(unsigned day=1;day<=month_length(&s_date);++day) {
        unsigned cell=start+day-1;int cx=x+(cell%7)*step_x,cy=y+(cell/7)*step_y;
        bool today=day==(unsigned)s_date.tm_mday;char value[4];snprintf(value,sizeof(value),"%u",day);
        if(today)epd_fill_circle(cx,cy,px/2+3,0x20,fb);
        label(fb,cx,cy,px,value,EPD_DRAW_ALIGN_CENTER,today?0xff:day>(unsigned)s_date.tm_mday?0x90:0x20);
        if(s_reading_days[day-1]>=goal && goal)ui_fill_rect_fast((EpdRect){cx-10,cy+px/2+9,20,2},0x30,fb);
    }
}
static home_book_t *featured(void) {return s_current.path[0]?&s_current:&s_recent[0];}
static void draw_cover(uint8_t *fb,const home_book_t *item,EpdRect bounds) {
    EpdRect box=bounds;
    if(item->cover&&item->cover_width&&item->cover_height) {
        unsigned w=item->cover_width,h=item->cover_height;
        const book_crop_t crop=book_cover_crop(w,h,(unsigned)box.width,(unsigned)box.height);
        for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x) {
            int dx=x<12?12-x:x>=box.width-12?x-(box.width-13):0;
            int dy=y<12?12-y:y>=box.height-12?y-(box.height-13):0;
            if(dx*dx+dy*dy>144)continue;
            unsigned sx=crop.x+(uint64_t)(unsigned)x*crop.width/(unsigned)box.width;
            unsigned sy=crop.y+(uint64_t)(unsigned)y*crop.height/(unsigned)box.height;
            uint8_t g=item->cover_fast?(item->cover[sy*((w+7)/8)+sx/8]&(1u<<(sx&7))?255:0):ui_image_dither_gray(ui_contrast_gray(item->cover[sy*w+sx]),box.x+x,box.y+y);
            epd_draw_pixel(box.x+x,box.y+y,g,fb);
        }
    } else ui_fill_round_rect(fb,box,12,0xe0);
    home_frame(fb,box,12);
}
static unsigned folder_books(home_book_t *books[3]) {
    unsigned n=0;
    const home_book_t *resume=featured();
    // 续读已展示第一本，书夹取其后的三本；无续读记录时也排除实际展示的回退书。
    // The resume card owns the first book; take the next three, excluding its fallback too.
    for(int i=0;i<6&&n<3;++i)
        if(s_recent[i].path[0]&&s_recent[i].has_progress&&strcmp(s_recent[i].path,resume->path))
            books[n++]=&s_recent[i];
    return n;
}
static uint32_t folder_key(void) {
    home_book_t *books[3];unsigned n=folder_books(books);uint32_t hash=2166136261u;
    for(unsigned i=0;i<n;++i) {
        for(const char *p=books[i]->path;*p;++p)hash=(hash^(uint8_t)*p)*16777619u;
        struct stat st;
        if(!stat(books[i]->path,&st)) { hash=(hash^(uint32_t)st.st_size)*16777619u;hash=(hash^(uint32_t)st.st_mtime)*16777619u; }
    }
    hash=(hash^n)*16777619u;hash=(hash^app_settings_main_fast_refresh())*16777619u;
    return (hash^app_settings_system_contrast())*16777619u;
}
static bool prepare_folder(void) {
    home_book_t *books[3];unsigned n=folder_books(books);ui_home_cover_t covers[3]={0};
    for(unsigned i=0;i<n;++i) {
        covers[i]=(ui_home_cover_t){books[i]->cover,books[i]->cover_width,books[i]->cover_height,
            books[i]->cover_fast?(books[i]->cover_width+7)/8:books[i]->cover_width,books[i]->cover_fast};
    }
    const bool fast=app_settings_main_fast_refresh();
    size_t size=fast?UI_HOME_FOLDER_BW_BYTES:UI_HOME_FOLDER_GRAY_BYTES;
    s_folder_attempted=true;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<size+224000+768*1024)return false;
    uint8_t *out=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *scratch=out?heap_caps_malloc(224000,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
    bool ok=out&&scratch&&ui_home_folder_compose(out,scratch,covers,n,fast);
    free(scratch);
    if(!ok){free(out);return false;}
    free(s_folder);s_folder=out;s_folder_fast=fast;s_folder_key=folder_key();return true;
}
static void avatar_prepare_state(void) {
    const char *path=app_settings_avatar_path();
    if(pico_boot_recovery()||!path[0]){ui_avatar_request("",60);s_avatar_ready=s_avatar_pending=false;s_avatar_path[0]=0;return;}
    if(strcmp(path,s_avatar_path))s_avatar_ready=false;
    snprintf(s_avatar_path,sizeof(s_avatar_path),"%s",path);
    ui_avatar_request(path,60);
    ui_avatar_state_t state=ui_avatar_poll(path,60,s_avatar);
    s_avatar_ready=state==UI_AVATAR_READY;s_avatar_pending=state==UI_AVATAR_PENDING;
}
static void header(uint8_t *fb,const char *title) {
    ui_nav_status(fb);ui_nav_back(fb,36,78);
    char t[96];snprintf(t,sizeof(t),"%s",title);int px=ui_text_title_fit(t,42,530,title);
    ui_text_title_vc(fb,120,113,px,t,title,EPD_DRAW_ALIGN_LEFT);
}
static int title_lines(uint8_t *fb,const char *text,uint8_t background) {
    char lines[2][128]={{0}};unsigned row=0;int px=40;
    for(const uint8_t *p=(const uint8_t *)text;*p&&row<2;) {
        unsigned n=*p<128?1:*p<0xe0?2:*p<0xf0?3:4;
        size_t used=strlen(lines[row]);
        if(strnlen((const char *)p,n)<n||used+n>=sizeof(lines[row]))break;
        memcpy(lines[row]+used,p,n);lines[row][used+n]=0;
        if(ui_text_fixed_context_width_px(px,lines[row],text)>358) {
            lines[row][used]=0;if(!used)break;
            bool closing=strchr(",.!?)]}",*p)!=NULL || (n==3&&p[0]==0xe3&&p[1]==0x80&&(p[2]==0x81||p[2]==0x82||p[2]==0x89||p[2]==0x91)) ||
                (n==3&&p[0]==0xef&&p[1]==0xbc&&(p[2]==0x8c||p[2]==0x89||p[2]==0x81||p[2]==0x9f));
            if(row==0&&closing) {
                size_t last=used-1;while(last&&((uint8_t)lines[0][last]&0xc0)==0x80)--last;
                memcpy(lines[1],lines[0]+last,used-last);lines[1][used-last]=0;lines[0][last]=0;
            }
            if(++row==2) {
                while(lines[1][0]&&ui_text_fixed_context_width_px(px,lines[1],text)>318) {
                    size_t len=strlen(lines[1])-1;while(len&&(lines[1][len]&0xc0)==0x80)--len;lines[1][len]=0;
                }
                strlcat(lines[1],"…",sizeof(lines[1]));break;
            }
            continue;
        }
        p+=n;
    }
    for(unsigned i=0;i<2;++i)if(lines[i][0])ui_text_fixed_context_aa_vc(fb,264,312+44*i,px,lines[i],text,EPD_DRAW_ALIGN_LEFT,0,background);
    return lines[1][0]?2:1;
}
static void render_root(uint8_t *fb) {
#ifdef KIIKO_LOCAL_LATENCY
    int64_t probe_home=esp_timer_get_time();
#define HOME_STAGE(name) do { int64_t probe_now=esp_timer_get_time();ESP_LOGI("latency","HOME stage=%s us=%lld",name,(long long)(probe_now-probe_home));probe_home=probe_now; } while(0)
#else
#define HOME_STAGE(name) do {} while(0)
#endif
    ui_nav_status(fb);ui_text(fb,36,91,52,"首页",EPD_DRAW_ALIGN_LEFT,false);
    char value[80];unsigned seconds=today_seconds();
    snprintf(value,sizeof(value),"%u 分钟",seconds/60);fit(value,24,180);label(fb,490,118,24,value,EPD_DRAW_ALIGN_RIGHT,0x30);
    ring(fb,540,118,30,7,seconds,app_settings_home_goal_minutes()*60);
    if(s_avatar_ready)ui_wallpaper_draw_avatar(fb,s_avatar,(EpdRect){588,88,60,60},30);
    else {epd_fill_circle(618,118,30,0x30,fb);label(fb,618,118,30,"K",EPD_DRAW_ALIGN_CENTER,0xff);}
    HOME_STAGE("header");
#if KIIKO_BOARD_METALIO47
    const uint8_t card_gray=0xff;
#else
    const uint8_t card_gray=0xff;
#endif
    ui_root_card(fb,(EpdRect){36,184,612,378},28);
    home_frame(fb,(EpdRect){36,184,612,378},28);
    for(int side=0;side<2;++side) {
        int x=side?648:36;epd_fill_circle(x,252,14,UI_ROOT_GRAY,fb);circle_frame(fb,x,252,14);
        ui_fill_rect_fast((EpdRect){side?x:0,237,side?36:36,30},UI_ROOT_GRAY,fb);
    }
    label_on(fb,60,222,24,"最近阅读",EPD_DRAW_ALIGN_LEFT,0x60,card_gray);
    if(s_excerpt[0]) {
        char quote[BOOK_EXCERPT_CAP+12];snprintf(quote,sizeof(quote),"“%s”",s_excerpt);
        int px=22;while(px>16&&ui_text_fixed_width_px(px,quote)>426)--px;
        if(ui_text_fixed_width_px(px,quote)<=426)label_on(fb,622,222,px,quote,EPD_DRAW_ALIGN_RIGHT,0x60,card_gray);
    }
    ui_draw_separator(fb,252,66,552,0x50);
    HOME_STAGE("card_top");
    home_book_t *b=featured();
    if(b->path[0]) {
        // 按住整张卡片时封面轻抬4px；原边框不动，不追加黑框或整页刷新。
        // Lift the cover4px while holding any part of the card; keep its frame still without extra boxes/full refreshes.
        draw_cover(fb,b,(EpdRect){60,continue_card_pressed()?274:278,176,240});
        int rows=title_lines(fb,b->title,card_gray);
        if(b->author[0]) {char author[128];snprintf(author,sizeof(author),"%s",b->author);fit(author,25,358);label_on(fb,264,rows==2?406:367,25,author,EPD_DRAW_ALIGN_LEFT,0x50,card_gray);}
        snprintf(value,sizeof(value),"已读 %u%%",b->has_progress?b->progress.pct:0);
        label_on(fb,264,482,27,value,EPD_DRAW_ALIGN_LEFT,0x20,card_gray);
        label_on(fb,622,482,24,"继续阅读 ›",EPD_DRAW_ALIGN_RIGHT,0,card_gray);
        EpdRect track={264,511,358,7};ui_fill_round_rect(fb,track,3,0xcc);
        track.width=358*(b->has_progress?b->progress.pct:0)/100;if(track.width)ui_fill_round_rect(fb,track,3,0x30);
    } else label_on(fb,342,410,23,"打开一本书后，会在这里继续阅读",EPD_DRAW_ALIGN_CENTER,0x60,card_gray);
    HOME_STAGE("card_body");
    ui_draw_separator(fb,606,36,612,0x50);
    if(app_settings_home_component()==APP_HOME_FOLDER) {
        ui_home_folder_draw(fb,s_folder,s_folder_fast,(EpdRect){24,604,320,350});
        label(fb,184,985,22,"最近阅读",EPD_DRAW_ALIGN_CENTER,0x50);
    } else {
        clock_draw_region(fb,(EpdRect){24,640,320,350},&s_date,app_settings_main_fast_refresh(),clock_style(app_settings_home_component()),(EpdRect){24,640,320,326},s_widget_phase);
        label(fb,184,985,22,component_name(app_settings_home_component()),EPD_DRAW_ALIGN_CENTER,0x50);
    }
    HOME_STAGE("component");
    if(s_reading_days_valid) {
        static const char *const wd[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
        snprintf(value,sizeof(value),"%d月%d日",s_date.tm_mon+1,s_date.tm_mday);
        label(fb,354,665,30,value,EPD_DRAW_ALIGN_LEFT,0);
        label(fb,642,665,24,wd[s_date.tm_wday],EPD_DRAW_ALIGN_RIGHT,0x50);
        int start=(s_date.tm_wday+7-(s_date.tm_mday-1)%7)%7;
        int rows=(start+month_length(&s_date)+6)/7;
        calendar(fb,375,754,42,rows==6?38:48,26,app_settings_home_goal_minutes()*60);
    } else label(fb,496,754,22,"请先设置日期与时间",EPD_DRAW_ALIGN_CENTER,0x60);
    label(fb,642,985,22,"· 已闭环",EPD_DRAW_ALIGN_RIGHT,0x50);
    HOME_STAGE("calendar");
    ui_nav_draw(fb,0);
    HOME_STAGE("nav");
#undef HOME_STAGE
}
static void render_goal(uint8_t *fb) {
    header(fb,"月阅读记录");char value[64];
    snprintf(value,sizeof(value),s_reading_days_valid?"%d年%d月":"日期未设置",s_date.tm_year+1900,s_date.tm_mon+1);
    label(fb,36,226,34,value,EPD_DRAW_ALIGN_LEFT,0);
    ring(fb,622,226,22,6,today_seconds(),s_goal_draft*60);
    calendar(fb,78,348,88,56,32,s_goal_draft*60);
    label(fb,36,678,24,"今日阅读",EPD_DRAW_ALIGN_LEFT,0x50);
    snprintf(value,sizeof(value),"%u 分钟",today_seconds()/60);label(fb,648,678,30,value,EPD_DRAW_ALIGN_RIGHT,0);
    label(fb,36,744,30,"每日闭环目标",EPD_DRAW_ALIGN_LEFT,0);
    label(fb,36,784,22,"达到目标的日期会显示下划线",EPD_DRAW_ALIGN_LEFT,0x60);
    ui_fill_round_rect(fb,(EpdRect){36,820,612,114},57,0xe0);
    home_frame(fb,(EpdRect){36,820,612,114},57);
    epd_fill_circle(99,877,43,0xff,fb);epd_fill_circle(585,877,43,0xff,fb);
    circle_frame(fb,99,877,43);circle_frame(fb,585,877,43);
    ui_fill_rect_fast((EpdRect){87,876,24,3},s_goal_draft==5?0xa0:0x30,fb);
    ui_fill_rect_fast((EpdRect){573,876,24,3},s_goal_draft==180?0xa0:0x30,fb);
    ui_fill_rect_fast((EpdRect){584,866,3,24},s_goal_draft==180?0xa0:0x30,fb);
    snprintf(value,sizeof(value),"%u",s_goal_draft);label(fb,342,861,46,value,EPD_DRAW_ALIGN_CENTER,0);
    label(fb,342,902,20,"分钟",EPD_DRAW_ALIGN_CENTER,0x50);
    const unsigned presets[]={15,30,60};
    for(unsigned i=0;i<3;++i) {
        EpdRect box={36+i*214,920,184,56};ui_fill_round_rect(fb,box,28,s_goal_draft==presets[i]?0xe0:0xf0);home_frame(fb,box,28);
        snprintf(value,sizeof(value),"%u 分钟",presets[i]);label(fb,box.x+92,948,25,value,EPD_DRAW_ALIGN_CENTER,0x20);
    }
    if(s_save_error)label(fb,342,990,23,"保存未完成，请再试一次",EPD_DRAW_ALIGN_CENTER,0x30);
    ui_fill_round_rect(fb,(EpdRect){36,1008,612,72},28,0x20);label(fb,342,1044,28,"保存",EPD_DRAW_ALIGN_CENTER,0xff);
}
static void render_component_controls(uint8_t *fb) {
    circle_frame(fb,68,504,32);circle_frame(fb,616,504,32);
    ui_draw_icon(fb,68,504,34,UI_ICON_CHEVRON_LEFT,0x30);ui_draw_icon(fb,616,504,34,UI_ICON_CHEVRON_RIGHT,0x30);
    bool current=s_component_browse==app_settings_home_component();
    bool pressed=s_press_pending&&s_pressed.y==996;
    ui_fill_round_rect(fb,(EpdRect){36,996,612,76},38,pressed?0xe0:0x20);
    label(fb,342,1034,30,current?"返回首页":"使用此样式",EPD_DRAW_ALIGN_CENTER,pressed?0x20:0xff);
}
static void render_components_body(uint8_t *fb) {
    if(s_component_browse==APP_HOME_FOLDER)ui_home_folder_draw(fb,s_folder,s_folder_fast,(EpdRect){132,282,420,459});
    else {
#if KIIKO_BOARD_METALIO47
        ui_home_clock_preview_draw(fb,(EpdRect){141,294,420,460},app_settings_main_fast_refresh(),clock_style(s_component_browse));
#else
        clock_draw_region(fb,(EpdRect){141,294,420,460},&s_date,app_settings_main_fast_refresh(),clock_style(s_component_browse),(EpdRect){141,294,420,460},s_widget_phase);
#endif
    }
    label(fb,342,787,36,component_name(s_component_browse),EPD_DRAW_ALIGN_CENTER,0);
    unsigned selected=component_index(s_component_browse);
    static const char *const details[]={"圆形表盘，十二个月相刻度","圆滑短底座，清晰数字表盘","短时针、长分针，圆点为秒针","猫眼左右转动，没有尾巴","柔软轮廓，弯曲的罗马数字","深灰放射图案，白色指针配细描边","尖屋顶、圆形开口，四个数字刻度","三本最近阅读，透过磨砂书夹显示"};
    label(fb,342,838,23,details[selected],EPD_DRAW_ALIGN_CENTER,0x60);
    for(unsigned i=0;i<HOME_COMPONENT_OPTIONS;++i)epd_fill_circle(342-(HOME_COMPONENT_OPTIONS-1)*12+i*24,909,i==selected?6:4,i==selected?0x20:app_settings_main_fast_refresh()?0x60:0xc0,fb);
    char value[24];snprintf(value,sizeof(value),"%u / %u",selected+1,(unsigned)HOME_COMPONENT_OPTIONS);label(fb,342,953,23,value,EPD_DRAW_ALIGN_CENTER,0x60);
    bool current=s_component_browse==app_settings_home_component();
    if(s_save_error)label(fb,342,1008,23,"保存未完成，请再试一次",EPD_DRAW_ALIGN_CENTER,0x30);
    else if(current)label(fb,342,1008,23,"当前使用",EPD_DRAW_ALIGN_CENTER,0x50);
    render_component_controls(fb);
}
static void render_components(uint8_t *fb) {
    header(fb,"首页组件");label(fb,36,195,24,"选择你喜欢的样式",EPD_DRAW_ALIGN_LEFT,0x60);
    render_components_body(fb);
}
static void render_time_value(uint8_t *fb,const struct tm *date) {
    char value[18];if(s_reading_days_valid)snprintf(value,sizeof(value),"%02d:%02d:%02d",date->tm_hour,date->tm_min,date->tm_sec);else snprintf(value,sizeof(value),"--:--:--");
    int px=72;while(px>36&&ui_text_fixed_width_px(px,value)>540)--px;
    ui_fill_rect_fast((EpdRect){72,840,540,98},0xff,fb);label(fb,342,884,px,value,EPD_DRAW_ALIGN_CENTER,0);
}
static void render_time(uint8_t *fb) {
    header(fb,"时间");char value[64];
    if(s_reading_days_valid)snprintf(value,sizeof(value),"%d年%d月%d日",s_date.tm_year+1900,s_date.tm_mon+1,s_date.tm_mday);else snprintf(value,sizeof(value),"请先设置日期与时间");
    label(fb,342,219,30,value,EPD_DRAW_ALIGN_CENTER,0x50);
    clock_draw_region(fb,time_clock_bounds(),&s_date,app_settings_main_fast_refresh(),clock_style(app_settings_home_component()),time_clock_bounds(),s_time_phase);
    label(fb,342,804,32,component_name(app_settings_home_component()),EPD_DRAW_ALIGN_CENTER,0x30);
    render_time_value(fb,&s_date);label(fb,342,975,23,"北京时间 · 24小时制",EPD_DRAW_ALIGN_CENTER,0x60);
    bool pressed=s_press_pending&&s_pressed.y==996;
    ui_fill_round_rect(fb,(EpdRect){36,996,612,76},38,pressed?0xe0:0x20);label(fb,342,1034,30,"切换时钟样式",EPD_DRAW_ALIGN_CENTER,pressed?0x20:0xff);
}
static void render(app_ctx_t *ctx,uint8_t *fb) {
    (void)ctx;ui_clear_page(fb);ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT},0xff,fb);
    if(s_view==HOME_GOAL){s_widget_trail.valid=s_time_trail.valid=false;}
    s_widget_phase=s_time_phase=clock_phase_now(&s_date);
    if(s_view==HOME_ROOT){ui_root_background(fb);ui_fill_round_rect(fb,(EpdRect){24,632,320,374},24,UI_GRAY_WHITE);ui_fill_round_rect(fb,(EpdRect){354,632,300,374},24,UI_GRAY_WHITE);render_root(fb);}else if(s_view==HOME_GOAL)render_goal(fb);else if(s_view==HOME_TIME)render_time(fb);else render_components(fb);
    if(s_press_pending && !continue_card_pressed())ui_draw_selected_round_rect(fb,s_pressed,s_pressed.width==60?30:s_pressed.width==64?32:s_pressed.width==86?43:s_pressed.height==76?38:20);
    if(s_view!=HOME_ROOT)ui_nav_draw(fb,0);
}
static enum EpdDrawError present_clock_area(app_ctx_t *ctx,EpdRect area) {
    // 脏行和脏列交集包含未变化灰底，必须使用等灰保持表，不能重刷该矩形。
    // Dirty-row/column intersections include unchanged grays; hold those pairs instead of driving the rectangle.
    const bool binary=app_settings_main_fast_refresh();
    return update_display_area_diff_with(ctx->hl,
        binary?display_main_bw_waveform():display_main_gray_waveform(),binary?MODE_DU:MODE_GL16,area);
}
static enum EpdDrawError present_clock_motion(app_ctx_t *ctx,EpdRect area) {
    // 秒针只扫变化圆点的短相位；灰底等值保持，不刷框、不重排整页。
    // Short phases drive only changed dot pixels, holding the gray face without a frame or whole-page paint.
    return update_display_area_diff_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
}
static bool present(app_ctx_t *ctx,app_redraw_t redraw) {
    if(redraw==APP_REDRAW_NONE)return true;
    int64_t minute=ui_nav_clock_minute();
    if(redraw==APP_REDRAW_AREA&&s_view==HOME_COMPONENT&&(s_component_pending||s_area.width==64||s_area.height==76)) {
        if(s_component_pending)s_area=s_component_dirty;
        ui_fill_rect_fast(s_area,0xff,ctx->fb);
        if(s_component_pending){s_widget_phase=clock_phase_now(&s_date);render_components_body(ctx->fb);}
        else render_component_controls(ctx->fb);
        if(s_press_pending)ui_draw_selected_round_rect(ctx->fb,s_pressed,s_pressed.width==64?32:38);
        enum EpdDrawError result=present_clock_area(ctx,s_area);guard_draw_result(ctx->hl,result);
#if KIIKO_BOARD_METALIO47
        s_component_warm_after_ms=esp_timer_get_time()/1000+600;
#endif
        if(result==EPD_DRAW_SUCCESS){s_component_pending=false;s_component_dirty=(EpdRect){0};if(visible_motion_clock()){s_widget_shown=s_date;s_widget_stamp=date_stamp(&s_date);s_widget_phase_shown=s_widget_phase;s_motion_ms=ctx->now_ms;}}
        return true;
    }
    if(redraw==APP_REDRAW_AREA&&s_widget_pending&&visible_motion_clock()){
        s_widget_pending=false;bool repair=s_widget_stamp<0;bool whole=repair||s_widget_date.tm_min!=s_widget_shown.tm_min||s_widget_date.tm_hour!=s_widget_shown.tm_hour;
        ui_home_clock_style_t style=clock_style(s_view==HOME_COMPONENT?s_component_browse:app_settings_home_component());
        bool skipped=style==UI_HOME_CLOCK_KEHAI&&(s_widget_phase+600-s_widget_phase_shown)%600>20;
        EpdRect box=component_clock_bounds(),dirty=whole||skipped?component_clock_graphics():ui_home_clock_motion_area(box,s_widget_phase_shown,s_widget_phase,style);
        clock_draw_region(ctx->fb,box,&s_widget_date,app_settings_main_fast_refresh(),style,dirty,s_widget_phase);
        if(minute!=s_clock_minute){ui_fill_rect_fast((EpdRect){24,18,156,44},0xff,ctx->fb);ui_nav_clock_draw(ctx->fb);dirty=ui_rect_union(dirty,(EpdRect){24,18,156,44});}
        enum EpdDrawError result=repair?present_clock_area(ctx,dirty):present_clock_motion(ctx,dirty);
        guard_draw_result(ctx->hl,result);
        if(result==EPD_DRAW_SUCCESS){s_widget_shown=s_widget_date;s_widget_stamp=date_stamp(&s_widget_date);s_widget_phase_shown=s_widget_phase;s_motion_ms=ctx->now_ms;s_clock_minute=minute;s_date=s_widget_date;}else s_widget_stamp=-1;return true;
    }
    if(redraw==APP_REDRAW_AREA&&s_time_pending&&s_view==HOME_TIME) {
        s_time_pending=false;bool repair=s_time_stamp<0;bool whole=repair||s_time_date.tm_min!=s_date.tm_min||s_time_date.tm_hour!=s_date.tm_hour;
        ui_home_clock_style_t style=clock_style(app_settings_home_component());
        EpdRect changes={0};
        if(s_time_stamp!=date_stamp(&s_time_date)){render_time_value(ctx->fb,&s_time_date);changes=(EpdRect){72,840,540,98};}
        if(whole||ui_home_clock_has_motion(style)){
            EpdRect box=time_clock_bounds();bool skipped=style==UI_HOME_CLOCK_KEHAI&&(s_time_phase+600-s_time_phase_shown)%600>20;
            EpdRect dirty=whole||skipped?box:ui_home_clock_motion_area(box,s_time_phase_shown,s_time_phase,style);
            clock_draw_region(ctx->fb,box,&s_time_date,app_settings_main_fast_refresh(),style,dirty,s_time_phase);
            changes=ui_rect_union(changes,dirty);
        }
        if(minute!=s_clock_minute){ui_fill_rect_fast((EpdRect){24,18,156,44},0xff,ctx->fb);ui_nav_clock_draw(ctx->fb);changes=ui_rect_union(changes,(EpdRect){24,18,156,44});}
        // 秒级数字与圆点合在同一次短局刷，避免数字刷新打断圆周运动。
        // Present second digits and the dot in one short partial update, avoiding a digit-induced pause in circular motion.
        enum EpdDrawError result=ui_home_clock_has_motion(style)&&!repair?present_clock_motion(ctx,changes):present_clock_area(ctx,changes);
        guard_draw_result(ctx->hl,result);
        if(result==EPD_DRAW_SUCCESS){s_date=s_time_date;s_time_stamp=date_stamp(&s_date);s_time_phase_shown=s_time_phase;s_motion_ms=ctx->now_ms;s_clock_minute=minute;}else s_time_stamp=-1;
        return true;
    }
    if(redraw==APP_REDRAW_AREA&&s_clock_pending) {
        s_clock_pending=false;
        ui_fill_rect_fast((EpdRect){24,18,156,44},0xff,ctx->fb);ui_nav_clock_draw(ctx->fb);
#if KIIKO_BOARD_METALIO47
        if(display_pipeline_enabled()) {
            EpdRect dirty={24,18,156,44};
            if(s_view==HOME_ROOT&&app_settings_home_component()!=APP_HOME_FOLDER) {
                date_now(&s_date);ui_home_clock_style_t style=clock_style(app_settings_home_component());
                clock_draw_region(ctx->fb,(EpdRect){24,640,320,350},&s_date,app_settings_main_fast_refresh(),style,(EpdRect){24,640,320,326},s_widget_phase);
                dirty=ui_rect_union(dirty,style==UI_HOME_CLOCK_PS1995?(EpdRect){78,684,198,198}:component_clock_graphics());
            }
            enum EpdDrawError result=present_clock_area(ctx,dirty);
            guard_draw_result(ctx->hl,result);if(result==EPD_DRAW_SUCCESS)s_clock_minute=minute;return true;
        }
#endif
        enum EpdDrawError result=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,(EpdRect){24,18,156,44});
        if(result==EPD_DRAW_SUCCESS&&s_view==HOME_ROOT&&app_settings_home_component()!=APP_HOME_FOLDER) {
            date_now(&s_date);ui_home_clock_style_t style=clock_style(app_settings_home_component());
            clock_draw_region(ctx->fb,(EpdRect){24,640,320,350},&s_date,app_settings_main_fast_refresh(),style,(EpdRect){24,640,320,326},s_widget_phase);
            result=present_clock_area(ctx,style==UI_HOME_CLOCK_PS1995?(EpdRect){78,684,198,198}:component_clock_graphics());
        }
        guard_draw_result(ctx->hl,result);if(result==EPD_DRAW_SUCCESS)s_clock_minute=minute;return true;
    }
    s_clock_pending=s_widget_pending=s_component_pending=false;render(ctx,ctx->fb);enum EpdDrawError result;
    if(redraw==APP_REDRAW_AREA)result=update_display_area_diff_with(ctx->hl,
        s_press_pending?&E0470_FOLLOW_WAVEFORM:&E0470_WAVEFORM,s_press_pending?MODE_DU:MODE_GL16,s_area);
    else if(redraw==APP_REDRAW_FULL||s_home_force_full_once) {s_home_force_full_once=false;result=update_display_full(ctx->hl);}
    else result=update_display_fast_page(ctx->hl);
    guard_draw_result(ctx->hl,result);
#if KIIKO_BOARD_METALIO47
    if(s_view==HOME_COMPONENT)s_component_warm_after_ms=esp_timer_get_time()/1000+600;
#endif
    if(redraw!=APP_REDRAW_AREA&&result==EPD_DRAW_SUCCESS){s_clock_minute=minute;
        if(s_view==HOME_TIME){s_time_stamp=date_stamp(&s_date);s_time_phase_shown=s_time_phase;}
        if(visible_motion_clock()){s_widget_shown=s_date;s_widget_stamp=date_stamp(&s_date);s_widget_phase_shown=s_widget_phase;}
        s_motion_ms=ctx->now_ms;
    }
    return true;
}
static void refresh_excerpt(void) {
    const home_book_t *book=featured();
    s_excerpt[0]=0;
    if(book->has_progress)book_excerpt_load(book->path,book->progress.chapter,s_excerpt);
}
static unsigned s_shelf_warm_next;static int64_t s_shelf_warm_after;
static void on_enter(app_ctx_t *ctx) {
    app_book_home_shelf_warm_stop();s_shelf_warm_next=0;s_shelf_warm_after=ctx->now_ms+1200;
    s_component_dirty=(EpdRect){0};
    s_cover_generation=book_cover_jobs_begin();
    const uint32_t progress_revision=book_progress_revision();
    s_clock_base_attempted=false;clock_base_prepare();
#if KIIKO_BOARD_METALIO47
    // Complete previews include font-rendered numerals; discard after returning from font/settings pages.
    ui_home_clock_preview_invalidate();
#endif
    s_widget_trail.valid=s_time_trail.valid=false;
    (void)ctx;s_view=HOME_ROOT;s_clock_pending=s_time_pending=s_widget_pending=s_component_pending=false;s_press_pending=false;s_save_error=false;s_time_stamp=s_widget_stamp=-1;s_motion_ms=-100;
    s_home_force_full_once=app_settings_home_full_refresh();refresh_days();avatar_prepare_state();
    read_pico_sd_info_t sd={0};esp_err_t error=read_pico_sd_get_info(&sd);
    bool same=s_books_cache_valid&&error!=ESP_ERR_NOT_FINISHED&&s_cached_store_revision==book_store_revision()&&
        s_cached_sd_present==sd.present&&s_cached_sd_mounted==sd.mounted&&!strcmp(s_cached_books_dir,app_settings_books_dir());
    char last[BOOK_STORE_PATH_MAX]={0};book_progress_last_path(last,sizeof(last));
    bool changed=strcmp(last,s_cached_last_path);
    if(same&&!changed){if(s_cached_progress_revision!=progress_revision)refresh_progress();s_scan_pending=false;}
    else {
        memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));s_scan_pending=!same;
        if(s_scan_pending) read_pico_sd_start_probe();
        scan_books();
        if(same||error!=ESP_ERR_NOT_FINISHED)remember_scan_state(&sd);
    }
    if(!same||changed)restore_cover_cache();
    rp_boot_mark("scan");
    s_cached_progress_revision=progress_revision;
    s_cover_next=0;
    if(app_settings_home_component()==APP_HOME_FOLDER&&(!same||changed||!s_folder_key)){uint32_t key=folder_key();
        if(key!=s_folder_key){free(s_folder);s_folder=NULL;s_folder_attempted=false;}}
    refresh_excerpt();
    rp_boot_mark("excerpt");
}
static void on_media_lost(app_ctx_t *ctx) {
    app_book_home_shelf_warm_stop();
#if KIIKO_BOARD_METALIO47
    metalio_cover_cache_clear();
#else
#endif
    app_home_release_clock_cache();
    (void)ctx;clear_cover_cache();memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));
    s_books_cache_valid=false;s_excerpt[0]=0;s_avatar_ready=s_avatar_pending=false;
}
static void on_media_ready(app_ctx_t *ctx) {(void)ctx;s_books_cache_valid=false;s_scan_pending=true;avatar_prepare_state();}
static app_redraw_t on_tick(app_ctx_t *ctx) {
    clock_base_prepare();
    if(ctx->consumed||(ctx->touch&&ctx->touch->touched))return APP_REDRAW_NONE;
    if(s_view==HOME_COMPONENT&&s_component_pending){s_area=s_component_dirty;return APP_REDRAW_AREA;}
    bool folder_preview=s_view==HOME_COMPONENT&&s_component_browse==APP_HOME_FOLDER;
    if(s_view==HOME_ROOT||folder_preview){
    if(s_scan_pending) {
        read_pico_sd_info_t info={0};if(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FINISHED)return APP_REDRAW_NONE;
        s_scan_pending=false;
        if(info.mounted) {
            s_cover_generation=book_cover_jobs_begin();
            memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));s_cover_next=0;
            scan_books();restore_cover_cache();remember_scan_state(&info);refresh_excerpt();avatar_prepare_state();s_folder_attempted=false;
            free(s_folder);s_folder=NULL;return APP_REDRAW_PAGE;
        }remember_scan_state(&info);
    }
    if(s_view==HOME_ROOT&&s_avatar_pending) {
        ui_avatar_state_t state=ui_avatar_poll(s_avatar_path,60,s_avatar);
        s_avatar_pending=state==UI_AVATAR_PENDING;
        s_avatar_ready=state==UI_AVATAR_READY;
        if(s_avatar_ready&&s_view==HOME_ROOT){s_area=(EpdRect){588,88,60,60};return APP_REDRAW_AREA;}
    }
        if(s_view==HOME_ROOT&&s_cover_next==0){
            s_cover_next=1;home_book_t *b=featured();
            if(b->path[0]&&!b->cover){int slot=b==&s_current?0:(int)(b-s_recent)+1;if(load_cover(b,slot)){s_area=(EpdRect){60,278,176,240};return APP_REDRAW_AREA;}if(s_cover_waiting)s_cover_next=0;}
        }
    }
    if(s_view==HOME_COMPONENT&&s_component_pending){s_area=s_component_dirty;return APP_REDRAW_AREA;}
    if(s_view==HOME_TIME){
        struct tm date;if(!date_now(&date))return APP_REDRAW_NONE;
        unsigned phase=clock_phase_now(&date);
        if(date.tm_yday!=s_date.tm_yday||date.tm_year!=s_date.tm_year){refresh_days();return APP_REDRAW_PAGE;}
        if(date_stamp(&date)!=s_time_stamp||(ui_home_clock_has_motion(clock_style(app_settings_home_component()))&&phase!=s_time_phase_shown&&clock_motion_due(ctx))){s_time_date=date;s_time_phase=phase;s_time_pending=true;return APP_REDRAW_AREA;}return APP_REDRAW_NONE;
    }
    if(visible_motion_clock()){
        struct tm date;if(date_now(&date)){
            unsigned phase=clock_phase_now(&date);
            if(date.tm_yday!=s_date.tm_yday||date.tm_year!=s_date.tm_year){refresh_days();return APP_REDRAW_PAGE;}
            if(clock_motion_due(ctx)&&(date_stamp(&date)!=s_widget_stamp||phase!=s_widget_phase_shown)){s_widget_date=date;s_widget_phase=phase;s_widget_pending=true;return APP_REDRAW_AREA;}
        }
    }
    if(ui_nav_clock_minute()!=s_clock_minute) {
        struct tm date;bool valid=date_now(&date);
        if(valid&&(!s_reading_days_valid||date.tm_yday!=s_date.tm_yday||date.tm_year!=s_date.tm_year)) {refresh_days();return APP_REDRAW_PAGE;}
        if(valid)s_date=date;
        s_clock_pending=true;s_area=(EpdRect){24,18,156,44};return APP_REDRAW_AREA;
    }
    // 只为正在预览的书夹准备封面；切到时钟即停止隐藏文件处理。
    // Prepare covers only for the visible folder preview; switching to a clock stops hidden media work.
    if(s_view==HOME_COMPONENT&&!folder_preview){
#if KIIKO_BOARD_METALIO47
        component_prepare_neighbor(ctx);
#endif
        return APP_REDRAW_NONE;
    }
    if(s_view==HOME_GOAL)return APP_REDRAW_NONE;
    home_book_t *books[3];unsigned n=folder_books(books);
    if(s_cover_next<n+1) {
        unsigned i=s_cover_next++;home_book_t *b=i?books[i-1]:featured();
        if(b->path[0]&&!b->cover) {
            int slot=b==&s_current?0:(int)(b-s_recent)+1;
            if(load_cover(b,slot)&&!i&&s_view==HOME_ROOT){s_area=(EpdRect){60,278,176,240};return APP_REDRAW_AREA;}
            if(s_cover_waiting)--s_cover_next;
        }
        return APP_REDRAW_NONE;
    }
    bool needs_folder=(s_view==HOME_ROOT&&app_settings_home_component()==APP_HOME_FOLDER)||(s_view==HOME_COMPONENT&&s_component_browse==APP_HOME_FOLDER);
    if(needs_folder&&!s_folder&&!s_folder_attempted) {
        if(prepare_folder()){
            if(folder_preview){s_component_pending=true;s_area=s_component_dirty=(EpdRect){36,260,612,812};}
            else s_area=(EpdRect){24,604,320,350};
            return APP_REDRAW_AREA;
        }
        s_folder_attempted=true;
    }
    if(s_view==HOME_ROOT&&!s_scan_pending&&!s_cover_waiting&&ctx->now_ms>=s_shelf_warm_after&&
       !ctx->consumed&&!ctx->request_app&&!ctx->request_menu&&!ctx->request_return&&!(ctx->touch&&ctx->touch->touched)&&s_shelf_warm_next<n+1){
        home_book_t *warm=s_shelf_warm_next?books[s_shelf_warm_next-1]:featured();
        if(app_book_home_shelf_warm(warm->path,warm->title,warm->author)){++s_shelf_warm_next;s_shelf_warm_after=ctx->now_ms+100;}
    }
    return APP_REDRAW_NONE;
}
static bool hit(EpdRect r,int x,int y) {return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static EpdRect control(int x,int y) {
    if(s_view==HOME_ROOT) {
        if(featured()->path[0] && hit(continue_card_hit_rect(),x,y))return continue_card_rect();
        if(hit((EpdRect){510,88,60,60},x,y))return(EpdRect){510,88,60,60};
    }else if(hit((EpdRect){36,78,60,60},x,y))return(EpdRect){0}; // 共享圆圈反馈负责题头返回，避免再叠错位框。/ Shared circle feedback owns header backs; do not add a shifted frame.
    if(s_view==HOME_COMPONENT){
        if(hit((EpdRect){36,472,64,64},x,y))return(EpdRect){36,472,64,64};
        if(hit((EpdRect){584,472,64,64},x,y))return(EpdRect){584,472,64,64};
    }
    if((s_view==HOME_COMPONENT||s_view==HOME_TIME)&&hit((EpdRect){36,996,612,76},x,y))return(EpdRect){36,996,612,76};
    if(s_view==HOME_GOAL) {
        if(hit((EpdRect){56,834,86,86},x,y))return(EpdRect){56,834,86,86};
        if(hit((EpdRect){542,834,86,86},x,y))return(EpdRect){542,834,86,86};
        if(hit((EpdRect){36,1008,612,72},x,y))return(EpdRect){36,1008,612,72};
    }
    return(EpdRect){0};
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    if(ev->type==UI_GESTURE_PRESS) {
        s_pressed=control(ev->x0,ev->y0);
#if KIIKO_BOARD_METALIO47
        s_component_warm_after_ms=ctx->now_ms+600;
        // 箭头不先推屏，松手后的新样式承担本次反馈，避免连续两次扫描。
        // Let the released arrow's new style provide feedback in one output, without a preceding press scan.
        if(s_view==HOME_COMPONENT&&s_pressed.width==64){s_press_pending=false;return APP_REDRAW_NONE;}
#endif
        s_press_pending=s_pressed.width>0;
        if(s_press_pending){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}
        return APP_REDRAW_NONE;
    }
    bool restore=s_press_pending;
    if(ev->type==UI_GESTURE_MOVE) {if(s_press_pending&&!hit(continue_card_pressed()?continue_card_hit_rect():s_pressed,ev->x,ev->y)){s_press_pending=false;s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;}
    s_press_pending=false;
#if KIIKO_BOARD_METALIO47
    s_component_warm_after_ms=ctx->now_ms+600;
#endif
    if(ev->type==UI_GESTURE_LONG_PRESS&&s_view==HOME_ROOT&&hit((EpdRect){24,640,320,365},ev->x0,ev->y0)) {
        s_component_browse=app_settings_home_component();s_view=HOME_COMPONENT;s_save_error=false;return APP_REDRAW_PAGE;
    }
    if(s_view==HOME_COMPONENT&&ev->y0>=250&&ev->y0<970){
        int direction=ev->type==UI_GESTURE_SWIPE_L?1:ev->type==UI_GESTURE_SWIPE_R?-1:0;
        if(ev->type==UI_GESTURE_TAP&&hit((EpdRect){36,472,64,64},ev->x0,ev->y0))direction=-1;
        if(ev->type==UI_GESTURE_TAP&&hit((EpdRect){584,472,64,64},ev->x0,ev->y0))direction=1;
        if(direction){
            unsigned index=component_index(s_component_browse);
#if KIIKO_BOARD_METALIO47
            bool was_current=s_component_browse==app_settings_home_component(),had_error=s_save_error;
#endif
            s_component_browse=s_components[(index+HOME_COMPONENT_OPTIONS+direction)%HOME_COMPONENT_OPTIONS];
            s_save_error=false;s_widget_pending=false;
#if KIIKO_BOARD_METALIO47
            // 只有“使用此样式/返回首页”改变或清理保存错误时才包含底部按钮。
            // Include the apply button only when its label changes or a save error must be cleared.
            bool controls=had_error||was_current!=(s_component_browse==app_settings_home_component());
            EpdRect next={36,260,612,controls?812:716};
#else
            EpdRect next={36,260,612,812};
#endif
            s_area=s_component_dirty=s_component_pending?ui_rect_union(s_component_dirty,next):next;
            s_component_pending=true;
            return APP_REDRAW_AREA;
        }
    }
    if(ev->type!=UI_GESTURE_TAP) {if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;}
    int x=ev->x0,y=ev->y0;
    if(s_view!=HOME_ROOT&&hit((EpdRect){36,78,60,60},x,y)) {s_view=HOME_ROOT;s_save_error=false;return APP_REDRAW_PAGE;}
    if(s_view==HOME_COMPONENT) {
        if(hit((EpdRect){36,996,612,76},x,y)) {
            s_save_error=s_component_browse!=app_settings_home_component()&&app_settings_set_home_component(s_component_browse)!=ESP_OK;
            if(!s_save_error) s_view=HOME_ROOT;
            return APP_REDRAW_PAGE;
        }
    }else if(s_view==HOME_TIME){
        if(hit((EpdRect){36,996,612,76},x,y)){s_view=HOME_COMPONENT;s_component_browse=app_settings_home_component();s_save_error=false;return APP_REDRAW_PAGE;}
    }else if(s_view==HOME_GOAL) {
        uint8_t old=s_goal_draft;
        if(hit((EpdRect){56,834,86,86},x,y)&&s_goal_draft>5)s_goal_draft-=5;
        else if(hit((EpdRect){542,834,86,86},x,y)&&s_goal_draft<180)s_goal_draft+=5;
        else if(hit((EpdRect){36,1008,612,72},x,y)) {
            s_save_error=app_settings_set_home_goal_minutes(s_goal_draft)!=ESP_OK;if(!s_save_error) s_view=HOME_ROOT;
            return APP_REDRAW_PAGE;
        }else {const uint8_t presets[]={15,30,60};for(int i=0;i<3;++i)if(hit((EpdRect){36+i*214,920,184,56},x,y))s_goal_draft=presets[i];}
        if(old!=s_goal_draft){s_area=(EpdRect){36,198,612,830};return APP_REDRAW_AREA;}
    }else {
        int tab=ui_nav_hit(x,y);if(tab>=0){ui_nav_request(ctx,tab);return APP_REDRAW_NONE;}
        if(hit((EpdRect){24,640,320,365},x,y)){
            if(app_settings_home_component()==APP_HOME_FOLDER){ui_nav_request(ctx,1);return APP_REDRAW_NONE;}
            s_time_trail.valid=false;s_view=HOME_TIME;s_time_pending=false;s_time_stamp=-1;refresh_days();return APP_REDRAW_PAGE;
        }
        if(hit((EpdRect){354,640,294,365},x,y)||hit((EpdRect){510,88,60,60},x,y)) {
            refresh_days();s_goal_draft=app_settings_home_goal_minutes();s_view=HOME_GOAL;s_save_error=false;return APP_REDRAW_PAGE;
        }
        if(hit(continue_card_hit_rect(),x,y)) {
            home_book_t *b=featured();
            if(b->path[0] && app_book_request_open_from_home(b->path)){
#if KIIKO_BOARD_METALIO47
                (void)metalio_haptic_click();
#endif
                ui_nav_request(ctx,1);return APP_REDRAW_NONE;
            }
            if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}
            return APP_REDRAW_NONE;
        }
        if(hit((EpdRect){588,88,60,60},x,y)){ui_nav_request(ctx,3);return APP_REDRAW_NONE;}
    }
    if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key) {(void)ctx;(void)key;if(s_view!=HOME_ROOT){s_view=HOME_ROOT;s_press_pending=false;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;}
static bool no_menu_handle(app_ctx_t *ctx) {(void)ctx;return false;}
static bool main_page_visible(app_ctx_t *ctx) {(void)ctx;return s_view==HOME_ROOT;}
static EpdRect area_hint(app_ctx_t *ctx) {(void)ctx;return s_area;}
static void home_exit(app_ctx_t *ctx){(void)ctx;app_book_home_shelf_warm_stop();book_cover_jobs_cancel(s_cover_generation);s_cover_generation=0;}
const app_desc_t app_dashboard={
    .title="首页 Home",.detail="继续阅读与月阅读记录",.enter_full=false,.owns_keys=true,.menu_handle_enabled=no_menu_handle,
    .main_page_visible=main_page_visible,.persistent_navigation=true,.on_enter=on_enter,.on_exit=home_exit,.on_media_lost=on_media_lost,.on_media_ready=on_media_ready,
    .render=render,.present=present,.on_tick=on_tick,.on_gesture=on_gesture,.on_key=on_key,.area_hint=area_hint,
};

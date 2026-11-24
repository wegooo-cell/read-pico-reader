#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：执行书架位置索引的上一代保留与回退：FAT 式改名不覆盖、改名失败放回、损坏或缺失时读上一代、触摸让路不回退。
# English: Execute shelf index generations: FAT-style non-overwriting rename, restore on a failed rename,
# fall back to the previous index when the current one is missing or corrupt, and never fall back on a touch yield.
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function

R = Path(__file__).resolve().parents[1]
book = R / 'main/apps/app_book.c'
text = book.read_text()
start = text.index('#define SHELF_CACHE_DIR')
end = text.index('static uint32_t shelf_cache_hash(')
defines = text[start:end]

# 固件的临时文件名缓冲只有64字节，测试目录要短。/ The firmware's temp-name buffer is 64 bytes, so keep the test root short.
with tempfile.TemporaryDirectory(dir='/tmp', prefix='kidx') as tmp:
    root = Path(tmp)
    defines = defines.replace('"/sdcard/.kiikoread"', '"' + str(root / 'k') + '"')
    unit = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define KIIKO_BOARD_METALIO47 1
#define BOOK_STORE_PATH_MAX 288
#define TAG "book"
static int warnings;
#define ESP_LOGI(tag, fmt, ...) ((void)(tag), (void)snprintf(NULL, 0, fmt, __VA_ARGS__))
#define ESP_LOGW(tag, ...) ((void)(tag), ++warnings)
typedef struct { int leaf; } app_ctx_t;
typedef struct {
    char path[BOOK_STORE_PATH_MAX], name[256], author[128];
    uint32_t size, recent; uint64_t added; uint16_t chapter; uint8_t pct;
    bool is_flash, has_progress, favorite, search_match, removed;
} shelf_entry_t;
typedef struct { bool present, mounted; } read_pico_sd_info_t;
enum { SHELF = 1 };
static shelf_entry_t s_shelf[16];
static int s_count, s_visible_count, s_view = SHELF, s_cache_shelf_style;
static char s_latest_path[BOOK_STORE_PATH_MAX];
static bool s_cache_sd_present, s_cache_sd_mounted, s_shelf_cache_valid, s_shelf_index_preloaded, touch_pending;
static unsigned s_store_revision, s_progress_revision;
static int fail_rename_to_current;
// FAT 改名不覆盖已存在目标；可注入新索引改名失败。/ FAT rename never overwrites; optionally fail the new-index rename.
static int fat_rename(const char *from, const char *to) {
    struct stat st;
    if (!stat(to, &st)) { errno = EEXIST; return -1; }
    if (fail_rename_to_current && !strstr(to, ".previous") && strstr(from, ".tmp")) { errno = EIO; return -1; }
    return rename(from, to);
}
#define rename fat_rename
static void copy_text(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s ? s : ""); }
static bool shelf_reserve(void) { return s_count < 16; }
static bool shelf_search_match(const char *name, const char *author) { (void)name; (void)author; return true; }
static bool book_file_path_supported(const char *path) { return strstr(path, ".epub") || strstr(path, ".txt"); }
static void shelf_added_load(void) {}
static void shelf_categories_recount(void) {}
static void sort_shelf(app_ctx_t *ctx) { (void)ctx; }
static void shelf_category_restore(app_ctx_t *ctx) { (void)ctx; }
static bool book_progress_last_path(char *out, size_t n) { (void)out; (void)n; return false; }
static int read_pico_sd_get_info(read_pico_sd_info_t *sd) { sd->present = sd->mounted = true; return 0; }
static int app_settings_shelf_style(void) { return 0; }
static unsigned book_store_revision(void) { return 0; }
static unsigned book_progress_revision(void) { return 0; }
static bool app_touch_input_pending(app_ctx_t *ctx) { (void)ctx; return touch_pending; }
''' + defines
    for name in ('shelf_cache_hash', 'shelf_cache_save', 'shelf_cache_load_file', 'shelf_cache_load'):
        unit += function(name, book) + '\n'
    unit += r'''
static void shelf(int count, const char *tag) {
    memset(s_shelf, 0, sizeof(s_shelf)); s_count = count;
    for (int i = 0; i < count; ++i) {
        snprintf(s_shelf[i].path, sizeof(s_shelf[i].path), "/sdcard/books/%s-%d.epub", tag, i);
        snprintf(s_shelf[i].name, sizeof(s_shelf[i].name), "%s-%d", tag, i);
        s_shelf[i].size = 100 + i;
    }
}
static bool exists(const char *path) { struct stat st; return !stat(path, &st); }
static int load(bool background) { app_ctx_t ctx = {0}; s_count = 0; return shelf_cache_load(&ctx, background) ? s_count : -1; }
int main(void) {
    (void)s_visible_count;
    mkdir(SHELF_CACHE_DIR, 0777);
    // 首次保存：没有上一代。/ First save: no previous generation.
    shelf(3, "a"); shelf_cache_save();
    assert(exists(SHELF_CACHE_PATH) && !exists(SHELF_CACHE_PREVIOUS));
    assert(load(false) == 3 && !strcmp(s_shelf[0].name, "a-0") && !warnings);
    // 再次保存：旧索引成为上一代，即使 FAT 不覆盖改名。/ Next save rotates the old index under FAT rename rules.
    shelf(5, "b"); shelf_cache_save();
    assert(exists(SHELF_CACHE_PREVIOUS) && load(false) == 5 && !strcmp(s_shelf[0].name, "b-0"));
    shelf(4, "c"); shelf_cache_save();
    assert(load(false) == 4 && !strcmp(s_shelf[0].name, "c-0"));
    // 当前索引损坏：读上一代并告警。/ A corrupt current index loads the previous generation with a warning.
    FILE *f = fopen(SHELF_CACHE_PATH, "r+b"); assert(f); fseek(f, 40, SEEK_SET); fputc(0x5a, f); fclose(f);
    assert(load(false) == 5 && !strcmp(s_shelf[0].name, "b-0") && warnings == 1);
    // 当前索引缺失：同样读上一代。/ A missing current index also loads the previous generation.
    remove(SHELF_CACHE_PATH);
    assert(load(false) == 5 && warnings == 2);
    // 后台读取被触摸打断时不改读旧索引。/ A touch-yielded background load never falls back.
    touch_pending = true; assert(load(true) == -1); touch_pending = false;
    // 新索引改名失败：上一代放回当前位置，临时文件不残留。/ A failed new-index rename restores the previous index and drops the temp file.
    shelf(2, "d"); shelf_cache_save();
    assert(load(false) == 2);
    shelf(6, "e"); fail_rename_to_current = 1; shelf_cache_save(); fail_rename_to_current = 0;
    assert(exists(SHELF_CACHE_PATH) && !exists(SHELF_CACHE_PATH ".tmp"));
    assert(load(false) == 2 && !strcmp(s_shelf[0].name, "d-0"));
    // 两代都坏时返回失败，交给完整扫描。/ With both generations unreadable the load fails over to a full scan.
    remove(SHELF_CACHE_PATH); remove(SHELF_CACHE_PREVIOUS);
    assert(load(false) == -1);
    puts("PASS: shelf index keeps a previous generation under FAT rename, restores it on a failed rename, falls back when the current index is missing or corrupt, and never falls back on a touch yield");
    return 0;
}
'''
    source = root / 'shelf_index_recovery.c'
    source.write_text(unit)
    binary = root / 'shelf_index_recovery'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(source), '-o', str(binary)], check=True)
    print(subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.strip())

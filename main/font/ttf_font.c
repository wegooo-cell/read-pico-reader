/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 可变 TTF：卡上按需读扇区，glyf/gvar 能装下就整表进 PSRAM；字形按
 * codepoint/字号/字重缓存。内置字体是 Noto Sans SC Medium 子集。
 *
 * Variable TTF: sector I/O from the card; glyf/gvar map into PSRAM when
 * they fit. Glyphs are cached by codepoint, size and weight. The built-in
 * font is a Noto Sans SC Medium subset.
 * 用户修订：标题复用当前字体的轮廓与缓存；覆盖检查只读现有 cmap，不切字体或另加载字库。
 * User revision: titles reuse the active outlines/cache; coverage checks read the existing cmap without switching or loading another face.
 */

#include "ttf_font.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "settings.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static void* ttf_malloc(size_t size, void* userdata) {
    (void)userdata;
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void ttf_free(void* ptr, void* userdata) {
    (void)userdata;
    heap_caps_free(ptr);
}

#define STBTT_malloc(x, u) ttf_malloc((x), (u))
#define STBTT_free(x, u) ttf_free((x), (u))
#define STBTT_assert(x) do { if (!(x)) ESP_LOGE("ttf", "assert %s", #x); } while (0)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

/* ---- 调参 / Tunables ---- */
// 光栅化位图 LRU 上限，PSRAM。/ Raster bitmap LRU cap, PSRAM.
// 字形缓存的总预算。TTF_CACHE_LIMIT 原来是每槽的，多槽之后那是 4 × 512 KB，而字体字节、
// loca 和字形表本身已经吃掉好几 MB；抽干 PSRAM 的症状不是文字出问题，而是插图解码拿不到
// 连续内存——翻几页之后图片就没了（诊断实测：图要 384 KB，只剩 269 KB）。
// 320 KB 够一页正文重新光栅化，代价只是翻页时多算几个字。
// One budget for all glyph caches. TTF_CACHE_LIMIT used to be per slot, which across four slots
// is 4 x 512 KB, on top of the face bytes, loca and the outline tables. Draining PSRAM does not
// show up as broken text but as illustration decoding that cannot get a contiguous block: images
// vanish after a few page turns (measured: an image wanted 384 KB with 269 KB left).
// 320 KB still re-rasterizes a page of body text; the cost is a few extra glyphs per turn.
#define TTF_CACHE_LIMIT (320 * 1024)
// 哈希桶数。/ Hash buckets.
#define TTF_CACHE_BUCKETS 256
// 两档字号的像素高，对齐 ui_kit 正文/标题。/ Small/large px, matches ui_kit body/title.
#define TTF_PIXEL_SMALL 43
#define TTF_PIXEL_LARGE 67
// 打开字体时顺读进 PSRAM 的 glyf 切片上限。/ Max glyf slice mapped into PSRAM at open.
#define TTF_GLYF_ARENA 131072
// 复合字形展开深度。/ Composite glyph walk limit.
#define TTF_TREE_MAX 48
// 简单轮廓点数；变体表多 4 个幻点。/ Simple-glyph points; gvar adds 4 phantom points.
#define TTF_MAX_PTS 512
#define TTF_MAX_VAR_PTS (TTF_MAX_PTS + 4)
// 单字形 gvar 切片缓冲。/ Per-glyph gvar slice buffer.
#define TTF_GVAR_SLICE_MAX 2048
// 可变字重轴的通用边界；静态内建字体忽略此轴。/ Generic variable-weight bounds; the static built-in font ignores this axis.
#define TTF_WGHT_MIN 300
#define TTF_WGHT_MAX 800
#define TTF_WGHT_DEF 300
// FatFs 扇区是 4KB；32 槽把最近扇区留在 PSRAM。/ FatFs sector is 4KB; 32 PSRAM slots keep recent sectors.
#define TTF_IO_BLOCK 4096
#define TTF_IO_SLOTS 32
// 预取时合并的连续块上限。/ Max contiguous blocks merged in one prefetch read.
#define TTF_IO_RUN_MAX 8
#define TTF_PREFETCH_MAX 96
#define TTF_GATHER_MAX 96
// glyf+gvar 整表映射的预留与硬顶。/ Reserve and hard cap for whole-table glyf+gvar maps.
#define TTF_MAP_RESERVE (2560u * 1024u)
#define TTF_MAP_MAX (5u * 1024u * 1024u)

static const char* TAG = "ttf_font";

static const char* const k_font_dirs[] = {
    "/sdcard/assets/fonts",
    NULL,
};

typedef struct glyph_entry {
    uint32_t codepoint;
    uint8_t size;
    uint16_t weight;
    int16_t width;
    int16_t height;
    int16_t left;
    int16_t top;
    int16_t advance_x;
    uint8_t* bitmap;
    size_t bitmap_bytes;
    struct glyph_entry* hash_next;
    struct glyph_entry* lru_prev;
    struct glyph_entry* lru_next;
} glyph_entry_t;

typedef struct {
    float scale;
    int ascent;
} ttf_size_metrics_t;

typedef struct {
    char tag[4];
    uint32_t offset;
    uint32_t length;
} sfnt_table_t;

/* ---- SD 分块 IO / SD block I/O ---- */
// 字形/gvar 仍按需从文件取。FatFs 扇区是 4KB，但每个新字往往落在不同扇区，
// 1-bit SD 上一次随机读就要好几毫秒。这里用 32 个 4KB 槽把最近扇区留在
// PSRAM；画一行字之前按文件偏移把用到的块排序合并再读。glyf/gvar 若装得
// 下，打开字体时顺读进 PSRAM，之后冷启动不再碰卡。
// Glyphs/gvar are still fetched on demand. FatFs sectors are 4KB, but each
// new glyph usually sits on a different sector; a random 1-bit SD read costs
// several milliseconds. 32×4KB PSRAM slots keep recent sectors. Before a
// line is drawn, needed blocks are sorted and merged. If glyf/gvar fit,
// they are streamed into PSRAM at open so a cold start does not touch the card.
extern const uint8_t builtin_ttf_start[] asm("_binary_builtin_ttf_start");
extern const uint8_t builtin_ttf_end[] asm("_binary_builtin_ttf_end");
typedef struct {
    uint8_t slice[TTF_GVAR_SLICE_MAX];
    int16_t x[TTF_MAX_PTS];
    int16_t y[TTF_MAX_PTS];
    int16_t endpts[64];
    uint8_t on[TTF_MAX_PTS];
    uint8_t flags[TTF_MAX_PTS];
    float dx[TTF_MAX_PTS];
    float dy[TTF_MAX_PTS];
    float tdx[TTF_MAX_PTS];
    float tdy[TTF_MAX_PTS];
    uint8_t setx[TTF_MAX_PTS];
    uint8_t sety[TTF_MAX_PTS];
    int16_t xdel[TTF_MAX_VAR_PTS];
    int16_t ydel[TTF_MAX_VAR_PTS];
    uint16_t pts[TTF_MAX_VAR_PTS];
    uint16_t shared_pts[TTF_MAX_VAR_PTS];
} ttf_work_t;

// 每套字体一份的状态。字段一律加 f_ 前缀，是刻意的：下面那组同名宏把旧的全局名映射到
// “当前槽”，字段若与宏同名，`s->name` 与 struct 内的字段声明都会被展开成语法错误。
// Per-face state. The f_ prefix is deliberate: the same-name macros below map the old
// globals onto the current slot, so a same-named field would be macro-expanded both in
// `s->name` accesses and inside this struct body.
typedef struct {
    int f_fd;
    const uint8_t* f_mem;
    uint32_t f_mem_len;
    bool f_mem_owned;      ///< f_mem 在堆上、关闭槽位时释放 / f_mem is heap, freed on close
    uint32_t f_file_pos;
    char f_path[TTF_FONT_PATH_MAX];
    int f_packed_root;
    int f_packed_weight;
    uint32_t f_file_glyf_off;
    uint32_t f_file_glyf_len;
    uint8_t* f_file_loca;
    uint32_t f_file_loca_len;
    bool f_loca_long;
    int f_num_glyphs;
    uint8_t* f_glyf_ram;
    uint32_t f_glyf_ram_off;
    uint32_t f_glyf_ram_len;
    uint8_t* f_gvar_ram;
    uint32_t f_gvar_ram_off;
    uint32_t f_gvar_ram_len;
    uint8_t* f_io_data;
    uint8_t* f_io_run_buf;
    uint32_t f_io_base[TTF_IO_SLOTS];
    uint16_t f_io_fill[TTF_IO_SLOTS];
    uint16_t f_io_age[TTF_IO_SLOTS];
    uint16_t f_io_clock;
    uint32_t f_touch_blocks[TTF_PREFETCH_MAX];
    int f_touch_n;
    uint8_t* f_font_data;
    uint32_t f_work_loca_off;
    uint32_t f_work_glyf_off;
    stbtt_fontinfo f_font_info;
    bool f_ready;
    // CFF/OpenType（OTTO）没有 glyf/loca：字形由 stb 直接从 CFF 表取，"工作字体"那套
    // 拼装和分块预读都用不上，字体字节也直接借用不复制。
    // A CFF/OpenType (OTTO) face has no glyf/loca: stb reads outlines straight from the CFF
    // table, so the working-font assembly and block prefetch do not apply, and the face bytes
    // are borrowed rather than copied.
    bool f_cff;
    bool f_data_borrowed;
    ttf_size_metrics_t f_size_metrics[2];
    int f_raw_ascent_units;
    int f_weight;
    int f_wght_min;
    int f_wght_def;
    int f_wght_max;
    uint32_t f_file_gvar_off;
    uint32_t f_file_gvar_len;
    uint32_t f_gvar_data_array_off;
    uint32_t* f_gvar_glyph_off;
    int16_t f_shared_tuple[8];
    int f_shared_tuple_count;
    int f_gvar_axis_count;
    bool f_gvar_ready;
    glyph_entry_t* f_cache_buckets[TTF_CACHE_BUCKETS];
    glyph_entry_t* f_lru_head;
    glyph_entry_t* f_lru_tail;
    size_t f_cache_bytes;
    bool f_used;           ///< 槽位已被占用 / the slot is in use
} ttf_slot_t;

static ttf_slot_t s_slots[TTF_FONT_SLOTS];
static ttf_slot_t* s_cur = &s_slots[TTF_FONT_SLOT_SYSTEM];
static int s_cur_index = TTF_FONT_SLOT_SYSTEM;

// 下面这组宏让 2000 多行现有函数体一行不改地作用在“当前槽”上。新增代码请直接写
// `s_cur->f_xxx`，不要用宏名，免得以后再加字段时又撞上。
// These macros keep the existing function bodies acting on the current slot with no
// source change. New code should use `s_cur->f_xxx` directly, not the macro names.
#define font_fd (s_cur->f_fd)
#define font_mem (s_cur->f_mem)
#define font_mem_len (s_cur->f_mem_len)
#define font_mem_owned (s_cur->f_mem_owned)
#define font_file_pos (s_cur->f_file_pos)
#define font_path (s_cur->f_path)
#define packed_root (s_cur->f_packed_root)
#define packed_weight (s_cur->f_packed_weight)
#define file_glyf_off (s_cur->f_file_glyf_off)
#define file_glyf_len (s_cur->f_file_glyf_len)
#define file_loca (s_cur->f_file_loca)
#define file_loca_len (s_cur->f_file_loca_len)
#define loca_long (s_cur->f_loca_long)
#define num_glyphs (s_cur->f_num_glyphs)
#define glyf_ram (s_cur->f_glyf_ram)
#define glyf_ram_off (s_cur->f_glyf_ram_off)
#define glyf_ram_len (s_cur->f_glyf_ram_len)
#define gvar_ram (s_cur->f_gvar_ram)
#define gvar_ram_off (s_cur->f_gvar_ram_off)
#define gvar_ram_len (s_cur->f_gvar_ram_len)
#define io_data (s_cur->f_io_data)
#define io_run_buf (s_cur->f_io_run_buf)
#define io_base (s_cur->f_io_base)
#define io_fill (s_cur->f_io_fill)
#define io_age (s_cur->f_io_age)
#define io_clock (s_cur->f_io_clock)
#define touch_blocks (s_cur->f_touch_blocks)
#define touch_n (s_cur->f_touch_n)
#define font_data (s_cur->f_font_data)
#define work_loca_off (s_cur->f_work_loca_off)
#define work_glyf_off (s_cur->f_work_glyf_off)
#define font_info (s_cur->f_font_info)
#define font_ready (s_cur->f_ready)
#define cff_outline (s_cur->f_cff)
#define font_data_borrowed (s_cur->f_data_borrowed)
#define size_metrics (s_cur->f_size_metrics)
#define raw_ascent_units (s_cur->f_raw_ascent_units)
#define current_weight (s_cur->f_weight)
#define wght_min (s_cur->f_wght_min)
#define wght_def (s_cur->f_wght_def)
#define wght_max (s_cur->f_wght_max)
#define file_gvar_off (s_cur->f_file_gvar_off)
#define file_gvar_len (s_cur->f_file_gvar_len)
#define gvar_data_array_off (s_cur->f_gvar_data_array_off)
#define gvar_glyph_off (s_cur->f_gvar_glyph_off)
#define shared_tuple_f2dot14 (s_cur->f_shared_tuple)
#define shared_tuple_count (s_cur->f_shared_tuple_count)
#define gvar_axis_count (s_cur->f_gvar_axis_count)
#define gvar_ready (s_cur->f_gvar_ready)
#define cache_buckets (s_cur->f_cache_buckets)
#define lru_head (s_cur->f_lru_head)
#define lru_tail (s_cur->f_lru_tail)
#define cache_bytes (s_cur->f_cache_bytes)

// gvar 的暂存缓冲不进槽：一次只光栅化一个字，共享安全，也省下每槽十几 KB。
// The gvar scratch does not move into a slot: one glyph is rasterized at a time, so it
// is safe to share and saves a dozen KB per slot.
static ttf_work_t* work;
static bool bench_on;
static ttf_bench_stats_t bench;
static int64_t bench_start_us;

/* ---- 字节序与文件读 / Endian and file I/O ---- */
static uint16_t be16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static bool io_read_raw(uint32_t offset, void* dst, size_t n) {
    if (font_mem != NULL) {
        if ((uint64_t)offset + n > font_mem_len) return false;
        memcpy(dst, font_mem + offset, n);
        return true;
    }
    if (font_fd < 0) return false;
    int64_t t0 = bench_on ? esp_timer_get_time() : 0;
    bool ok = false;
    do {
        if (font_file_pos != offset) {
            if (lseek(font_fd, (off_t)offset, SEEK_SET) < 0) {
                font_file_pos = UINT32_MAX;
                break;
            }
            font_file_pos = offset;
        }
        uint8_t* out = dst;
        size_t done = 0;
        while (done < n) {
            size_t chunk = n - done;
            if (chunk > 16384) chunk = 16384;
            ssize_t got = read(font_fd, out + done, chunk);
            if (got <= 0) {
                font_file_pos = UINT32_MAX;
                break;
            }
            done += (size_t)got;
            font_file_pos = offset + (uint32_t)done;
        }
        ok = done == n;
    } while (0);
    if (bench_on) bench.read_us += esp_timer_get_time() - t0;
    return ok;
}

static void io_reset(void) {
    for (int i = 0; i < TTF_IO_SLOTS; i++) {
        io_base[i] = UINT32_MAX;
        io_fill[i] = 0;
        io_age[i] = 0;
    }
    io_clock = 0;
    touch_n = 0;
}

static void io_unmap(void) {
    heap_caps_free(glyf_ram);
    glyf_ram = NULL;
    glyf_ram_off = 0;
    glyf_ram_len = 0;
    heap_caps_free(gvar_ram);
    gvar_ram = NULL;
    gvar_ram_off = 0;
    gvar_ram_len = 0;
}

static bool io_ensure(void) {
    if (io_data == NULL) {
        io_data = heap_caps_malloc(
            (size_t)TTF_IO_SLOTS * TTF_IO_BLOCK,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (io_data == NULL) return false;
        io_reset();
    }
    if (io_run_buf == NULL) {
        io_run_buf = heap_caps_malloc(
            (size_t)TTF_IO_RUN_MAX * TTF_IO_BLOCK,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
    }
    return true;
}

static bool io_from_map(uint32_t off, size_t n, void* dst) {
    if (glyf_ram != NULL && off >= glyf_ram_off
        && off + n <= glyf_ram_off + glyf_ram_len) {
        memcpy(dst, glyf_ram + (off - glyf_ram_off), n);
        return true;
    }
    if (gvar_ram != NULL && off >= gvar_ram_off
        && off + n <= gvar_ram_off + gvar_ram_len) {
        memcpy(dst, gvar_ram + (off - gvar_ram_off), n);
        return true;
    }
    return false;
}

static bool io_mapped_covers(uint32_t off, uint32_t len) {
    if (len == 0) return true;
    if (glyf_ram != NULL && off >= glyf_ram_off
        && off + len <= glyf_ram_off + glyf_ram_len) {
        return true;
    }
    if (gvar_ram != NULL && off >= gvar_ram_off
        && off + len <= gvar_ram_off + gvar_ram_len) {
        return true;
    }
    return false;
}

static int io_find(uint32_t base) {
    for (int i = 0; i < TTF_IO_SLOTS; i++) {
        if (io_base[i] == base) {
            io_age[i] = ++io_clock;
            return i;
        }
    }
    return -1;
}

static int io_victim(void) {
    int empty = -1;
    int best = 0;
    for (int i = 0; i < TTF_IO_SLOTS; i++) {
        if (io_base[i] == UINT32_MAX) {
            empty = i;
            break;
        }
        if (io_age[i] < io_age[best]) best = i;
    }
    return empty >= 0 ? empty : best;
}

static bool io_load_block(uint32_t base) {
    if (io_find(base) >= 0) return true;
    if (io_data == NULL && !io_ensure()) return false;
    int slot = io_victim();
    uint8_t* dst = io_data + (size_t)slot * TTF_IO_BLOCK;
    int64_t t0 = bench_on ? esp_timer_get_time() : 0;
    if (lseek(font_fd, (off_t)base, SEEK_SET) < 0) {
        font_file_pos = UINT32_MAX;
        if (bench_on) bench.read_us += esp_timer_get_time() - t0;
        return false;
    }
    ssize_t got = read(font_fd, dst, TTF_IO_BLOCK);
    if (bench_on) bench.read_us += esp_timer_get_time() - t0;
    if (got <= 0) {
        font_file_pos = UINT32_MAX;
        return false;
    }
    font_file_pos = base + (uint32_t)got;
    io_base[slot] = base;
    io_fill[slot] = (uint16_t)got;
    io_age[slot] = ++io_clock;
    return true;
}

static bool file_read_at(uint32_t offset, void* dst, size_t n) {
    if (n == 0) return true;
    if (font_mem != NULL) {
        if (io_from_map(offset, n, dst)) return true;
        return io_read_raw(offset, dst, n);
    }
    if (font_fd < 0) return false;
    if (io_from_map(offset, n, dst)) return true;
    if (io_data == NULL && !io_ensure()) return io_read_raw(offset, dst, n);

    uint8_t* out = dst;
    uint32_t pos = offset;
    size_t left = n;
    while (left > 0) {
        uint32_t base = pos & ~(uint32_t)(TTF_IO_BLOCK - 1);
        uint32_t skip = pos - base;
        if (!io_load_block(base)) return false;
        int slot = io_find(base);
        if (slot < 0 || skip >= io_fill[slot]) return false;
        size_t take = (size_t)io_fill[slot] - skip;
        if (take > left) take = left;
        memcpy(out, io_data + (size_t)slot * TTF_IO_BLOCK + skip, take);
        out += take;
        pos += (uint32_t)take;
        left -= take;
    }
    return true;
}

static void io_touch(uint32_t off, uint32_t len) {
    if (len == 0 || io_mapped_covers(off, len)) return;
    uint32_t a = off & ~(uint32_t)(TTF_IO_BLOCK - 1);
    uint32_t end = off + len;
    while (a < end && touch_n < TTF_PREFETCH_MAX) {
        touch_blocks[touch_n++] = a;
        a += TTF_IO_BLOCK;
    }
}

static int u32_cmp(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a;
    uint32_t y = *(const uint32_t*)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static bool io_load_run(uint32_t base, int blocks) {
    if (blocks <= 1) return io_load_block(base);
    if (blocks > TTF_IO_RUN_MAX) blocks = TTF_IO_RUN_MAX;
    if (io_run_buf == NULL
        || !io_read_raw(base, io_run_buf, (size_t)blocks * TTF_IO_BLOCK)) {
        bool ok = true;
        for (int i = 0; i < blocks; i++) {
            if (!io_load_block(base + (uint32_t)i * TTF_IO_BLOCK)) ok = false;
        }
        return ok;
    }
    for (int i = 0; i < blocks; i++) {
        uint32_t b = base + (uint32_t)i * TTF_IO_BLOCK;
        if (io_find(b) >= 0) continue;
        int slot = io_victim();
        memcpy(
            io_data + (size_t)slot * TTF_IO_BLOCK,
            io_run_buf + (size_t)i * TTF_IO_BLOCK,
            TTF_IO_BLOCK
        );
        io_base[slot] = b;
        io_fill[slot] = TTF_IO_BLOCK;
        io_age[slot] = ++io_clock;
    }
    return true;
}

static void io_flush_touches(void) {
    if (touch_n <= 0) return;
    if (io_data == NULL && !io_ensure()) {
        touch_n = 0;
        return;
    }
    qsort(touch_blocks, (size_t)touch_n, sizeof(touch_blocks[0]), u32_cmp);
    int w = 1;
    for (int i = 1; i < touch_n; i++) {
        if (touch_blocks[i] != touch_blocks[w - 1]) {
            touch_blocks[w++] = touch_blocks[i];
        }
    }
    int i = 0;
    while (i < w) {
        int j = i + 1;
        while (j < w && j - i < TTF_IO_RUN_MAX
               && touch_blocks[j] == touch_blocks[j - 1] + TTF_IO_BLOCK) {
            j++;
        }
        io_load_run(touch_blocks[i], j - i);
        i = j;
    }
    touch_n = 0;
}

static bool try_map_table(
    uint32_t off, uint32_t len, uint8_t** ram, uint32_t* ram_off,
    uint32_t* ram_len, const char* name
) {
    if (*ram != NULL || len < 4096 || len > TTF_MAP_MAX) return false;
    size_t largest = heap_caps_get_largest_free_block(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (largest < (size_t)len + TTF_MAP_RESERVE) {
        ESP_LOGI(
            TAG, "%s %u KB, skip map (largest %u KB)",
            name, (unsigned)(len / 1024), (unsigned)(largest / 1024)
        );
        return false;
    }
    uint8_t* p = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) return false;
    int64_t t0 = esp_timer_get_time();
    if (!io_read_raw(off, p, len)) {
        heap_caps_free(p);
        return false;
    }
    *ram = p;
    *ram_off = off;
    *ram_len = len;
    ESP_LOGI(
        TAG, "mapped %s %u KB in %dms",
        name, (unsigned)(len / 1024),
        (int)((esp_timer_get_time() - t0) / 1000)
    );
    return true;
}

static const char* font_basename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

// Finder 拷到 FAT 会留下 ._xxx.ttf（AppleDouble）和 .DS_Store，不是真字体。
static bool is_junk_name(const char* name) {
    return name[0] == '.' || strncmp(name, "._", 2) == 0;
}

static bool is_ttf_name(const char* name) {
    if (is_junk_name(name)) return false;
    size_t len = strlen(name);
    return len >= 4 && strcasecmp(name + len - 4, ".ttf") == 0;
}

static void font_stem(const char* path, char* out, size_t n) {
    const char* slash = strrchr(path, '/');
    const char* name = slash != NULL ? slash + 1 : path;
    strlcpy(out, name, n);
    size_t len = strlen(out);
    if (len >= 4 && strcasecmp(out + len - 4, ".ttf") == 0) {
        out[len - 4] = '\0';
    }
}

static int font_item_cmp(const void* a, const void* b) {
    return strcasecmp(
        ((const ttf_font_item_t*)a)->name, ((const ttf_font_item_t*)b)->name
    );
}

static bool catalog_has_name(
    const ttf_font_item_t* items, int n, const char* name
) {
    for (int i = 0; i < n; i++) {
        if (strcasecmp(items[i].name, name) == 0) return true;
    }
    return false;
}

// 字体目录放 PSRAM，理由同文件列表。
// The font catalogue lives in PSRAM for the same reason as the file list.
static ttf_font_item_t *s_catalog;

static bool catalog_alloc(void) {
    if (!s_catalog)
        s_catalog = heap_caps_calloc(TTF_FONT_MAX, sizeof(ttf_font_item_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_catalog != NULL;
}
static int s_catalog_n;

int ttf_font_scan(void) {
    if (!catalog_alloc()) return 0;
    s_catalog_n = 0;
    for (size_t d = 0; d < sizeof(k_font_dirs) / sizeof(k_font_dirs[0]); d++) {
        const char* font_dir = k_font_dirs[d] ? k_font_dirs[d] : app_settings_fonts_dir();
        DIR* dir = opendir(font_dir);
        if (dir == NULL) continue;
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL && s_catalog_n < TTF_FONT_MAX) {
            if (ent->d_name[0] == '.') continue; // Ignore macOS ._ font sidecars.
            if (!is_ttf_name(ent->d_name)) continue;
            char name[TTF_FONT_NAME_MAX];
            font_stem(ent->d_name, name, sizeof(name));
            if (name[0] == '\0' || catalog_has_name(s_catalog, s_catalog_n, name)) {
                continue;
            }
            snprintf(
                s_catalog[s_catalog_n].path, sizeof(s_catalog[s_catalog_n].path),
                "%s/%s", font_dir, ent->d_name
            );
            strlcpy(s_catalog[s_catalog_n].name, name, sizeof(s_catalog[0].name));
            s_catalog_n++;
        }
        closedir(dir);
    }
    if (s_catalog_n > 1) {
        qsort(s_catalog, (size_t)s_catalog_n, sizeof(s_catalog[0]), font_item_cmp);
    }
    return s_catalog_n;
}

int ttf_font_count(void) {
    return s_catalog_n;
}

const ttf_font_item_t* ttf_font_item(int index) {
    if (index < 0 || index >= s_catalog_n) return NULL;
    return &s_catalog[index];
}

static int try_open_path(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        strlcpy(font_path, path, sizeof(font_path));
        ESP_LOGI(TAG, "using %s", path);
    }
    return fd;
}

static int open_font_file(const char* preferred) {
    if (preferred == NULL || ttf_font_path_is_builtin(preferred)
        || is_junk_name(font_basename(preferred))) {
        return -1;
    }
    return try_open_path(preferred);
}

const char* ttf_font_path(void) {
    // 系统字体的路径永远取自 0 号槽：内嵌字体可能占着游标，设置页问的仍是系统字体。
    // The system path always comes from slot 0: an embedded face may own the cursor,
    // but the settings page is still asking about the system face.
    return s_slots[TTF_FONT_SLOT_SYSTEM].f_path;
}

bool ttf_font_path_is_builtin(const char* path) {
    return path == NULL || path[0] == '\0' || strcmp(path, TTF_FONT_BUILTIN) == 0;
}

bool ttf_font_is_builtin(void) {
    const ttf_slot_t* sys = &s_slots[TTF_FONT_SLOT_SYSTEM];
    return sys->f_ready && sys->f_mem != NULL;
}

const char* ttf_font_display_name(void) {
    static char name[TTF_FONT_NAME_MAX];
    const char* path = s_slots[TTF_FONT_SLOT_SYSTEM].f_path;
    if (ttf_font_is_builtin() || ttf_font_path_is_builtin(path)) return "思源黑体（内建）";
    if (path[0] == '\0') return "";
    font_stem(path, name, sizeof(name));
    return ttf_font_localized_name(name);
}

const char* ttf_font_localized_name(const char* stem) {
    if (!stem) return "";
    if (!strcasecmp(stem, "Song")) return "思源宋体";
    if (!strcasecmp(stem, "Hei")) return "思源黑体";
    if (!strcasecmp(stem, "Kai")) return "思源楷体";
    if (!strcasecmp(stem, "FangSong")) return "思源仿宋";
    if (!strcasecmp(stem, "WenKai")) return "文楷";
    if (!strcasecmp(stem, "KingHwa")) return "京华老宋";
    if (!strcasecmp(stem, "ChillKai")) return "寒蝉正楷";
    if (!strcasecmp(stem, "CangErYunHei05")) return "仓耳云黑05";
    return stem;
}

static bool find_sfnt_table(
    const uint8_t* header, size_t header_len, const char* tag,
    uint32_t* offset, uint32_t* length
) {
    if (header_len < 12) return false;
    uint16_t count = be16(header + 4);
    for (uint16_t i = 0; i < count; i++) {
        size_t rec = 12 + (size_t)i * 16;
        if (rec + 16 > header_len) return false;
        if (memcmp(header + rec, tag, 4) == 0) {
            *offset = be32(header + rec + 8);
            *length = be32(header + rec + 12);
            return true;
        }
    }
    return false;
}

static uint32_t file_glyph_off(int gid) {
    if (gid < 0 || gid > num_glyphs) return 0;
    if (loca_long) return be32(file_loca + (size_t)gid * 4);
    return (uint32_t)be16(file_loca + (size_t)gid * 2) * 2u;
}

static uint32_t file_glyph_len(int gid) {
    if (gid < 0 || gid >= num_glyphs) return 0;
    uint32_t a = file_glyph_off(gid);
    uint32_t b = file_glyph_off(gid + 1);
    return b > a ? b - a : 0;
}

static void set_work_loca(int gid, uint32_t off) {
    uint8_t* loca = font_data + work_loca_off;
    if (loca_long) {
        put_be32(loca + (size_t)gid * 4, off);
    } else {
        put_be16(loca + (size_t)gid * 2, (uint16_t)(off / 2));
    }
}

static bool enqueue_composite_children(
    const uint8_t* blob, uint32_t blob_len, int* tree, int* tree_n
) {
    if (blob_len < 10) return true;
    if ((int16_t)be16(blob) >= 0) return true;

    size_t p = 10;
    while (p + 4 <= blob_len) {
        uint16_t flags = be16(blob + p);
        int gidx = (int)be16(blob + p + 2);
        p += 4;
        if (flags & 1) {
            if (p + 4 > blob_len) break;
            p += 4;
        } else {
            if (p + 2 > blob_len) break;
            p += 2;
        }
        if (flags & (1 << 3)) p += 2;
        else if (flags & (1 << 6)) p += 4;
        else if (flags & (1 << 7)) p += 8;
        if (gidx >= 0 && gidx < num_glyphs) {
            bool seen = false;
            for (int i = 0; i < *tree_n; i++) {
                if (tree[i] == gidx) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                if (*tree_n >= TTF_TREE_MAX) return false;
                tree[(*tree_n)++] = gidx;
            }
        }
        if ((flags & (1 << 5)) == 0) break;
    }
    return true;
}

/* ---- gvar 变体 / gvar variation ---- */
static int clamp_weight(int wght) {
    if (wght < wght_min) return wght_min;
    if (wght > wght_max) return wght_max;
    return wght;
}

static float f2dot14(int16_t v) {
    return (float)v / 16384.0f;
}

static float norm_wght(void) {
    int w = current_weight;
    if (w == wght_def) return 0.0f;
    if (w < wght_def) {
        if (wght_def == wght_min) return 0.0f;
        return (float)(w - wght_def) / (float)(wght_def - wght_min);
    }
    if (wght_max == wght_def) return 0.0f;
    return (float)(w - wght_def) / (float)(wght_max - wght_def);
}

static float tuple_scalar(float peak, float start, float end, bool intermediate) {
    float n = norm_wght();
    if (intermediate) {
        if (n < start || n > end) return 0.0f;
        if (n == peak) return 1.0f;
        if (n < peak) {
            if (peak == start) return 1.0f;
            return (n - start) / (peak - start);
        }
        if (peak == end) return 1.0f;
        return (end - n) / (end - peak);
    }
    if (peak == 0.0f) return 1.0f;
    if (n == peak) return 1.0f;
    if (peak > 0.0f && (n < 0.0f || n > peak)) return 0.0f;
    if (peak < 0.0f && (n > 0.0f || n < peak)) return 0.0f;
    return n / peak;
}

static bool read_packed_points(
    const uint8_t** pp, const uint8_t* end, uint16_t* pts, int* n_out
) {
    if (*pp >= end) return false;
    uint8_t b = *(*pp)++;
    if (b == 0) {
        *n_out = -1;
        return true;
    }
    int count = b;
    if (b & 0x80) {
        if (*pp >= end) return false;
        count = ((b & 0x7F) << 8) | *(*pp)++;
    }
    if (count > TTF_MAX_VAR_PTS) return false;
    int n = 0;
    uint16_t last = 0;
    while (n < count) {
        if (*pp >= end) return false;
        uint8_t ctrl = *(*pp)++;
        int words = ctrl & 0x80;
        int run = (ctrl & 0x7F) + 1;
        for (int i = 0; i < run && n < count; i++) {
            uint16_t delta;
            if (words) {
                if (*pp + 2 > end) return false;
                delta = (uint16_t)(((*pp)[0] << 8) | (*pp)[1]);
                *pp += 2;
            } else {
                if (*pp >= end) return false;
                delta = *(*pp)++;
            }
            last = (uint16_t)(last + delta);
            pts[n++] = last;
        }
    }
    *n_out = n;
    return true;
}

static bool read_packed_deltas(
    const uint8_t** pp, const uint8_t* end, int count, int16_t* out
) {
    int n = 0;
    while (n < count) {
        if (*pp >= end) return false;
        uint8_t ctrl = *(*pp)++;
        int run = (ctrl & 0x3F) + 1;
        if (ctrl & 0x80) {
            for (int i = 0; i < run && n < count; i++) out[n++] = 0;
        } else if (ctrl & 0x40) {
            for (int i = 0; i < run && n < count; i++) {
                if (*pp + 2 > end) return false;
                out[n++] = (int16_t)(((*pp)[0] << 8) | (*pp)[1]);
                *pp += 2;
            }
        } else {
            for (int i = 0; i < run && n < count; i++) {
                if (*pp >= end) return false;
                out[n++] = (int8_t)(*(*pp)++);
            }
        }
    }
    return true;
}

// TrueType IUP：按原坐标在两个已赋值点之间插值未赋值点。
static void iup_contour(float* delta, const int16_t* org, int begin, int end) {
    int n = end - begin + 1;
    if (n <= 0) return;
    int first = -1;
    for (int i = 0; i < n; i++) {
        if (!isnan(delta[begin + i])) {
            first = i;
            break;
        }
    }
    if (first < 0) {
        for (int i = 0; i < n; i++) delta[begin + i] = 0.0f;
        return;
    }
    int touched = 0;
    for (int i = 0; i < n; i++) {
        if (!isnan(delta[begin + i])) touched++;
    }
    if (touched == 1) {
        float v = delta[begin + first];
        for (int i = 0; i < n; i++) delta[begin + i] = v;
        return;
    }

    int prev = first;
    for (int step = 1; step <= n; step++) {
        int i = (first + step) % n;
        if (isnan(delta[begin + i]) && step < n) continue;
        int next = i;
        if (step == n) next = first;
        int ia = begin + prev;
        int ib = begin + next;
        int16_t ca = org[ia];
        int16_t cb = org[ib];
        float da = delta[ia];
        float db = delta[ib];
        int j = (prev + 1) % n;
        while (j != next) {
            int ij = begin + j;
            int16_t c = org[ij];
            if (ca == cb) {
                delta[ij] = da;
            } else if ((c <= ca && c <= cb) || (c >= ca && c >= cb)) {
                int16_t da_abs = c >= ca ? (int16_t)(c - ca) : (int16_t)(ca - c);
                int16_t db_abs = c >= cb ? (int16_t)(c - cb) : (int16_t)(cb - c);
                delta[ij] = da_abs <= db_abs ? da : db;
            } else {
                delta[ij] = da + (db - da) * (float)(c - ca) / (float)(cb - ca);
            }
            j = (j + 1) % n;
        }
        prev = next;
        if (step == n) break;
    }
}

static void iup_axis(
    float* delta, const int16_t* org, int n_pts, const int16_t* endpts, int n_contours
) {
    int start = 0;
    for (int c = 0; c < n_contours; c++) {
        int last = endpts[c];
        if (last >= n_pts) last = n_pts - 1;
        if (last >= start) iup_contour(delta, org, start, last);
        start = last + 1;
    }
    for (int i = 0; i < n_pts; i++) {
        if (isnan(delta[i])) delta[i] = 0.0f;
    }
}

static bool decode_simple_xy(
    const uint8_t* blob, uint32_t len,
    int16_t* x, int16_t* y, uint8_t* on, int16_t* endpts,
    int* n_pts, int* n_contours, const uint8_t** ins, uint16_t* ins_len
) {
    if (len < 10) return false;
    int16_t contours = (int16_t)be16(blob);
    if (contours <= 0 || contours > 64) return false;
    if ((uint32_t)(10 + contours * 2 + 2) > len) return false;
    for (int i = 0; i < contours; i++) endpts[i] = (int16_t)be16(blob + 10 + i * 2);
    int n = endpts[contours - 1] + 1;
    if (n <= 0 || n > TTF_MAX_PTS) return false;
    uint16_t ilen = be16(blob + 10 + contours * 2);
    size_t p = (size_t)(12 + contours * 2 + ilen);
    if (p > len) return false;
    *ins = blob + 12 + contours * 2;
    *ins_len = ilen;

    uint8_t* flags = work->flags;
    int i = 0;
    while (i < n) {
        if (p >= len) return false;
        uint8_t f = blob[p++];
        flags[i] = f;
        i++;
        if (f & 0x08) {
            if (p >= len) return false;
            int rep = blob[p++];
            while (rep-- > 0 && i < n) flags[i++] = f;
        }
    }
    int16_t acc = 0;
    for (i = 0; i < n; i++) {
        uint8_t f = flags[i];
        if (f & 0x02) {
            if (p >= len) return false;
            int16_t d = blob[p++];
            acc = (int16_t)(acc + ((f & 0x10) ? d : -d));
        } else if (!(f & 0x10)) {
            if (p + 2 > len) return false;
            acc = (int16_t)(acc + (int16_t)be16(blob + p));
            p += 2;
        }
        x[i] = acc;
        on[i] = (uint8_t)(f & 0x01);
    }
    acc = 0;
    for (i = 0; i < n; i++) {
        uint8_t f = flags[i];
        if (f & 0x04) {
            if (p >= len) return false;
            int16_t d = blob[p++];
            acc = (int16_t)(acc + ((f & 0x20) ? d : -d));
        } else if (!(f & 0x20)) {
            if (p + 2 > len) return false;
            acc = (int16_t)(acc + (int16_t)be16(blob + p));
            p += 2;
        }
        y[i] = acc;
    }
    *n_pts = n;
    *n_contours = contours;
    return true;
}

static uint32_t encode_simple_xy(
    uint8_t* out, uint32_t cap,
    const int16_t* x, const int16_t* y, const uint8_t* on,
    const int16_t* endpts, int n_pts, int n_contours,
    const uint8_t* ins, uint16_t ins_len
) {
    uint32_t need = (uint32_t)(10 + n_contours * 2 + 2 + ins_len + n_pts * 5);
    if (need > cap) return 0;
    put_be16(out, (uint16_t)n_contours);
    int16_t xmin = x[0], xmax = x[0], ymin = y[0], ymax = y[0];
    for (int i = 1; i < n_pts; i++) {
        if (x[i] < xmin) xmin = x[i];
        if (x[i] > xmax) xmax = x[i];
        if (y[i] < ymin) ymin = y[i];
        if (y[i] > ymax) ymax = y[i];
    }
    put_be16(out + 2, (uint16_t)xmin);
    put_be16(out + 4, (uint16_t)ymin);
    put_be16(out + 6, (uint16_t)xmax);
    put_be16(out + 8, (uint16_t)ymax);
    for (int i = 0; i < n_contours; i++) {
        put_be16(out + 10 + i * 2, (uint16_t)endpts[i]);
    }
    put_be16(out + 10 + n_contours * 2, ins_len);
    size_t p = (size_t)(12 + n_contours * 2);
    if (ins_len > 0) memcpy(out + p, ins, ins_len);
    p += ins_len;
    for (int i = 0; i < n_pts; i++) out[p++] = (uint8_t)(on[i] ? 0x01 : 0x00);
    int16_t prev = 0;
    for (int i = 0; i < n_pts; i++) {
        int16_t d = (int16_t)(x[i] - prev);
        if (d == 0) {
            out[12 + n_contours * 2 + ins_len + i] |= 0x10;
        } else {
            put_be16(out + p, (uint16_t)d);
            p += 2;
        }
        prev = x[i];
    }
    prev = 0;
    for (int i = 0; i < n_pts; i++) {
        int16_t d = (int16_t)(y[i] - prev);
        if (d == 0) {
            out[12 + n_contours * 2 + ins_len + i] |= 0x20;
        } else {
            put_be16(out + p, (uint16_t)d);
            p += 2;
        }
        prev = y[i];
    }
    return (uint32_t)p;
}

static bool apply_gvar_deltas(
    float* dx, float* dy, const int16_t* ox, const int16_t* oy,
    int n_outline, int n_var, const int16_t* endpts, int n_contours,
    const uint8_t* data, uint32_t data_len
) {
    if (data_len < 4) return false;
    uint16_t tvc = be16(data);
    int tuple_n = tvc & 0x0FFF;
    bool share_pts = (tvc & 0x8000) != 0;
    uint16_t data_off = be16(data + 2);
    if (data_off > data_len) return false;

    const uint8_t* hp = data + 4;
    const uint8_t* end = data + data_len;
    const uint8_t* sp = data + data_off;
    uint16_t* shared_pts = work->shared_pts;
    int shared_n = -2;
    if (share_pts) {
        if (!read_packed_points(&sp, end, shared_pts, &shared_n)) return false;
    }

    int16_t* xdel = work->xdel;
    int16_t* ydel = work->ydel;
    uint16_t* pts = work->pts;

    for (int t = 0; t < tuple_n; t++) {
        if (hp + 4 > end) return false;
        uint16_t ti = be16(hp + 2);
        hp += 4;
        float peak = 0.0f, start = 0.0f, endc = 0.0f;
        bool intermediate = (ti & 0x4000) != 0;
        if (ti & 0x8000) {
            if (hp + 2 > end) return false;
            peak = f2dot14((int16_t)be16(hp));
            hp += 2;
        } else {
            int idx = ti & 0x0FFF;
            if (idx >= shared_tuple_count) return false;
            peak = f2dot14(shared_tuple_f2dot14[idx]);
        }
        if (intermediate) {
            if (hp + 4 > end) return false;
            start = f2dot14((int16_t)be16(hp));
            endc = f2dot14((int16_t)be16(hp + 2));
            hp += 4;
        }
        float scalar = tuple_scalar(peak, start, endc, intermediate);
        int pn = -1;
        if (ti & 0x2000) {
            if (!read_packed_points(&sp, end, pts, &pn)) return false;
        } else if (share_pts) {
            pn = shared_n;
            if (pn > 0) memcpy(pts, shared_pts, (size_t)pn * sizeof(pts[0]));
        }
        int delta_n = (pn < 0) ? n_var : pn;
        if (delta_n > TTF_MAX_VAR_PTS) return false;
        if (!read_packed_deltas(&sp, end, delta_n, xdel)) return false;
        if (!read_packed_deltas(&sp, end, delta_n, ydel)) return false;
        if (scalar == 0.0f) continue;

        float* tdx = work->tdx;
        float* tdy = work->tdy;
        uint8_t* setx = work->setx;
        uint8_t* sety = work->sety;
        memset(setx, 0, (size_t)n_outline);
        memset(sety, 0, (size_t)n_outline);
        memset(tdx, 0, sizeof(work->tdx));
        memset(tdy, 0, sizeof(work->tdy));
        if (pn < 0) {
            for (int i = 0; i < n_outline && i < delta_n; i++) {
                tdx[i] = (float)xdel[i] * scalar;
                tdy[i] = (float)ydel[i] * scalar;
                setx[i] = 1;
                sety[i] = 1;
            }
        } else {
            for (int i = 0; i < pn; i++) {
                int pi = pts[i];
                if (pi < n_outline) {
                    tdx[pi] = (float)xdel[i] * scalar;
                    tdy[pi] = (float)ydel[i] * scalar;
                    setx[pi] = 1;
                    sety[pi] = 1;
                }
            }
            for (int i = 0; i < n_outline; i++) {
                if (!setx[i]) tdx[i] = NAN;
                if (!sety[i]) tdy[i] = NAN;
            }
            iup_axis(tdx, ox, n_outline, endpts, n_contours);
            iup_axis(tdy, oy, n_outline, endpts, n_contours);
        }
        for (int i = 0; i < n_outline; i++) {
            dx[i] += tdx[i];
            dy[i] += tdy[i];
        }
        (void)n_var;
    }
    return true;
}

static bool apply_gvar_simple(uint8_t* blob, uint32_t* len, uint32_t cap, int gid) {
    uint32_t a = gvar_glyph_off[gid];
    uint32_t b = gvar_glyph_off[gid + 1];
    if (b <= a || b - a > TTF_GVAR_SLICE_MAX) return true;
    uint8_t* slice = work->slice;
    if (!file_read_at(file_gvar_off + gvar_data_array_off + a, slice, b - a)) {
        return false;
    }

    int16_t* x = work->x;
    int16_t* y = work->y;
    int16_t* endpts = work->endpts;
    uint8_t* on = work->on;
    int n_pts = 0, n_contours = 0;
    const uint8_t* ins = NULL;
    uint16_t ins_len = 0;
    if (!decode_simple_xy(blob, *len, x, y, on, endpts, &n_pts, &n_contours, &ins, &ins_len)) {
        return true;
    }
    int n_var = n_pts + 4;
    float* dx = work->dx;
    float* dy = work->dy;
    memset(dx, 0, sizeof(work->dx));
    memset(dy, 0, sizeof(work->dy));
    if (!apply_gvar_deltas(
            dx, dy, x, y, n_pts, n_var, endpts, n_contours, slice, b - a
        )) {
        return true;
    }
    for (int i = 0; i < n_pts; i++) {
        x[i] = (int16_t)lroundf((float)x[i] + dx[i]);
        y[i] = (int16_t)lroundf((float)y[i] + dy[i]);
    }
    uint32_t encoded = encode_simple_xy(
        blob, cap, x, y, on, endpts, n_pts, n_contours, ins, ins_len
    );
    if (encoded == 0) return false;
    *len = encoded;
    return true;
}

static bool apply_gvar_composite(uint8_t* blob, uint32_t len, int gid) {
    uint32_t a = gvar_glyph_off[gid];
    uint32_t b = gvar_glyph_off[gid + 1];
    if (b <= a || b - a > TTF_GVAR_SLICE_MAX) return true;
    uint8_t* slice = work->slice;
    if (!file_read_at(file_gvar_off + gvar_data_array_off + a, slice, b - a)) {
        return false;
    }

    typedef struct {
        size_t arg_off;
        bool words;
        int16_t x;
        int16_t y;
    } comp_t;
    comp_t comps[16];
    int ncomp = 0;
    size_t p = 10;
    while (p + 4 <= len && ncomp < 16) {
        uint16_t flags = be16(blob + p);
        p += 4;
        comps[ncomp].arg_off = p;
        comps[ncomp].words = (flags & 1) != 0;
        if (flags & 1) {
            if (p + 4 > len) break;
            comps[ncomp].x = (int16_t)be16(blob + p);
            comps[ncomp].y = (int16_t)be16(blob + p + 2);
            p += 4;
        } else {
            if (p + 2 > len) break;
            comps[ncomp].x = (int8_t)blob[p];
            comps[ncomp].y = (int8_t)blob[p + 1];
            p += 2;
        }
        if (flags & (1 << 3)) p += 2;
        else if (flags & (1 << 6)) p += 4;
        else if (flags & (1 << 7)) p += 8;
        ncomp++;
        if ((flags & (1 << 5)) == 0) break;
    }
    if (ncomp == 0) return true;

    // 复合 gvar 点序：4 个幻影点 + 每分量 4 点，前两个是原点 x/y。
    int n_var = 4 + 4 * ncomp;
    const uint8_t* data = slice;
    uint32_t data_len = b - a;
    if (data_len < 4) return true;
    uint16_t tvc = be16(data);
    int tuple_n = tvc & 0x0FFF;
    bool share_pts = (tvc & 0x8000) != 0;
    uint16_t data_off = be16(data + 2);
    if (data_off > data_len) return true;
    const uint8_t* hp = data + 4;
    const uint8_t* end = data + data_len;
    const uint8_t* sp = data + data_off;
    uint16_t* shared_pts = work->shared_pts;
    int shared_n = -2;
    if (share_pts && !read_packed_points(&sp, end, shared_pts, &shared_n)) return true;

    int16_t* xdel = work->xdel;
    int16_t* ydel = work->ydel;
    uint16_t* pts = work->pts;
    float cdx[16] = {0};
    float cdy[16] = {0};

    for (int t = 0; t < tuple_n; t++) {
        if (hp + 4 > end) break;
        uint16_t ti = be16(hp + 2);
        hp += 4;
        float peak = 0.0f, start = 0.0f, endc = 0.0f;
        bool intermediate = (ti & 0x4000) != 0;
        if (ti & 0x8000) {
            if (hp + 2 > end) break;
            peak = f2dot14((int16_t)be16(hp));
            hp += 2;
        } else {
            int idx = ti & 0x0FFF;
            if (idx >= shared_tuple_count) break;
            peak = f2dot14(shared_tuple_f2dot14[idx]);
        }
        if (intermediate) {
            if (hp + 4 > end) break;
            start = f2dot14((int16_t)be16(hp));
            endc = f2dot14((int16_t)be16(hp + 2));
            hp += 4;
        }
        float scalar = tuple_scalar(peak, start, endc, intermediate);
        int pn = -1;
        if (ti & 0x2000) {
            if (!read_packed_points(&sp, end, pts, &pn)) break;
        } else if (share_pts) {
            pn = shared_n;
            if (pn > 0) memcpy(pts, shared_pts, (size_t)pn * sizeof(pts[0]));
        }
        int delta_n = (pn < 0) ? n_var : pn;
        if (delta_n > TTF_MAX_VAR_PTS) break;
        if (!read_packed_deltas(&sp, end, delta_n, xdel)) break;
        if (!read_packed_deltas(&sp, end, delta_n, ydel)) break;
        if (scalar == 0.0f) continue;

        if (pn < 0) {
            for (int i = 0; i < ncomp; i++) {
                int pi = 4 + 4 * i;
                if (pi < delta_n) cdx[i] += (float)xdel[pi] * scalar;
                if (pi + 1 < delta_n) cdy[i] += (float)ydel[pi] * scalar;
            }
        } else {
            for (int i = 0; i < pn; i++) {
                int pi = pts[i];
                if (pi < 4) continue;
                int ci = (pi - 4) / 4;
                int which = (pi - 4) % 4;
                if (ci < 0 || ci >= ncomp) continue;
                if (which == 0) cdx[ci] += (float)xdel[i] * scalar;
                if (which == 1) cdy[ci] += (float)ydel[i] * scalar;
            }
        }
    }

    for (int i = 0; i < ncomp; i++) {
        int16_t nx = (int16_t)lroundf((float)comps[i].x + cdx[i]);
        int16_t ny = (int16_t)lroundf((float)comps[i].y + cdy[i]);
        uint8_t* arg = blob + comps[i].arg_off;
        if (comps[i].words) {
            put_be16(arg, (uint16_t)nx);
            put_be16(arg + 2, (uint16_t)ny);
        } else {
            if (nx < -128) nx = -128;
            if (nx > 127) nx = 127;
            if (ny < -128) ny = -128;
            if (ny > 127) ny = 127;
            arg[0] = (uint8_t)nx;
            arg[1] = (uint8_t)ny;
        }
    }
    return true;
}

static bool ensure_work(void) {
    if (work != NULL) return true;
    work = heap_caps_calloc(1, sizeof(ttf_work_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return work != NULL;
}

static bool apply_gvar_to_blob(uint8_t* blob, uint32_t* len, uint32_t cap, int gid) {
    if (!gvar_ready || current_weight == wght_def || gid < 0 || gid >= num_glyphs) {
        return true;
    }
    if (!ensure_work()) return false;
    if (*len < 2) return true;
    int16_t contours = (int16_t)be16(blob);
    if (contours > 0) return apply_gvar_simple(blob, len, cap, gid);
    if (contours < 0) return apply_gvar_composite(blob, *len, gid);
    return true;
}

static void reset_variation(void) {
    gvar_ready = false;
    heap_caps_free(gvar_glyph_off);
    gvar_glyph_off = NULL;
    file_gvar_off = 0;
    file_gvar_len = 0;
    gvar_data_array_off = 0;
    shared_tuple_count = 0;
    gvar_axis_count = 0;
    wght_min = TTF_WGHT_MIN;
    wght_def = TTF_WGHT_DEF;
    wght_max = TTF_WGHT_MAX;
    current_weight = TTF_WGHT_DEF;
}

static bool load_variation(const uint8_t* header, size_t header_len) {
    reset_variation();
    uint32_t fvar_off = 0, fvar_len = 0;
    uint32_t gvar_off = 0, gvar_len = 0;
    if (!find_sfnt_table(header, header_len, "fvar", &fvar_off, &fvar_len)
        || !find_sfnt_table(header, header_len, "gvar", &gvar_off, &gvar_len)) {
        ESP_LOGW(TAG, "no fvar/gvar, weight locked to default");
        return false;
    }
    file_gvar_len = gvar_len;
    uint8_t fvar[32];
    if (!file_read_at(fvar_off, fvar, sizeof(fvar))) return false;
    uint16_t axis_off = be16(fvar + 4);
    uint16_t axis_n = be16(fvar + 8);
    uint16_t axis_sz = be16(fvar + 10);
    if (axis_n < 1 || axis_sz < 20) return false;
    uint8_t axis[20];
    if (!file_read_at(fvar_off + axis_off, axis, 20)) return false;
    if (memcmp(axis, "wght", 4) != 0) {
        ESP_LOGW(TAG, "first axis is not wght");
    }
    wght_min = (int)lroundf((float)((int32_t)be32(axis + 4)) / 65536.0f);
    wght_def = (int)lroundf((float)((int32_t)be32(axis + 8)) / 65536.0f);
    wght_max = (int)lroundf((float)((int32_t)be32(axis + 12)) / 65536.0f);

    uint8_t gh[20];
    if (!file_read_at(gvar_off, gh, sizeof(gh))) return false;
    gvar_axis_count = be16(gh + 4);
    shared_tuple_count = be16(gh + 6);
    uint32_t shared_off = be32(gh + 8);
    uint16_t gc = be16(gh + 12);
    uint16_t flags = be16(gh + 14);
    gvar_data_array_off = be32(gh + 16);
    const bool gvar_long = (flags & 1u) != 0;
    if (gc != (uint16_t)num_glyphs || gvar_axis_count < 1) {
        ESP_LOGW(TAG, "gvar header mismatch gc=%u flags=%u", gc, flags);
        return false;
    }
    if (shared_tuple_count > 8) shared_tuple_count = 8;
    if (shared_tuple_count > 0) {
        uint8_t st[16];
        if (!file_read_at(gvar_off + shared_off, st, (size_t)shared_tuple_count * 2)) {
            return false;
        }
        for (int i = 0; i < shared_tuple_count; i++) {
            shared_tuple_f2dot14[i] = (int16_t)be16(st + i * 2);
        }
    }
    gvar_glyph_off = heap_caps_malloc(
        (size_t)(num_glyphs + 1) * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (gvar_glyph_off == NULL) return false;
    // 短偏移是 16 位字偏移（×2）；子集 TTF 常走短格式，源 VF 是长格式。
    const size_t off_bytes = (size_t)(num_glyphs + 1) * (gvar_long ? 4u : 2u);
    if (!file_read_at(gvar_off + 20, gvar_glyph_off, off_bytes)) {
        heap_caps_free(gvar_glyph_off);
        gvar_glyph_off = NULL;
        return false;
    }
    uint8_t* raw = (uint8_t*)gvar_glyph_off;
    if (gvar_long) {
        for (int i = 0; i <= num_glyphs; i++) {
            gvar_glyph_off[i] = be32(raw + (size_t)i * 4);
        }
    } else {
        for (int i = num_glyphs; i >= 0; i--) {
            gvar_glyph_off[i] = (uint32_t)be16(raw + (size_t)i * 2) * 2u;
        }
    }
    file_gvar_off = gvar_off;
    gvar_ready = true;
    current_weight = 400;
    ESP_LOGI(
        TAG, "gvar wght %d..%d def %d, %d shared tuples",
        wght_min, wght_max, wght_def, shared_tuple_count
    );
    return true;
}

// 把当前字形及其复合引用从 SD 填进工作字体的 glyf 窗口，并改写 loca。
static bool pack_glyph_tree(int root_gid) {
    // CFF 字形由 stb 直接从 CFF 表取，没有 glyf 可打包。
    // CFF outlines come straight out of the CFF table, so there is no glyf run to pack.
    if (cff_outline) return true;
    if (root_gid == packed_root && current_weight == packed_weight) {
        return true;
    }
    if (root_gid < 0 || root_gid >= num_glyphs) return true;

    int tree[TTF_TREE_MAX];
    int tree_n = 0;
    tree[tree_n++] = root_gid;

    uint8_t* arena = font_data + work_glyf_off;
    uint32_t packed = 0;
    for (int i = 0; i < tree_n; i++) {
        int gid = tree[i];
        uint32_t len = file_glyph_len(gid);
        if (packed + len > TTF_GLYF_ARENA) {
            ESP_LOGE(TAG, "glyf arena overflow gid=%d len=%u", gid, (unsigned)len);
            return false;
        }
        uint8_t* blob = arena + packed;
        if (len > 0
            && !file_read_at(file_glyf_off + file_glyph_off(gid), blob, len)) {
            return false;
        }
        uint32_t used = len;
        if (len > 0) {
            uint32_t varied = len;
            if (apply_gvar_to_blob(
                    blob, &varied, TTF_GLYF_ARENA - packed, gid
                )) {
                used = varied;
            }
        }
        set_work_loca(gid, packed);
        packed += used;
        packed = (packed + 3u) & ~3u;
        if (packed > TTF_GLYF_ARENA) return false;
        set_work_loca(gid + 1, packed);
        if (used >= 2 && !enqueue_composite_children(blob, used, tree, &tree_n)) {
            return false;
        }
    }
    packed_root = root_gid;
    packed_weight = current_weight;
    return true;
}

static uint32_t align4(uint32_t v) {
    return (v + 3u) & ~3u;
}

static bool copy_table(
    uint8_t* dst, uint32_t dst_off, uint32_t file_off, uint32_t length
) {
    return file_read_at(file_off, dst + dst_off, length);
}

static uint8_t* build_working_font(const uint8_t* header, size_t header_len) {
    // OTTO 是 CFF/OpenType：没有 glyf/loca，拼"工作字体"无从谈起。stb 自己会读 CFF 表，
    // 所以整份原始字体就是字体数据；字节已在 PSRAM，借用不复制。文件型字体（SD 上的
    // 系统字体）不支持这条路径，得先把整份读进内存才有意义。
    // OTTO means CFF/OpenType: there is no glyf/loca, so there is nothing to assemble. stb
    // reads the CFF table itself, so the original bytes are the font data; they are already in
    // PSRAM and are borrowed rather than copied. A file-backed face (a system font on the card)
    // is not supported here, since it would have to be read in full first.
    if (header_len >= 4 && memcmp(header, "OTTO", 4) == 0) {
        if (font_mem == NULL) {
            ESP_LOGW(TAG, "CFF face is file-backed; only in-memory CFF is supported");
            return NULL;
        }
        uint32_t maxp_off = 0, maxp_len = 0;
        if (!find_sfnt_table(header, header_len, "maxp", &maxp_off, &maxp_len) ||
            maxp_len < 6 || (size_t)maxp_off + 6 > font_mem_len) {
            ESP_LOGW(TAG, "CFF face has no usable maxp table");
            return NULL;
        }
        s_cur->f_cff = true;
        s_cur->f_data_borrowed = true;
        num_glyphs = be16(font_mem + maxp_off + 4);
        file_glyf_off = 0;
        file_glyf_len = 0;
        file_loca = NULL;
        file_loca_len = 0;
        work_loca_off = 0;
        work_glyf_off = 0;
        return (uint8_t*)font_mem;
    }

    const char* tags[] = { "cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp" };
    sfnt_table_t src[7];
    for (int i = 0; i < 7; i++) {
        memcpy(src[i].tag, tags[i], 4);
        if (memcmp(tags[i], "glyf", 4) == 0) {
            uint32_t glyf_len = 0;
            if (!find_sfnt_table(header, header_len, "glyf", &file_glyf_off, &glyf_len)) {
                // OTTO（CFF/OpenType）没有 glyf/loca，这套引擎只做 TrueType 轮廓；这里以前
                // 静默返回，装载失败时看不出是字体格式不对还是文件坏了。
                // An OTTO (CFF/OpenType) face has no glyf/loca and this engine only handles
                // TrueType outlines. This used to return silently, leaving a failed load
                // indistinguishable from a corrupt file.
                ESP_LOGW(TAG, "no glyf table: CFF/OpenType face unsupported");
                return NULL;
            }
            file_glyf_len = glyf_len;
            src[i].offset = 0;
            src[i].length = TTF_GLYF_ARENA;
            continue;
        }
        if (!find_sfnt_table(
                header, header_len, tags[i], &src[i].offset, &src[i].length
            )) {
            ESP_LOGE(TAG, "missing table %.4s", tags[i]);
            return NULL;
        }
    }

    uint32_t cursor = 12 + 7 * 16;
    uint32_t dst_off[7];
    uint32_t total = cursor;
    for (int i = 0; i < 7; i++) {
        dst_off[i] = total;
        total = align4(total + src[i].length);
    }

    uint8_t* data = heap_caps_calloc(1, total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) return NULL;

    put_be32(data, 0x00010000);
    put_be16(data + 4, 7);
    put_be16(data + 6, 64);
    put_be16(data + 8, 2);
    put_be16(data + 10, 32);

    for (int i = 0; i < 7; i++) {
        uint8_t* rec = data + 12 + i * 16;
        memcpy(rec, src[i].tag, 4);
        put_be32(rec + 8, dst_off[i]);
        put_be32(rec + 12, src[i].length);
        if (memcmp(src[i].tag, "glyf", 4) == 0) continue;
        if (!copy_table(data, dst_off[i], src[i].offset, src[i].length)) {
            heap_caps_free(data);
            return NULL;
        }
    }

    work_loca_off = dst_off[5];
    work_glyf_off = dst_off[1];
    file_loca_len = src[5].length;
    file_loca = heap_caps_malloc(file_loca_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (file_loca == NULL) {
        heap_caps_free(data);
        return NULL;
    }
    memcpy(file_loca, data + work_loca_off, file_loca_len);

    uint8_t* head = data + dst_off[2];
    loca_long = be16(head + 50) != 0;
    uint8_t* maxp = data + dst_off[6];
    num_glyphs = be16(maxp + 4);
    return data;
}

static uint32_t decode_utf8(const char** cursor) {
    const uint8_t* p = (const uint8_t*)*cursor;
    if (p[0] == 0) return 0;
    if (p[0] < 0x80) {
        *cursor += 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *cursor += 2;
        return ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *cursor += 3;
        return ((uint32_t)(p[0] & 0x0F) << 12)
            | ((uint32_t)(p[1] & 0x3F) << 6)
            | (p[2] & 0x3F);
    }
    if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80
        && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *cursor += 4;
        return ((uint32_t)(p[0] & 0x07) << 18)
            | ((uint32_t)(p[1] & 0x3F) << 12)
            | ((uint32_t)(p[2] & 0x3F) << 6)
            | (p[3] & 0x3F);
    }
    *cursor += 1;
    return 0xFFFD;
}

static int clamp_size(int size) {
    return size == TTF_SIZE_LARGE ? TTF_SIZE_LARGE : TTF_SIZE_SMALL;
}

static int size_to_px(int size) {
    return clamp_size(size) == TTF_SIZE_LARGE ? TTF_PIXEL_LARGE : TTF_PIXEL_SMALL;
}

static int clamp_px(int pixel_height) {
    if (pixel_height < 12) return 12;
    // 锁屏91em需要更高的原生栅格高度；仍小于8位缓存键上限。
    // The 91em lock count needs a taller native raster; stay below the byte-sized cache key limit.
    if (pixel_height > 160) return 160;
    return pixel_height;
}

/* ---- 字形缓存 / Glyph cache ---- */
static unsigned cache_bucket(uint32_t codepoint, int size) {
    return (unsigned)((codepoint * 33u + (uint32_t)size) % TTF_CACHE_BUCKETS);
}

// 新条目 calloc 后 prev/next 都是 NULL，但并不在链表里。若把「prev==NULL」
// 当成「我就是头」，会把整条 LRU 掐掉，只剩刚插入的那一个；unload / 超限
// 淘汰都只走 LRU，于是旧字形永远留在哈希桶里，换字体后还会被命中。
static void lru_detach(glyph_entry_t* entry) {
    if (entry->lru_prev != NULL) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else if (lru_head == entry) {
        lru_head = entry->lru_next;
    }
    if (entry->lru_next != NULL) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else if (lru_tail == entry) {
        lru_tail = entry->lru_prev;
    }
    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

static void lru_touch(glyph_entry_t* entry) {
    if (lru_head == entry) return;
    lru_detach(entry);
    entry->lru_next = lru_head;
    if (lru_head != NULL) lru_head->lru_prev = entry;
    lru_head = entry;
    if (lru_tail == NULL) lru_tail = entry;
}

// 字形缓存的总预算。TTF_CACHE_LIMIT 原来是每槽的，多槽之后那是 4 × 512 KB，而字体字节、
// loca 和字形表本身已经吃掉好几 MB；抽干 PSRAM 的症状不是文字出问题，而是插图解码拿不到
// 连续内存——翻几页之后图片就没了。所以这里按所有槽合起来算。
// One budget for all glyph caches. TTF_CACHE_LIMIT used to be per slot, which across four slots
// is 4 x 512 KB, on top of the face bytes, loca and the outline tables. Draining PSRAM does not
// show up as broken text but as illustration decoding that cannot get a contiguous block:
// images disappear after a few page turns. So the budget is now shared.
static size_t s_cache_total;

static void cache_reset(void) {
    size_t n = 0;
    for (unsigned i = 0; i < TTF_CACHE_BUCKETS; i++) {
        glyph_entry_t* entry = cache_buckets[i];
        cache_buckets[i] = NULL;
        while (entry != NULL) {
            glyph_entry_t* next = entry->hash_next;
            heap_caps_free(entry->bitmap);
            heap_caps_free(entry);
            entry = next;
            n++;
        }
    }
    lru_head = NULL;
    lru_tail = NULL;
    s_cache_total -= cache_bytes;
    cache_bytes = 0;
    if (n > 0) {
        ESP_LOGI(TAG, "glyph cache dropped %u entries", (unsigned)n);
    }
}

// 本槽已经淘汰干净还是超预算，说明份额被别的槽占着；整体清一次。
// 让缓存反复回填，好过让整页插图解码失败。
// This slot is empty and we are still over budget, so another slot is holding it: drop them
// all. Refilling caches beats a page of illustrations that cannot be decoded.
static void cache_clear_all(void) {
    int keep = s_cur_index;
    for (int i = 0; i < TTF_FONT_SLOTS; i++) {
        if (!s_slots[i].f_ready || i == keep) continue;
        s_cur_index = i;
        s_cur = &s_slots[i];
        cache_reset();
    }
    s_cur_index = keep;
    s_cur = &s_slots[keep];
}

static void cache_evict_one(void) {
    glyph_entry_t* victim = lru_tail;
    if (victim == NULL) return;
    lru_detach(victim);

    unsigned bucket = cache_bucket(victim->codepoint, victim->size);
    glyph_entry_t** slot = &cache_buckets[bucket];
    while (*slot != NULL) {
        if (*slot == victim) {
            *slot = victim->hash_next;
            break;
        }
        slot = &(*slot)->hash_next;
    }

    cache_bytes -= victim->bitmap_bytes + sizeof(*victim);
    s_cache_total -= victim->bitmap_bytes + sizeof(*victim);
    heap_caps_free(victim->bitmap);
    heap_caps_free(victim);
}

static void cache_reserve(size_t extra) {
    while (lru_tail != NULL && s_cache_total + extra > TTF_CACHE_LIMIT) {
        cache_evict_one();
    }
    if (s_cache_total + extra > TTF_CACHE_LIMIT) cache_clear_all();
}

static glyph_entry_t* cache_lookup(uint32_t codepoint, int size) {
    unsigned bucket = cache_bucket(codepoint, size);
    for (glyph_entry_t* entry = cache_buckets[bucket]; entry != NULL; entry = entry->hash_next) {
        if (entry->codepoint == codepoint && entry->size == (uint8_t)size
            && entry->weight == (uint16_t)current_weight) {
            lru_touch(entry);
            return entry;
        }
    }
    return NULL;
}

static glyph_entry_t* rasterize_glyph(uint32_t codepoint, int pixel_height) {
    int gid = stbtt_FindGlyphIndex(&font_info, (int)codepoint);
    if (!pack_glyph_tree(gid)) return NULL;

    float scale = stbtt_ScaleForPixelHeight(&font_info, (float)pixel_height);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&font_info, gid, scale, scale, &x0, &y0, &x1, &y1);

    int width = x1 - x0;
    int height = y1 - y0;
    if (width < 0) width = 0;
    if (height < 0) height = 0;

    size_t bitmap_bytes = (size_t)width * (size_t)height;
    cache_reserve(bitmap_bytes + sizeof(glyph_entry_t));

    glyph_entry_t* entry = heap_caps_calloc(
        1, sizeof(*entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (entry == NULL) return NULL;

    if (bitmap_bytes > 0) {
        entry->bitmap = heap_caps_malloc(
            bitmap_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (entry->bitmap == NULL) {
            heap_caps_free(entry);
            return NULL;
        }
        stbtt_MakeGlyphBitmap(
            &font_info, entry->bitmap, width, height, width, scale, scale, gid
        );
    }

    int advance = 0;
    int lsb = 0;
    stbtt_GetGlyphHMetrics(&font_info, gid, &advance, &lsb);

    entry->codepoint = codepoint;
    entry->size = (uint8_t)pixel_height;
    entry->weight = (uint16_t)current_weight;
    entry->width = (int16_t)width;
    entry->height = (int16_t)height;
    entry->left = (int16_t)x0;
    entry->top = (int16_t)(-y0);
    entry->advance_x = (int16_t)lroundf(advance * scale);
    entry->bitmap_bytes = bitmap_bytes;

    unsigned bucket = cache_bucket(codepoint, pixel_height);
    entry->hash_next = cache_buckets[bucket];
    cache_buckets[bucket] = entry;
    cache_bytes += bitmap_bytes + sizeof(*entry);
    s_cache_total += bitmap_bytes + sizeof(*entry);
    lru_touch(entry);
    return entry;
}

static const glyph_entry_t* get_glyph(uint32_t codepoint, int pixel_height) {
    if (!font_ready) return NULL;
    pixel_height = clamp_px(pixel_height);
    if (bench_on) bench.glyphs++;
    glyph_entry_t* entry = cache_lookup(codepoint, pixel_height);
    if (entry != NULL) {
        if (bench_on) bench.hits++;
        return entry;
    }
    if (bench_on) bench.misses++;
    int64_t t0 = bench_on ? esp_timer_get_time() : 0;
    entry = rasterize_glyph(codepoint, pixel_height);
    if (bench_on) bench.raster_us += esp_timer_get_time() - t0;
    return entry;
}

/* ---- 绘制期的字体切换 / Faces during a draw ---- */

// 一行里可能混着几种字体：正文用书内字体，行内数字/引文用另一种。调用方在绘制前
// 交出这一行的切换点，绘制循环走到哪个字节就换哪套字体；行宽、字距、两端对齐的
// 算法一个字都不动，只有取字形的槽跟着变。
// A line may mix faces: body text in a book face, inline digits or quotes in another. The
// caller hands over this line's switch points before drawing; the glyph loop follows them.
// Line width, tracking and justification math are untouched -- only the slot changes.
static const ttf_run_t* s_draw_runs;
static size_t s_draw_run_count;

void ttf_draw_set_runs(const ttf_run_t* runs, size_t count) {
    s_draw_runs = (runs != NULL && count > 0) ? runs : NULL;
    s_draw_run_count = s_draw_runs != NULL ? count : 0;
    // 先站到首段的字体上：各绘制函数开头的 font_ready 判断看的就是当前槽。首段没装载
    // 就留在原地，绘制循环自己会按段跳过它。
    // Stand on the first run's face: the font_ready test at the top of each draw entry
    // checks the current slot. An unloaded first run is left alone and skipped by the loop.
    if (s_draw_runs != NULL && ttf_font_slot_ready(s_draw_runs[0].slot)) {
        ttf_font_select(s_draw_runs[0].slot);
    }
}

// 一行里的 run 很少（通常只有一个），线性看过去比二分更快也更好读。
// A line has few runs, usually one, so a linear walk beats a binary search.
static uint8_t run_slot_at(size_t offset) {
    uint8_t slot = s_draw_runs[0].slot;
    for (size_t i = 1; i < s_draw_run_count; ++i) {
        if (s_draw_runs[i].offset > offset) break;
        slot = s_draw_runs[i].slot;
    }
    return slot;
}

// 绘制循环每个字都调用，所以只在真的换槽时才动游标。没有 run 表时什么都不做，
// 保持单字体调用方的行为完全不变。
// Called once per glyph, so the cursor only moves on a real change. With no run table it
// does nothing, leaving single-face callers exactly as they were.
static inline void run_apply(size_t offset) {
    if (s_draw_runs == NULL) return;
    uint8_t slot = run_slot_at(offset);
    // 没装载成功的槽一律不选：否则循环会拿一张空字体去取字形。
    // Never select an unloaded slot, or the loop would look glyphs up in an empty face.
    if (slot != (uint8_t)s_cur_index && ttf_font_slot_ready(slot)) ttf_font_select(slot);
}

static void warm_text_io(int pixel_height, const char* text, const char* end);

// 按 run 分段预热分块 IO：不同字体的块缓存是分开的，混在一起 flush 会串槽。
// Warm block IO per run: each face keeps its own block cache, and flushing them together
// would mix slots.
static void warm_runs(int pixel_height, const char* text) {
    const char* end = text + strlen(text);
    if (s_draw_runs == NULL) {
        warm_text_io(pixel_height, text, end);
        return;
    }
    for (size_t i = 0; i < s_draw_run_count; ++i) {
        const char* from = text + s_draw_runs[i].offset;
        const char* to = i + 1 < s_draw_run_count ? text + s_draw_runs[i + 1].offset : end;
        if (from >= end || to <= from) continue;
        if (to > end) to = end;
        ttf_font_select(s_draw_runs[i].slot);
        warm_text_io(pixel_height, from, to);
    }
}

static void warm_text_io(int pixel_height, const char* text, const char* end) {
    // CFF 没有 glyf 分块可预读：`file_loca` 是空的，照旧走下去会取到空表。
    // There are no glyf blocks to prefetch for CFF: `file_loca` is empty and the walk below
    // would index a null table.
    if (text == NULL || glyf_ram != NULL || cff_outline) return;
    touch_n = 0;
    const char* cursor = text;
    uint32_t cps[TTF_GATHER_MAX];
    int seen = 0;
    pixel_height = clamp_px(pixel_height);
    while (cursor < end && *cursor != '\0' && seen < TTF_GATHER_MAX) {
        uint32_t cp = decode_utf8(&cursor);
        if (cp == 0) break;
        bool dup = false;
        for (int i = 0; i < seen; i++) {
            if (cps[i] == cp) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        cps[seen++] = cp;
        if (cache_lookup(cp, pixel_height) != NULL) continue;
        int gid = stbtt_FindGlyphIndex(&font_info, (int)cp);
        if (gid < 0 || gid >= num_glyphs) continue;
        io_touch(file_glyf_off + file_glyph_off(gid), file_glyph_len(gid));
        if (gvar_ready && current_weight != wght_def && gvar_glyph_off != NULL) {
            uint32_t a = gvar_glyph_off[gid];
            uint32_t b = gvar_glyph_off[gid + 1];
            if (b > a) {
                io_touch(file_gvar_off + gvar_data_array_off + a, b - a);
            }
        }
    }
    io_flush_touches();
}

static int measure_width(int pixel_height, const char* text) {
    int width = 0;
    float scale = stbtt_ScaleForPixelHeight(&font_info, (float)pixel_height);
    const char* cursor = text;
    while (*cursor != '\0') {
        run_apply((size_t)(cursor - text));
        uint32_t cp = decode_utf8(&cursor);
        const glyph_entry_t* glyph = cache_lookup(cp, pixel_height);
        if (glyph != NULL) {
            width += glyph->advance_x;
        } else {
            // 排版只需字宽；实际绘制时才读取轮廓并生成位图。/ Layout needs advances only; drawing loads outlines and rasterizes.
            int advance = 0, lsb = 0;
            int gid = stbtt_FindGlyphIndex(&font_info, (int)cp);
            stbtt_GetGlyphHMetrics(&font_info, gid, &advance, &lsb);
            width += (int)lroundf(advance * scale);
        }
    }
    return width;
}

// 这两个查询问的是“系统字体”：主循环、设置页和书架都靠它们判断当前字体能否排这本书。
// 内嵌字体把游标借走时，这里必须仍然看 0 号槽。
// These two answer for the system face: the loop, settings and shelf ask whether the
// active face can set this book. While an embedded face owns the cursor, slot 0 is
// still the answer.
bool ttf_font_ready(void) {
    return s_slots[TTF_FONT_SLOT_SYSTEM].f_ready;
}

bool ttf_font_has_text(const char *text) {
    const ttf_slot_t* sys = &s_slots[TTF_FONT_SLOT_SYSTEM];
    if (!sys->f_ready || !text) return false;
    while (*text) {
        if (!stbtt_FindGlyphIndex(&sys->f_font_info, (int)decode_utf8(&text))) return false;
    }
    return true;
}

// 字重是用户级设置，对每一套驻留字体都成立；写在当前槽上会让内嵌字体漏掉。
// Weight is a user-level setting that holds for every resident face; writing it on the
// current slot alone would miss the embedded ones.
void ttf_set_weight(int wght) {
    int w = clamp_weight(wght);
    for (int i = 0; i < TTF_FONT_SLOTS; i++) {
        s_slots[i].f_weight = w;
    }
}

int ttf_get_weight(void) {
    return s_slots[TTF_FONT_SLOT_SYSTEM].f_weight;
}

int ttf_ascender(int size) {
    return ttf_ascender_px(size_to_px(size));
}

int ttf_ascender_px(int pixel_height) {
    if (!font_ready) return 0;
    pixel_height = clamp_px(pixel_height);
    return (int)lroundf(
        raw_ascent_units * stbtt_ScaleForPixelHeight(&font_info, (float)pixel_height)
    );
}

int ttf_em_height_px(int em_size) {
    if (!font_ready) return clamp_px(em_size);
    if (em_size < 1) em_size = 1;
    if (em_size > 120) em_size = 120;
    float scale = stbtt_ScaleForPixelHeight(&font_info, 1.f);
    float em = stbtt_ScaleForMappingEmToPixels(&font_info, (float)em_size);
    return scale > 0 && isfinite(em / scale) ? clamp_px((int)lroundf(em / scale)) : clamp_px(em_size);
}

void ttf_font_cache_clear(void) {
    cache_reset();
    packed_root = -1;
    packed_weight = -1;
    io_reset();
}

void ttf_bench_begin(void) {
    memset(&bench, 0, sizeof(bench));
    bench_on = true;
    bench_start_us = esp_timer_get_time();
}

void ttf_bench_end(ttf_bench_stats_t* out) {
    bench_on = false;
    bench.total_us = esp_timer_get_time() - bench_start_us;
    if (out != NULL) *out = bench;
}

static void abandon_font_source(void) {
    if (font_fd >= 0) {
        close(font_fd);
        font_fd = -1;
    }
    // 内嵌字体的字节由槽位自己分配，关槽时要连它一起还回去。
    // An embedded face owns heap bytes; closing the slot must hand them back too.
    if (font_mem_owned && font_mem != NULL) {
        heap_caps_free((void*)font_mem);
    }
    font_mem_owned = false;
    font_mem = NULL;
    font_mem_len = 0;
}

// 只清“当前槽”。对外入口 ttf_font_unload() 永远作用于 0 号槽，见文件后半的字体槽一节。
// Clears the current slot only. The public ttf_font_unload() always acts on slot 0; see
// the font-slot section further down.
static void unload_here(void) {
    font_ready = false;
    cache_reset();
    io_unmap();
    io_reset();
    abandon_font_source();
    font_file_pos = UINT32_MAX;
    heap_caps_free(file_loca);
    file_loca = NULL;
    // 借来的字体字节属于槽的内存镜像，由 abandon_font_source 归还。
    // Borrowed face bytes belong to the slot's memory image and go back through
    // abandon_font_source.
    if (!font_data_borrowed) heap_caps_free(font_data);
    font_data = NULL;
    font_data_borrowed = false;
    cff_outline = false;
    reset_variation();
    packed_root = -1;
    packed_weight = -1;
    num_glyphs = 0;
    file_glyf_off = 0;
    file_glyf_len = 0;
    file_gvar_off = 0;
    file_gvar_len = 0;
    file_loca_len = 0;
    loca_long = false;
    raw_ascent_units = 0;
    memset(&font_info, 0, sizeof(font_info));
    memset(size_metrics, 0, sizeof(size_metrics));
}

static esp_err_t load_opened_font(void) {
    font_file_pos = UINT32_MAX;
    io_ensure();
    io_reset();
    io_unmap();

    uint8_t header[12 + 32 * 16];
    if (!file_read_at(0, header, sizeof(header))) {
        abandon_font_source();
        return ESP_FAIL;
    }

    font_data = build_working_font(header, sizeof(header));
    if (font_data == NULL) {
        abandon_font_source();
        ESP_LOGE(TAG, "build working font failed");
        return ESP_ERR_NO_MEM;
    }

    // CFF 没有 glyf 表，长度为零，不必去映射。
    // A CFF face has no glyf table; its length is zero and there is nothing to map.
    if (file_glyf_len) {
        try_map_table(
            file_glyf_off, file_glyf_len, &glyf_ram, &glyf_ram_off, &glyf_ram_len, "glyf"
        );
    }

    load_variation(header, sizeof(header));
    if (gvar_ready && file_gvar_len > 0) {
        try_map_table(
            file_gvar_off, file_gvar_len,
            &gvar_ram, &gvar_ram_off, &gvar_ram_len, "gvar"
        );
    }

    if (!stbtt_InitFont(&font_info, font_data, 0)) {
        io_unmap();
        heap_caps_free(file_loca);
        heap_caps_free(font_data);
        file_loca = NULL;
        font_data = NULL;
        abandon_font_source();
        ESP_LOGE(TAG, "stbtt_InitFont failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    int raw_ascent = 0, raw_descent = 0, raw_gap = 0;
    stbtt_GetFontVMetrics(&font_info, &raw_ascent, &raw_descent, &raw_gap);
    raw_ascent_units = raw_ascent;
    const int pixel_heights[2] = { TTF_PIXEL_SMALL, TTF_PIXEL_LARGE };
    for (int i = 0; i < 2; i++) {
        size_metrics[i].scale = stbtt_ScaleForPixelHeight(&font_info, (float)pixel_heights[i]);
        size_metrics[i].ascent = (int)lroundf(raw_ascent * size_metrics[i].scale);
    }

    font_ready = true;
    ESP_LOGI(
        TAG, "%s %s, %d glyphs, glyf %u KB%s, gvar %u KB%s, working %u KB",
        glyf_ram != NULL ? "mapped" : "stream",
        font_path, num_glyphs,
        (unsigned)(file_glyf_len / 1024), glyf_ram != NULL ? " ram" : "",
        (unsigned)(file_gvar_len / 1024), gvar_ram != NULL ? " ram" : "",
        (unsigned)((file_loca_len + TTF_GLYF_ARENA) / 1024)
    );
    return ESP_OK;
}

/* ---- 字体槽 / Font slots ---- */

// 槽 0 是系统字体，只由 ttf_font_open* 重载；1..N 装书籍内嵌字体，按需打开。
// Slot 0 is the system face, reloaded only through ttf_font_open*; slots 1..N hold
// embedded book faces opened on demand.

// 这里清的是槽结构本身，包括只在装载时赋初值的文件游标、packed 记忆和字重。
// This clears the slot struct itself, including the file cursor, the packed-glyph memo
// and the weight that only get their initial values at load time.
static void slot_init(ttf_slot_t* s) {
    memset(s, 0, sizeof(*s));
    s->f_fd = -1;
    s->f_file_pos = UINT32_MAX;
    s->f_packed_root = -1;
    s->f_packed_weight = -1;
    s->f_weight = TTF_WGHT_DEF;
    s->f_wght_min = TTF_WGHT_MIN;
    s->f_wght_def = TTF_WGHT_DEF;
    s->f_wght_max = TTF_WGHT_MAX;
}

static bool s_slots_ready;

static void slot_restore(int index) {
    s_cur_index = index;
    s_cur = &s_slots[index];
}

void ttf_font_slots_init(void) {
    if (s_slots_ready) return;
    for (int i = 0; i < TTF_FONT_SLOTS; i++) {
        slot_init(&s_slots[i]);
    }
    s_slots_ready = true;
    slot_restore(TTF_FONT_SLOT_SYSTEM);
}

// 借游标去指定槽干活；返回原槽号，调用方负责还原。
// Lend the cursor to one slot; returns the previous index for the caller to restore.
static int slot_lend(int slot) {
    int keep = s_cur_index;
    s_cur_index = slot;
    s_cur = &s_slots[slot];
    return keep;
}

int ttf_font_select(int slot) {
    ttf_font_slots_init();
    int keep = s_cur_index;
    if (slot >= 0 && slot < TTF_FONT_SLOTS) {
        s_cur_index = slot;
        s_cur = &s_slots[slot];
    }
    return keep;
}

int ttf_font_selected(void) {
    return s_cur_index;
}

bool ttf_font_slot_ready(int slot) {
    return slot >= 0 && slot < TTF_FONT_SLOTS && s_slots[slot].f_ready;
}

bool ttf_font_slot_has_text(int slot, const char* text) {
    if (slot < 0 || slot >= TTF_FONT_SLOTS || text == NULL) return false;
    const ttf_slot_t* s = &s_slots[slot];
    if (!s->f_ready) return false;
    while (*text) {
        if (!stbtt_FindGlyphIndex(&s->f_font_info, (int)decode_utf8(&text))) return false;
    }
    return true;
}

// 对外入口永远拆系统字体：主循环和存储卡页说的都是“把当前字体放掉”，
// 不能让游标停在内嵌槽上时误伤书籍字体。
// The public entry always drops the system face: the loop and the card page mean the
// active face, and must not hit a book face just because the cursor sits there.
void ttf_font_unload(void) {
    ttf_font_slots_init();
    int keep = slot_lend(TTF_FONT_SLOT_SYSTEM);
    unload_here();
    s_cur_index = keep;
    s_cur = &s_slots[keep];
}

// 把**所有**槽的字形位图和分块缓冲还给堆，字体本身保持装载。阅读页开书前调用：
// 系统字体的缓存是 PSRAM 里最容易腾的一块，而书内字体要的正是这块地方；字体不动，
// 用到时按需重新缓存。
// Hand every slot's glyph bitmaps and block buffers back to the heap while keeping the faces
// loaded. The reader calls this before opening a book: the system face's cache is the easiest
// PSRAM to reclaim, and embedded faces need exactly that room. Faces stay put and re-cache on
// demand.
size_t ttf_font_cache_clear_all(void) {
    ttf_font_slots_init();
    int keep = s_cur_index;
    size_t freed = 0;
    for (int i = 0; i < TTF_FONT_SLOTS; i++) {
        if (!s_slots[i].f_ready) continue;
        s_cur_index = i;
        s_cur = &s_slots[i];
        freed += cache_bytes;
        cache_reset();
        packed_root = -1;
        packed_weight = -1;
        io_reset();
    }
    s_cur_index = keep;
    s_cur = &s_slots[keep];
    return freed;
}

void ttf_font_close_embedded(void) {
    ttf_font_slots_init();
    int keep = s_cur_index;
    for (int i = TTF_FONT_SLOT_SYSTEM + 1; i < TTF_FONT_SLOTS; i++) {
        if (!s_slots[i].f_used) continue;
        s_cur_index = i;
        s_cur = &s_slots[i];
        unload_here();
        s_slots[i].f_used = false;
    }
    if (keep <= TTF_FONT_SLOT_SYSTEM || !s_slots[keep].f_used) {
        keep = TTF_FONT_SLOT_SYSTEM;
    }
    s_cur_index = keep;
    s_cur = &s_slots[keep];
}

int ttf_font_open_mem(uint8_t* data, size_t len, const char* label) {
    ttf_font_slots_init();
    if (data == NULL || len < 64) {
        if (data != NULL) heap_caps_free(data);
        return -1;
    }

    int slot = -1;
    for (int i = TTF_FONT_SLOT_SYSTEM + 1; i < TTF_FONT_SLOTS; i++) {
        if (!s_slots[i].f_used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        heap_caps_free(data);
        return -1;
    }

    int keep = slot_lend(slot);
    unload_here();   // 槽里可能还留着上一本书的字形 / the slot may hold a previous book
    s_slots[slot].f_used = true;
    font_fd = -1;
    font_mem = data;
    font_mem_len = (uint32_t)len;
    font_mem_owned = true;
    if (label != NULL) {
        strlcpy(font_path, label, sizeof(font_path));
    }

    esp_err_t err = ensure_work() ? load_opened_font() : ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        // 顺带把 data 还掉，失败后调用方不必再管这块内存。
        // This also hands back data, so the caller owns nothing after a failure.
        unload_here();
        s_slots[slot].f_used = false;
        slot = -1;
    }
    s_cur_index = keep;
    s_cur = &s_slots[keep];
    return slot;
}

// 系统字体永远住在 0 号槽。这两个入口把游标借过去再还回来，免得设置页换系统字体
// 时踩掉正在用的内嵌槽。
// The system face always lives in slot 0. These entries lend the cursor and restore it,
// so reloading the system face never clobbers a live embedded slot.
static esp_err_t open_builtin_here(void) {
    unload_here();
    if (!ensure_work()) return ESP_ERR_NO_MEM;

    font_mem = builtin_ttf_start;
    font_mem_len = (uint32_t)(builtin_ttf_end - builtin_ttf_start);
    font_mem_owned = false;
    font_fd = -1;
    strlcpy(font_path, TTF_FONT_BUILTIN, sizeof(font_path));
    ESP_LOGI(TAG, "using builtin (%u KB)", (unsigned)(font_mem_len / 1024));
    return load_opened_font();
}

static esp_err_t open_path_here(const char* path) {
    if (ttf_font_path_is_builtin(path)) {
        return ttf_font_open_builtin();
    }

    char prev[TTF_FONT_PATH_MAX];
    strlcpy(prev, font_path, sizeof(prev));
    bool had = font_ready;
    bool prev_file = had && !ttf_font_path_is_builtin(prev);

    unload_here();
    if (!ensure_work()) return ESP_ERR_NO_MEM;

    font_fd = open_font_file(path);
    if (font_fd < 0) {
        ESP_LOGW(TAG, "TTF missing: %s", path);
        if (prev_file) {
            font_fd = try_open_path(prev);
            if (font_fd >= 0) return load_opened_font();
        }
        return ttf_font_open_builtin() == ESP_OK ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    esp_err_t err = load_opened_font();
    if (err != ESP_OK) {
        if (prev_file && strcmp(prev, path) != 0) {
            unload_here();
            font_fd = try_open_path(prev);
            if (font_fd >= 0 && load_opened_font() == ESP_OK) return err;
        }
        return ttf_font_open_builtin() == ESP_OK ? err : ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ttf_font_open_builtin(void) {
    ttf_font_slots_init();
    int keep = slot_lend(TTF_FONT_SLOT_SYSTEM);
    esp_err_t err = open_builtin_here();
    s_cur_index = keep;
    s_cur = &s_slots[keep];
    return err;
}

esp_err_t ttf_font_open(const char* path) {
    ttf_font_slots_init();
    int keep = slot_lend(TTF_FONT_SLOT_SYSTEM);
    esp_err_t err = open_path_here(path);
    s_cur_index = keep;
    s_cur = &s_slots[keep];
    return err;
}

esp_err_t ttf_font_init(void) {
    ttf_font_slots_init();
    if (ttf_font_ready()) return ESP_OK;
    const char* path = app_settings_font_path();
    if (ttf_font_path_is_builtin(path)) {
        return ttf_font_open_builtin();
    }
    esp_err_t err = ttf_font_open(path);
    if (err != ESP_OK && ttf_font_ready()) return ESP_OK;
    return err;
}

void ttf_measure_line(int size, const char* text, int* above, int* below) {
    ttf_measure_line_px(size_to_px(size), text, above, below);
}

void ttf_measure_line_px(
    int pixel_height, const char* text, int* above, int* below
) {
    int max_above = 0;
    int max_below = 0;
    if (font_ready && text != NULL) {
        pixel_height = clamp_px(pixel_height);
        warm_runs(pixel_height, text);
        const char* cursor = text;
        while (*cursor != '\0') {
            run_apply((size_t)(cursor - text));
            uint32_t cp = decode_utf8(&cursor);
            const glyph_entry_t* glyph = get_glyph(cp, pixel_height);
            if (glyph == NULL) continue;
            if (glyph->top > max_above) max_above = glyph->top;
            int under = (int)glyph->height - glyph->top;
            if (under > max_below) max_below = under;
        }
    }
    if (above != NULL) *above = max_above;
    if (below != NULL) *below = max_below;
}

int ttf_text_width_px(int pixel_height, const char* text) {
    if (!font_ready || text == NULL) return 0;
    return measure_width(clamp_px(pixel_height), text);
}

int ttf_text_left_bearing_px(int pixel_height, const char* text) {
    if (!font_ready || !text || !*text) return 0;
    pixel_height = clamp_px(pixel_height);
    const char *cursor = text;
    uint32_t cp = decode_utf8(&cursor);
    const glyph_entry_t *glyph = cache_lookup(cp, pixel_height);
    if (glyph) return glyph->left;
    int advance, lsb;
    int gid = stbtt_FindGlyphIndex(&font_info, (int)cp);
    stbtt_GetGlyphHMetrics(&font_info, gid, &advance, &lsb);
    return (int)floorf(lsb * stbtt_ScaleForPixelHeight(&font_info, (float)pixel_height));
}

// 线性抗锯齿的中间灰在这块屏上偏亮。按 TTF_COVER_GAMMA 抬覆盖率，半透明边缘更深。
static uint8_t s_cover[256];

/* ---- 绘制 / Draw ---- */
static void ttf_cover_lut_init(void) {
    static bool ready;
    if (ready) return;
    for (int i = 0; i < 256; i++) {
        float t = (float)i / 255.f;
        int v = (int)(255.f * powf(t, TTF_COVER_GAMMA) + 0.5f);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        s_cover[i] = (uint8_t)v;
    }
    ready = true;
}

static uint8_t mix_ink(uint8_t alpha, uint8_t fg, uint8_t bg) {
    return (uint8_t)(bg + s_cover[alpha] * ((int)fg - (int)bg) / 255);
}

void ttf_draw_text(
    uint8_t* framebuffer, int x, int y, int size, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
) {
    ttf_draw_text_px(framebuffer, x, y, size_to_px(size), text, align, fg, bg);
}

void ttf_draw_text_px(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
) {
    if (!font_ready || framebuffer == NULL || text == NULL) return;
    ttf_cover_lut_init();
    pixel_height = clamp_px(pixel_height);
    warm_runs(pixel_height, text);

    int cursor_x = x;
    if (align & EPD_DRAW_ALIGN_CENTER) {
        cursor_x = x - measure_width(pixel_height, text) / 2;
    } else if (align & EPD_DRAW_ALIGN_RIGHT) {
        cursor_x = x - measure_width(pixel_height, text);
    }

    const char* cursor = text;
    while (*cursor != '\0') {
        run_apply((size_t)(cursor - text));
        uint32_t cp = decode_utf8(&cursor);
        const glyph_entry_t* glyph = get_glyph(cp, pixel_height);
        if (glyph == NULL) continue;

        if (glyph->bitmap != NULL) {
            for (int gy = 0; gy < glyph->height; gy++) {
                int yy = y - glyph->top + gy;
                for (int gx = 0; gx < glyph->width; gx++) {
                    uint8_t alpha = glyph->bitmap[gy * glyph->width + gx];
                    if (alpha == 0) continue;
                    epd_draw_pixel(
                        cursor_x + glyph->left + gx, yy, mix_ink(alpha, fg, bg) << 4, framebuffer
                    );
                }
            }
        }
        cursor_x += glyph->advance_x;
    }
}

bool ttf_text_mask_px(uint8_t *mask, unsigned width, unsigned height, int x,
                      int baseline, int pixel_height, const char *text) {
    if (!font_ready || !mask || !text || !width || !height ||
        width > 684u || height > 180u) return false;
    pixel_height = clamp_px(pixel_height);
    ttf_cover_lut_init();
    warm_runs(pixel_height, text);
    const char *cursor = text;
    while (*cursor) {
        const glyph_entry_t *glyph = get_glyph(decode_utf8(&cursor), pixel_height);
        if (!glyph) return false;
        for (int gy = 0; glyph->bitmap && gy < glyph->height; ++gy) {
            int yy = baseline - glyph->top + gy;
            if (yy < 0 || (unsigned)yy >= height) continue;
            for (int gx = 0; gx < glyph->width; ++gx) {
                int xx = x + glyph->left + gx;
                if (xx < 0 || (unsigned)xx >= width) continue;
                uint8_t alpha = s_cover[glyph->bitmap[gy * glyph->width + gx]];
                uint8_t *pixel = &mask[(size_t)yy * width + (unsigned)xx];
                if (alpha > *pixel) *pixel = alpha;
            }
        }
        x += glyph->advance_x;
    }
    return true;
}

void ttf_draw_text_px_spaced(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, uint8_t fg, uint8_t bg
) {
    if (!tracking_px) {
        ttf_draw_text_px(framebuffer, x, y, pixel_height, text, EPD_DRAW_ALIGN_LEFT, fg, bg);
        return;
    }
    if (!font_ready || framebuffer == NULL || text == NULL) return;
    ttf_cover_lut_init();
    pixel_height = clamp_px(pixel_height);
    warm_runs(pixel_height, text);
    int cursor_x = x;
    const char* cursor = text;
    while (*cursor != '\0') {
        run_apply((size_t)(cursor - text));
        uint32_t cp = decode_utf8(&cursor);
        const glyph_entry_t* glyph = get_glyph(cp, pixel_height);
        if (glyph == NULL) continue;
        if (glyph->bitmap != NULL) {
            for (int gy = 0; gy < glyph->height; gy++) {
                int yy = y - glyph->top + gy;
                for (int gx = 0; gx < glyph->width; gx++) {
                    uint8_t alpha = glyph->bitmap[gy * glyph->width + gx];
                    if (alpha == 0) continue;
                    epd_draw_pixel(cursor_x + glyph->left + gx, yy,
                                   mix_ink(alpha, fg, bg) << 4, framebuffer);
                }
            }
        }
        cursor_x += glyph->advance_x;
        if (*cursor) cursor_x += tracking_px;
    }
}

static bool cjk_spacing_point(uint32_t cp) {
    return (cp >= 0x2e80 && cp <= 0xffef) || (cp >= 0x2018 && cp <= 0x201d);
}

void ttf_draw_text_px_fitted(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, int target_width, uint8_t fg, uint8_t bg
) {
    if (!font_ready || !framebuffer || !text || !*text) return;
    ttf_cover_lut_init();
    pixel_height = clamp_px(pixel_height);
    warm_runs(pixel_height, text);
    int glyph_count = 0, cjk_gaps = 0;
    const char* cursor = text;
    uint32_t previous = 0;
    while (*cursor) {
        uint32_t cp = decode_utf8(&cursor);
        if (glyph_count && (cjk_spacing_point(previous) || cjk_spacing_point(cp))) ++cjk_gaps;
        previous = cp;
        ++glyph_count;
    }
    int gaps = glyph_count - 1;
    int natural = measure_width(pixel_height, text) + tracking_px * gaps;
    int delta = target_width - natural;
    bool compress = delta < 0;
    int adjustable = compress ? gaps : cjk_gaps;
    if (adjustable < 2 || (compress && -delta > adjustable * 3) ||
        (!compress && delta > adjustable * 12)) {
        ttf_draw_text_px_spaced(framebuffer, x, y, pixel_height, text, tracking_px, fg, bg);
        return;
    }
    int quotient = delta / adjustable;
    int remainder = delta % adjustable;
    int adjusted = 0, cursor_x = x;
    cursor = text;
    while (*cursor) {
        run_apply((size_t)(cursor - text));
        uint32_t cp = decode_utf8(&cursor);
        const glyph_entry_t* glyph = get_glyph(cp, pixel_height);
        if (glyph && glyph->bitmap) {
            for (int gy = 0; gy < glyph->height; ++gy) {
                int yy = y - glyph->top + gy;
                for (int gx = 0; gx < glyph->width; ++gx) {
                    uint8_t alpha = glyph->bitmap[gy * glyph->width + gx];
                    if (alpha) epd_draw_pixel(cursor_x + glyph->left + gx, yy,
                                              mix_ink(alpha, fg, bg) << 4, framebuffer);
                }
            }
        }
        if (glyph) cursor_x += glyph->advance_x;
        if (*cursor) {
            const char* peek = cursor;
            uint32_t next = decode_utf8(&peek);
            cursor_x += tracking_px;
            if (compress || cjk_spacing_point(cp) || cjk_spacing_point(next)) {
                int step = quotient;
                if (remainder > 0 && adjusted < remainder) ++step;
                if (remainder < 0 && adjusted < -remainder) --step;
                cursor_x += step;
                ++adjusted;
            }
        }
    }
}

void ttf_draw_text_px_bw(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
) {
    if (!font_ready || framebuffer == NULL || text == NULL) return;
    pixel_height = clamp_px(pixel_height);
    warm_runs(pixel_height, text);

    int cursor_x = x;
    if (align & EPD_DRAW_ALIGN_CENTER) {
        cursor_x = x - measure_width(pixel_height, text) / 2;
    } else if (align & EPD_DRAW_ALIGN_RIGHT) {
        cursor_x = x - measure_width(pixel_height, text);
    }

    const char* cursor = text;
    while (*cursor != '\0') {
        run_apply((size_t)(cursor - text));
        uint32_t cp = decode_utf8(&cursor);
        const glyph_entry_t* glyph = get_glyph(cp, pixel_height);
        if (glyph == NULL) continue;

        if (glyph->bitmap != NULL) {
            for (int gy = 0; gy < glyph->height; gy++) {
                int yy = y - glyph->top + gy;
                for (int gx = 0; gx < glyph->width; gx++) {
                    uint8_t alpha = glyph->bitmap[gy * glyph->width + gx];
                    uint8_t ink = alpha >= 128 ? fg : bg;
                    if (ink == bg) continue;
                    epd_draw_pixel(cursor_x + glyph->left + gx, yy, ink << 4, framebuffer);
                }
            }
        }
        cursor_x += glyph->advance_x;
    }
}

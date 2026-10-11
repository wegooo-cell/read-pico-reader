/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借鉴阅读演示的逐码点折行，建立 PSRAM 页表并绘制章节。
 * English: Adapt the reading demo's codepoint wrapping into PSRAM chapter pagination.
 *
 * 冻结：原文由调用方持有；字体测量和绘制必须由调用方串行化。
 * Frozen: Caller owns source text and serializes all font measurement and drawing.
 */
#include "book_layout.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "ttf_font.h"

#define PAGE_MAX 4096u
#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static const char* s_text;
static size_t s_len;
static size_t* s_pages;
static size_t s_count;
static size_t s_capacity;
static char* s_line;
static EpdRect s_rect;
static int s_px;
static unsigned s_line_percent = 150, s_paragraph_percent = 50;
static int s_tracking_px;
static unsigned s_first_line_indent_em = 2;
static int s_first_line_indent_adjust_px;
static unsigned s_reading_line;
static int s_reading_line_offset;
static bool s_images_visible = true;
static size_t s_lead_skip;
static unsigned s_lead_height;

static const blk_t* s_blocks;
static size_t s_block_count;
// 布局代次：重排或释放后自增，页内位图缓存靠它失效。/ Bumped on rebuild or free so page bitmap caches expire.
static uint32_t s_generation;

// 页内插图：图片序号、页内 y、显示宽高。平坦池按页顺序存放，每页用起点表切片，
// 这样大多数没有插图的页只花两个下标。
// Images placed on a page: index, y within the page, and display width/height. The pool is flat
// and ordered by page, sliced by a per-page start table, so a page without images costs two
// indices and nothing else.
typedef struct {
    int16_t image;
    int16_t y;
    int16_t width;
    int16_t height;
} layout_image_t;

static layout_image_t* s_images;
static size_t s_image_count;
static size_t s_image_capacity;
static uint32_t* s_page_img_start;

static book_layout_image_dims_fn s_dims_fn;
static void* s_dims_ctx;

// 插图可用区域。通栏开启时宽度换成整屏，竖直方向仍是正文那一条，分页数学不动。
// Region illustrations may occupy. With full bleed on, its width becomes the panel's and its
// vertical band stays the body's, leaving the pagination math alone.
static EpdRect s_image_rect;
// 通栏宽度（整屏像素）。面板常量，重排不必跟着改；0 关闭。
// Full-bleed width in panel pixels. A panel constant, so re-layouts need not update it; 0 disables.
static int s_image_bleed_width;

// 块表是有序字节区间，二分查找当前行样式。/ Blocks are ordered byte ranges; binary-search the line style.
static const blk_t* block_at(size_t off) {
    if (!s_block_count) return NULL;
    size_t lo = 0, hi = s_block_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_blocks[mid].offset <= off) lo = mid;
        else hi = mid;
    }
    return &s_blocks[lo];
}

// 字体 run 表由调用方借用（和块表同一份 html_text），绘制时按段换字体。
// The run table is borrowed from the caller's html_text, next to the block table, so drawing
// can switch faces span by span.
static const html_run_t* s_runs;
static size_t s_run_count;

// 一行最多切几段字体。屏上一行十几个字，段数只由内联标签决定。
// How many face spans one line may carry: a line holds a dozen glyphs and spans only come
// from inline tags.
#define LINE_RUN_MAX 64u
static ttf_run_t s_line_runs[LINE_RUN_MAX];
static size_t s_line_run_count;

void book_layout_set_runs(const html_run_t* runs, size_t count) {
    s_runs = (runs != NULL && count > 0) ? runs : NULL;
    s_run_count = s_runs != NULL ? count : 0;
}

// 该字节属于哪个字体槽。没有 run 表就一律系统字体，排版与从前完全一致。
// Which face owns this byte. Without a run table everything is the system face and the
// layout behaves exactly as before.
static uint8_t slot_at(size_t off) {
    if (s_runs == NULL) return TTF_FONT_SLOT_SYSTEM;
    const blk_t* block = block_at(off);
    if (block == NULL || block->run_count == 0 || off < block->offset) return TTF_FONT_SLOT_SYSTEM;
    size_t rel = off - block->offset;
    size_t last = block->run_first + block->run_count;
    if (last > s_run_count) last = s_run_count;
    uint8_t slot = TTF_FONT_SLOT_SYSTEM;
    for (size_t i = block->run_first; i < last; ++i) {
        if (s_runs[i].offset > rel) break;
        slot = s_runs[i].slot;
    }
    return slot;
}

// 装载失败或没装载的槽一律退回系统字体：拿一张空字体去量宽会得到零。
// A slot that failed to load, or never loaded, falls back to the system face; measuring an
// empty face would return zero.
static uint8_t slot_ready_or_system(uint8_t slot) {
    if (slot != TTF_FONT_SLOT_SYSTEM && !ttf_font_slot_ready(slot)) return TTF_FONT_SLOT_SYSTEM;
    return slot;
}

static void slot_select_for(size_t off) {
    uint8_t slot = slot_ready_or_system(slot_at(off));
    if (slot != (uint8_t)ttf_font_selected()) ttf_font_select(slot);
}

static void line_run_push(size_t offset, uint8_t slot) {
    if (offset > UINT32_MAX) return;
    if (s_line_run_count) {
        ttf_run_t* prev = &s_line_runs[s_line_run_count - 1];
        if (prev->slot == slot) return;
        if (prev->offset == (uint32_t)offset) {
            prev->slot = slot;
            return;
        }
    }
    if (s_line_run_count >= LINE_RUN_MAX) return;
    s_line_runs[s_line_run_count].offset = (uint32_t)offset;
    s_line_runs[s_line_run_count].slot = slot;
    ++s_line_run_count;
}

// 把块内 run 平移到 s_line 的坐标系：s_line 从 visible_off 起，只覆盖这一行。
// Shift the block's runs into s_line coordinates; s_line starts at visible_off and covers
// this line only.
static void line_runs_build(const blk_t* block, size_t visible_off, size_t line_end) {
    s_line_run_count = 0;
    if (s_runs == NULL || block == NULL || block->run_count == 0) return;
    size_t block_end = block->offset + block->len;
    if (visible_off < block->offset || line_end > block_end || line_end <= visible_off) return;
    size_t base = visible_off - block->offset, stop = line_end - block->offset;
    size_t last = block->run_first + block->run_count;
    if (last > s_run_count) last = s_run_count;
    for (size_t i = block->run_first; i < last; ++i) {
        size_t from = s_runs[i].offset, to = from + s_runs[i].len;
        if (to <= base) continue;
        if (from >= stop) break;
        line_run_push(from > base ? from - base : 0, slot_ready_or_system(s_runs[i].slot));
    }
}

// 拒绝截断、过长编码、代理项和嵌入零字节。/ Reject truncation, overlong encodings, surrogates and embedded NUL.
static size_t codepoint_size(const char* text, size_t remaining) {    if (!remaining) return 0;
    const unsigned char* p = (const unsigned char*)text;
    if (p[0] > 0 && p[0] < 0x80) return 1;
    size_t n = p[0] >= 0xc2 && p[0] <= 0xdf ? 2 :
               p[0] >= 0xe0 && p[0] <= 0xef ? 3 :
               p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
    if (!n || n > remaining) return 0;
    for (size_t i = 1; i < n; i++) if ((p[i] & 0xc0) != 0x80) return 0;
    if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
        (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
    return n;
}

static uint32_t codepoint_value(const char* text, size_t n) {
    const unsigned char* p = (const unsigned char*)text;
    if (n == 1) return p[0];
    uint32_t cp = p[0] & (0x7f >> n);
    for (size_t i = 1; i < n; ++i) cp = (cp << 6) | (p[i] & 63);
    return cp;
}

// 插图容器常带全角空白、零宽字符和换行实体；它们不是可阅读的独立段落。
// Image wrappers often contain fullwidth spaces, zero-width characters and line-break entities, not readable paragraphs.
static bool image_spacing_block(const blk_t* block) {
    if (!block || block->image >= 0) return false;
    size_t end = block->offset + block->len;
    for (size_t at = block->offset; at < end;) {
        size_t n = codepoint_size(s_text + at, end - at);
        if (!n) return false;
        uint32_t cp = codepoint_value(s_text + at, n);
        bool space = cp == ' ' || (cp >= '\t' && cp <= '\r') || cp == 0x85 || cp == 0xa0 ||
                     cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200b) || cp == 0x2028 ||
                     cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x2060 ||
                     cp == 0x3000 || cp == 0xfeff;
        if (!space) return false;
        at += n;
    }
    return true;
}

// 只跳过插图相邻的空白块，保留原文字节位置、正文段落与 TXT 的手动空行。
// Skip only image-adjacent spacer blocks; keep source offsets, text paragraphs and manual TXT blank lines.
static size_t skip_image_spacing(size_t off) {
    while (off < s_len && s_block_count) {
        const blk_t* block = block_at(off);
        if (block->offset != off) break;
        size_t end = block->offset + block->len;
        if (block->image >= 0) {
            if (s_images_visible) break;
        } else {
            if (!image_spacing_block(block)) break;
            size_t first = (size_t)(block - s_blocks), last = first + 1;
            while (last < s_block_count && image_spacing_block(&s_blocks[last])) ++last;
            bool adjacent = (first > 0 && s_blocks[first - 1].image >= 0) ||
                            (last < s_block_count && s_blocks[last].image >= 0);
            if (!adjacent) break;
            end = s_blocks[last - 1].offset + s_blocks[last - 1].len;
        }
        off = end;
        if (off < s_len && s_text[off] == '\n') ++off;
    }
    return off;
}

// 中文排版禁则：右标点不得出现在行首，左标点不得停在行尾。
// CJK kinsoku: closing punctuation may not start a line; opening punctuation may not end one.
static bool prohibited_line_start(uint32_t cp) {
    switch (cp) {
        case 0x0021: case 0x0025: case 0x0029: case 0x002c: case 0x002e: case 0x003a:
        case 0x003b: case 0x003f: case 0x005d: case 0x007d:
        case 0x2019: case 0x201d: case 0x2026: case 0x3001: case 0x3002: case 0x3009:
        case 0x300b: case 0x300d: case 0x300f: case 0x3011: case 0x3015: case 0x3017:
        case 0x3019: case 0x301b: case 0xff01: case 0xff05: case 0xff09: case 0xff0c:
        case 0xff0e: case 0xff1a: case 0xff1b: case 0xff1f: case 0xff3d: case 0xff5d:
            return true;
        default: return false;
    }
}

static bool prohibited_line_end(uint32_t cp) {
    switch (cp) {
        case 0x0028: case 0x005b: case 0x007b: case 0x2018: case 0x201c: case 0x3008:
        case 0x300a: case 0x300c: case 0x300e: case 0x3010: case 0x3014: case 0x3016:
        case 0x3018: case 0x301a: case 0xff08: case 0xff3b: case 0xff5b:
            return true;
        default: return false;
    }
}

uint32_t book_layout_generation(void) { return s_generation; }

void book_layout_free(void) {
    ++s_generation;
    free(s_pages);
    free(s_line);
    free(s_images);
    free(s_page_img_start);
    s_pages = NULL;
    s_line = NULL;
    s_images = NULL;
    s_page_img_start = NULL;
    s_image_count = s_image_capacity = 0;
    s_text = NULL;
    s_count = s_capacity = s_len = 0;
    s_px = 0;
    s_blocks = NULL;
    s_block_count = 0;
    s_image_rect = (EpdRect){0, 0, 0, 0};
}

void book_layout_set_spacing(unsigned line_percent, unsigned paragraph_percent) {
    s_line_percent = line_percent >= 110 && line_percent <= 200 ? line_percent : 150;
    s_paragraph_percent = paragraph_percent <= 100 ? paragraph_percent : 50;
}
void book_layout_set_typography(int tracking_px) {
    s_tracking_px = tracking_px >= -4 && tracking_px <= 4 && tracking_px % 2 == 0 ? tracking_px : 0;
}
void book_layout_set_first_line_indent(unsigned em) {
    s_first_line_indent_em = em <= 3 ? em : 2;
}
void book_layout_set_first_line_indent_adjust(int px) {
    s_first_line_indent_adjust_px = px >= -20 && px <= 20 ? px : 0;
}
EpdRect book_layout_balanced_rect(EpdRect outer, int px, int tracking_px) {
    if (px <= 0 || outer.width < px || tracking_px < -4 || tracking_px > 4) return outer;
    int step = px + tracking_px;
    if (step <= 0) return outer;
    int columns = 1 + (outer.width - px) / step;
    int used = px + (columns - 1) * step;
    outer.x += (outer.width - used) / 2;
    outer.width = used;
    return outer;
}
void book_layout_set_reading_line(unsigned style) {
    s_reading_line = style <= 2 ? style : 0;
}
void book_layout_set_reading_line_offset(int offset_px) {
    s_reading_line_offset = offset_px < -8 ? -8 : offset_px > 8 ? 8 : offset_px;
}
void book_layout_set_images_visible(bool visible) { s_images_visible = visible; }
void book_layout_set_chapter_lead(size_t skip_bytes, unsigned height_px) {
    s_lead_skip = skip_bytes;
    s_lead_height = height_px;
}

void book_layout_set_image_dims(book_layout_image_dims_fn fn, void* ctx) {
    s_dims_fn = fn;
    s_dims_ctx = ctx;
}

void book_layout_set_image_bleed_width(int screen_width) {
    s_image_bleed_width = screen_width > 0 ? screen_width : 0;
}

EpdRect book_layout_image_rect(void) {
    return s_image_rect.width > 0 ? s_image_rect : s_rect;
}

// 每次重排由正文栏推出插图区域：宽度换成整屏并与正文栏同心，其余保持原样。
// Derive the illustration region from the body column on every re-layout: the width becomes the
// panel's, concentric with the column, and everything else stays put.
static void resolve_image_rect(void) {
    s_image_rect = s_rect;
    if (s_image_bleed_width > s_rect.width) {
        s_image_rect.x = s_rect.x + (s_rect.width - s_image_bleed_width) / 2;
        s_image_rect.width = s_image_bleed_width;
    }
}

// 按插图区域等比缩放、不超过区域高、不放大——与参考实现同一套规则。
// Aspect-fit to the illustration region, never taller than it, never upscaled, matching the
// reference implementations.
static bool image_display_size(int image, int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!s_dims_fn) return false;
    int w = 0, h = 0;
    if (!s_dims_fn(s_dims_ctx, image, &w, &h) || w <= 0 || h <= 0) return false;
    int64_t cw = s_image_rect.width, ch = s_image_rect.height;
    if (cw <= 0 || ch <= 0) return false;
    if (w > cw) { h = (int)((int64_t)h * cw / w); w = (int)cw; }
    if (h > ch) { w = (int)((int64_t)w * ch / h); h = (int)ch; }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return true;
}

static bool record_page_image(int image, int y, int w, int h) {
    if (s_image_count == s_image_capacity) {
        size_t cap = s_image_capacity ? s_image_capacity * 2 : 16;
        if (cap > PAGE_MAX * 4) return false;
        layout_image_t* grown = heap_caps_realloc(s_images, cap * sizeof(*grown), PSRAM_CAPS);
        if (!grown) return false;
        s_images = grown;
        s_image_capacity = cap;
    }
    layout_image_t* rec = &s_images[s_image_count++];
    rec->image = (int16_t)image;
    rec->y = (int16_t)y;
    rec->width = (int16_t)w;
    rec->height = (int16_t)h;
    if (s_page_img_start && s_count) s_page_img_start[s_count] = (uint32_t)s_image_count;
    return true;
}

int book_layout_page_image_count(size_t page) {
    if (page >= s_count || !s_page_img_start) return 0;
    return (int)(s_page_img_start[page + 1] - s_page_img_start[page]);
}

bool book_layout_page_image_at(size_t page, int i, int* image, int* y, int* width, int* height) {
    if (page >= s_count || !s_page_img_start || i < 0) return false;
    uint32_t a = s_page_img_start[page];
    if ((uint32_t)i >= s_page_img_start[page + 1] - a) return false;
    const layout_image_t* rec = &s_images[a + (uint32_t)i];
    if (image) *image = rec->image;
    if (y) *y = rec->y;
    if (width) *width = rec->width;
    if (height) *height = rec->height;
    return true;
}

int book_layout_page_image(size_t page) {
    if (page >= s_count || !s_block_count) return -1;
    const blk_t* block = block_at(s_pages[page]);
    if (!block || block->offset != s_pages[page] || block->image < 0) return -1;
    // 图片流进正文之后，插图常常正好落在页首，只判断"页首是图片块"会把普通文字页也算成整页插图。
    // 这里要求这一页的范围【只覆盖这一个图片块】，才是真正需要强刷的整页灰阶图。
    // Once images flow into the page an inline image often sits at the top, so testing only the
    // page start also matched ordinary text pages. Require the page to span exactly this image
    // block: that is a whole-page gray bitmap, the case the forced refresh exists for.
    size_t after = block->offset + block->len;
    if (after < s_len && s_text[after] == '\n') ++after;
    size_t page_end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    return page_end == after ? block->image : -1;
}

static int line_height_for(int px) { return (int)((unsigned)px * s_line_percent / 100); }
static int gap_for(int height, bool heading) {
    return (int)((unsigned)height * s_paragraph_percent / (heading ? 150 : 100));
}

static bool append_page(size_t off) {
    if (s_count == PAGE_MAX) return false;
    if (s_count == s_capacity) {
        size_t cap = s_capacity ? s_capacity * 2 : 16;
        size_t* pages = heap_caps_realloc(s_pages, cap * sizeof(*pages), PSRAM_CAPS);
        if (!pages) return false;
        s_pages = pages;
        // 起点表要多一项：第 page 页的插图区间是 [start[page], start[page+1])。
        // The start table needs one extra slot: page `page` owns [start[page], start[page+1]).
        uint32_t* starts = heap_caps_realloc(s_page_img_start, (cap + 1) * sizeof(*starts), PSRAM_CAPS);
        if (!starts) return false;
        s_page_img_start = starts;
        s_capacity = cap;
    }
    s_pages[s_count] = off;
    s_page_img_start[s_count] = (uint32_t)s_image_count;
    ++s_count;
    s_page_img_start[s_count] = (uint32_t)s_image_count;
    return true;
}

// 折行时保留原文字节位置；CRLF 算一个段落边界。/ Preserve source offsets while wrapping; CRLF is one paragraph boundary.
static bool take_line(size_t off, size_t* next, bool* paragraph_end, int* px, bool* heading,
                      int* line_width, int* indent, uint8_t* align,
                      int* margin_before, int* margin_after) {
    const blk_t* block = block_at(off);
    *heading = block && block->heading;
    *px = s_px + (*heading ? 8 : 0);
    bool first_line = block ? off == block->offset :
        off == 0 || s_text[off - 1] == '\n' || s_text[off - 1] == '\r';
    *align = block ? block->align : 0;
    // 首行缩进由阅读设置统一控制；书内标题和对齐块仍保持原本的位置。
    // The reader setting controls paragraph indent; headings and aligned blocks keep their placement.
    *indent = 0;
    if (first_line && !*heading && !*align) {
        // 缩进按当前字体真实全角字宽和字间距计算，不把行高当字宽。
        // Indent by actual full-width advances plus tracking, not by the font's line height.
        slot_select_for(off);
        int advance = ttf_text_width_px(*px, "　");
        if (advance <= 0) advance = ttf_text_width_px(*px, "一");
        if (advance <= 0) advance = *px;
        advance += s_tracking_px;
        if (advance < 1) advance = 1;
        *indent = advance * (int)s_first_line_indent_em;
        if (s_first_line_indent_em) *indent += s_first_line_indent_adjust_px;
        if (*indent < 0) *indent = 0;
    }
    if (*indent >= s_rect.width) *indent = s_rect.width > 1 ? s_rect.width - 1 : 0;
    if (*indent + *px > s_rect.width) *indent = 0;
    *margin_before = first_line && block ? (int)((unsigned)*px * block->margin_before_percent / 100) : 0;
    *margin_after = block ? (int)((unsigned)*px * block->margin_after_percent / 100) : 0;
    size_t limit = block ? block->offset + block->len : s_len;
    // 纸书常把全角空格写进段首；统一由阅读设置决定缩进，原文偏移仍保留。
    // Printed-book source often contains leading spaces; the reader setting owns the visual indent.
    size_t visible_off = off;
    if (first_line && !*heading && !*align) {
        while (visible_off < limit) {
            size_t n = codepoint_size(s_text + visible_off, limit - visible_off);
            if (!n) return false;
            uint32_t cp = codepoint_value(s_text + visible_off, n);
            if (cp == ' ' || cp == '\t' || cp == 0xa0 || cp == 0x1680 ||
                (cp >= 0x2000 && cp <= 0x200b) || cp == 0x202f || cp == 0x205f ||
                cp == 0x2060 || cp == 0x3000 || cp == 0xfeff) {
                visible_off += n; continue;
            }
            break;
        }
        if (visible_off < limit && *indent > 0) {
            size_t n = codepoint_size(s_text + visible_off, limit - visible_off);
            if (!n) return false;
            uint32_t cp = codepoint_value(s_text + visible_off, n);
            if (prohibited_line_end(cp)) {
                // 段首开标点自身常有半字留白，向缩进区悬挂，保持可见左边缘对齐。
                // Hang an opener's own leading whitespace into the indent, keeping its ink edge aligned.
                char glyph[5]; memcpy(glyph, s_text + visible_off, n); glyph[n] = 0;
                slot_select_for(visible_off);
                int bearing = ttf_text_left_bearing_px(*px, glyph);
                if (bearing > *indent) bearing = *indent;
                if (bearing > 0) *indent -= bearing;
            }
        }
    }
    int available = s_rect.width - *indent;
    size_t end = visible_off;
    size_t last_start = visible_off;
    uint32_t last_cp = 0;
    int64_t width = 0;
    int64_t last_width = 0;
    int glyph_count = 0;
    s_line[0] = 0;
    *paragraph_end = false;
    while (end < limit && s_text[end] != '\r' && s_text[end] != '\n') {
        size_t n = codepoint_size(s_text + end, s_len - end);
        if (!n) return false;
        uint32_t cp = codepoint_value(s_text + end, n);
        // 字体逐字取整后累加 advance；单字测量避免反复扫描整行前缀。
        // Font advances are rounded per glyph and summed; measure each glyph once instead of every prefix.
        char glyph[5];
        memcpy(glyph, s_text + end, n);
        glyph[n] = 0;
        // 每个字都按自己那一段的字体量宽：行宽、字距和对齐算法一个字没改。
        // Every glyph is measured in its own span's face; width, tracking and alignment math
        // are untouched.
        slot_select_for(end);
        int64_t candidate = width + ttf_text_width_px(*px, glyph) +
                            (end > visible_off && !*heading ? s_tracking_px : 0);
        if (candidate < 0) return false;
        if (candidate > available) {
            if (end == visible_off) return false;
            // 优先让行尾正文对齐，再允许右标点小幅悬挂到留白；超出安全宽度才回退前一字。
            // Align the text edge first, hang a closing mark slightly into the margin, then roll back only if needed.
            if (prohibited_line_start(cp)) {
                int hang = s_rect.x > 4 ? *px / 3 : 0;
                if (hang > s_rect.x - 4) hang = s_rect.x - 4;
                int squeeze = glyph_count > 1 ? (glyph_count - 1) * 3 : 0;
                if (candidate <= (int64_t)available + hang + squeeze) {
                    memcpy(s_line + end - visible_off, glyph, n);
                    s_line[end - visible_off + n] = 0;
                    width = candidate;
                    end += n;
                    break;
                }
                if (last_start > visible_off) {
                    end = last_start;
                    s_line[end - visible_off] = 0;
                    width = last_width;
                    break;
                }
                // 极窄行容不下两个字时才保留悬挂，避免出现以标点开头的死循环。
                // Only a one-glyph-wide line may hang punctuation to avoid a non-progressing wrap.
                if (candidate <= (int64_t)available + *px * 2) {
                    memcpy(s_line + end - visible_off, glyph, n);
                    s_line[end - visible_off + n] = 0;
                    width = candidate;
                    end += n;
                    break;
                }
            }
            // 若最后一个字是左括号，将它回退到下一行；极窄行则让括号和首字成组悬挂。
            // Move a trailing opener to the next line; on a one-glyph line keep the pair together.
            if (prohibited_line_end(last_cp)) {
                if (last_start > visible_off) {
                    end = last_start;
                    s_line[end - visible_off] = 0;
                    width = last_width;
                } else if (candidate <= (int64_t)available + *px * 2) {
                    memcpy(s_line + end - visible_off, glyph, n);
                    s_line[end - visible_off + n] = 0;
                    width = candidate;
                    end += n;
                }
            }
            break;
        }
        last_width = width;
        width = candidate;
        ++glyph_count;
        last_start = end;
        last_cp = cp;
        memcpy(s_line + end - visible_off, glyph, n);
        s_line[end - visible_off + n] = 0;
        end += n;
    }
    line_runs_build(block, visible_off, end);
    *line_width = (int)width;
    *next = end;
    if (end < s_len && (s_text[end] == '\r' || s_text[end] == '\n')) {
        *next = end + 1;
        if (s_text[end] == '\r' && *next < s_len && s_text[*next] == '\n') (*next)++;
        *paragraph_end = true;
    }
    return *next > off;
}

bool book_layout_build(const char* utf8, size_t len, EpdRect rect, int px) {
    return book_layout_build_blocks(utf8, len, NULL, 0, rect, px);
}

bool book_layout_build_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    book_layout_free();
    if ((!utf8 && len) || len == SIZE_MAX || s_lead_skip > len || s_lead_height >= (unsigned)rect.height ||
        px <= 0 || px > INT_MAX / 3 ||
        rect.width <= 0 || rect.height <= 0 || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return false;
    int line_height = line_height_for(px);
    if (line_height > rect.height) return false;
    for (size_t off = 0; off < len;) {
        size_t n = codepoint_size(utf8 + off, len - off);
        if (!n) return false;
        off += n;
    }
    if (count) {
        if (!blocks || count > HTML_TEXT_MAX_BLOCKS) return false;
        size_t expected = 0;
        for (size_t i = 0; i < count; ++i) {
            const blk_t* b = &blocks[i];
            if (b->offset != expected || b->offset >= len || !b->len || b->len > len - b->offset ||
                ((unsigned char)utf8[b->offset] & 0xc0) == 0x80) return false;
            size_t end = b->offset + b->len;
            if (i + 1 < count) {
                if (end >= len || utf8[end] != '\n') return false;
                expected = end + 1;
            } else if (end != len) return false;
        }
    }
    s_blocks = blocks;
    s_block_count = count;
    s_text = utf8;
    s_len = len;
    s_px = px;
    s_rect = rect;
    resolve_image_rect();
    s_line = heap_caps_malloc(len + 1, PSRAM_CAPS);
    size_t off = s_lead_skip;
    if (!s_line) goto fail;
    size_t visible_off = skip_image_spacing(off);
    if (visible_off < len) off = visible_off;
    else if (!s_images_visible) {
        // 纯插图章节仍保留可检测的插图页，让跨章跳过逻辑继续前进。
        // Image-only chapters keep a detectable image page for the chapter-skip path.
        for (size_t i = 0; i < count; ++i)
            if (blocks[i].image >= 0 && blocks[i].offset >= off) { off = blocks[i].offset; break; }
    }
    if (!append_page(off)) goto fail;
    int64_t used = s_lead_height;
    while (off < len) {
        size_t visible = skip_image_spacing(off);
        if (visible != off) { off = visible; continue; }
        const blk_t* block = block_at(off);
        if (block && block->image >= 0 && off == block->offset) {
            int img_w = 0, img_h = 0;
            if (image_display_size(block->image, &img_w, &img_h)) {
                // 图是页面流里的一个块：放得下就留在本页，文字接着图下面排；放不下才翻页。
                // An image is one block in the page flow: keep it on this page when it fits and
                // let the text continue below it; only start a new page when it does not fit.
                if (used && used + img_h > s_image_rect.height) {
                    if (!append_page(off)) goto fail;
                    used = 0;
                }
                if (!record_page_image(block->image, (int)used, img_w, img_h)) goto fail;
                used += img_h + px / 2;  // 图下留半个行高 / half a line under the image
            } else {
                // 取不到尺寸时退回整页显示，保证纯插图章节仍可被跨章跳过逻辑识别。
                // Without dimensions, fall back to a whole-page image so an image-only chapter
                // stays detectable by the chapter-skip path.
                if (used && !append_page(off)) goto fail;
                used = rect.height;
            }
            off = block->offset + block->len;
            if (off < len && utf8[off] == '\n') ++off;
            continue;
        }
        size_t next;
        bool paragraph_end, heading;
        int line_px, line_width, indent, margin_before, margin_after;
        uint8_t align;
        if (!take_line(off, &next, &paragraph_end, &line_px, &heading,
                       &line_width, &indent, &align, &margin_before, &margin_after)) goto fail;
        (void)line_width; (void)indent; (void)align;
        line_height = line_height_for(line_px);
        if (line_height > rect.height) goto fail;
        int leading = used ? margin_before : 0;
        if (used + leading + line_height > rect.height) {
            if (!append_page(off)) goto fail;
            used = 0;
            leading = 0;
        }
        used += leading + line_height;
        if (paragraph_end) used += gap_for(line_height, heading) + margin_after;
        off = next;
    }
    // 测量会按 run 移动游标；还回去，后面的界面与状态栏仍从系统字体起。
    // Measuring moves the cursor across spans; hand it back so the UI and status bar still
    // start from the system face.
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    return true;
fail:
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    book_layout_free();
    return false;
}

size_t book_layout_page_count(void) { return s_count; }

size_t book_layout_page_start_offset(size_t page) {
    return page < s_count ? s_pages[page] : s_len;
}

size_t book_layout_page_for_offset(size_t off) {
    size_t lo = 0, hi = s_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_pages[mid] <= off) lo = mid;
        else hi = mid;
    }
    return lo;
}

void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px) {
    if (!fb || page >= s_count || px != s_px || rect.width != s_rect.width ||
        rect.height != s_rect.height || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return;
    // 走回退路径的"整页插图"没有图片记录：这里没有文字可画，那张图由调用方绘制，直接返回。
    // 有记录时说明图片已经流进正文，文字和图一起排。
    // A whole-page image from the fallback path has no record: there is no text to draw here and
    // the caller draws that image, so return. With records the image flowed into the page and the
    // text lays out alongside it.
    if (book_layout_page_image(page) >= 0 && book_layout_page_image_count(page) == 0) return;
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = page == 0 ? s_lead_height : 0;
    // 本页插图按出现顺序排在池里，游标走到哪个块就用哪条记录，不必再查表。
    // This page's images sit in the pool in encounter order, so the walk just consumes them in
    // turn instead of looking each one up.
    uint32_t img_at = s_page_img_start ? s_page_img_start[page] : 0;
    const uint32_t img_end = s_page_img_start ? s_page_img_start[page + 1] : 0;
    while (off < end) {
        size_t visible = skip_image_spacing(off);
        if (visible != off) { off = visible; continue; }
        const blk_t* image_block = block_at(off);
        if (image_block && image_block->image >= 0 && off == image_block->offset) {
            while (img_at < img_end && s_images[img_at].image != image_block->image) ++img_at;
            if (img_at < img_end) {
                const layout_image_t* rec = &s_images[img_at++];
                used = (int64_t)rec->y + rec->height + s_px / 2;
            }
            off = image_block->offset + image_block->len;
            if (off < end && s_text[off] == '\n') ++off;
            continue;
        }
        size_t next;
        bool paragraph_end, heading;
        int line_px, line_width, indent, margin_before, margin_after;
        uint8_t align;
        if (!take_line(off, &next, &paragraph_end, &line_px, &heading,
                       &line_width, &indent, &align, &margin_before, &margin_after)) return;
        int line_height = line_height_for(line_px);
        int leading = used ? margin_before : 0;
        if (used + leading + line_height > rect.height) return;
        used += leading;
        if (s_line[0]) {
            if (s_reading_line) {
                int guide_y = rect.y + (int)used + line_px + (line_height - line_px) / 2 +
                              s_reading_line_offset;
                int dash = s_reading_line == 1 ? 19 : 2;
                int period = s_reading_line == 1 ? 31 : 13;
                for (int dx = 0; dx < rect.width; dx += period) {
                    int width = dx + dash <= rect.width ? dash : rect.width - dx;
                    epd_fill_rect((EpdRect){rect.x + dx, guide_y, width, 2}, 0x50, fb);
                }
            }
            int x = rect.x + indent;
            int available = rect.width - indent;
            if (align == 1) x += (available - line_width) / 2;
            else if (align == 2) x += available - line_width;
            // 行基线整行共用，取行首那一段的字体，免得同一行上下浮动。
            // The baseline is shared by the whole line, so take it from the span the line
            // starts in and keep the line from floating.
            slot_select_for(off);
            int baseline = rect.y + (int)used + ttf_ascender_px(line_px);
            const blk_t *line_block = block_at(off);
            bool final_line = paragraph_end || next >= s_len ||
                (line_block && next >= line_block->offset + line_block->len);
            // 绘制按 run 换字体：行宽、字距、两端对齐的算法一个字没动，只有取字形的槽跟着走。
            // Drawing follows the runs: width, tracking and justification math are untouched,
            // only the slot each glyph comes from changes.
            ttf_draw_set_runs(s_line_run_count ? s_line_runs : NULL, s_line_run_count);
            if (!heading && !align && (!final_line || line_width > available)) {
                // 完整正文行对齐到统一右边界；悬挂标点最多使用少量右侧留白。
                // Justify complete body lines; a hanging closer may use a small part of the right margin.
                int hang = s_rect.x > 4 ? line_px / 3 : 0;
                if (hang > s_rect.x - 4) hang = s_rect.x - 4;
                int target = line_width > available ? available + hang : available;
                ttf_draw_text_px_fitted(fb, x, baseline, line_px, s_line, s_tracking_px,
                                        target, 0, 15);
            } else if (s_tracking_px && !heading)
                ttf_draw_text_px_spaced(fb, x, baseline, line_px, s_line, s_tracking_px, 0, 15);
            else
                ttf_draw_text_px(fb, x, baseline, line_px, s_line, EPD_DRAW_ALIGN_LEFT, 0, 15);
            // 画完把游标还回去：页面其它文字、界面和状态栏都按系统字体走。
            // Hand the cursor back: every other page, the UI and the status bar use the system
            // face.
            ttf_draw_set_runs(NULL, 0);
            ttf_font_select(TTF_FONT_SLOT_SYSTEM);
        }
        used += line_height;
        if (paragraph_end) used += gap_for(line_height, heading) + margin_after;
        off = next;
    }
}

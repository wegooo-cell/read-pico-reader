/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：建立 PSRAM 页表并绘制章节，横排优先保留英文整词，中文沿用禁则折行。
 * English: Paginate and draw chapters in PSRAM, preferring whole English words horizontally while retaining CJK kinsoku wrapping.
 *
 * 冻结：原文由调用方持有；字体测量和绘制必须由调用方串行化。
 * Frozen: Caller owns source text and serializes all font measurement and drawing.
 */
#include "book_layout.h"
#include "english_hyphen.h"
#include "../font/text_glue.h"
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
static book_layout_draw_cursor_t *s_idle_draw;
static char* s_line;
static size_t s_line_capacity;
static EpdRect s_rect;
static int s_px;
static unsigned s_line_percent = 150, s_paragraph_percent = 50;
static int s_tracking_px;
static unsigned s_first_line_indent_em = 2;
static int s_first_line_indent_adjust_px;
static unsigned s_reading_line;
static int s_reading_line_offset;
static bool s_images_visible = true;
// 竖排：同一套折行与禁则逻辑，只是推进方向朝下、堆叠方向朝左。
// Vertical setting: the same wrapping and kinsoku logic, with the advance running down and the
// stack running leftward.
static bool s_vertical;
// 竖排首页右起为竖排题头预留的列宽，调用方按同一宽度绘制题头。
// Columns reserved from the right of the first vertical page for a vertical chapter heading; the
// caller draws the heading into the same width.
static unsigned s_chapter_lead_width;
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
    int16_t x; ///< 行内图标为行末 x；-1 表示块级图居中 / Inline icon row-end x; -1 centres block images
} layout_image_t;

static layout_image_t* s_images;
static size_t s_image_count;
static size_t s_image_capacity;
static uint32_t* s_page_img_start;
// 上一行文字的结束 x 与该行顶部，用于把行内注号图标贴到字后。
// End x and top of the previous text line, used to attach inline footnote icons to the text.
static int s_last_line_x_end, s_last_line_used;

// 行内注号图标：正文占位符 U+FFFC 对应的注表，排版把图标宽度算进行宽、记录精确 x。
// Inline footnote icons: the note table behind U+FFFC placeholders, so layout can charge the
// icon width to the line and record its exact x.
static const html_note_t* s_inline_notes;
static size_t s_inline_note_count;
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
    s_line = NULL; s_line_capacity = 0;
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
    s_tracking_px = tracking_px >= -15 && tracking_px <= 15 ? tracking_px : 0;
}
void book_layout_set_first_line_indent(unsigned em) {
    s_first_line_indent_em = em <= 3 ? em : 2;
}
void book_layout_set_first_line_indent_adjust(int px) {
    s_first_line_indent_adjust_px = px >= -100 && px <= 100 ? px : 0;
}
EpdRect book_layout_balanced_rect(EpdRect outer, int px, int tracking_px) {
    if (px <= 0 || outer.width < px || tracking_px < -15 || tracking_px > 15) return outer;
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
void book_layout_set_vertical(bool vertical) { s_vertical = vertical; }
bool book_layout_vertical(void) { return s_vertical; }
void book_layout_set_chapter_lead_width(unsigned width_px) { s_chapter_lead_width = width_px; }
void book_layout_set_chapter_lead(size_t skip_bytes, unsigned height_px) {
    s_lead_skip = skip_bytes;
    s_lead_height = height_px;
}

void book_layout_set_inline_images(const html_note_t* notes, size_t count) {
    s_inline_notes = notes;
    s_inline_note_count = count;
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

// 按正文偏移找行内注号图标的大小；不是注号或取不到尺寸时返回 false。
// Look up an inline footnote icon by text offset; false when not a marker or no dimensions.
static bool inline_icon_at(size_t offset, int* image, int* w, int* h) {
    for (size_t i = 0; i < s_inline_note_count; ++i) {
        if (s_inline_notes[i].marker_offset != offset) continue;
        int iw = 0, ih = 0;
        if (!s_dims_fn || !s_dims_fn(s_dims_ctx, s_inline_notes[i].image, &iw, &ih) || iw <= 0 || ih <= 0)
            return false;
        if (image) *image = s_inline_notes[i].image;
        if (w) *w = iw;
        if (h) *h = ih;
        return true;
    }
    return false;
}

static bool record_page_image(int image, int x, int y, int w, int h) {
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
    rec->x = (int16_t)x;
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

// 取本页第 i 张图的原始 x（行内图标为行末坐标，块级图为 -1）。越界返回 false。
// Raw x of image i on the page (row-end x for inline icons, -1 for block images); false when out of range.
bool book_layout_page_image_rect(size_t page, int i, int* image, int* x, int* y, int* width, int* height) {
    if (page >= s_count || !s_page_img_start || i < 0) return false;
    uint32_t a = s_page_img_start[page];
    if ((uint32_t)i >= s_page_img_start[page + 1] - a) return false;
    const layout_image_t* rec = &s_images[a + (uint32_t)i];
    if (image) *image = rec->image;
    if (x) *x = rec->x;
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
    s_last_line_x_end = 0;
    s_last_line_used = 0;
    s_page_img_start[s_count] = (uint32_t)s_image_count;
    ++s_count;
    s_page_img_start[s_count] = (uint32_t)s_image_count;
    return true;
}

// 段首的全角/半角空白不参与排版，缩进由阅读设置决定；返回第一个可见字节。
// Leading blanks of a paragraph take no part in typesetting -- the reader setting owns the
// indent -- so return the first visible byte.
static size_t visible_start(size_t off, size_t limit) {
    while (off < limit) {
        size_t n = codepoint_size(s_text + off, limit - off);
        if (!n) break;
        uint32_t cp = codepoint_value(s_text + off, n);
        if (cp == ' ' || cp == '\t' || cp == 0xa0 || cp == 0x1680 ||
            (cp >= 0x2000 && cp <= 0x200b) || cp == 0x202f || cp == 0x205f ||
            cp == 0x2060 || cp == 0x3000 || cp == 0xfeff) {
            off += n; continue;
        }
        break;
    }
    return off;
}

// 折行时保留原文字节位置；CRLF 算一个段落边界。advance_limit 是行宽（竖排是列高），
// hang_margin 是允许标点悬挂的外侧留白（竖排为 0，列底没有可悬挂的空白）。
// Preserve source offsets while wrapping; CRLF is one paragraph boundary. advance_limit is the
// line width (the column height vertically) and hang_margin is the outer slack a hanging mark may
// use (0 vertically: a column has no bottom margin to hang into).
// 行缓冲按实际UTF-8长度增长，包含零宽字符的长行仍可安全扩容；失败保留原指针。
// Grow by actual UTF-8 line bytes, including long zero-width runs; failed growth keeps the old allocation.
static bool line_append(size_t at, const char *text, size_t bytes) {
    if (at > s_len || bytes > s_len - at) return false;
    size_t need = at + bytes + 1;
    if (need > s_line_capacity) {
        size_t capacity = s_line_capacity ? s_line_capacity : 256;
        while (capacity < need) {
            if (capacity > (s_len + 1) / 2) { capacity = s_len + 1; break; }
            capacity *= 2;
        }
        if (capacity > s_len + 1) capacity = s_len + 1;
        char *next = heap_caps_realloc(s_line, capacity, PSRAM_CAPS);
        if (!next) return false;
        s_line = next; s_line_capacity = capacity;
    }
    if (bytes) memcpy(s_line + at, text, bytes);
    s_line[at + bytes] = 0;
    return true;
}

// 英文词含数字、重音字母和组合音标；词内撇号、不换行空格/连字符随词移动。
// English tokens include digits, accented Latin and combining marks; internal apostrophes and nonbreaking spaces/hyphens stay with the token.
static bool latin_letter(uint32_t cp) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
        (cp >= 0xc0 && cp <= 0xd6) || (cp >= 0xd8 && cp <= 0xf6) ||
        (cp >= 0xf8 && cp <= 0x2af) || (cp >= 0x1e00 && cp <= 0x1eff);
}
static bool latin_part(uint32_t cp) {
    return latin_letter(cp) || (cp >= '0' && cp <= '9') || (cp >= 0x300 && cp <= 0x36f);
}
static bool latin_member(uint32_t cp, uint32_t previous, size_t at, size_t n, size_t limit) {
    if (latin_part(cp)) return true;
    if (cp != '\'' && cp != 0x2019 && cp != 0x2011 && cp != 0xa0 && cp != 0x202f) return false;
    if (!latin_part(previous) || at + n >= limit) return false;
    size_t size = codepoint_size(s_text + at + n, limit - at - n);
    return size && latin_part(codepoint_value(s_text + at + n, size));
}
static size_t previous_codepoint(size_t at, size_t start) {
    if (at > start) --at;
    while (at > start && ((unsigned char)s_text[at] & 0xc0) == 0x80) --at;
    return at;
}

// 只探查一个有界完整词；不把英语规则施加到缩写、法语重音字或连接词上。
// Probe one bounded whole word; do not apply English patterns to acronyms, accented languages or joined tokens.
static bool word_hyphen_points(size_t word, size_t limit, uint8_t *points) {
    size_t start = word;
    while (start && word - start <= ENGLISH_HYPHEN_MAX && latin_part((unsigned char)s_text[start-1])) --start;
    if (word - start > ENGLISH_HYPHEN_MAX) return false;
    size_t at = start;
    while (at < limit && at - start <= ENGLISH_HYPHEN_MAX) {
        size_t n = codepoint_size(s_text + at, limit - at);
        if (!n) return false;
        uint32_t cp = codepoint_value(s_text + at, n);
        if (!latin_member(cp, at > start ? (unsigned char)s_text[at-1] : 0, at, n, limit)) break;
        if (n != 1 || !((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))) return false;
        at += n;
    }
    if (at < word || at - start > ENGLISH_HYPHEN_MAX) return false;
    uint8_t original[ENGLISH_HYPHEN_MAX + 1];
    if (!english_hyphen_points(s_text + start, at - start, original)) return false;
    memset(points, 0, ENGLISH_HYPHEN_MAX + 1);
    memcpy(points, original + (word - start), at - word + 1);
    return true;
}

// 用词间胶水的三次方代价评价本行；不是整段Knuth–Plass，不建立整段节点表。
// Score this line by cubic word-space badness; this is not paragraph-wide Knuth–Plass and builds no paragraph node list.
static int line_badness(int64_t delta, int64_t stretch, int64_t shrink) {
    int64_t capacity = delta < 0 ? shrink : stretch;
    if (!delta) return 0;
    if (delta < 0) delta = -delta;
    if (capacity <= 0 || delta >= capacity * 5) return 10000;
    int64_t ratio = delta * 256 / capacity;
    int64_t bad = (100 * ratio * ratio * ratio + (INT64_C(1) << 23)) >> 24;
    return bad < 10000 ? (int)bad : 10000;
}

// 只在字表允许的原文字节处添加显示连字符，按空格代价选择；不修改原文或书签偏移。
// Add display-only hyphens at pattern-approved source offsets, choosing by space badness without changing source/bookmark offsets.
static bool hyphenate_line(size_t visible, size_t word, size_t limit, int px,
                           int available, int64_t stretch, int64_t shrink, int best_cost,
                           size_t *end, int64_t *width) {
    uint8_t points[ENGLISH_HYPHEN_MAX + 1];
    if (!word_hyphen_points(word, limit, points)) return false;
    size_t cut = *end, chosen = 0;
    int64_t advance = *width, chosen_width = 0;
    while (cut > word) {
        size_t previous = previous_codepoint(cut, visible);
        slot_select_for(previous);
        int64_t fitted = advance + ttf_text_width_px(px, "-") + s_tracking_px;
        if (cut - word >= 2 && cut - word <= ENGLISH_HYPHEN_MAX && points[cut-word] && fitted >= 0 &&
            fitted <= available + shrink) {
            int cost = line_badness(available - fitted, stretch, shrink) + 50;
            if (cost < best_cost) { best_cost = cost; chosen = cut; chosen_width = fitted; }
        }
        char glyph[5];
        memcpy(glyph, s_text + previous, cut - previous); glyph[cut - previous] = 0;
        advance -= ttf_text_width_px(px, glyph) + (previous > visible ? s_tracking_px : 0);
        cut = previous;
    }
    if (!chosen || !line_append(chosen - visible, "-", 1)) return false;
    *end = chosen; *width = chosen_width;
    return true;
}
static bool take_line(size_t off, int advance_limit, int hang_margin, size_t* next,
                      bool* paragraph_end, int* px, bool* heading,
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
        // 全角空格在部分字体中不是一个汉字宽，使用正文汉字的实际推进量。
        // Some faces have a narrow ideographic space; use an actual CJK advance instead.
        int advance = ttf_text_width_px(*px, "一");
        if (advance <= 0) advance = *px;
        advance += s_tracking_px;
        if (advance < 1) advance = 1;
        *indent = advance * (int)s_first_line_indent_em;
        if (s_first_line_indent_em) *indent += s_first_line_indent_adjust_px;
        if (*indent < 0) *indent = 0;
    }
    if (*indent >= advance_limit) *indent = advance_limit > 1 ? advance_limit - 1 : 0;
    if (*indent + *px > advance_limit) *indent = 0;
    *margin_before = first_line && block ? (int)((unsigned)*px * block->margin_before_percent / 100) : 0;
    *margin_after = block ? (int)((unsigned)*px * block->margin_after_percent / 100) : 0;
    size_t limit = block ? block->offset + block->len : s_len;
    // 纸书常把全角空格写进段首；统一由阅读设置决定缩进，原文偏移仍保留。
    // Printed-book source often contains leading spaces; the reader setting owns the visual indent.
    size_t visible_off = off;
    if (first_line && !*heading && !*align) visible_off = visible_start(off, limit);
    if (!s_vertical && !first_line)
        while (visible_off < limit && (s_text[visible_off] == ' ' || s_text[visible_off] == '\t')) ++visible_off;
    if (first_line && !*heading && !*align) {
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
    int available = advance_limit - *indent;
    size_t end = visible_off;
    size_t last_start = visible_off;
    uint32_t last_cp = 0;
    int64_t width = 0;
    int64_t last_width = 0;
    int glyph_count = 0;
    size_t word_start = visible_off, wrap_next = visible_off, wrap_end = visible_off;
    size_t ink_end = visible_off, before_last_ink = visible_off, resume = SIZE_MAX;
    int64_t wrap_width = 0, ink_width = 0, before_last_ink_width = 0;
    bool previous_word = false, english_line = true;
    int64_t stretch = 0, shrink = 0, ink_stretch = 0, ink_shrink = 0;
    int64_t wrap_stretch = 0, wrap_shrink = 0;
    s_line[0] = 0;
    *paragraph_end = false;
    while (end < limit && s_text[end] != '\r' && s_text[end] != '\n') {
        size_t n = codepoint_size(s_text + end, s_len - end);
        if (!n) return false;
        uint32_t cp = codepoint_value(s_text + end, n);
        bool word = !s_vertical && latin_member(cp, last_cp, end, n, limit);
        if (word && !previous_word) {
            word_start = end;
            // 开括号跟随首词；普通连字符自身保留在上一行。
            // An opener follows its word; an ordinary hyphen stays at the previous line end.
            bool opener = prohibited_line_end(last_cp);
            wrap_next = opener ? last_start : end;
            wrap_end = opener ? before_last_ink : ink_end;
            wrap_width = opener ? before_last_ink_width : ink_width;
            wrap_stretch = ink_stretch; wrap_shrink = ink_shrink;
        }
        if (cp >= 0x2e80 && cp <= 0xffef) english_line = false;
        // 字体逐字取整后累加 advance；单字测量避免反复扫描整行前缀。
        // Font advances are rounded per glyph and summed; measure each glyph once instead of every prefix.
        char glyph[5];
        memcpy(glyph, s_text + end, n);
        glyph[n] = 0;
        // 行内注号占位符按图标宽度算进行宽；超出可用宽度时把图标移到下一行。
        // Charge the inline marker's icon width to the line; when it does not fit, move the icon
        // to the next line.
        if (cp == 0xFFFC) {
            int iw = 0, ih = 0;
            if (!inline_icon_at(end, NULL, &iw, &ih)) iw = *px;
            int64_t cand = width + iw + (end > visible_off && !*heading ? s_tracking_px : 0);
            if (cand < 0) return false;
            if (cand > available && end != visible_off) {
                s_line[end - visible_off] = 0;
                break;
            }
            last_width = width;
            width = cand;
            ++glyph_count;
            last_start = end;
            last_cp = cp;
            if (!line_append(end - visible_off, glyph, n)) return false;
            end += n;
            before_last_ink = ink_end; before_last_ink_width = ink_width;
            ink_end = end; ink_width = width; ink_stretch = stretch; ink_shrink = shrink;
            previous_word = false;
            continue;
        }
        // 每个字都按自己那一段的字体量宽：行宽、字距和对齐算法一个字没改。
        // Every glyph is measured in its own span's face; width, tracking and alignment math
        // are untouched.
        slot_select_for(end);
        int glyph_advance = ttf_text_width_px(*px, glyph);
        int64_t candidate = width + glyph_advance +
                            (end > visible_off && !*heading ? s_tracking_px : 0);
        if (candidate < 0) return false;
        bool english_body = !s_vertical && !*heading && !*align && english_line;
        if (candidate > available && !(english_body && candidate <= available + shrink)) {
            if (end == visible_off) return false;
            if (!s_vertical && (word || (previous_word && prohibited_line_start(cp)))) {
                if (wrap_end > visible_off) {
                    size_t split = end;
                    int64_t split_width = width;
                    int whole_cost = line_badness(available - wrap_width, wrap_stretch, wrap_shrink);
                    if (word && english_body && hyphenate_line(visible_off, word_start, limit,
                            *px, available, stretch, shrink, whole_cost, &split, &split_width)) {
                        end = split; width = split_width;
                        break;
                    }
                    resume = wrap_next;
                    end = wrap_end; width = wrap_width;
                    s_line[end - visible_off] = 0;
                    break;
                }
                if (word && english_body && hyphenate_line(visible_off, word_start, limit, *px,
                        available, stretch, shrink, 10051, &end, &width)) break;
            }
            // 优先让行尾正文对齐，再允许右标点小幅悬挂到留白；超出安全宽度才回退前一字。
            // Align the text edge first, hang a closing mark slightly into the margin, then roll back only if needed.
            if (prohibited_line_start(cp) || (!s_vertical && (cp == '-' || cp == 0x2010))) {
                // 竖排传 0：列底没有可悬挂的留白，标点一律回退到下一列。
                // Vertical setting passes 0: a column has no bottom slack to hang into, so the
                // mark always rolls back to the next column.
                int hang = hang_margin > 4 ? *px / 3 : 0;
                if (hang > hang_margin - 4) hang = hang_margin - 4;
                if (hang < 0) hang = 0;
                int squeeze = glyph_count > 1 ? (glyph_count - 1) * 3 : 0;
                if (candidate <= (int64_t)available + hang + squeeze) {
                    if (!line_append(end - visible_off, glyph, n)) return false;
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
                    if (!line_append(end - visible_off, glyph, n)) return false;
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
                    if (!line_append(end - visible_off, glyph, n)) return false;
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
        if (!line_append(end - visible_off, glyph, n)) return false;
        end += n;
        before_last_ink = ink_end; before_last_ink_width = ink_width;
        if (cp == ' ' || cp == '\t') {
            stretch += text_space_stretch(glyph_advance);
            shrink += text_space_shrink(glyph_advance);
        } else {
            ink_end = end; ink_width = width; ink_stretch = stretch; ink_shrink = shrink;
        }
        previous_word = word;
    }
    if (!s_vertical && end > ink_end && (s_text[end - 1] == ' ' || s_text[end - 1] == '\t')) {
        if (resume == SIZE_MAX) resume = end;
        end = ink_end; width = ink_width; s_line[end - visible_off] = 0;
    }
    line_runs_build(block, visible_off, end);
    *line_width = (int)width;
    *next = resume == SIZE_MAX ? end : resume;
    if (!s_vertical)
        while (*next < limit && (s_text[*next] == ' ' || s_text[*next] == '\t')) ++*next;
    if (*next < s_len && (s_text[*next] == '\r' || s_text[*next] == '\n')) {
        size_t newline = *next;
        *next = newline + 1;
        if (s_text[newline] == '\r' && *next < s_len && s_text[*next] == '\n') (*next)++;
        *paragraph_end = !block || !block->line_break_after;
    }
    return *next > off;
}

/* ---- 竖排 / Vertical setting ---- */

// 竖排分页：整页自右向左铺列，列内自上而下；插图独占一页，前后各断一次。
// 每字是正立、抬到字格右上角还是转 90°，由字体引擎的竖排绘制统一决定。
// Vertical pagination: columns fill the page right to left and each column runs down; an
// illustration takes a page of its own, breaking both before and after it. Whether a glyph stands
// upright, lifts into the cell's corner or turns 90° is the font engine's vertical drawing rule.
static bool build_vertical(size_t len) {
    size_t off = s_lead_skip;
    size_t visible = skip_image_spacing(off);
    if (visible < len) off = visible;
    else if (!s_images_visible) {
        for (size_t i = 0; i < s_block_count; ++i)
            if (s_blocks[i].image >= 0 && s_blocks[i].offset >= off) { off = s_blocks[i].offset; break; }
    }
    if (!append_page(off)) return false;
    // 首页右起为竖排题头留出列宽；留太多时第一列正文会另起一页，不会卡住。
    // The first page keeps room for a vertical chapter heading at its right; if that is too wide
    // the first body column starts a new page instead of stalling.
    int64_t used = s_count == 1 && s_chapter_lead_width < (unsigned)s_rect.width
        ? (int64_t)s_chapter_lead_width : 0;
    while (off < len) {
        size_t visible_off = skip_image_spacing(off);
        if (visible_off != off) { off = visible_off; continue; }
        const blk_t* block = block_at(off);
        if (block && block->image >= 0 && off == block->offset) {
            int img_w = 0, img_h = 0;
            // 首页题头占的就是顶部这一条，插图不能在题头上开画。
            // The first page's heading owns the top band, so an illustration may not start there.
            bool heading_band = s_count == 1 && s_lead_height > 0;
            if (image_display_size(block->image, &img_w, &img_h)) {
                if ((used || heading_band) && !append_page(off)) return false;
                if (!record_page_image(block->image, -1, 0, img_w, img_h)) return false;
                off = block->offset + block->len;
                if (off < len && s_text[off] == '\n') ++off;
                // 图后另起一页给正文，图页自己只剩这一张图。
                // Start a fresh page for the text so the image page holds nothing else.
                if (off < len && !append_page(off)) return false;
                used = 0;
            } else {
                if ((used || heading_band) && !append_page(off)) return false;
                used = s_rect.width;
                off = block->offset + block->len;
                if (off < len && s_text[off] == '\n') ++off;
            }
            continue;
        }
        int lead = s_count == 1 ? (int)s_lead_height : 0;
        size_t next;
        bool paragraph_end, heading;
        int px, run_height, indent, margin_before, margin_after;
        uint8_t align;
        if (!take_line(off, s_rect.height - lead, 0, &next, &paragraph_end, &px, &heading,
                       &run_height, &indent, &align, &margin_before, &margin_after)) return false;
        int pitch = line_height_for(px);
        if (pitch > s_rect.width) return false;
        int leading = used ? margin_before : 0;
        if (used + leading + pitch > s_rect.width) {
            if (!append_page(off)) return false;
            used = 0;
            leading = 0;
        }
        used += leading + pitch;
        if (paragraph_end) used += gap_for(pitch, heading) + margin_after;
        off = next;
    }
    return true;
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
    s_line_capacity = len + 1 < 256 ? len + 1 : 256;
    s_line = heap_caps_malloc(s_line_capacity, PSRAM_CAPS);
    size_t off = s_lead_skip;
    if (!s_line) goto fail;
    if (s_vertical) {
        if (!build_vertical(len)) goto fail;
        ttf_font_select(TTF_FONT_SLOT_SYSTEM);
        return true;
    }
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
                // 小图标（注号等）贴着上一行末尾画，不占新行；其余插图按块级流排。
                // Small icons (footnote markers) attach to the end of the previous line instead of
                // claiming a new line; other illustrations stay block-level.
                const bool inline_icon = s_last_line_x_end > 0 && line_height > 0 &&
                    img_h <= line_height * 2 && img_w <= s_rect.width / 2 &&
                    s_last_line_x_end + img_w + 4 <= s_rect.x + s_rect.width;
                if (inline_icon) {
                    int iy = s_last_line_used + (line_height - img_h) / 2;
                    if (iy < 0) iy = 0;
                    if (!record_page_image(block->image, s_last_line_x_end + 2, iy, img_w, img_h)) goto fail;
                } else {
                    // 图是页面流里的一个块：放得下就留在本页，文字接着图下面排；放不下才翻页。
                    // An image is one block in the page flow: keep it on this page when it fits and
                    // let the text continue below it; only start a new page when it does not fit.
                    if (used && used + img_h > s_image_rect.height) {
                        if (!append_page(off)) goto fail;
                        used = 0;
                    }
                    if (!record_page_image(block->image, -1, (int)used, img_w, img_h)) goto fail;
                    used += img_h + px / 2;  // 图下留半个行高 / half a line under the image
                }
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
        if (!take_line(off, s_rect.width, s_rect.x, &next, &paragraph_end, &line_px, &heading,
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
        // 记下这一行里的行内注号图标：x 用行内前缀宽度估算，y 落在该行内。
        // Record inline markers on this line: x from the measured prefix, y inside the line.
        for (size_t p = off; p + 3 <= next; ++p) {
            if ((uint8_t)s_text[p] != 0xEF || (uint8_t)s_text[p + 1] != 0xBF || (uint8_t)s_text[p + 2] != 0xBC)
                continue;
            int image = -1, iw = 0, ih = 0;
            if (!inline_icon_at(p, &image, &iw, &ih)) continue;
            char prefix[256];
            size_t plen = p - off;
            if (plen >= sizeof(prefix)) plen = sizeof(prefix) - 1;
            memcpy(prefix, s_text + off, plen);
            prefix[plen] = 0;
            int px_w = 0;
            if (plen) { slot_select_for(off); px_w = ttf_text_width_px(line_px, prefix); }
            int icon_x = rect.x + indent + px_w;
            int icon_y = (int)used + leading + (line_height - ih) / 2;
            if (icon_y < 0) icon_y = 0;
            if (!record_page_image(image, icon_x, icon_y, iw, ih)) goto fail;
        }
        s_last_line_used = (int)used + leading;
        s_last_line_x_end = s_rect.x + indent + line_width;
        used += leading + line_height;
        if (paragraph_end) used += gap_for(line_height, heading) + margin_after;
        off = next;
    }
    // 整页只有一张插图时把它上下居中：贴顶会让插图页看起来像被裁掉了下半页。
    // Center a sheet that holds one illustration and no text: top alignment left the lower half
    // empty and read like a cropped page. A recorded offset above zero means a chapter heading
    // already owns that band, so those pages keep their image where the flow put it.
    for (size_t page = 0; page < s_count; ++page) {
        if (book_layout_page_image_count(page) != 1) continue;
        layout_image_t* rec = &s_images[s_page_img_start[page]];
        if (rec->y != 0 || rec->height >= s_image_rect.height) continue;
        size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
        bool text = false;
        for (size_t i = 0; i < s_block_count && !text; ++i) {
            const blk_t* block = &s_blocks[i];
            if (block->image >= 0 || block->offset < s_pages[page] || block->offset >= end) continue;
            for (size_t k = 0; k < block->len; ++k) {
                char c = s_text[block->offset + k];
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') { text = true; break; }
            }
        }
        if (!text) rec->y = (int16_t)((s_image_rect.height - rec->height) / 2);
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

// 阅读线只铺在有字的地方：整行只有空白字符时不画。
// Guides cover inked spans only; whitespace-only lines get none.
static bool line_has_ink(const char* line) {
    for (const char* at = line; at && *at; ++at)
        if (*at != ' ' && *at != '\t' && *at != '\r' && *at != '\n') return true;
    return false;
}

// 画一列：字格居中于列宽，列内自上而下逐字；转 90° 的字走旋转绘制，右上角标点抬高半格。
// 完整列把余量摊到字间，和横排的两端对齐同一套规则。
// Draw one column: em boxes are centered across the column pitch and glyphs run downward.
// Rotated glyphs use the rotated draw and corner marks lift half a cell. A complete column
// spreads its slack between glyphs, mirroring horizontal justification.
static void draw_vertical_column(uint8_t* fb, EpdRect rect, int left, int pitch, int lead,
                                 int px, int indent, uint8_t align, int run_height,
                                 bool justify) {
    const int cell = left + (pitch - px) / 2;
    const int available = rect.height - lead;
    const size_t line_len = strlen(s_line);
    int y = rect.y + lead + indent;
    // 竖排里的居中与右对齐是列内的纵向对齐，和横排的行内对齐一一对应。
    // Centering and end alignment in vertical setting line the column up vertically, mirroring
    // their horizontal meaning inside a line.
    if (align == 1) y += (available - indent - run_height) / 2;
    else if (align == 2) y += available - indent - run_height;
    int extra = 0;
    int glyph_count = 0;
    for (size_t at = 0; at < line_len;) {
        size_t n = codepoint_size(s_line + at, line_len - at);
        if (!n) break;
        ++glyph_count;
        at += n;
    }
    if (justify) {
        int slack = available - indent - run_height;
        if (glyph_count > 1 && slack > 0) extra = slack / (glyph_count - 1);
    }
    if (s_reading_line && line_has_ink(s_line)) {
        // 阅读线跟着列走，位置在列的右侧：把整页逆时针转 90° 来读这一列，横排的「字下」正落在这里。
        // 原先画在列中线上（cell + px/2），等于穿字而过，看上去就是错乱。
        // 上下位置滑杆在竖排里横着移动这条线；再往里就压字了，所以夹在字格右边。
        // The guide follows the column and runs down its right side: turn the page 90° counter-clockwise
        // to read the column as a horizontal line and the horizontal "under the glyphs" lands here. It
        // used to sit on the column's centre line, straight through the glyphs. The offset slider moves
        // it sideways; it is clamped to the right of the glyph cell so it can never bite into the text.
        const int cell_left = left + (pitch - px) / 2;
        int guide_x = left + pitch - 2 + s_reading_line_offset;
        if (guide_x < cell_left + px + 1) guide_x = cell_left + px + 1;
        const int dash = s_reading_line == 1 ? 19 : 2;
        const int period = s_reading_line == 1 ? 31 : 13;
        // 与横排对称：竖排走整页高度、从页上边起算，每列的虚实节奏完全一样，不会因为列首缩进、
        // 居中或列长短而各画各的。只在有字的纵向范围内落墨：列首缩进与列尾留白保持白底。
        // Symmetric with the horizontal rule: the rhythm counts from the page's top edge so every
        // column shares it regardless of indent, centring or column length. The dashes ink only the
        // column's glyph span; the leading indent and the trailing blank stay white.
        const int ink_top = y;
        int ink_bottom = y + ttf_vertical_text_height_px(px, s_line, s_tracking_px + extra);
        if (ink_bottom > rect.y + rect.height) ink_bottom = rect.y + rect.height;
        for (int pos = rect.y; pos < rect.y + rect.height; pos += period) {
            int top = pos, bottom = pos + dash;
            if (bottom > rect.y + rect.height) bottom = rect.y + rect.height;
            if (top < ink_top) top = ink_top;
            if (bottom > ink_bottom) bottom = ink_bottom;
            if (bottom > top) epd_fill_rect((EpdRect){guide_x, top, 2, bottom - top}, 0x50, fb);
        }
    }
    // 整列一次交给字体引擎：它按 run 换字体，并按竖排规则决定每字正立、抬角还是转 90°。
    // Hand the whole column to the font engine: it follows the run table for faces and decides
    // per glyph whether it stands upright, lifts into the corner or turns 90°.
    ttf_draw_set_runs(s_line_run_count ? s_line_runs : NULL, s_line_run_count);
    (void)ttf_draw_text_px_vertical(fb, cell, y, px, s_line, s_tracking_px + extra, 0, 15);
    // 画完把游标还回去：页面其它文字、界面和状态栏都按系统字体走。
    // Hand the cursor back: every other page, the UI and the status bar use the system face.
    ttf_draw_set_runs(NULL, 0);
    ttf_font_select(TTF_FONT_SLOT_SYSTEM);
}

// 竖排绘制：与 build_vertical 走同样的折列与页界，只是这里落笔。
// Vertical drawing: the same column walk and page boundaries as build_vertical, inking as it goes.
static void draw_vertical_page(uint8_t* fb, size_t page, EpdRect rect) {
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = page == 0 && s_chapter_lead_width < (unsigned)rect.width
        ? (int64_t)s_chapter_lead_width : 0;
    uint32_t img_at = s_page_img_start ? s_page_img_start[page] : 0;
    const uint32_t img_end = s_page_img_start ? s_page_img_start[page + 1] : 0;
    if (s_idle_draw && s_idle_draw->started) {
        off=s_idle_draw->offset;used=s_idle_draw->used;img_at=s_idle_draw->image_at;
    }
    while (off < end) {
        size_t visible = skip_image_spacing(off);
        if (visible != off) { off = visible; continue; }
        const blk_t* image_block = block_at(off);
        if (image_block && image_block->image >= 0 && off == image_block->offset) {
            // 插图独占一页，位置由调用方按整页图绘制；记录只用于识别这一页。
            // The illustration owns its page and the caller draws it whole; the record only marks
            // the page.
            while (img_at < img_end && s_images[img_at].image != image_block->image) ++img_at;
            if (img_at < img_end) ++img_at;
            off = image_block->offset + image_block->len;
            if (off < end && s_text[off] == '\n') ++off;
            continue;
        }
        int lead = page == 0 ? (int)s_lead_height : 0;
        size_t next;
        bool paragraph_end, heading;
        int px, run_height, indent, margin_before, margin_after;
        uint8_t align;
        if (!take_line(off, rect.height - lead, 0, &next, &paragraph_end, &px, &heading,
                       &run_height, &indent, &align, &margin_before, &margin_after)) { if(s_idle_draw)s_idle_draw->failed=true; return; }
        int pitch = line_height_for(px);
        int leading = used ? margin_before : 0;
        if (used + leading + pitch > rect.width) return;
        used += leading;
        if (s_line[0]) {
            const blk_t* block = block_at(off);
            bool final_column = paragraph_end || next >= s_len ||
                (block && next >= block->offset + block->len);
            draw_vertical_column(fb, rect, rect.x + rect.width - (int)used - pitch, pitch, lead,
                                 px, indent, align, run_height,
                                 !heading && !align &&
                                 (!final_column || run_height > rect.height - lead - indent));
        }
        used += pitch;
        if (paragraph_end) used += gap_for(pitch, heading) + margin_after;
        off = next;
        if (s_idle_draw && off < end) {
            s_idle_draw->offset=off;s_idle_draw->used=used;s_idle_draw->image_at=img_at;
            s_idle_draw->started=true;s_idle_draw->complete=false;
            return;
        }
    }
}

// 构造带行内注号图标的行：按占位符切段绘字，图标位置留给 app 覆盖绘制。
// Draw a line that carries inline footnote icons: split at placeholders and leave the icon gaps.
static void draw_line_with_icons(uint8_t* fb, int x, int baseline, int line_px,
                                 size_t off, size_t next) {
    static char seg[512];
    const char *p = s_line;
    size_t source = off;
    int cursor = x;
    while (*p) {
        const char *q = strstr(p, "\xEF\xBF\xBC");
        size_t len = q ? (size_t)(q - p) : strlen(p);
        if (len) {
            if (len >= sizeof(seg)) len = sizeof(seg) - 1;
            memcpy(seg, p, len);
            seg[len] = 0;
            int width = ttf_text_width_px(line_px, seg);
            int points = 0;
            for (size_t i = 0; i < len; ++i)
                if (((unsigned char)seg[i] & 0xc0) != 0x80) ++points;
            if (points > 1) width += s_tracking_px * (points - 1);
            ttf_draw_text_px(fb, cursor, baseline, line_px, seg, EPD_DRAW_ALIGN_LEFT, 0, 15);
            cursor += width;
        }
        if (!q) break;
        // 占位符在正文里的位置按顺序对应注表，图标宽度取排版同一次探测的结果。
        // Placeholders map to the note table in order; the icon width comes from the same probe.
        size_t icon_off = SIZE_MAX;
        while (source + 3 <= next) {
            if ((uint8_t)s_text[source] == 0xEF && (uint8_t)s_text[source + 1] == 0xBF &&
                (uint8_t)s_text[source + 2] == 0xBC) {
                icon_off = source;
                source += 3;
                break;
            }
            ++source;
        }
        int iw = 0, ih = 0;
        if (icon_off != SIZE_MAX && inline_icon_at(icon_off, NULL, &iw, &ih)) cursor += iw;
        else cursor += line_px;
        p = q + 3;
    }
}

static bool line_has_inline_icon(const char* line) { return strstr(line, "\xEF\xBF\xBC") != NULL; }

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
    if (s_vertical) { draw_vertical_page(fb, page, rect); return; }
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = page == 0 ? s_lead_height : 0;
    // 本页插图按出现顺序排在池里，游标走到哪个块就用哪条记录，不必再查表。
    // This page's images sit in the pool in encounter order, so the walk just consumes them in
    // turn instead of looking each one up.
    uint32_t img_at = s_page_img_start ? s_page_img_start[page] : 0;
    const uint32_t img_end = s_page_img_start ? s_page_img_start[page + 1] : 0;
    if (s_idle_draw && s_idle_draw->started) {
        off=s_idle_draw->offset;used=s_idle_draw->used;img_at=s_idle_draw->image_at;
    }
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
        if (!take_line(off, s_rect.width, s_rect.x, &next, &paragraph_end, &line_px, &heading,
                       &line_width, &indent, &align, &margin_before, &margin_after)) { if(s_idle_draw)s_idle_draw->failed=true; return; }
        int line_height = line_height_for(line_px);
        int leading = used ? margin_before : 0;
        if (used + leading + line_height > rect.height) return;
        used += leading;
        if (s_line[0]) {
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
            // 完整正文行对齐到统一右边界；悬挂标点最多使用少量右侧留白。
            // Justify complete body lines; a hanging closer may use a small part of the right margin.
            const bool fitted = !heading && !align && (!final_line || line_width > available);
            int line_extent = line_width > 0 ? line_width : 0;
            if (fitted) {
                int hang = s_rect.x > 4 ? line_px / 3 : 0;
                if (hang > s_rect.x - 4) hang = s_rect.x - 4;
                line_extent = line_width > available ? available + hang : available;
            }
            // 阅读线只铺在有字的横向范围：首行缩进、行尾空白和对齐留白保持白底。
            // 虚实仍从页左边缘起算，各行的点划节奏保持一致。
            // The guide inks only the line's glyph span: the first-line indent, trailing blanks and
            // alignment slack stay white. Dashes still count from the page's left edge so every line
            // keeps the same rhythm.
            if (s_reading_line && line_has_ink(s_line)) {
                int guide_y = rect.y + (int)used + line_px + (line_height - line_px) / 2 +
                              s_reading_line_offset;
                int dash = s_reading_line == 1 ? 19 : 2;
                int period = s_reading_line == 1 ? 31 : 13;
                const int ink_left = x, ink_right = x + line_extent;
                for (int dx = 0; dx < rect.width; dx += period) {
                    int left = rect.x + dx, right = rect.x + dx + dash;
                    if (right > rect.x + rect.width) right = rect.x + rect.width;
                    if (left < ink_left) left = ink_left;
                    if (right > ink_right) right = ink_right;
                    if (right > left)
                        epd_fill_rect((EpdRect){left, guide_y, right - left, 2}, 0x50, fb);
                }
            }
            // 绘制按 run 换字体：行宽、字距、两端对齐的算法一个字没动，只有取字形的槽跟着走。
            // Drawing follows the runs: width, tracking and justification math are untouched,
            // only the slot each glyph comes from changes.
            ttf_draw_set_runs(s_line_run_count ? s_line_runs : NULL, s_line_run_count);
            if (line_has_inline_icon(s_line))
                draw_line_with_icons(fb, x, baseline, line_px, off, next);
            else if (fitted)
                ttf_draw_text_px_fitted(fb, x, baseline, line_px, s_line, s_tracking_px,
                                        line_extent, 0, 15);
            else if (s_tracking_px && !heading)
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
        if (s_idle_draw && off < end) {
            s_idle_draw->offset=off;s_idle_draw->used=used;s_idle_draw->image_at=img_at;
            s_idle_draw->started=true;s_idle_draw->complete=false;
            return;
        }
    }
}

int book_layout_draw_slice(uint8_t *fb,size_t page,EpdRect rect,int px,book_layout_draw_cursor_t *cursor) {
    if(!cursor||!fb||s_idle_draw||page>=s_count||px!=s_px||rect.width!=s_rect.width||rect.height!=s_rect.height)return -1;
    if(!cursor->generation){cursor->generation=s_generation;cursor->page=page;}
    if(cursor->generation!=s_generation||cursor->page!=page||cursor->failed)return -1;
    if(cursor->complete)return 1;
    cursor->complete=true;
    s_idle_draw=cursor;
    book_layout_draw_page(fb,page,rect,px);
    s_idle_draw=NULL;
    ttf_draw_set_runs(NULL,0);ttf_font_select(TTF_FONT_SLOT_SYSTEM);
    return cursor->failed?-1:cursor->complete?1:0;
}

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
#include "esp_log.h"
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
static unsigned s_reading_line;
static int s_reading_line_offset;
static bool s_images_visible = true;
static size_t s_lead_skip;
static unsigned s_lead_height;

// 阶段1（映射+串口打印）不绘制划线；弹窗阶段置 1 恢复。
// / Stage 1 maps and logs only; flip to 1 when the popup stage lands.
#ifndef BOOK_NOTES_DRAW_MARKS
#define BOOK_NOTES_DRAW_MARKS 1
#endif
static struct {
    size_t lo, hi;
    unsigned src;
} *s_marks;
static unsigned s_mark_count;

static const blk_t* s_blocks;
static size_t s_block_count;

// PR #7 的页内图片池，页起点表划分每页的记录；尺寸优先用有界头部探测结果。
// PR #7's flat image pool uses page starts to slice records; prefer bounded header probe dimensions.
typedef struct { int image, y, width, height; } layout_image_t;
static layout_image_t *s_images;
static size_t s_image_count, s_image_capacity;
static uint32_t *s_page_img_start;
static book_layout_image_dims_fn s_dims_fn;
static void *s_dims_ctx;
static uint32_t s_generation;

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
// 拒绝截断、过长编码、代理项和嵌入零字节。/ Reject truncation, overlong encodings, surrogates and embedded NUL.
static size_t codepoint_size(const char* text, size_t remaining) {
    if (!remaining) return 0;
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

static bool punctuation_character(uint32_t cp) {
    return prohibited_line_start(cp) || prohibited_line_end(cp) ||
           cp == '\'' || cp == '"' || cp == '-' || cp == 0x2013 || cp == 0x2014 ||
           cp == 0x2025 || cp == 0x00b7 || cp == 0x30fb;
}

// 连续标点是一个不可拆分的单元，例如 ：“、……、——、！”。
// Treat a punctuation run as one unbreakable unit; paragraph boundaries still win.
static size_t punctuation_run_end(size_t start, size_t limit, uint32_t *last) {
    size_t end = start;
    while (end < limit && s_text[end] != '\r' && s_text[end] != '\n') {
        size_t n = codepoint_size(s_text + end, limit - end);
        if (!n) break;
        uint32_t cp = codepoint_value(s_text + end, n);
        if (!punctuation_character(cp)) break;
        *last = cp;
        end += n;
    }
    return end;
}

void book_layout_free(void) {
    free(s_pages);
    free(s_line);
    free(s_images);
    free(s_page_img_start);
    s_images = NULL;
    s_page_img_start = NULL;
    s_image_count = s_image_capacity = 0;
    ++s_generation;
    s_pages = NULL;
    s_line = NULL;
    s_text = NULL;
    free(s_marks);
    s_marks = NULL;
    s_mark_count = 0;
    s_count = s_capacity = s_len = 0;
    s_px = 0;
    s_blocks = NULL;
    s_block_count = 0;
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

void book_layout_set_image_dims(book_layout_image_dims_fn fn, void *ctx) { s_dims_fn = fn; s_dims_ctx = ctx; }
uint32_t book_layout_generation(void) { return s_generation; }
int book_layout_page_image_count(size_t page) {
    if (page >= s_count || !s_page_img_start) return 0;
    return (int)(s_page_img_start[page + 1] - s_page_img_start[page]);
}
bool book_layout_page_image_at(size_t page, int i, int *image, int *y, int *width, int *height) {
    if (i < 0 || i >= book_layout_page_image_count(page)) return false;
    const layout_image_t *r = &s_images[s_page_img_start[page] + (uint32_t)i];
    if (image) *image = r->image;
    if (y) *y = r->y;
    if (width) *width = r->width;
    if (height) *height = r->height;
    return true;
}
int book_layout_page_image(size_t page) {
    if (page >= s_count || !s_block_count) return -1;
    const blk_t *first = block_at(s_pages[page]);
    if (!s_images_visible) return first && first->offset == s_pages[page] ? first->image : -1;
    if (book_layout_page_image_count(page) != 1) return -1;
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    for (size_t i = (size_t)(first - s_blocks); i < s_block_count && s_blocks[i].offset < end; ++i)
        if (s_blocks[i].image < 0 && !image_spacing_block(&s_blocks[i])) return -1;
    return s_images[s_page_img_start[page]].image;
}
static bool image_display_size(const blk_t *block, int *width, int *height) {
    uint64_t w = block->image_width, h = block->image_height;
    if ((!w || !h) && s_dims_fn) {
        int iw = 0, ih = 0;
        if (s_dims_fn(s_dims_ctx, block->image, &iw, &ih) && iw > 0 && ih > 0) { w = iw; h = ih; }
    }
    if (!w || !h) return false;
    if (w > (unsigned)s_rect.width) { h = h * s_rect.width / w; w = s_rect.width; }
    if (h > (unsigned)s_rect.height) { w = w * s_rect.height / h; h = s_rect.height; }
    *width = w ? (int)w : 1; *height = h ? (int)h : 1;
    return true;
}
static bool record_page_image(int image, int y, int width, int height) {
    if (s_image_count == s_image_capacity) {
        size_t cap = s_image_capacity ? s_image_capacity * 2 : 16;
        if (cap > HTML_TEXT_MAX_BLOCKS) return false;
        layout_image_t *grown = heap_caps_realloc(s_images, cap * sizeof(*grown), PSRAM_CAPS);
        if (!grown) return false;
        s_images = grown; s_image_capacity = cap;
    }
    s_images[s_image_count++] = (layout_image_t){image, y, width, height};
    s_page_img_start[s_count] = (uint32_t)s_image_count;
    return true;
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
        uint32_t *starts = heap_caps_realloc(s_page_img_start, (cap + 1) * sizeof(*starts), PSRAM_CAPS);
        if (!starts) return false;
        s_page_img_start = starts;
        s_capacity = cap;
    }
    s_pages[s_count] = off;
    s_page_img_start[s_count++] = (uint32_t)s_image_count;
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
        int advance = ttf_text_width_px(*px, "　");
        if (advance <= 0) advance = ttf_text_width_px(*px, "一");
        if (advance <= 0) advance = *px;
        advance += s_tracking_px;
        if (advance < 1) advance = 1;
        *indent = advance * (int)s_first_line_indent_em;
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
        uint32_t run_last = cp;
        size_t run_end = punctuation_character(cp)
            ? punctuation_run_end(end, limit, &run_last) : end;
        if (run_end > end + n) {
            bool ends_with_opener = prohibited_line_end(run_last) ||
                ((run_last == '\'' || run_last == '"') && prohibited_line_start(cp));
            // 左引号/括号须带上后面的首字，不能把完整标点串单独留在行尾。
            // A trailing opener travels with the first quoted character.
            if (ends_with_opener && run_end < limit && s_text[run_end] != '\r' && s_text[run_end] != '\n') {
                size_t following = codepoint_size(s_text + run_end, limit - run_end);
                if (!following) return false;
                run_last = codepoint_value(s_text + run_end, following);
                run_end += following;
            }
            int64_t candidate = width;
            int run_glyphs = 0;
            for (size_t at = end; at < run_end;) {
                size_t bytes = codepoint_size(s_text + at, run_end - at);
                if (!bytes) return false;
                char mark[5]; memcpy(mark, s_text + at, bytes); mark[bytes] = 0;
                candidate += ttf_text_width_px(*px, mark) +
                             (at > visible_off && !*heading ? s_tracking_px : 0);
                ++run_glyphs;
                at += bytes;
            }
            if (candidate < 0) return false;
            if (candidate > available && end > visible_off) {
                int hang = s_rect.x > 4 ? *px / 3 : 0;
                if (hang > s_rect.x - 4) hang = s_rect.x - 4;
                int squeeze = glyph_count + run_glyphs > 1 ? (glyph_count + run_glyphs - 1) * 3 : 0;
                bool can_hang = prohibited_line_start(cp) && !ends_with_opener &&
                                candidate <= (int64_t)available + hang + squeeze;
                if (!can_hang) {
                    if (prohibited_line_start(cp) && last_start > visible_off) {
                        // 标点串随前一字一起下移，避免下一行从逗号/冒号开始。
                        // Carry the preceding character with closing punctuation.
                        end = last_start;
                        width = last_width;
                        s_line[end - visible_off] = 0;
                    } else if (prohibited_line_start(cp) && last_start == visible_off) {
                        // 极窄行仍成组前进，防止在同一源偏移无限重排。
                        // An exceptionally narrow line must still make progress.
                        memcpy(s_line + end - visible_off, s_text + end, run_end - end);
                        s_line[run_end - visible_off] = 0;
                        width = candidate;
                        end = run_end;
                    }
                    break;
                }
            }
            last_width = width;
            last_start = end;
            last_cp = run_last;
            width = candidate;
            glyph_count += run_glyphs;
            memcpy(s_line + end - visible_off, s_text + end, run_end - end);
            s_line[run_end - visible_off] = 0;
            end = run_end;
            if (width > available) break;
            continue;
        }
        // 字体逐字取整后累加 advance；单字测量避免反复扫描整行前缀。
        // Font advances are rounded per glyph and summed; measure each glyph once instead of every prefix.
        char glyph[5];
        memcpy(glyph, s_text + end, n);
        glyph[n] = 0;
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
        if (block && block->chapter_start && off == block->offset && used &&
            s_pages[s_count - 1] != off) {
            if (!append_page(off)) goto fail;
            used = 0;
        }
        if (block && block->image >= 0 && off == block->offset) {
            int width = rect.width, height = rect.height;
            bool known = image_display_size(block, &width, &height);
            if (used && used + height > rect.height) {
                if (!append_page(off)) goto fail;
                used = 0;
            }
            // 未知尺寸也记录完整图片槽，调用方仍可尝试解码或在槽内显示错误。
            // Record an unknown-size full-page slot too, allowing decode or a confined failure notice.
            if (!record_page_image(block->image, (int)used, width, height)) goto fail;
            used += height + (known && height < rect.height ? px / 2 : 0);
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
    return true;
fail:
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

// 在 [hay, hay+cap) 内跳过空白字符匹配 needle，给出匹配区间；未命中返回 false。
// needle 侧已由调用方去空白。纯内存线性扫描，仅加载时调用。
// / Whitespace-tolerant substring search; needle is pre-normalized by the caller.
static bool mark_find(const char* hay, size_t cap, const char* needle, size_t* lo, size_t* hi) {
    if (!hay || !needle || !needle[0]) return false;
    for (size_t i = 0; i < cap && hay[i]; ++i) {
        if (hay[i] == ' ' || hay[i] == '\t' || hay[i] == '\n' || hay[i] == '\r' ||
            ((unsigned char)hay[i] == 0xE3 && i + 2 < cap && (unsigned char)hay[i + 1] == 0x80 &&
             (unsigned char)hay[i + 2] == 0x80))  // 全角空格 U+3000 / ideographic space
            continue;
        size_t j = 0, k = i;
        while (needle[j]) {
            while (k < cap && hay[k] &&
                   (hay[k] == ' ' || hay[k] == '\t' || hay[k] == '\n' || hay[k] == '\r' ||
                    ((unsigned char)hay[k] == 0xE3 && k + 2 < cap &&
                     (unsigned char)hay[k + 1] == 0x80 && (unsigned char)hay[k + 2] == 0x80)))
                k += (unsigned char)hay[k] == 0xE3 ? 3 : 1;
            if (k >= cap || !hay[k] || hay[k] != needle[j]) break;
            ++k;
            ++j;
        }
        if (!needle[j]) { *lo = i; *hi = k; return true; }
    }
    return false;
}

bool book_layout_set_marks(const char* texts, size_t stride, unsigned count, unsigned* resolved) {
    free(s_marks);
    s_marks = NULL;
    s_mark_count = 0;
    if (resolved) *resolved = 0;
    if (!texts || !stride || !count || !s_text || !s_len) return false;
    s_marks = malloc(count * sizeof(*s_marks));
    if (!s_marks) return false;
    for (unsigned i = 0; i < count; ++i) {
        size_t lo, hi;
        if (mark_find(s_text, s_len, texts + i * stride, &lo, &hi)) {
            s_marks[s_mark_count] = (typeof(*s_marks)){.lo = lo, .hi = hi, .src = i};
            ++s_mark_count;
        }
    }
    if (resolved) *resolved = s_mark_count;
    if (s_mark_count < count) {
        // 失配诊断：取前 2 条失败划线，用其前 12 字节（约 4 个汉字）做短前缀
        // 定位并打印命中处上下文——分叉字符直接可见。
        // / Mismatch diagnosis: locate the first 2 failed highlights by a short
        // / 12-byte prefix and dump the surrounding text; the diverging chars show up.
        unsigned dumped = 0;
        for (unsigned i = 0; i < count && dumped < 2; ++i) {
            const char* hl = texts + i * stride;
            size_t pfx = 0;
            while (hl[pfx] && pfx < 12) {
                if ((unsigned char)hl[pfx] >= 0x80) { pfx += 3; if (pfx > 12) break; }
                else ++pfx;
            }
            if (pfx < 6) continue;  // 前缀太短无定位价值 / prefix too short to locate
            char probe[13];
            memcpy(probe, hl, pfx);
            probe[pfx] = 0;
            size_t lo, hi;
            if (!mark_find(s_text, s_len, probe, &lo, &hi)) {
                ESP_LOGW("book_layout", "mark miss: no prefix hit hl[%u]='%.48s'", i, hl);
            } else {
                const size_t ctx0 = lo > 24 ? lo - 24 : 0;
                ESP_LOGW("book_layout", "mark miss: prefix hit ctx='%.40s' hl[%u]='%.48s'",
                         s_text + ctx0, i, hl);
            }
            ++dumped;
        }
    }
    if (!s_mark_count) {
        // 终极诊断：一条都定位不到时，打印正文开头与首条划线的原始字节，
        // 直接暴露编码/字符级差异。/ Ultimate diagnosis: dump raw bytes of the
        // chapter head and the first highlight to expose encoding-level drift.
        char dump[193];
        size_t w = 0;
        for (size_t i = 0; i < s_len && w < sizeof(dump) - 3 && s_text[i]; ++i) {
            const unsigned char c = (unsigned char)s_text[i];
            if (c == '\n' || c == '\r') break;
            w += (size_t)snprintf(dump + w, sizeof(dump) - w, "%02X", c);
        }
        const char* first = texts;
        ESP_LOGW("book_layout", "mark spans=0 text[%zu]='%.48s' hex=%s hl='%.48s'", s_len, s_text, dump, first);
    }
    return true;
}

// 行区间 [off, next) 与哪条划线相交；命中返回划线内部序号。
// / Which mark intersects the line span [off, next); returns the mark index.
static int mark_hit(size_t off, size_t next) {
    for (unsigned i = 0; i < s_mark_count; ++i)
        if (s_marks[i].lo < next && off < s_marks[i].hi) return (int)i;
    return -1;
}

int book_layout_mark_at(size_t page, int y_rel) {
    if (page >= s_count || !s_block_count || book_layout_page_image(page) >= 0) return -1;
    // 与 draw_page 相同的行推进重放，定位行后查区间相交。
    // / Same line walk as draw_page, then intersect the resolved marks.
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = page == 0 ? s_lead_height : 0;
    while (off < end) {
        size_t visible = skip_image_spacing(off);
        if (visible != off) { off = visible; continue; }
        size_t next;
        bool paragraph_end, heading;
        int line_px, line_width, indent, margin_before, margin_after;
        uint8_t align;
        if (!take_line(off, &next, &paragraph_end, &line_px, &heading,
                       &line_width, &indent, &align, &margin_before, &margin_after)) return -1;
        int line_height = line_height_for(line_px);
        int leading = used ? margin_before : 0;
        if (used + leading + line_height > s_rect.height) return -1;
        const int top = (int)used + leading;
        used += leading;
        if (s_line[0] && y_rel >= top && y_rel < top + line_height) return mark_hit(off, next);
        used += line_height;
        if (paragraph_end) used += gap_for(line_height, heading) + margin_after;
        off = next;
    }
    return -1;
}

unsigned book_layout_mark_src(unsigned k) {
    return s_marks && k < s_mark_count ? s_marks[k].src : UINT_MAX;
}

bool book_layout_mark_span(unsigned k, size_t* lo, size_t* hi) {
    if (!s_marks || k >= s_mark_count) return false;
    if (lo) *lo = s_marks[k].lo;
    if (hi) *hi = s_marks[k].hi;
    return true;
}

unsigned book_layout_page_mark_rects(size_t page, book_layout_mark_rect_t* out, unsigned cap) {
    if (!out || !cap || page >= s_count || !s_block_count || book_layout_page_image(page) >= 0)
        return 0;
    // 与 draw_page 相同的行推进重放：行区间与划线相交即输出矩形，
    // 同一 mark 的连续行合并为一块，翻页日志与点击判定共用同一几何。
    // / Same line walk as draw_page: an intersecting line emits a rect, and
    // / consecutive lines of one mark merge into one block for logs and taps.
    unsigned n = 0;
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = page == 0 ? s_lead_height : 0;
    while (off < end) {
        size_t visible = skip_image_spacing(off);
        if (visible != off) { off = visible; continue; }
        size_t next;
        bool paragraph_end, heading;
        int line_px, line_width, indent, margin_before, margin_after;
        uint8_t align;
        if (!take_line(off, &next, &paragraph_end, &line_px, &heading,
                       &line_width, &indent, &align, &margin_before, &margin_after)) break;
        int line_height = line_height_for(line_px);
        int leading = used ? margin_before : 0;
        if (used + leading + line_height > s_rect.height) break;
        const int top = (int)used + leading;
        used += leading;
        if (s_line[0]) {
            const int hit = mark_hit(off, next);
            if (hit >= 0) {
                const EpdRect r = {s_rect.x + indent, s_rect.y + top,
                                   line_width > 0 ? line_width : line_px, line_height};
                if (n && out[n - 1].mark == (unsigned)hit &&
                    out[n - 1].rect.y + out[n - 1].rect.height == r.y)
                    out[n - 1].rect.height += r.height;
                else if (n < cap)
                    out[n++] = (book_layout_mark_rect_t){.mark = (unsigned)hit, .rect = r};
                if (n == cap) break;
            }
        }
        used += line_height;
        if (paragraph_end) used += gap_for(line_height, heading) + margin_after;
        off = next;
    }
    return n;
}

void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px) {
    if (!fb || page >= s_count || px != s_px || rect.width != s_rect.width ||
        rect.height != s_rect.height || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return;
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = page == 0 ? s_lead_height : 0;
    uint32_t img_at = s_page_img_start[page], img_end = s_page_img_start[page + 1];
    while (off < end) {
        size_t visible = skip_image_spacing(off);
        if (visible != off) { off = visible; continue; }
        const blk_t *block = block_at(off);
        if (block && block->image >= 0 && off == block->offset) {
            if (img_at < img_end && s_images[img_at].image == block->image) {
                const layout_image_t *image = &s_images[img_at++];
                used = (int64_t)image->y + image->height + (image->height < rect.height ? px / 2 : 0);
            } else {
                // 缺失插图记录时仍保留其排版空间，让后续正文继续绘制。
                // If an image record is missing, retain its layout space and keep drawing the following text.
                int width = rect.width, height = rect.height;
                bool known = image_display_size(block, &width, &height);
                used += height + (known && height < rect.height ? px / 2 : 0);
            }
            off = block->offset + block->len;
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
            int baseline = rect.y + (int)used + ttf_ascender_px(line_px);
            const blk_t *line_block = block_at(off);
            bool final_line = paragraph_end || next >= s_len ||
                (line_block && next >= line_block->offset + line_block->len);
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
            // 划线装饰：行区间与划线区间相交即画点状虚线（仿微信读书 app：
            // 3px 点 + 3px 间隔），点小色深以保证电子纸可见。
            // / Highlight decoration: a dotted underline on intersecting spans
            // / (WeRead mobile style: 3 px dots, 3 px gaps), dark enough for e-paper.
#if BOOK_NOTES_DRAW_MARKS
            const int hit_mark = mark_hit(off, next);
            if (hit_mark >= 0) {
                int mark_w = line_width;
                if (x + mark_w > rect.x + rect.width) mark_w = rect.x + rect.width - x;
                if (mark_w > 0) {
                    const int dot_y = baseline + line_px / 8 + 2;
                    for (int dx = 0; dx < mark_w; dx += 6) {
                        const int dw = dx + 3 <= mark_w ? 3 : mark_w - dx;
                        epd_fill_rect((EpdRect){x + dx, dot_y, dw, 2}, 0x30, fb);
                    }
                }
            }
#endif
        }
        used += line_height;
        if (paragraph_end) used += gap_for(line_height, heading) + margin_after;
        off = next;
    }
}

bool book_layout_page_image_rect(size_t page, EpdRect *area) {
    if (!area || !book_layout_page_image_at(page, 0, NULL, &area->y, &area->width, &area->height)) return false;
    area->x = s_rect.x + (s_rect.width - area->width) / 2;
    area->y += s_rect.y;
    return true;
}

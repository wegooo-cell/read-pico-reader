/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借用章节 UTF-8 文本，生成分页并绘制正文。
 * English: Paginate borrowed chapter UTF-8 text and draw its body.
 *
 * 冻结：不释放原文、不刷新屏幕；调用方持有字体绘制互斥锁。
 * Frozen: Never free source text or present the display; caller holds the font draw lock.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "epdiy.h"
#include "html_text.h"

/// 重排借用文本；空文一页，失败清空布局，超过 4096 页返回 false。/ Borrow and paginate; empty text has one page, failure clears layout, over 4096 pages fails.
bool book_layout_build(const char* utf8, size_t len, EpdRect rect, int px);
/// 借用块表；标题字号加8，块间单换行；原文与块表须存活至free。/ Borrow blocks; headings add 8 px, with one newline between blocks; text and blocks must outlive layout.
bool book_layout_build_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px);
/// 设置行高百分比与段后距离百分比，重排时生效。/ Set line height and paragraph gap percentages for the next layout.
void book_layout_set_spacing(unsigned line_percent, unsigned paragraph_percent);
/// 正文额外字间距为 -4/-2/0/+2/+4 像素。/ Extra body tracking is -4/-2/0/+2/+4 pixels.
void book_layout_set_typography(int tracking_px);
/// 普通正文首行缩进 0..3 字；不改变标题及居中、右对齐块。/ Indent ordinary body paragraphs by 0..3 em; keep headings and aligned blocks unchanged.
void book_layout_set_first_line_indent(unsigned em);
/// 把正文宽度收齐到完整汉字列并居中，避免折行余量只堆在右侧。/ Center complete CJK columns so wrap slack does not accumulate on the right.
EpdRect book_layout_balanced_rect(EpdRect outer, int px, int tracking_px);
/// 正文阅读线：0 无，1 虚线，2 点线。/ Reading guides: none, dashed or dotted.
void book_layout_set_reading_line(unsigned style);
/// 阅读线相对默认位置上下移动 -8..+8 像素；负值上移。/ Move guide -8..+8 px; negative is up.
void book_layout_set_reading_line_offset(int offset_px);
/// 关闭插图时，排版跳过图片块，不为其保留页或正文空位。/ When false, omit image blocks without reserving pages or body space.
void book_layout_set_images_visible(bool visible);
/// EPUB 章节首页预留标题区并跳过已经在题头显示的前置标题块；每章重排前调用。
/// Reserve a first-page chapter heading and skip heading blocks already shown there; call before each chapter layout.
void book_layout_set_chapter_lead(size_t skip_bytes, unsigned height_px);
/// 查询插图尺寸；先用块表的缓存尺寸，无尺寸时回调，失败保留整页图片槽。
/// Query dimensions after consulting block metadata; failure reserves a full-page image slot.
typedef bool (*book_layout_image_dims_fn)(void* ctx, int image, int* width, int* height);
/// 注册尺寸回调；传 NULL 取消。排版不持有它，调用方负责生命周期。
/// Register the dimensions callback; NULL clears it. Layout borrows it, the caller owns it.
void book_layout_set_image_dims(book_layout_image_dims_fn fn, void* ctx);

/// 本页插图张数；越界返回 0。
/// Number of images placed on this page; 0 when out of range.
int book_layout_page_image_count(size_t page);
/// 取本页第 i 张图：序号、页内 y、显示宽高。越界返回 false。
/// Image i of this page: index, y within the page, and display width/height.
bool book_layout_page_image_at(size_t page, int i, int* image, int* y, int* width, int* height);

/// 纯插图页的序号，混排或文字页为 -1；关闭图片后保留纯图章跳过标记。
/// Image index for an image-only page, -1 for mixed/text pages; hidden image-only chapters retain a skip marker.
int book_layout_page_image(size_t page);
/// 第一张图片的屏幕坐标，兼容旧调用方。/ First image screen coordinates for legacy callers.
bool book_layout_page_image_rect(size_t page, EpdRect *area);
/// 布局代次，重排或释放后改变，用于使页内位图缓存失效。/ Layout generation changes after rebuild/free, invalidating page bitmap caches.
uint32_t book_layout_generation(void);
/// 释放页表，不释放原文。/ Free layout storage, never the borrowed text.
void book_layout_free(void);
/// 返回页数；未建立布局时为零。/ Return page count, zero without a layout.
size_t book_layout_page_count(void);
/// 使用建立布局时的宽高与字号绘图；不匹配或越界时不绘制。/ Draw with the built dimensions and size; mismatches or invalid pages do nothing.
void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px);
/// 查找字节偏移所属页，越界偏移夹到末页。/ Find page containing a byte offset; excessive offsets clamp to the last page.
size_t book_layout_page_for_offset(size_t off);
/// 返回页首原文字节偏移；越界页返回文本长度。/ Return source byte offset at page start; invalid pages return text length.
size_t book_layout_page_start_offset(size_t page);

/// 划线装饰（偏移区间语义）：加载时对章节全文做跳空白搜索，记录每条划线
/// 在原文中的 [起点,终点) 区间；绘制时行区间与划线区间相交即画下划线。
/// / Highlight decoration by source spans: locate each highlight inside the chapter
/// / text once at load, then underline any line whose span intersects one.
/// texts/count 为等长条目数组（条目步长 stride 字节）；换章或关书传 NULL。
/// resolved 出参返回定位成功的条数。
/// / texts is a fixed-stride array; pass NULL to clear. *resolved gets the hit count.
bool book_layout_set_marks(const char* texts, size_t stride, unsigned count, unsigned* resolved);
/// 查 y_rel 所在行与哪条划线相交（mark 索引，-1 无命中）。调用方须与绘制串行化。
/// / Index of the mark intersecting the line at y_rel (-1 none); serialize with drawing.
int book_layout_mark_at(size_t page, int y_rel);
/// 第 k 个定位成功的 mark 对应的源条目序号（set_marks 输入数组下标）；越界返回 UINT_MAX。
/// / Source entry index of the k-th resolved mark (into set_marks input); UINT_MAX out of range.
unsigned book_layout_mark_src(unsigned k);
/// 第 k 个 mark 在章节原文中的字节区间 [lo,hi)（阶段1串口打印用）；越界返回 false。
/// / Byte span [lo,hi) of the k-th mark inside the chapter text; false out of range.
bool book_layout_mark_span(unsigned k, size_t* lo, size_t* hi);
/// 当前页上与划线相交的行段矩形（同 mark 连续行合并）；返回写入 out 的条数。
/// 调用方须与绘制串行化（take_line 重放共用静态行缓冲）。
/// / Rects of line runs intersecting marks on this page (consecutive lines merged);
/// / returns the count written. Serialize with drawing (shared static line buffer).
typedef struct {
    unsigned mark; ///< mark 序号（mark_at/ mark_src 语义）/ Mark index (see mark_at/mark_src)
    EpdRect rect;  ///< 屏幕矩形 / On-screen rectangle
} book_layout_mark_rect_t;
unsigned book_layout_page_mark_rects(size_t page, book_layout_mark_rect_t* out, unsigned cap);

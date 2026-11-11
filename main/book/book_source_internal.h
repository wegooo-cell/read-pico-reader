/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书源编码和章节解析，不依赖界面。
 * English: Source encoding and chapter parsing without UI dependencies.
 * 冻结：保留原文件偏移，无效编码替换，不写文件。
 * Frozen: Preserve original byte offsets, replace invalid encoding, never write files.
 */
#pragma once
#include "book_source.h"
#include <stdbool.h>
#include <stdio.h>
#define BOOK_CHAPTER_MAX 2048
#define BOOK_CHAPTER_BYTES_MAX (96 * 1024)
#define TXT_NAV_PAGE 128u
#define TXT_NAV_MAX 16384u
#define TXT_NAV_PAGES (TXT_NAV_MAX / TXT_NAV_PAGE)
#define TXT_VOLUME_MAX 256u
typedef struct {
    uint16_t begin, end, title, number; ///< 有界卷范围；UINT16_MAX为自动卷名 / Bounded volume range; UINT16_MAX means an automatic title
} book_txt_volume_t;
typedef struct {
    uint32_t offset; ///< 原文件偏移 / Original file offset
    uint32_t title_offset; ///< 标题原始位置；UINT32_MAX为兼容分段 / Source title position or a legacy part
} book_entry_t;
typedef struct {
    FILE *file; ///< 文件句柄 / File handle
    book_entry_t *entries; ///< 保留旧章节编号的紧凑定位 / Compact locations retaining legacy chapter IDs
    struct book_txt_nav *navigation[TXT_NAV_PAGES]; ///< 按需分配1KiB页 / Demand-allocated1KiB pages
    struct book_txt_titles *titles; ///< 可释放的八条标题缓存 / Releasable eight-title cache
    book_txt_volume_t *volumes; ///< 有分卷时才分配，最多2KiB / Allocate only for volumes, at most2KiB
    size_t volume_count;
    bool volumes_incomplete; ///< 卷表不足时保留平铺目录且不缓存残缺结果 / Flat fallback without caching incomplete volume metadata
    size_t nav_count;
    size_t mapped_chapter, mapped_begin, mapped_end;
    bool mapped; ///< 当前正文分段的目录UTF8定位已就绪 / Current body segment has UTF8 navigation positions
    bool nav_incomplete; ///< 资源不足时保留旧目录，稍后可重建 / Retain legacy navigation on resource exhaustion
    size_t count; ///< 目录条数 / Entry count
    uint32_t total; ///< 文件长度 / File length
    bool gbk; ///< GBK 编码 / GBK encoding
#if KIIKO_BOARD_METALIO47
    uint8_t utf16; ///< 0=非UTF16，1=小端，2=大端 / 0=other,1=LE,2=BE
#endif
    uint32_t bom; ///< BOM 字节数 / BOM size
} book_txt_t;
/// 建立 TXT 目录。/ Build the TXT index.
esp_err_t book_txt_open(book_txt_t *book, const char *path);
/// 加载一章并转为 UTF-8。/ Load a chapter as UTF-8.
esp_err_t book_txt_load(book_txt_t *book, size_t i, char **utf8, size_t *len);

/// 释放文件及有界目录页；浅睡仍由书源暂停原文件。/ Release the file and bounded index pages; source sleep still suspends the original file.
void book_txt_close(book_txt_t *book);
size_t book_txt_nav_chapter(const book_txt_t *book, size_t position);
size_t book_txt_nav_source_offset(const book_txt_t *book, size_t position);
esp_err_t book_txt_title(book_txt_t *book, size_t i, bool navigation, char *buf, size_t cap);
esp_err_t book_txt_load_target(book_txt_t *book, size_t i, size_t source_offset,
                               size_t *text_offset, char **utf8, size_t *len);
size_t book_txt_nav_position(const book_txt_t *book,size_t chapter,size_t text_offset);

/// 只读与文件身份匹配的已有索引；未知/过期/损坏返回0，不打开或解析书籍。
/// Read an identity-matching existing index only; unknown/stale/corrupt returns zero without parsing a book.
uint16_t book_txt_cached_chapter_count(const char *path,uint32_t size,int64_t modified);

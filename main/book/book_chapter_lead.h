/* 中文：从 EPUB 章首与目录生成阅读题头。/ English: Build the reader heading from an EPUB's opening blocks and TOC. */
#pragma once

#include "book_chapter_label.h"
#include "html_text.h"

static inline bool epub_lead_line(const html_text_t *text, size_t i, char *out, size_t cap) {
    const blk_t *block = &text->blocks[i];
    if (block->image >= 0 || block->auxiliary || block->linked || !block->len || block->len >= cap)
        return false;
    memcpy(out, text->utf8 + block->offset, block->len);
    out[block->len] = 0;
    return true;
}

static inline void epub_lead_copy(char *out, size_t cap, const char *text, size_t size) {
    if (size >= cap) {
        size = cap - 1;
        while (size && ((unsigned char)text[size] & 0xc0) == 0x80) --size;
    }
    memcpy(out, text, size);
    out[size] = 0;
}

static inline void book_heading_parts(const char *display, chapter_label_t parts, const char *subtitle,
                                      char *title, size_t title_cap, char *label, size_t label_cap) {
    label[0] = 0;
    if (parts.kind && subtitle && *subtitle && (size_t)(parts.end - parts.begin) < label_cap) {
        epub_lead_copy(label, label_cap, parts.begin, (size_t)(parts.end - parts.begin));
        epub_lead_copy(title, title_cap, subtitle, strlen(subtitle));
    } else {
        display = chapter_label_space(display);
        epub_lead_copy(title, title_cap, display, strlen(display));
    }
}

// 有些书只在正文里拉开汉字间距。/ Some books space out Han titles only in the body.
static inline bool epub_chapter_name_equal(const char *a, const char *b) {
    if (chapter_label_text_equal(a, b)) return true;
    for (;;) {
        a = chapter_label_space(a);
        b = chapter_label_space(b);
        if (!*a || !*b) return !*a && !*b;
        const unsigned char first = (unsigned char)*a;
        if (first < 0xe4 || first > 0xe9 || !a[1] || !a[2] || strncmp(a, b, 3))
            return false;
        a += 3;
        b += 3;
    }
}

static inline size_t epub_chapter_lead(const html_text_t *text, const char *toc,
                                       char *title, size_t title_cap, char *label, size_t label_cap) {
    title[0] = label[0] = 0;
    char first[160], line[160];
    size_t start = 0;
    while (start < text->count && epub_lead_line(text, start, first, sizeof(first)) &&
           !*chapter_label_space(first)) ++start;
    if (start == text->count) return 0;
    const blk_t *opening = &text->blocks[start];
    if (opening->image >= 0 || opening->auxiliary) return 0;
    // 只复用已核实的正文题头；卷页由流式分页拥有，不能凭目录文字造题头。
    // Reuse verified body headings only; volume pages belong to flow layout, never synthesize from a TOC label alone.
    if (!opening->reader_heading || opening->volume_start || chapter_label_auxiliary(toc)) return 0;
    if (opening->authored_heading_layout && (opening->chapter_start || opening->heading_level)) return 0;
    const char *display = chapter_label_space(toc);
    size_t skip = start;
    bool opening_label = false;
    if (epub_lead_line(text, start, first, sizeof(first))) {
        if (chapter_label_auxiliary(first)) return 0;
        const chapter_label_t body = chapter_label_parse(first, opening->heading);
        const chapter_label_t nav = chapter_label_parse(display, true);
        opening_label = opening->chapter_start || (opening->heading && body.kind) ||
                        (*display && (chapter_label_repeats(display, first, opening->heading) ||
                                      (opening->heading && epub_chapter_name_equal(display, first))));
        if (opening_label) {
            if (opening->authored_heading_layout) return 0;
            if (!chapter_label_same_number(body, nav) || *body.subtitle || !*nav.subtitle) display = first;
            ++skip;
        } else if (opening->heading) {
            return 0;
        }
    }
    if (!opening_label || !*display) return 0;
    chapter_label_t parts = chapter_label_parse(display, true);
    const char *subtitle = parts.kind ? parts.subtitle : NULL;
    char split[160];
    if (opening_label && parts.kind && !*subtitle && skip < text->count &&
        text->blocks[skip].heading && !text->blocks[skip].chapter_start &&
        epub_lead_line(text, skip, split, sizeof(split)) &&
        !chapter_label_parse(split, true).kind) {
        if (text->blocks[skip].authored_heading_layout) return 0;
        subtitle = chapter_label_space(split);
        if (*subtitle) ++skip;
    }
    while (skip && skip < text->count && skip - start < 4 &&
           epub_lead_line(text, skip, line, sizeof(line))) {
        const blk_t *block = &text->blocks[skip];
        if (!block->repeated_heading && !chapter_label_repeats(display, line, block->heading) &&
            !(subtitle && *subtitle && chapter_label_text_equal(subtitle, line))) break;
        ++skip;
    }
    if (skip < text->count && text->blocks[skip].image >= 0) return 0;
    book_heading_parts(display, parts, subtitle, title, title_cap, label, label_cap);
    return skip < text->count ? text->blocks[skip].offset : text->len;
}

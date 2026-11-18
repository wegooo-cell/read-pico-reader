/* Shared reader chapter lookup. / 双机共用正文位置的章节识别。 */
#include "../book/book_chapter_label.h"
#if defined(ESP_PLATFORM) && !KIIKO_BOARD_METALIO47 && defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif
static void reader_current_chapter(char *dst, size_t cap) {
    if (!cap) return;
    dst[0] = 0;
    const size_t offset = reader_page_offset(s_page);
    if (book_kind() == BOOK_KIND_TXT) {
        const size_t toc = book_navigation_position(s_chapter, offset);
        // A logical TXT chapter can span multiple physical file segments.
        if (toc != SIZE_MAX) (void)book_navigation_title(toc, dst, cap);
        return;
    }

    const bool selected = s_selected_toc < book_navigation_count() &&
        book_navigation_chapter(s_selected_toc) == s_chapter && offset >= s_selected_toc_offset;
    if (selected && book_navigation_title(s_selected_toc, dst, cap) != ESP_OK) dst[0] = 0;
    if (!dst[0] && book_chapter_title(s_chapter, dst, cap) != ESP_OK) dst[0] = 0;

    // EPUB may contain several verified body chapter headings in the same XHTML.
    size_t first_after = 0, end = s_block_count;
    while (first_after < end) {
        const size_t middle = first_after + (end - first_after) / 2;
        if (s_blocks[middle].offset <= offset) first_after = middle + 1;
        else end = middle;
    }
    while (first_after > 0) {
        const blk_t *block = &s_blocks[--first_after];
        if (selected && block->offset < s_selected_toc_offset) break;
        if (!block->chapter_start || !block->len) continue;
        size_t bytes = block->len;
        if (first_after + 1 < s_block_count && s_blocks[first_after + 1].heading_subtitle) {
            const blk_t *subtitle = &s_blocks[first_after + 1];
            bytes = subtitle->offset + subtitle->len - block->offset;
        }
        char current[193];
        size_t length = bytes < sizeof(current) ? bytes : sizeof(current) - 1;
        const char *heading = s_text + block->offset;
        while (length && ((unsigned char)heading[length] & 0xc0) == 0x80) --length;
        memcpy(current, heading, length);
        current[length] = 0;
        for (size_t i = 0; i < length; ++i)
            if (current[i] == '\r' || current[i] == '\n') current[i] = ' ';
        chapter_label_t original = chapter_label_parse(dst, true), actual = chapter_label_parse(current, true);
        // 目录含副题、正文只写章号时保留更完整的目录名称。
        // Keep the fuller TOC label when the body repeats only its number.
        if (!(chapter_label_same_number(original, actual) && *original.subtitle && !*actual.subtitle)) {
            length = strlen(current);
            if (length >= cap) length = cap - 1;
            while (length && ((unsigned char)current[length] & 0xc0) == 0x80) --length;
            memcpy(dst, current, length);
            dst[length] = 0;
        }
        break;
    }
    if (!strcmp(dst, "本章") || !strcmp(dst, "章节")) dst[0] = 0;
}
#if defined(ESP_PLATFORM) && !KIIKO_BOARD_METALIO47 && defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

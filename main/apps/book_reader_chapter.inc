/* Shared reader chapter lookup. / 双机共用正文位置的章节识别。 */
static void reader_current_chapter(char *dst, size_t cap) {
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
        if ((selected && block->offset <= s_selected_toc_offset) ||
            (!block->offset && dst[0])) break;
        if (!block->chapter_start || !block->len) continue;
        size_t length = block->len < cap ? block->len : cap - 1;
        const char *heading = s_text + block->offset;
        while (length && ((unsigned char)heading[length] & 0xc0) == 0x80) --length;
        memcpy(dst, heading, length);
        dst[length] = 0;
        break;
    }
    if (!strcmp(dst, "本章") || !strcmp(dst, "章节")) dst[0] = 0;
}

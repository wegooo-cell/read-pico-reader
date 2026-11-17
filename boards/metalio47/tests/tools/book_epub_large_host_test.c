/* SPDX-License-Identifier: Apache-2.0 */
/* Validate bounded opening, navigation, and distant chapters in a long EPUB. */
#include "book_epub.h"
#include "zip_reader.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Keep the font engine's failed-load ownership while testing the EPUB container on the host.
int ttf_font_open_mem(uint8_t *data, size_t len, const char *label) {
    (void)len; (void)label;
    free(data);
    return -1;
}
void ttf_font_close_embedded(void) {}

static void test_segmented_book(const char *path) {
    book_epub_t *book = NULL;
    assert(book_epub_open(path, &book) == ESP_OK);
    html_text_t page = {0};
    size_t local = 0;
    assert(book_epub_load_part(book, 0, 0, false, NULL, SIZE_MAX, &local, &page) == ESP_OK);
    assert(page.part_more && page.part_base == 0 && page.source_end < 24000 && local == 0);
    assert(strstr(page.utf8, "MARK0000") && !strstr(page.utf8, "MARK1999"));
    assert(book_epub_suspend_io(book) == ESP_OK);
    assert(book_epub_resume_io(book) == ESP_OK);
    size_t previous_end = page.source_end;
    size_t next = page.len + 1;
    html_text_free(&page);
    assert(book_epub_load_part(book, 0, next, false, NULL, SIZE_MAX, &local, &page) == ESP_OK);
    assert(page.part_more && page.source_begin == previous_end && page.part_base == next && local == 0);
    bool centered = false;
    for (size_t i = 0; i < page.count; ++i) centered |= page.blocks[i].align == 1;
    assert(centered);
    size_t last = page.part_base + 10;
    html_text_free(&page);
    assert(book_epub_load_part(book, 0, last, false, NULL, SIZE_MAX, &local, &page) == ESP_OK);
    assert(local == 10 && page.part_base == next);
    html_text_free(&page);
    assert(book_epub_load_part(book, 0, next - 1, false, NULL, SIZE_MAX, &local, &page) == ESP_OK);
    assert(!page.source_begin && local > 0);
    html_text_free(&page);
    assert(book_epub_load_part(book, 0, 0, false, "later", SIZE_MAX, &local, &page) == ESP_OK);
    assert(page.target_found && page.source_begin && strstr(page.utf8 + local, "MARK1999") == page.utf8 + local);
    html_text_free(&page);
    assert(book_epub_load_part(book, 0, 0, true, NULL, SIZE_MAX, &local, &page) == ESP_OK);
    assert(!page.part_more && page.source_begin && strstr(page.utf8, "END OF CHAPTER"));
    html_text_free(&page);
    size_t cursor = 0, markers = 0;
    do {
        assert(book_epub_load_part(book, 0, cursor, false, NULL, SIZE_MAX, &local, &page) == ESP_OK);
        assert(page.part_base == cursor && !local);
        for (const char *mark = page.utf8; (mark = strstr(mark, "MARK")); mark += 8) {
            char expected[10];
            snprintf(expected, sizeof(expected), "MARK%04u", (unsigned)markers);
            assert(!memcmp(mark, expected, 8));
            ++markers;
        }
        cursor = page.part_more ? cursor + page.len + 1 : 0;
        html_text_free(&page);
    } while (cursor);
    assert(markers == 2300);
    unsigned w, h;
    size_t bytes;
    assert(book_epub_image_info(book, 0, "../big.png", &w, &h, &bytes) == ESP_OK);
    assert(w == 1800 && h == 1600 && bytes > 384 * 1024);
    uint8_t *image = NULL;
    size_t size = 0;
    bool png = false;
    assert(book_epub_image_bounded(book, 0, "../big.png", &image, &size, &png,
                                   bytes - 1) == ESP_ERR_NO_MEM);
    assert(!image && !size);
    assert(book_epub_image_bounded(book, 0, "../big.png", &image, &size, &png,
                                   bytes) == ESP_OK);
    assert(image && size == bytes && png);
    free(image);
    assert(book_epub_image_info(book, 0, "../small.svg", &w, &h, &bytes) == ESP_OK);
    assert(w == 120 && h == 90 && bytes < 384 * 1024);
    book_epub_close(book);
}

static void test_stream(const char *path, bool corrupt) {
    zip_reader_t *zip = NULL;
    assert(zip_open(path, &zip) == ESP_OK);
    int index = zip_find(zip, "OPS/text/long.xhtml");
    size_t length = zip_entry_size(zip, index);
    zip_stream_t *stream = NULL;
    assert(zip_stream_open(zip, index, &stream) == ESP_OK);
    size_t consumed = 0;
    esp_err_t err;
    uint8_t output[517];
    for (;;) {
        size_t read = 0;
        err = zip_stream_read(stream, output, sizeof(output), &read);
        if (err != ESP_OK || !read) break;
        if (!consumed) assert(!memcmp(output, "<html>", 6));
        consumed += read;
    }
    assert(corrupt ? err != ESP_OK : (err == ESP_OK && consumed == length));
    zip_stream_close(stream);
    zip_close(zip);
}

int main(int argc, char **argv) {
    assert(argc == 5);
    book_epub_t *book = NULL;
    assert(book_epub_open(argv[1], &book) == ESP_OK && book);
    assert(book_epub_chapter_count(book) == 5000);
    assert(book_epub_suspend_io(book) == ESP_OK);
    assert(book_epub_chapter_count(book) == 5000);
    assert(book_epub_resume_io(book) == ESP_OK);
    assert(book_epub_navigation_count(book) == 5000);
    assert(book_epub_navigation_chapter(book, 4999) == 4999);
    char title[160];
    assert(book_epub_navigation_title(book, 4999, title, sizeof(title)) == ESP_OK);
    assert(!strcmp(title, "第5000章 终点"));
    html_text_t page = {0};
    assert(book_epub_load(book, 4999, &page) == ESP_OK);
    assert(strstr(page.utf8, "终点") != NULL);
    html_text_free(&page);
    book_epub_close(book);
    for (int i = 2; i < 4; ++i) {
        test_segmented_book(argv[i]);
        test_stream(argv[i], false);
    }
    test_stream(argv[4], true);
    puts("large EPUB and segmented chapter host tests passed");
    return 0;
}

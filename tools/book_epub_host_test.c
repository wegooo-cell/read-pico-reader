/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB 容器、spine、目录与正文集成测试。
 * English: EPUB container, spine, navigation and text integration tests.
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "book_epub.h"
#include "book_chapter_lead.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Match the source tests' font fallback and ownership without linking the hardware font engine.
int ttf_font_open_mem(uint8_t *data, size_t len, const char *label) {
    (void)len; (void)label;
    free(data);
    return -1;
}
void ttf_font_close_embedded(void) {}
int main(int argc, char **argv) {
    assert(argc > 1);
    for (int a = 1; a < argc; ++a) {
        book_epub_t *book = NULL;
        if (strstr(argv[a], "/bad_")) {
            assert(book_epub_open(argv[a], &book) != ESP_OK && !book);
            printf("epub rejection passed: %s\n", argv[a]); continue;
        }
        assert(book_epub_open(argv[a], &book) == ESP_OK && book);
        if (strstr(argv[a], "good_heading_flow")) {
            const bool native = strstr(argv[a], "native") != NULL;
            const bool only = strstr(argv[a], "volume_only") != NULL;
            const bool bare = strstr(argv[a], "bare") != NULL;
            html_text_t text={0};
            assert(book_epub_load(book,0,&text)==ESP_OK);
            unsigned volumes=0,chapters=0,subtitles=0;
            for(size_t i=0;i<text.count;++i){
                const blk_t *block=&text.blocks[i];
                char title[160];memcpy(title,text.utf8+block->offset,block->len);title[block->len]=0;
                if(block->volume_start){
                    assert(!strcmp(title,"第一卷"));++volumes;
                    assert(block->reader_heading==(native?BOOK_HEADING_NATIVE:BOOK_HEADING_EPUB));
                }else if(block->chapter_start){
                    assert(!strcmp(title,bare?"1":"第一章 起点")||!strcmp(title,"第二章"));++chapters;
                }
                if(block->heading_subtitle){assert(!strcmp(title,"初见")||!strcmp(title,"归来"));++subtitles;}
            }
            if(!(volumes==1 && chapters==(only?0u:2u) && subtitles==(native?1u:only?1u:2u)))fprintf(stderr,"FLOW %s volumes=%u chapters=%u subtitles=%u\n",argv[a],volumes,chapters,subtitles);
            assert(volumes==1 && chapters==(only?0u:2u) && subtitles==(native?1u:only?1u:2u));
            char title[128],label[32];
            assert(!epub_chapter_lead(&text,"目录",title,sizeof(title),label,sizeof(label)) && !title[0]);
            assert(!epub_chapter_lead(&text,"第一卷 初见",title,sizeof(title),label,sizeof(label)) && !title[0]);
            html_text_free(&text);book_epub_close(book);
            puts("epub front matter, volume/subtitle, following chapters, native styling and no phantom TOC lead passed");
            continue;
        }
        if (strstr(argv[a], "good_ncx_numbered_with_extra_pages")) {
            const char *labels[] = {"11", "12", "17", "作家的话"};
            const size_t destinations[] = {0, 1, 3, 4};
            assert(book_epub_chapter_count(book) == 5);
            assert(book_epub_navigation_count(book) == 4);
            for (size_t i = 0; i < 4; ++i) {
                char title[160];
                assert(book_epub_navigation_chapter(book, i) == destinations[i]);
                assert(book_epub_navigation_title(book, i, title, sizeof(title)) == ESP_OK);
                assert(!strcmp(title, labels[i]));
                assert(book_epub_navigation_source_offset(book, i) == SIZE_MAX);
                const char *anchor = book_epub_navigation_anchor(book, i);
                assert(anchor && !strcmp(anchor, "head"));
                html_text_t text = {0};
                size_t offset = SIZE_MAX;
                assert(book_epub_load_anchor(book, destinations[i], anchor, &offset, &text) == ESP_OK);
                assert(offset == 0 && text.blocks[0].heading);
                html_text_free(&text);
            }
            book_epub_close(book);
            puts("epub authored numeric labels, afterword, anchors and unlisted illustration passed");
            continue;
        }
        if (strstr(argv[a], "good_numbered_")) {
            bool volumes = strstr(argv[a], "volumes") != NULL;
            const char *titles[] = {"1", "二", "第３章 ： 夜雨", "第十二章"};
            const char *grouped[] = {"第一卷 出发", "一", "卷二：归来", "４"};
            assert(book_epub_navigation_count(book) == 4);
            assert(book_epub_navigation_volume_count(book) == (volumes ? 2u : 0u));
            for (size_t i = 0; i < 4; ++i) {
                char toc[160], title[128], label[32];
                assert(book_epub_navigation_chapter(book, i) == i);
                assert(book_epub_navigation_title(book, i, toc, sizeof(toc)) == ESP_OK);
                assert(!strcmp(toc, volumes ? grouped[i] : titles[i]));
                html_text_t text = {0};
                size_t offset = 0;
                const char *anchor = book_epub_navigation_anchor(book, i);
                if (volumes) assert(anchor && !strcmp(anchor, "origin"));
                assert(book_epub_load_target(book, i, anchor, book_epub_navigation_source_offset(book, i),
                                             &offset, &text) == ESP_OK);
                assert(offset == 0 && text.count == 4 && text.blocks[0].chapter_start);
                assert(text.blocks[1].repeated_heading && !text.blocks[2].repeated_heading);
                assert(!text.blocks[3].chapter_start && !text.blocks[3].repeated_heading);
                size_t skip = epub_chapter_lead(&text, toc, title, sizeof(title), label, sizeof(label));
                if(volumes && i%2==0)assert(!skip&&!title[0]&&text.blocks[0].volume_start);
                else {
                    assert(skip == text.blocks[2].offset);
                    assert(!strncmp(text.utf8 + skip, "第一章说到的人", strlen("第一章说到的人")));
                }
                if (!volumes && i == 2) assert(!strcmp(title, "夜雨") && !strcmp(label, "第３章"));
                if (!volumes && i == 0) assert(!strcmp(title, "1") && !label[0]);
                if (volumes && i % 2 == 0) {
                    book_volume_t group;
                    assert(book_epub_navigation_volume(book, i / 2, &group));
                    assert(group.begin == i && group.end == i + 2 && group.title == i && group.include_title);
                }
                html_text_free(&text);
            }
            book_epub_close(book);
            printf("epub numbered labels, repeat blocks, anchors and volumes passed: %s\n", argv[a]);
            continue;
        }
        if (strstr(argv[a], "good_bounded_cover")) {
            book_epub_close(book);
            uint8_t *cover = NULL; size_t size = 0; bool png = false;
            for (size_t budget = 0; budget < 128; budget += 127) {
                assert(book_epub_cover_bounded(argv[a], &cover, &size, &png, budget) == ESP_ERR_INVALID_SIZE);
                assert(!cover && !size && !png);
            }
            assert(book_epub_cover_bounded(argv[a], &cover, &size, &png, 128) == ESP_OK);
            assert(cover && size == 128 && png);
            for (unsigned i = 0; i < 128; ++i) assert(cover[i] == i);
            free(cover);
            printf("epub cover budget/cleanup fixture passed: %s\n", argv[a]);
            continue;
        }
        if (strstr(argv[a], "good_chapter_breaks_")) {
            bool auxiliary = strstr(argv[a], "auxiliary") != NULL;
            bool named = strstr(argv[a], "named") != NULL;
            assert(book_epub_chapter_count(book) == 1);
            assert(book_epub_navigation_count(book) == 2);
            char title[160];
            assert(book_epub_navigation_title(book, 0, title, sizeof(title)) == ESP_OK);
            assert(!strcmp(title, named ? "出发" : "第一章 起点"));
            html_text_t text = {0}; size_t offset = 0;
            assert(book_epub_load_anchor(book, 0, "two", &offset, &text) == ESP_OK);
            size_t marked = 0;
            for (size_t i = 0; i < text.count; ++i) {
                const blk_t *block = &text.blocks[i];
                if (block->chapter_start) {
                    assert(!block->linked && !block->auxiliary && block->heading_level == 1);
                    const char *expected = marked ? (named ? "归来" : "第2章 第二站") : (named ? "出发" : "第一章 起点");
                    assert(block->len == strlen(expected));
                    assert(!memcmp(text.utf8 + block->offset, expected, block->len));
                    if (marked) assert(offset == block->offset);
                    ++marked;
                }
            }
            assert(marked == (auxiliary ? 0u : 2u));
            html_text_free(&text);
            book_epub_close(book);
            printf("epub chapter boundary fixture passed: %s\n", argv[a]);
            continue;
        }
        if (strstr(argv[a], "good_dual_samefile_partial_ncx")) {
            assert(book_epub_chapter_count(book) == 1);
            assert(book_epub_navigation_count(book) == 2);
            for (size_t i = 0; i < 2; ++i) {
                assert(book_epub_navigation_chapter(book, i) == 0);
                char title[160];
                assert(book_epub_navigation_title(book, i, title, sizeof(title)) == ESP_OK);
                assert(!strcmp(title, i ? "第二章 继续" : "第一章 起点"));
                assert(book_epub_navigation_anchor(book, i) &&
                       !strcmp(book_epub_navigation_anchor(book, i), i ? "two" : "one"));
            }
            book_epub_close(book);
            printf("epub dual-TOC same-file fixture passed: %s\n", argv[a]);
            continue;
        }
        if (strstr(argv[a], "good_ncx_")) {
            bool corrupt = strstr(argv[a], "corrupt") != NULL;
            assert(book_epub_chapter_count(book) == 4);
            assert(book_epub_navigation_count(book) == (corrupt ? 2 : 4));
            for (size_t i = 0; i < (corrupt ? 2u : 4u); ++i) {
                size_t expected_chapter = corrupt ? i * 2 : i;
                assert(book_epub_navigation_chapter(book, i) == expected_chapter);
                char title[160];
                assert(book_epub_navigation_title(book, i, title, sizeof(title)) == ESP_OK);
                const char *source = corrupt || i % 2 == 0 ? "NCX" : "NAV";
                char expected[32];
                snprintf(expected, sizeof(expected), "%s %u", source,
                         (unsigned)expected_chapter + 1);
                assert(!strcmp(title, expected));
            }
            book_epub_close(book);
            printf("epub partial-navigation fixture passed: %s\n", argv[a]);
            continue;
        }
        if (strstr(argv[a], "good_no_cover")) {
            uint8_t *cover = NULL; size_t cover_size = 0; bool png = false;
            assert(book_epub_cover(argv[a], &cover, &cover_size, &png) == ESP_ERR_NOT_FOUND);
            assert(!cover && !cover_size && !png);
        }
        if (strstr(argv[a], "good_heading_banner")) {
            html_text_t text = {0};
            assert(book_epub_load(book,0,&text)==ESP_OK && text.count==5);
            assert(text.blocks[0].image==0 && text.blocks[1].chapter_start);
            assert(text.blocks[1].reader_heading==BOOK_HEADING_NATIVE && text.blocks[1].align==1);
            assert(text.blocks[1].margin_before_percent==100 && text.blocks[3].reader_heading==BOOK_HEADING_EPUB);
            char title[128],label[32];
            assert(epub_chapter_lead(&text,"第一章 起点",title,sizeof(title),label,sizeof(label))==0);
            assert(!title[0] && !label[0]);
            html_text_free(&text);
            book_epub_close(book);
            puts("epub illustrated chapter opening keeps its original heading");
            continue;
        }
        if (strstr(argv[a], "good_heading_priority")) {
            html_text_t text = {0};
            assert(book_epub_load(book,0,&text)==ESP_OK && text.count==5);
            assert(text.blocks[0].authored_style && text.blocks[0].font_percent==160 &&
                   text.blocks[0].align==2 && text.blocks[0].margin_after_percent==100 &&
                   text.blocks[0].reader_heading==BOOK_HEADING_NATIVE);
            assert(text.blocks[1].repeated_heading);
            assert(text.blocks[3].reader_heading==BOOK_HEADING_EPUB && !text.blocks[3].authored_style);
            char title[128],label[32];
            assert(epub_chapter_lead(&text,"第一章 起点",title,sizeof(title),label,sizeof(label))==0);
            assert(!title[0] && !label[0]);
            html_text_free(&text);
            book_epub_set_fonts_enabled(book, false);
            assert(book_epub_load(book,0,&text)==ESP_OK && text.count==5);
            assert(text.blocks[0].chapter_start && text.blocks[0].reader_heading==BOOK_HEADING_EPUB);
            assert(text.blocks[0].authored_style && !text.blocks[0].authored_heading_layout);
            assert(text.blocks[1].repeated_heading);
            assert(epub_chapter_lead(&text,"第一章 起点",title,sizeof(title),label,sizeof(label))==text.blocks[2].offset);
            assert(!strcmp(title,"起点") && !strcmp(label,"第一章"));
            html_text_free(&text);
            book_epub_set_fonts_enabled(book, true);
            assert(book_epub_load(book,0,&text)==ESP_OK && text.blocks[0].authored_heading_layout &&
                   text.blocks[0].reader_heading==BOOK_HEADING_NATIVE);
            html_text_free(&text);
            book_epub_close(book);
            puts("epub author CSS takes precedence; unstyled same-file chapter uses reader heading");
            continue;
        }
        if (strstr(argv[a], "good_body_")) {
            bool samefile = strstr(argv[a], "samefile") != NULL;
            bool partial = strstr(argv[a], "partial") != NULL;
            bool false_positive = strstr(argv[a], "false_positive") != NULL;
            assert(book_epub_chapter_count(book) == (samefile ? 1 :
                strstr(argv[a], "priority") || strstr(argv[a], "without_nav") ||
                strstr(argv[a], "ideographic") ? 5 : 4));
            assert(book_epub_navigation_count(book) == (samefile ? 2 : 4));
            char title[160];
            assert(book_epub_navigation_title(book, 0, title, sizeof(title)) == ESP_OK);
            if (false_positive) assert(!strcmp(title, "Parent & One"));
            else assert(!strcmp(title, "第一章 起点"));
            if (!partial && !false_positive) {
                assert(book_epub_navigation_title(book, 1, title, sizeof(title)) == ESP_OK);
                assert(!strcmp(title, strstr(argv[a], "ideographic") ? "第2章　第二站" : "第2章 第二站"));
                for (size_t i = 0; i < (samefile ? 2 : 4); ++i) {
                    size_t chapter = book_epub_navigation_chapter(book, i);
                    size_t source = book_epub_navigation_source_offset(book, i);
                    html_text_t text = {0}; size_t offset = 0;
                    assert(source != SIZE_MAX);
                    assert(book_epub_navigation_anchor(book, i) == NULL);
                    assert(book_epub_load_target(book, chapter, NULL, source, &offset, &text) == ESP_OK);
                    assert(offset < text.len);
                    assert(text.utf8[offset] == (char)0xe7);
                    html_text_free(&text);
                }
            } else if (partial) {
                assert(book_epub_navigation_title(book, 1, title, sizeof(title)) == ESP_OK);
                assert(!strcmp(title, "Child Two"));
            }
            book_epub_close(book); printf("epub fixture passed: %s\n", argv[a]); continue;
        }
        if (strstr(argv[a], "good_multianchor")) {
            assert(book_epub_chapter_count(book) == 1);
            assert(book_epub_navigation_count(book) == 2);
            char first_title[160];
            assert(book_epub_chapter_title(book, 0, first_title, sizeof(first_title)) == ESP_OK);
            assert(!strcmp(first_title, "第一章 起点"));
            for (size_t i = 0; i < 2; ++i) {
                char title[160]; html_text_t text = {0}; size_t offset = 0;
                assert(book_epub_navigation_chapter(book, i) == 0);
                assert(book_epub_navigation_title(book, i, title, sizeof(title)) == ESP_OK);
                assert(!strncmp(title, i ? "第二章" : "第一章", strlen("第一章")));
                const char *anchor = book_epub_navigation_anchor(book, i);
                assert(anchor && !strcmp(anchor, i ? "two" : "one"));
                assert(book_epub_load_anchor(book, 0, anchor, &offset, &text) == ESP_OK);
                assert(offset > 0 && offset < text.len);
                assert(strstr(text.utf8 + offset, i ? "第二章" : "第一章") == text.utf8 + offset);
                html_text_free(&text);
            }
            book_epub_close(book); printf("epub fixture passed: %s\n", argv[a]); continue;
        }
        bool frontmatter = strstr(argv[a], "good_frontmatter") != NULL;
        size_t chapters = frontmatter ? 5 : 4;
        assert(book_epub_chapter_count(book) == chapters);
        assert(book_epub_navigation_count(book) == 4);
        for (size_t i = 0; i < 4; ++i)
            assert(book_epub_navigation_chapter(book, i) == i + (frontmatter ? 1 : 0));
        assert(book_epub_navigation_chapter(book, 4) == SIZE_MAX);
        uint32_t previous = 0;
        for (size_t i = 0; i < chapters; ++i) {
            char title[160]; html_text_t text = {0};
            assert(book_epub_chapter_title(book, i, title, sizeof(title)) == ESP_OK);
            // 既无题头也无目录名的页不再编造「第 N 节」。/ Pages with neither heading nor TOC label stay unnamed.
            assert(title[0] || (strstr(argv[a], "good_resources") && i == 0));
            if (title[0]) {
            if (frontmatter && i == 0) assert(!strcmp(title, "作者信息"));
            else if (strstr(argv[a], "good_defaults") || (frontmatter && i > 0 && strstr(argv[a], "fallback")))
                assert(!strncmp(title, "Chapter ", strlen("Chapter ")));
            else assert(strncmp(title, "第 ", strlen("第 ")));
            if (strstr(argv[a], "good_paths") && i < 2) assert(!strcmp(title, i ? "Child Two" : "Parent & One"));
            if (strstr(argv[a], "good_navfallback") || strstr(argv[a], "good_navrole") ||
                strstr(argv[a], "good_navtype") || strstr(argv[a], "good_navunmarked"))
                assert(!strncmp(title, "NAV ", 4));
            }
            uint32_t offset = book_epub_chapter_byte_offset(book, i);
            assert(i ? offset > previous : offset == 0); previous = offset;
            assert(book_epub_load(book, i, &text) == ESP_OK);
            assert(text.utf8 && text.len && text.blocks && text.count);
            if (strstr(argv[a], "good_resources") && i == 0) {
                assert(text.image_count == 1 && !strcmp(text.images[0], "../images/pic&one.png"));
                uint8_t *image = NULL; size_t size = 0; bool png = false;
                assert(book_epub_image(book, i, text.images[0], &image, &size, &png) == ESP_OK);
                assert(image && size == 24 && png); free(image);
                assert(book_epub_image(book, i, "../images/wrapper.svg", &image, &size, &png) == ESP_OK);
                assert(image && size == 24 && png); free(image);
                unsigned width = 0, height = 0;
                const char *resources[] = {text.images[0], "../images/wrapper.svg", "../images/late.jpg"};
                for (size_t r = 0; r < sizeof(resources) / sizeof(resources[0]); ++r) {
                    assert(book_epub_image_dimensions(book, i, resources[r], &width, &height) == ESP_OK);
                    assert(width == 80 && height == 20);
                }
                assert(book_epub_image_dimensions(book, i, "../images/missing.png", &width, &height) == ESP_ERR_NOT_FOUND);
                assert(book_epub_image_dimensions(book, i, "../images/broken.jpg", &width, &height) != ESP_OK);
                assert(book_epub_image_dimensions(book, i, "../images/unused.bin", &width, &height) == ESP_ERR_INVALID_SIZE);
                assert(text.blocks[0].align == 1);
            }
            html_text_free(&text);
        }
        assert(book_epub_total_bytes(book) > previous);
        book_epub_close(book); printf("epub fixture passed: %s\n", argv[a]);
    }
    puts("epub host tests passed");
}

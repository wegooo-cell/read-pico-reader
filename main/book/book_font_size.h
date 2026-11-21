/* SPDX-License-Identifier: Apache-2.0
 * 中文：阅读字号的统一范围；设置、备份、进度和滑杆必须使用相同边界。
 * English: Shared reader type-size limits for settings, backups, progress and slider controls.
 */
#pragma once
#define BOOK_READER_PX_MIN 20
#define BOOK_READER_PX_MAX 120
#define BOOK_READER_PX_DEFAULT 48

// 字格紧挨至原150%上限的两倍；段距每10%一档。/ Touching type cells through twice the former 150% ceiling; paragraph gaps step by 10%.
#define BOOK_READER_LINE_MIN 100
#define BOOK_READER_LINE_MAX 300
#define BOOK_READER_LINE_COUNT (BOOK_READER_LINE_MAX - BOOK_READER_LINE_MIN + 1)
#define BOOK_READER_PARAGRAPH_MIN (-100)
#define BOOK_READER_PARAGRAPH_MAX 100
#define BOOK_READER_PARAGRAPH_STEP 10

// 旧备份投影保留其真实可读范围；完整副本保存精确新值。/ Project only into ranges old readers accept; retain exact new values in the full copy.
static inline int book_reader_legacy_line(int percent) {
    return percent < 110 ? 110 : percent > 150 ? 150 : percent;
}
static inline int book_reader_legacy_paragraph(int percent) {
    static const unsigned char stops[] = {0,5,10,15,20,25,30,40,50,75,100};
    int best = 0, distance = 1000;
    for (unsigned i = 0; i < sizeof(stops); ++i) {
        int delta = percent - stops[i];
        if (delta < 0) delta = -delta;
        if (delta < distance) { best = stops[i]; distance = delta; }
    }
    return best;
}

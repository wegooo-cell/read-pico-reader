/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 句子级划线想法：按章拉取（underlines + readReviews 直连接口），
 * 逐章原子落盘断点续传；读取桥供 UI 点击弹窗。
 * Sentence-level highlights: per-chapter fetch with atomic resume caches;
 * read bridge for the reader tap popup.
 * 冻结：只读公开数据，无标注写操作；缓存独立于引擎 browse 体系。
 * Frozen: read-only public data, no annotation writes; cache is separate from the engine browse store.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define WEREAD_NOTE_CONTENT_CAP 512
#define WEREAD_NOTE_AUTHOR_CAP 96
#define WEREAD_NOTE_RANGE_CAP 64
#define WEREAD_NOTE_TEXT_CAP 512

typedef struct {
    char content[WEREAD_NOTE_CONTENT_CAP]; ///< 想法正文 / Thought body
    char author[WEREAD_NOTE_AUTHOR_CAP];   ///< 作者昵称 / Author nickname
    unsigned likes;                        ///< 点赞数 / Like count
} weread_note_t;

typedef struct {
    char range[WEREAD_NOTE_RANGE_CAP]; ///< 码点区间 "a-b" / Codepoint span
    char text[WEREAD_NOTE_TEXT_CAP];   ///< 划线原文（abstract 回填）/ Highlight text
    unsigned review_count;             ///< 该句想法条数 / Thoughts on this sentence
    unsigned index;                    ///< 章内划线序号（review_at 定位用）/ Highlight index for review_at
} weread_highlight_t;

/// 拉取整本句子级划线想法（worker 内调用）：逐章 underlines + readReviews，
/// 每句按 maxIdx 全量翻页（时间换数据：分页限速、逐页流式落盘、去重判停、
/// 单句 200 条保险上限），每章原子落盘；已完成的章自动跳过（断点续传）。
/// progress 汇报章粒度，notes_progress 汇报条粒度（notes_done 累计条数，
/// notes_total 为服务器已知总数累计，未知为 0）。cancel 返回 true 时中止且
/// 保留已完成章。返回 0 = 全部章完成；
/// 非 0 = 状态页错误码（12=目录缺失请先下载，2=网络，3=登录失效，6=存储，10=内存）。
/// / Fetch sentence-level notes for a whole book inside the worker; per-sentence
/// / full paging trades time for completeness (paced, streamed to SD, dedup-guarded,
/// / 200-per-sentence cap). Chapter and per-thought progress callbacks; returns 0 on
/// / success, otherwise a status-page error code (12 missing TOC, 2 network,
/// / 3 auth, 6 storage, 10 memory).
int weread_notes_fetch(const char* book_id,
                       bool (*cancel)(void* ctx), void* cancel_ctx,
                       void (*progress)(void* ctx, unsigned done, unsigned total),
                       void* progress_ctx,
                       void (*notes_progress)(void* ctx, unsigned notes_done, unsigned notes_total),
                       void* notes_ctx);

/// 阅读打开时绑定（EPUB 路径反查 bookId 并加载目录映射）；失败即该书无笔记数据。
/// / Bind on reader open (resolve bookId from the EPUB path and load the TOC map).
bool weread_notes_bind(const char* epub_path);
void weread_notes_unbind(void);
bool weread_notes_ready(void);

/// 绑定成功的云端书号（供引擎 browse 缓存 weread_thoughts_open 使用）。
/// / The bound remote book id, for weread_thoughts_open on the engine browse cache.
bool weread_notes_book_id(char* out, size_t cap);

/// 书级只读统计（书架角标 / 详情页标记用，不依赖 bind）：扫描该书章缓存目录，
/// highlights 为已缓存划线句总数（各章文件头累加），chapters_cached 为已完成
/// 章数；chapters_total 取自 meta（缺失为 0）。返回 false = 无任何已完成章缓存。
/// / Book-level read-only stats for shelf badge / detail marker (no bind needed):
/// / scans the book's chapter cache dir and sums file headers; false when the
/// / book has no completed chapter cache.
bool weread_notes_book_stats(const char* book_id, unsigned* chapters_cached,
                             unsigned* chapters_total, unsigned* highlights);

/// spine 章的云端 chapterUid（review/list 按章缓存定位用）。/ Remote chapter uid of a spine.
bool weread_notes_chapter_uid(uint32_t spine, char* out, size_t cap);

/// spine 章是否已完成拉取。/ True when that chapter's cache file exists.
bool weread_notes_chapter_done(uint32_t spine);

/// 该章已缓存的划线条数；未完成章返回 0。/ Cached highlights of that chapter; 0 when not fetched.
unsigned weread_notes_chapter_highlights(uint32_t spine);

/// 点击行文本 → 该章划线匹配（空白规范化双向包含）。/ Match a tapped line to a cached highlight.
bool weread_notes_match(uint32_t spine, const char* line_utf8, weread_highlight_t* out);

/// 按章内序号读取划线（点击定位用，单次调用）。/ Read a cached highlight by chapter index.
bool weread_notes_highlight_at(uint32_t spine, unsigned index, weread_highlight_t* out);

/// 顺序遍历该章全部划线（一次打开文件读到尾）。装饰层加载全章时必须用它：
/// highlight_at 每次从头扫，O(N²) 读放大在划线密集章（90+ 条）会把 UI 冻结几十秒。
/// fn 返回 false 提前停止。返回读取条数。/ Sequential full-chapter iteration with one
/// open; the decoration layer must use this - per-index reads re-scan from the start
/// (O(N²)) and freeze the UI for tens of seconds on dense chapters.
/// Callback returning false stops early. Returns the number of rows read.
typedef bool (*weread_notes_row_fn)(void* user, const weread_highlight_t* hl);
unsigned weread_notes_for_each_highlight(uint32_t spine, weread_notes_row_fn fn, void* user);

/// 命中划线的想法条数与逐条读取。/ Thought count and per-item read of a matched highlight.
unsigned weread_notes_review_count(uint32_t spine, unsigned highlight);
bool weread_notes_review_at(uint32_t spine, unsigned highlight, unsigned index,
                            weread_note_t* out);

/// 想法流式游标：沿该句想法链顺序逐条读。长句（几十上百条）弹窗必须用它按页
/// 惰性加载——review_at 每条都从链头重走，O(N²) 次随机寻道会让弹窗卡顿数秒。
/// / Streaming thought cursor along the per-sentence chain. Popups with dozens of
/// / thoughts must lazy-load page by page - review_at re-walks from the chain head
/// / per item (O(N²) random seeks) and stalls the popup for seconds.
typedef struct weread_notes_cursor weread_notes_cursor_t;
weread_notes_cursor_t* weread_notes_cursor_open(uint32_t spine, unsigned highlight,
                                                unsigned* total);
bool weread_notes_cursor_next(weread_notes_cursor_t* cur, weread_note_t* out);
void weread_notes_cursor_close(weread_notes_cursor_t* cur);
#ifdef __cplusplus
}
#endif

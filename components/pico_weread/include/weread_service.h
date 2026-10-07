/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 微信读书后台任务与 UI 快照契约。/ WeRead worker and UI snapshot contract.
 * 冻结：只读云端进度与划线想法，无标注写操作；仅成品进入现有书库。
 * Frozen: read-only cloud progress and thoughts, no annotation writes; only finished books enter the library.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WEREAD_ROWS 7
#define WEREAD_BATCH_MAX 1024
typedef enum {
    WEREAD_IDLE, ///< 就绪 / Ready
    WEREAD_CONNECTING, ///< 联网 / Connecting
    WEREAD_WORKING, ///< 同步或下载 / Syncing or downloading
    WEREAD_QR, ///< 等待扫码 / Awaiting scan
    WEREAD_COMPLETE, ///< 完成 / Complete
    WEREAD_FAILED, ///< 失败 / Failed
    WEREAD_CANCELLED, ///< 已取消 / Cancelled
} weread_state_t;
typedef enum {
    WEREAD_LOAD, ///< 读取本地书架 / Load cached shelf
    WEREAD_SYNC, ///< 联网登录与同步 / Login and sync online
    WEREAD_DOWNLOAD, ///< 下载书籍 / Download a book
    WEREAD_LOGOUT, ///< 清除会话与书架 / Clear session and shelf
    WEREAD_BATCH, ///< 串行批量下载 / Sequential batch download
    WEREAD_THOUGHTS, ///< 拉取整本划线想法缓存 / Fetch the whole-book thoughts cache
    WEREAD_NOTES, ///< 按章拉取句子级划线想法（断点续传）/ Per-chapter sentence-level notes fetch
    WEREAD_READ_REPORT, ///< 上传阅读进度+时长（rt，静默失败）/ Upload progress+reading time (rt, silent)
} weread_action_t;
typedef struct {
    char id[64]; ///< 云端书号 / Remote book ID
    char title[192]; ///< 显示书名 / Display title
    char author[96]; ///< 作者 / Author
    char local_path[288]; ///< 已下载路径 / Completed download path
} weread_book_t;
typedef struct {
    char id[64]; ///< 稳定云端书号，跨页或重排序不串书 / Stable ID across pages or reordering
    unsigned index; ///< 缓存索引提示 / Cached index hint
} weread_selection_t;
typedef enum { WEREAD_CHAPTERS, WEREAD_PREPARING, WEREAD_IMAGES, WEREAD_PACKAGING } weread_stage_t;
typedef struct {
    weread_action_t action; ///< 当前操作 / Current operation
    weread_stage_t stage; ///< 下载阶段 / Download stage
    unsigned skipped_images; ///< 无法获取的插图数 / Unavailable illustrations
    weread_state_t state; ///< 生命周期 / Lifecycle
    bool active; ///< 后台任务仍存在 / Worker still running
    bool logged_in; ///< 本地有会话 / Local session exists
    unsigned revision; ///< 状态版本 / Status revision
    unsigned total; ///< 书架总数 / Shelf count
    unsigned page; ///< 零起始页码 / Zero-based page
    unsigned count; ///< 当前页数量 / Visible count
    unsigned done, target; ///< 下载进度 / Download progress
    unsigned notes_done, notes_total; ///< 划线想法条数进度（total 未知为 0）/ Thought counts (0 = total unknown)
    unsigned batch_total, batch_current, batch_success, batch_failed; ///< 批次进度 / Batch counters
    char batch_title[192]; ///< 当前下载书名 / Current download title
    unsigned changed; ///< 成品发布版本 / Completed-publication revision
    int error; ///< 协议或连接错误 / Protocol or connection error
    char qr[320]; ///< 登录确认网址 / Login confirmation URL
    char output[288]; ///< 最近完成文件 / Last completed file
    weread_book_t books[WEREAD_ROWS]; ///< 当前页书籍 / Visible books
} weread_snapshot_t;
/// 已挂载 TF 卡目录；空闲时由 UI 配置。/ UI configures mounted SD roots while idle.
bool weread_configure(const char* cache_root, const char* books_root);
/// 启动一次任务，成功表示已派发。/ Start one worker; true means dispatched.
bool weread_start(weread_action_t action, unsigned page, unsigned index);
/// 按章拉取当前章的 review/list（网友划线+想法）；book_id 为云端书号。
/// / Dispatch a per-chapter review/list fetch for the given remote book and chapter.
bool weread_start_chapter_reviews(const char* book_id, const char* chapter_uid);
/// 按书号整本拉取句子级划线想法（全量翻页、断点续传）；阅读页缓存缺失时调用。
/// / Fetch the whole book's sentence-level notes by book id (full paging, resume);
/// / called from the reader on a chapter-cache miss.
bool weread_start_notes(const char* book_id);
/// 上传一次阅读进度与时长（rt=本次阅读秒数），同步到官方统计；忙时拒绝由调用方稍后重试。
/// elapsed_seconds=0 为纯进度同步（详情页手动「立即同步」，官方 enter 包同形态，无时长）。
/// / Upload one progress+reading-time report (rt = seconds read); caller retries when busy.
/// / elapsed_seconds=0 means progress-only (manual "Sync now"; same shape as the official
/// / enter packet, no reading time attached).
bool weread_start_read_report(const char* book_id, uint16_t chapter, uint32_t byte_off,
                              uint8_t pct, uint32_t elapsed_seconds);
/// 本次开机内最近一次阅读上报的结果；has=false 表示从未尝试。epoch 为设备秒（时钟无效为 0）。
/// / Latest read-report attempt this boot; has=false until the first try. epoch is device
/// / seconds (0 when the clock was invalid at the time).
void weread_last_read_report(bool* has, bool* ok, int64_t* epoch);
/// 复制选择后仅启动一个后台任务，逐本下载；失败书不阻塞后续，取消停止整个队列。
/// Copy selection into one worker; download sequentially, continue after book errors, cancel the whole queue.
bool weread_start_batch(unsigned page, const weread_selection_t* selection, unsigned count);
/// 线程安全读取快照。/ Copy a consistent snapshot across threads.
void weread_snapshot(weread_snapshot_t* out);
/// 下一次下载是否嵌入插图；仅空闲时修改。/ Set inline-image policy while idle.
bool weread_set_include_images(bool enabled);
/// 请求取消并等待清理；返回后可离页或卸载介质。/ Cancel and join cleanup before page exit or media teardown.
void weread_stop(void);
/// 空闲时清除停机后遗留的取消标志；忙时拒绝。weread_stop 会永久置位该标志并拦截
/// 全部 HalFile 读取（含离线绑定反查），此处仅在无后台任务时复位。
/// Clear the stale cancel flag left by weread_stop while idle; refuse when busy.
bool weread_cancel_clear_if_idle(void);

#define WEREAD_THOUGHT_HIGHLIGHTS 0 ///< 热门划线 / Popular highlights
#define WEREAD_THOUGHT_MINE 1       ///< 我的划线 / My highlights
#define WEREAD_THOUGHT_REVIEWS 2    ///< 热门想法 / Popular reviews
#define WEREAD_THOUGHT_ABSTRACT_CAP 512 ///< 划线原文片段上限（同 WEREAD_NOTE_TEXT_CAP）/ Excerpt cap (matches)

typedef struct {
    char chapter[128];   ///< 所在章节 / Chapter title
    char author[96];     ///< 想法作者（划线为空）/ Review author (empty for highlights)
    uint32_t heat;       ///< 划线人数或点赞数 / Highlight count or likes
    uint16_t rating;     ///< 想法推荐值 / Review rating
    char abstract[WEREAD_THOUGHT_ABSTRACT_CAP]; ///< 划线原文片段（review/list）/ Highlight excerpt
} weread_thought_meta_t;

/// 打开一本书的划线想法缓存（任务完成后调用）；无缓存返回 false。
/// / Open a book's thoughts cache after the fetch task; false when absent.
bool weread_thoughts_open(const char* book_id);
/// 打开某一章的 review/list 缓存（网友划线+想法，按章目录）。
/// / Open one chapter's review/list cache (per-chapter directory).
bool weread_thoughts_open_chapter(const char* book_id, const char* chapter_uid);
/// 某类记录条数；未打开缓存返回 0。/ Records of one kind; 0 when no cache is open.
unsigned weread_thoughts_count(unsigned kind);
/// 读取一条记录：文本截断到 cap（含 NUL），元数据可空。/ One record: text capped at cap incl. NUL; meta nullable.
bool weread_thoughts_get(unsigned kind, unsigned index, char* text, size_t cap,
                         weread_thought_meta_t* out);
#ifdef __cplusplus
}
#endif

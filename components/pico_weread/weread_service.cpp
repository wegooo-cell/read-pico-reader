/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 串行微信读书任务，后台连接、扫码、同步与下载，UI 只读取快照。
 * Serialized background connection, login, shelf and download; UI reads snapshots only.
 * 冻结：退出与介质失效先取消并等待；只读云端进度。
 * Frozen: cancel and join before exit/media loss; read-only cloud progress.
 */
#include "weread_service.h"
#include "WeReadClient.h"
#include "HalStorage.h"
#include "TimeUtils.h"
#include "weread_notes.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
extern "C" {
#include "read_pico_transfer.h"
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <new>
#include <sys/stat.h>

// C 页面使用这组协议错误编号，防止上游枚举漂移。/ Guard protocol error numbers used by the C page.
static_assert(static_cast<int>(WeReadClient::Error::SdCard) == 6 &&
              static_cast<int>(WeReadClient::Error::Integrity) == 7 &&
              static_cast<int>(WeReadClient::Error::Unavailable) == 8 &&
              static_cast<int>(WeReadClient::Error::Clock) == 9 &&
              static_cast<int>(WeReadClient::Error::OutOfMemory) == 10 &&
              static_cast<int>(WeReadClient::Error::WholeBookOnly) == 11);

static SemaphoreHandle_t s_mutex, s_finished;
// 快照放扩展内存，内部内存留给 WiFi 与任务栈。/ Keep snapshots in PSRAM for WiFi/task-stack headroom.
static weread_snapshot_t* s_status_storage;
#define s_status (*s_status_storage)
static std::atomic<bool> s_cancel{false};
static bool s_configured;
static bool s_include_images = true;
static weread_action_t s_action;
static unsigned s_page, s_index, s_queue_count;
static weread_selection_t* s_queue;
static char s_book_id[64];      ///< 章级拉取目标书号 / Target book for chapter fetch
static char s_chapter_uid[64];  ///< 章级拉取目标章 / Target chapter uid
static uint32_t s_report_offset;    ///< 阅读上报：章内字节偏移 / Read report: in-chapter byte offset
static unsigned s_report_pct;       ///< 阅读上报：全书百分比 / Read report: whole-book percent
static uint32_t s_report_seconds;   ///< 阅读上报：本次阅读秒数（rt）/ Read report: seconds read (rt)
static bool s_report_has, s_report_ok;  ///< 本次开机最近一次上报尝试与结果 / Latest attempt this boot
static int64_t s_report_epoch;          ///< 最近一次尝试时刻（设备秒，时钟无效为 0）/ Attempt time (0 = no clock)
bool pico_weread_cancelled() { return s_cancel.load(); }

static void log_memory(const char* stage) {
    ESP_LOGI("weread", "%s internal=%u largest=%u psram=%u stack_free=%u", stage,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}
static bool initialize() {
    if (!s_status_storage) s_status_storage = static_cast<weread_snapshot_t*>(
        heap_caps_calloc(1, sizeof(weread_snapshot_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_finished) s_finished = xSemaphoreCreateBinary();
    return s_status_storage && s_mutex && s_finished;
}
static void lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(s_mutex); }
static void set_state(weread_state_t state, int error = 0) {
    lock();
    if (s_status.state != state || s_status.error != error) {
        s_status.state = state; s_status.error = error; ++s_status.revision;
    }
    if (state != WEREAD_QR) s_status.qr[0] = 0;
    if (state == WEREAD_FAILED) ESP_LOGE("weread", "failed error=%d", error);
    unlock();
}
static bool load_page(unsigned page) {
    auto* visible = static_cast<weread_book_t*>(heap_caps_calloc(
        WEREAD_ROWS, sizeof(weread_book_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!visible) { set_state(WEREAD_FAILED, 10); return false; }
    HalFile file;
    uint32_t total = 0;
    if (!WeReadStore::openShelf(file, total)) total = 0;
    const unsigned pages = total ? (total + WEREAD_ROWS - 1) / WEREAD_ROWS : 1;
    if (page >= pages) page = pages - 1;
    unsigned count = 0;
    for (unsigned i = 0; i < WEREAD_ROWS && page * WEREAD_ROWS + i < total; ++i) {
        WeReadStore::ShelfRecord record;
        if (!WeReadStore::readShelfRecord(file, page * WEREAD_ROWS + i, record)) break;
        if (!memchr(record.bookId, 0, sizeof(record.bookId)) || !memchr(record.title, 0, sizeof(record.title)) ||
            !memchr(record.author, 0, sizeof(record.author))) break;
        memcpy(visible[i].id, record.bookId, sizeof(record.bookId));
        memcpy(visible[i].title, record.title, sizeof(record.title));
        memcpy(visible[i].author, record.author, sizeof(record.author));
        const auto path = WeReadStore::finalBookPath(record);
        if (Storage.exists(path)) snprintf(visible[i].local_path, sizeof(visible[i].local_path), "%s", Storage.map(path).c_str());
        ++count;
    }
    WeReadStore::Session session;
    const bool logged_in = WeReadStore::loadSession(session);
    session.clear();
    lock();
    s_status.logged_in = logged_in;
    s_status.total = total; s_status.page = page; s_status.count = count;
    memcpy(s_status.books, visible, sizeof(s_status.books));
    ++s_status.revision;
    unlock();
    heap_caps_free(visible);
    return true;
}
static bool connect_online(bool& owned_network) {
    char ssid[33]; bool configured = false;
    if (read_pico_transfer_get_saved_wifi(ssid, &configured) != ESP_OK || !configured) {
        set_state(WEREAD_FAILED, 100); return false;
    }
    read_pico_transfer_cfg_t cfg = {};
    cfg.mode = READ_PICO_TRANSFER_MODE_STA; cfg.network_only = true;
    // 复用已有 STA 并保持连接；只清理本任务创建的网络。
    // Reuse an existing STA without disconnecting it on completion. Only tear down a session this worker starts.
    read_pico_transfer_status_t existing;
    read_pico_transfer_get_status(&existing);
    if (!(existing.mode == READ_PICO_TRANSFER_MODE_STA && existing.network_ready)) {
        read_pico_transfer_stop();
        owned_network = true;
        const esp_err_t error = read_pico_transfer_start(&cfg);
        if (error != ESP_OK) {
            ESP_LOGE("weread", "network start failed: %s", esp_err_to_name(error));
            set_state(WEREAD_FAILED, error == ESP_ERR_NO_MEM ? 10 : 101); return false;
        }
    }
    log_memory("network started");
    const TickType_t began = xTaskGetTickCount();
    while (!s_cancel.load()) {
        read_pico_transfer_service_poll();
        read_pico_transfer_status_t status;
        read_pico_transfer_get_status(&status);
        if (status.network_ready) {
            uint32_t epoch;
            if (!TimeUtils::isClockValid() && read_pico_transfer_sync_time_online(&epoch) != ESP_OK) {
                set_state(WEREAD_FAILED, 102); return false;
            }
            return true;
        }
        if (status.state == READ_PICO_TRANSFER_ERROR || xTaskGetTickCount() - began > pdMS_TO_TICKS(20000)) {
            set_state(WEREAD_FAILED, 101); return false;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}
static void progress_callback(void* raw) {
    auto* op = static_cast<WeReadClient::Operation*>(raw);
    if (s_cancel.load()) op->cancel();
    // 长打包循环定期让 UI 处理输入。/ Let the UI process input during long packaging loops.
    vTaskDelay(pdMS_TO_TICKS(1));
}
// 选择以书号校验，索引只做提示，避免同步重排序后下载错书。
// Validate selection by ID; an index is only a hint after shelf reordering.
static bool valid_record(const WeReadStore::ShelfRecord& record) {
    return memchr(record.bookId, 0, sizeof(record.bookId)) && memchr(record.title, 0, sizeof(record.title)) &&
           memchr(record.author, 0, sizeof(record.author)) && memchr(record.coverUrl, 0, sizeof(record.coverUrl));
}
static bool resolve_selection(unsigned index, const char* id, WeReadStore::ShelfRecord& record) {
    HalFile shelf; uint32_t total = 0;
    if (!WeReadStore::openShelf(shelf, total)) return false;
    if (index < total && WeReadStore::readShelfRecord(shelf, index, record) && valid_record(record) &&
        (!id || !strcmp(id, record.bookId))) return true;
    if (!id) return false;
    for (unsigned i = 0; i < total && !s_cancel.load(); ++i) {
        if (!WeReadStore::readShelfRecord(shelf, i, record) || !valid_record(record)) return false;
        if (!strcmp(id, record.bookId)) return true;
    }
    return false;
}
// 按章拉取划线想法缓存：引擎原生 browse 状态机（当前章 review/list + 续期 + SD 提交）。
// / Per-chapter thoughts cache via the engine's native browse state machine (review/list).
static bool run_browse(WeReadClient::Operation& op, const WeReadStore::ShelfRecord& selected,
                       const char* chapter_uid) {
    if (!op.beginBrowseCache(WeReadStore::bookRecord(selected), chapter_uid)) {
        set_state(WEREAD_FAILED, static_cast<int>(op.error())); return false;
    }
    set_state(WEREAD_WORKING);
    while (op.active()) {
        if (s_cancel.load()) op.cancel();
        const auto event = op.step(progress_callback, &op);
        if (event == WeReadClient::Operation::Event::QrReady) {
            lock(); snprintf(s_status.qr, sizeof(s_status.qr), "%.319s", op.qrUrl());
            s_status.state = WEREAD_QR; ++s_status.revision; unlock();
        } else if (event == WeReadClient::Operation::Event::Authenticated) set_state(WEREAD_WORKING);
        if (event == WeReadClient::Operation::Event::Complete) {
            // 串口验收：打印本章 review/list 记录条数。
            // / Serial acceptance log: this chapter's review record count.
            WeReadStore::Session session;
            WeReadBrowse::CacheManifest manifest;
            if (WeReadStore::loadSession(session) && session.vid[0] &&
                WeReadBrowse::loadCache(selected.bookId, chapter_uid, session.vid, manifest)) {
                ESP_LOGI("weread", "chapter reviews cached: book=%s ch=%s reviews=%u",
                         selected.bookId, chapter_uid,
                         (unsigned)manifest.recordCounts[WeReadBrowse::kindIndex(WeReadBrowse::Kind::PopularReviews)]);
            }
            session.clear();
            return true;
        }
        if (event == WeReadClient::Operation::Event::Failed) {
            set_state(WEREAD_FAILED, static_cast<int>(op.error())); return false;
        }
        if (event == WeReadClient::Operation::Event::Cancelled) { set_state(WEREAD_CANCELLED); return false; }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return false;
}
static bool run_operation(WeReadClient::Operation& op, const WeReadStore::ShelfRecord* selected) {
    WeReadClient::DownloadOptions options;
    options.imagePolicy = s_include_images ? WeReadStore::ImagePolicy::Embed : WeReadStore::ImagePolicy::Exclude;
    const auto kind = selected ? WeReadClient::Operation::Kind::Download : WeReadClient::Operation::Kind::Sync;
    if (!op.begin(kind, selected, options)) {
        set_state(WEREAD_FAILED, static_cast<int>(op.error())); return false;
    }
    set_state(WEREAD_WORKING);
    while (op.active()) {
        if (s_cancel.load()) op.cancel();
        const auto event = op.step(progress_callback, &op);
        if (event == WeReadClient::Operation::Event::QrReady) {
            lock(); snprintf(s_status.qr, sizeof(s_status.qr), "%.319s", op.qrUrl());
            s_status.state = WEREAD_QR; ++s_status.revision; unlock();
        } else if (event == WeReadClient::Operation::Event::Authenticated) set_state(WEREAD_WORKING);
        lock();
        if (s_status.done != op.progressCompleted() || s_status.target != op.progressTotal() ||
            (int)s_status.stage != (int)op.progressStage() || s_status.skipped_images != op.skippedImageCount()) {
            s_status.done = op.progressCompleted(); s_status.target = op.progressTotal();
            s_status.stage = static_cast<weread_stage_t>(op.progressStage());
            s_status.skipped_images = op.skippedImageCount(); ++s_status.revision;
        }
        unlock();
        if (event == WeReadClient::Operation::Event::Complete) {
            if (selected) {
                lock(); snprintf(s_status.output, sizeof(s_status.output), "%s", Storage.map(op.finalPath()).c_str());
                ++s_status.changed; unlock();
            }
            return true;
        }
        if (event == WeReadClient::Operation::Event::Failed) {
            set_state(WEREAD_FAILED, static_cast<int>(op.error())); return false;
        }
        if (event == WeReadClient::Operation::Event::Cancelled) { set_state(WEREAD_CANCELLED); return false; }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return false;
}
static void worker(void*) {
    log_memory("worker started");
    bool owned_network = false;
    WeReadClient::Operation* op = nullptr;
    if (s_action == WEREAD_LOAD) {
        if (load_page(s_page)) set_state(WEREAD_IDLE);
    } else if (s_action == WEREAD_LOGOUT) {
        const bool session = WeReadStore::clearSession();
        const bool shelf = WeReadStore::clearShelf() && WeReadBrowse::clearAllCaches();
        if (load_page(0)) set_state(session && shelf ? WEREAD_COMPLETE : WEREAD_FAILED, session && shelf ? 0 : 103);
    } else if (s_action == WEREAD_NOTES) {
        set_state(WEREAD_CONNECTING);
        if (connect_online(owned_network) && !s_cancel.load()) {
            WeReadStore::ShelfRecord selected;
            // 书号优先（阅读页整本拉取路由），详情页按钮仍按索引解析。
            // / Book id first (the reader's whole-book route); the detail button resolves by index.
            if (!resolve_selection(s_index, s_book_id[0] ? s_book_id : nullptr, selected))
                set_state(WEREAD_FAILED, 103);
            else {
                set_state(WEREAD_WORKING);
                // 双粒度进度：done/target=章，notes_done/notes_total=想法条数（总数未知为 0）。
                // / Two granularities: chapters in done/target, thought counts in notes_*.
                struct ProgressCtx {
                    void report(unsigned done, unsigned total) {
                        lock();
                        s_status.done = done; s_status.target = total;
                        ++s_status.revision;
                        unlock();
                    }
                    void report_notes(unsigned notes, unsigned total) {
                        lock();
                        s_status.notes_done = notes; s_status.notes_total = total;
                        ++s_status.revision;
                        unlock();
                    }
                } progress_ctx;
                const auto on_progress = [](void* raw, unsigned done, unsigned total) {
                    static_cast<ProgressCtx*>(raw)->report(done, total);
                };
                const auto on_notes = [](void* raw, unsigned notes, unsigned total) {
                    static_cast<ProgressCtx*>(raw)->report_notes(notes, total);
                };
                const auto on_cancel = [](void*) { return s_cancel.load(); };
                // 返回值即状态页错误码：0 成功，2 网络，3 登录，6 存储，10 内存，12 目录缺失。
                // / The return value is the status-page error code (see weread_notes.h).
                const int notes_error =
                    weread_notes_fetch(selected.bookId, on_cancel, nullptr, on_progress, &progress_ctx,
                                       on_notes, &progress_ctx);
                if (notes_error == 0) set_state(WEREAD_COMPLETE);
                else set_state(s_cancel.load() ? WEREAD_CANCELLED : WEREAD_FAILED, notes_error);
            }
        }
    } else if (s_action == WEREAD_THOUGHTS) {
        // 按章 review/list 拉取：章上下文由 weread_start_chapter_reviews 设置。
        // / Per-chapter review/list fetch; context set by weread_start_chapter_reviews.
        set_state(WEREAD_CONNECTING);
        if (connect_online(owned_network) && !s_cancel.load()) {
            void* memory = heap_caps_malloc(sizeof(WeReadClient::Operation), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (memory) op = new (memory) WeReadClient::Operation();
            WeReadStore::ShelfRecord selected;
            if (!op) set_state(WEREAD_FAILED, 10);
            else if (!s_book_id[0] || !s_chapter_uid[0] || !resolve_selection(0, s_book_id, selected))
                set_state(WEREAD_FAILED, 103);
            else if (run_browse(*op, selected, s_chapter_uid)) set_state(WEREAD_COMPLETE);
        }
    } else if (s_action == WEREAD_READ_REPORT) {
        // 阅读时长（rt）+进度一次性上报：UploadLocal 无条件上传，静默失败不打扰阅读。
        // / One-shot reading-time (rt) + progress upload via UploadLocal; silent on failure.
        set_state(WEREAD_CONNECTING);
        bool sent = false;
        if (connect_online(owned_network) && !s_cancel.load()) {
            void* memory = heap_caps_malloc(sizeof(WeReadClient::Operation), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (memory) op = new (memory) WeReadClient::Operation();
            if (!op) ESP_LOGW("weread", "read report OOM");
            else {
                WeReadClient::ProgressSyncInput input;
                input.localFraction = s_report_pct / 100.0f;
                input.localTocIndex = s_index;
                input.localOffset = s_report_offset;
                input.localOffsetBasis = WeReadClient::LocalOffsetBasis::VisibleText;
                input.elapsedSeconds = s_report_seconds;
                ESP_LOGI("weread", "read report: book=%s ch=%lu off=%lu pct=%u rt=%lu", s_book_id,
                         (unsigned long)s_index, (unsigned long)s_report_offset, s_report_pct,
                         (unsigned long)s_report_seconds);
                if (op->beginProgressSync(s_book_id, input, WeReadClient::ProgressSyncMode::UploadLocal)) {
                    while (op->active()) {
                        if (s_cancel.load()) op->cancel();
                        const auto event = op->step(progress_callback, &op);
                        if (event == WeReadClient::Operation::Event::QrReady) {
                            // 阅读中无人扫码：会话已过期，放弃本次上报。
                            // / Nobody scans while reading: session expired; abandon this report.
                            ESP_LOGW("weread", "read report: session expired");
                            op->cancel();
                        }
                        if (event == WeReadClient::Operation::Event::Complete ||
                            event == WeReadClient::Operation::Event::Failed ||
                            event == WeReadClient::Operation::Event::Cancelled)
                            break;
                        vTaskDelay(pdMS_TO_TICKS(30));
                    }
                    sent = op->error() == WeReadClient::Error::Ok;
                }
                ESP_LOGI("weread", "read report %s (error=%d outcome=%u)", sent ? "sent" : "skipped",
                         static_cast<int>(op->error()),
                         static_cast<unsigned>(op->progressSyncResult().outcome));
            }
        }
        // 详情页状态显示用：最近一次尝试的结果与时刻（含失败）。
        // / For the detail-page status line: latest attempt result and time (failures included).
        s_report_has = true;
        s_report_ok = sent;
        s_report_epoch = TimeUtils::getCurrentValidTimestamp();
        // 静默语义：成败只留串口日志，快照回 IDLE，不惊扰状态页。
        // / Silent semantics: serial logs only; the snapshot returns to IDLE.
        set_state(WEREAD_IDLE);
    } else {
        set_state(WEREAD_CONNECTING);
        if (connect_online(owned_network) && !s_cancel.load()) {
            // 每个批次只分配一个操作对象、一个任务栈，完成一本后释放其 TLS 再处理下一本。
            // One operation and task stack per batch; release each book's TLS before starting the next.
            void* memory = heap_caps_malloc(sizeof(WeReadClient::Operation), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (memory) op = new (memory) WeReadClient::Operation();
            if (!op) set_state(WEREAD_FAILED, 10);
            else if (s_action == WEREAD_SYNC) {
                if (run_operation(*op, nullptr) && load_page(s_page)) set_state(WEREAD_COMPLETE);
            } else {
                const unsigned count = s_action == WEREAD_BATCH ? s_queue_count : 1;
                int last_error = 0;
                for (unsigned i = 0; i < count && !s_cancel.load(); ++i) {
                    WeReadStore::ShelfRecord selected;
                    const bool found = resolve_selection(s_action == WEREAD_BATCH ? s_queue[i].index : s_index,
                                                         s_action == WEREAD_BATCH ? s_queue[i].id : nullptr, selected);
                    lock();
                    s_status.done = s_status.target = s_status.skipped_images = 0;
                    s_status.stage = WEREAD_PREPARING;
                    s_status.batch_current = s_action == WEREAD_BATCH ? i + 1 : 0;
                    snprintf(s_status.batch_title, sizeof(s_status.batch_title), "%s", found ? selected.title : "");
                    ++s_status.revision; unlock();
                    if (!found) set_state(WEREAD_FAILED, 103);
                    const bool success = found && run_operation(*op, &selected);
                    op->reset();
                    // 下载后不再自动拉划线想法：数据源已切到按章 review/list，无章上下文；
                    // 章级补拉由阅读页 set_chapter 触发。/ No post-download auto fetch: the
                    // source is per-chapter now; the reader page triggers chapter fetches.
                    if (s_cancel.load()) break;
                    lock();
                    if (success) ++s_status.batch_success; else ++s_status.batch_failed;
                    ++s_status.revision;
                    const int error = s_status.error; unlock();
                    if (!success) last_error = error;
                    // 断网、失卡或内存不足停止队列；内容权限或单本封面错误继续下一本。
                    // Stop on network, card or memory failure; continue past per-book permission/cover errors.
                    if (!success && (error == 2 || error == 3 || error == 4 || error == 6 || error == 9 || error == 10 || error >= 100)) break;
                }
                lock(); const bool failed = s_status.batch_failed != 0;
                unlock();
                if (!s_cancel.load() && load_page(s_page)) set_state(failed ? WEREAD_FAILED : WEREAD_COMPLETE, failed ? last_error : 0);
            }
        }
    }
    if (op) { op->reset(); op->~Operation(); heap_caps_free(op); }
    heap_caps_free(s_queue); s_queue = nullptr; s_queue_count = 0;
    if (owned_network) read_pico_transfer_stop();
    if (s_cancel.load()) set_state(WEREAD_CANCELLED);
    log_memory("worker finished");
    lock(); s_status.active = false; ++s_status.revision;
    xSemaphoreGive(s_finished); unlock();
    vTaskDelete(nullptr);
}
extern "C" bool weread_configure(const char* cache, const char* books) {
    if (!initialize() || !cache || !books || strncmp(cache, "/sdcard/", 8) || strncmp(books, "/sdcard/", 8) ||
        strlen(cache) >= 160 || strlen(books) >= 160) return false;
    lock();
    if (s_status.active) { unlock(); return false; }
    Storage.configure(cache, books); s_configured = true;
    unlock();
    return true;
}
static bool dispatch(weread_action_t action, unsigned page, unsigned index,
                     const weread_selection_t* selection, unsigned count,
                     const char* book_id = nullptr, const char* chapter_uid = nullptr,
                     const uint32_t report_offset = 0, const unsigned report_pct = 0,
                     const uint32_t report_seconds = 0) {
    if (!initialize() || !s_configured) return false;
    weread_selection_t* queue = nullptr;
    if (action == WEREAD_BATCH) {
        if (!selection || !count || count > WEREAD_BATCH_MAX) return false;
        queue = static_cast<weread_selection_t*>(heap_caps_calloc(count, sizeof(*queue), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!queue) return false;
        for (unsigned i = 0; i < count; ++i) {
            if (!selection[i].id[0] || !memchr(selection[i].id, 0, sizeof(selection[i].id))) {
                heap_caps_free(queue); return false;
            }
            for (unsigned j = 0; j < i; ++j) if (!strcmp(selection[i].id, selection[j].id)) {
                heap_caps_free(queue); return false;
            }
            queue[i] = selection[i];
        }
    }
    if (action == WEREAD_THOUGHTS &&
        (!book_id || !book_id[0] || !chapter_uid || !chapter_uid[0] ||
         strlen(book_id) >= sizeof(s_book_id) || strlen(chapter_uid) >= sizeof(s_chapter_uid))) {
        heap_caps_free(queue); return false;
    }
    // 阅读上报必须带书号；seconds=0 合法（详情页手动纯进度同步，官方 enter 包形态）。
    // / Read reports require a book id; seconds=0 is legal (manual progress-only sync,
    // / same shape as the official enter packet).
    if (action == WEREAD_READ_REPORT &&
        (!book_id || !book_id[0] || strlen(book_id) >= sizeof(s_book_id))) {
        heap_caps_free(queue); return false;
    }
    // 整本划线想法路由可带书号（阅读页），也可不带（详情页按索引解析）。
    // / Whole-book notes route takes an optional book id (reader) or resolves by index (detail page).
    if (action == WEREAD_NOTES && book_id && book_id[0] && strlen(book_id) >= sizeof(s_book_id)) {
        heap_caps_free(queue); return false;
    }
    lock();
    if (s_status.active) { unlock(); heap_caps_free(queue); return false; }
    while (xSemaphoreTake(s_finished, 0) == pdTRUE) {}
    s_cancel.store(false);
    s_action = action; s_page = page; s_index = index; s_queue = queue; s_queue_count = count;
    if (action == WEREAD_THOUGHTS) {
        snprintf(s_book_id, sizeof(s_book_id), "%s", book_id);
        snprintf(s_chapter_uid, sizeof(s_chapter_uid), "%s", chapter_uid);
    } else if (action == WEREAD_READ_REPORT) {
        snprintf(s_book_id, sizeof(s_book_id), "%s", book_id);
        s_chapter_uid[0] = 0;
        s_report_offset = report_offset; s_report_pct = report_pct; s_report_seconds = report_seconds;
    } else if (action == WEREAD_NOTES && book_id && book_id[0]) {
        snprintf(s_book_id, sizeof(s_book_id), "%s", book_id);
        s_chapter_uid[0] = 0;
    } else {
        s_book_id[0] = s_chapter_uid[0] = 0;
    }
    s_status.action = action; s_status.active = true; s_status.qr[0] = s_status.output[0] = s_status.batch_title[0] = 0;
    s_status.done = s_status.target = s_status.skipped_images = 0; s_status.error = 0;
    s_status.notes_done = s_status.notes_total = 0;
    s_status.batch_total = count; s_status.batch_current = s_status.batch_success = s_status.batch_failed = 0;
    s_status.stage = WEREAD_CHAPTERS; s_status.state = WEREAD_WORKING; ++s_status.revision;
    unlock(); log_memory("dispatch");
    if (xTaskCreate(worker, "weread", 16384, nullptr, 3, nullptr) != pdPASS) {
        heap_caps_free(s_queue); s_queue = nullptr; s_queue_count = 0;
        lock(); s_status.active = false; s_status.state = WEREAD_FAILED;
        s_status.error = 10; ++s_status.revision; unlock(); return false;
    }
    return true;
}
extern "C" bool weread_start(weread_action_t action, unsigned page, unsigned index) {
    if (action < WEREAD_LOAD || action > WEREAD_NOTES || action == WEREAD_THOUGHTS) return false;
    return dispatch(action, page, index, nullptr, 0);
}
extern "C" bool weread_start_chapter_reviews(const char* book_id, const char* chapter_uid) {
    return dispatch(WEREAD_THOUGHTS, 0, 0, nullptr, 0, book_id, chapter_uid);
}
// 整本划线想法：书号优先；空书号时 worker 回退按 s_index 解析书架记录。
// / Whole-book notes: book id first; empty id falls back to shelf-index resolution in the worker.
extern "C" bool weread_start_notes(const char* book_id) {
    if (!book_id || !book_id[0]) return false;
    return dispatch(WEREAD_NOTES, 0, 0, nullptr, 0, book_id, nullptr);
}
// 阅读时长（rt）+进度一次性上报：UploadLocal 无条件上传，官方时长统计的唯一入口。
// seconds=0 为纯进度同步（详情页手动触发）。
// / One-shot reading-time (rt) + progress upload via UploadLocal; the only path into
// / official stats. seconds=0 is a progress-only sync (manual trigger).
extern "C" bool weread_start_read_report(const char* book_id, uint16_t chapter, uint32_t byte_off,
                                         uint8_t pct, uint32_t elapsed_seconds) {
    if (!book_id || !book_id[0]) return false;
    return dispatch(WEREAD_READ_REPORT, 0, chapter, nullptr, 0, book_id, nullptr, byte_off, pct, elapsed_seconds);
}
extern "C" void weread_last_read_report(bool* has, bool* ok, int64_t* epoch) {
    lock();
    if (has) *has = s_report_has;
    if (ok) *ok = s_report_ok;
    if (epoch) *epoch = s_report_epoch;
    unlock();
}
extern "C" bool weread_start_batch(unsigned page, const weread_selection_t* selection, unsigned count) {
    return dispatch(WEREAD_BATCH, page, 0, selection, count);
}
extern "C" void weread_snapshot(weread_snapshot_t* out) {
    if (!out) return;
    if (!initialize()) { memset(out, 0, sizeof(*out)); out->state = WEREAD_FAILED; out->error = 10; return; }
    lock(); *out = s_status; unlock();
}
extern "C" void weread_stop() {
    if (!initialize()) return;
    s_cancel.store(true);
    lock(); const bool active = s_status.active; unlock();
    if (active) xSemaphoreTake(s_finished, portMAX_DELAY);
}
extern "C" bool weread_cancel_clear_if_idle(void) {
    if (!initialize()) return false;
    lock(); const bool active = s_status.active; unlock();
    if (active) return false;  // 忙时不碰取消语义。/ Never touch cancellation while busy.
    s_cancel.store(false);
    return true;
}

extern "C" bool weread_set_include_images(bool enabled) {
    if (!initialize()) return false;
    lock();
    if (s_status.active) { unlock(); return false; }
    s_include_images = enabled;
    unlock();
    return true;
}

// ---- 划线想法缓存读取（UI 线程；仅在后台任务空闲时调用） ----
// ---- Thoughts cache reader (UI thread; call only while no worker runs) ----
static WeReadBrowse::CacheManifest* s_thought_manifest;
static char s_thought_book[64];
static char s_thought_vid[64];
static char s_thought_chapter[64];

static WeReadBrowse::Kind thought_kind(unsigned kind) {
    return kind == WEREAD_THOUGHT_REVIEWS ? WeReadBrowse::Kind::PopularReviews
        : kind == WEREAD_THOUGHT_MINE ? WeReadBrowse::Kind::MyHighlights
        : WeReadBrowse::Kind::PopularHighlights;
}

// 缓存按章落盘；book 级 open 仅保留给旧调用点（永远无新缓存，自然失败）。
// / Per-chapter caches; the book-level open stays only for legacy callers and always misses.
static bool thoughts_open_locked_source(const char* book_id, const char* chapter_uid) {
    WeReadStore::Session session;
    if (!WeReadStore::loadSession(session) || !session.vid[0]) return false;
    auto* manifest = static_cast<WeReadBrowse::CacheManifest*>(
        heap_caps_calloc(1, sizeof(WeReadBrowse::CacheManifest), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!manifest) return false;
    if (!WeReadBrowse::loadCache(book_id, chapter_uid, session.vid, *manifest)) {
        heap_caps_free(manifest); return false;
    }
    lock();
    heap_caps_free(s_thought_manifest);
    s_thought_manifest = manifest;
    snprintf(s_thought_book, sizeof(s_thought_book), "%s", book_id);
    snprintf(s_thought_vid, sizeof(s_thought_vid), "%s", session.vid);
    snprintf(s_thought_chapter, sizeof(s_thought_chapter), "%s", chapter_uid ? chapter_uid : "");
    unlock();
    session.clear();
    return true;
}

extern "C" bool weread_thoughts_open_chapter(const char* book_id, const char* chapter_uid) {
    if (!initialize() || !book_id || !book_id[0] || !chapter_uid || !chapter_uid[0] ||
        strlen(book_id) >= sizeof(s_thought_book) || strlen(chapter_uid) >= sizeof(s_thought_chapter)) return false;
    lock();
    const bool same = s_thought_manifest && !strcmp(s_thought_book, book_id) &&
                      !strcmp(s_thought_chapter, chapter_uid);
    unlock();
    if (same) return true;
    return thoughts_open_locked_source(book_id, chapter_uid);
}

extern "C" bool weread_thoughts_open(const char* book_id) {
    if (!initialize() || !book_id || !book_id[0] || strlen(book_id) >= sizeof(s_thought_book)) return false;
    lock();
    const bool same = s_thought_manifest && !strcmp(s_thought_book, book_id) && !s_thought_chapter[0];
    unlock();
    if (same) return true;
    return thoughts_open_locked_source(book_id, "");
}

extern "C" unsigned weread_thoughts_count(unsigned kind) {
    if (kind >= WeReadBrowse::kKindCount) return 0;
    lock();
    const unsigned count = s_thought_manifest ? s_thought_manifest->recordCounts[kind] : 0;
    unlock();
    return count;
}

extern "C" bool weread_thoughts_get(unsigned kind, unsigned index, char* text, size_t cap,
                                    weread_thought_meta_t* out) {
    if (!text || !cap) return false;
    text[0] = 0;
    if (out) memset(out, 0, sizeof(*out));
    if (kind >= WeReadBrowse::kKindCount) return false;
    lock();
    if (!s_thought_manifest || index >= s_thought_manifest->recordCounts[kind]) { unlock(); return false; }
    const WeReadBrowse::CacheManifest manifest = *s_thought_manifest;
    char book[64], chapter[64];
    snprintf(book, sizeof(book), "%s", s_thought_book);
    snprintf(chapter, sizeof(chapter), "%s", s_thought_chapter);
    unlock();
    if (!book[0]) return false;
    const WeReadBrowse::Kind k = thought_kind(kind);
    // 逐页累计定位记录；页头计数即页内条数。/ Walk pages; the header carries per-page counts.
    uint32_t seen = 0;
    for (uint32_t page = 0; page < manifest.pageCounts[kind]; ++page) {
        WeReadBrowse::PageHeader header;
        HalFile index_file, text_file;
        if (!WeReadBrowse::openPage(book, chapter, manifest, k, page, header, index_file, text_file)) return false;
        if (index < seen + header.count) {
            WeReadBrowse::Record record;
            const bool found = WeReadBrowse::readRecord(index_file, header, index - seen, record);
            index_file.close();
            if (!found) { text_file.close(); return false; }
            const size_t want = record.textLength < cap - 1 ? record.textLength : cap - 1;
            size_t got = 0;
            if (want && text_file.seek(record.textOffset)) {
                const int bytes = text_file.read(text, want);
                if (bytes > 0) got = static_cast<size_t>(bytes);
            }
            text[got] = 0;
            if (out) {
                snprintf(out->chapter, sizeof(out->chapter), "%s", record.chapter);
                snprintf(out->author, sizeof(out->author), "%s", record.author);
                out->heat = record.heat;
                out->rating = record.rating;
                // 划线原文片段（review/list abstract 段）。/ Highlight excerpt from the abstract segment.
                const size_t awant = record.abstractLength < sizeof(out->abstract) - 1
                                         ? record.abstractLength : sizeof(out->abstract) - 1;
                size_t agot = 0;
                if (awant && text_file.seek(record.abstractOffset)) {
                    const int bytes = text_file.read(out->abstract, awant);
                    if (bytes > 0) agot = static_cast<size_t>(bytes);
                }
                out->abstract[agot] = 0;
            }
            text_file.close();
            return true;
        }
        seen += header.count;
    }
    return false;
}

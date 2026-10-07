/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 句子级划线想法的数据面：直连 underlines / readReviews 两个 web 端点
 * （协议与撷思 0.5.4 对齐，cookie 鉴权与引擎会话同源），按章原子落盘断点续传；
 * 读取桥给 UI 点击弹窗。不触碰引擎 Operation/Phase/browse 缓存。
 * Sentence-level notes data plane: direct underlines / readReviews calls aligned
 * with the XieSi plugin, per-chapter atomic resume caches, read bridge for UI.
 * 冻结：不改引擎内部；失败不阻塞下载；仅读公开数据。
 * Frozen: engine untouched; fetch failure never blocks downloads; read-only data.
 */
#include "weread_notes.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "HalStorage.h"
#include "WeReadHttpClient.h"
#include "WeReadStore.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char* kTag = "wr_notes";
constexpr const char* kHost = "https://weread.qq.com";
constexpr const char* kOrigin = "https://weread.qq.com";
constexpr const char* kReferer = "https://weread.qq.com/";
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 "
    "Safari/537.36 Edg/135.0.0.0";
constexpr int kTimeoutMs = 45000;
constexpr size_t kReadBuffer = 8192;
constexpr size_t kCookieCap = 1024;
constexpr int kBatchHighlights = 3;   ///< 每批拉想法的划线数。划线密集的书（一章 90+ 条）单批响应
                                      ///< 可达 115KB，5 条会把内部 RAM 压到 SDMMC DMA 分配失败；
                                      ///< 3 条把峰值降到 ~70KB。
                                      ///< / Highlights per thought batch; 3 keeps peak responses ~70KB
                                      ///< / so SDMMC DMA buffer allocation stays safe.
constexpr int kReviewsPerRange = 15;  ///< 每页请求的想法条数（翻页页大小）。响应对象很肥
                                      ///<（~1.5KB/条），页大小是完整性与内部 RAM 的折中。
                                      ///< / Page size per sentence; review objects are fat (~1.5KB each).
constexpr int kMaxThoughtsPerRange = 200;  ///< 单句想法保险上限：极端热句封顶，防失控。
                                           ///< / Hard cap per sentence against pathological hot spots.
constexpr int kMaxPagesPerBatch = 40;      ///< 单批翻页保险上限（3×15×40=1800 条/批理论极值）。
                                           ///< / Hard page cap per batch as a second guard.
constexpr uint32_t kSeenSlots = 512;       ///< 每句去重哈希槽数（>200 上限，负载<0.5）。
                                           ///< / Dedup slots per sentence (load factor < 0.5).
constexpr int kChapterPauseMs = 400;  ///< 章间限速，防风控。/ Inter-chapter pacing.
constexpr int kBatchPauseMs = 300;    ///< 页/批间限速。/ Inter-page and inter-batch pacing.
constexpr uint32_t kMetaMagic = 0x314E5257;     // WRN1
constexpr uint32_t kChapterMagic = 0x324E5257;  // WRN2
// v3：每句全量翻页（maxIdx 偏移）+ 定长文本槽（支持想法回填原位重写）；读取侧校验
// 版本使旧缓存自动失效重拉。/ v3: full per-sentence paging plus a fixed-size text slot
// / for in-place backfill; readers check the version so stale caches re-fetch.
constexpr uint16_t kFormatVersion = 4;

// ---- cJSON 钩子：解析期间切 PSRAM，用完恢复默认，不影响其他 cJSON 用户。 ----
// ---- Parse-time PSRAM allocator for cJSON; restored afterwards. ----
void* psram_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
struct PsramJsonScope {
    PsramJsonScope() {
        cJSON_Hooks hooks = {psram_malloc, free};
        cJSON_InitHooks(&hooks);
    }
    ~PsramJsonScope() { cJSON_InitHooks(nullptr); }
};

// ---- 文本规范化：去全部空白后用于匹配（划线与正文逐字同源，仅空白有差）。 ----
// ---- Strip all whitespace before matching; texts are verbatim-same source. ----
void normalize_copy(char* dst, size_t cap, const char* src) {
    size_t w = 0;
    for (const unsigned char* p = (const unsigned char*)src; *p && w + 1 < cap; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') continue;
        dst[w++] = (char)*p;
    }
    dst[w] = 0;
}

// 内存探针：定位内部 RAM 在哪一步被打光（诊断用，根因确认后可整体移除）。
// / Memory probe: pinpoints which step drains internal RAM (diagnostic; removable once stable).
void log_mem(const char* where) {
    ESP_LOGI(kTag, "[mem] %-16s int=%u largest=%u psram=%u", where,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

enum class HttpOutcome { Ok, Network, Protocol, SessionExpired, Cancelled };

// ---- 单 worker 串行的请求上下文：会话、连接复用、响应累积。 ----
// ---- Serial-worker request context: session, keep-alive, response buffer. ----
struct FetchCtx {
    WeReadStore::Session session;
    WeReadHttpClient::Session http;
    char cookie[kCookieCap];
    char url[512];
    std::string body;
    uint8_t read_buf[kReadBuffer];
    bool (*cancel)(void* ctx);
    void* cancel_ctx;

    bool stopped() const { return cancel && cancel(cancel_ctx); }
};

void absorb_set_cookie(WeReadStore::Session& session, const char* header_value) {
    // header_value = "wr_skey=xxx; Path=/..."；只认引擎会话三键。
    // / header_value is "name=value; attrs"; only the three session keys persist.
    const char* eq = strchr(header_value, '=');
    if (!eq) return;
    const size_t name_len = static_cast<size_t>(eq - header_value);
    std::string value(eq + 1);
    const size_t semi = value.find(';');
    if (semi != std::string::npos) value.resize(semi);
    if (name_len == 6 && !strncmp(header_value, "wr_vid", 6))
        session.setCookie("wr_vid", value.c_str(), value.size());
    else if (name_len == 7 && !strncmp(header_value, "wr_skey", 7))
        session.setCookie("wr_skey", value.c_str(), value.size());
    else if (name_len == 5 && !strncmp(header_value, "wr_rt", 5))
        session.setCookie("wr_rt", value.c_str(), value.size());
}

HttpOutcome http_exchange(FetchCtx& f, const char* tag, const char* url, bool post, const char* json_body,
                          int& status) {
    f.body.clear();
    f.cookie[0] = 0;
    if (!f.session.cookieHeader(f.cookie, sizeof(f.cookie))) return HttpOutcome::Protocol;
    WeReadHttpClient::Header headers[6] = {};
    size_t count = 0;
    headers[count++] = {"User-Agent", kUserAgent};
    headers[count++] = {"Accept", "application/json, text/plain, */*"};
    headers[count++] = {"Origin", kOrigin};
    headers[count++] = {"Referer", kReferer};
    if (post) headers[count++] = {"Content-Type", "application/json;charset=UTF-8"};
    if (f.cookie[0]) headers[count++] = {"Cookie", f.cookie};
    WeReadHttpClient::RequestOptions options;
    options.method = post ? "POST" : "GET";
    options.body = post ? reinterpret_cast<const uint8_t*>(json_body) : nullptr;
    options.bodySize = post ? strlen(json_body) : 0;
    options.headers = headers;
    options.headerCount = count;
    options.timeoutMs = kTimeoutMs;
    options.readBuffer = f.read_buf;
    options.readBufferSize = sizeof(f.read_buf);
    const auto on_data = [&f](const uint8_t* data, size_t len) {
        f.body.append(reinterpret_cast<const char*>(data), len);
        return !f.stopped();
    };
    const auto on_header = [&f](const char* name, const char* value) {
        if (!strcasecmp(name, "Set-Cookie")) absorb_set_cookie(f.session, value);
    };
    char where[48];
    snprintf(where, sizeof(where), "%s pre", tag);
    log_mem(where);
    const auto result =
        WeReadHttpClient::request(f.http, url, options, on_data, on_header, status);
    const HttpOutcome outcome = f.stopped() ? HttpOutcome::Cancelled
                                : result != WeReadHttpClient::Result::Ok ? HttpOutcome::Network
                                                                          : HttpOutcome::Ok;
    snprintf(where, sizeof(where), "%s post", tag);
    log_mem(where);
    ESP_LOGI(kTag, "[http] %s out=%d st=%d body=%u", tag, (int)outcome, status,
             (unsigned)f.body.size());
    return outcome;
}

// 续期语义与引擎 renewSession 一致：body 校验 + 会话仍有效才保存。
// / Renewal follows the engine renewSession semantics: body check plus session validity.
bool renew_session(FetchCtx& f) {
    if (!f.session.rt[0]) return false;
    int status = 0;
    const char* body = "{\"rq\":\"%2Fweb%2Fbook%2Fread\",\"ql\":false}";
    char url[160];
    snprintf(url, sizeof(url), "%s/web/login/renewal", kHost);
    const HttpOutcome outcome = http_exchange(f, "renewal", url, true, body, status);
    if (outcome != HttpOutcome::Ok || status != 200) return false;
    PsramJsonScope json_scope;
    cJSON* root = cJSON_Parse(f.body.c_str());
    bool ok = false;
    if (root) {
        const cJSON* succ = cJSON_GetObjectItem(root, "succ");
        const cJSON* err = cJSON_GetObjectItem(root, "errCode");
        if (cJSON_IsNumber(succ)) ok = succ->valuedouble == 1;
        else if (cJSON_IsNumber(err)) ok = err->valuedouble == 0;
        else ok = true;  // 无 errCode 视为成功，交由会话有效性把关。
        cJSON_Delete(root);
    }
    if (!ok || !f.session.valid()) {
        WeReadStore::clearSession();
        return false;
    }
    return WeReadStore::saveSession(f.session);
}

bool session_expired_body(const std::string& body, int status) {
    if (status == 401 || status == 403) return true;
    if (body.size() >= 2048 || body.empty()) return false;
    PsramJsonScope json_scope;
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return false;
    const cJSON* err = cJSON_GetObjectItem(root, "errCode");
    if (!cJSON_IsNumber(err)) err = cJSON_GetObjectItem(root, "errcode");
    const bool expired = cJSON_IsNumber(err) && err->valuedouble == -2012;
    cJSON_Delete(root);
    return expired;
}

// 带会话续期的请求：-2012 → renewal → 原请求重放一次（引擎/撷思同款）。
// / Authenticated exchange: on -2012 renew once and replay the original request.
HttpOutcome authed_exchange(FetchCtx& f, const char* tag, const char* url, bool post,
                            const char* json_body, int& status) {
    HttpOutcome outcome = http_exchange(f, tag, url, post, json_body, status);
    if (outcome != HttpOutcome::Ok) return outcome;
    if (!session_expired_body(f.body, status)) return HttpOutcome::Ok;
    ESP_LOGW(kTag, "session expired, renewing");
    if (!renew_session(f)) return HttpOutcome::SessionExpired;
    return http_exchange(f, tag, url, post, json_body, status);
}

// ---- 缓存路径与原子落盘：统一走引擎前缀（Storage.map 映射到配置缓存根），
// ---- rename/exists/ensureDirectory 均吃虚拟路径，文件读写走 openFileFor*。
// ---- Cache paths ride the engine prefix; virtual paths for Storage helpers.
constexpr const char* kNotesRoot = "/.crosspoint/weread/notes";

std::string notes_dir(const char* book_id) {
    return std::string(kNotesRoot) + "/" + book_id;
}
std::string chapter_cache_path(const char* book_id, uint32_t index) {
    char name[128];
    snprintf(name, sizeof(name), "%s/%.32s/ch_%06u.bin", kNotesRoot, book_id, (unsigned)index);
    return name;
}
std::string meta_cache_path(const char* book_id) {
    char name[128];
    snprintf(name, sizeof(name), "%s/%.32s/meta.bin", kNotesRoot, book_id);
    return name;
}

bool read_u16_file(HalFile& file, uint16_t* out) { return file.read(out, sizeof(*out)) == (int)sizeof(*out); }
bool read_u32_file(HalFile& file, uint32_t* out) { return file.read(out, sizeof(*out)) == (int)sizeof(*out); }

bool write_u16(HalFile& file, uint16_t v) { return file.write(&v, sizeof(v)) == sizeof(v); }
bool write_u32(HalFile& file, uint32_t v) { return file.write(&v, sizeof(v)) == sizeof(v); }
bool read_text_file(HalFile& file, std::string& out) {
    uint16_t len = 0;
    if (!read_u16_file(file, &len)) return false;
    out.resize(len);
    if (len && file.read(out.data(), len) != (int)len) return false;
    return true;
}
// 定长槽读取：u16 len + len 字节后跳过保留区，使指针落在槽尾（骨架按
// write_text_reserved 落盘，变长 read_text_file 会停在槽中间导致后续全错位）。
// / Fixed-slot read: skip the reserved tail after the payload so the cursor
// / lands on the slot boundary (skeletons use write_text_reserved; a plain
// / variable-length read would stop mid-slot and desync the whole stream).
bool read_text_slot(HalFile& file, std::string& out) {
    uint16_t len = 0;
    if (!read_u16_file(file, &len)) return false;
    if (len > WEREAD_NOTE_TEXT_CAP - 1) return false;
    out.assign(len, '\0');
    if (len && file.read(out.data(), len) != (int)len) return false;
    if (len < WEREAD_NOTE_TEXT_CAP - 1) file.seek(file.position() + (WEREAD_NOTE_TEXT_CAP - 1 - len));
    return true;
}
// 读一条骨架记录：定长原文槽 + 计数 + 链首（链首供想法链遍历）。
// / Read one skeleton record: fixed text slot + count + chain head.
bool read_skeleton_row(HalFile& file, std::string& text, uint32_t* reviews, uint32_t* head) {
    return read_text_slot(file, text) && read_u32_file(file, reviews) && read_u32_file(file, head);
}

bool ensure_meta(const char* book_id, const char* owner_vid, uint32_t chapter_count) {
    const std::string path = meta_cache_path(book_id);
    HalFile probe;
    if (Storage.openFileForRead("meta", path, probe)) {
        uint32_t magic = 0;
        return read_u32_file(probe, &magic) && magic == kMetaMagic;
    }
    if (!Storage.ensureDirectoryExists(notes_dir(book_id))) return false;
    HalFile part;
    const std::string part_path = path + ".part";
    if (!Storage.openFileForWrite("meta", part_path, part)) return false;
    bool ok = part.write(&kMetaMagic, sizeof(kMetaMagic)) == sizeof(kMetaMagic);
    ok = ok && write_u16(part, kFormatVersion);
    char vid[64] = {};
    snprintf(vid, sizeof(vid), "%.63s", owner_vid ? owner_vid : "");
    ok = ok && part.write(vid, sizeof(vid)) == sizeof(vid);
    ok = ok && write_u32(part, chapter_count);
    part.close();
    if (!ok) {
        Storage.remove(part_path);
        return false;
    }
    return Storage.rename(part_path, path);
}

bool chapter_cache_exists(const char* book_id, uint32_t index) {
    HalFile probe;
    if (!Storage.openFileForRead("chapter", chapter_cache_path(book_id, index), probe)) return false;
    uint32_t magic = 0;
    uint16_t version = 0;
    // 版本不符视同不存在：旧格式的章自动重拉（如 v1 的 8 条/句 → v2 的 15 条/句）。
    // / Version mismatch counts as missing: stale chapters re-fetch automatically.
    return read_u32_file(probe, &magic) && magic == kChapterMagic &&
           read_u16_file(probe, &version) && version == kFormatVersion;
}

// ---- 远端数据结构。/ Remote data shapes. ----
struct HighlightRow {
    std::string range;
    std::string text;         ///< markText（阶段 A 写盘后立即释放）。/ Freed right after the phase-A write.
    uint32_t text_off = 0;    ///< 文件内 excerpt 槽偏移（回填重写用）。/ File offset of the text slot.
    uint32_t count_off = 0;   ///< 文件内想法计数偏移。/ File offset of the thought counter.
    uint32_t tail_off = 0;    ///< 链尾：最后一条想法的 next 字段偏移（0=空链）。
                              ///< / Tail: file offset of the last thought's next link (0 = empty chain).
    uint32_t count = 0;       ///< 已落盘想法条数。/ Thoughts flushed so far.
    uint32_t total_hint = 0;  ///< 服务器回包带的想法总数（可选字段）。/ Server-reported total when present.
    bool total_seen = false;
    bool text_empty = false;  ///< markText 缺失，待想法回填。/ markText missing; backfill pending.
};

bool parse_underlines(const std::string& body, std::vector<HighlightRow>& out) {
    PsramJsonScope json_scope;
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return false;
    static const char* kKeys[] = {"underlines", "updated", "bookmarks"};
    const cJSON* rows = nullptr;
    for (const char* key : kKeys) {
        rows = cJSON_GetObjectItem(root, key);
        if (cJSON_IsArray(rows)) break;
        rows = nullptr;
    }
    if (rows) {
        const cJSON* row = nullptr;
        cJSON_ArrayForEach(row, rows) {
            // range 键名兼容 range/markRange/bookmarkRange（撷思同款）。
            // / Range key aliases matching the plugin's compatibility set.
            const cJSON* range = cJSON_GetObjectItem(row, "range");
            if (!cJSON_IsString(range)) range = cJSON_GetObjectItem(row, "markRange");
            if (!cJSON_IsString(range)) range = cJSON_GetObjectItem(row, "bookmarkRange");
            if (!cJSON_IsString(range) || !range->valuestring[0]) continue;
            HighlightRow item;
            item.range = range->valuestring;
            // 入库即截断：range/text 超长部分对匹配无用，白吃内部 RAM。
            // / Clip at ingest; oversize tails never help matching, only RAM.
            if (item.range.size() > 63) item.range.resize(63);
            const cJSON* text = cJSON_GetObjectItem(row, "markText");
            if (cJSON_IsString(text) && text->valuestring) item.text = text->valuestring;
            if (item.text.size() > 511) item.text.resize(511);
            out.push_back(std::move(item));
        }
    }
    cJSON_Delete(root);
    return true;
}

// ---- 全量翻页与流式落盘。/ Full paging with streaming writes. ----
// 64 位 FNV-1a 内容哈希：防止服务器忽略 maxIdx 重复回同一页导致死循环。
// / 64-bit FNV-1a content hashes; guards against a server ignoring maxIdx.
uint64_t content_hash(const char* s, size_t len) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ull;
    }
    return h ? h : 1;  // 0 作空槽标记 / 0 marks an empty slot
}
bool seen_has(const uint64_t* slots, uint64_t h) {
    // 探测步数有界：满表时直接判不含，绝不自旋（worker 曾因此占死 CPU1）。
    // / Bounded probing: a full pool just misses; probing must never spin.
    for (uint32_t n = 0, i = (uint32_t)(h % kSeenSlots);
         slots[i] && n < kSeenSlots; ++n, i = (uint32_t)((i + 1) % kSeenSlots))
        if (slots[i] == h) return true;
    return false;
}
// 满表返回 false 放弃去重：单句写入侧上限会先截停超热句，此处只是兜底。
// / A full pool gives up dedup: the write-side per-sentence cap stops hot
// / sentences first; this is the last-resort guard.
bool seen_put(uint64_t* slots, uint64_t h) {
    for (uint32_t n = 0, i = (uint32_t)(h % kSeenSlots); n < kSeenSlots;
         ++n, i = (uint32_t)((i + 1) % kSeenSlots)) {
        if (!slots[i]) {
            slots[i] = h;
            return true;
        }
        if (slots[i] == h) return true;
    }
    return false;
}

// 定长文本槽：u16 实际长度 + reserve 字节保留区（零填充），回填可原位重写不挪文件。
// / Fixed text slot: u16 length + reserved bytes, so backfill rewrites in place.
bool write_text_reserved(HalFile& file, const std::string& text, size_t reserve) {
    const size_t len = text.size() < reserve ? text.size() : reserve;
    if (!write_u16(file, (uint16_t)len)) return false;
    if (len && file.write(text.data(), len) != (int)len) return false;
    static const char pad[128] = {};
    for (size_t done = len; done < reserve;) {
        const size_t n = reserve - done < sizeof(pad) ? reserve - done : sizeof(pad);
        if (file.write(pad, n) != (int)n) return false;
        done += n;
    }
    return true;
}
bool rewrite_text(HalFile& file, const char* text, size_t reserve) {
    const size_t len = strnlen(text, reserve);
    if (!write_u16(file, (uint16_t)len)) return false;
    return len == 0 || file.write(text, len) == (int)len;
}

// 单页响应流式解析：匹配本批 range，新想法即刻落盘并回填缺失的划线原文。
// 返回 1 = 解析 OK（*added 新增条数），0 = 响应非法，-1 = 落盘失败。
// / Stream one review page: new thoughts for this batch's ranges flush to SD as
// / parsed; missing excerpts get backfilled. 1 = ok, 0 = bad payload, -1 = write error.
int stream_review_page(const std::string& body, std::vector<HighlightRow>& rows,
                       size_t base, size_t end, HalFile& part, uint64_t* seen_pool,
                       char (*backfills)[WEREAD_NOTE_TEXT_CAP], bool* backfill_seen,
                       unsigned* added) {
    *added = 0;
    PsramJsonScope json_scope;
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return 0;
    int result = 1;
    const cJSON* blocks = cJSON_GetObjectItem(root, "reviews");
    if (cJSON_IsArray(blocks)) {
        const cJSON* block = nullptr;
        cJSON_ArrayForEach(block, blocks) {
            if (result != 1) break;
            const cJSON* range_item = cJSON_IsObject(block) ? cJSON_GetObjectItem(block, "range") : nullptr;
            if (!cJSON_IsString(range_item) || !range_item->valuestring[0]) continue;
            // 定位本批 range；服务器偶尔回批外内容直接忽略。/ Out-of-batch echoes are ignored.
            size_t k = end;
            for (size_t q = base; q < end; ++q)
                if (rows[q].range == range_item->valuestring) { k = q; break; }
            if (k == end) continue;
            const size_t slot = k - base;
            // 回包带该句想法总数（可选字段）：首次见到即记录，供进度 N/M 显示。
            // / Optional per-range total: recorded once for the N/M progress line.
            if (!rows[k].total_seen) {
                const cJSON* total = cJSON_GetObjectItem(block, "total");
                if (cJSON_IsNumber(total) && total->valuedouble >= 0) {
                    rows[k].total_hint = (uint32_t)total->valuedouble;
                    rows[k].total_seen = true;
                }
            }
            // pageReviews 缺失时按单条处理（撷思同款兼容）。/ Fall back to a single entry.
            const cJSON* entries = cJSON_IsObject(block) ? cJSON_GetObjectItem(block, "pageReviews") : nullptr;
            const cJSON* cursor = cJSON_IsArray(entries) ? entries->child : block;
            for (; cursor; cursor = cursor->next) {
                const cJSON* thought = cJSON_IsObject(cursor) ? cJSON_GetObjectItem(cursor, "review") : nullptr;
                if (!cJSON_IsObject(thought)) thought = cursor;
                const cJSON* content = cJSON_GetObjectItem(thought, "content");
                if (!cJSON_IsString(content) || !content->valuestring[0]) continue;
                char body_buf[WEREAD_NOTE_CONTENT_CAP];
                char who_buf[WEREAD_NOTE_AUTHOR_CAP];
                snprintf(body_buf, sizeof(body_buf), "%s", content->valuestring);
                who_buf[0] = 0;
                const cJSON* author = cJSON_GetObjectItem(thought, "author");
                if (cJSON_IsObject(author)) {
                    const cJSON* name = cJSON_GetObjectItem(author, "name");
                    const cJSON* nick = cJSON_GetObjectItem(author, "nick");
                    if (cJSON_IsString(name) && name->valuestring[0])
                        snprintf(who_buf, sizeof(who_buf), "%s", name->valuestring);
                    else if (cJSON_IsString(nick) && nick->valuestring[0])
                        snprintf(who_buf, sizeof(who_buf), "%s", nick->valuestring);
                }
                uint32_t likes = 0;
                const cJSON* like = cJSON_GetObjectItem(cursor, "likesCount");
                if (!cJSON_IsNumber(like)) like = cJSON_GetObjectItem(thought, "likesCount");
                if (!cJSON_IsNumber(like)) like = cJSON_GetObjectItem(block, "likesCount");
                if (cJSON_IsNumber(like)) likes = (uint32_t)like->valuedouble;
                const size_t body_len = strnlen(body_buf, sizeof(body_buf) - 1);
                // 单句封顶在写入侧强制生效：超热句（数千条想法）到此为止，去重池
                // 永不溢出；封顶后 added 只计其他句，其他句回空时 added=0 判停。
                // / Enforce the per-sentence cap at write time: ultra-hot sentences
                // / stop here so the dedup pool can't overflow; remaining paging is
                // / for the other sentences and stops when they run dry (added=0).
                if (rows[k].count >= kMaxThoughtsPerRange) continue;
                if (seen_has(seen_pool + slot * kSeenSlots, content_hash(body_buf, body_len))) continue;
                // 回填：markText 缺失时优先 abstract，否则退用首条想法正文（同源文本）。
                // / Backfill missing markText with abstract, else the first thought body.
                if (rows[k].text_empty && !backfill_seen[slot]) {
                    const cJSON* ab = cJSON_GetObjectItem(thought, "abstract");
                    if (!cJSON_IsString(ab) || !ab->valuestring[0])
                        ab = cJSON_GetObjectItem(thought, "contextAbstract");
                    snprintf(backfills[slot], WEREAD_NOTE_TEXT_CAP, "%s",
                             (cJSON_IsString(ab) && ab->valuestring[0]) ? ab->valuestring : body_buf);
                    backfill_seen[slot] = true;
                }
                // 即刻落盘：内存永远只压住当前一页；失败交由整章原子兜底（.part 丢弃）。
                // / Flush immediately; RAM only holds the current page. A write error
                // / falls back to the chapter-atomic path (the .part is discarded).
                const size_t who_len = strnlen(who_buf, sizeof(who_buf) - 1);
                // 想法块 = [next u32][body 槽][who 槽][likes u32]，按到达顺序追加文件尾，
                // 用单链表归属句子：首条回填骨架 head，其余回补链尾 next，最后 seek 回 EOF。
                // / Thought block = [next u32][body][who][likes], appended in arrival order
                // / and linked per sentence: the first patch fills the skeleton head, the
                // / rest patch the previous tail's next, then the cursor returns to EOF.
                const uint32_t block_off = (uint32_t)part.position();
                const bool wrote = write_u32(part, 0) &&
                                   write_u16(part, (uint16_t)body_len) &&
                                   (!body_len || part.write(body_buf, body_len) == (int)body_len) &&
                                   write_u16(part, (uint16_t)who_len) &&
                                   (!who_len || part.write(who_buf, who_len) == (int)who_len) &&
                                   write_u32(part, likes);
                bool linked = wrote;
                if (linked)
                    linked = rows[k].tail_off
                                 ? part.seek(rows[k].tail_off) && write_u32(part, block_off)
                                 : part.seek(rows[k].count_off + 4) && write_u32(part, block_off);
                if (linked) linked = part.seek(block_off + 4 + 2 + body_len + 2 + who_len + 4);
                if (!linked) {
                    result = -1;
                    break;
                }
                rows[k].tail_off = block_off;
                seen_put(seen_pool + slot * kSeenSlots, content_hash(body_buf, body_len));
                ++rows[k].count;
                ++*added;
            }
        }
    }
    cJSON_Delete(root);
    return result;
}

// ---- 章缓存流式写：开 .part 写头（总划线数已知），逐批追加，全部成功才改名。
// ---- 落盘中途失败即删 .part，RAM 里永远只压住当前一批想法。
// ---- Streaming chapter cache: header up front, per-batch appends, rename on full success.
bool begin_chapter_cache(const char* book_id, uint32_t index, const char* chapter_uid,
                         uint32_t total_rows, HalFile& part, std::string& part_path) {
    if (!Storage.ensureDirectoryExists(notes_dir(book_id))) return false;
    part_path = chapter_cache_path(book_id, index) + ".part";
    if (!Storage.openFileForWrite("chapter", part_path, part)) return false;
    bool ok = part.write(&kChapterMagic, sizeof(kChapterMagic)) == sizeof(kChapterMagic);
    ok = ok && write_u16(part, kFormatVersion);
    char uid[64] = {};
    snprintf(uid, sizeof(uid), "%.63s", chapter_uid ? chapter_uid : "");
    ok = ok && part.write(uid, sizeof(uid)) == sizeof(uid);
    ok = ok && write_u32(part, total_rows);
    if (!ok) {
        part.close();
        Storage.remove(part_path);
    }
    return ok;
}

bool finish_chapter_cache(HalFile& part, const std::string& part_path, const std::string& final_path) {
    part.close();
    // 目标已存在（旧版本缓存）时 FatFS rename 返回 FR_EXIST：先删旧文件再改名。
    // / FatFS rename fails with FR_EXIST when the target exists (stale old-format
    // / cache); unlink it first, then one retry guards transient SD errors.
    Storage.remove(final_path);
    if (!Storage.rename(part_path, final_path)) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!Storage.rename(part_path, final_path)) {
            Storage.remove(part_path);
            return false;
        }
    }
    return true;
}

void discard_chapter_cache(HalFile& part, const std::string& part_path) {
    part.close();
    Storage.remove(part_path);
}

}  // namespace

extern "C" int weread_notes_fetch(const char* book_id,
                                  bool (*cancel)(void* ctx), void* cancel_ctx,
                                  void (*progress)(void* ctx, unsigned done, unsigned total),
                                  void* progress_ctx,
                                  void (*notes_progress)(void* ctx, unsigned notes_done,
                                                         unsigned notes_total),
                                  void* notes_ctx) {
    if (!book_id || !book_id[0]) return 12;
    WeReadStore::Session session;
    if (!WeReadStore::loadSession(session) || !session.valid()) {
        ESP_LOGW(kTag, "notes fetch: no session");
        return 3;  // 登录失效 / Auth lost.
    }
    HalFile toc;
    uint32_t chapter_count = 0;
    const std::string toc_path = WeReadStore::tocPath(book_id);
    const bool toc_open = WeReadStore::openToc(toc_path, toc, chapter_count);
    if (!toc_open || !chapter_count) {
        // 目录缓存缺失或为空：多半是本地没有本书的引擎下载记录。/ No engine download record locally.
        ESP_LOGW(kTag, "notes fetch: toc unavailable book=%s path=%s open=%d count=%u",
                 book_id, toc_path.c_str(), (int)toc_open, (unsigned)chapter_count);
        return 12;
    }
    if (!ensure_meta(book_id, session.vid, chapter_count)) {
        ESP_LOGW(kTag, "notes fetch: meta unavailable");
        return 6;  // 存储失败 / Storage failure.
    }
    // 上下文约 10KB（含 8KB 读缓冲），必须放堆：worker 栈只有 16KB，栈上必溢出。
    // / The ~10KB context (8KB read buffer) lives on the heap; the 16KB worker stack cannot hold it.
    void* ctx_memory = heap_caps_calloc(1, sizeof(FetchCtx), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx_memory) {
        ESP_LOGW(kTag, "notes fetch: OOM");
        return 10;  // 内存不足 / Out of memory.
    }
    FetchCtx& ctx = *new (ctx_memory) FetchCtx();
    ctx.session = session;
    ctx.cancel = cancel;
    ctx.cancel_ctx = cancel_ctx;
    unsigned done = 0;
    unsigned long long book_notes = 0, book_hint_base = 0;  ///< 全书想法累计 / 已知总数累计
    ESP_LOGI(kTag, "notes fetch start: book=%s chapters=%u", book_id, (unsigned)chapter_count);
    if (progress) progress(progress_ctx, 0, chapter_count);  // 先亮出章总数 / surface the chapter total right away
    log_mem("fetch entry");
    for (uint32_t i = 0; i < chapter_count; ++i) {
        if (ctx.stopped()) { ctx.~FetchCtx(); heap_caps_free(ctx_memory); return 2; }
        if (chapter_cache_exists(book_id, i)) {
            ++done;
            if (progress) progress(progress_ctx, done, chapter_count);
            continue;
        }
        WeReadStore::TocRecord record;
        if (!WeReadStore::readTocRecord(toc, i, record) || !record.chapterUid[0]) {
            ++done;
            if (progress) progress(progress_ctx, done, chapter_count);
            continue;
        }
        const long long chapter_uid = strtoll(record.chapterUid, nullptr, 10);
        if (chapter_uid <= 0) {
            ++done;
            if (progress) progress(progress_ctx, done, chapter_count);
            continue;
        }
        // 划线列表：GET underlines，失败重试一次。/ Chapter highlights with one retry.
        char where[48];
        snprintf(where, sizeof(where), "ch%u begin", (unsigned)i);
        log_mem(where);
        ESP_LOGI(kTag, "ch%u uid=%lld fetch underlines", (unsigned)i, chapter_uid);
        std::vector<HighlightRow> rows;
        bool ok = false;
        for (int attempt = 0; attempt < 2 && !ok && !ctx.stopped(); ++attempt) {
            rows.clear();  // 重试不得叠加半截解析结果 / never stack partial parses on retry
            int status = 0;
            char url[320];
            snprintf(url, sizeof(url), "%s/web/book/underlines?bookId=%.48s&chapterUid=%lld",
                     kHost, book_id, chapter_uid);
            const HttpOutcome outcome = authed_exchange(ctx, "underlines", url, false, nullptr, status);
            if (outcome == HttpOutcome::Ok && status == 200) ok = parse_underlines(ctx.body, rows);
            else if (outcome == HttpOutcome::SessionExpired) break;
            else vTaskDelay(pdMS_TO_TICKS(500));
        }
        ESP_LOGI(kTag, "ch%u underlines rows=%u", (unsigned)i, (unsigned)rows.size());
        if (!ok) {
            ESP_LOGW(kTag, "notes: underlines failed at chapter %u", (unsigned)i);
            ctx.~FetchCtx(); heap_caps_free(ctx_memory);
            return 2;  // 断点续传：已完成章保留，下次从这里继续。
        }
        // 流式章缓存：头部先写（总划线数已知），想法逐批拉、逐批落盘、逐批释放。
        // / Streaming chapter cache: header first; thoughts fetched, flushed and freed per batch.
        HalFile part;
        std::string part_path;
        const std::string final_path = chapter_cache_path(book_id, i);
        if (!begin_chapter_cache(book_id, i, record.chapterUid, (uint32_t)rows.size(), part, part_path)) {
            ESP_LOGW(kTag, "notes: cache begin failed at chapter %u", (unsigned)i);
            ctx.~FetchCtx(); heap_caps_free(ctx_memory);
            return 6;
        }
        // 阶段 A：全章块骨架一次写盘（定长划线原文槽 + 计数 + 链首占位），text 写后
        // 立即释放；此后翻页只携带 range 字符串，整章常驻内部 RAM 约 6KB。
        // / Phase A: write every block skeleton (fixed text slot + counter + chain
        // / head), freeing the text right after; paging carries range strings only.
        bool skeleton = true;
        for (size_t k = 0; k < rows.size() && skeleton; ++k) {
            rows[k].text_off = part.position();
            rows[k].text_empty = rows[k].text.empty();
            skeleton = write_text_reserved(part, rows[k].text, WEREAD_NOTE_TEXT_CAP - 1);
            if (skeleton) {
                rows[k].count_off = part.position();
                skeleton = write_u32(part, 0) && write_u32(part, 0);  // count + 链首 head / count + chain head
            }
            std::string().swap(rows[k].text);
        }
        if (!skeleton) {
            ESP_LOGW(kTag, "notes: skeleton write failed at chapter %u", (unsigned)i);
            discard_chapter_cache(part, part_path);
            ctx.~FetchCtx(); heap_caps_free(ctx_memory);
            return 6;
        }
        // 全量翻页：每批 3 句，每页 maxIdx = 该句已收集条数（翻页偏移），synckey 恒 0
        //（撷思/官方 Eink 同款）。时间换数据：逐页限速、逐页流式落盘，内存只压当前
        // 响应（~68KB）；服务器若忽略 maxIdx 重复回同一页，去重使 new=0 自然判停，
        // 加上单句 200 条、单批 40 页两道保险，任何回包行为都有界可控。
        // / Full paging: batches of 3 sentences, per-page maxIdx = collected counts,
        // / synckey stays 0. Time for data: paced pages streamed straight to SD while
        // / RAM only holds the current response (~68KB). If the server ignores maxIdx
        // / the dedup makes new=0 stop naturally; per-sentence/batch caps bound it all.
        unsigned long long chapter_notes = 0;
        for (size_t base = 0; base < rows.size() && !ctx.stopped(); base += kBatchHighlights) {
            const size_t end = base + kBatchHighlights < rows.size() ? base + kBatchHighlights : rows.size();
            const size_t span = end - base;
            uint64_t* seen_pool = static_cast<uint64_t*>(heap_caps_calloc(
                span * kSeenSlots, sizeof(uint64_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!seen_pool) {
                ESP_LOGW(kTag, "notes: dedup pool OOM at chapter %u", (unsigned)i);
                discard_chapter_cache(part, part_path);
                ctx.~FetchCtx(); heap_caps_free(ctx_memory);
                return 10;
            }
            char backfills[kBatchHighlights][WEREAD_NOTE_TEXT_CAP] = {{0}};
            bool backfill_seen[kBatchHighlights] = {};
            unsigned pages = 0;
            while (!ctx.stopped()) {
                std::string payload = "{\"bookId\":\"";
                payload += book_id;
                payload += "\",\"chapterUid\":";
                payload += std::to_string(chapter_uid);
                payload += ",\"reviews\":[";
                for (size_t k = base; k < end; ++k) {
                    if (k > base) payload += ",";
                    payload += "{\"range\":\"";
                    payload += rows[k].range;
                    payload += "\",\"maxIdx\":";
                    payload += std::to_string(rows[k].count);
                    payload += ",\"count\":";
                    payload += std::to_string(kReviewsPerRange);
                    payload += ",\"synckey\":0}";
                }
                payload += "]}";
                ESP_LOGI(kTag, "ch%u batch base=%u page=%u maxIdx=%u payload=%u", (unsigned)i,
                         (unsigned)base, (unsigned)pages, (unsigned)rows[base].count,
                         (unsigned)payload.size());
                int status = 0;
                char url[320];
                snprintf(url, sizeof(url), "%s/web/book/readReviews", kHost);
                // 网络抖动重试一次；会话死透才放弃整本（断点续传保住已完成章）。
                // / One network retry per page; a dead session aborts the whole run.
                HttpOutcome outcome = HttpOutcome::Network;
                for (int attempt = 0; attempt < 2 && !ctx.stopped(); ++attempt) {
                    status = 0;
                    outcome = authed_exchange(ctx, "readReviews", url, true, payload.c_str(), status);
                    if (outcome != HttpOutcome::Network) break;
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                if (outcome == HttpOutcome::SessionExpired) {
                    ESP_LOGW(kTag, "notes: session dead at chapter %u", (unsigned)i);
                    heap_caps_free(seen_pool);
                    discard_chapter_cache(part, part_path);
                    ctx.~FetchCtx(); heap_caps_free(ctx_memory);
                    return 3;
                }
                if (outcome != HttpOutcome::Ok || status != 200) {
                    ESP_LOGW(kTag, "notes: readReviews failed (%d) at chapter %u", status, (unsigned)i);
                    heap_caps_free(seen_pool);
                    discard_chapter_cache(part, part_path);
                    ctx.~FetchCtx(); heap_caps_free(ctx_memory);
                    return 2;
                }
                unsigned added = 0;
                const int stream =
                    stream_review_page(ctx.body, rows, base, end, part, seen_pool, backfills,
                                       backfill_seen, &added);
                if (stream < 0) {
                    // 诊断落盘失败：打印卡剩余空间，区分「卡满」与「卡故障」。
                    // / Diagnose the write failure: log the card's free space.
                    struct statvfs vfs;
                    if (statvfs("/sdcard", &vfs) == 0)
                        ESP_LOGW(kTag, "notes: page write failed ch%u free=%uMB", (unsigned)i,
                                 (unsigned)((uint64_t)vfs.f_bfree * vfs.f_frsize >> 20));
                    else
                        ESP_LOGW(kTag, "notes: page write failed ch%u (statvfs errno=%d)",
                                 (unsigned)i, errno);
                    heap_caps_free(seen_pool);
                    discard_chapter_cache(part, part_path);
                    ctx.~FetchCtx(); heap_caps_free(ctx_memory);
                    return 6;
                }
                ++pages;
                chapter_notes += added;
                // 双粒度进度：章（progress）+ 条（notes_progress，已知总数为服务器提示累计）。
                // / Dual-granularity progress: chapters + thought counts (server hints summed).
                unsigned long long hint_sum = book_hint_base;
                for (const auto& row : rows)
                    if (row.total_seen) hint_sum += row.total_hint;
                if (notes_progress)
                    notes_progress(notes_ctx, (unsigned)(book_notes + chapter_notes),
                                   (unsigned)hint_sum);
                ESP_LOGI(kTag, "ch%u batch base=%u page=%u new=%u chapter_thoughts=%u",
                         (unsigned)i, (unsigned)base, (unsigned)(pages - 1), added,
                         (unsigned)chapter_notes);
                bool capped = true;
                for (size_t k = base; k < end; ++k)
                    if (rows[k].count < kMaxThoughtsPerRange) capped = false;
                if (!added || capped || pages >= kMaxPagesPerBatch) break;
                vTaskDelay(pdMS_TO_TICKS(kBatchPauseMs));
            }
            // 批收尾：回填缺失的划线原文、原位写回每句计数，然后回到文件尾继续追加。
            // / Batch epilogue: backfill missing excerpts in place, patch the counters,
            // / then rewind to EOF so the next batch appends after everything.
            const uint32_t eof = (uint32_t)part.position();  // 追加流尾 / append-stream tail
            bool patched = true;
            uint32_t batch_thoughts = 0;
            for (size_t k = base; k < end && patched; ++k) {
                HighlightRow& row = rows[k];
                const size_t slot = k - base;
                if (row.text_empty && backfill_seen[slot] && backfills[slot][0])
                    patched = part.seek(row.text_off) &&
                              rewrite_text(part, backfills[slot], WEREAD_NOTE_TEXT_CAP - 1);
                if (patched) patched = part.seek(row.count_off) && write_u32(part, row.count);
            }
            if (patched) patched = part.seek(eof);
            for (size_t k = base; k < end; ++k) batch_thoughts += rows[k].count;
            heap_caps_free(seen_pool);
            if (!patched) {
                ESP_LOGW(kTag, "notes: counter patch failed at chapter %u", (unsigned)i);
                discard_chapter_cache(part, part_path);
                ctx.~FetchCtx(); heap_caps_free(ctx_memory);
                return 6;
            }
            ESP_LOGI(kTag, "ch%u batch base=%u done: %u thought(s) on %u sentence(s)",
                     (unsigned)i, (unsigned)base, batch_thoughts, (unsigned)(end - base));
            vTaskDelay(pdMS_TO_TICKS(kBatchPauseMs));
        }
        if (ctx.stopped()) {
            discard_chapter_cache(part, part_path);
            ctx.~FetchCtx(); heap_caps_free(ctx_memory);
            return 2;  // 取消路径由 worker 裁决状态 / Caller decides the cancelled state.
        }
        log_mem("pre-rename");
        if (!finish_chapter_cache(part, part_path, final_path)) {
            ESP_LOGW(kTag, "notes: cache finalize failed at chapter %u", (unsigned)i);
            ctx.~FetchCtx(); heap_caps_free(ctx_memory);
            return 6;
        }
        ++done;
        if (progress) progress(progress_ctx, done, chapter_count);
        // 全书记录：累计想法条数与已知总数，串口留痕 + 条粒度进度收口。
        // / Book-level record: running totals logged and reported once more.
        uint32_t ch_thoughts = 0;
        for (const auto& row : rows) ch_thoughts += row.count;
        book_notes += ch_thoughts;
        for (const auto& row : rows)
            if (row.total_seen) book_hint_base += row.total_hint;
        if (notes_progress)
            notes_progress(notes_ctx, (unsigned)book_notes, (unsigned)book_hint_base);
        log_mem("ch done");
        ESP_LOGI(kTag, "notes: chapter %u done, highlights=%u thoughts=%u book_total=%llu hint=%llu",
                 (unsigned)i, (unsigned)rows.size(), ch_thoughts, book_notes, book_hint_base);
        vTaskDelay(pdMS_TO_TICKS(kChapterPauseMs));
    }
    ESP_LOGI(kTag, "notes fetch complete: book=%s chapters=%u", book_id, (unsigned)chapter_count);
    ctx.~FetchCtx();
    heap_caps_free(ctx_memory);
    return 0;
}

// ---- 读取桥（UI 线程）：绑定后按 spine 查询已缓存章。 ----
// ---- Read bridge (UI thread): spine-based queries over finished chapter caches. ----
namespace {

struct NotesBinding {
    char book[64] = {};
    char vid[64] = {};
    char (*uids)[64] = nullptr;  ///< spine → chapterUid。/ Chapter uid per spine.
    uint32_t count = 0;
};
NotesBinding g_notes;

bool open_chapter_file(uint32_t spine, HalFile& file, char* chapter_uid, uint32_t* highlight_count) {
    if (spine >= g_notes.count) return false;
    file.close();
    if (!Storage.openFileForRead("chapter", chapter_cache_path(g_notes.book, spine), file)) return false;
    uint32_t magic = 0;
    uint16_t version = 0;
    if (!read_u32_file(file, &magic) || magic != kChapterMagic || !read_u16_file(file, &version)) return false;
    if (version != kFormatVersion) return false;
    if (file.read(chapter_uid, 64) != 64) return false;
    if (!read_u32_file(file, highlight_count)) return false;
    return true;
}

}  // namespace

extern "C" bool weread_notes_bind(const char* epub_path) {
    char book[64] = {};
    if (!epub_path) return false;
    if (!WeReadStore::findBookIdForPath(epub_path, book, sizeof(book))) {
        // 诊断：区分书架打不开 vs 记录不匹配；hex 对比排查中文/全角括号编码差异。
        // / Diagnose: shelf unreadable vs no match; hex compare to spot encoding drift.
        HalFile shelf;
        uint32_t count = 0;
        if (!WeReadStore::openShelf(shelf, count)) {
            // 三个失败维度一次打全：取消标志、虚拟→物理映射、目标文件 stat。
            // / Print all three failure axes at once: cancel flag, mapping, stat.
            const std::string mapped = Storage.map(WeReadStore::kShelfPath);
            struct stat st = {};
            const int st_ok = mapped.empty() ? -2 : stat(mapped.c_str(), &st);
            ESP_LOGW(kTag, "bind: shelf open failed cancelled=%d map='%s' stat=%d size=%ld errno=%d",
                     (int)pico_weread_cancelled(), mapped.c_str(), st_ok,
                     st_ok == 0 ? (long)st.st_size : -1L, st_ok == 0 ? 0 : (int)errno);
        } else {
            ESP_LOGW(kTag, "bind: no match in %u shelf records", count);
            WeReadStore::ShelfRecord record;
            for (uint32_t i = 0; i < count && i < 40; ++i) {
                if (!WeReadStore::readShelfRecord(shelf, i, record)) break;
                if (!strstr(record.title, "乡村") && !strstr(epub_path, "乡村")) continue;
                char hex[49] = {};
                for (size_t j = 0; j < 16 && record.title[j]; ++j)
                    snprintf(hex + j * 3, sizeof(hex) - j * 3, "%02x ",
                             static_cast<unsigned char>(record.title[j]));
                ESP_LOGI(kTag, "shelf[%u] title='%s' hex=%s id=%s", (unsigned)i, record.title, hex,
                         record.bookId);
            }
            char path_hex[49] = {};
            const size_t path_len = strnlen(epub_path, 96);
            for (size_t j = path_len > 16 ? path_len - 16 : 0; j < path_len; ++j)
                snprintf(path_hex + (j - (path_len > 16 ? path_len - 16 : 0)) * 3,
                         sizeof(path_hex) - (j - (path_len > 16 ? path_len - 16 : 0)) * 3, "%02x ",
                         static_cast<unsigned char>(epub_path[j]));
            ESP_LOGI(kTag, "path tail hex=%s", path_hex);
        }
        return false;
    }
    WeReadStore::Session session;
    if (!WeReadStore::loadSession(session) || !session.vid[0]) {
        ESP_LOGW(kTag, "bind: no session (book=%s)", book);
        return false;
    }
    HalFile toc;
    uint32_t count = 0;
    if (!WeReadStore::openToc(WeReadStore::tocPath(book), toc, count) || !count) {
        ESP_LOGW(kTag, "bind: toc missing (book=%s path=%s)", book, WeReadStore::tocPath(book).c_str());
        return false;
    }
    char (*uids)[64] = static_cast<char (*)[64]>(
        heap_caps_calloc(count, 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!uids) return false;
    for (uint32_t i = 0; i < count; ++i) {
        WeReadStore::TocRecord record;
        if (WeReadStore::readTocRecord(toc, i, record))
            snprintf(uids[i], 64, "%.63s", record.chapterUid);
    }
    weread_notes_unbind();
    snprintf(g_notes.book, sizeof(g_notes.book), "%s", book);
    snprintf(g_notes.vid, sizeof(g_notes.vid), "%s", session.vid);
    g_notes.uids = uids;
    g_notes.count = count;
    return true;
}

extern "C" void weread_notes_unbind(void) {
    heap_caps_free(g_notes.uids);
    g_notes = NotesBinding{};
}

extern "C" bool weread_notes_ready(void) { return g_notes.count != 0; }

extern "C" bool weread_notes_book_id(char* out, size_t cap) {
    if (!out || !cap || !g_notes.count || !g_notes.book[0]) return false;
    snprintf(out, cap, "%s", g_notes.book);
    return true;
}

// ---- 书级只读统计：书架角标与详情页标记（不依赖 bind，直接扫缓存目录）。 ----
// ---- Book-level read-only stats: shelf badge and detail marker (bind-free). ----
extern "C" bool weread_notes_book_stats(const char* book_id, unsigned* chapters_cached,
                                        unsigned* chapters_total, unsigned* highlights) {
    if (chapters_cached) *chapters_cached = 0;
    if (chapters_total) *chapters_total = 0;
    if (highlights) *highlights = 0;
    if (!book_id || !book_id[0]) return false;
    HalFile dir;
    if (!dir.open(Storage.map(notes_dir(book_id)), "rb") || !dir.isDirectory()) return false;
    uint32_t rows_total = 0, files = 0;
    char name[64];
    while (true) {
        HalFile next = dir.openNextFile();
        if (!next.isOpen()) break;
        const size_t len = next.getName(name, sizeof(name));
        // 只认 ch_XXXXXX.bin：.part 半成品与 meta.bin 不计入。/ Only finished ch files.
        if (len < 6 || strncmp(name, "ch_", 3) || strcmp(name + len - 4, ".bin")) continue;
        // 章头 = magic u32 + version u16 + uid[64] + total_rows u32 → 偏移 70，只读头部。
        // / Chapter header: magic + version + uid[64] + total_rows at offset 70; header only.
        uint32_t rows = 0;
        if (next.seek(70) && read_u32_file(next, &rows)) {
            rows_total += rows;
            ++files;
        }
    }
    if (!files) return false;
    if (chapters_cached) *chapters_cached = (unsigned)files;
    if (highlights) *highlights = (unsigned)rows_total;
    if (chapters_total) {
        HalFile meta;
        // meta 头 = magic u32 + version u16 + vid[64] + chapter_count u32，同一偏移 70。
        // / meta shares the same 70-byte prefix; chapter_count sits at offset 70.
        uint32_t total = 0;
        if (Storage.openFileForRead("meta", meta_cache_path(book_id), meta) && meta.seek(70) &&
            read_u32_file(meta, &total))
            *chapters_total = (unsigned)total;
    }
    return true;
}

extern "C" bool weread_notes_chapter_uid(uint32_t spine, char* out, size_t cap) {
    if (!out || !cap || !g_notes.uids || spine >= g_notes.count || !g_notes.uids[spine][0]) return false;
    snprintf(out, cap, "%s", g_notes.uids[spine]);
    return true;
}

extern "C" bool weread_notes_chapter_done(uint32_t spine) {
    return spine < g_notes.count && chapter_cache_exists(g_notes.book, spine);
}

extern "C" unsigned weread_notes_chapter_highlights(uint32_t spine) {
    HalFile file;
    char uid[64];
    uint32_t count = 0;
    if (!open_chapter_file(spine, file, uid, &count)) return 0;
    return count;
}

extern "C" unsigned weread_notes_for_each_highlight(uint32_t spine, weread_notes_row_fn fn,
                                                    void* user) {
    if (!fn) return 0;
    HalFile file;
    char uid[64];
    uint32_t count = 0;
    if (!open_chapter_file(spine, file, uid, &count)) return 0;
    unsigned read = 0;
    for (uint32_t i = 0; i < count; ++i) {
        weread_highlight_t hl;
        memset(&hl, 0, sizeof(hl));
        std::string text;
        uint32_t reviews = 0, head = 0;
        // 布局 = 定长原文槽 + count + 链首；想法块按链表组织，骨架区顺序读完即可。
        // / Layout = fixed text slot + count + chain head; thoughts live in linked
        // / blocks, so iterating highlights reads the skeleton region sequentially.
        if (!read_skeleton_row(file, text, &reviews, &head)) break;
        snprintf(hl.text, sizeof(hl.text), "%s", text.c_str());
        hl.review_count = reviews;
        hl.index = i;
        ++read;
        if (!fn(user, &hl)) break;
    }
    return read;
}

extern "C" bool weread_notes_match(uint32_t spine, const char* line_utf8,
                                   weread_highlight_t* out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!line_utf8 || !line_utf8[0]) return false;
    char needle[WEREAD_NOTE_TEXT_CAP];
    normalize_copy(needle, sizeof(needle), line_utf8);
    if (!needle[0]) return false;
    HalFile file;
    char uid[64];
    uint32_t count = 0;
    if (!open_chapter_file(spine, file, uid, &count)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        std::string text;
        uint32_t reviews = 0, head = 0;
        if (!read_skeleton_row(file, text, &reviews, &head)) return false;
        char haystack[WEREAD_NOTE_TEXT_CAP];
        normalize_copy(haystack, sizeof(haystack), text.c_str());
        // 双向包含：行是划线的子串，或划线是行的子串（重叠划线取首条）。
        // / Bidirectional containment; overlapping highlights take the first match.
        if (strlen(needle) <= strlen(haystack) ? strstr(haystack, needle) != nullptr
                                               : strstr(needle, haystack) != nullptr) {
            file.close();
            if (out) {
                snprintf(out->text, sizeof(out->text), "%s", text.c_str());
                out->review_count = reviews;
                out->index = i;
            }
            return true;
        }
    }
    return false;
}

extern "C" bool weread_notes_highlight_at(uint32_t spine, unsigned index,
                                          weread_highlight_t* out) {
    if (out) memset(out, 0, sizeof(*out));
    HalFile file;
    char uid[64];
    uint32_t count = 0;
    if (!open_chapter_file(spine, file, uid, &count) || index >= count) return false;
    for (uint32_t i = 0; i <= index; ++i) {
        std::string text;
        uint32_t reviews = 0, head = 0;
        if (!read_skeleton_row(file, text, &reviews, &head)) return false;
        if (i == index) {
            file.close();
            if (out) {
                snprintf(out->text, sizeof(out->text), "%s", text.c_str());
                out->review_count = reviews;
                out->index = i;
            }
            return true;
        }
    }
    return false;
}

extern "C" unsigned weread_notes_review_count(uint32_t spine, unsigned highlight) {
    HalFile file;
    char uid[64];
    uint32_t count = 0;
    if (!open_chapter_file(spine, file, uid, &count) || highlight >= count) return 0;
    for (uint32_t i = 0; i <= highlight; ++i) {
        std::string text;
        uint32_t reviews = 0, head = 0;
        if (!read_skeleton_row(file, text, &reviews, &head)) return 0;
        if (i == highlight) return reviews;
    }
    return 0;
}

extern "C" bool weread_notes_review_at(uint32_t spine, unsigned highlight, unsigned index,
                                       weread_note_t* out) {
    if (out) memset(out, 0, sizeof(*out));
    HalFile file;
    char uid[64];
    uint32_t count = 0;
    if (!open_chapter_file(spine, file, uid, &count) || highlight >= count) return false;
    // 顺序走到目标句骨架，然后沿想法链走 index 步读取目标条。
    // / Walk skeletons to the target highlight, then follow its thought chain.
    for (uint32_t i = 0; i <= highlight; ++i) {
        std::string text;
        uint32_t reviews = 0, head = 0;
        if (!read_skeleton_row(file, text, &reviews, &head)) return false;
        if (i != highlight) continue;
        if (index >= reviews || !head) return false;
        uint32_t off = head;
        for (uint32_t t = 0; t < reviews && off; ++t) {
            std::string content, author;
            uint32_t likes = 0, next = 0;
            if (!file.seek(off) || !read_u32_file(file, &next) || !read_text_file(file, content) ||
                !read_text_file(file, author) || !read_u32_file(file, &likes))
                return false;
            if (t == index) {
                snprintf(out->content, sizeof(out->content), "%s", content.c_str());
                snprintf(out->author, sizeof(out->author), "%s", author.c_str());
                out->likes = likes;
                return true;
            }
            off = next;
        }
        return false;
    }
    return false;
}

// 流式游标：一次定位到目标句骨架，之后 next() 沿链逐条前进，每条恰好一次寻道。
// / Streaming cursor: locate the skeleton once, then next() walks the chain with
// / exactly one seek per thought.
struct weread_notes_cursor {
    HalFile file;
    uint32_t off = 0;        ///< 下一块偏移（0 = 链尽）/ Next node offset (0 = end).
    uint32_t remaining = 0;  ///< 尚未读出的条数 / Thoughts left to read.
};

extern "C" weread_notes_cursor_t* weread_notes_cursor_open(uint32_t spine, unsigned highlight,
                                                           unsigned* total) {
    auto* cur = new weread_notes_cursor();
    if (!cur) return nullptr;
    char uid[64];
    uint32_t count = 0;
    if (!open_chapter_file(spine, cur->file, uid, &count) || highlight >= count) {
        delete cur;
        return nullptr;
    }
    for (uint32_t i = 0; i <= highlight; ++i) {
        std::string text;
        uint32_t reviews = 0, head = 0;
        if (!read_skeleton_row(cur->file, text, &reviews, &head)) break;
        if (i == highlight) {
            cur->off = head;
            cur->remaining = reviews;
            if (total) *total = reviews;
            return cur;
        }
    }
    delete cur;
    return nullptr;
}

extern "C" bool weread_notes_cursor_next(weread_notes_cursor_t* cur, weread_note_t* out) {
    if (!cur || !out || !cur->off || !cur->remaining) return false;
    std::string content, author;
    uint32_t likes = 0, next = 0;
    if (!cur->file.seek(cur->off) || !read_u32_file(cur->file, &next) ||
        !read_text_file(cur->file, content) || !read_text_file(cur->file, author) ||
        !read_u32_file(cur->file, &likes))
        return false;
    memset(out, 0, sizeof(*out));
    snprintf(out->content, sizeof(out->content), "%s", content.c_str());
    snprintf(out->author, sizeof(out->author), "%s", author.c_str());
    out->likes = likes;
    cur->off = next;
    --cur->remaining;
    return true;
}

extern "C" void weread_notes_cursor_close(weread_notes_cursor_t* cur) { delete cur; }

/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：有界 EPUB 元数据解析；ZIP 和正文解析由独立后端负责。
 * English: Bounded EPUB metadata parsing; separate backends own ZIP and body conversion.
 *
 * 冻结：只读；不解析外部实体；最多 8192 个清单项与章节；累计原始 HTML 字节用于进度。
 * Frozen: Read-only; no external entity resolution; at most 8192 manifest items and chapters; progress uses source HTML bytes.
 * 为兼容图片较多的大 EPUB 扩大清单容量，章节仍保持有界。
 * The manifest capacity grows for image-rich EPUBs while chapter allocation remains bounded.
 * 目录兼容 EPUB 3 的 type/role 标记和无标记单导航；正文编号题头优先并写入索引。
 * Navigation accepts EPUB 3 type/role markers and an unmarked nav; numbered body headings take priority and are cached.
 */
#include "book_epub.h"
#include "book_cover.h"
#include "book_index_cache.h"
#include "zip_reader.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>

#define EPUB_MANIFEST_MAX 8192
#define EPUB_CHAPTER_MAX 8192
#define EPUB_NAV_MAX 8192
#define EPUB_XML_DEPTH 64
#define EPUB_PATH_CAP 512
#define EPUB_ID_CAP 128
#define EPUB_TITLE_CAP 160
#define EPUB_ENTRY_MAX (4u * 1024u * 1024u)
#define EPUB_CACHE_VERSION 12u
#define EPUB_META_CACHE_VERSION 1u
#define EPUB_CSS_MAX (256u * 1024u)
#define EPUB_CSS_FILE_MAX (128u * 1024u)

typedef struct {
    int zip_index; ///< ZIP 条目 / ZIP entry
    uint32_t offset; ///< spine 累计原始字节 / Cumulative source bytes in spine
    char title[EPUB_TITLE_CAP]; ///< UTF-8 目录标题 / UTF-8 navigation title
    bool titled; ///< 已从目录命名 / Named from navigation
    bool nav_titled; ///< 正式 NCX/NAV 指向本章 / Linked by the authored navigation
    bool heading_checked; ///< 缺目录标题时已检查正文 / Body checked for a missing navigation label
    bool toc_visible; ///< 目录选择结果，打开目录时计算 / Visibility calculated when opening the TOC
} chapter_t;
typedef struct {
    uint16_t chapter;
    bool valid, visible;
    uint32_t source_offset; ///< XHTML 起始标签偏移；没有时为 UINT32_MAX / Source tag offset or UINT32_MAX
    char title[EPUB_TITLE_CAP];
    char anchor[EPUB_ID_CAP];
} nav_entry_t;
struct book_epub {
    zip_reader_t *zip; ///< ZIP 所有权 / ZIP ownership
    chapter_t *chapters; ///< 按实际章节数分配的 PSRAM 表 / PSRAM table sized to actual spine
    size_t count; ///< 章节数 / Chapter count
    size_t chapter_capacity;
    uint32_t total; ///< 原始 HTML 字节总量 / Total source HTML bytes
    char source_path[EPUB_PATH_CAP]; ///< 延迟目录缓存来源 / Source for lazy navigation cache
    bool headings_dirty; ///< 标题需要写回 / Titles need caching
    bool navigation_ready; ///< 本次打开已筛选目录 / Navigation prepared for this open
    size_t navigation_count; ///< 可见目录条数 / Visible navigation entries
    nav_entry_t *navigation; ///< 独立于 spine 的 NCX/NAV 目录 / Authored TOC, independent of spine
    uint16_t *visible_index; ///< 可见目录位置直接映射 / Direct index for visible navigation
    size_t authored_count;
    size_t navigation_capacity;
    bool body_scanned; ///< 正文编号标题已检查并缓存 / Numbered body headings already indexed
};
typedef struct {
    uint32_t count;
    uint32_t total;
    uint32_t authored_count;
    uint8_t body_scanned;
} epub_cache_payload_t;
typedef struct {
    char title[256];
    char author[160];
} epub_meta_cache_payload_t;
typedef struct {
    char id[EPUB_ID_CAP]; ///< manifest 标识 / Manifest identifier
    int zip_index; ///< 借用 ZIP 条目名称，不为每个项目保存 512 字节路径 / Borrow ZIP name
    bool nav, ncx, html; ///< 媒体用途 / Media roles
} item_t;
typedef struct { const char *p; size_t n; } span_t;
typedef enum { XML_OPEN, XML_CLOSE, XML_TEXT } xml_kind_t;
typedef struct {
    xml_kind_t kind; ///< token 类型 / Token kind
    span_t name, attrs, text; ///< 输入借用片段 / Borrowed input spans
    size_t depth; ///< 当前元素深度 / Current element depth
    bool empty, cdata; ///< 自闭合及原文段 / Self-closing and literal text
} token_t;
typedef struct {
    const char *p, *end; ///< 输入范围 / Input bounds
    span_t stack[EPUB_XML_DEPTH]; ///< 嵌套名称 / Nested names
    size_t depth; ///< 当前嵌套 / Current nesting
    size_t roots; ///< 根节点数 / Root element count
    bool failed; ///< 解析错误 / Parse error
} xml_t;
static void *psram(size_t n);

static bool epub_cache_load(const char* source, book_epub_t* book) {
    char path[112]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source, "epub", EPUB_CACHE_VERSION, path, sizeof(path), &key)) return false;
    FILE* cache = book_index_cache_open_read(path, &key);
    if (!cache) return false;
    epub_cache_payload_t payload;
    bool ok = fread(&payload, 1, sizeof(payload), cache) == sizeof(payload) &&
        payload.count > 0 && payload.count <= EPUB_CHAPTER_MAX &&
        payload.authored_count <= EPUB_NAV_MAX && payload.body_scanned <= 1;
    if (ok) {
        book->chapters = psram(payload.count * sizeof(*book->chapters));
        ok = book->chapters &&
             fread(book->chapters, sizeof(*book->chapters), payload.count, cache) == payload.count;
        if (ok) book->chapter_capacity = payload.count;
    }
    if (ok && payload.authored_count) {
        book->navigation = psram(payload.authored_count * sizeof(*book->navigation));
        ok = book->navigation &&
             fread(book->navigation, sizeof(*book->navigation), payload.authored_count, cache) == payload.authored_count;
    }
    if (ok) ok = fgetc(cache) == EOF;
    fclose(cache);
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < payload.count; ++i) {
        chapter_t* chapter = &book->chapters[i];
        ok = chapter->zip_index >= 0 && zip_entry_name(book->zip, chapter->zip_index) &&
            chapter->offset >= previous && chapter->offset <= payload.total &&
            memchr(chapter->title, 0, sizeof(chapter->title)) != NULL;
        previous = chapter->offset;
    }
    for (size_t i = 0; ok && i < payload.authored_count; ++i) {
        nav_entry_t *entry = &book->navigation[i];
        ok = entry->chapter < payload.count &&
             (entry->source_offset == UINT32_MAX ||
              entry->source_offset < zip_entry_size(book->zip, book->chapters[entry->chapter].zip_index)) &&
             memchr(entry->title, 0, sizeof(entry->title)) != NULL &&
             memchr(entry->anchor, 0, sizeof(entry->anchor)) != NULL;
    }
    if (!ok) {
        // 损坏或写入中断的索引必须释放两张表；随后才重建正文目录。
        // Release both partially loaded tables before rebuilding a damaged index.
        free(book->navigation); book->navigation = NULL; book->navigation_capacity = 0;
        free(book->chapters); book->chapters = NULL; book->chapter_capacity = 0;
        return false;
    }
    book->count = payload.count; book->total = payload.total; book->authored_count = payload.authored_count;
    book->navigation_capacity = payload.authored_count;
    book->body_scanned = payload.body_scanned != 0;
    // 临时诊断：确认目录来自缓存还是重建。
    // / Temporary diagnostics: cache hit vs rebuilt navigation.
    ESP_LOGI("epub_nav", "cache hit: chapters=%u authored=%u",
             (unsigned)payload.count, (unsigned)payload.authored_count);
    return true;
}

static void epub_cache_save(const char* source, const book_epub_t* book) {
    char path[112], temp[120]; book_index_cache_header_t key;
    if (!book_index_cache_prepare(source, "epub", EPUB_CACHE_VERSION, path, sizeof(path), &key)) return;
    FILE* cache = book_index_cache_open_write(path, &key, temp, sizeof(temp));
    if (!cache) return;
    epub_cache_payload_t payload = {.count = (uint32_t)book->count, .total = book->total,
                                    .authored_count = (uint32_t)book->authored_count,
                                    .body_scanned = book->body_scanned ? 1 : 0};
    bool ok = fwrite(&payload, 1, sizeof(payload), cache) == sizeof(payload) &&
        fwrite(book->chapters, sizeof(book->chapters[0]), book->count, cache) == book->count &&
        (!book->authored_count ||
         fwrite(book->navigation, sizeof(*book->navigation), book->authored_count, cache) == book->authored_count);
    (void)book_index_cache_finish_write(cache, temp, path, ok);
}

/* ---- XML 边界与实体 / XML bounds and entities ---- */

static void *psram(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static bool nav_reserve(book_epub_t *book, size_t needed) {
    if (needed <= book->navigation_capacity) return true;
    size_t cap = book->navigation_capacity ? book->navigation_capacity * 2 : 16;
    if (cap < needed) cap = needed;
    if (cap > EPUB_NAV_MAX) cap = EPUB_NAV_MAX;
    nav_entry_t *items = heap_caps_realloc(book->navigation, cap * sizeof(*items),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!items) return false;
    book->navigation = items; book->navigation_capacity = cap;
    return true;
}
static bool equal(span_t s, const char *text) { return s.n == strlen(text) && !memcmp(s.p, text, s.n); }
static bool local_name(span_t s, const char *name) {
    for (size_t i = 0; i < s.n; ++i) if (s.p[i] == ':') { s.n -= i + 1; s.p += i + 1; break; }
    return equal(s, name);
}
static bool begins(const char *p, const char *end, const char *s) {
    size_t n = strlen(s); return (size_t)(end - p) >= n && !memcmp(p, s, n);
}
static const char *find_end(const char *p, const char *end, const char *marker) {
    size_t n = strlen(marker);
    while ((size_t)(end - p) >= n) { if (!memcmp(p, marker, n)) return p; ++p; }
    return NULL;
}
static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool name_char(char c) { return !space(c) && c != '/' && c != '>' && c != '=' && c != '<' && c != '\'' && c != '"'; }
static size_t utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = 0xc0 | (cp >> 6); out[1] = 0x80 | (cp & 63); return 2; }
    if (cp < 0x10000) { out[0] = 0xe0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 63); out[2] = 0x80 | (cp & 63); return 3; }
    out[0] = 0xf0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 63); out[2] = 0x80 | ((cp >> 6) & 63); out[3] = 0x80 | (cp & 63); return 4;
}
static size_t utf8_read(const char *p, size_t n, uint32_t *cp) {
    unsigned char a = (unsigned char)p[0];
    if (a < 0x80) { *cp = a; return 1; }
    size_t count = a >= 0xc2 && a <= 0xdf ? 2 : a >= 0xe0 && a <= 0xef ? 3 : a >= 0xf0 && a <= 0xf4 ? 4 : 0;
    if (!count || count > n) return 0;
    uint32_t value = a & (0x7f >> count);
    for (size_t i = 1; i < count; ++i) {
        unsigned char b = (unsigned char)p[i]; if ((b & 0xc0) != 0x80) return 0;
        value = (value << 6) | (b & 63);
    }
    if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) || (count == 4 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    *cp = value; return count;
}
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool entity(span_t s, uint32_t *cp) {
    if (equal(s, "amp")) *cp = '&';
    else if (equal(s, "lt")) *cp = '<';
    else if (equal(s, "gt")) *cp = '>';
    else if (equal(s, "quot")) *cp = '"';
    else if (equal(s, "apos")) *cp = '\'';
    else if (equal(s, "nbsp")) *cp = 160;
    else if (s.n > 1 && s.p[0] == '#') {
        size_t i = 1; unsigned base = 10;
        if (s.p[i] == 'x' || s.p[i] == 'X') { base = 16; ++i; }
        if (i == s.n) return false;
        uint32_t value = 0;
        for (; i < s.n; ++i) {
            int digit = hex(s.p[i]); if (digit < 0 || (unsigned)digit >= base || value > (0x10ffffu - (unsigned)digit) / base) return false;
            value = value * base + (unsigned)digit;
        }
        if (!value || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
        *cp = value;
    } else return false;
    return true;
}
// 属性必须完整容纳；目录标题可在标量边界截断。/ Attributes must fit fully; navigation labels may truncate at scalar boundaries.
static bool decode(span_t s, char *out, size_t cap, bool label, bool literal) {
    size_t used = strlen(out); bool full = false;
    for (size_t i = 0; i < s.n;) {
        uint32_t cp; size_t n;
        if (!literal && s.p[i] == '&') {
            size_t end = i + 1; while (end < s.n && s.p[end] != ';' && end - i < 20) ++end;
            if (end == s.n || s.p[end] != ';' || !entity((span_t){s.p + i + 1, end - i - 1}, &cp)) return false;
            n = end - i + 1;
        } else { n = utf8_read(s.p + i, s.n - i, &cp); if (!n) return false; }
        i += n;
        if (!cp || (cp < 32 && cp != '\n' && cp != '\r' && cp != '\t')) return false;
        if (label && (cp == 160 || cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r')) {
            if (!used || out[used - 1] == ' ') continue;
            cp = ' ';
        }
        char bytes[4]; size_t bytes_n = utf8_encode(cp, bytes);
        if (full || used + bytes_n >= cap) { if (!label) return false; full = true; continue; }
        memcpy(out + used, bytes, bytes_n); used += bytes_n; out[used] = 0;
    }
    return true;
}
static bool attr_next(span_t attrs, size_t *offset, span_t *name, span_t *value) {
    size_t i = *offset; while (i < attrs.n && space(attrs.p[i])) ++i;
    if (i == attrs.n) { *offset = i; return false; }
    size_t start = i; while (i < attrs.n && name_char(attrs.p[i])) ++i;
    if (i == start) { *offset = SIZE_MAX; return false; }
    *name = (span_t){attrs.p + start, i - start};
    while (i < attrs.n && space(attrs.p[i])) ++i;
    if (i == attrs.n || attrs.p[i++] != '=') { *offset = SIZE_MAX; return false; }
    while (i < attrs.n && space(attrs.p[i])) ++i;
    if (i == attrs.n || (attrs.p[i] != '\'' && attrs.p[i] != '"')) { *offset = SIZE_MAX; return false; }
    char quote = attrs.p[i++]; start = i;
    while (i < attrs.n && attrs.p[i] != quote) { if (attrs.p[i] == '<') { *offset = SIZE_MAX; return false; } ++i; }
    if (i == attrs.n) { *offset = SIZE_MAX; return false; }
    *value = (span_t){attrs.p + start, i - start}; *offset = i + 1; return true;
}
static bool attribute(token_t t, const char *wanted, char *out, size_t cap) {
    size_t offset = 0; span_t name, value; bool found = false; out[0] = 0;
    while (attr_next(t.attrs, &offset, &name, &value)) if (equal(name, wanted)) {
        if (found || !decode(value, out, cap, false, false)) return false;
        found = true;
    }
    return offset != SIZE_MAX;
}
static bool xml_next(xml_t *xml, token_t *t) {
    memset(t, 0, sizeof(*t));
    while (xml->p < xml->end && !xml->failed) {
        const char *p = xml->p;
        if (*p != '<') {
            const char *start = p; while (p < xml->end && *p != '<') ++p;
            if (!xml->depth) {
                const char *q = start; while (q < p && space(*q)) ++q;
                if (q != p) break;
            }
            t->kind = XML_TEXT; t->text = (span_t){start, (size_t)(p - start)}; t->depth = xml->depth; xml->p = p; return true;
        }
        if (begins(p, xml->end, "<!--") || begins(p, xml->end, "<?")) {
            bool comment = p[1] == '!'; const char *marker = comment ? "-->" : "?>";
            const char *end = find_end(p + (comment ? 4 : 2), xml->end, marker);
            if (!end) break;
            xml->p = end + strlen(marker); continue;
        }
        if (begins(p, xml->end, "<![CDATA[")) {
            if (!xml->depth) break;
            const char *end = find_end(p + 9, xml->end, "]]>"); if (!end) break;
            t->kind = XML_TEXT; t->cdata = true; t->text = (span_t){p + 9, (size_t)(end - p - 9)}; t->depth = xml->depth; xml->p = end + 3; return true;
        }
        if (begins(p, xml->end, "<!DOCTYPE")) {
            // 跳过声明但从不读取外部 DTD；内部实体也不展开。/ Skip declarations without loading external DTDs or expanding internal entities.
            int brackets = 0; char quote = 0; p += 9;
            while (p < xml->end) {
                char c = *p++;
                if (quote) { if (c == quote) quote = 0; continue; }
                if (c == '\'' || c == '"') quote = c;
                else if (c == '[') ++brackets;
                else if (c == ']') { if (!brackets) break; --brackets; }
                else if (c == '>' && !brackets) { xml->p = p; break; }
            }
            if (xml->p != p || quote || brackets) break;
            continue;
        }
        ++p; bool closing = p < xml->end && *p == '/'; if (closing) ++p;
        const char *name = p; while (p < xml->end && name_char(*p)) ++p;
        if (p == name) break;
        t->name = (span_t){name, (size_t)(p - name)};
        const char *attrs = p; char quote = 0;
        while (p < xml->end) {
            if (quote) { if (*p == quote) quote = 0; }
            else if (*p == '\'' || *p == '"') quote = *p;
            else if (*p == '>') break;
            ++p;
        }
        if (p == xml->end || quote) break;
        const char *end = p; while (end > attrs && space(end[-1])) --end;
        if (end > attrs && end[-1] == '/') { t->empty = true; --end; }
        t->attrs = (span_t){attrs, (size_t)(end - attrs)};
        size_t at = 0; span_t an, av; while (attr_next(t->attrs, &at, &an, &av)) {}
        if (at == SIZE_MAX) break;
        if (closing) {
            if (t->empty || !xml->depth || t->attrs.n) {
                bool only_space = !t->empty && xml->depth;
                for (size_t i = 0; i < t->attrs.n; ++i) if (!space(t->attrs.p[i])) only_space = false;
                if (!only_space) break;
            }
            span_t top = xml->stack[xml->depth - 1];
            if (top.n != t->name.n || memcmp(top.p, t->name.p, top.n)) break;
            t->kind = XML_CLOSE; t->depth = xml->depth; --xml->depth;
        } else {
            if (xml->depth == EPUB_XML_DEPTH) break;
            if (!xml->depth && ++xml->roots != 1) break;
            t->kind = XML_OPEN; t->depth = xml->depth + 1;
            if (!t->empty) xml->stack[xml->depth++] = t->name;
        }
        xml->p = p + 1; return true;
    }
    if (xml->p != xml->end || xml->depth || xml->roots != 1) xml->failed = true;
    return false;
}
static void xml_reader(xml_t *xml, const char *text, size_t n) {
    memset(xml, 0, sizeof(*xml)); xml->p = text; xml->end = text + n;
    if (memchr(text, 0, n)) xml->failed = true;
    if (n >= 3 && !memcmp(text, "\xef\xbb\xbf", 3)) xml->p += 3;
}

/* ---- ZIP 路径及内容 / ZIP paths and content ---- */

static bool resolve_path(const char *base, const char *href, char out[EPUB_PATH_CAP]) {
    char decoded[EPUB_PATH_CAP]; size_t used = 0;
    for (size_t i = 0; href[i] && href[i] != '#'; ++i) {
        unsigned char c = (unsigned char)href[i];
        if (c == '%') {
            if (!href[i + 1] || !href[i + 2]) return false;
            int a = hex(href[i + 1]), b = hex(href[i + 2]); if (a < 0 || b < 0) return false;
            c = (unsigned char)((a << 4) | b); i += 2;
        }
        if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '?' || used + 1 >= sizeof(decoded)) return false;
        decoded[used++] = (char)c;
    }
    decoded[used] = 0;
    if (!used) {
        if (href[0] != '#' || !base[0] || strlen(base) >= EPUB_PATH_CAP) return false;
        strcpy(out, base); return true;
    }
    if (decoded[0] == '/') return false;
    size_t length = 0;
    const char *slash = strrchr(base, '/'); if (slash) length = (size_t)(slash - base);
    if (length >= EPUB_PATH_CAP) return false;
    memcpy(out, base, length);
    for (size_t i = 0; decoded[i];) {
        size_t start = i; while (decoded[i] && decoded[i] != '/') ++i;
        size_t n = i - start; if (decoded[i]) ++i;
        if (!n || (n == 1 && decoded[start] == '.')) continue;
        if (n == 2 && decoded[start] == '.' && decoded[start + 1] == '.') {
            if (!length) return false;
            while (length && out[length - 1] != '/') --length;
            if (length) --length;
            continue;
        }
        if (length + (length != 0) + n >= EPUB_PATH_CAP) return false;
        if (length) out[length++] = '/';
        memcpy(out + length, decoded + start, n); length += n;
    }
    out[length] = 0; return length != 0;
}
static esp_err_t load_entry(book_epub_t *book, int index, char **out, size_t *len) {
    *out = NULL; *len = 0;
    if (index < 0) return ESP_ERR_NOT_FOUND;
    size_t n = zip_entry_size(book->zip, index); if (n > EPUB_ENTRY_MAX) return ESP_ERR_INVALID_SIZE;
    char *text = psram(n + 1); if (!text) return ESP_ERR_NO_MEM;
    esp_err_t err = zip_extract(book->zip, index, text, n);
    if (err != ESP_OK) { free(text); return err; }
    text[n] = 0; *out = text; *len = n; return ESP_OK;
}
static bool word(const char *list, const char *needle) {
    size_t n = strlen(needle);
    while (*list) {
        while (space(*list)) ++list;
        const char *start = list; while (*list && !space(*list)) ++list;
        if ((size_t)(list - start) == n && !memcmp(start, needle, n)) return true;
    }
    return false;
}
static esp_err_t container_path(book_epub_t *book, char out[EPUB_PATH_CAP]) {
    char *text; size_t len; esp_err_t err = load_entry(book, zip_find(book->zip, "META-INF/container.xml"), &text, &len);
    if (err != ESP_OK) return err;
    xml_t xml; xml_reader(&xml, text, len); token_t t; bool found = false, root = false;
    while (xml_next(&xml, &t)) if (t.kind == XML_OPEN) {
        if (t.depth == 1 && local_name(t.name, "container")) root = true;
        if (root && local_name(t.name, "rootfile") && !found) {
            char href[EPUB_PATH_CAP], media[80];
            if (!attribute(t, "full-path", href, sizeof(href)) || !attribute(t, "media-type", media, sizeof(media))) { xml.failed = true; break; }
            if ((!media[0] || !strcmp(media, "application/oebps-package+xml")) && href[0]) {
                if (!resolve_path("", href, out)) { xml.failed = true; break; } found = true;
            }
        }
    }
    free(text); return !xml.failed && root && found ? ESP_OK : ESP_ERR_INVALID_ARG;
}

/* ---- manifest 与 spine / Manifest and spine ---- */

static uint32_t item_hash(const char *id) {
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p)
        hash = (hash ^ *p) * 16777619u;
    return hash;
}
static int item_find(const item_t *items, const uint16_t *slots, size_t mask, const char *id) {
    size_t slot = item_hash(id) & mask;
    while (slots[slot]) {
        int index = slots[slot] - 1;
        if (!strcmp(items[index].id, id)) return index;
        slot = (slot + 1) & mask;
    }
    return -1;
}
static size_t package_child_count(const char *text, size_t len,
                                  const char *section, const char *child) {
    xml_t xml; token_t t; size_t depth = 0, count = 0;
    xml_reader(&xml, text, len);
    while (xml_next(&xml, &t)) {
        if (t.kind == XML_OPEN && local_name(t.name, section)) depth = t.depth;
        if (t.kind == XML_CLOSE && t.depth == depth) depth = 0;
        if (t.kind == XML_OPEN && depth && t.depth == depth + 1 && local_name(t.name, child)) ++count;
    }
    return xml.failed ? 0 : count;
}
static esp_err_t package_parse(book_epub_t *book, const char *opf, char ncx[EPUB_PATH_CAP], char nav[EPUB_PATH_CAP]) {
    char *text; size_t len; esp_err_t err = load_entry(book, zip_find(book->zip, opf), &text, &len);
    if (err != ESP_OK) return err;
    size_t manifest_capacity = package_child_count(text, len, "manifest", "item");
    size_t chapter_capacity = package_child_count(text, len, "spine", "itemref");
    if (!manifest_capacity || manifest_capacity > EPUB_MANIFEST_MAX ||
        !chapter_capacity || chapter_capacity > EPUB_CHAPTER_MAX) {
        free(text); return ESP_ERR_INVALID_SIZE;
    }
    item_t *items = psram(manifest_capacity * sizeof(*items));
    size_t slot_count = 16;
    while (slot_count < manifest_capacity * 2) slot_count *= 2;
    uint16_t *slots = psram(slot_count * sizeof(*slots));
    book->chapters = psram(chapter_capacity * sizeof(*book->chapters));
    if (!items || !slots || !book->chapters) {
        free(items); free(slots); free(text); return ESP_ERR_NO_MEM;
    }
    memset(slots, 0, slot_count * sizeof(*slots));
    memset(book->chapters, 0, chapter_capacity * sizeof(*book->chapters));
    book->chapter_capacity = chapter_capacity;
    size_t count = 0, manifest_depth = 0; bool root = false;
    xml_t xml; xml_reader(&xml, text, len); token_t t;
    while (xml_next(&xml, &t)) {
        if (t.kind == XML_OPEN && t.depth == 1 && local_name(t.name, "package")) root = true;
        if (t.kind == XML_OPEN && local_name(t.name, "manifest")) manifest_depth = t.depth;
        if (t.kind == XML_CLOSE && t.depth == manifest_depth) manifest_depth = 0;
        if (t.kind != XML_OPEN || !manifest_depth || t.depth != manifest_depth + 1 || !local_name(t.name, "item")) continue;
        if (count == manifest_capacity) { err = ESP_ERR_INVALID_SIZE; break; }
        item_t *item = &items[count]; memset(item, 0, sizeof(*item));
        char href[EPUB_PATH_CAP], media[80], properties[256];
        char resolved[EPUB_PATH_CAP];
        if (!attribute(t, "id", item->id, sizeof(item->id)) || !attribute(t, "href", href, sizeof(href)) || !attribute(t, "media-type", media, sizeof(media)) || !attribute(t, "properties", properties, sizeof(properties)) || !item->id[0] || !href[0] || item_find(items, slots, slot_count - 1, item->id) >= 0 || !resolve_path(opf, href, resolved)) { err = ESP_ERR_INVALID_ARG; break; }
        item->zip_index = zip_find(book->zip, resolved);
        item->nav = word(properties, "nav"); item->ncx = !strcmp(media, "application/x-dtbncx+xml");
        item->html = !strcmp(media, "application/xhtml+xml") || !strcmp(media, "text/html");
        size_t slot = item_hash(item->id) & (slot_count - 1);
        while (slots[slot]) slot = (slot + 1) & (slot_count - 1);
        slots[slot] = (uint16_t)(++count);
    }
    if (xml.failed || !root || !count) err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) {
        for (size_t i = 0; i < count; ++i) {
            const char *name = zip_entry_name(book->zip, items[i].zip_index);
            if (!nav[0] && items[i].nav && name) strcpy(nav, name);
            if (!ncx[0] && items[i].ncx && name) strcpy(ncx, name);
        }
        xml_reader(&xml, text, len); size_t spine_depth = 0; bool seen_spine = false;
        while (xml_next(&xml, &t)) {
            if (t.kind == XML_OPEN && local_name(t.name, "spine")) {
                if (seen_spine) { err = ESP_ERR_INVALID_ARG; break; }
                seen_spine = true; spine_depth = t.depth; char toc[EPUB_ID_CAP];
                if (!attribute(t, "toc", toc, sizeof(toc))) { err = ESP_ERR_INVALID_ARG; break; }
                int index = item_find(items, slots, slot_count - 1, toc);
                const char *name = index >= 0 ? zip_entry_name(book->zip, items[index].zip_index) : NULL;
                if (name && items[index].ncx) strcpy(ncx, name);
            }
            if (t.kind == XML_CLOSE && t.depth == spine_depth) spine_depth = 0;
            if (t.kind != XML_OPEN || !spine_depth || t.depth != spine_depth + 1 || !local_name(t.name, "itemref")) continue;
            if (book->count == book->chapter_capacity) { err = ESP_ERR_INVALID_SIZE; break; }
            char id[EPUB_ID_CAP]; if (!attribute(t, "idref", id, sizeof(id))) { err = ESP_ERR_INVALID_ARG; break; }
            int item_index = item_find(items, slots, slot_count - 1, id);
            if (item_index < 0 || !items[item_index].html) { err = ESP_ERR_NOT_SUPPORTED; break; }
            int entry = items[item_index].zip_index;
            if (entry < 0) { err = ESP_ERR_NOT_FOUND; break; }
            size_t bytes = zip_entry_size(book->zip, entry);
            if (bytes > EPUB_ENTRY_MAX || bytes > UINT32_MAX - book->total) { err = ESP_ERR_INVALID_SIZE; break; }
            chapter_t *chapter = &book->chapters[book->count]; chapter->zip_index = entry; chapter->offset = book->total;
            snprintf(chapter->title, sizeof(chapter->title), "第 %u 节", (unsigned)book->count + 1);
            book->total += (uint32_t)bytes; ++book->count;
        }
        if (xml.failed || !book->count) err = ESP_ERR_INVALID_ARG;
    }
    free(slots); free(items); free(text); return err;
}

/* ---- NCX 与 EPUB 3 导航 / NCX and EPUB 3 navigation ---- */

static bool front_matter_title(const char *title);
static void assign_title(book_epub_t *book, const int16_t *chapter_by_zip,
                         const char *base, const char *href,
                         char title[EPUB_TITLE_CAP], size_t slot) {
    size_t n = strlen(title); while (n && title[n - 1] == ' ') title[--n] = 0;
    if (!n || !href[0] || slot >= EPUB_NAV_MAX) return;
    char path[EPUB_PATH_CAP]; if (!resolve_path(base, href, path)) return;
    int index = zip_find(book->zip, path); if (index < 0) return;
    size_t first = chapter_by_zip ?
        (chapter_by_zip[index] >= 0 ? (size_t)chapter_by_zip[index] : book->count) : 0;
    for (size_t i = first; i < book->count; ++i) if (book->chapters[i].zip_index == index) {
        nav_entry_t *entry = &book->navigation[slot];
        const char *fragment = strchr(href, '#');
        entry->chapter = (uint16_t)i;
        strcpy(entry->title, title);
        if (fragment && fragment[1]) {
            size_t used = 0;
            for (const char *p = fragment + 1; *p && used + 1 < sizeof(entry->anchor); ++p) {
                if (*p == '%' && p[1] && p[2]) {
                    int a = hex(p[1]), b = hex(p[2]);
                    if (a >= 0 && b >= 0) { entry->anchor[used++] = (char)((a << 4) | b); p += 2; continue; }
                }
                entry->anchor[used++] = *p;
            }
            entry->anchor[used] = 0;
        }
        entry->valid = true;
        if (!book->chapters[i].titled ||
            (front_matter_title(book->chapters[i].title) && !front_matter_title(title))) {
            strcpy(book->chapters[i].title, title);
            book->chapters[i].titled = true;
            book->chapters[i].nav_titled = true;
        }
        return;
    }
}
typedef struct {
    size_t depth, label_depth; ///< 节点与标签深度 / Node and label depths
    size_t slot;
    char href[EPUB_PATH_CAP]; ///< 内容目标 / Content target
    char title[EPUB_TITLE_CAP]; ///< 当前节点标题 / Current node title
} nav_node_t;
static bool nav_is_toc(token_t t) {
    char value[128];
    if (!attribute(t, "epub:type", value, sizeof(value))) return false;
    if (word(value, "toc")) return true;
    if (!attribute(t, "type", value, sizeof(value))) return false;
    if (word(value, "toc")) return true;
    if (!attribute(t, "role", value, sizeof(value))) return false;
    return word(value, "doc-toc");
}
static esp_err_t navigation_parse(book_epub_t *book, const char *path, bool ncx) {
    char *text; size_t len; esp_err_t err = load_entry(book, zip_find(book->zip, path), &text, &len);
    if (err != ESP_OK) return err;
    if (ncx && !book->navigation) {
        // NCX 的节点数可先数出，避免大书目录反复扩容时同时占住新旧两块 PSRAM。
        // Count NCX nodes first to avoid holding old and new PSRAM arrays during growth.
        xml_t scan; token_t node; size_t expected = 0;
        xml_reader(&scan, text, len);
        while (xml_next(&scan, &node))
            if (node.kind == XML_OPEN && local_name(node.name, "navPoint") && ++expected > EPUB_NAV_MAX) break;
        if (!scan.failed && expected && expected <= EPUB_NAV_MAX) {
            book->navigation = psram(expected * sizeof(*book->navigation));
            if (book->navigation) book->navigation_capacity = expected;
        }
    }
    bool explicit_toc = false;
    if (!ncx) {
        xml_t scan; token_t candidate;
        xml_reader(&scan, text, len);
        while (xml_next(&scan, &candidate))
            if (candidate.kind == XML_OPEN && local_name(candidate.name, "nav") && nav_is_toc(candidate))
                explicit_toc = true;
        if (scan.failed) { free(text); return ESP_ERR_INVALID_ARG; }
    }
    nav_node_t *nodes = psram(sizeof(*nodes) * EPUB_XML_DEPTH);
    if (!nodes) { free(text); return ESP_ERR_NO_MEM; }
    size_t zip_count = zip_entry_count(book->zip);
    int16_t *chapter_by_zip = psram(zip_count * sizeof(*chapter_by_zip));
    if (chapter_by_zip) {
        memset(chapter_by_zip, 0xff, zip_count * sizeof(*chapter_by_zip));
        for (size_t i = 0; i < book->count; ++i)
            if (chapter_by_zip[book->chapters[i].zip_index] < 0)
                chapter_by_zip[book->chapters[i].zip_index] = (int16_t)i;
    }
    // Keep the TOC rollback bitmap in PSRAM: this call runs under the UI task.
    // 目录回退位图放在 PSRAM，避免和界面事件栈叠加。
    unsigned char *previously_titled = psram(EPUB_CHAPTER_MAX / 8);
    if (!previously_titled) {
        free(chapter_by_zip); free(nodes); free(text);
        return ESP_ERR_NO_MEM;
    }
    memset(previously_titled, 0, EPUB_CHAPTER_MAX / 8);
    for (size_t i = 0; i < book->count; ++i) if (book->chapters[i].titled) previously_titled[i / 8] |= (unsigned char)(1u << (i % 8));
    size_t original_count = book->authored_count;
    size_t count = 0, toc_depth = 0; bool fallback_used = false;
    xml_t xml; xml_reader(&xml, text, len); token_t t;
    while (xml_next(&xml, &t)) {
        if (!ncx && t.kind == XML_OPEN && local_name(t.name, "nav")) {
            if (!t.empty && (nav_is_toc(t) || (!explicit_toc && !fallback_used))) {
                toc_depth = t.depth;
                if (!explicit_toc) fallback_used = true;
            }
        }
        if (!ncx && t.kind == XML_CLOSE && t.depth == toc_depth) toc_depth = 0;
        bool node_open = t.kind == XML_OPEN && (ncx ? local_name(t.name, "navPoint") : toc_depth && local_name(t.name, "a"));
        if (node_open) {
            if (count == EPUB_XML_DEPTH || book->authored_count == EPUB_NAV_MAX) { xml.failed = true; break; }
            if (!nav_reserve(book, book->authored_count + 1)) { err = ESP_ERR_NO_MEM; break; }
            nav_node_t *node = &nodes[count++]; memset(node, 0, sizeof(*node)); node->depth = t.depth;
            node->slot = book->authored_count++;
            memset(&book->navigation[node->slot], 0, sizeof(*book->navigation));
            book->navigation[node->slot].source_offset = UINT32_MAX;
            if (!ncx) { node->label_depth = t.depth; if (!attribute(t, "href", node->href, sizeof(node->href))) { xml.failed = true; break; } }
            if (t.empty) --count;
            continue;
        }
        if (!count) continue;
        nav_node_t *node = &nodes[count - 1];
        if (ncx && t.kind == XML_OPEN && local_name(t.name, "navLabel")) node->label_depth = t.depth;
        if (t.kind == XML_TEXT && node->label_depth && !decode(t.text, node->title, sizeof(node->title), true, t.cdata)) { xml.failed = true; break; }
        if (ncx && t.kind == XML_OPEN && local_name(t.name, "content")) {
            if (!attribute(t, "src", node->href, sizeof(node->href))) { xml.failed = true; break; }
            assign_title(book, chapter_by_zip, path, node->href, node->title, node->slot);
        }
        if (t.kind == XML_CLOSE && t.depth == node->label_depth) node->label_depth = 0;
        if (t.kind == XML_CLOSE && t.depth == node->depth) {
            assign_title(book, chapter_by_zip, path, node->href, node->title, node->slot); --count;
        }
    }
    if (xml.failed) {
        err = ESP_ERR_INVALID_ARG;
        book->authored_count = original_count;
        // 损坏目录不留下部分命名，确保备用目录可完整接管。/ A broken TOC leaves no partial labels so fallback navigation can take over fully.
        for (size_t i = 0; i < book->count; ++i) if (!(previously_titled[i / 8] & (1u << (i % 8)))) {
            book->chapters[i].titled = false;
            book->chapters[i].nav_titled = false;
            snprintf(book->chapters[i].title, sizeof(book->chapters[i].title), "第 %u 节", (unsigned)i + 1);
        }
    }
    if (err == ESP_OK) {
        size_t write = original_count;
        for (size_t i = original_count; i < book->authored_count; ++i)
            if (book->navigation[i].valid) book->navigation[write++] = book->navigation[i];
        book->authored_count = write;
    }
    free(previously_titled); free(chapter_by_zip); free(nodes); free(text); return err;
}

// 统计正式目录覆盖的正文文件；不完整 NCX 不能仅凭首条有效项屏蔽更完整的 NAV。
// Count distinct spine files referenced by an authored directory. A short NCX must not
// suppress a more complete EPUB 3 NAV just because its first entry was valid.
static size_t navigation_coverage(const book_epub_t *book) {
    uint8_t *covered = psram(EPUB_CHAPTER_MAX / 8);
    if (!covered) return 0;
    memset(covered, 0, EPUB_CHAPTER_MAX / 8);
    size_t count = 0;
    for (size_t i = 0; i < book->authored_count; ++i) {
        const nav_entry_t *entry = &book->navigation[i];
        if (!entry->valid || entry->chapter >= book->count) continue;
        size_t chapter = entry->chapter;
        uint8_t bit = (uint8_t)(1u << (chapter % 8));
        if (!(covered[chapter / 8] & bit)) {
            covered[chapter / 8] |= bit;
            ++count;
        }
    }
    free(covered);
    return count;
}

static size_t navigation_entries_for_chapter(const nav_entry_t *items, size_t count,
                                             uint16_t chapter) {
    size_t found = 0;
    for (size_t i = 0; i < count; ++i)
        if (items[i].valid && items[i].chapter == chapter) ++found;
    return found;
}

static bool navigation_same_target(const nav_entry_t *a, const nav_entry_t *b,
                                   size_t a_chapter_entries, size_t b_chapter_entries) {
    if (a->chapter != b->chapter) return false;
    if (!strcmp(a->anchor, b->anchor)) return true;
    // 若某格式没有锚点，而两边在该文件都只有一条，视作相同章节。
    // One format may omit a fragment for a file containing just one chapter.
    return (!a->anchor[0] || !b->anchor[0]) &&
           a_chapter_entries == 1 && b_chapter_entries == 1;
}

// NAV 提供基本顺序，再按正文顺序插入仅见于 NCX 的目标。
// NAV supplies the book's reading order; retain NCX-only targets in spine order.
static void navigation_merge(book_epub_t *book, book_epub_t *nav_book) {
    nav_entry_t *ncx = book->navigation;
    size_t ncx_count = book->authored_count;
    book->navigation = nav_book->navigation;
    book->authored_count = nav_book->authored_count;
    book->navigation_capacity = nav_book->navigation_capacity;
    nav_book->navigation = NULL;
    nav_book->authored_count = nav_book->navigation_capacity = 0;
    for (size_t i = 0; i < ncx_count && book->authored_count < EPUB_NAV_MAX; ++i) {
        const nav_entry_t *candidate = &ncx[i];
        if (!candidate->valid) continue;
        size_t ncx_entries = navigation_entries_for_chapter(ncx, ncx_count, candidate->chapter);
        size_t nav_entries = navigation_entries_for_chapter(book->navigation,
                                                            book->authored_count, candidate->chapter);
        bool duplicate = false;
        for (size_t j = 0; j < book->authored_count; ++j) {
            if (navigation_same_target(candidate, &book->navigation[j], ncx_entries, nav_entries)) {
                book->navigation[j] = *candidate;
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        if (!nav_reserve(book, book->authored_count + 1)) {
            // 内存不足时保留原 NCX，避免用残缺的合并结果替换它。
            // Keep the original NCX if the union cannot fit in memory.
            free(book->navigation);
            book->navigation = ncx;
            book->authored_count = ncx_count;
            book->navigation_capacity = ncx_count;
            return;
        }
        size_t at = 0;
        while (at < book->authored_count && book->navigation[at].chapter <= candidate->chapter) ++at;
        memmove(book->navigation + at + 1, book->navigation + at,
                (book->authored_count - at) * sizeof(*book->navigation));
        book->navigation[at] = *candidate;
        ++book->authored_count;
    }
    free(ncx);
}

/* ---- 后端公共接口 / Backend public interface ---- */

esp_err_t book_epub_open(const char *path, book_epub_t **out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL; if (!path || !*path) return ESP_ERR_INVALID_ARG;
    book_epub_t *book = psram(sizeof(*book)); if (!book) return ESP_ERR_NO_MEM;
    memset(book, 0, sizeof(*book));
    if (strlen(path) < sizeof(book->source_path)) strcpy(book->source_path, path);
    char (*paths)[EPUB_PATH_CAP] = psram(3 * EPUB_PATH_CAP);
    if (!paths) { free(book); return ESP_ERR_NO_MEM; }
    memset(paths, 0, 3 * EPUB_PATH_CAP);
    char *opf = paths[0], *ncx = paths[1], *nav = paths[2];
    esp_err_t err = zip_open(path, &book->zip);
    if (err == ESP_OK && epub_cache_load(path, book)) {
        free(paths);
        *out = book;
        return ESP_OK;
    }
    if (err == ESP_OK) err = container_path(book, opf);
    if (err == ESP_OK) err = package_parse(book, opf, ncx, nav);
    if (err == ESP_OK) {
        // NCX 有缺项时补入 NAV 的有效目标；两者都不可用时按 spine 生成备用目录。
        // Complete a partial NCX from NAV; spine remains the fallback.
        if (ncx[0]) (void)navigation_parse(book, ncx, true);
        if (nav[0] && !book->authored_count) (void)navigation_parse(book, nav, false);
        // 小书也核对同一正文文件内的多个锚点；大书仅在 NCX 缺正文文件时额外解析 NAV。
        // Check intra-file anchors for small books; avoid a second full TOC parse on large complete books.
        else if (nav[0] && (book->count <= 256 || navigation_coverage(book) < book->count)) {
            book_epub_t *nav_book = psram(sizeof(*nav_book));
            if (nav_book) {
                memset(nav_book, 0, sizeof(*nav_book));
                nav_book->zip = book->zip;
                nav_book->chapters = book->chapters;
                nav_book->count = book->count;
                esp_err_t nav_err = navigation_parse(nav_book, nav, false);
                if (nav_err == ESP_OK && nav_book->authored_count)
                    navigation_merge(book, nav_book);
                free(nav_book->navigation);
                free(nav_book);
            }
        }
        if (book->navigation) {
            if (!book->authored_count) { free(book->navigation); book->navigation = NULL; book->navigation_capacity = 0; }
            else {
                nav_entry_t *compact = heap_caps_realloc(book->navigation,
                    book->authored_count * sizeof(*book->navigation), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (compact) { book->navigation = compact; book->navigation_capacity = book->authored_count; }
            }
        }
        epub_cache_save(path, book);
    }
    free(paths);
    if (err != ESP_OK) { book_epub_close(book); return err; }
    *out = book; return ESP_OK;
}
void book_epub_close(book_epub_t *book) {
    if (!book) return;
    if (book->headings_dirty && book->source_path[0]) epub_cache_save(book->source_path, book);
    zip_close(book->zip); free(book->visible_index); free(book->navigation); free(book->chapters); free(book);
}
size_t book_epub_chapter_count(const book_epub_t *book) { return book ? book->count : 0; }
// 目录缺项时只解析当前可见章节的标题，不在开书时解压所有章节。
// Resolve missing navigation labels lazily so opening a large book remains fast.
static void chapter_heading(book_epub_t *book, size_t i) {
    chapter_t *chapter = &book->chapters[i];
    if (chapter->titled || chapter->heading_checked) return;
    chapter->heading_checked = true;
    char *html = NULL; size_t len = 0;
    if (load_entry(book, chapter->zip_index, &html, &len) != ESP_OK) return;
    const char *end = html + (len < 65536 ? len : 65536);
    const char *at = html;
    while (at < end && *at) {
        const char *open = memchr(at, '<', (size_t)(end - at));
        if (!open || end - open < 4) break;
        if ((open[1] == 'h' || open[1] == 'H') && open[2] >= '1' && open[2] <= '3' &&
            (space(open[3]) || open[3] == '>')) {
            const char *body = memchr(open + 3, '>', (size_t)(end - open - 3));
            if (!body) break;
            ++body;
            char title[EPUB_TITLE_CAP] = "";
            const char *cursor = body;
            while (cursor < end && (size_t)(cursor - body) < 1024) {
                const char *tag = memchr(cursor, '<', (size_t)(end - cursor));
                if (!tag) break;
                if (!decode((span_t){cursor, (size_t)(tag - cursor)}, title, sizeof(title), true, false)) break;
                if (end - tag >= 4 && tag[1] == '/' && (tag[2] == 'h' || tag[2] == 'H') && tag[3] == open[2]) {
                    size_t n = strlen(title);
                    while (n && title[n - 1] == ' ') title[--n] = 0;
                    if (n) { strcpy(chapter->title, title); chapter->titled = true; book->headings_dirty = true; }
                    break;
                }
                cursor = memchr(tag, '>', (size_t)(end - tag));
                if (!cursor) break;
                ++cursor;
            }
            if (chapter->titled) break;
        }
        at = open + 1;
    }
    free(html);
}

static bool front_matter_title(const char *title) {
    static const char *const chinese[] = {
        "封面", "扉页", "书名页", "版权", "出版信息", "出版社", "作者信息",
        "作者简介", "关于作者", "图书信息", "书籍信息", "制作信息", NULL
    };
    static const char *const english[] = {
        "cover", "title page", "copyright", "publisher", "publication information",
        "about the author", "author information", "imprint", "colophon", NULL
    };
    while (*title == ' ' || *title == '\t') ++title;
    if (!strcmp(title, "目录") || !strcmp(title, "目次") || !strcmp(title, "简介") ||
        !strcmp(title, "内容简介") || !strcasecmp(title, "contents") ||
        !strcasecmp(title, "table of contents")) return true;
    if (!strcmp(title, "作者") || !strcmp(title, "出版") || !strcmp(title, "出版者")) return true;
    for (int i = 0; chinese[i]; ++i)
        if (!strncmp(title, chinese[i], strlen(chinese[i]))) return true;
    for (int i = 0; english[i]; ++i)
        if (!strncasecmp(title, english[i], strlen(english[i]))) return true;
    return false;
}

static bool front_matter_path(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    static const char *const names[] = {
        "cover", "front", "titlepage", "copyright", "publisher", "imprint",
        "colophon", "bookinfo", "authorinfo", "metadata", "toc.", "nav.", "contents.", NULL
    };
    for (int i = 0; names[i]; ++i)
        if (!strncasecmp(name, names[i], strlen(names[i]))) return true;
    return false;
}

static bool numbered_chapter_title(const char *title) {
    if (!strncasecmp(title, "Chapter", 7)) return true;
    return !strncmp(title, "第", strlen("第")) &&
           (strstr(title, "章") || strstr(title, "节"));
}

typedef struct {
    nav_entry_t *items;
    size_t count, capacity;
} body_headings_t;

static bool chinese_number(uint32_t cp) {
    if ((cp >= '0' && cp <= '9') || (cp >= 0xff10 && cp <= 0xff19)) return true;
    switch (cp) {
        case 0x3007: case 0x96f6: case 0x4e00: case 0x4e8c: case 0x4e09:
        case 0x56db: case 0x4e94: case 0x516d: case 0x4e03: case 0x516b:
        case 0x4e5d: case 0x5341: case 0x767e: case 0x5343: case 0x4e07:
        case 0x4e24: case 0x58f9: case 0x8d30: case 0x53c1: case 0x8086:
        case 0x4f0d: case 0x9646: case 0x67d2: case 0x634c: case 0x7396:
        case 0x62fe: case 0x4f70: case 0x4edf: return true;
        default: return false;
    }
}

static bool explicit_chapter_title(const char *title, bool heading_tag) {
    const char *p = title;
    while (*p == ' ' || *p == '\t' || !strncmp(p, "　", 3))
        p += *p == ' ' || *p == '\t' ? 1 : 3;
    if (strncmp(p, "第", 3)) return false;
    p += 3;
    while (*p == ' ' || *p == '\t' || !strncmp(p, "　", 3))
        p += *p == ' ' || *p == '\t' ? 1 : 3;
    unsigned digits = 0;
    while (*p && digits < 12) {
        uint32_t cp; size_t n = utf8_read(p, strlen(p), &cp);
        if (!n || !chinese_number(cp)) break;
        p += n; ++digits;
    }
    if (!digits) return false;
    while (*p == ' ' || *p == '\t' || !strncmp(p, "　", 3))
        p += *p == ' ' || *p == '\t' ? 1 : 3;
    uint32_t unit; size_t n = utf8_read(p, strlen(p), &unit);
    if (!n || (unit != 0x7ae0 && unit != 0x8282 && unit != 0x5377 &&
               unit != 0x56de && unit != 0x7bc7 && unit != 0x90e8 && unit != 0x96c6)) return false;
    p += n;
    size_t length = strlen(title);
    if (length > (heading_tag ? EPUB_TITLE_CAP - 1 : 96)) return false;
    // 普通段落必须像独立标题，防止“第一章说到……”被误作目录。
    // Plain paragraphs need a heading separator or a standalone chapter number.
    return heading_tag || !*p || *p == ' ' || *p == '\t' || *p == ':' ||
           !strncmp(p, "　", 3) || !strncmp(p, "：", 3) ||
           !strncmp(p, "、", 3) || *p == '-';
}

static bool pagination_numbered_title(const char *title, bool heading_tag) {
    if (explicit_chapter_title(title, heading_tag)) return true;
    const char *p = title;
    while (*p == ' ' || *p == '\t') ++p;
    if (!strncasecmp(p, "chapter", 7) && (p[7] == ' ' || p[7] == '\t')) {
        p += 7;
        while (*p == ' ' || *p == '\t') ++p;
        unsigned digits = 0;
        while (*p && ((*p >= '0' && *p <= '9') || strchr("IVXLCDMivxlcdm", *p))) {
            if (++digits > 12) break;
            ++p;
        }
        return digits && digits <= 12 && strlen(title) < (heading_tag ? EPUB_TITLE_CAP : 97) &&
            (!*p || *p == ' ' || *p == '\t' || *p == ':' || *p == '-' || *p == '.');
    }
    return false;
}

static bool block_title(const html_text_t *text, size_t i, char title[EPUB_TITLE_CAP]) {
    const blk_t *block = &text->blocks[i];
    if (block->image >= 0 || !block->len || block->len >= EPUB_TITLE_CAP) return false;
    memcpy(title, text->utf8 + block->offset, block->len);
    title[block->len] = 0;
    return true;
}

static bool authored_body_title(const book_epub_t *book, size_t chapter, const char *title,
                                size_t first, size_t last) {
    for (size_t i = first; i < last; ++i) {
        const nav_entry_t *entry = &book->navigation[i];
        if (entry->valid && entry->chapter == chapter && !front_matter_title(entry->title) &&
            !strcmp(entry->title, title)) return true;
    }
    return false;
}

// 连续的目录条目没有正文；副题可以紧跟章名，图片也可以位于正文开头。
// A list of TOC labels has no following body; allow a subtitle or illustration before chapter text.
static bool body_follows(const html_text_t *text, size_t i) {
    for (size_t j = i + 1; j < text->count && j - i <= 32; ++j) {
        const blk_t *block = &text->blocks[j];
        if (block->auxiliary || block->linked) return false;
        if (block->image >= 0) continue;
        char title[EPUB_TITLE_CAP];
        bool short_text = block_title(text, j, title);
        if (short_text && (front_matter_title(title) ||
            pagination_numbered_title(title, block->heading_level != 0))) return false;
        if (block->heading_level) continue;
        // 空白段不算正文；其余长度不作限制，兼容短诗及很短的章节。
        // Ignore whitespace-only blocks without imposing a minimum prose length.
        const char *p = text->utf8 + block->offset;
        for (size_t k = 0; k < block->len; ++k)
            if (!space(p[k]) && p[k] != '\f') return true;
    }
    return false;
}

static void chapter_breaks_prepare(book_epub_t *book, size_t chapter, html_text_t *text) {
    const char *path = zip_entry_name(book->zip, book->chapters[chapter].zip_index);
    if (front_matter_path(path)) return;
    size_t first = book->authored_count, last = 0;
    for (size_t i = 0; i < book->authored_count; ++i) {
        if (!book->navigation[i].valid || book->navigation[i].chapter != chapter) continue;
        if (first == book->authored_count) first = i;
        last = i + 1;
    }
    uint8_t main_level = 0;
    for (size_t i = 0; i < text->count; ++i) {
        const blk_t *block = &text->blocks[i]; char title[EPUB_TITLE_CAP];
        if (block->linked || block->auxiliary || !block->heading_level || block->heading_level > 2 ||
            !block_title(text, i, title) || front_matter_title(title) || !body_follows(text, i)) continue;
        if (!main_level || block->heading_level < main_level) main_level = block->heading_level;
    }
    bool auxiliary_section = false;
    for (size_t i = 0; i < text->count; ++i) {
        blk_t *block = &text->blocks[i]; char title[EPUB_TITLE_CAP];
        if (!block_title(text, i, title)) continue;
        if (block->heading_level && !block->auxiliary && front_matter_title(title)) auxiliary_section = true;
        if (block->linked || block->auxiliary || front_matter_title(title) || !body_follows(text, i)) continue;
        bool numbered = pagination_numbered_title(title, block->heading_level != 0);
        bool primary = main_level && block->heading_level == main_level;
        // 介绍/目录后的普通编号列表不能仅靠文字脱离书前区；正式正文题头可开始新章。
        // A numbered list after front matter cannot escape it by label alone; a body heading can.
        if (auxiliary_section && !block->heading_level) continue;
        bool authored = !numbered && authored_body_title(book, chapter, title, first, last);
        // 章号和章名分成相邻两块时只在章号前分页，避免把题头拆成两页。
        // A chapter number followed by its title needs one break, not two title-only pages.
        if (!numbered && i && text->blocks[i - 1].chapter_start) continue;
        if (numbered || authored || (primary && !last)) {
            block->chapter_start = true;
            auxiliary_section = false;
        }
    }
}

static bool auxiliary_xml(token_t t) {
    if (local_name(t.name, "nav")) return true;
    char value[160];
    static const char *const attrs[] = {"epub:type", "type", "role", "id", "class"};
    static const char *const types[] = {"toc", "contents", "doc-toc", "copyright", "doc-copyright",
        "titlepage", "cover", "frontmatter", "colophon", "imprint", "dedication", "abstract"};
    for (size_t a = 0; a < sizeof(attrs) / sizeof(attrs[0]); ++a) {
        if (!attribute(t, attrs[a], value, sizeof(value))) continue;
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
            if (word(value, types[i])) return true;
    }
    return false;
}

static bool body_heading_add(body_headings_t *found, size_t chapter, size_t source_offset,
                             const char *title) {
    if (found->count == EPUB_NAV_MAX) return false;
    if (found->count) {
        const nav_entry_t *last = &found->items[found->count - 1];
        if (last->chapter == chapter && !strcmp(last->title, title) &&
            source_offset >= last->source_offset && source_offset - last->source_offset < 1024)
            return true;
    }
    if (found->count == found->capacity) {
        size_t cap = found->capacity ? found->capacity * 2 : 16;
        if (cap > EPUB_NAV_MAX) cap = EPUB_NAV_MAX;
        nav_entry_t *items = heap_caps_realloc(found->items, cap * sizeof(*items),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!items) return false;
        found->items = items; found->capacity = cap;
    }
    nav_entry_t *entry = &found->items[found->count++];
    memset(entry, 0, sizeof(*entry));
    entry->chapter = (uint16_t)chapter;
    entry->source_offset = (uint32_t)source_offset;
    entry->valid = true;
    strcpy(entry->title, title);
    return true;
}

static bool body_headings_scan(book_epub_t *book, body_headings_t *found) {
    for (size_t chapter = 0; chapter < book->count; ++chapter) {
        const char *chapter_path = zip_entry_name(book->zip, book->chapters[chapter].zip_index);
        const char *name = strrchr(chapter_path, '/');
        name = name ? name + 1 : chapter_path;
        if (front_matter_path(name)) continue;
        char *html = NULL; size_t length = 0;
        if (load_entry(book, book->chapters[chapter].zip_index, &html, &length) != ESP_OK) return false;
        size_t before = found->count;
        xml_t xml; token_t t;
        xml_reader(&xml, html, length);
        size_t skipped = 0, candidate = 0, source_offset = 0;
        bool strong = false, label_ok = true, auxiliary_section = false, pending_strong = false;
        bool plain_text = false;
        size_t pending_offset = 0, link_depth = 0;
        char pending_title[EPUB_TITLE_CAP] = "";
        char title[EPUB_TITLE_CAP] = "";
        while (xml_next(&xml, &t)) {
            if (t.kind == XML_CLOSE && t.depth == link_depth) link_depth = 0;
            if (t.kind == XML_OPEN && (local_name(t.name, "head") ||
                local_name(t.name, "script") || local_name(t.name, "style") ||
                auxiliary_xml(t))) {
                if (!skipped && !t.empty) skipped = t.depth;
            }
            if (skipped) {
                if (t.kind == XML_CLOSE && t.depth == skipped) skipped = 0;
                continue;
            }
            if (t.kind == XML_OPEN && !candidate && !t.empty) {
                bool heading = local_name(t.name, "h1") || local_name(t.name, "h2") ||
                               local_name(t.name, "h3") || local_name(t.name, "h4");
                bool paragraph = local_name(t.name, "p");
                if (heading || paragraph) {
                    candidate = t.depth; strong = heading; label_ok = true; plain_text = false; link_depth = 0;
                    source_offset = (size_t)(t.name.p - html - 1); title[0] = 0;
                }
            } else if (candidate && t.kind == XML_OPEN && local_name(t.name, "a")) {
                size_t offset = 0; span_t key, value;
                while (attr_next(t.attrs, &offset, &key, &value))
                    if (equal(key, "href") && value.n) { label_ok = false; link_depth = t.depth; }
            } else if (candidate && t.kind == XML_TEXT && t.depth >= candidate) {
                if (!link_depth) for (size_t i = 0; i < t.text.n; ++i)
                    if (!space(t.text.p[i])) { plain_text = true; break; }
                if (label_ok) label_ok = decode(t.text, title, sizeof(title), true, t.cdata);
            } else if (candidate && t.kind == XML_CLOSE && t.depth == candidate) {
                size_t n = strlen(title);
                while (n && title[n - 1] == ' ') title[--n] = 0;
                if (strong && front_matter_title(title)) {
                    auxiliary_section = true;
                    pending_title[0] = 0;
                }
                bool numbered = label_ok && explicit_chapter_title(title, strong);
                // 编号列表本身不够：等到随后出现正文才收录，连续编号目录项不会互相作证。
                // Wait for following prose; consecutive numbered TOC labels cannot validate each other.
                if (numbered) {
                    pending_title[0] = 0;
                    if (strong || !auxiliary_section) {
                        strcpy(pending_title, title);
                        pending_offset = source_offset;
                        pending_strong = strong;
                    }
                } else if (!strong && plain_text && !front_matter_title(title) && pending_title[0]) {
                    if (!auxiliary_section || pending_strong) {
                        if (!body_heading_add(found, chapter, pending_offset, pending_title)) {
                            free(html); return false;
                        }
                        auxiliary_section = false;
                    }
                    pending_title[0] = 0;
                } else if (!label_ok) {
                    pending_title[0] = 0;
                }
                candidate = 0;
            }
        }
        if (xml.failed) found->count = before;
        free(html);
    }
    return true;
}

static void body_headings_prepare(book_epub_t *book) {
    if (book->body_scanned) return;
    // 大书已有正式目录时不在第一次绘制阅读页前解压数千章；当前章节仍按需加载。
    // With a large authored TOC, do not inflate thousands of chapters before the first page.
    if (book->count > 256 && book->authored_count) return;
    body_headings_t found = {0};
    if (!body_headings_scan(book, &found)) { free(found.items); return; }
    for (size_t i = 0; i < found.count; ++i) {
        nav_entry_t *entry = &found.items[i];
        chapter_t *chapter = &book->chapters[entry->chapter];
        if (i && found.items[i - 1].chapter == entry->chapter) continue;
        strcpy(chapter->title, entry->title);
        chapter->titled = true;
        chapter->heading_checked = true;
    }
    size_t authored_visible = 0;
    for (size_t i = 0; i < book->authored_count; ++i)
        if (book->navigation[i].valid && !front_matter_title(book->navigation[i].title)) ++authored_visible;
    bool authored_matches_body = authored_visible == found.count && authored_visible != 0;
    size_t matched = 0;
    for (size_t i = 0; authored_matches_body && i < book->authored_count; ++i) {
        const nav_entry_t *entry = &book->navigation[i];
        if (!entry->valid || front_matter_title(entry->title)) continue;
        authored_matches_body = entry->chapter == found.items[matched].chapter &&
                                !strcmp(entry->title, found.items[matched].title);
        ++matched;
    }
    size_t reference = authored_visible ? authored_visible : book->count;
    bool prefer_body = found.count &&
        (found.count >= 2 || reference <= 1) &&
        found.count * 4 >= reference * 3 && !authored_matches_body;
    if (prefer_body) {
        free(book->navigation);
        book->navigation = found.items;
        book->authored_count = found.count;
        book->navigation_capacity = found.capacity;
        found.items = NULL;
    } else if (book->authored_count && found.count) {
        unsigned char used[EPUB_NAV_MAX / 8] = {0};
        for (size_t j = 0; j < found.count; ++j) {
            for (size_t i = 0; i < book->authored_count; ++i) {
                nav_entry_t *entry = &book->navigation[i];
                if (entry->chapter != found.items[j].chapter ||
                    front_matter_title(entry->title) || used[i / 8] & (1u << (i % 8))) continue;
                if (strcmp(entry->title, found.items[j].title)) {
                    strcpy(entry->title, found.items[j].title);
                    entry->source_offset = found.items[j].source_offset;
                    entry->anchor[0] = 0;
                }
                used[i / 8] |= (unsigned char)(1u << (i % 8));
                break;
            }
        }
    }
    free(found.items);
    book->body_scanned = true;
    if (book->source_path[0]) epub_cache_save(book->source_path, book);
}

static void navigation_prepare(book_epub_t *book) {
    if (!book || book->navigation_ready) return;
    body_headings_prepare(book);
    bool authored = book->authored_count != 0;
    size_t capacity = authored ? book->authored_count : book->count;
    book->visible_index = psram(capacity * sizeof(*book->visible_index));
    if (authored) {
        // 临时诊断：目录条目异常（1 节）时打印每条 authored nav 的真实状态。
        // / Temporary diagnostics: dump authored nav entries when the TOC looks wrong.
        ESP_LOGI("epub_nav", "authored entries=%u", (unsigned)book->authored_count);
        for (size_t i = 0; i < book->authored_count; ++i) {
            const nav_entry_t *entry = &book->navigation[i];
            ESP_LOGI("epub_nav", "  [%u] chapter=%u valid=%d title='%.40s'",
                     (unsigned)i, (unsigned)entry->chapter, (int)entry->valid, entry->title);
        }
        for (size_t i = 0; i < book->authored_count; ++i) {
            nav_entry_t *entry = &book->navigation[i];
            entry->visible = entry->valid && !front_matter_title(entry->title);
            if (entry->visible) {
                if (book->visible_index) book->visible_index[book->navigation_count] = (uint16_t)i;
                ++book->navigation_count;
            }
        }
        book->navigation_ready = true;
        return;
    }
    bool reached_chapters = false;
    for (size_t i = 0; i < book->count; ++i) {
        chapter_t *chapter = &book->chapters[i];
        // 无正式目录时仅检查书前少量正文题头，避免打开大 EPUB 时解压全部章节。
        // With no authored TOC, inspect only early headings instead of inflating every chapter.
        if (!authored && !reached_chapters && i < 6 && !chapter->titled) chapter_heading(book, i);
        bool front = front_matter_title(chapter->title) ||
                     (!chapter->titled && front_matter_path(zip_entry_name(book->zip, chapter->zip_index)));
        chapter->toc_visible = !front;
        if (chapter->toc_visible) {
            if (book->visible_index) book->visible_index[book->navigation_count] = (uint16_t)i;
            ++book->navigation_count;
        }
        if (!front && numbered_chapter_title(chapter->title)) reached_chapters = true;
    }
    book->navigation_ready = true;
}

size_t book_epub_navigation_count(book_epub_t *book) {
    navigation_prepare(book);
    return book ? book->navigation_count : 0;
}

size_t book_epub_navigation_chapter(book_epub_t *book, size_t position) {
    navigation_prepare(book);
    if (!book || position >= book->navigation_count) return SIZE_MAX;
    if (book->visible_index) {
        size_t index = book->visible_index[position];
        return book->authored_count ? book->navigation[index].chapter : index;
    }
    if (book->authored_count) {
        for (size_t i = 0; i < book->authored_count; ++i)
            if (book->navigation[i].visible && position-- == 0) return book->navigation[i].chapter;
        return SIZE_MAX;
    }
    for (size_t i = 0; i < book->count; ++i)
        if (book->chapters[i].toc_visible && position-- == 0) return i;
    return SIZE_MAX;
}

static nav_entry_t *navigation_entry(book_epub_t *book, size_t position) {
    navigation_prepare(book);
    if (!book || !book->authored_count || position >= book->navigation_count) return NULL;
    if (book->visible_index) return &book->navigation[book->visible_index[position]];
    for (size_t i = 0; i < book->authored_count; ++i)
        if (book->navigation[i].visible && position-- == 0) return &book->navigation[i];
    return NULL;
}

esp_err_t book_epub_navigation_title(book_epub_t *book, size_t position, char *buf, size_t cap) {
    if (!buf || !cap) return ESP_ERR_INVALID_ARG;
    nav_entry_t *entry = navigation_entry(book, position);
    if (!entry) {
        size_t chapter = book_epub_navigation_chapter(book, position);
        return chapter == SIZE_MAX ? ESP_ERR_INVALID_ARG : book_epub_chapter_title(book, chapter, buf, cap);
    }
    size_t n = strlen(entry->title);
    if (n >= cap) return ESP_ERR_INVALID_SIZE;
    memcpy(buf, entry->title, n + 1);
    return ESP_OK;
}

const char *book_epub_navigation_anchor(book_epub_t *book, size_t position) {
    nav_entry_t *entry = navigation_entry(book, position);
    return entry && entry->anchor[0] ? entry->anchor : NULL;
}
size_t book_epub_navigation_source_offset(book_epub_t *book, size_t position) {
    nav_entry_t *entry = navigation_entry(book, position);
    return entry && entry->source_offset != UINT32_MAX ? entry->source_offset : SIZE_MAX;
}

esp_err_t book_epub_chapter_title(book_epub_t *book, size_t i, char *buf, size_t cap) {
    if (!book || i >= book->count || !buf || !cap) return ESP_ERR_INVALID_ARG;
    chapter_heading(book, i);
    buf[0] = 0; size_t n = strlen(book->chapters[i].title);
    if (n >= cap) return ESP_ERR_INVALID_SIZE;
    memcpy(buf, book->chapters[i].title, n + 1); return ESP_OK;
}
// 仅提取章节引用的本地样式，超过预算的 CSS 忽略但不阻断正文。
// Load bounded in-book stylesheets; oversized CSS never blocks chapter text.
static char *chapter_css(book_epub_t *book, const char *chapter_path,
                         const char *html, size_t len, size_t *css_len) {
    char *css = NULL; *css_len = 0;
    const char *at = html, *end = html + len;
    while (at < end) {
        const char *tag = at;
        while (tag + 5 <= end && strncasecmp(tag, "<link", 5)) ++tag;
        if (tag + 5 > end) break;
        const char *close = memchr(tag + 5, '>', (size_t)(end - tag - 5));
        if (!close) break;
        const char *attrs_end = close;
        while (attrs_end > tag + 5 && space(attrs_end[-1])) --attrs_end;
        if (attrs_end > tag + 5 && attrs_end[-1] == '/') --attrs_end;
        token_t t = {.attrs = {tag + 5, (size_t)(attrs_end - tag - 5)}};
        char rel[80], href[EPUB_PATH_CAP], type[80];
        if (attribute(t, "rel", rel, sizeof(rel)) && attribute(t, "href", href, sizeof(href)) &&
            attribute(t, "type", type, sizeof(type)) && href[0] &&
            (word(rel, "stylesheet") || !strcasecmp(type, "text/css"))) {
            char path[EPUB_PATH_CAP];
            if (resolve_path(chapter_path, href, path)) {
                int index = zip_find(book->zip, path);
                size_t n = index >= 0 ? zip_entry_size(book->zip, index) : 0;
                if (n && n <= EPUB_CSS_FILE_MAX && *css_len < EPUB_CSS_MAX &&
                    n < EPUB_CSS_MAX - *css_len) {
                    char *next = heap_caps_realloc(css, *css_len + n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (next) {
                        css = next;
                        if (zip_extract(book->zip, index, css + *css_len, n) == ESP_OK) {
                            *css_len += n; css[(*css_len)++] = '\n';
                        }
                    }
                }
            }
        }
        at = close + 1;
    }
    return css;
}

esp_err_t book_epub_load_target(book_epub_t *book, size_t i, const char *anchor,
                                size_t source_offset, size_t *anchor_offset, html_text_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out)); if (!book || i >= book->count) return ESP_ERR_INVALID_ARG;
    char *text; size_t len; esp_err_t err = load_entry(book, book->chapters[i].zip_index, &text, &len);
    if (err != ESP_OK) return err;
    size_t css_len = 0;
    char *css = chapter_css(book, zip_entry_name(book->zip, book->chapters[i].zip_index), text, len, &css_len);
    err = html_to_blocks_with_css_target(text, len, css, css_len,
                                         anchor, source_offset, anchor_offset, out);
    if (err == ESP_OK) chapter_breaks_prepare(book, i, out);
    free(css); free(text); return err;
}
esp_err_t book_epub_load_anchor(book_epub_t *book, size_t i, const char *anchor,
                                size_t *anchor_offset, html_text_t *out) {
    return book_epub_load_target(book, i, anchor, SIZE_MAX, anchor_offset, out);
}
esp_err_t book_epub_load(book_epub_t *book, size_t i, html_text_t *out) {
    return book_epub_load_anchor(book, i, NULL, NULL, out);
}
esp_err_t book_epub_image(book_epub_t *book, size_t chapter, const char *src,
                          uint8_t **data, size_t *size, bool *png) {
    if (!book || chapter >= book->count || !src || !data || !size || !png) return ESP_ERR_INVALID_ARG;
    *data = NULL; *size = 0; *png = false;
    char path[EPUB_PATH_CAP];
    if (!resolve_path(zip_entry_name(book->zip, book->chapters[chapter].zip_index), src, path)) return ESP_ERR_INVALID_ARG;
    int entry = zip_find(book->zip, path);
    if (entry < 0) return ESP_ERR_NOT_FOUND;
    size_t n = zip_entry_size(book->zip, entry);
    if (!n || n > ZIP_OUTPUT_MAX) return ESP_ERR_INVALID_SIZE;
    uint8_t *image = psram(n);
    if (!image) return ESP_ERR_NO_MEM;
    esp_err_t err = zip_extract(book->zip, entry, image, n);
    if (err != ESP_OK) { free(image); return err; }
    bool is_png = n >= 8 && !memcmp(image, "\x89PNG\r\n\x1a\n", 8);
    bool is_jpeg = n >= 3 && image[0] == 0xff && image[1] == 0xd8 && image[2] == 0xff;
    if (!is_png && !is_jpeg) {
        // 许多中文 EPUB 用 SVG 只包一张 JPEG/PNG。解析这一层引用即可复用现有低内存解码器。
        // Many Chinese EPUBs wrap one JPEG/PNG in SVG. Resolve that one reference and reuse the bounded decoder.
        char nested[EPUB_PATH_CAP] = {0};
        for (size_t i = 0; i + 4 < n && !nested[0]; ++i) {
            if (strncasecmp((const char *)image + i, "href", 4)) continue;
            size_t p = i + 4;
            while (p < n && (image[p] == ' ' || image[p] == '\t' || image[p] == '\r' || image[p] == '\n')) ++p;
            if (p >= n || image[p++] != '=') continue;
            while (p < n && (image[p] == ' ' || image[p] == '\t' || image[p] == '\r' || image[p] == '\n')) ++p;
            if (p >= n || (image[p] != '\'' && image[p] != '"')) continue;
            uint8_t quote = image[p++]; size_t start = p;
            while (p < n && image[p] != quote) ++p;
            size_t length = p - start;
            if (!length || length >= sizeof(nested)) continue;
            char href[EPUB_PATH_CAP] = {0};
            if (!decode((span_t){(const char *)image + start, length}, href, sizeof(href), false, false) ||
                !resolve_path(path, href, nested)) nested[0] = 0;
        }
        free(image); image = NULL;
        if (!nested[0]) return ESP_ERR_NOT_SUPPORTED;
        entry = zip_find(book->zip, nested);
        n = entry >= 0 ? zip_entry_size(book->zip, entry) : 0;
        if (entry < 0) return ESP_ERR_NOT_FOUND;
        if (!n || n > ZIP_OUTPUT_MAX) return ESP_ERR_INVALID_SIZE;
        image = psram(n);
        if (!image) return ESP_ERR_NO_MEM;
        err = zip_extract(book->zip, entry, image, n);
        if (err != ESP_OK) { free(image); return err; }
        is_png = n >= 8 && !memcmp(image, "\x89PNG\r\n\x1a\n", 8);
        is_jpeg = n >= 3 && image[0] == 0xff && image[1] == 0xd8 && image[2] == 0xff;
        if (!is_png && !is_jpeg) { free(image); return ESP_ERR_NOT_SUPPORTED; }
    }
    *data = image; *size = n; *png = is_png;
    return ESP_OK;
}
uint32_t book_epub_total_bytes(const book_epub_t *book) { return book ? book->total : 0; }
uint32_t book_epub_chapter_byte_offset(const book_epub_t *book, size_t i) { return book && i < book->count ? book->chapters[i].offset : 0; }

esp_err_t book_epub_metadata_cached(const char *path, char *title, size_t title_cap,
                                    char *author, size_t author_cap) {
    if (!path || !title || !title_cap || !author || !author_cap) return ESP_ERR_INVALID_ARG;
    title[0] = author[0] = 0;
    char cache_path[112]; book_index_cache_header_t cache_key;
    bool cacheable = book_index_cache_prepare(path, "epmeta", EPUB_META_CACHE_VERSION,
                                              cache_path, sizeof(cache_path), &cache_key);
    if (cacheable) {
        FILE* cache = book_index_cache_open_read(cache_path, &cache_key);
        if (cache) {
            epub_meta_cache_payload_t payload;
            bool ok = fread(&payload, 1, sizeof(payload), cache) == sizeof(payload) &&
                fgetc(cache) == EOF && memchr(payload.title, 0, sizeof(payload.title)) &&
                memchr(payload.author, 0, sizeof(payload.author));
            fclose(cache);
            if (ok) {
                size_t tn = strlen(payload.title), an = strlen(payload.author);
                if (tn >= title_cap || an >= author_cap) return ESP_ERR_INVALID_SIZE;
                memcpy(title, payload.title, tn + 1); memcpy(author, payload.author, an + 1);
                return ESP_OK;
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t book_epub_metadata(const char *path, char *title, size_t title_cap, char *author, size_t author_cap) {
    if (!path || !title || !title_cap || !author || !author_cap) return ESP_ERR_INVALID_ARG;
    if (book_epub_metadata_cached(path, title, title_cap, author, author_cap) == ESP_OK) return ESP_OK;
    char cache_path[112]; book_index_cache_header_t cache_key;
    bool cacheable = book_index_cache_prepare(path, "epmeta", EPUB_META_CACHE_VERSION,
                                              cache_path, sizeof(cache_path), &cache_key);
    book_epub_t *book = psram(sizeof(*book));
    if (!book) return ESP_ERR_NO_MEM;
    memset(book, 0, sizeof(*book));
    esp_err_t err = zip_open(path, &book->zip);
    char opf[EPUB_PATH_CAP];
    if (err == ESP_OK) err = container_path(book, opf);
    char *text = NULL; size_t len = 0;
    if (err == ESP_OK) err = load_entry(book, zip_find(book->zip, opf), &text, &len);
    if (err == ESP_OK) {
        xml_t xml; token_t t; xml_reader(&xml, text, len);
        size_t title_depth = 0, creator_depth = 0;
        while (xml_next(&xml, &t)) {
            if (t.kind == XML_OPEN && !title[0] && local_name(t.name, "title")) title_depth = t.depth;
            else if (t.kind == XML_OPEN && !author[0] && local_name(t.name, "creator")) creator_depth = t.depth;
            else if (t.kind == XML_TEXT && title_depth && t.depth == title_depth) {
                if (!decode(t.text, title, title_cap, true, t.cdata)) title[0] = 0;
                title_depth = 0;
            } else if (t.kind == XML_TEXT && creator_depth && t.depth == creator_depth) {
                if (!decode(t.text, author, author_cap, true, t.cdata)) author[0] = 0;
                creator_depth = 0;
            }
            if (title[0] && author[0]) break;
        }
        if (!title[0]) err = ESP_ERR_NOT_FOUND;
        else if (cacheable) {
            epub_meta_cache_payload_t payload = {0};
            snprintf(payload.title, sizeof(payload.title), "%s", title);
            snprintf(payload.author, sizeof(payload.author), "%s", author);
            char temp[120];
            FILE* cache = book_index_cache_open_write(cache_path, &cache_key, temp, sizeof(temp));
            if (cache) {
                bool ok = fwrite(&payload, 1, sizeof(payload), cache) == sizeof(payload);
                (void)book_index_cache_finish_write(cache, temp, cache_path, ok);
            }
        }
    }
    free(text); book_epub_close(book);
    return err;
}

esp_err_t book_epub_cover(const char *path, uint8_t **data, size_t *size, bool *is_png) {
    if (!path || !data || !size || !is_png) return ESP_ERR_INVALID_ARG;
    *data = NULL; *size = 0; *is_png = false;
    book_epub_t *book = psram(sizeof(*book));
    if (!book) return ESP_ERR_NO_MEM;
    memset(book, 0, sizeof(*book));
    esp_err_t err = zip_open(path, &book->zip);
    char opf[EPUB_PATH_CAP] = {0};
    if (err == ESP_OK) err = container_path(book, opf);
    char *xml_text = NULL; size_t xml_len = 0;
    if (err == ESP_OK) err = load_entry(book, zip_find(book->zip, opf), &xml_text, &xml_len);
    if (err == ESP_OK) {
        char cover_id[EPUB_ID_CAP] = {0}, cover_path[EPUB_PATH_CAP] = {0};
        bool cover_png = false;
        xml_t xml; token_t t; xml_reader(&xml, xml_text, xml_len);
        while (xml_next(&xml, &t)) {
            if (t.kind == XML_OPEN && local_name(t.name, "meta")) {
                char name[64], content[EPUB_ID_CAP];
                if (attribute(t, "name", name, sizeof(name)) && attribute(t, "content", content, sizeof(content)) && !strcmp(name, "cover"))
                    snprintf(cover_id, sizeof(cover_id), "%s", content);
            }
        }
        if (!xml.failed) {
            xml_reader(&xml, xml_text, xml_len);
            while (xml_next(&xml, &t)) {
                if (t.kind != XML_OPEN || !local_name(t.name, "item")) continue;
                char id[EPUB_ID_CAP], href[EPUB_PATH_CAP], media[80], props[256];
                if (!attribute(t, "id", id, sizeof(id)) || !attribute(t, "href", href, sizeof(href)) ||
                    !attribute(t, "media-type", media, sizeof(media)) || !attribute(t, "properties", props, sizeof(props))) continue;
                bool png = !strcmp(media, "image/png"), jpeg = !strcmp(media, "image/jpeg") || !strcmp(media, "image/jpg");
                if (!(png || jpeg) || !(word(props, "cover-image") || (cover_id[0] && !strcmp(id, cover_id)))) continue;
                if (resolve_path(opf, href, cover_path)) { cover_png = png; break; }
            }
        }
        if (!cover_path[0]) err = ESP_ERR_NOT_FOUND;
        else {
            int index = zip_find(book->zip, cover_path);
            size_t bytes = zip_entry_size(book->zip, index);
            if (index < 0) err = ESP_ERR_NOT_FOUND;
            else if (!bytes || bytes > ZIP_OUTPUT_MAX) err = ESP_ERR_INVALID_SIZE;
            else {
                uint8_t *image = psram(bytes);
                if (!image) err = ESP_ERR_NO_MEM;
                else {
                    err = zip_extract(book->zip, index, image, bytes);
                    if (err == ESP_OK) { *data = image; *size = bytes; *is_png = cover_png; }
                    else free(image);
                }
            }
        }
    }
    free(xml_text); book_epub_close(book); return err;
}

esp_err_t book_epub_image_dimensions(book_epub_t *book, size_t chapter, const char *src, unsigned *width, unsigned *height) {
    if (!book || chapter >= book->count || !src || !width || !height) return ESP_ERR_INVALID_ARG;
    *width = *height = 0;
    char path[EPUB_PATH_CAP];
    if (!resolve_path(zip_entry_name(book->zip, book->chapters[chapter].zip_index), src, path)) return ESP_ERR_INVALID_ARG;
    int entry = zip_find(book->zip, path);
    if (entry < 0) return ESP_ERR_NOT_FOUND;
    size_t size = zip_entry_size(book->zip, entry);
    if (!size) return ESP_ERR_INVALID_SIZE;
    if (size > 65536) size = 65536;
    uint8_t *header = psram(size);
    if (!header) return ESP_ERR_NO_MEM;
    esp_err_t err = zip_extract_prefix(book->zip, entry, header, size);
    if (err == ESP_OK) {
        bool png = size >= 8 && !memcmp(header, "\x89PNG\r\n\x1a\n", 8);
        if (!book_image_dimensions(header, size, png, width, height)) err = ESP_ERR_NOT_SUPPORTED;
    }
    free(header);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        // 头部不足或 SVG 包装时，复用插图读取路径；缓存到块中，避免每页重复读取。
        // For a late frame header or SVG wrapper, reuse image loading; callers cache dimensions in blocks.
        uint8_t *image = NULL; size_t bytes = 0; bool png = false;
        err = book_epub_image(book, chapter, src, &image, &bytes, &png);
        if (err == ESP_OK && !book_image_dimensions(image, bytes, png, width, height))
            err = ESP_ERR_NOT_SUPPORTED;
        free(image);
    }
    return err;
}

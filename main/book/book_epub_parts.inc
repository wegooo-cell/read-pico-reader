/* SPDX-License-Identifier: Apache-2.0 */
#define EPUB_PART_TARGET (12u * 1024u)
#define EPUB_PART_WINDOW (48u * 1024u)
#define EPUB_PART_WRAPPERS 4096u
#define EPUB_PART_HEAD 8192u

typedef struct {
    uint16_t start, name, length;
} epub_part_tag_t;

struct epub_parts {
    zip_stream_t *stream;
    size_t chapter, total, source, available, cut, base, text_len;
    size_t head_len, wrappers_len, prefix_len, depth, head_start;
    char *css;
    size_t css_len;
    char head[EPUB_PART_HEAD], wrappers[EPUB_PART_WRAPPERS];
    epub_part_tag_t tags[EPUB_XML_DEPTH];
    char buffer[EPUB_PART_HEAD + EPUB_PART_WRAPPERS + EPUB_PART_WINDOW + 1];
};

static void epub_parts_close(epub_parts_t *parts) {
    if (!parts) return;
    zip_stream_close(parts->stream);
    free(parts->css);
    free(parts);
}

static void epub_parts_reset(book_epub_t *book) {
    epub_parts_close(book->parts);
    book->parts = NULL;
}

static bool part_tag_is(span_t name, const char *word) {
    return name.n == strlen(word) && !strncasecmp(name.p, word, name.n);
}

// Read one complete lexical token. Quotes, comment and CDATA bodies can contain '>'.
static const char *part_tag_end(const char *tag, const char *end) {
    const char *marker = NULL;
    size_t skip = 0;
    if (end - tag >= 4 && !memcmp(tag, "<!--", 4)) { marker = "-->"; skip = 4; }
    else if (end - tag >= 9 && !memcmp(tag, "<![CDATA[", 9)) { marker = "]]>"; skip = 9; }
    else if (end - tag >= 2 && tag[1] == '?') { marker = "?>"; skip = 2; }
    if (marker) {
        const char *close = strstr(tag + skip, marker);
        return close ? close + strlen(marker) : NULL;
    }
    char quote = 0;
    for (const char *p = tag + 1; p < end; ++p) {
        if (quote) { if (*p == quote) quote = 0; }
        else if (*p == '\'' || *p == '"') quote = *p;
        else if (*p == '>') return p + 1;
    }
    return NULL;
}

static bool part_void(span_t name) {
    static const char *const tags[] = {"img", "br", "hr", "meta", "link", "input", "area",
        "base", "col", "embed", "param", "source", "track", "wbr"};
    for (size_t i = 0; i < sizeof(tags) / sizeof(*tags); ++i)
        if (part_tag_is(name, tags[i])) return true;
    return false;
}

static esp_err_t part_cut(epub_parts_t *s, char *raw) {
    const char *end = raw + s->available;
    const bool final_window = s->source + s->available == s->total;
    const char *p = raw;
    bool in_head = false;
    const size_t goal = s->source ? EPUB_PART_TARGET : EPUB_PART_TARGET / 2;
    while (p < end) {
        size_t at = (size_t)(p - raw);
        bool can_split = !in_head && s->total - s->source - at > goal;
        if (*p != '<') {
            unsigned char first = (unsigned char)*p++;
            if (first == '&') {
                while (p < end && p - raw - at < 40 && *p != '<' && !space(*p)) {
                    if (*p++ == ';') break;
                }
            } else while (p < end && ((unsigned char)*p & 0xc0u) == 0x80u) ++p;
            // A giant single paragraph may have no closing tag for many pages.
            if (can_split && at >= 2 * goal && p < end) break;
            continue;
        }
        const char *tag = p;
        p = part_tag_end(tag, end);
        if (!p) {
            if (final_window) { p = end; break; }
            if (!at || in_head) return ESP_ERR_NOT_SUPPORTED;
            p = tag;
            break;
        }
        if (tag[1] == '!' || tag[1] == '?') continue;
        bool closing = tag[1] == '/';
        const char *name_start = tag + 1 + closing;
        const char *name_end = name_start;
        while (name_end < p && name_char((unsigned char)*name_end)) ++name_end;
        span_t name = {.p = name_start, .n = (size_t)(name_end - name_start)};
        if (!name.n) continue;
        if (!closing && (part_tag_is(name, "style") || part_tag_is(name, "script"))) {
            // These elements use raw text. Keep their complete content in the same window.
            const char *raw_end = p;
            for (; raw_end < end; ++raw_end) {
                if (*raw_end == '<' && (size_t)(end - raw_end) > 2 + name.n &&
                    raw_end[1] == '/' && !strncasecmp(raw_end + 2, name.p, name.n) &&
                    (raw_end[2 + name.n] == '>' || space(raw_end[2 + name.n]))) break;
            }
            const char *close = raw_end < end ? part_tag_end(raw_end, end) : NULL;
            if (!close) {
                if (final_window) { p = end; break; }
                if (!at || in_head) return ESP_ERR_NOT_SUPPORTED;
                p = tag;
                break;
            }
            p = close;
            continue;
        }
        if (closing) {
            for (size_t i = s->depth; i; --i) {
                epub_part_tag_t frame = s->tags[i - 1];
                if (frame.length != name.n || strncasecmp(s->wrappers + frame.name, name.p, name.n)) continue;
                s->depth = i - 1;
                s->wrappers_len = frame.start;
                break;
            }
            if (part_tag_is(name, "head")) {
                in_head = false;
                size_t n = (size_t)(p - raw) - s->head_start;
                if (n <= sizeof(s->head)) {
                    memcpy(s->head, raw + s->head_start, n);
                    s->head_len = n;
                }
            }
            if (can_split && at >= goal && !in_head &&
                (part_tag_is(name, "p") || part_tag_is(name, "div") || part_tag_is(name, "li") ||
                 part_tag_is(name, "section") || part_tag_is(name, "blockquote"))) break;
            continue;
        }
        if (part_tag_is(name, "head")) { in_head = true; s->head_start = at; }
        const char *tail = p - 1;
        while (tail > name_end && space(tail[-1])) --tail;
        if ((tail > name_end && tail[-1] == '/') || part_void(name)) continue;
        size_t n = (size_t)(p - tag);
        if (n > sizeof(s->wrappers) - s->wrappers_len || s->depth == EPUB_XML_DEPTH)
            return ESP_ERR_NOT_SUPPORTED;
        size_t length = s->wrappers_len;
        memcpy(s->wrappers + length, tag, n);
        s->tags[s->depth++] = (epub_part_tag_t){length, length + (size_t)(name.p - tag), name.n};
        s->wrappers_len += n;
    }
    if (in_head && !final_window) return ESP_ERR_NOT_SUPPORTED;
    s->cut = (size_t)(p - raw);
    return s->cut ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t part_advance(book_epub_t *book, epub_parts_t *s) {
    char *raw = s->buffer + EPUB_PART_HEAD + EPUB_PART_WRAPPERS;
    size_t consumed = s->cut;
    size_t remaining = s->available - consumed;
    if (consumed) s->base += s->text_len + 1;
    s->source += consumed;
    memmove(raw, raw + consumed, remaining);
    size_t more;
    esp_err_t err = zip_stream_read(s->stream, raw + remaining, EPUB_PART_WINDOW - remaining, &more);
    if (err != ESP_OK) return err;
    s->available = remaining + more;
    raw[s->available] = 0;
    s->prefix_len = s->source ? s->head_len + s->wrappers_len : 0;
    if (s->source) {
        memcpy(raw - s->prefix_len, s->head, s->head_len);
        memcpy(raw - s->wrappers_len, s->wrappers, s->wrappers_len);
    }
    err = part_cut(s, raw);
    if (err == ESP_OK && !s->source)
        s->css = chapter_css(book, zip_entry_name(book->zip, book->chapters[s->chapter].zip_index),
                             raw, s->cut, &s->css_len);
    return err;
}

static esp_err_t parts_start(book_epub_t *book, size_t chapter, size_t bytes) {
    epub_parts_reset(book);
    epub_parts_t *s = psram(sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;
    memset(s, 0, offsetof(epub_parts_t, head));
    s->chapter = chapter;
    s->total = bytes;
    book->parts = s;
    esp_err_t err = zip_stream_open(book->zip, book->chapters[chapter].zip_index, &s->stream);
    return err == ESP_OK ? part_advance(book, s) : err;
}

esp_err_t book_epub_load_part(book_epub_t *book, size_t chapter, size_t offset, bool last,
                              const char *anchor, size_t source_offset, size_t *local_offset,
                              html_text_t *out) {
    if (!book || chapter >= book->count || !out || !local_offset) return ESP_ERR_INVALID_ARG;
    size_t bytes = zip_entry_size(book->zip, book->chapters[chapter].zip_index);
    if (bytes <= EPUB_PART_THRESHOLD) {
        epub_parts_reset(book);
        *local_offset = offset;
        return book_epub_load_target(book, chapter, anchor, source_offset,
                                     (anchor || source_offset != SIZE_MAX) ? local_offset : NULL, out);
    }
    epub_parts_t *s = book->parts;
    esp_err_t err;
    if (!s || s->chapter != chapter || offset < s->base || anchor || source_offset < s->source) {
        err = parts_start(book, chapter, bytes);
        if (err != ESP_OK) {
            epub_parts_reset(book);
            if (err != ESP_ERR_NOT_SUPPORTED || bytes > EPUB_ENTRY_MAX) return err;
            *local_offset = offset;
            return book_epub_load_target(book, chapter, anchor, source_offset,
                                         (anchor || source_offset != SIZE_MAX) ? local_offset : NULL, out);
        }
        s = book->parts;
    }
    for (;;) {
        bool more = s->source + s->cut < s->total;
        size_t target = source_offset;
        if (target >= s->source && target < s->source + s->cut) target = target - s->source + s->prefix_len;
        else target = SIZE_MAX;
        const char *raw = s->buffer + EPUB_PART_HEAD + EPUB_PART_WRAPPERS;
        *local_offset = 0;
        err = parse_chapter_html(book, chapter, raw - s->prefix_len, s->prefix_len + s->cut,
                                s->css, s->css_len, anchor, target, local_offset, out);
        if (err != ESP_OK) break;
        s->text_len = out->len;
        size_t next = s->base + s->text_len + 1;
        bool searching = anchor || source_offset != SIZE_MAX;
        bool found = searching ? out->target_found : offset < next;
        if (!more || (!last && found && (out->count || out->len))) {
            // A missing TOC anchor keeps its existing behavior of landing at the chapter start.
            if (searching && !found && s->base) {
                html_text_free(out);
                return book_epub_load_part(book, chapter, 0, false, NULL, SIZE_MAX, local_offset, out);
            }
            if (!searching) *local_offset = offset > s->base ? offset - s->base : 0;
            out->part_base = s->base;
            out->part_more = more;
            out->source_begin = s->source;
            out->source_end = s->source + s->cut;
            return ESP_OK;
        }
        html_text_free(out);
        err = part_advance(book, s);
        if (err != ESP_OK) break;
    }
    epub_parts_reset(book);
    return err;
}

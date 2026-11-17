/*
 * 中文：为本地 TXT 批量查找在线封面，书名、作者和封面作为同一份 TF 记录提交。
 * English: Look up covers for local TXT books and commit title, author and pixels as one TF record.
 */
#include "book_weread_cover.h"
#include "book_cover.h"
#include "book_title.h"
#include "gbk.h"
#include "read_pico_sd.h"
#include "read_pico_sd_access.h"
#include "read_pico_transfer.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define COVER_DIRECTORY "/sdcard/.kiikoread/covers"
#define COVER_MAGIC UINT32_C(0x57435231)
#define COVER_BYTES (BOOK_COVER_W * BOOK_COVER_H)
#define HTTP_LIMIT (256u * 1024u)
#define COVER_WORKER_STACK (24u * 1024u)
#define TITLE_CAP (sizeof(((book_weread_cover_option_t *)0)->title))
#define AUTHOR_CAP (sizeof(((book_weread_cover_option_t *)0)->author))

typedef struct {
    char title[TITLE_CAP], author[AUTHOR_CAP];
} book_name_t;

typedef struct {
    uint64_t size;
    int64_t modified;
    uint32_t magic;
    book_weread_path_t source;
    book_name_t name;
} cover_record_t;


static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static book_weread_cover_status_t s_status;
static bool s_cancel;
static unsigned s_revision;
static const char *const TAG = "weread_cover";

bool book_weread_cover_is_txt(const char *path) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && !strcasecmp(dot, ".txt");
}

static bool identity(const char *path, struct stat *st) {
    return book_weread_cover_is_txt(path) && strlen(path) < sizeof(book_weread_path_t) &&
        !stat(path, st) && S_ISREG(st->st_mode) && st->st_size >= 0;
}

static void record_path(const char *path, const struct stat *st, char name[128]) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    snprintf(name, 128, COVER_DIRECTORY "/wr-%016" PRIx64 "-%" PRIx64 "-%" PRIx64 ".bin",
             hash, (uint64_t)st->st_size, (uint64_t)st->st_mtime);
}

static FILE *open_record(const char *path, cover_record_t *record) {
    if (!book_weread_cover_is_txt(path) || !read_pico_sd_io_ready()) return NULL;
    struct stat st;
    if (!identity(path, &st)) return NULL;
    char name[128];
    record_path(path, &st, name);
    FILE *file = fopen(name, "rb");
    if (!file) return NULL;
    if (fread(record, sizeof(*record), 1, file) != 1 || record->magic != COVER_MAGIC ||
        record->size != (uint64_t)st.st_size || record->modified != st.st_mtime ||
        strncmp(record->source, path, sizeof(record->source)) ||
        !record->name.title[0] || !memchr(record->name.title, 0, TITLE_CAP) ||
        !memchr(record->name.author, 0, AUTHOR_CAP) ||
        fseek(file, 0, SEEK_END) || ftell(file) != (long)(sizeof(*record) + COVER_BYTES) ||
        fseek(file, sizeof(*record), SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    return file;
}

bool book_weread_cover_metadata(const char *path, char *title, size_t title_cap,
                                char *author, size_t author_cap) {
    cover_record_t record;
    FILE *file = open_record(path, &record);
    if (!file) return false;
    fclose(file);
    if (title && strlen(record.name.title) < title_cap) {
        char custom[TITLE_CAP];
        const char *display = book_title_get(path, custom, sizeof(custom)) &&
            strlen(custom) < title_cap ? custom : record.name.title;
        strcpy(title, display);
    }
    if (author && strlen(record.name.author) < author_cap) strcpy(author, record.name.author);
    return true;
}

uint64_t book_weread_cover_stamp(const char *path) {
    cover_record_t record;
    FILE *file = open_record(path, &record);
    if (!file) return 0;
    struct stat st;
    uint64_t stamp = fstat(fileno(file), &st) == 0 ? (uint64_t)st.st_mtime : 0;
    fclose(file);
    return stamp | (UINT64_C(1) << 63);
}

bool book_weread_cover_read(const char *path, uint8_t *out, unsigned width, unsigned height) {
    if (!out || !width || !height || width > BOOK_COVER_W || height > BOOK_COVER_H) return false;
    cover_record_t record;
    FILE *file = open_record(path, &record);
    if (!file) return false;
    bool ok = true;
    if (width == BOOK_COVER_W && height == BOOK_COVER_H) {
        ok = fread(out, COVER_BYTES, 1, file) == 1;
    } else {
        uint8_t row[BOOK_COVER_W];
        const book_crop_t crop = book_cover_crop(BOOK_COVER_W, BOOK_COVER_H, width, height);
        for (unsigned y = 0; y < height && ok; ++y) {
            const unsigned sy = crop.y + y * crop.height / height;
            ok = !fseek(file, (long)sizeof(record) + sy * BOOK_COVER_W, SEEK_SET) &&
                fread(row, sizeof(row), 1, file) == 1;
            if (ok) for (unsigned x = 0; x < width; ++x)
                out[(size_t)y * width + x] = row[crop.x + x * crop.width / width];
        }
    }
    fclose(file);
    return ok;
}

static bool cancelled(void) {
    portENTER_CRITICAL(&s_lock);
    const bool result = s_cancel;
    portEXIT_CRITICAL(&s_lock);
    return result;
}

static bool save_record(const char *path, const struct stat *expected, const book_name_t *name,
                        const uint8_t *pixels) {
    if (cancelled() || !read_pico_sd_io_ready()) return false;
#if KIIKO_BOARD_METALIO47
    read_pico_sd_access_scope_t access __attribute__((cleanup(read_pico_sd_access_scope_end))) =
        read_pico_sd_access_scope(COVER_DIRECTORY);
    if (!access.allowed) return false;
#endif
    struct stat st;
    if (!identity(path, &st) || st.st_size != expected->st_size || st.st_mtime != expected->st_mtime)
        return false;
    if (mkdir("/sdcard/.kiikoread", 0777) && errno != EEXIST) return false;
    if (mkdir(COVER_DIRECTORY, 0777) && errno != EEXIST) return false;
    char target[128], part[136];
    record_path(path, &st, target);
    snprintf(part, sizeof(part), "%s.part", target);
    cover_record_t record = {.size = st.st_size, .modified = st.st_mtime, .magic = COVER_MAGIC, .name = *name};
    strcpy(record.source, path);
    FILE *file = fopen(part, "wb");
    if (!file) return false;
    bool ok = fwrite(&record, sizeof(record), 1, file) == 1 && fwrite(pixels, COVER_BYTES, 1, file) == 1;
    if (fclose(file)) ok = false;
    if (ok && !cancelled()) {
        if (!rename(part, target)) return true;
        // FAT rename rejects an existing destination. Only displace an unreadable record.
        // FAT 不覆盖已存在的目标；仅移除无法读取的旧记录。
        if (errno == EEXIST && !book_weread_cover_metadata(path, NULL, 0, NULL, 0) &&
            !cancelled() && !unlink(target) && !rename(part, target)) return true;
    }
    unlink(part);
    return false;
}

static bool noise(const char *word) {
    static const char *const ignored[] = {
        "完结", "全本", "精校", "精校版", "无删减", "下载", "副本", "电子书", "小说",
        "TXT", "ZLib", "Z-Library", "z-lib", "全本精校", "完结精校", "完结精校版", "校对版",
    };
    for (unsigned i = 0; i < sizeof(ignored) / sizeof(*ignored); ++i)
        if (!strcasecmp(word, ignored[i])) return true;
    return *word && strspn(word, "0123456789") == strlen(word);
}

static void trim(char *text) {
    char *start = text + strspn(text, " \t\r\n_[]");
    size_t size = strlen(start);
    while (size && strchr(" \t\r\n_[]", start[size - 1])) --size;
    memmove(text, start, size);
    text[size] = 0;
}

static void strip_promotion(char *title) {
    const size_t n = strlen(title);
    const char *starts[] = {"（", "(", "【"};
    for (unsigned i = 0; i < sizeof(starts) / sizeof(*starts); ++i) {
        char *suffix = strstr(title, starts[i]);
        if (suffix && suffix != title && n >= 3 &&
            (title[n - 1] == ')' || !strcmp(title + n - 3, "）") || !strcmp(title + n - 3, "】")) &&
            (strstr(suffix, "同名影视原著") || (strstr(suffix, "主演") && strstr(suffix, "原著")))) {
            *suffix = 0;
            break;
        }
    }
    trim(title);
}

static void strip_filename_tags(char *title) {
    static const struct {const char *open, *close;} pairs[] = {
        {"（", "）"}, {"(", ")"}, {"【", "】"}, {"[", "]"}, {" ", ""},
    };
    for (unsigned pass = 0; pass < 3; ++pass) {
        bool removed = false;
        for (unsigned i = 0; i < sizeof(pairs) / sizeof(*pairs); ++i) {
            char *at = NULL;
            const size_t open_len = strlen(pairs[i].open);
            for (char *next = strstr(title, pairs[i].open); next; next = strstr(next + open_len, pairs[i].open))
                at = next;
            if (!at || at == title) continue;
            const size_t end_len = strlen(pairs[i].close);
            const char *tag = at + open_len;
            const size_t n = strlen(tag);
            char label[32];
            if (n <= end_len || n - end_len >= sizeof(label) ||
                strcmp(tag + n - end_len, pairs[i].close)) continue;
            memcpy(label, tag, n - end_len);
            label[n - end_len] = 0;
            trim(label);
            if (!*label || isdigit((unsigned char)*label) || !noise(label)) continue;
            *at = 0;
            trim(title);
            removed = true;
            break;
        }
        if (!removed) break;
    }
}

static bool enclosed(const char *text, char *title, size_t capacity) {
    const char *start = strstr(text, "《");
    const char *end = start ? strstr(start + strlen("《"), "》") : NULL;
    if (!end) return false;
    start += strlen("《");
    if (end <= start || (size_t)(end - start) >= capacity) return false;
    memcpy(title, start, end - start);
    title[end - start] = 0;
    trim(title);
    return *title;
}

static void bounded_copy(char *out, size_t cap, const char *in) {
    size_t n = strlen(in);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)in[n] & 0xc0) == 0x80) --n;
    }
    memcpy(out, in, n);
    out[n] = 0;
}

static void credit(const char *start, char author[AUTHOR_CAP]) {
    start += strspn(start, " \t_");
    const size_t n = strcspn(start, "_[\r\n");
    if (!n || n >= AUTHOR_CAP) return;
    memcpy(author, start, n);
    author[n] = 0;
    char *suffix = strstr(author, "【");
    if (suffix) *suffix = 0;
    trim(author);
    if (noise(author)) author[0] = 0;
    if (*author) book_author_clean_display(author, AUTHOR_CAP);
}

static void header_name(const char *path, book_name_t *name) {
    FILE *file = fopen(path, "rb");
    if (!file) return;
    char raw[1025], decoded[1540];
    const size_t size = fread(raw, 1, sizeof(raw) - 1, file);
    fclose(file);
    raw[size] = 0;
    const char *text = raw;
    if (!strstr(raw, "书名") && !strstr(raw, "作者")) {
        gbk_to_utf8(raw, size, decoded, sizeof(decoded));
        text = decoded;
    }
    const char *title = strstr(text, "书名：");
    if (!title) title = strstr(text, "书名:");
    if (title) {
        title += strstr(title, "书名：") == title ? strlen("书名：") : strlen("书名:");
        char line[TITLE_CAP] = {0};
        const size_t n = strcspn(title, "\r\n");
        if (n && n < sizeof(line)) {
            memcpy(line, title, n);
            if (!enclosed(line, name->title, sizeof(name->title))) {
                trim(line);
                bounded_copy(name->title, sizeof(name->title), line);
            }
        }
    }
    const char *author = strstr(text, "作者：");
    if (!author) author = strstr(text, "作者:");
    if (author) credit(author + (strstr(author, "作者：") == author ? strlen("作者：") : strlen("作者:")), name->author);
}

static bool extract_name(const char *path, book_name_t *name) {
    if (!book_name_from_path(path, name->title, sizeof(name->title), name->author, sizeof(name->author))) return false;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[BOOK_STORE_PATH_MAX];
    bounded_copy(stem, sizeof(stem), base);
    char *extension = strrchr(stem, '.');
    if (!extension) return false;
    *extension = 0;
    if (enclosed(stem, name->title, sizeof(name->title))) {
        const char *after = strstr(stem, "》") + strlen("》");
        const char *mark = strstr(after, "作者：");
        if (!mark) mark = strstr(after, "作者:");
        if (mark) after = mark + (strstr(mark, "作者：") == mark ? strlen("作者：") : strlen("作者:"));
        if (mark || !name->author[0]) credit(after, name->author);
    } else {
        const char *mark = strstr(stem, "作者：");
        if (!mark) mark = strstr(stem, "作者:");
        if (mark) credit(mark + (strstr(mark, "作者：") == mark ? strlen("作者：") : strlen("作者:")), name->author);
        char cleaned[TITLE_CAP] = {0};
        size_t used = 0;
        char *cursor = NULL;
        for (char *word = strtok_r(name->title, "_", &cursor); word; word = strtok_r(NULL, "_", &cursor)) {
            trim(word);
            if (noise(word) || !*word) continue;
            const size_t n = strlen(word);
            if (used + (used != 0) + n >= sizeof(cleaned)) break;
            if (used) cleaned[used++] = ' ';
            memcpy(cleaned + used, word, n + 1);
            used += n;
        }
        if (*cleaned) strcpy(name->title, cleaned);
        char *tag_end = strstr(name->title, "】");
        if (!strncmp(name->title, "【", strlen("【")) && tag_end) {
            char tag[24] = {0};
            const size_t bytes = tag_end - name->title - strlen("【");
            if (bytes < sizeof(tag)) {
                memcpy(tag, name->title + strlen("【"), bytes);
                if (noise(tag)) {
                    const char *rest = tag_end + strlen("】");
                    memmove(name->title, rest, strlen(rest) + 1);
                    trim(name->title);
                }
            }
        }
    }
    if (noise(name->title) || !name->author[0]) {
        book_name_t from_header = {0};
        header_name(path, &from_header);
        if (noise(name->title) && from_header.title[0]) strcpy(name->title, from_header.title);
        if (!name->author[0]) strcpy(name->author, from_header.author);
    }
    strip_promotion(name->title);
    strip_filename_tags(name->title);
    return name->title[0] && !noise(name->title);
}

static void normalized(char out[TITLE_CAP], const char *text) {
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && used + 1 < TITLE_CAP;) {
        if (*p < 128) {
            if (isalnum(*p)) out[used++] = (char)tolower(*p);
            ++p;
        } else {
            size_t bytes = (*p & 0xe0) == 0xc0 ? 2 : (*p & 0xf0) == 0xe0 ? 3 : (*p & 0xf8) == 0xf0 ? 4 : 1;
            if (strnlen((const char *)p, bytes) < bytes || used + bytes >= TITLE_CAP) break;
            if (bytes == 3 && p[0] == 0xef && p[1] >= 0xbc && p[1] <= 0xbd) {
                unsigned cp = ((p[0] & 15u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u);
                if (cp >= 0xff01 && cp <= 0xff5e && isalnum((int)(cp - 0xfee0)))
                    out[used++] = (char)tolower((int)(cp - 0xfee0));
            } else if (!(bytes == 3 && p[0] == 0xe3 && p[1] == 0x80) &&
                       !(bytes == 2 && p[0] == 0xc2 && p[1] == 0xb7)) {
                memcpy(out + used, p, bytes);
                used += bytes;
            }
            p += bytes;
        }
    }
    out[used] = 0;
}

enum { MATCH_TYPO = 50, MATCH_SUBTITLE = 60, MATCH_SERIES = 70, MATCH_EXACT = 90 };

static size_t character_width(const char *text) {
    size_t width = 1;
    while (((unsigned char)text[width] & 0xc0) == 0x80) ++width;
    return width;
}

static size_t character_count(const char *text) {
    size_t count = 0;
    for (; *text; text += character_width(text)) ++count;
    return count;
}

static const char *subtitle_separator(const char *title) {
    static const char *const separators[] = {"·", "：", ":", "——", " — ", " - "};
    const char *first = NULL;
    for (unsigned i = 0; i < sizeof(separators) / sizeof(*separators); ++i) {
        const char *position = strstr(title, separators[i]);
        if (position && position > title && (!first || position < first)) first = position;
    }
    return first;
}

static bool different_work(const char *suffix) {
    static const char *const markers[] = {
        "全集", "全套", "漫画", "有声", "解读", "导读", "同人", "番外", "前传", "续集",
    };
    for (unsigned i = 0; i < sizeof(markers) / sizeof(*markers); ++i)
        if (strstr(suffix, markers[i])) return true;
    return false;
}

static bool normalize_main_title(const char *title, char out[TITLE_CAP]) {
    const char *separator = subtitle_separator(title);
    if (!separator || (size_t)(separator - title) >= TITLE_CAP) return false;
    char head[TITLE_CAP];
    const size_t size = (size_t)(separator - title);
    memcpy(head, title, size);
    head[size] = 0;
    normalized(out, head);
    return *out != 0;
}

static bool one_title_edit(const char *left, const char *right) {
    const size_t n = character_count(left);
    const size_t m = character_count(right);
    if (n < 4 || m < 4 || n > m + 1 || m > n + 1) return false;
    while (*left && *right) {
        const size_t width = character_width(left);
        if (width != character_width(right) || memcmp(left, right, width)) break;
        left += width;
        right += width;
    }
    if (isdigit((unsigned char)*left) || isdigit((unsigned char)*right) ||
        strstr(left, "册") || strstr(right, "册") || strstr(left, "卷") || strstr(right, "卷"))
        return false;
    if (*left && n >= m) left += character_width(left);
    if (*right && m >= n) right += character_width(right);
    return !strcmp(left, right);
}

static unsigned match_score(const book_name_t *input, const book_name_t *candidate) {
    char left[TITLE_CAP], right[TITLE_CAP], a[TITLE_CAP], b[TITLE_CAP];
    normalized(left, input->title);
    normalized(right, candidate->title);
    normalized(a, input->author);
    normalized(b, candidate->author);
    if (!*left || !*right || (*a && strcmp(a, b))) return 0;
    const bool author = *a && *b;
    if (!strcmp(left, right)) return MATCH_EXACT + (author ? 10 : 0);
    const size_t title_len = strlen(right);
    // A delimiter-separated filename can validate both fields against one candidate.
    if (!*a && *b && strstr(input->title, " ") && !strncmp(left, right, title_len) &&
        !strcmp(left + title_len, b)) return MATCH_EXACT + 10;

    // A search for the series can return only individual titles. Accept an explicit
    // title separator and a short suffix, but keep adaptations and sequels separate.
    char base[TITLE_CAP];
    if (*b && normalize_main_title(candidate->title, base) && !strcmp(left, base) &&
        character_count(base) >= (author ? 3 : 4)) {
        const char *suffix = right + strlen(base);
        if (*suffix && character_count(suffix) <= 8 && !different_work(suffix))
            return MATCH_SERIES + (author ? 5 : 0);
    }
    if (!author) return 0;
    const size_t n = strlen(left), m = strlen(right);
    if (n != m && n >= 9 && m >= 9) {
        const char *longer = n > m ? left : right;
        const size_t common = n < m ? n : m;
        const char *tail = longer + common;
        if (!strncmp(left, right, common) && !isdigit((unsigned char)*tail) && !different_work(tail) &&
            !strstr(tail, "册") && !strstr(tail, "卷") && !strstr(tail, "第"))
            return MATCH_SUBTITLE;
    }
    return one_title_edit(left, right) ? MATCH_TYPO : 0;
}

static void matched_metadata(const book_name_t *input, book_name_t *result, unsigned score) {
    if (score >= MATCH_SERIES && score < MATCH_EXACT)
        strcpy(result->title, input->title);
}

static bool shorter_query(const char *title, char out[TITLE_CAP]) {
    const char *split = subtitle_separator(title);
    if (!split) split = strrchr(title, ' ');
    if (!split || (size_t)(split - title) >= TITLE_CAP) return false;
    memcpy(out, title, split - title);
    out[split - title] = 0;
    trim(out);
    return character_count(out) >= 3;
}

// Moon's Z-Library client documents this catalog format; these two mirrors were probed without a session.
static const char *const zlib_catalog_hosts[] = {"librella.fi", "bookabooki.fi"};
#define ZLIB_CATALOG_COUNT (sizeof(zlib_catalog_hosts) / sizeof(*zlib_catalog_hosts))

static bool cover_url_for_source(char out[640], const char *source, bool zlib) {
    const char *host = !strncmp(source, "https://", 8) ? source + 8 :
                       !strncmp(source, "http://", 7) ? source + 7 : NULL;
    const char *path = host ? strchr(host, '/') : NULL;
    if (!path) return false;
    static const char *const weread[] = {
        "cdn.weread.qq.com", "wfqqreader-1252317822.image.myqcloud.com",
        "wrco-40036.sh.gfp.tencent-cloud.com",
    };
    static const char *const zlib_images[] = {"s3proxy-alp2-covers.cdn-zlib.sk", "covers.zlibcdn2.com"};
    const char *const *hosts = zlib ? zlib_images : weread;
    const size_t count = zlib ? sizeof(zlib_images) / sizeof(*zlib_images) : sizeof(weread) / sizeof(*weread);
    const size_t host_length = (size_t)(path - host);
    if (zlib) {
        if (strncmp(path, "/covers", 7)) return false;
        const char *suffix = path + 7;
        while (isdigit((unsigned char)*suffix)) ++suffix;
        if (*suffix != '/' || strpbrk(path, "\\\r\n\t ")) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (host_length == strlen(hosts[i]) && !strncasecmp(host, hosts[i], host_length))
            return snprintf(out, 640, "https://%s", host) < 640;
    }
    return false;
}

static bool cover_url(char out[640], const char *source) {
    return cover_url_for_source(out, source, false) || cover_url_for_source(out, source, true);
}

typedef enum {
    COVER_FETCH_JSON, COVER_FETCH_IMAGE, COVER_FETCH_ZLIB_SEARCH,
    COVER_FETCH_ZLIB_PREVIEW, COVER_FETCH_ZLIB_IMAGE,
} cover_fetch_t;

typedef struct {
    uint8_t *data;
    size_t size;
    bool overflow, prefer_zlib_proxy, prefer_weserv;
    char location[640];
} http_response_t;

static esp_err_t collect(esp_http_client_event_t *event) {
    http_response_t *response = event->user_data;
    if (cancelled()) return ESP_FAIL;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "Location") && strlen(event->header_value) < sizeof(response->location))
        strcpy(response->location, event->header_value);
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    if (esp_http_client_get_status_code(event->client) != 200) return ESP_OK;
    if ((size_t)event->data_len > HTTP_LIMIT - response->size) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->data + response->size, event->data, event->data_len);
    response->size += event->data_len;
    return ESP_OK;
}

static bool save_pending(void) {
    portENTER_CRITICAL(&s_lock);
    const bool pending = s_status.saving;
    portEXIT_CRITICAL(&s_lock);
    return pending;
}

static esp_err_t perform_zlib_image(esp_http_client_handle_t client, int timeout_ms, bool preview) {
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        if (cancelled() || (preview && save_pending())) return ESP_ERR_NOT_FINISHED;
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        const esp_err_t error = esp_http_client_perform(client);
        if (error != ESP_ERR_HTTP_EAGAIN) return error;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static esp_err_t fetch_attempt(const char *url, http_response_t *response, cover_fetch_t kind,
                               const char *form, int *status, bool *retryable) {
    response->size = 0;
    response->overflow = false;
    response->location[0] = 0;
    const bool zlib_image = kind == COVER_FETCH_ZLIB_IMAGE || kind == COVER_FETCH_ZLIB_PREVIEW;
    const bool weserv = zlib_image && !strncmp(url, "https://images.weserv.nl/", sizeof("https://images.weserv.nl/") - 1);
    const bool photon = zlib_image && !strncmp(url, "https://i0.wp.com/", sizeof("https://i0.wp.com/") - 1);
    const int timeout_ms = weserv ? 12000 : photon ? 8000 : zlib_image ? 5000 :
        kind == COVER_FETCH_ZLIB_SEARCH ? 6000 : 12000;
    const esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
        .method = form ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = timeout_ms, .addr_type = HTTP_ADDR_TYPE_INET, .is_async = zlib_image,
        .disable_auto_redirect = true, .event_handler = collect, .user_data = response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    const bool image = kind == COVER_FETCH_IMAGE || zlib_image;
    esp_http_client_set_header(client, "Accept", image ? "image/jpeg,image/png,*/*;q=0.5" : "application/json");
    if (form) {
        esp_err_t result = esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        if (result == ESP_OK) result = esp_http_client_set_post_field(client, form, strlen(form));
        if (result != ESP_OK) { esp_http_client_cleanup(client); return result; }
    }
    esp_err_t result = zlib_image ? perform_zlib_image(client, timeout_ms, kind == COVER_FETCH_ZLIB_PREVIEW) :
        esp_http_client_perform(client);
    *status = esp_http_client_get_status_code(client);
    if (zlib_image && *status == 200 && response->size && !response->overflow) {
        const int64_t length = esp_http_client_get_content_length(client);
        const bool complete = length >= 0 ? (uint64_t)length == response->size :
            esp_http_client_is_complete_data_received(client);
        if (complete && result == ESP_ERR_TIMEOUT) result = ESP_OK;
        if (!complete && result == ESP_OK) result = ESP_ERR_INVALID_RESPONSE;
    }
    int tls_code = 0, verify_flags = 0;
    const esp_err_t tls_error = result == ESP_OK ? ESP_OK :
        esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &verify_flags);
    esp_http_client_cleanup(client);
    if (result == ESP_ERR_NOT_FINISHED || cancelled()) return ESP_ERR_NOT_FINISHED;
    if (response->overflow) return ESP_ERR_INVALID_SIZE;
    if (tls_error == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT || result == ESP_ERR_HTTP_EAGAIN ||
        result == ESP_ERR_HTTP_CONNECTING) result = ESP_ERR_TIMEOUT;
    *retryable = !verify_flags && (result == ESP_ERR_TIMEOUT || result == ESP_ERR_HTTP_CONNECT ||
        result == ESP_ERR_HTTP_CONNECTION_CLOSED || result == ESP_ERR_HTTP_FETCH_HEADER ||
        *status == 502 || *status == 503 || *status == 504);
    const char *request = weserv ? "Z-Library wsrv" : photon ? "Z-Library Jetpack" :
        zlib_image ? "Z-Library image" : image ? "image" : form ? "Z-Library search" : "search";
    if (result == ESP_OK && *status == 200)
        ESP_LOGI(TAG, "%s HTTP OK bytes=%u", request, (unsigned)response->size);
    else
        ESP_LOGW(TAG, "%s HTTP status=%d result=%s bytes=%u tls=0x%x code=%d verify=0x%x",
                 request, *status, esp_err_to_name(result), (unsigned)response->size,
                 (unsigned)tls_error, tls_code, (unsigned)verify_flags);
    return result;
}

static esp_err_t fetch(const char *url, http_response_t *response, cover_fetch_t kind) {
    char target[704];
    if (strlen(url) >= sizeof(target)) return ESP_ERR_INVALID_ARG;
    strcpy(target, url);
    for (unsigned hop = 0; hop < 4 && !cancelled(); ++hop) {
        int status = 0;
        bool retryable = false;
        esp_err_t result = fetch_attempt(target, response, kind, NULL, &status, &retryable);
        if (retryable && kind != COVER_FETCH_ZLIB_IMAGE && kind != COVER_FETCH_ZLIB_PREVIEW && !cancelled()) {
            vTaskDelay(pdMS_TO_TICKS(300));
            if (cancelled()) return ESP_ERR_NOT_FINISHED;
            result = fetch_attempt(target, response, kind, NULL, &status, &retryable);
        }
        if (result != ESP_OK) return result;
        if (status == 200) return response->size ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
        if (kind != COVER_FETCH_IMAGE || status < 300 || status >= 400 || !cover_url(target, response->location))
            return ESP_ERR_INVALID_RESPONSE;
    }
    return cancelled() ? ESP_ERR_NOT_FINISHED : ESP_ERR_INVALID_STATE;
}

typedef struct {
    book_name_t name;
    char url[640], saved_title[TITLE_CAP];
    unsigned score;
    bool from_zlib;
} match_t;
typedef struct {unsigned count; match_t entries[BOOK_WEREAD_OPTIONS];} match_list_t;

typedef struct {
    book_weread_cover_row_t result;
    match_t *options;
    uint64_t file_size;
    int64_t modified;
} cover_row_t;

typedef struct {
    book_weread_path_t *paths;
    cover_row_t *rows;
    unsigned count, focus, preview_row, request_row, request_option;
    uint8_t preview_checked, preview_ready;
    unsigned zlib_host, full_cover_row;
    uint8_t *full_covers[BOOK_WEREAD_OPTIONS];
    uint8_t previews[BOOK_WEREAD_OPTIONS][BOOK_WEREAD_PREVIEW_BYTES];
} batch_t;

static batch_t *s_batch;

static void offer_match(match_list_t *matches, const book_name_t *name, unsigned score,
                        const char *source, bool from_zlib) {
    char url[640];
    if (!score || !cover_url_for_source(url, source, from_zlib)) return;
    unsigned position = 0;
    while (position < matches->count && matches->entries[position].score >= score) ++position;
    if (position == BOOK_WEREAD_OPTIONS) return;
    for (unsigned i = 0; i < matches->count; ++i)
        if (!strcmp(matches->entries[i].url, url)) return;
    const unsigned kept = matches->count < BOOK_WEREAD_OPTIONS ? matches->count : BOOK_WEREAD_OPTIONS - 1;
    memmove(&matches->entries[position + 1], &matches->entries[position],
            (kept - position) * sizeof(matches->entries[0]));
    matches->entries[position] = (match_t){.name = *name, .score = score, .from_zlib = from_zlib};
    strcpy(matches->entries[position].url, url);
    matches->count = kept + 1;
}

static void rank(const cJSON *node, const book_name_t *input, match_list_t *matches, unsigned depth) {
    if (depth > 9) return;
    if (cJSON_IsObject(node)) {
        const cJSON *title = cJSON_GetObjectItemCaseSensitive(node, "title");
        const cJSON *author = cJSON_GetObjectItemCaseSensitive(node, "author");
        const cJSON *cover = cJSON_GetObjectItemCaseSensitive(node, "cover");
        if (cJSON_GetObjectItemCaseSensitive(node, "bookId") &&
            cJSON_IsString(title) && cJSON_IsString(author) && cJSON_IsString(cover) &&
            strlen(title->valuestring) < TITLE_CAP && strlen(author->valuestring) < AUTHOR_CAP) {
            book_name_t candidate = {0};
            strcpy(candidate.title, title->valuestring);
            strcpy(candidate.author, author->valuestring);
            strip_promotion(candidate.title);
            book_author_clean_display(candidate.author, sizeof(candidate.author));
            offer_match(matches, &candidate, match_score(input, &candidate), cover->valuestring, false);
        }
    }
    for (const cJSON *child = node->child; child; child = child->next)
        rank(child, input, matches, depth + 1);
}

static bool append_keyword(char *query, size_t capacity, const char *keyword) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = strlen(query);
    for (const unsigned char *p = (const unsigned char *)keyword; *p; ++p) {
        if (used + 4 >= capacity) return false;
        if (isalnum(*p) && *p < 128) query[used++] = (char)*p;
        else {
            query[used++] = '%';
            query[used++] = hex[*p >> 4];
            query[used++] = hex[*p & 15];
        }
    }
    query[used] = 0;
    return true;
}

static esp_err_t search(const char *keyword, const book_name_t *input,
                        http_response_t *response, match_list_t *matches) {
    char query[704] = "https://weread.qq.com/api/store/search?keyword=";
    if (!append_keyword(query, sizeof(query), keyword)) return ESP_ERR_INVALID_SIZE;
    esp_err_t result = fetch(query, response, COVER_FETCH_JSON);
    if (result != ESP_OK) return result;
    cJSON *document = cJSON_ParseWithLength((const char *)response->data, response->size);
    if (!document) return ESP_ERR_INVALID_RESPONSE;
    rank(document, input, matches, 0);
    cJSON_Delete(document);
    return matches->count ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t rank_zlib(const cJSON *document, const book_name_t *input, match_list_t *matches) {
    const cJSON *success = cJSON_GetObjectItemCaseSensitive(document, "success");
    if (!cJSON_IsObject(document) || !cJSON_IsNumber(success) || success->valueint != 1)
        return ESP_ERR_INVALID_RESPONSE;
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive(document, "books");
    if (!cJSON_IsArray(rows) || !rows->child) {
        const cJSON *exact = cJSON_GetObjectItemCaseSensitive(document, "exactMatch");
        const cJSON *exact_rows = cJSON_GetObjectItemCaseSensitive(exact, "books");
        if (cJSON_IsArray(exact_rows)) rows = exact_rows;
    }
    if (!cJSON_IsArray(rows)) return ESP_ERR_INVALID_RESPONSE;
    const size_t title_length = strlen(input->title);
    const bool multibyte_title = character_count(input->title) < title_length;
    const cJSON *row;
    cJSON_ArrayForEach(row, rows) {
        const cJSON *title = cJSON_GetObjectItemCaseSensitive(row, "title");
        const cJSON *author = cJSON_GetObjectItemCaseSensitive(row, "author");
        const cJSON *cover = cJSON_GetObjectItemCaseSensitive(row, "cover");
        if (!cJSON_IsString(author)) author = cJSON_GetObjectItemCaseSensitive(row, "authors");
        if (!cJSON_IsString(title) || strlen(title->valuestring) >= TITLE_CAP || !cJSON_IsString(cover)) continue;
        book_name_t candidate = {0};
        strcpy(candidate.title, title->valuestring);
        if (cJSON_IsString(author) && strlen(author->valuestring) < AUTHOR_CAP)
            strcpy(candidate.author, author->valuestring);
        strip_promotion(candidate.title);
        char *translated = strstr(candidate.author, "著，");
        if (translated && strstr(translated, "译")) *translated = 0;
        book_author_clean_display(candidate.author, sizeof(candidate.author));
        unsigned score = match_score(input, &candidate);
        // Match a bilingual catalog title without dropping its original title on display.
        if (multibyte_title && title_length >= 6 && score < MATCH_SERIES &&
            (!input->author[0] || score == MATCH_SUBTITLE) &&
            !strncmp(candidate.title, input->title, title_length) && candidate.title[title_length] == ' ') {
            const char *suffix = candidate.title + title_length + 1;
            const unsigned char first = *suffix;
            if (first < 128 && isalpha(first) && strchr(suffix, ' ') && !different_work(suffix) &&
                !strstr(suffix, "册") && !strstr(suffix, "卷")) score = MATCH_SERIES;
        }
        offer_match(matches, &candidate, score, cover->valuestring, true);
    }
    return matches->count ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t search_zlib(const book_name_t *input, http_response_t *response,
                              match_list_t *matches, unsigned *next_host) {
    static const char params[] = "&page=1&limit=12";
    char form[704] = "message=";
    if (!append_keyword(form, sizeof(form) - sizeof(params), input->title)) return ESP_ERR_INVALID_SIZE;
    strcat(form, params);
    while (*next_host < ZLIB_CATALOG_COUNT && !cancelled()) {
        char url[96];
        snprintf(url, sizeof(url), "https://%s/eapi/book/search", zlib_catalog_hosts[*next_host]);
        int status = 0;
        bool retryable = false;
        esp_err_t result = fetch_attempt(url, response, COVER_FETCH_ZLIB_SEARCH, form, &status, &retryable);
        if (result == ESP_OK) {
            result = ESP_ERR_INVALID_RESPONSE;
            if (status == 200 && response->size) {
                cJSON *document = cJSON_ParseWithLength((const char *)response->data, response->size);
                result = rank_zlib(document, input, matches);
                cJSON_Delete(document);
            }
        }
        if (result == ESP_ERR_NOT_FINISHED || result == ESP_OK || result == ESP_ERR_NOT_FOUND ||
            result == ESP_ERR_NO_MEM) return result;
        ++*next_host;
    }
    return cancelled() ? ESP_ERR_NOT_FINISHED : ESP_ERR_INVALID_STATE;
}

static esp_err_t collect_matches(const char *path, http_response_t *response,
                                 match_list_t *matches, unsigned *zlib_host) {
    book_name_t input = {0};
    matches->count = 0;
    if (!extract_name(path, &input)) return ESP_ERR_NOT_FOUND;
    esp_err_t result = search(input.title, &input, response, matches);
    if ((result == ESP_ERR_NOT_FOUND || result == ESP_OK) &&
        (!matches->count || matches->entries[0].score < MATCH_EXACT) && !cancelled()) {
        char keyword[TITLE_CAP];
        if (shorter_query(input.title, keyword)) {
            const esp_err_t fallback = search(keyword, &input, response, matches);
            result = matches->count ? ESP_OK : fallback;
        }
    }
    if (!matches->count && result != ESP_ERR_NO_MEM && !cancelled()) {
        const esp_err_t fallback = search_zlib(&input, response, matches, zlib_host);
        if (fallback == ESP_OK || fallback == ESP_ERR_NOT_FINISHED || fallback == ESP_ERR_NO_MEM)
            result = fallback;
    }
    if (result == ESP_OK) {
        for (unsigned i = 0; i < matches->count; ++i) {
            match_t *candidate = &matches->entries[i];
            book_name_t metadata = candidate->name;
            matched_metadata(&input, &metadata, candidate->score);
            strcpy(candidate->saved_title, metadata.title);
        }
    }
    return result;
}

static bool zlib_proxy_url(char out[704], const char *url, bool weserv) {
    if (!weserv) {
        const int length = snprintf(out, 704, "https://i0.wp.com/%s?ssl=1&w=200&quality=55", url + 8);
        return length > 0 && length < 704;
    }
    static const char params[] = "&w=200&h=288&fit=inside&output=jpg&q=55";
    strcpy(out, "https://images.weserv.nl/?url=");
    if (!append_keyword(out, 704 - sizeof(params), url)) return false;
    strcat(out, params);
    return true;
}

static esp_err_t fetch_zlib_image(const char *url, http_response_t *response, bool preview) {
    const cover_fetch_t kind = preview ? COVER_FETCH_ZLIB_PREVIEW : COVER_FETCH_ZLIB_IMAGE;
    const bool public_url = !strncmp(url, "https://", 8) && !strpbrk(url, "?#");
    if (!response->prefer_zlib_proxy || !public_url) {
        const esp_err_t result = fetch(url, response, kind);
        if (!public_url || result == ESP_OK || result == ESP_ERR_NOT_FINISHED || result == ESP_ERR_NO_MEM)
            return result;
        if (result == ESP_ERR_TIMEOUT || result == ESP_ERR_HTTP_CONNECT)
            response->prefer_zlib_proxy = true;
    }
    // Resize only known public cover addresses. No local title, path or user credentials are sent here.
    char *proxy = heap_caps_malloc(704, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!proxy) return ESP_ERR_NO_MEM;
    esp_err_t result = ESP_ERR_INVALID_SIZE;
    bool weserv = response->prefer_weserv;
    for (unsigned attempt = 0; attempt < 2; ++attempt, weserv = !weserv) {
        if (!zlib_proxy_url(proxy, url, weserv)) continue;
        result = fetch(proxy, response, kind);
        if (result == ESP_OK) {
            response->prefer_zlib_proxy = true;
            response->prefer_weserv = weserv;
            break;
        }
        if (result == ESP_ERR_NOT_FINISHED || result == ESP_ERR_NO_MEM) break;
    }
    free(proxy);
    return result;
}

static esp_err_t download_cover(const match_t *match, http_response_t *response,
                                unsigned width, unsigned height, uint8_t *pixels, bool preview) {
    const esp_err_t result = match->from_zlib ? fetch_zlib_image(match->url, response, preview) :
        fetch(match->url, response, COVER_FETCH_IMAGE);
    if (result != ESP_OK) return result;
    const bool png = response->size >= 8 && !memcmp(response->data, "\x89PNG\r\n\x1a\n", 8);
    const bool jpeg = response->size >= 3 && !memcmp(response->data, "\xff\xd8\xff", 3);
    unsigned image_width = 0, image_height = 0;
    if (cancelled()) return ESP_ERR_NOT_FINISHED;
    const bool webp = match->from_zlib && book_webp_is(response->data, response->size);
    if ((!png && !jpeg && !webp) ||
        !book_image_dimensions(response->data, response->size, png, &image_width, &image_height) ||
        image_width < 32 || image_height < 40 || (uint64_t)image_width * image_height > 3000000 ||
        !book_image_grayscale(response->data, response->size, png, width, height, pixels))
        return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

static bool cover_network_ready(void) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    return status.network_ready && status.mode == READ_PICO_TRANSFER_MODE_STA &&
           (!KIIKO_BOARD_METALIO47 || status.cover_lookup);
}

static esp_err_t save_downloaded_cover(const char *path, const struct stat *file, const match_t *match,
                                       const uint8_t *pixels) {
    book_name_t metadata = match->name;
    strcpy(metadata.title, match->saved_title);
    return save_record(path, file, &metadata, pixels) ? ESP_OK : ESP_FAIL;
}

static esp_err_t download_and_save(const char *path, const struct stat *file, const match_t *match,
                                   http_response_t *response, uint8_t *pixels) {
    if (!cover_network_ready()) return ESP_ERR_INVALID_STATE;
    const esp_err_t error = download_cover(match, response, BOOK_COVER_W, BOOK_COVER_H, pixels, false);
    if (error != ESP_OK) return error;
    return save_downloaded_cover(path, file, match, pixels);
}

static esp_err_t scan_cover(batch_t *batch, unsigned index, http_response_t *response,
                           uint8_t *pixels, match_list_t *matches) {
    cover_row_t *row = &batch->rows[index];
    const char *path = batch->paths[index];
    portENTER_CRITICAL(&s_lock);
    row->result.state = BOOK_WEREAD_CHECKING;
    portEXIT_CRITICAL(&s_lock);

    struct stat file;
    esp_err_t error = ESP_ERR_NOT_FOUND;
    book_weread_row_state_t state = BOOK_WEREAD_NOT_FOUND;
    match_t *options = NULL;
    if (!read_pico_sd_io_ready() || !cover_network_ready()) {
        error = ESP_ERR_INVALID_STATE;
    } else if (identity(path, &file)) {
        if (book_weread_cover_metadata(path, NULL, 0, NULL, 0)) {
            portENTER_CRITICAL(&s_lock);
            row->result.state = BOOK_WEREAD_SAVED;
            ++s_status.done;
            ++s_status.saved;
            ++s_status.changes;
            portEXIT_CRITICAL(&s_lock);
            return ESP_OK;
        }
        error = collect_matches(path, response, matches, &batch->zlib_host);
        if (error == ESP_OK && matches->count == 1)
            error = download_and_save(path, &file, &matches->entries[0], response, pixels);
        else if (error == ESP_OK) {
            options = heap_caps_malloc(matches->count * sizeof(*options), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (options) memcpy(options, matches->entries, matches->count * sizeof(*options));
            else error = ESP_ERR_NO_MEM;
        }
    }
    if (error == ESP_OK) state = options ? BOOK_WEREAD_MULTIPLE : BOOK_WEREAD_SAVED;
    else if (error != ESP_ERR_NOT_FOUND) state = BOOK_WEREAD_FAILED;

    portENTER_CRITICAL(&s_lock);
    row->options = options;
    if (options) { row->file_size = file.st_size; row->modified = file.st_mtime; }
    row->result = (book_weread_cover_row_t){state, options ? matches->count : 0, error};
    ++s_status.done;
    s_status.zlib_unavailable = batch->zlib_host >= ZLIB_CATALOG_COUNT;
    if (state == BOOK_WEREAD_SAVED) { ++s_status.saved; ++s_revision; }
    if (state == BOOK_WEREAD_MULTIPLE) ++s_status.multiple;
    ++s_status.changes;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "item=%u state=%d options=%u result=%s stack=%u", index + 1, state,
             options ? matches->count : 0, esp_err_to_name(error), (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return error;
}

// The output is smaller than the source, so shrinking in place leaves unread pixels intact.
static void preview_from_saved_cover(uint8_t *pixels) {
    const book_crop_t crop = book_cover_crop(BOOK_COVER_W, BOOK_COVER_H,
                                             BOOK_WEREAD_PREVIEW_W, BOOK_WEREAD_PREVIEW_H);
    for (unsigned y = 0; y < BOOK_WEREAD_PREVIEW_H; ++y) {
        const unsigned sy = crop.y + y * crop.height / BOOK_WEREAD_PREVIEW_H;
        for (unsigned x = 0; x < BOOK_WEREAD_PREVIEW_W; ++x) {
            const unsigned sx = crop.x + x * crop.width / BOOK_WEREAD_PREVIEW_W;
            pixels[y * BOOK_WEREAD_PREVIEW_W + x] = pixels[sy * BOOK_COVER_W + sx];
        }
    }
}

static void publish_preview_locked(batch_t *batch, unsigned row, unsigned option, const uint8_t *pixels) {
    if (s_cancel || batch->preview_row != row) return;
    if (pixels) {
        memcpy(batch->previews[option], pixels, BOOK_WEREAD_PREVIEW_BYTES);
        batch->preview_ready |= 1u << option;
    }
    batch->preview_checked |= 1u << option;
    ++s_status.changes;
}

static bool save_selected(batch_t *batch, http_response_t *response, uint8_t *pixels) {
    portENTER_CRITICAL(&s_lock);
    const unsigned index = batch->request_row;
    const unsigned option = batch->request_option;
    portEXIT_CRITICAL(&s_lock);
    if (index == BOOK_WEREAD_NO_ROW) return false;
    cover_row_t *row = &batch->rows[index];
    const char *path = batch->paths[index];
    struct stat file;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (read_pico_sd_io_ready() && identity(path, &file) &&
        (uint64_t)file.st_size == row->file_size && file.st_mtime == row->modified) {
        const match_t *match = &row->options[option];
        const uint8_t *cached = batch->full_cover_row == index ? batch->full_covers[option] : NULL;
        if (cached) {
            error = save_downloaded_cover(path, &file, match, cached);
            if (error == ESP_OK) memcpy(pixels, cached, COVER_BYTES);
        } else error = download_and_save(path, &file, match, response, pixels);
    }
    ESP_LOGI(TAG, "save cover item=%u choice=%u result=%s", index + 1, option + 1, esp_err_to_name(error));
    if (error == ESP_OK) preview_from_saved_cover(pixels);
    portENTER_CRITICAL(&s_lock);
    row->result.error = error;
    row->result.state = error == ESP_OK ? BOOK_WEREAD_SAVED : BOOK_WEREAD_MULTIPLE;
    if (error == ESP_OK) {
        ++s_status.saved;
        --s_status.multiple;
        ++s_revision;
        publish_preview_locked(batch, index, option, pixels);
    }
    batch->request_row = BOOK_WEREAD_NO_ROW;
    s_status.saving = false;
    ++s_status.changes;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

// The worker exclusively owns these decoded images, including when the UI changes detail pages.
static void reset_full_covers(batch_t *batch, unsigned row) {
    for (unsigned i = 0; i < BOOK_WEREAD_OPTIONS; ++i) {
        free(batch->full_covers[i]);
        batch->full_covers[i] = NULL;
    }
    batch->full_cover_row = row;
}

static bool load_preview(batch_t *batch, http_response_t *response, uint8_t *pixels) {
    portENTER_CRITICAL(&s_lock);
    const unsigned index = batch->focus;
    unsigned option = 0;
    unsigned count = 0;
    if (index < batch->count) {
        const book_weread_cover_row_t row = batch->rows[index].result;
        if (row.state == BOOK_WEREAD_MULTIPLE || row.state == BOOK_WEREAD_SAVED) count = row.options;
    }
    while (option < count && (batch->preview_checked & (1u << option))) ++option;
    portEXIT_CRITICAL(&s_lock);
    if (option >= count) return false;
    const match_t *match = &batch->rows[index].options[option];
    if (batch->full_cover_row != index) reset_full_covers(batch, index);
    const esp_err_t result = cover_network_ready() ?
        download_cover(match, response, match->from_zlib ? BOOK_COVER_W : BOOK_WEREAD_PREVIEW_W,
                       match->from_zlib ? BOOK_COVER_H : BOOK_WEREAD_PREVIEW_H, pixels, true) :
        ESP_ERR_INVALID_STATE;
    if (result == ESP_ERR_NOT_FINISHED) return true;
    if (result == ESP_OK && match->from_zlib) {
        uint8_t *cached = heap_caps_malloc(COVER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (cached) {
            memcpy(cached, pixels, COVER_BYTES);
            free(batch->full_covers[option]);
            batch->full_covers[option] = cached;
        }
        preview_from_saved_cover(pixels);
    }
    ESP_LOGI(TAG, "preview cover item=%u choice=%u result=%s", index + 1, option + 1, esp_err_to_name(result));
    portENTER_CRITICAL(&s_lock);
    publish_preview_locked(batch, index, option, result == ESP_OK ? pixels : NULL);
    portEXIT_CRITICAL(&s_lock);
    return true;
}

static void worker(void *argument) {
    batch_t *batch = argument;
    http_response_t response = {.data = heap_caps_malloc(HTTP_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)};
    uint8_t *pixels = heap_caps_malloc(COVER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    match_list_t *matches = heap_caps_malloc(sizeof(*matches), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t error = response.data && pixels && matches ? ESP_OK : ESP_ERR_NO_MEM;
    unsigned transport_failures = 0;
    unsigned index = 0;
    bool has_choices = false;
    ESP_LOGI(TAG, "start count=%u internal_free=%u stack=%u",
             batch->count, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    while (!cancelled() && (error == ESP_OK || has_choices)) {
        if (save_selected(batch, &response, pixels)) continue;
        if (load_preview(batch, &response, pixels)) continue;
        if (!cancelled() && index < batch->count && error == ESP_OK) {
            const esp_err_t result = scan_cover(batch, index, &response, pixels, matches);
            if (batch->rows[index++].options) has_choices = true;
            transport_failures = result == ESP_OK || result == ESP_ERR_NOT_FOUND ? 0 : transport_failures + 1;
            if (result == ESP_ERR_INVALID_STATE || result == ESP_ERR_NO_MEM || transport_failures >= 3) error = result;
            if (index == batch->count || error != ESP_OK) {
                portENTER_CRITICAL(&s_lock);
                s_status.running = false;
                s_status.error = error;
                ++s_status.changes;
                portEXIT_CRITICAL(&s_lock);
                ESP_LOGI(TAG, "scan finished result=%s", esp_err_to_name(error));
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    portENTER_CRITICAL(&s_lock);
    if (s_cancel) s_status.error = ESP_ERR_NOT_FINISHED;
    else if (error != ESP_OK) s_status.error = error;
    s_status.active = s_status.running = s_status.saving = false;
    s_status.multiple = 0;
    ++s_status.changes;
    s_batch = NULL;
    portEXIT_CRITICAL(&s_lock);
    reset_full_covers(batch, BOOK_WEREAD_NO_ROW);
    free(matches);
    free(pixels);
    free(response.data);
    for (unsigned i = 0; i < batch->count; ++i) free(batch->rows[i].options);
    free(batch->rows);
    free(batch->paths);
    free(batch);
    vTaskDelete(NULL);
}

esp_err_t book_weread_cover_start(book_weread_path_t *paths, unsigned count) {
    if (!paths || !count || count > SIZE_MAX / sizeof(cover_row_t)) return ESP_ERR_INVALID_ARG;
    if (!read_pico_sd_io_ready() || !cover_network_ready()) return ESP_ERR_INVALID_STATE;
    batch_t *batch = heap_caps_calloc(1, sizeof(*batch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!batch) return ESP_ERR_NO_MEM;
    batch->rows = heap_caps_calloc(count, sizeof(cover_row_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!batch->rows) { free(batch); return ESP_ERR_NO_MEM; }
    batch->paths = paths;
    batch->count = count;
    batch->focus = batch->preview_row = batch->full_cover_row = batch->request_row = BOOK_WEREAD_NO_ROW;
    portENTER_CRITICAL(&s_lock);
    const bool busy = s_batch != NULL;
    if (!busy) {
        s_batch = batch;
        s_status = (book_weread_cover_status_t){.active = true, .running = true, .total = count, .changes = 1};
        s_cancel = false;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!busy && xTaskCreate(worker, "weread_cover", COVER_WORKER_STACK, batch, 1, NULL) == pdPASS) return ESP_OK;
    if (!busy) {
        portENTER_CRITICAL(&s_lock);
        s_batch = NULL;
        s_status.active = s_status.running = false;
        portEXIT_CRITICAL(&s_lock);
    }
    free(batch->rows);
    free(batch);
    return busy ? ESP_ERR_INVALID_STATE : ESP_ERR_NO_MEM;
}

void book_weread_cover_cancel(void) {
    portENTER_CRITICAL(&s_lock);
    if (s_batch) s_cancel = true;
    portEXIT_CRITICAL(&s_lock);
}

book_weread_cover_status_t book_weread_cover_status(void) {
    portENTER_CRITICAL(&s_lock);
    const book_weread_cover_status_t status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}

unsigned book_weread_cover_revision(void) {
    portENTER_CRITICAL(&s_lock);
    const unsigned revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    return revision;
}


book_weread_cover_row_t book_weread_cover_row(unsigned row) {
    portENTER_CRITICAL(&s_lock);
    const book_weread_cover_row_t result = s_batch && row < s_batch->count ?
        s_batch->rows[row].result : (book_weread_cover_row_t){0};
    portEXIT_CRITICAL(&s_lock);
    return result;
}

void book_weread_cover_focus(unsigned row) {
    portENTER_CRITICAL(&s_lock);
    if (s_batch) {
        s_batch->focus = row;
        if (row < s_batch->count && s_batch->preview_row != row) {
            s_batch->preview_row = row;
            s_batch->preview_checked = s_batch->preview_ready = 0;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

bool book_weread_cover_option(unsigned row, unsigned option, book_weread_cover_option_t *out,
                              uint8_t preview[BOOK_WEREAD_PREVIEW_BYTES]) {
    portENTER_CRITICAL(&s_lock);
    const bool available = out && s_batch && row < s_batch->count && option < s_batch->rows[row].result.options;
    if (available) {
        const match_t *match = &s_batch->rows[row].options[option];
        strcpy(out->title, match->name.title);
        strcpy(out->author, match->name.author);
        out->from_zlib = match->from_zlib;
        out->preview_checked = s_batch->focus == row && (s_batch->preview_checked & (1u << option));
        out->preview_ready = s_batch->focus == row && (s_batch->preview_ready & (1u << option));
        if (out->preview_ready && preview) memcpy(preview, s_batch->previews[option], BOOK_WEREAD_PREVIEW_BYTES);
    }
    portEXIT_CRITICAL(&s_lock);
    return available;
}

bool book_weread_cover_choose(unsigned row, unsigned option) {
    portENTER_CRITICAL(&s_lock);
    const bool queued = s_batch && !s_cancel && !s_status.saving && row < s_batch->count &&
        s_batch->rows[row].result.state == BOOK_WEREAD_MULTIPLE &&
        option < s_batch->rows[row].result.options;
    if (queued) {
        s_batch->rows[row].result = (book_weread_cover_row_t){
            BOOK_WEREAD_SAVING, s_batch->rows[row].result.options, ESP_OK};
        s_batch->request_row = row;
        s_batch->request_option = option;
        s_status.saving = true;
        ++s_status.changes;
    }
    portEXIT_CRITICAL(&s_lock);
    return queued;
}

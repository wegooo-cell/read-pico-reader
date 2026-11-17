#!/usr/bin/env python3
"""Exercise the firmware's filename matching and TF record transaction."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/book/book_weread_cover.c"

parser = argparse.ArgumentParser()
parser.add_argument("--cjson", type=Path)
parser.add_argument("--fixture", nargs=2, metavar=("JSON", "BOOK_FILENAME"), action="append", default=[])
args = parser.parse_args()
cjson = args.cjson or next(
    (path for path in (
        ROOT / "managed_components/espressif__cjson/cJSON",
        ROOT.parent / "build/pico/source/managed_components/espressif__cjson/cJSON",
    ) if (path / "cJSON.c").is_file()),
    None,
)
assert cjson is not None, "Pass --cjson path/to/cJSON"


with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    root = temp / "tf"
    root.mkdir()
    unit = r'''
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_cover.h"
#include "book_weread_cover.h"
#include "book_title.h"
#include "gbk.h"
#include "cJSON.h"
#define COVER_MAGIC UINT32_C(0x57435231)
#define COVER_BYTES (BOOK_COVER_W * BOOK_COVER_H)
#define TITLE_CAP 192
#define AUTHOR_CAP 128
enum { MATCH_TYPO = 50, MATCH_SUBTITLE = 60, MATCH_SERIES = 70, MATCH_EXACT = 90 };
enum { ESP_ERR_INVALID_RESPONSE=10, ESP_ERR_NOT_FOUND=11, ESP_ERR_NO_MEM=12 };
typedef enum { COVER_FETCH_JSON, COVER_FETCH_IMAGE, COVER_FETCH_ZLIB_SEARCH } cover_fetch_t;
typedef struct { char title[TITLE_CAP], author[AUTHOR_CAP]; } book_name_t;
typedef struct {
    uint64_t size; int64_t modified; uint32_t magic;
    book_weread_path_t source; book_name_t name;
} cover_record_t;
static bool tf_ready = true, cancel, custom;
static bool cancelled(void) { return cancel; }
static bool read_pico_sd_io_ready(void) { return tf_ready; }
bool book_title_get(const char *path, char *title, size_t cap) {
    (void)path;
    if (!custom) return false;
    return snprintf(title, cap, "自定义") > 0;
}
'''
    unit += f'#define COVER_DIRECTORY "{root}/.kiikoread/covers"\n'
    # Keep the actual local parser, while replacing the NVS-only display wrapper.
    title = (ROOT / "main/book/book_title.c").read_text()
    unit += title[title.index("typedef struct { const char *p;"):title.index("bool book_title_from_path")]
    unit += function("book_cover_crop", ROOT / "main/book/book_cover.c")
    for name in (
        "book_weread_cover_is_txt", "identity", "record_path", "open_record",
        "book_weread_cover_metadata", "book_weread_cover_stamp", "book_weread_cover_read", "save_record",
        "noise", "trim", "strip_promotion", "strip_filename_tags", "enclosed", "bounded_copy", "credit",
        "header_name", "extract_name", "normalized", "character_width", "character_count",
        "subtitle_separator", "different_work", "normalize_main_title", "one_title_edit",
        "match_score", "matched_metadata", "shorter_query", "cover_url_for_source", "cover_url",
    ):
        unit += "\n" + function(name, SOURCE).replace("/sdcard/.kiikoread", f"{root}/.kiikoread") + "\n"
    source = SOURCE.read_text()
    start = source.index("typedef struct {\n    book_name_t name;\n    char url[640]")
    end = source.index("static void rank(", start)
    unit += "\n" + source[start:end] + "\n"
    for name in ("append_keyword", "rank_zlib"):
        unit += "\n" + function(name, SOURCE) + "\n"
    unit += """
static book_weread_cover_status_t s_status;
static bool s_cancel;
#define portENTER_CRITICAL(lock) ((void)0)
#define portEXIT_CRITICAL(lock) ((void)0)
"""
    for name in ("book_weread_cover_row", "book_weread_cover_focus",
                 "book_weread_cover_option", "book_weread_cover_choose"):
        unit += "\n" + function(name, SOURCE) + "\n"
    unit += r"""
static unsigned s_revision, selected_downloads, preview_downloads;
typedef struct {uint8_t *data; size_t size; bool overflow;} http_response_t;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(size, flags) malloc(size)
#define ESP_LOGI(tag, ...) ((void)0)
static bool abort_preview;
static unsigned catalog_queries, weread_queries;
static bool catalog_fails, weread_matches;
static const char *catalog_fixture;
static esp_err_t fetch_attempt(const char *url, http_response_t *response, cover_fetch_t kind,
                               const char *form, int *status, bool *retryable) {
    assert(kind == COVER_FETCH_ZLIB_SEARCH && strstr(url, "/eapi/book/search"));
    assert(!strcmp(form, "message=%E5%8D%81%E6%97%A5%E7%BB%88%E7%84%89&page=1&limit=12"));
    ++catalog_queries;
    *retryable = false;
    if (catalog_fails || strstr(url, "librella.fi")) {
        *status = 503;
        return ESP_OK;
    }
    *status = 200;
    response->size = strlen(catalog_fixture);
    memcpy(response->data, catalog_fixture, response->size);
    return ESP_OK;
}
static esp_err_t search(const char *keyword, const book_name_t *input, http_response_t *response,
                        match_list_t *matches) {
    assert(*keyword);
    (void)response;
    ++weread_queries;
    if (!weread_matches) return ESP_ERR_NOT_FOUND;
    offer_match(matches, input, MATCH_EXACT, "https://cdn.weread.qq.com/other.jpg", false);
    return ESP_OK;
}
static esp_err_t download_and_save(const char *path, const struct stat *file, const match_t *match,
                                   http_response_t *response, uint8_t *pixels) {
    (void)response;
    assert(!strcmp(match->url, "https://cdn.weread.qq.com/other.jpg"));
    ++selected_downloads;
    for (unsigned y = 0; y < BOOK_COVER_H; ++y)
        memset(pixels + y * BOOK_COVER_W, y + 13, BOOK_COVER_W);
    book_name_t metadata = match->name;
    strcpy(metadata.title, match->saved_title);
    return save_record(path, file, &metadata, pixels) ? ESP_OK : ESP_FAIL;
}
static esp_err_t save_downloaded_cover(const char *path, const struct stat *file, const match_t *match,
                                       const uint8_t *pixels) {
    return save_record(path, file, &match->name, pixels) ? ESP_OK : ESP_FAIL;
}
static bool cover_network_ready(void) { return true; }
static esp_err_t download_cover(const match_t *match, http_response_t *response,
                                unsigned width, unsigned height, uint8_t *pixels, bool preview) {
    (void)response;
    assert(preview);
    assert(width == (match->from_zlib ? BOOK_COVER_W : BOOK_WEREAD_PREVIEW_W) &&
           height == (match->from_zlib ? BOOK_COVER_H : BOOK_WEREAD_PREVIEW_H));
    ++preview_downloads;
    if (abort_preview) return ESP_ERR_NOT_FINISHED;
    if (strstr(match->url, "subtitle.jpg")) return ESP_FAIL;
    memset(pixels, 91, width * height);
    return ESP_OK;
}
"""
    unit += "\n" + source[source.index("static const char *const zlib_catalog_hosts"):
                                  source.index("static bool cover_url_for_source(")]
    for name in ("search_zlib", "collect_matches", "preview_from_saved_cover",
                 "publish_preview_locked", "save_selected", "reset_full_covers", "load_preview"):
        unit += "\n" + function(name, SOURCE) + "\n"
    unit += f'\n#define TEST_BOOK "{root}/000017.TXT"\n'
    unit += f'#define CHOICE_BOOK "{root}/十日终焉.TXT"\n'
    unit += r'''
int main(int argc, char **argv) {
    book_name_t input = {0};
    assert(extract_name("001_《长安的荔枝》_马伯庸_完结.txt", &input));
    assert(!strcmp(input.title, "长安的荔枝") && !strcmp(input.author, "马伯庸"));
    book_name_t candidate = {"长安的荔枝（同名影视原著）", "马伯庸"};
    strip_promotion(candidate.title);
    assert(match_score(&input, &candidate) == 100);
    strcpy(candidate.author, "其他作者");
    assert(match_score(&input, &candidate) == 0);

    input = (book_name_t){0};
    assert(extract_name("【精校版】《重生之女将星》 作者：千山茶客 [ZLib].txt", &input));
    assert(!strcmp(input.title, "重生之女将星") && !strcmp(input.author, "千山茶客"));
    candidate = (book_name_t){"重生之女将星（周也、丞磊主演电视剧《锦月如歌》原著）", "千山茶客"};
    strip_promotion(candidate.title);
    assert(match_score(&input, &candidate) == 100);

    input = (book_name_t){0};
    assert(extract_name("长安的荔枝_马伯庸_完结.txt", &input));
    candidate = (book_name_t){"长安的荔枝", "马伯庸"};
    assert(match_score(&input, &candidate) == 100);
    input = (book_name_t){"活着", ""};
    candidate = (book_name_t){"活着", "余华"};
    assert(match_score(&input, &candidate) == MATCH_EXACT);
    input = (book_name_t){"三体全集", "刘慈欣"};
    candidate = (book_name_t){"三体", "刘慈欣"};
    assert(match_score(&input, &candidate) == 0);

    // Public WeRead results for the series contain the matching volume and an unrelated near-name.
    input = (book_name_t){0};
    assert(extract_name("十日终焉（完结精校版）.txt", &input));
    assert(!strcmp(input.title, "十日终焉"));
    candidate = (book_name_t){"十日终焉·因果", "杀虫队队员"};
    const unsigned series = match_score(&input, &candidate);
    assert(series == MATCH_SERIES);
    matched_metadata(&input, &candidate, series);
    assert(!strcmp(candidate.title, "十日终焉") && !strcmp(candidate.author, "杀虫队队员"));
    assert(match_score(&input, &candidate) > series);
    candidate = (book_name_t){"十日连丧", "一山钩月"};
    assert(!match_score(&input, &candidate));
    candidate = (book_name_t){"十日终焉：番外", "杀虫队队员"};
    assert(!match_score(&input, &candidate));
    input = (book_name_t){"十日终焉·白羊", "杀虫队队员"};
    candidate = (book_name_t){"十日终焉·因果", "杀虫队队员"};
    assert(!match_score(&input, &candidate));
    char keyword[TITLE_CAP];
    assert(shorter_query(input.title, keyword) && !strcmp(keyword, "十日终焉"));
    assert(shorter_query("十日终焉 杀虫队队员", keyword) && !strcmp(keyword, "十日终焉"));
    assert(!shorter_query("十日终焉", keyword));
    input = (book_name_t){"十日终烟", "杀虫队队员"};
    candidate = (book_name_t){"十日终焉", "杀虫队队员"};
    assert(match_score(&input, &candidate) == MATCH_TYPO);
    strcpy(candidate.author, "其他作者");
    assert(!match_score(&input, &candidate));
    input = (book_name_t){"十日终焉第2册", "杀虫队队员"};
    candidate = (book_name_t){"十日终焉第3册", "杀虫队队员"};
    assert(!match_score(&input, &candidate));

    match_list_t matches = {0};
    const book_name_t whole = {"十日终焉", "杀虫队队员"};
    const book_name_t volume = {"十日终焉·因果", "杀虫队队员"};
    offer_match(&matches, &volume, MATCH_SERIES, "https://cdn.weread.qq.com/outpic/209/3004712209.jpg", false);
    offer_match(&matches, &volume, MATCH_SERIES, "http://cdn.weread.qq.com/outpic/209/3004712209.jpg", false);
    assert(matches.count == 1);
    offer_match(&matches, &whole, MATCH_EXACT, "https://cdn.weread.qq.com/other.jpg", false);
    assert(matches.count == 2 && !strcmp(matches.entries[0].name.title, "十日终焉"));
    offer_match(&matches, &whole, MATCH_SUBTITLE, "https://example.com/other.jpg", false);
    assert(matches.count == 2);
    offer_match(&matches, &whole, MATCH_SUBTITLE, "https://cdn.weread.qq.com/subtitle.jpg", false);
    offer_match(&matches, &whole, MATCH_TYPO, "https://cdn.weread.qq.com/typo.jpg", false);
    offer_match(&matches, &whole, MATCH_EXACT + 10, "https://cdn.weread.qq.com/exact.jpg", false);
    assert(matches.count == BOOK_WEREAD_OPTIONS &&
           matches.entries[0].score == MATCH_EXACT + 10 && matches.entries[3].score == MATCH_SUBTITLE);


    const char *catalog =
        "{\"success\":1,\"books\":[],\"exactMatch\":{\"books\":["
        "{\"title\":\"十日终焉\",\"author\":\"其他作者\",\"cover\":\"https://s3proxy-alp2-covers.cdn-zlib.sk/covers/books/bad.jpg\"},"
        "{\"title\":\"十日终焉\",\"author\":\"杀虫队队员\",\"cover\":\"https://s3proxy-alp2-covers.cdn-zlib.sk/covers299/collections/userbooks/real.jpg\"},"
        "{\"title\":\"十日终焉\",\"authors\":\"杀虫队队员\",\"cover\":\"https://s3proxy-alp2-covers.cdn-zlib.sk/covers/books/alternate.png\"},"
        "{\"title\":\"十日终焉\",\"authors\":\"杀虫队队员\",\"cover\":\"https://s3proxy-alp2-covers.cdn-zlib.sk.invalid.test/covers/bogus.jpg\"}]}}";
    match_list_t zlib = {0};
    cJSON *json = cJSON_Parse(catalog);
    assert(rank_zlib(json, &whole, &zlib) == ESP_OK && zlib.count == 2);
    assert(zlib.entries[0].from_zlib && !strcmp(zlib.entries[0].name.title, "十日终焉"));
    assert(strstr(zlib.entries[0].url, "/userbooks/real.jpg"));
    assert(strstr(zlib.entries[1].url, "/books/alternate.png"));
    cJSON_Delete(json);
    zlib.count = 0;
    assert(rank_zlib(NULL, &whole, &zlib) == ESP_ERR_INVALID_RESPONSE);
    json = cJSON_Parse("{\"success\":1,\"books\":[]}");
    assert(rank_zlib(json, &whole, &zlib) == ESP_ERR_NOT_FOUND);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"success\":1,\"books\":["
        "{\"title\":\"沉睡的人 Un homme qui dort（蜂鸟文丛）\","
        "\"author\":\"乔治·佩雷克 Georges Perec 著，李玉民 译\","
        "\"cover\":\"https://s3proxy-alp2-covers.cdn-zlib.sk/covers299/books/bilingual.jpg\"}]}");
    const book_name_t perec = {"沉睡的人", "乔治·佩雷克"};
    assert(rank_zlib(json, &perec, &zlib) == ESP_OK && zlib.count == 1);
    assert(zlib.entries[0].score == MATCH_SERIES && !strcmp(zlib.entries[0].name.author, perec.author));
    cJSON_Delete(json);
    char query[704] = "message=";
    assert(append_keyword(query, sizeof(query), "A /十") && !strcmp(query, "message=A%20%2F%E5%8D%81"));

    cover_row_t row = {.result={BOOK_WEREAD_MULTIPLE, 2, ESP_OK}, .options=matches.entries};
    static batch_t batch;
    batch.rows = &row; batch.count = 1; batch.focus = batch.request_row = BOOK_WEREAD_NO_ROW;
    s_batch = &batch;
    book_weread_cover_option_t info;
    static uint8_t preview[BOOK_WEREAD_PREVIEW_BYTES];
    assert(book_weread_cover_row(0).options == 2);
    assert(book_weread_cover_option(0, 0, &info, preview) && !info.preview_ready);
    assert(!book_weread_cover_option(0, 2, &info, preview));
    book_weread_cover_focus(0);
    batch.preview_checked = batch.preview_ready = 1;
    batch.previews[0][0] = 23;
    assert(book_weread_cover_option(0, 0, &info, preview) && info.preview_ready && preview[0] == 23);
    assert(!book_weread_cover_choose(0, 2) && book_weread_cover_choose(0, 1));
    assert(s_status.saving && batch.request_row == 0 && batch.request_option == 1);
    assert(book_weread_cover_row(0).state == BOOK_WEREAD_SAVING && !book_weread_cover_choose(0, 0));
    book_weread_cover_focus(BOOK_WEREAD_NO_ROW);
    assert(book_weread_cover_option(0, 0, &info, preview) && !info.preview_ready);
    static book_weread_path_t paths[1] = {CHOICE_BOOK};
    batch.paths = paths;
    FILE *selected = fopen(CHOICE_BOOK, "wb");
    assert(selected && !fclose(selected));
    struct stat choice_file;
    assert(identity(CHOICE_BOOK, &choice_file));
    row.file_size = 100; // A replaced file cannot keep the earlier search result.
    row.modified = choice_file.st_mtime;
    s_status.multiple = 1;
    static uint8_t selected_pixels[COVER_BYTES];
    http_response_t response = {0};
    assert(save_selected(&batch, &response, selected_pixels) && !selected_downloads);
    assert(book_weread_cover_row(0).state == BOOK_WEREAD_MULTIPLE && !s_status.saving);
    row.file_size = choice_file.st_size;
    row.result.options = 4;
    strcpy(matches.entries[1].saved_title, "十日终焉");
    assert(book_weread_cover_choose(0, 1));
    assert(save_selected(&batch, &response, selected_pixels) && selected_downloads == 1);
    assert(book_weread_cover_row(0).state == BOOK_WEREAD_SAVED && !s_status.saving);
    assert(s_status.saved == 1 && s_status.multiple == 0 && s_revision == 1);
    assert(!load_preview(&batch, &response, selected_pixels)); // Closed details perform no requests.
    book_weread_cover_focus(0);
    assert(book_weread_cover_option(0, 0, &info, preview) && info.preview_ready && preview[0] == 23);
    assert(book_weread_cover_option(0, 1, &info, preview) && info.preview_ready);
    const book_crop_t crop = book_cover_crop(BOOK_COVER_W, BOOK_COVER_H,
                                             BOOK_WEREAD_PREVIEW_W, BOOK_WEREAD_PREVIEW_H);
    for (unsigned y = 0; y < BOOK_WEREAD_PREVIEW_H; ++y)
        for (unsigned x = 0; x < BOOK_WEREAD_PREVIEW_W; ++x)
            assert(preview[y * BOOK_WEREAD_PREVIEW_W + x] ==
                   (uint8_t)(13 + crop.y + y * crop.height / BOOK_WEREAD_PREVIEW_H));
    assert(load_preview(&batch, &response, selected_pixels));
    assert(book_weread_cover_option(0, 2, &info, preview) && info.preview_ready && preview[0] == 91);
    assert(load_preview(&batch, &response, selected_pixels));
    assert(book_weread_cover_option(0, 3, &info, preview) && info.preview_checked && !info.preview_ready);
    assert(!load_preview(&batch, &response, selected_pixels) && preview_downloads == 2);
    row.result = (book_weread_cover_row_t){BOOK_WEREAD_MULTIPLE, 4, ESP_OK};
    s_status.multiple = 1;
    matches.entries[2].from_zlib = true;
    strcpy(matches.entries[2].name.title, "十日终焉");
    batch.preview_checked &= ~(1u << 2);
    batch.preview_ready &= ~(1u << 2);
    abort_preview = true;
    assert(load_preview(&batch, &response, selected_pixels));
    assert(book_weread_cover_option(0, 2, &info, preview) && !info.preview_checked);
    abort_preview = false;
    assert(load_preview(&batch, &response, selected_pixels));
    assert(book_weread_cover_option(0, 2, &info, preview) && info.preview_ready && preview[0] == 91);
    assert(book_weread_cover_choose(0, 2));
    assert(save_selected(&batch, &response, selected_pixels) && selected_downloads == 1);
    assert(book_weread_cover_row(0).state == BOOK_WEREAD_SAVED && !s_status.saving);
    reset_full_covers(&batch, BOOK_WEREAD_NO_ROW);
    uint8_t page[4096];
    response.data = page;
    catalog_fixture = catalog;
    unsigned catalog_host = 0;
    weread_matches = true;
    assert(collect_matches(CHOICE_BOOK, &response, &zlib, &catalog_host) == ESP_OK);
    assert(weread_queries == 1 && !catalog_queries && zlib.count == 1 && !zlib.entries[0].from_zlib);
    weread_matches = false;
    assert(collect_matches(CHOICE_BOOK, &response, &zlib, &catalog_host) == ESP_OK);
    assert(catalog_queries == 2 && catalog_host == 1 && zlib.count && zlib.entries[0].from_zlib);
    catalog_fails = true;
    assert(collect_matches(CHOICE_BOOK, &response, &zlib, &catalog_host) == ESP_ERR_NOT_FOUND);
    assert(catalog_queries == 3 && catalog_host == 2);
    assert(collect_matches(CHOICE_BOOK, &response, &zlib, &catalog_host) == ESP_ERR_NOT_FOUND);
    assert(catalog_queries == 3);
    char chosen[64];
    assert(book_weread_cover_metadata(CHOICE_BOOK, chosen, sizeof(chosen), NULL, 0) &&
           !strcmp(chosen, "十日终焉"));
    s_batch = NULL;

    char url[640];
    assert(cover_url(url, "http://cdn.weread.qq.com/cover.jpg"));
    assert(!strcmp(url, "https://cdn.weread.qq.com/cover.jpg"));
    assert(!cover_url(url, "https://cdn.weread.qq.com.example.org/cover.jpg"));
    assert(!cover_url(url, "https://cdn.weread.qq.com@invalid.example/cover.jpg"));

    FILE *book = fopen(TEST_BOOK, "wb");
    assert(book && fputs("书名：《活着》\n作者：余华\n第一章\n", book) >= 0 && !fclose(book));
    input = (book_name_t){0};
    assert(extract_name(TEST_BOOK, &input));
    assert(!strcmp(input.title, "活着") && !strcmp(input.author, "余华"));
    struct stat st;
    assert(identity(TEST_BOOK, &st));
    static uint8_t pixels[COVER_BYTES], loaded[COVER_BYTES];
    for (unsigned i = 0; i < COVER_BYTES; ++i) pixels[i] = i & 255;
    cancel = true;
    assert(!save_record(TEST_BOOK, &st, &input, pixels));
    cancel = false;
    assert(book_weread_cover_stamp(TEST_BOOK) == 0);
    assert(save_record(TEST_BOOK, &st, &input, pixels));
    uint64_t stamp = book_weread_cover_stamp(TEST_BOOK);
    assert(stamp && book_weread_cover_stamp(TEST_BOOK) == stamp);
    char title[64], author[64];
    assert(book_weread_cover_metadata(TEST_BOOK, title, sizeof(title), author, sizeof(author)));
    assert(!strcmp(title, "活着") && !strcmp(author, "余华"));
    assert(book_weread_cover_read(TEST_BOOK, loaded, BOOK_COVER_W, BOOK_COVER_H));
    assert(!memcmp(pixels, loaded, sizeof(pixels)));
    assert(book_weread_cover_read(TEST_BOOK, loaded, 80, 108));
    custom = true;
    assert(book_weread_cover_metadata(TEST_BOOK, title, sizeof(title), NULL, 0));
    assert(!strcmp(title, "自定义"));
    tf_ready = false;
    assert(book_weread_cover_stamp(TEST_BOOK) == 0);
    assert(!book_weread_cover_read(TEST_BOOK, loaded, BOOK_COVER_W, BOOK_COVER_H));
    tf_ready = true;
    assert(book_weread_cover_read(TEST_BOOK, loaded, BOOK_COVER_W, BOOK_COVER_H));
    book = fopen(TEST_BOOK, "ab");
    assert(book && fputs("changed", book) >= 0 && !fclose(book));
    assert(book_weread_cover_stamp(TEST_BOOK) == 0);
    assert(!book_weread_cover_read(TEST_BOOK, loaded, BOOK_COVER_W, BOOK_COVER_H));

    for (int i = 1; i + 1 < argc; i += 2) {
        static char fixture[256 * 1024];
        FILE *file = fopen(argv[i], "rb");
        assert(file);
        const size_t size = fread(fixture, 1, sizeof(fixture), file);
        assert(size && size < sizeof(fixture) && !ferror(file));
        fclose(file);
        json = cJSON_ParseWithLength(fixture, size);
        zlib.count = 0;
        input = (book_name_t){0};
        assert(extract_name(argv[i + 1], &input));
        assert(rank_zlib(json, &input, &zlib) == ESP_OK && zlib.count && zlib.entries[0].from_zlib);
        if (!strcmp(input.title, "沉睡的人")) {
            assert(!strcmp(zlib.entries[0].name.author, "乔治·佩雷克"));
            assert(!strncmp(zlib.entries[0].name.title, input.title, strlen(input.title)));
        }
        cJSON_Delete(json);
        printf("PASS: live Z-Library response ranked for %s\n", input.title);
    }
    puts("PASS: TXT fuzzy matching, Z-Library eAPI and trusted covers, preview and selection, validated TF save and file identity");
}
'''
    test = temp / "test.c"
    test.write_text(unit)
    executable = temp / "test"
    subprocess.run([
        "cc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "main/book"),
        "-I" + str(ROOT / "tools/book_storage_stubs"), "-I" + str(cjson), str(test),
        "-Wno-deprecated-declarations", str(cjson / "cJSON.c"),
        str(ROOT / "main/book/gbk.c"), "-o", str(executable)
    ], check=True)
    subprocess.run([str(executable), *(value for fixture in args.fixture for value in fixture)], check=True)

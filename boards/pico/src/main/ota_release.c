/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：公开升级清单的严格解析与版本比较。/ English: strict public release parsing and version comparison.
 */
#include "ota_online.h"
#include "cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 内测版兼容平台的alpha/beta序号，保留原rc格式并拒绝未知后缀。
// Accept platform alpha/beta sequences and legacy rc versions, rejecting unknown suffixes.
static bool version_parts(const char *s, unsigned parts[5]) {
    if (!s || !*s || strnlen(s, 32) > 31) return false;
    memset(parts, 0, 5 * sizeof(*parts));
    for (unsigned i = 0; i < 3; ++i) {
        if (!isdigit((unsigned char)*s)) return false;
        if (*s == '0' && isdigit((unsigned char)s[1])) return false;
        char *end; unsigned long n = strtoul(s, &end, 10);
        if (n > 65535) return false;
        parts[i] = (unsigned)n; s = end;
        if (i < 2 && *s++ != '.') return false;
    }
    // 同一版本按alpha、beta、rc、正式版排序，再比较数字序号。
    // Sort matching versions by alpha, beta, rc and stable, then numeric sequence.
    parts[3] = 3;
    if (!*s) return true;
    bool dotted = false;
    if (!strncmp(s, "-alpha.", 7)) { parts[3] = 0; s += 7; dotted = true; }
    else if (!strncmp(s, "-beta.", 6)) { parts[3] = 1; s += 6; dotted = true; }
    else if (!strncmp(s, "-rc", 3)) { parts[3] = 2; s += 3; }
    else return false;
    if (!isdigit((unsigned char)*s)) return false;
    if (dotted && *s == '0' && isdigit((unsigned char)s[1])) return false;
    char *end; unsigned long n = strtoul(s, &end, 10);
    if (*end || n > 65535) return false;
    parts[4] = (unsigned)n;
    return true;
}
int pico_version_compare(const char *a, const char *b) {
    unsigned left[5], right[5];
    if (!version_parts(a, left) || !version_parts(b, right)) return 0;
    for (unsigned i = 0; i < 5; ++i) {
        if (left[i] < right[i]) return -1;
        if (left[i] > right[i]) return 1;
    }
    return 0;
}
bool pico_mindreset_url_valid(const char *url) {
    const char *prefix = PICO_MINDRESET_DOWNLOAD_PREFIX;
    if (!url || strnlen(url, 256) >= 256 || strncmp(url, prefix, strlen(prefix))) return false;
    const char *token = url + strlen(prefix);
    if (!*token) return false;
    for (; *token; ++token)
        if (!isalnum((unsigned char)*token) && *token != '-' && *token != '_') return false;
    return true;
}
bool pico_mindreset_cdn_url_valid(const char *url) {
    const char *prefix = PICO_MINDRESET_CDN_PREFIX;
    if (!url || strnlen(url, PICO_MINDRESET_CDN_URL_MAX) >= PICO_MINDRESET_CDN_URL_MAX ||
        strncmp(url, prefix, strlen(prefix)) || !url[strlen(prefix)]) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p)
        if (*p <= 0x20 || *p >= 0x7f || *p == '\\' || *p == '#') return false;
    return true;
}
esp_err_t pico_mindreset_query_body(const char *current, char *out, size_t capacity) {
    unsigned version[5];
    if (!out || !capacity || !version_parts(current, version)) return ESP_ERR_INVALID_ARG;
    int n = snprintf(out, capacity,
        "{\"series\":\"read\",\"model\":\"read_0\",\"edition\":1,"
        "\"firmwareId\":\"%s\",\"status\":\"%s\",\"currentVersion\":\"%s\"}",
        PICO_MINDRESET_FIRMWARE_ID, pico_mindreset_channel(current), current);
    if (n < 0 || (size_t)n >= capacity) { out[0] = 0; return ESP_ERR_INVALID_SIZE; }
    return ESP_OK;
}
static bool field(const cJSON *root, const char *key, char *out, size_t cap) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(v) || !v->valuestring || !*v->valuestring || strlen(v->valuestring) >= cap) return false;
    memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
    return true;
}
// 先限制JSON嵌套与NUL转义，再交给解析器；重复字段在所有对象层都拒绝。
// Bound JSON nesting and NUL escapes before parsing; reject duplicate fields in every object.
static bool bounded_json(const char *json, size_t length) {
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char c = json[i];
        if (quoted) {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') {
                if (i + 5 < length && !memcmp(json + i + 1, "u0000", 5)) return false;
                escaped = true;
            } else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && !escaped && !depth;
}
static bool unique_fields(const cJSON *value, unsigned depth) {
    if (depth > 8) return false;
    for (const cJSON *a = value->child; a; a = a->next) {
        for (const cJSON *b = a->next; b; b = b->next)
            if (a->string && b->string && !strcmp(a->string, b->string)) return false;
        if (!unique_fields(a, depth + 1)) return false;
    }
    return true;
}
static bool equal_field(const cJSON *root, const char *key, const char *expected) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(value) && value->valuestring && !strcmp(value->valuestring, expected);
}
esp_err_t pico_mindreset_parse(const char *json, size_t length, const char *current,
                             pico_release_t *release, bool *need_update) {
    unsigned current_parts[5];
    if (!release || !need_update) return ESP_ERR_INVALID_ARG;
    memset(release, 0, sizeof(*release));
    *need_update = false;
    if (!json || !length || length > 8192 || memchr(json, 0, length) ||
        !version_parts(current, current_parts) || !bounded_json(json, length)) return ESP_ERR_INVALID_ARG;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    bool valid = unique_fields(root, 0);
    while (end && end < json + length && isspace((unsigned char)*end)) ++end;
    if (!end || end != json + length) valid = false;
    const cJSON *edition = cJSON_GetObjectItemCaseSensitive(root, "edition");
    const cJSON *update = cJSON_GetObjectItemCaseSensitive(root, "needUpdate");
    const cJSON *ota = cJSON_GetObjectItemCaseSensitive(root, "ota");
    unsigned version[5];
    valid = valid && equal_field(root, "series", "read") && equal_field(root, "model", "read_0") &&
        cJSON_IsNumber(edition) && edition->valuedouble == 1 &&
        equal_field(root, "firmwareId", PICO_MINDRESET_FIRMWARE_ID) &&
        equal_field(root, "status", pico_mindreset_channel(current)) && cJSON_IsBool(update) &&
        field(root, "version", release->version, sizeof(release->version)) &&
        version_parts(release->version, version) && pico_ota_same_channel(release->version, current) && equal_field(root, "updateVersion", release->version) &&
        (cJSON_IsTrue(update) == (pico_version_compare(release->version, current) > 0)) &&
        (cJSON_IsNull(ota) || cJSON_IsObject(ota));
    if (valid && cJSON_IsObject(ota)) {
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(ota, "size");
        valid = field(ota, "downloadUrl", release->url, sizeof(release->url)) &&
            field(ota, "sha256", release->sha256, sizeof(release->sha256)) &&
            pico_mindreset_url_valid(release->url) && strlen(release->sha256) == 64 &&
            cJSON_IsNumber(size) && size->valuedouble >= 288 && size->valuedouble <= PICO_OTA_IMAGE_MAX &&
            size->valuedouble == (uint32_t)size->valuedouble;
        if (valid) {
            for (unsigned i = 0; i < 64; ++i)
                if (!isxdigit((unsigned char)release->sha256[i])) valid = false;
            release->size = (uint32_t)size->valuedouble;
        }
    }
    if (valid) *need_update = cJSON_IsTrue(update);
    cJSON_Delete(root);
    if (!valid) { memset(release, 0, sizeof(*release)); return ESP_ERR_INVALID_RESPONSE; }
    snprintf(release->project, sizeof(release->project), "%s", PICO_OTA_PROJECT);
    snprintf(release->board, sizeof(release->board), "%s", PICO_OTA_BOARD);
    snprintf(release->layout, sizeof(release->layout), "%s", PICO_OTA_LAYOUT);
    snprintf(release->notes, sizeof(release->notes), "%s", "MindReset 更新");
    return ESP_OK;
}

bool pico_github_url_valid(const char *url) {
    // 升级源限定官方 HTTPS 站点，避免重定向泄漏或被替换为任意下载源。
    // Limit updates to the official HTTPS origin; do not follow arbitrary redirects.
    static const char *const prefixes[] = {
        "https://wegooo-cell.github.io/read-pico-reader/"
    };
    if (!url || strlen(url) >= 256) return false;
    const char *name = NULL;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        size_t n = strlen(prefixes[i]);
        if (!strncmp(url, prefixes[i], n)) { name = url + n; break; }
    }
    if (!name) return false;
    if (!*name || strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) return false;
    for (const char *p = name; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.') return false;
    size_t n = strlen(name);
    return n > 4 && !strcmp(name + n - 4, ".bin");
}


// 渠道由运行版本决定，与网页刷机入口无关。/ Channel follows the running version, independent of installer.
const char *pico_mindreset_channel(const char *current) {
    unsigned v[5];
    if (!version_parts(current, v)) return NULL;
    return v[3] == 0 ? "ALPHA" : v[3] == 1 ? "TESTING" : "RELEASED";
}
bool pico_ota_same_channel(const char *candidate, const char *current) {
    const char *a=pico_mindreset_channel(candidate), *b=pico_mindreset_channel(current);
    return a && b && !strcmp(a,b);
}
const char *pico_github_feed_url(const char *current) {
    const char *channel=pico_mindreset_channel(current);
    if (!channel) return NULL;
    if (!strcmp(channel,"ALPHA")) return "https://wegooo-cell.github.io/read-pico-reader/update-alpha.json";
    if (!strcmp(channel,"TESTING")) return "https://wegooo-cell.github.io/read-pico-reader/update-testing.json";
    return PICO_OTA_FEED_URL;
}
bool pico_release_url_valid(const char *url) {
    return pico_mindreset_url_valid(url) || pico_github_url_valid(url);
}

esp_err_t pico_release_parse(const char *json, size_t length, pico_release_t *release) {
    if (!json || !release || !length || length > 8192 || memchr(json, 0, length)) return ESP_ERR_INVALID_ARG;
    memset(release, 0, sizeof(*release));
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    // 拒绝重复字段与尾随载荷。/ Reject duplicate fields and trailing payloads.
    bool valid = bounded_json(json, length) && unique_fields(root, 0);
    for (const cJSON *a = root->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (a->string && b->string && !strcmp(a->string, b->string)) valid = false;
    while (end && end < json + length && isspace((unsigned char)*end)) ++end;
    if (!end || end != json + length) valid = false;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    unsigned version[5], base[5];
    valid = valid && cJSON_IsNumber(schema) && schema->valuedouble == 1 &&
        field(root, "version", release->version, sizeof(release->version)) &&
        field(root, "project", release->project, sizeof(release->project)) &&
        field(root, "board", release->board, sizeof(release->board)) &&
        field(root, "layout", release->layout, sizeof(release->layout)) &&
        field(root, "minimum_base_version", release->minimum_base_version, sizeof(release->minimum_base_version)) &&
        field(root, "url", release->url, sizeof(release->url)) &&
        field(root, "sha256", release->sha256, sizeof(release->sha256)) &&
        field(root, "notes", release->notes, sizeof(release->notes)) &&
        cJSON_IsNumber(size) && size->valuedouble >= 288 && size->valuedouble <= 0x400000 &&
        size->valuedouble == (uint32_t)size->valuedouble &&
        version_parts(release->version, version) && version_parts(release->minimum_base_version, base) &&
        !strcmp(release->board, PICO_OTA_BOARD) && !strcmp(release->layout, PICO_OTA_LAYOUT) &&
        pico_github_url_valid(release->url) && !strcmp(release->project, PICO_OTA_PROJECT) && strlen(release->sha256) == 64;
    if (valid) {
        for (unsigned i = 0; i < 64; ++i)
            if (!isxdigit((unsigned char)release->sha256[i])) valid = false;
        release->size = (uint32_t)size->valuedouble;
    }
    cJSON_Delete(root);
    if (!valid) { memset(release, 0, sizeof(*release)); return ESP_ERR_INVALID_RESPONSE; }
    return ESP_OK;
}

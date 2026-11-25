/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：HTTPS 分块下载、完整性校验和可取消升级任务。/ English: streamed HTTPS updates, integrity checks and cancellable workers.
 * 冻结：禁止覆盖当前槽；离页或锁屏先等待任务结束。/ Frozen: never overwrite the running slot; join before leaving or locking.
 * 用户修订：分段擦除并串行化闪存与屏幕推送，启动前检查连续内存，低内存安全失败。
 * User revision: erase incrementally and serialize flash with display output; require memory headroom and fail safely on shortage.
 * 用户2026-11-24：PICO内测仅使用MindReset ALPHA，禁用旧清单回退；HTTPS、校验、双槽与数据保护保持。
 * User2026-11-24: Pico alpha uses MindReset ALPHA only, retaining HTTPS, integrity, inactive-slot and data guards without legacy feed fallback.
 */
#include "ota_online.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "read_pico_transfer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static SemaphoreHandle_t s_mutex, s_done, s_flash_display;
static pico_update_status_t *s_status;
static atomic_bool s_cancel;
static bool s_joinable, s_download;
static const esp_partition_t *s_verified;

static bool initialize(void) {
    if (!s_status) s_status = heap_caps_calloc(1, sizeof(*s_status), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_done) s_done = xSemaphoreCreateBinary();
    if (!s_flash_display) s_flash_display = xSemaphoreCreateRecursiveMutex();
    return s_status && s_mutex && s_done && s_flash_display;
}
bool pico_online_display_begin(void) {
    return s_flash_display && xSemaphoreTakeRecursive(s_flash_display, portMAX_DELAY) == pdTRUE;
}
void pico_online_display_end(bool held) {
    if (held) xSemaphoreGiveRecursive(s_flash_display);
}
static bool memory_available(size_t internal_min, size_t internal_block, size_t external_min) {
    const unsigned internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const unsigned external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    return heap_caps_get_free_size(internal) >= internal_min &&
        heap_caps_get_largest_free_block(internal) >= internal_block &&
        heap_caps_get_free_size(external) >= external_min &&
        heap_caps_get_largest_free_block(external) >= 32768;
}
// 锁只覆盖闪存操作，不覆盖网络等待、状态互斥锁或任务收尾，避免取消/刷屏互相等待。
// Lock flash operations only, excluding network waits, status locks and joins to avoid cancellation/display deadlocks.
static esp_err_t flash_begin(const esp_partition_t *target, esp_ota_handle_t *handle) {
    bool held = pico_online_display_begin();
    esp_err_t error = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, handle);
    pico_online_display_end(held);
    return error;
}
static esp_err_t flash_write(esp_ota_handle_t handle, const void *data, size_t size) {
    bool held = pico_online_display_begin();
    esp_err_t error = esp_ota_write(handle, data, size);
    pico_online_display_end(held);
    return error;
}
static esp_err_t flash_finish(esp_ota_handle_t handle, bool aborting) {
    bool held = pico_online_display_begin();
    esp_err_t error = aborting ? esp_ota_abort(handle) : esp_ota_end(handle);
    pico_online_display_end(held);
    return error;
}
static void state(pico_update_state_t value, const char *message) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status->state = value;
    snprintf(s_status->message, sizeof(s_status->message), "%s", message);
    ++s_status->revision;
    xSemaphoreGive(s_mutex);
}
bool pico_online_busy(void) {
    if (!s_status || !s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool busy = s_status->busy;
    xSemaphoreGive(s_mutex); return busy;
}
void pico_online_get_status(pico_update_status_t *status) {
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!s_status || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(status, s_status, sizeof(*status));
    xSemaphoreGive(s_mutex);
}
static bool layout_valid(void) {
    const esp_partition_t *a = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *b = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t *data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    return a && b && data && a->address == 0x10000 && b->address == 0x910000 &&
        a->size == 0x400000 && b->size == 0x400000 && data->address == 0xd10000 && data->size == 0x2000;
}
static bool online(bool *owned) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (!(status.mode == READ_PICO_TRANSFER_MODE_STA && status.network_ready)) {
        char ssid[33]; bool saved = false;
        if (read_pico_transfer_get_saved_wifi(ssid, &saved) != ESP_OK || !saved) {
            state(PICO_UPDATE_FAILED, "请先在 WiFi 设置中连接网络"); return false;
        }
        read_pico_transfer_stop();
        read_pico_transfer_cfg_t cfg = {.mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true};
        *owned = true;
        if (read_pico_transfer_start(&cfg) != ESP_OK) {
            state(PICO_UPDATE_FAILED, "WiFi 连接失败，请重试"); return false;
        }
    }
    int64_t deadline = esp_timer_get_time() + 30000000;
    while (!atomic_load(&s_cancel)) {
        read_pico_transfer_service_poll();
        read_pico_transfer_get_status(&status);
        if (status.mode == READ_PICO_TRANSFER_MODE_STA && status.network_ready) {
            if (time(NULL) < 1704067200) {
                uint32_t utc;
                if (read_pico_transfer_sync_time_online(&utc) != ESP_OK) {
                    state(PICO_UPDATE_FAILED, "网络对时失败，请对时后重试"); return false;
                }
            }
            return !atomic_load(&s_cancel);
        }
        if (status.state == READ_PICO_TRANSFER_ERROR || esp_timer_get_time() >= deadline) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!atomic_load(&s_cancel)) state(PICO_UPDATE_FAILED, "网络连接超时，请重试");
    return false;
}
typedef struct {
    uint32_t start, end, size;
    bool valid;
    unsigned range_headers, location_headers;
    char *location;
    bool invalid_location;
} download_range_t;
static esp_err_t range_header(esp_http_client_event_t *event) {
    download_range_t *range = event->user_data;
    if (!range || event->event_id != HTTP_EVENT_ON_HEADER || !event->header_key || !event->header_value)
        return ESP_OK;
    if (!strcasecmp(event->header_key, "Content-Range")) {
        unsigned long start, end, size; char tail;
        range->valid = ++range->range_headers == 1 && strnlen(event->header_value, 96) < 96 &&
            sscanf(event->header_value, "bytes %lu-%lu/%lu%c", &start, &end, &size, &tail) == 3 &&
            start <= end && end < size && size <= UINT32_MAX;
        if (range->valid) { range->start = start; range->end = end; range->size = size; }
    }
    if (range->location && !strcasecmp(event->header_key, "Location")) {
        size_t n = strnlen(event->header_value, PICO_MINDRESET_CDN_URL_MAX);
        if (++range->location_headers != 1 || n >= PICO_MINDRESET_CDN_URL_MAX)
            range->invalid_location = true;
        else memcpy(range->location, event->header_value, n + 1);
    }
    return ESP_OK;
}
static esp_http_client_handle_t open_request(const char *url, const char *body, uint32_t offset,
        download_range_t *range, char *location, int *status, int64_t *length) {
    if (range) { memset(range, 0, sizeof(*range)); range->location = location; }
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 15000, .buffer_size = 2048,
        .buffer_size_tx = 1024, .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true, .keep_alive_enable = true,
        .event_handler = range_header, .user_data = range};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return NULL;
    if (esp_http_client_set_method(client, body ? HTTP_METHOD_POST : HTTP_METHOD_GET) != ESP_OK ||
        esp_http_client_set_header(client, "User-Agent", PICO_OTA_CAPABILITY) != ESP_OK ||
        esp_http_client_set_header(client, "Accept-Encoding", "identity") != ESP_OK ||
        esp_http_client_set_header(client, "Cache-Control", "no-cache") != ESP_OK) goto fail;
    size_t body_size = body ? strnlen(body, 512) : 0;
    if (body_size >= 512 || (body && esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK))
        goto fail;
    if (offset) {
        char value[48]; snprintf(value, sizeof(value), "bytes=%lu-", (unsigned long)offset);
        if (esp_http_client_set_header(client, "Range", value) != ESP_OK) goto fail;
    }
    if (esp_http_client_open(client, (int)body_size) != ESP_OK || atomic_load(&s_cancel)) goto fail;
    int64_t write_deadline = esp_timer_get_time() + 15000000;
    for (size_t written = 0; written < body_size;) {
        if (esp_timer_get_time() >= write_deadline) goto fail;
        int n = esp_http_client_write(client, body + written, (int)(body_size - written));
        if (n <= 0 || (size_t)n > body_size - written || atomic_load(&s_cancel)) goto fail;
        written += (size_t)n;
    }
    *length = esp_http_client_fetch_headers(client);
    if (*length < 0 || atomic_load(&s_cancel)) goto fail;
    *status = esp_http_client_get_status_code(client);
    esp_http_client_set_timeout_ms(client, 1000);
    return client;
fail:
    esp_http_client_cleanup(client);
    return NULL;
}
// 每次初始下载和续传均重新获取签名地址；不缓存短期链接，不向CDN发送组织凭据。
// Resolve a fresh signed URL for every initial/resumed request; never cache it or send organization credentials.
static esp_http_client_handle_t open_http_range(const char *url, uint32_t offset,
        download_range_t *range, int *status, int64_t *length) {
    if (atomic_load(&s_cancel)) return NULL;
    if (pico_github_url_valid(url)) return open_request(url, NULL, offset, range, NULL, status, length);
    if (!pico_mindreset_url_valid(url)) return NULL;
    char *signed_url = heap_caps_calloc(PICO_MINDRESET_CDN_URL_MAX, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!signed_url) return NULL;
    download_range_t redirect;
    esp_http_client_handle_t grant = open_request(url, NULL, 0, &redirect, signed_url, status, length);
    bool received_grant = grant != NULL;
    if (grant) esp_http_client_cleanup(grant);
    bool valid = received_grant && *status == 302 && redirect.location_headers == 1 &&
        !redirect.invalid_location && pico_mindreset_cdn_url_valid(signed_url) && !atomic_load(&s_cancel);
    esp_http_client_handle_t client = valid ? open_request(signed_url, NULL, offset, range, NULL, status, length) : NULL;
    heap_caps_free(signed_url);
    // 过期签名和临时服务错误回到外层有界重试，重试时再次申请新签名。
    // Expired signatures and temporary errors use the outer bounded retry, obtaining another fresh URL.
    if (client && (*status == 403 || *status == 410 || *status == 408 || *status == 429 || *status >= 500)) {
        esp_http_client_cleanup(client); client = NULL;
    }
    return client;
}
// 复用连接管理器等待短暂断网恢复，允许取消，不重启整个无线服务。
// Wait for the connection manager to recover a short outage, with cancellation and no radio restart.
static bool wait_network(void) {
    int64_t deadline = esp_timer_get_time() + 30000000;
    while (!atomic_load(&s_cancel) && esp_timer_get_time() < deadline) {
        read_pico_transfer_service_poll();
        read_pico_transfer_status_t status;
        read_pico_transfer_get_status(&status);
        if (status.mode == READ_PICO_TRANSFER_MODE_STA && status.network_ready) return true;
        if (status.state == READ_PICO_TRANSFER_ERROR) return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}
// 读超时可重试；每次收到数据续期，避免慢速网络被总时长截断。
// Retry temporary read timeouts and renew the idle deadline after each chunk.
static int read_chunk(esp_http_client_handle_t client, uint8_t *buffer, int cap, int64_t *idle) {
    while (!atomic_load(&s_cancel) && esp_timer_get_time() < *idle) {
        read_pico_transfer_service_poll();
        int n = esp_http_client_read(client, (char *)buffer, cap);
        if (n > 0) { *idle = esp_timer_get_time() + 30000000; return n; }
        if (!n && esp_http_client_is_complete_data_received(client)) return 0;
        if (n < 0 && n != -ESP_ERR_HTTP_EAGAIN) return -1;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return -1;
}
// 主源不可用才查询同渠道GitHub；不改变合法的“已是最新”响应。
// Consult same-channel GitHub only when the primary is unavailable; respect a valid latest response.
static bool github_query(const esp_app_desc_t *running, char *json, pico_release_t *release, bool *need_update) {
    const char *url=pico_github_feed_url(running->version);
    if (!url || atomic_load(&s_cancel) || !wait_network()) return false;
    int code=-1; int64_t length=-1;
    esp_http_client_handle_t client=open_request(url,NULL,0,NULL,NULL,&code,&length);
    if (!client) return false;
    if (code!=200 || length>8192) {esp_http_client_cleanup(client);return false;}
    size_t received=0; int n=-1; int64_t idle=esp_timer_get_time()+30000000;
    while (received<8192 && (n=read_chunk(client,(uint8_t*)json+received,(int)(8192-received),&idle))>0) received+=n;
    bool complete=esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);json[received]=0;
    if (n<0 || !complete || pico_release_parse(json,received,release)!=ESP_OK ||
        !pico_ota_same_channel(release->version,running->version) ||
        pico_version_compare(running->version,release->minimum_base_version)<0) return false;
    *need_update=pico_version_compare(release->version,running->version)>0;
    return true;
}

// 续传换源只允许逐字节相同的镜像；不得把另一版本写到已开始的槽中。
// Resume across providers only for the identical image; never mix versions in an in-progress slot.
static bool download_use_github(pico_release_t *release) {
    if (!pico_mindreset_url_valid(release->url) || atomic_load(&s_cancel)) return false;
    char *json=heap_caps_malloc(8193,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    pico_release_t *other=heap_caps_calloc(1,sizeof(*other),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool update=false;
    bool ok=json && other && github_query(esp_app_get_description(),json,other,&update) && update &&
        !strcmp(other->version,release->version) && other->size==release->size &&
        !strcasecmp(other->sha256,release->sha256) && !strcmp(other->project,release->project) &&
        !strcmp(other->board,release->board) && !strcmp(other->layout,release->layout);
    if (ok) snprintf(release->url,sizeof(release->url),"%s",other->url);
    heap_caps_free(json);heap_caps_free(other);return ok;
}

static bool check_feed(void) {
    char *json = heap_caps_malloc(8193, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    pico_release_t *release = heap_caps_calloc(1, sizeof(*release), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json || !release) { state(PICO_UPDATE_FAILED, "内存不足，请退出传书后重试"); goto failed; }
    const esp_app_desc_t *running = esp_app_get_description();
    char body[384];
    if (strcmp(running->project_name, PICO_OTA_PROJECT) || !layout_valid() ||
        pico_mindreset_query_body(running->version, body, sizeof(body)) != ESP_OK) {
        state(PICO_UPDATE_FAILED, "基础包不兼容，请先通过官网完整刷机"); goto failed;
    }
    bool valid = false, need_update = false;
    for (unsigned attempt = 0; attempt < 2 && !atomic_load(&s_cancel); ++attempt) {
        int code = -1; int64_t length = -1;
        esp_http_client_handle_t client = NULL;
        if (wait_network()) client = open_request(PICO_MINDRESET_QUERY_URL, body, 0, NULL, NULL, &code, &length);
        if (!client) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
        if (code == 404) {
            esp_http_client_cleanup(client);
            break;
        }
        if (code != 200 || length > 8192) {
            esp_http_client_cleanup(client);
            if (code == 429 || code >= 500) vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        size_t received = 0;
        int64_t idle = esp_timer_get_time() + 30000000;
        int n = -1;
        while (received < 8192 && (n = read_chunk(client, (uint8_t *)json + received, (int)(8192 - received), &idle)) > 0)
            received += n;
        bool complete = esp_http_client_is_complete_data_received(client);
        esp_http_client_cleanup(client);
        json[received] = 0;
        if (n >= 0 && complete && pico_mindreset_parse(json, received, running->version, release, &need_update) == ESP_OK) {
            valid = true; break;
        }
    }
    if ((!valid || (need_update && !release->url[0])) && !atomic_load(&s_cancel))
        valid=github_query(running,json,release,&need_update);
    if (!valid) { state(PICO_UPDATE_FAILED, "更新源连接失败或信息无效，请稍后重试"); goto failed; }
    if (need_update && !release->size) {
        state(PICO_UPDATE_FAILED, "此内测版本未提供 OTA 升级包"); goto failed;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&s_status->release, release, sizeof(*release));
    xSemaphoreGive(s_mutex);
    state(need_update ? PICO_UPDATE_AVAILABLE : PICO_UPDATE_LATEST,
          need_update ? "发现新版本，可下载安装" : "当前已是最新版本");
    heap_caps_free(json); heap_caps_free(release);
    return true;
failed:
    heap_caps_free(json); heap_caps_free(release);
    return false;
}
static bool header_valid(const uint8_t *data, const pico_release_t *release) {
    const esp_image_header_t *header = (const esp_image_header_t *)data;
    const esp_app_desc_t *desc = (const esp_app_desc_t *)(data + sizeof(*header) + sizeof(esp_image_segment_header_t));
    const esp_app_desc_t *current = esp_app_get_description();
    return header->magic == ESP_IMAGE_HEADER_MAGIC && header->chip_id == ESP_CHIP_ID_ESP32S3 &&
        desc->magic_word == ESP_APP_DESC_MAGIC_WORD &&
        memchr(desc->project_name, 0, sizeof(desc->project_name)) && memchr(desc->version, 0, sizeof(desc->version)) &&
        !strcmp(desc->project_name, current->project_name) && !strcmp(desc->version, release->version);
}
static bool download(void) {
    pico_release_t *release = heap_caps_malloc(sizeof(*release), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *buffer = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *header = heap_caps_malloc(288, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!release || !buffer || !header) { state(PICO_UPDATE_FAILED, "内存不足，无法下载升级包"); goto failed; }
    xSemaphoreTake(s_mutex, portMAX_DELAY); *release = s_status->release; xSemaphoreGive(s_mutex);
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!layout_valid() || !target || target == esp_ota_get_running_partition() || release->size > target->size) {
        state(PICO_UPDATE_FAILED, "基础包不兼容，请通过官网完整刷机"); goto failed;
    }
    int code = -1; int64_t length = -1;
    esp_http_client_handle_t client = NULL;
    download_range_t range;
    esp_ota_handle_t handle = 0;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    bool begun = false;
    esp_err_t error = ESP_OK;
    if (psa_crypto_init() != PSA_SUCCESS || psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) error = ESP_FAIL;
    size_t total = 0, header_size = 0;
    int64_t idle = esp_timer_get_time() + 30000000;
    int n = -1;
    unsigned retries = 0;
    bool mirror_tried = false;
    bool complete = false;
    while (error == ESP_OK && !atomic_load(&s_cancel)) {
        if (!client) {
            if (wait_network()) client = open_http_range(release->url, total, &range, &code, &length);
            if (!client) {
                if (atomic_load(&s_cancel)) { error=ESP_FAIL;break; }
                if (retries++ >= 3) {
                    if (!mirror_tried) {mirror_tried=true;if(download_use_github(release)){retries=0;continue;}}
                    error=ESP_FAIL;break;
                }
                vTaskDelay(pdMS_TO_TICKS(250 * retries)); continue;
            }
            // 续传必须严格匹配起点与全文件长度；最终仍验证整本镜像的 SHA256。
            // Resumed responses must match their offset and full length; the whole image still gets SHA256 verification.
            bool response_ok = total ? code == 206 && range.valid && range.start == total &&
                range.size == release->size && range.end == release->size - 1 : code == 200;
            if (!response_ok || (length > 0 && (uint64_t)length != release->size - total)) {
                error = ESP_ERR_INVALID_RESPONSE; break;
            }
            idle = esp_timer_get_time() + 30000000;
        }
        n = read_chunk(client, buffer, 4096, &idle);
        if (n <= 0) {
            complete = esp_http_client_is_complete_data_received(client);
            esp_http_client_cleanup(client); client = NULL;
            if (n == 0 && complete && total == release->size) break;
            complete = false;
            if (total >= release->size || atomic_load(&s_cancel)) {error=ESP_FAIL;break;}
            if (retries++ >= 3) {
                if (!mirror_tried) {mirror_tried=true;if(download_use_github(release)){retries=0;continue;}}
                error=ESP_FAIL;break;
            }
            vTaskDelay(pdMS_TO_TICKS(250 * retries));
            continue;
        }
        if (total + n > release->size) { error = ESP_ERR_INVALID_SIZE; break; }
        size_t used = 0;
        if (!begun) {
            size_t needed = 288 - header_size;
            used = (size_t)n < needed ? (size_t)n : needed;
            memcpy(header + header_size, buffer, used); header_size += used;
            if (header_size == 288) {
                if (!header_valid(header, release)) { error = ESP_ERR_INVALID_RESPONSE; break; }
                error = flash_begin(target, &handle);
                begun = error == ESP_OK;
                if (begun) error = flash_write(handle, header, header_size);
            }
        }
        if (error == ESP_OK && begun && (size_t)n > used) error = flash_write(handle, buffer + used, n - used);
        if (error != ESP_OK || psa_hash_update(&hash, buffer, n) != PSA_SUCCESS) { error = ESP_FAIL; break; }
        total += n;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_status->received = total; ++s_status->revision;
        xSemaphoreGive(s_mutex);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (client) esp_http_client_cleanup(client);
    uint8_t digest[32]; size_t digest_size = 0; char hex[65];
    if (error == ESP_OK && (!begun || n < 0 || !complete || total != release->size || atomic_load(&s_cancel))) error = ESP_FAIL;
    if (error == ESP_OK && psa_hash_finish(&hash, digest, sizeof(digest), &digest_size) != PSA_SUCCESS) error = ESP_FAIL;
    psa_hash_abort(&hash);
    if (error == ESP_OK) {
        for (unsigned i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
        if (digest_size != 32 || strcasecmp(hex, release->sha256)) error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        if (begun) flash_finish(handle, true);
        state(PICO_UPDATE_FAILED, "下载中断或校验失败，当前版本保持不变"); goto failed;
    }
    error = flash_finish(handle, false);
    if (error != ESP_OK) { state(PICO_UPDATE_FAILED, "固件校验失败，当前版本保持不变"); goto failed; }
    // 后台仅验证镜像；主线程处理最终启动切换，取消时不影响当前槽。
    // The worker verifies only; the main task commits boot, so cancellation cannot change the running slot.
    xSemaphoreTake(s_mutex, portMAX_DELAY); s_verified = target; xSemaphoreGive(s_mutex);
    state(PICO_UPDATE_READY, "校验通过，即将重启安装");
    heap_caps_free(release); heap_caps_free(buffer); heap_caps_free(header);
    return true;
failed:
    heap_caps_free(release); heap_caps_free(buffer); heap_caps_free(header);
    return false;
}
static void worker(void *unused) {
    (void)unused;
    bool owned = false;
    wifi_ps_type_t previous_ps = WIFI_PS_MIN_MODEM;
    bool restore_ps = false;
    if (online(&owned) && !atomic_load(&s_cancel)) {
        // 更新期间关闭无线省电；完成、取消和失败均恢复原策略。
        // Disable radio power saving for updates, then restore it on success, cancellation or failure.
        restore_ps = esp_wifi_get_ps(&previous_ps) == ESP_OK && esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK;
        if (!memory_available(32768, 8192, 262144))
            state(PICO_UPDATE_FAILED, "内存不足，请退出传书后重试");
        else if (s_download) (void)download(); else (void)check_feed();
    }
    if (restore_ps) (void)esp_wifi_set_ps(previous_ps);
    if (owned) read_pico_transfer_stop();
    if (atomic_load(&s_cancel)) state(PICO_UPDATE_CANCELLED, "已取消，当前版本保持不变");
    xSemaphoreTake(s_mutex, portMAX_DELAY); s_status->busy = false; ++s_status->revision; xSemaphoreGive(s_mutex);
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}
static esp_err_t start(bool downloading) {
    if (!initialize()) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool busy = s_status->busy;
    bool available = s_status->state == PICO_UPDATE_AVAILABLE;
    xSemaphoreGive(s_mutex);
    if (busy || (downloading && !available)) return ESP_ERR_INVALID_STATE;
    if (s_joinable) { xSemaphoreTake(s_done, portMAX_DELAY); s_joinable = false; }
    if (!memory_available(49152, 16384, 524288)) {
        state(PICO_UPDATE_FAILED, "内存不足，请退出传书后重试");
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&s_cancel, false);
    s_download = downloading; s_verified = NULL;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status->busy = true; s_status->received = 0;
    xSemaphoreGive(s_mutex);
    state(downloading ? PICO_UPDATE_DOWNLOADING : PICO_UPDATE_CHECKING,
          downloading ? "正在下载，短暂断网会自动重连" : "正在连接 MindReset 检查更新");
    if (xTaskCreate(worker, "kiikoread_ota", 12288, NULL, 4, NULL) != pdPASS) {
        xSemaphoreTake(s_mutex, portMAX_DELAY); s_status->busy = false; xSemaphoreGive(s_mutex);
        state(PICO_UPDATE_FAILED, "内存不足，无法开始升级"); return ESP_ERR_NO_MEM;
    }
    s_joinable = true;
    return ESP_OK;
}
esp_err_t pico_online_check(void) { return start(false); }
esp_err_t pico_online_download(void) { return start(true); }
void pico_online_cancel_join(void) {
    if (s_joinable) {
        atomic_store(&s_cancel, true);
        xSemaphoreTake(s_done, portMAX_DELAY); s_joinable = false;
    }
    s_verified = NULL;
    if (s_status && s_status->state == PICO_UPDATE_READY) state(PICO_UPDATE_CANCELLED, "已取消，当前版本保持不变");
}
esp_err_t pico_online_commit(void) {
    if (s_joinable) { xSemaphoreTake(s_done, portMAX_DELAY); s_joinable = false; }
    if (!s_verified || atomic_load(&s_cancel)) return ESP_ERR_INVALID_STATE;
    bool held = pico_online_display_begin();
    esp_err_t error = esp_ota_set_boot_partition(s_verified);
    pico_online_display_end(held);
    s_verified = NULL;
    if (error != ESP_OK) state(PICO_UPDATE_FAILED, "启动切换失败，当前版本保持不变");
    return error;
}

/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：星曈云同步设备端实现：二维码信封、签名令牌、任务下载与导入。
 * English: Xteink cloud sync implementation: bind envelope, signed token, task download/import.
 *
 * 冻结：只连官方 HTTPS；身份走请求头；令牌只落卡；校验失败不上架；不静默关闭 TLS/SHA 校验。
 * Frozen: official HTTPS only; identity in headers; tokens stay on the card; failed checks never
 *      reach the shelf; TLS and integrity checks are never silently disabled.
 */

#include "book_xteink_sync.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "read_pico_transfer.h"
#include "zip_reader.h"

#include "book_store.h"
#include "settings.h"

#define XTEINK_BASE_URL "https://api-prod.xteink.cn"
#define XTEINK_APP_ID "cl-20260508"
#define XTEINK_APP_SECRET "3d3f2e6d-9745-79eb-ad19-9b33e5b4a424"
#define XTEINK_TOKEN_PATH "/api/v1/third-party-device/token"
#define XTEINK_PULL_PATH "/api/v1/third-party-device/tasks/pull?limit=20"
#define XTEINK_TASK_PATH "/api/v1/third-party-device/tasks/"
#define XTEINK_HTTP_TIMEOUT_MS 20000
#define XTEINK_RESPONSE_MAX (16U * 1024U)
#define XTEINK_ENVELOPE_MAX 1024U

static const char* TAG = "xteink";

typedef struct {
    char chip_id[24];
    char bluetooth_mac[20];
    char wifi_mac[20];
    char device_type[16];
    char brand[24];
    char firmware_name[32];
    char version[40];
} xteink_identity_t;

typedef struct {
    xteink_identity_t id;
    char access_token[512];
    char refresh_token[512];
    int64_t access_expires_at;
    char root[BOOK_STORE_PATH_MAX];
} xteink_state_t;

typedef struct {
    char* buf;
    size_t cap;
    size_t length;
    bool overflow;
} http_sink_t;

static SemaphoreHandle_t s_lock, s_done;
static TaskHandle_t s_task;
static bool s_cancel;
static bool s_owns_network;
static unsigned s_import_count;
static xteink_sync_status_t s_status = {.state = XTEINK_SYNC_IDLE};
static xteink_state_t s_state;
static bool s_state_loaded;

// ---- 状态发布 / status publishing -------------------------------------------
static void publish(xteink_sync_state_t state, const char* message) {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    s_status.busy = state != XTEINK_SYNC_IDLE && state != XTEINK_SYNC_DONE &&
                    state != XTEINK_SYNC_FAILED && state != XTEINK_SYNC_CANCELLED;
    if (message) snprintf(s_status.message, sizeof(s_status.message), "%s", message);
    ++s_status.revision;
    if (s_lock) xSemaphoreGive(s_lock);
}

static void publish_book(const char* book, int pending) {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    if (book) snprintf(s_status.book, sizeof(s_status.book), "%s", book);
    if (pending >= 0) s_status.pending = pending;
    ++s_status.revision;
    if (s_lock) xSemaphoreGive(s_lock);
}

static void set_qr(const char* text) {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_status.qr, sizeof(s_status.qr), "%s", text ? text : "");
    ++s_status.revision;
    if (s_lock) xSemaphoreGive(s_lock);
}

// ---- 纯函数 / pure helpers ---------------------------------------------------
static bool uri_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '.' || c == '_' || c == '~';
}

size_t xteink_url_encode_path(const char* in, char* out, size_t cap) {
    static const char kHex[] = "0123456789ABCDEF";
    size_t length = 0;
    if (!in || !out || !cap) return 0;
    for (const unsigned char* p = (const unsigned char*)in; *p; ++p) {
        if (uri_unreserved(*p) || *p == '/' || *p == '%') {
            if (length + 1 >= cap) return 0;
            out[length++] = (char)*p;
        } else {
            if (length + 3 >= cap) return 0;
            out[length++] = '%';
            out[length++] = kHex[*p >> 4];
            out[length++] = kHex[*p & 0x0f];
        }
    }
    out[length] = 0;
    return length;
}

size_t xteink_hex_encode(const uint8_t* in, size_t length, char* out, size_t cap) {
    static const char kHex[] = "0123456789abcdef";
    if (!in || !out || cap < length * 2 + 1) return 0;
    for (size_t i = 0; i < length; ++i) {
        out[i * 2] = kHex[in[i] >> 4];
        out[i * 2 + 1] = kHex[in[i] & 0x0f];
    }
    out[length * 2] = 0;
    return length * 2;
}

size_t xteink_base64url_encode(const uint8_t* in, size_t length, char* out, size_t cap) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t needed = (length * 4 + 2) / 3;
    if (!in || !out || cap < needed + 1) return 0;
    size_t o = 0;
    for (size_t i = 0; i < length; i += 3) {
        uint32_t value = (uint32_t)in[i] << 16;
        if (i + 1 < length) value |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < length) value |= in[i + 2];
        out[o++] = kAlphabet[(value >> 18) & 0x3f];
        if (o >= needed) break;
        out[o++] = kAlphabet[(value >> 12) & 0x3f];
        if (o >= needed) break;
        out[o++] = kAlphabet[(value >> 6) & 0x3f];
        if (o >= needed) break;
        out[o++] = kAlphabet[value & 0x3f];
    }
    out[o] = 0;
    return o;
}

// ---- 加密 / crypto ----------------------------------------------------------
static bool sha256_bytes(const uint8_t* in, size_t length, uint8_t out[32]) {
    size_t written = 0;
    return psa_crypto_init() == PSA_SUCCESS &&
           psa_hash_compute(PSA_ALG_SHA_256, in, length, out, 32, &written) == PSA_SUCCESS &&
           written == 32;
}

static bool hmac_sha256_hex(const char* key, const char* message, char out[65]) {
    uint8_t mac[32];
    size_t length = 0;
    if (psa_crypto_init() != PSA_SUCCESS) return false;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, strlen(key) * 8);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_key_id_t id = 0;
    psa_status_t status = psa_import_key(&attributes, (const uint8_t*)key, strlen(key), &id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) return false;
    status = psa_mac_compute(id, PSA_ALG_HMAC(PSA_ALG_SHA_256), (const uint8_t*)message,
                             strlen(message), mac, sizeof(mac), &length);
    psa_destroy_key(id);
    if (status != PSA_SUCCESS || length != 32) return false;
    return xteink_hex_encode(mac, 32, out, 65) == 64;
}

static bool aes_gcm_seal(const uint8_t key[32], const uint8_t iv[12], const char* plain,
                         uint8_t* sealed, size_t sealed_cap, size_t* sealed_length) {
    if (psa_crypto_init() != PSA_SUCCESS) return false;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    psa_key_id_t id = 0;
    psa_status_t status = psa_import_key(&attributes, key, 32, &id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) return false;
    status = psa_aead_encrypt(id, PSA_ALG_GCM, iv, 12, NULL, 0, (const uint8_t*)plain,
                              strlen(plain), sealed, sealed_cap, sealed_length);
    psa_destroy_key(id);
    return status == PSA_SUCCESS;
}

// ---- 状态文件 / state file ---------------------------------------------------
static bool state_path(char* out, size_t cap, const char* suffix) {
    if (!s_state.root[0]) return false;
    int n = snprintf(out, cap, "%s" XTEINK_SYNC_STATE_DIR "%s", s_state.root, suffix ? suffix : "");
    return n > 0 && (size_t)n < cap;
}

static void ensure_parent_dirs(void) {
    char dir[BOOK_STORE_PATH_MAX + 64];
    if (!s_state.root[0]) return;
    snprintf(dir, sizeof(dir), "%s/.readpico", s_state.root);
    mkdir(dir, 0777);
    snprintf(dir, sizeof(dir), "%s" XTEINK_SYNC_STATE_DIR, s_state.root);
    mkdir(dir, 0777);
}

static bool state_load(void) {
    if (s_state_loaded) return true;
    char path[BOOK_STORE_PATH_MAX + 64];
    if (!state_path(path, sizeof(path), "/xteink_sync.json")) return false;
    FILE* file = fopen(path, "rb");
    if (!file) {
        s_state_loaded = true;
        return false;
    }
    char* text = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!text) {
        fclose(file);
        return false;
    }
    size_t length = fread(text, 1, 4095, file);
    fclose(file);
    text[length] = 0;
    cJSON* root = cJSON_Parse(text);
    heap_caps_free(text);
    if (!root) return false;
    struct { const char* key; char* out; size_t cap; } fields[] = {
        {"chip_id", s_state.id.chip_id, sizeof(s_state.id.chip_id)},
        {"bluetooth_mac", s_state.id.bluetooth_mac, sizeof(s_state.id.bluetooth_mac)},
        {"wifi_mac", s_state.id.wifi_mac, sizeof(s_state.id.wifi_mac)},
        {"device_type", s_state.id.device_type, sizeof(s_state.id.device_type)},
        {"brand", s_state.id.brand, sizeof(s_state.id.brand)},
        {"firmware_name", s_state.id.firmware_name, sizeof(s_state.id.firmware_name)},
        {"version", s_state.id.version, sizeof(s_state.id.version)},
        {"access_token", s_state.access_token, sizeof(s_state.access_token)},
        {"refresh_token", s_state.refresh_token, sizeof(s_state.refresh_token)},
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        cJSON* item = cJSON_GetObjectItem(root, fields[i].key);
        if (cJSON_IsString(item) && item->valuestring)
            snprintf(fields[i].out, fields[i].cap, "%s", item->valuestring);
    }
    cJSON* expires = cJSON_GetObjectItem(root, "access_expires_at");
    if (cJSON_IsNumber(expires)) s_state.access_expires_at = (int64_t)expires->valuedouble;
    cJSON_Delete(root);
    s_state_loaded = true;
    return s_state.id.chip_id[0] != 0;
}

static bool state_save(void) {
    char dir[BOOK_STORE_PATH_MAX + 64], path[BOOK_STORE_PATH_MAX + 96];
    if (!state_path(dir, sizeof(dir), "")) return false;
    ensure_parent_dirs();
    if (!state_path(path, sizeof(path), "/xteink_sync.json")) return false;
    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "chip_id", s_state.id.chip_id);
    cJSON_AddStringToObject(root, "bluetooth_mac", s_state.id.bluetooth_mac);
    cJSON_AddStringToObject(root, "wifi_mac", s_state.id.wifi_mac);
    cJSON_AddStringToObject(root, "device_type", s_state.id.device_type);
    cJSON_AddStringToObject(root, "brand", s_state.id.brand);
    cJSON_AddStringToObject(root, "firmware_name", s_state.id.firmware_name);
    cJSON_AddStringToObject(root, "version", s_state.id.version);
    cJSON_AddStringToObject(root, "access_token", s_state.access_token);
    cJSON_AddStringToObject(root, "refresh_token", s_state.refresh_token);
    cJSON_AddNumberToObject(root, "access_expires_at", (double)s_state.access_expires_at);
    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return false;
    char tmp[BOOK_STORE_PATH_MAX + 96];
    if (!state_path(tmp, sizeof(tmp), "/xteink_sync.json.tmp")) {
        cJSON_free(text);
        return false;
    }
    FILE* file = fopen(tmp, "wb");
    bool ok = false;
    if (file) {
        ok = fwrite(text, 1, strlen(text), file) == strlen(text);
        ok = fclose(file) == 0 && ok;
    }
    cJSON_free(text);
    if (ok) ok = rename(tmp, path) == 0;
    return ok;
}

static void identity_init(void) {
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(s_state.id.wifi_mac, sizeof(s_state.id.wifi_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        char hex[13];
        xteink_hex_encode(mac, 6, hex, sizeof(hex));
        for (int i = 0; hex[i]; ++i)
            if (hex[i] >= 'a' && hex[i] <= 'f') hex[i] = (char)(hex[i] - 'a' + 'A');
        snprintf(s_state.id.chip_id, sizeof(s_state.id.chip_id), "%s", hex);
    }
    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK)
        snprintf(s_state.id.bluetooth_mac, sizeof(s_state.id.bluetooth_mac),
                 "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    else
        snprintf(s_state.id.bluetooth_mac, sizeof(s_state.id.bluetooth_mac), "%s", s_state.id.wifi_mac);
    snprintf(s_state.id.device_type, sizeof(s_state.id.device_type), "%s", "pico");
    snprintf(s_state.id.brand, sizeof(s_state.id.brand), "%s", "kiikoread");
    snprintf(s_state.id.firmware_name, sizeof(s_state.id.firmware_name), "%s", XTEINK_SYNC_FIRMWARE_NAME);
    const esp_app_desc_t* app = esp_app_get_description();
    snprintf(s_state.id.version, sizeof(s_state.id.version), "%s", app ? app->version : "0");
}

// ---- HTTP -------------------------------------------------------------------
static esp_err_t http_collect(esp_http_client_event_t* event) {
    http_sink_t* sink = (http_sink_t*)event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && sink) {
        if (sink->length + event->data_len >= sink->cap) {
            sink->overflow = true;
            return ESP_FAIL;
        }
        memcpy(sink->buf + sink->length, event->data, event->data_len);
        sink->length += event->data_len;
        sink->buf[sink->length] = 0;
    }
    return ESP_OK;
}

static bool http_request(const char* url, bool post, const char* body, const char* bearer,
                         char* response, size_t response_cap, int* status) {
    if (s_cancel) return false;
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = XTEINK_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .event_handler = http_collect,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    http_sink_t sink = {.buf = response, .cap = response_cap, .length = 0, .overflow = false};
    if (response && response_cap) response[0] = 0;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "User-Agent", "kiikoread/" XTEINK_SYNC_FIRMWARE_NAME);
    esp_http_client_set_header(client, "X-Chip-Id", s_state.id.chip_id);
    esp_http_client_set_header(client, "X-Wifi-Mac", s_state.id.wifi_mac);
    esp_http_client_set_header(client, "X-Bluetooth-Mac", s_state.id.bluetooth_mac);
    if (bearer && bearer[0]) {
        char header[560];
        snprintf(header, sizeof(header), "Bearer %s", bearer);
        esp_http_client_set_header(client, "Authorization", header);
    }
    esp_http_client_set_method(client, post ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    esp_http_client_set_post_field(client, body, body ? (int)strlen(body) : 0);
    esp_http_client_set_user_data(client, &sink);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_err_t err = esp_http_client_perform(client);
    *status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && !sink.overflow;
}

// ---- 二维码信封 / bind envelope ---------------------------------------------
static bool build_bind_envelope(uint32_t timestamp, char* out, size_t cap) {
    uint8_t nonce[8];
    esp_fill_random(nonce, sizeof(nonce));
    char nonce_hex[17];
    xteink_hex_encode(nonce, sizeof(nonce), nonce_hex, sizeof(nonce_hex));

    char payload[320];
    int payload_length = snprintf(
        payload, sizeof(payload),
        "{\"chip_id\":\"%s\",\"bluetooth_mac\":\"%s\",\"wifi_mac\":\"%s\",\"device_type\":\"%s\","
        "\"brand\":\"%s\",\"version\":\"%s\",\"timestamp\":%u,\"nonce\":\"%s\"}",
        s_state.id.chip_id, s_state.id.bluetooth_mac, s_state.id.wifi_mac, s_state.id.device_type,
        s_state.id.brand, s_state.id.version, (unsigned)timestamp, nonce_hex);
    if (payload_length <= 0 || (size_t)payload_length >= sizeof(payload)) return false;

    uint8_t key[32];
    if (!sha256_bytes((const uint8_t*)XTEINK_APP_SECRET, strlen(XTEINK_APP_SECRET), key)) return false;
    uint8_t iv[12];
    esp_fill_random(iv, sizeof(iv));
    uint8_t sealed[384];
    size_t sealed_length = 0;
    if (!aes_gcm_seal(key, iv, payload, sealed, sizeof(sealed), &sealed_length)) return false;
    if (sealed_length < 16) return false;

    char payload_b64[512], iv_b64[24], tag_b64[32];
    if (!xteink_base64url_encode(sealed, sealed_length - 16, payload_b64, sizeof(payload_b64)))
        return false;
    if (!xteink_base64url_encode(iv, sizeof(iv), iv_b64, sizeof(iv_b64))) return false;
    if (!xteink_base64url_encode(sealed + sealed_length - 16, 16, tag_b64, sizeof(tag_b64)))
        return false;

    char message[700];
    snprintf(message, sizeof(message), "%s.%s.%s.%s", XTEINK_APP_ID, payload_b64, iv_b64, tag_b64);
    char signature[65];
    if (!hmac_sha256_hex(XTEINK_APP_SECRET, message, signature)) return false;

    int n = snprintf(out, cap,
                     "{\"appid\":\"%s\",\"firmware_name\":\"%s\",\"payload\":\"%s\",\"iv\":\"%s\","
                     "\"tag\":\"%s\",\"sign\":\"%s\"}",
                     XTEINK_APP_ID, s_state.id.firmware_name, payload_b64, iv_b64, tag_b64,
                     signature);
    return n > 0 && (size_t)n < cap;
}

static const char* json_string(cJSON* object, const char* key) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : NULL;
}

static cJSON* json_find(cJSON* node, const char* key) {
    if (!node) return NULL;
    cJSON* item = cJSON_GetObjectItem(node, key);
    if (item) return item;
    for (cJSON* child = node->child; child; child = child->next) {
        if (cJSON_IsObject(child) || cJSON_IsArray(child)) {
            cJSON* found = json_find(child, key);
            if (found) return found;
        }
    }
    return NULL;
}

// ---- WiFi -------------------------------------------------------------------
static bool ensure_network(void) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (status.network_ready) return true;
    char ssid[33];
    bool configured = false;
    read_pico_transfer_get_saved_wifi(ssid, &configured);
    if (!configured) {
        publish(XTEINK_SYNC_FAILED, "请先连接 WiFi（设置→网络）");
        return false;
    }
    if (status.state != READ_PICO_TRANSFER_STOPPED) read_pico_transfer_stop();
    read_pico_transfer_cfg_t cfg = {.mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true};
    if (read_pico_transfer_start(&cfg) != ESP_OK) {
        publish(XTEINK_SYNC_FAILED, "WiFi 连接启动失败");
        return false;
    }
    s_owns_network = true;
    for (int i = 0; i < 300 && !s_cancel; ++i) {
        read_pico_transfer_service_poll();
        read_pico_transfer_get_status(&status);
        if (status.network_ready) return true;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    publish(s_cancel ? XTEINK_SYNC_CANCELLED : XTEINK_SYNC_FAILED,
            s_cancel ? "已取消" : "WiFi 连接超时");
    return false;
}

static uint32_t current_time_or_zero(void) {
    uint32_t utc = 0;
    if (read_pico_transfer_sync_time_online(&utc) == ESP_OK && utc > 1700000000u) return utc;
    time_t now = time(NULL);
    if (now > 1700000000) return (uint32_t)now;
    return 0;
}

// ---- 令牌 / token -----------------------------------------------------------
static bool request_token(char* qr, size_t qr_cap, int* poll_interval, bool* bound) {
    char* response = heap_caps_calloc(1, XTEINK_RESPONSE_MAX, MALLOC_CAP_SPIRAM);
    if (!response) {
        publish(XTEINK_SYNC_FAILED, "内存不足");
        return false;
    }
    for (;;) {
        if (s_cancel) return false;
        uint32_t timestamp = current_time_or_zero();
        if (!timestamp) {
            heap_caps_free(response);
            publish(XTEINK_SYNC_FAILED, "系统时间未同步，请联网后重试");
            return false;
        }
        uint8_t nonce[16];
        esp_fill_random(nonce, sizeof(nonce));
        char nonce_hex[33];
        xteink_hex_encode(nonce, sizeof(nonce), nonce_hex, sizeof(nonce_hex));
        char message[256];
        snprintf(message, sizeof(message), "%s.%s.%s.%s.%u.%s", XTEINK_APP_ID,
                 s_state.id.chip_id, s_state.id.bluetooth_mac, s_state.id.wifi_mac,
                 (unsigned)timestamp, nonce_hex);
        char signature[65];
        if (!hmac_sha256_hex(XTEINK_APP_SECRET, message, signature)) {
            publish(XTEINK_SYNC_FAILED, "签名失败");
            return false;
        }
        char body[384];
        snprintf(body, sizeof(body),
                 "{\"appid\":\"%s\",\"timestamp\":%u,\"nonce\":\"%s\",\"sign\":\"%s\"}",
                 XTEINK_APP_ID, (unsigned)timestamp, nonce_hex, signature);
        int status = 0;
        if (!http_request(XTEINK_BASE_URL XTEINK_TOKEN_PATH, true, body, NULL, response,
                          XTEINK_RESPONSE_MAX, &status)) {
            heap_caps_free(response);
            publish(XTEINK_SYNC_FAILED, "网络请求失败");
            return false;
        }
        cJSON* root = cJSON_Parse(response);
        if (!root) {
            heap_caps_free(response);
            publish(XTEINK_SYNC_FAILED, "响应解析失败");
            return false;
        }
        cJSON* token = json_find(root, "access_token");
        if (status == 200 && cJSON_IsString(token)) {
            snprintf(s_state.access_token, sizeof(s_state.access_token), "%s", token->valuestring);
            cJSON* refresh = json_find(root, "refresh_token");
            if (cJSON_IsString(refresh))
                snprintf(s_state.refresh_token, sizeof(s_state.refresh_token), "%s", refresh->valuestring);
            cJSON* expires = json_find(root, "expires_in");
            int64_t seconds = cJSON_IsNumber(expires) ? (int64_t)expires->valuedouble : 3600;
            s_state.access_expires_at = (int64_t)time(NULL) + seconds;
            state_save();
            cJSON_Delete(root);
            heap_caps_free(response);
            *bound = true;
            return true;
        }
        cJSON* poll = json_find(root, "poll_interval");
        *poll_interval = cJSON_IsNumber(poll) ? (int)poll->valuedouble : 5;
        if (*poll_interval < 2) *poll_interval = 2;
        if (*poll_interval > 30) *poll_interval = 30;
        const char* note = json_string(root, "message");
        publish(XTEINK_SYNC_WAITING, note && note[0] ? note : "等待手机扫码绑定");
        cJSON_Delete(root);
        if (!qr[0]) {
            uint32_t timestamp2 = current_time_or_zero();
            if (timestamp2 && build_bind_envelope(timestamp2, qr, qr_cap)) {
                set_qr(qr);
            } else {
                heap_caps_free(response);
                publish(XTEINK_SYNC_FAILED, "二维码生成失败");
                return false;
            }
        }
        for (int waited = 0; waited < *poll_interval * 10 && !s_cancel; ++waited)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (s_cancel) return false;
    }
}

// ---- 下载与导入 / download and import --------------------------------------
static xteink_task_t* s_tasks;
static int s_task_count;
static int s_work_mode;   // 0 = 只看列表 / list only, 1 = 下载一本 / one download
static int s_work_index;
static int s_active_index = -1;

int xteink_sync_task_count(void) {
    return s_task_count;
}

bool xteink_sync_task_get(int index, xteink_task_t* out) {
    if (!out || index < 0 || index >= s_task_count || !s_tasks) return false;
    *out = s_tasks[index];
    return true;
}

static bool tasks_alloc(void) {
    if (!s_tasks)
        s_tasks = heap_caps_calloc(XTEINK_SYNC_TASK_MAX, sizeof(xteink_task_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_tasks != NULL;
}

static bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

typedef enum {
    XTEINK_FILE_EPUB = 0,
    XTEINK_FILE_FONT,
    XTEINK_FILE_TEXT,
    XTEINK_FILE_OTHER,
} xteink_file_kind_t;

static xteink_file_kind_t file_kind(const char* name) {
    const char* dot = name ? strrchr(name, '.') : NULL;
    if (!dot) return XTEINK_FILE_OTHER;
    if (!strcasecmp(dot, ".epub")) return XTEINK_FILE_EPUB;
    if (!strcasecmp(dot, ".ttf") || !strcasecmp(dot, ".otf")) return XTEINK_FILE_FONT;
    if (!strcasecmp(dot, ".txt")) return XTEINK_FILE_TEXT;
    return XTEINK_FILE_OTHER;
}

static bool verify_font(const char* path) {
    FILE* file = fopen(path, "rb");
    uint8_t header[4] = {0};
    size_t length = file ? fread(header, 1, sizeof(header), file) : 0;
    if (file) fclose(file);
    if (length < 4) return false;
    // sfnt: TrueType 0x00010000、"OTTO"、旧式 "true"、字体集合 "ttcf"。
    // sfnt magic: TrueType 0x00010000, "OTTO", legacy "true", or collection "ttcf".
    if (!memcmp(header, "OTTO", 4) || !memcmp(header, "true", 4) || !memcmp(header, "ttcf", 4))
        return true;
    return header[0] == 0 && header[1] == 1 && header[2] == 0 && header[3] == 0;
}

static bool verify_text(const char* path) {
    struct stat st;
    if (stat(path, &st) || st.st_size <= 0) return false;
    FILE* file = fopen(path, "rb");
    uint8_t buffer[1024];
    size_t length = file ? fread(buffer, 1, sizeof(buffer), file) : 0;
    if (file) fclose(file);
    for (size_t i = 0; i < length; ++i)
        if (buffer[i] == 0) return false;
    return length > 0;
}

static bool verify_epub(const char* path) {
    zip_reader_t* reader = NULL;
    if (zip_open(path, &reader) != ESP_OK || !reader) return false;
    bool ok = false;
    int index = zip_find(reader, "mimetype");
    if (index < 0) index = zip_find(reader, "/mimetype");
    if (index >= 0) {
        char mime[64] = {0};
        if (zip_extract(reader, index, mime, sizeof(mime) - 1) == ESP_OK)
            ok = strncmp(mime, "application/epub+zip", 20) == 0;
    }
    zip_close(reader);
    return ok;
}

static bool join_name(char* out, size_t cap, const char* dir, const char* name) {
    size_t dir_length = strlen(dir);
    size_t name_length = strlen(name);
    if (dir_length + name_length + 2 > cap) return false;
    memcpy(out, dir, dir_length);
    out[dir_length] = '/';
    memcpy(out + dir_length + 1, name, name_length + 1);
    return true;
}

static void unique_name(const char* dir, const char* name, char* out, size_t cap) {
    if (!join_name(out, cap, dir, name)) {
        out[0] = 0;
        return;
    }
    if (!file_exists(out)) return;
    const char* dot = strrchr(name, '.');
    size_t stem = dot ? (size_t)(dot - name) : strlen(name);
    for (int i = 2; i < 100; ++i) {
        char numbered[64];
        snprintf(numbered, sizeof(numbered), "%.*s (%d)%s", (int)stem, name, i, dot ? dot : "");
        if (join_name(out, cap, dir, numbered)) {
            if (!file_exists(out)) return;
        }
    }
    out[0] = 0;
}

typedef struct {
    FILE* file;
    size_t written;
    bool failed;
} download_sink_t;

static void touch_revision(void) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ++s_status.revision;
    xSemaphoreGive(s_lock);
}

static esp_err_t download_event(esp_http_client_event_t* event) {
    download_sink_t* sink = (download_sink_t*)event->user_data;
    if (!sink) return ESP_OK;
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        if (s_cancel) {
            sink->failed = true;
            return ESP_FAIL;
        }
        if (event->data_len > 0 &&
            fwrite(event->data, 1, (size_t)event->data_len, sink->file) != (size_t)event->data_len) {
            sink->failed = true;
            return ESP_FAIL;
        }
        sink->written += (size_t)event->data_len;
        // 进度条只在跨过 10% 时触发一次重绘，避免每块数据都刷屏。
        // Redraw at most once per 10% to keep e-ink traffic sane.
        if (s_active_index >= 0 && s_active_index < s_task_count && s_tasks) {
            xteink_task_t* item = &s_tasks[s_active_index];
            item->received = (uint32_t)sink->written;
            uint32_t percent = item->size ? (uint32_t)((uint64_t)sink->written * 100u / item->size) : 0;
            static uint32_t last_bucket;
            if (percent / 10 != last_bucket / 10) {
                last_bucket = percent;
                touch_revision();
            }
        }
    }
    return ESP_OK;
}

// 0 = 成功，1 = 下载失败，2 = 校验失败，3 = 写入失败，4 = 不支持的类型
static int download_file(const char* url, const char* save_path, const char* incoming,
                         char* saved_path, size_t saved_cap) {
    if (!url || strncmp(url, "https://", 8)) return 1;
    const char* name = save_path && save_path[0] ? strrchr(save_path, '/') : NULL;
    name = name ? name + 1 : (save_path && save_path[0] ? save_path : "book.epub");
    if (!name[0] || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) return 1;
    const xteink_file_kind_t kind = file_kind(name);
    if (kind == XTEINK_FILE_OTHER) {
        ESP_LOGE(TAG, "unsupported file type: %s", name);
        return 4;
    }

    // 只编码路径：scheme://host 与查询串保持原样，否则 "https://" 会被编码坏。
    // Encode the path only; keep scheme://host and the query intact.
    char encoded[768];
    const char* scheme_end = strstr(url, "://");
    const char* path = scheme_end ? strchr(scheme_end + 3, '/') : NULL;
    size_t prefix = path ? (size_t)(path - url) : strlen(url);
    if (prefix >= sizeof(encoded)) return 1;
    memcpy(encoded, url, prefix);
    encoded[prefix] = 0;
    if (path) {
        const char* query = strchr(path, '?');
        size_t path_length = query ? (size_t)(query - path) : strlen(path);
        char path_buf[640];
        if (path_length >= sizeof(path_buf)) return 1;
        memcpy(path_buf, path, path_length);
        path_buf[path_length] = 0;
        char encoded_path[768];
        if (!xteink_url_encode_path(path_buf, encoded_path, sizeof(encoded_path))) return 1;
        if (prefix + strlen(encoded_path) + (query ? strlen(query) : 0) + 1 >= sizeof(encoded))
            return 1;
        strcat(encoded, encoded_path);
        if (query) strcat(encoded, query);
    }

    char temp[BOOK_STORE_PATH_MAX + 96];
    if (snprintf(temp, sizeof(temp), "%s/download.part", incoming) >= (int)sizeof(temp)) return 1;
    mkdir(incoming, 0777);

    FILE* file = fopen(temp, "wb");
    if (!file) {
        ESP_LOGE(TAG, "cannot open download temp: %s", temp);
        return 1;
    }
    download_sink_t ctx = {.file = file, .written = 0, .failed = false};
    esp_http_client_config_t config = {
        .url = encoded,
        .timeout_ms = XTEINK_HTTP_TIMEOUT_MS * 3,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .event_handler = download_event,
        .user_data = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        fclose(file);
        unlink(temp);
        return 1;
    }
    esp_http_client_set_header(client, "User-Agent", "kiikoread/" XTEINK_SYNC_FIRMWARE_NAME);
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || ctx.failed || ctx.written == 0) {
        ESP_LOGE(TAG, "download failed: err=%s status=%d written=%u failed=%d",
                 esp_err_to_name(err), status, (unsigned)ctx.written, ctx.failed ? 1 : 0);
        fclose(file);
        unlink(temp);
        return 1;
    }
    if (fclose(file) != 0) {
        unlink(temp);
        return 3;
    }
    bool verified = kind == XTEINK_FILE_EPUB   ? verify_epub(temp)
                    : kind == XTEINK_FILE_FONT ? verify_font(temp)
                                               : verify_text(temp);
    if (!verified) {
        ESP_LOGE(TAG, "verify failed: %s kind=%d", name, (int)kind);
        unlink(temp);
        return 2;
    }
    const char* target_dir = kind == XTEINK_FILE_FONT ? app_settings_fonts_dir() : s_state.root;
    if (!target_dir || !target_dir[0]) target_dir = s_state.root;
    mkdir(target_dir, 0777);
    unique_name(target_dir, name, saved_path, saved_cap);
    if (!saved_path[0] || rename(temp, saved_path) != 0) {
        ESP_LOGE(TAG, "rename failed: %s", saved_path);
        unlink(temp);
        return 3;
    }
    return 0;
}

static void complete_task(const char* task_id, bool success, const char* error) {
    if (!task_id || !task_id[0]) return;
    char url[256];
    snprintf(url, sizeof(url), "%s" XTEINK_TASK_PATH "%s/complete", XTEINK_BASE_URL, task_id);
    char body[256];
    snprintf(body, sizeof(body),
             "{\"success\":%s,\"completed\":%s,\"error_msg\":\"%s\",\"retry_count\":0}",
             success ? "true" : "false", success ? "true" : "false", error ? error : "");
    char response[512];
    int status = 0;
    if (http_request(url, true, body, s_state.access_token, response, sizeof(response), &status))
        ESP_LOGI(TAG, "complete %s -> %d", task_id, status);
}

static void pull_tasks(void) {
    publish(XTEINK_SYNC_PULLING, "正在拉取云端列表");
    char* response = heap_caps_calloc(1, XTEINK_RESPONSE_MAX, MALLOC_CAP_SPIRAM);
    if (!response) {
        publish(XTEINK_SYNC_FAILED, "内存不足");
        return;
    }
    int status = 0;
    bool ok = http_request(XTEINK_BASE_URL XTEINK_PULL_PATH, false, NULL, s_state.access_token,
                           response, XTEINK_RESPONSE_MAX, &status);
    if (ok && status == 401) {
        // 令牌过期：用签名重新换一次令牌，再重试一次拉取。
        // Expired token: obtain a fresh one with the signed request, then retry once.
        ESP_LOGI(TAG, "token expired, refreshing");
        s_state.access_token[0] = 0;
        char qr[XTEINK_ENVELOPE_MAX] = {0};
        int interval = 5;
        bool bound = false;
        if (request_token(qr, sizeof(qr), &interval, &bound) && bound) {
            ok = http_request(XTEINK_BASE_URL XTEINK_PULL_PATH, false, NULL, s_state.access_token,
                              response, XTEINK_RESPONSE_MAX, &status);
        }
    }
    if (!ok || status != 200) {
        ESP_LOGE(TAG, "pull failed: status=%d body=%.160s", status, response);
        heap_caps_free(response);
        publish(XTEINK_SYNC_FAILED,
                status == 401 ? "绑定已过期，请解绑后重新扫码" : "拉取列表失败");
        return;
    }
    cJSON* root = cJSON_Parse(response);
    heap_caps_free(response);
    if (!root) {
        publish(XTEINK_SYNC_FAILED, "列表解析失败");
        return;
    }
    int count = 0;
    cJSON* tasks = json_find(root, "tasks");
    if (tasks && cJSON_IsArray(tasks) && tasks_alloc()) {
        cJSON* task = NULL;
        cJSON_ArrayForEach(task, tasks) {
            if (count >= XTEINK_SYNC_TASK_MAX) break;
            const char* task_id = json_string(task, "task_id");
            const char* url = json_string(task, "file_url");
            if (!task_id || !url) continue;
            xteink_task_t* item = &s_tasks[count];
            memset(item, 0, sizeof(*item));
            snprintf(item->task_id, sizeof(item->task_id), "%s", task_id);
            snprintf(item->file_url, sizeof(item->file_url), "%s", url);
            const char* save = json_string(task, "save_path");
            if (!save || !save[0]) {
                const char* slash = strrchr(url, '/');
                save = slash ? slash + 1 : url;
            }
            snprintf(item->save_path, sizeof(item->save_path), "%.240s", save);
            const char* name = strrchr(item->save_path, '/');
            snprintf(item->name, sizeof(item->name), "%.140s", name ? name + 1 : item->save_path);
            item->is_font = !!strcasestr(item->name, ".ttf") || !!strcasestr(item->name, ".otf");
            cJSON* size = json_find(task, "size");
            if (cJSON_IsNumber(size)) item->size = (uint32_t)size->valuedouble;
            ++count;
        }
    }
    cJSON_Delete(root);
    s_task_count = count;
    publish_book(NULL, count);
    char message[96];
    if (count)
        snprintf(message, sizeof(message), "云端待下载 %d 本，点选一本", count);
    else
        snprintf(message, sizeof(message), "云端没有待下载的图书");
    publish(XTEINK_SYNC_DONE, message);
}

static bool download_one(int index) {
    if (index < 0 || index >= s_task_count || !s_tasks) {
        publish(XTEINK_SYNC_FAILED, "任务已失效，请刷新列表");
        return false;
    }
    xteink_task_t item = s_tasks[index];
    s_tasks[index].downloading = true;
    s_tasks[index].failed = false;
    s_tasks[index].imported = false;
    s_tasks[index].received = 0;
    s_tasks[index].error[0] = 0;
    publish(XTEINK_SYNC_DOWNLOADING, "正在下载");
    publish_book(item.name, -1);
    if (!ensure_network()) {
        s_tasks[index].downloading = false;
        s_tasks[index].failed = true;
        snprintf(s_tasks[index].error, sizeof(s_tasks[index].error), "网络失败");
        return false;
    }
    char incoming[BOOK_STORE_PATH_MAX + 64];
    if (!state_path(incoming, sizeof(incoming), "/incoming")) {
        s_tasks[index].downloading = false;
        s_tasks[index].failed = true;
        snprintf(s_tasks[index].error, sizeof(s_tasks[index].error), "存储失败");
        publish(XTEINK_SYNC_FAILED, "存储路径失败");
        return false;
    }
    char saved[BOOK_STORE_PATH_MAX + 64];
    s_active_index = index;
    int reason = download_file(item.file_url, item.save_path, incoming, saved, sizeof(saved));
    s_active_index = -1;
    if (reason != 0) {
        const char* text = reason == 4   ? "不支持的类型"
                           : reason == 2 ? (item.is_font ? "字体校验失败" : "校验失败")
                                         : "下载失败";
        s_tasks[index].downloading = false;
        s_tasks[index].failed = true;
        snprintf(s_tasks[index].error, sizeof(s_tasks[index].error), "%s", text);
        publish(XTEINK_SYNC_FAILED, text);
        complete_task(item.task_id, false, reason == 2 ? "verify failed" : "download failed");
        return false;
    }
    s_tasks[index].downloading = false;
    s_tasks[index].failed = false;
    s_tasks[index].imported = true;
    s_tasks[index].received = s_tasks[index].size ? s_tasks[index].size : s_tasks[index].received;
    ++s_import_count;
    complete_task(item.task_id, true, "");
    publish(XTEINK_SYNC_DONE, item.is_font ? "已导入字体" : "已导入 1 本图书");
    return true;
}

// ---- 后台任务 / worker -------------------------------------------------------
static void worker(void* argument) {
    (void)argument;
    int mode = s_work_mode;
    int index = s_work_index;
    if (mode == 1) {
        download_one(index);
    } else {
        time_t now = time(NULL);
        if (s_state.access_token[0] && s_state.access_expires_at > 0 && now > 1700000000 &&
            (int64_t)now >= s_state.access_expires_at)
            s_state.access_token[0] = 0;
        publish(s_state.access_token[0] ? XTEINK_SYNC_PULLING : XTEINK_SYNC_BINDING,
                s_state.access_token[0] ? "正在拉取列表" : "正在申请绑定");
        if (ensure_network()) {
            if (!s_state.access_token[0]) {
                char qr[XTEINK_ENVELOPE_MAX] = {0};
                int interval = 5;
                bool bound = false;
                if (request_token(qr, sizeof(qr), &interval, &bound) && bound)
                    publish(XTEINK_SYNC_PULLING, "正在拉取列表");
            }
            if (s_state.access_token[0]) pull_tasks();
        }
    }
    if (s_owns_network) {
        read_pico_transfer_stop();
        s_owns_network = false;
    }
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.busy = false;
    if (s_status.state != XTEINK_SYNC_FAILED && s_status.state != XTEINK_SYNC_CANCELLED)
        s_status.state = XTEINK_SYNC_DONE;
    ++s_status.revision;
    if (s_lock) xSemaphoreGive(s_lock);
    s_task = NULL;
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

// ---- 公开 API / public API ---------------------------------------------------
static void init_once(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_done) s_done = xSemaphoreCreateBinary();
}

static void load_root(void) {
    book_store_root_t root;
    if (book_store_upload_root(&root) == ESP_OK) {
        snprintf(s_state.root, sizeof(s_state.root), "%s", root.path);
    }
}

bool xteink_sync_bound(void) {
    init_once();
    if (!s_state.root[0]) load_root();
    if (!s_state_loaded) state_load();
    return s_state.access_token[0] != 0;
}

static esp_err_t start_work(int mode, int index) {
    init_once();
    if (!s_lock || !s_done) return ESP_ERR_NO_MEM;
    if (s_task) return ESP_ERR_INVALID_STATE;
    if (!s_state.root[0]) load_root();
    if (!s_state.root[0]) {
        publish(XTEINK_SYNC_FAILED, "没有可用的存储");
        return ESP_ERR_NOT_FOUND;
    }
    if (!state_load()) {
        identity_init();
        state_save();
    }
    s_cancel = false;
    s_work_mode = mode;
    s_work_index = index;
    while (s_done && xSemaphoreTake(s_done, 0) == pdTRUE) {
    }
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = mode == 1 ? XTEINK_SYNC_DOWNLOADING
                               : (s_state.access_token[0] ? XTEINK_SYNC_PULLING : XTEINK_SYNC_BINDING);
    s_status.busy = true;
    s_status.imported = 0;
    s_status.revision++;
    if (s_lock) xSemaphoreGive(s_lock);
    BaseType_t created = xTaskCreate(worker, "xteink_sync", 12288, NULL, 5, &s_task);
    if (created != pdPASS) {
        s_task = NULL;
        publish(XTEINK_SYNC_FAILED, "后台任务创建失败");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t xteink_sync_start(void) {
    return start_work(0, -1);
}

esp_err_t xteink_sync_download(int index) {
    if (index < 0 || index >= s_task_count) return ESP_ERR_INVALID_ARG;
    return start_work(1, index);
}

esp_err_t xteink_sync_unbind(void) {
    init_once();
    xteink_sync_cancel_join();
    if (!s_state.root[0]) load_root();
    s_state.access_token[0] = 0;
    s_state.refresh_token[0] = 0;
    s_state.access_expires_at = 0;
    s_task_count = 0;
    state_save();
    publish(XTEINK_SYNC_IDLE, "已解绑，请重新扫码");
    return ESP_OK;
}

bool xteink_sync_busy(void) {
    return s_task != NULL;
}

void xteink_sync_get_status(xteink_sync_status_t* status) {
    if (!status) return;
    init_once();
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    if (s_lock) xSemaphoreGive(s_lock);
    status->busy = s_task != NULL;
    if (!s_state_loaded) state_load();
    status->bound = s_state.access_token[0] != 0;
}

void xteink_sync_cancel_join(void) {
    if (!s_task) return;
    s_cancel = true;
    if (s_done) xSemaphoreTake(s_done, pdMS_TO_TICKS(3000));
    s_cancel = false;
}

esp_err_t xteink_sync_forget(void) {
    return xteink_sync_unbind();
}

unsigned xteink_sync_import_count(void) {
    return s_import_count;
}

/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户2026-11-10：配网成功后才生成绑定码；每轮未绑定请求先校准在线UTC，防止旧RTC参与二维码/签名。
 * User2026-11-10: wait for WiFi before QR generation; obtain fresh online UTC for each unbound attempt, avoiding stale RTC signatures.
 * 用户2026-11-10下载重启：真机确认xteink_sync栈溢出；任务快照改PSRAM，绑定帧与下载分离，TLS工作栈24KiB。
 * User2026-11-10: measured xteink_sync stack overflow during download; keep snapshots in PSRAM, isolate bind frames, and retain24KiB internal TLS-worker stack.
 * 中文：星曈云同步设备端实现：二维码信封、签名令牌、任务下载与导入。
 * English: Xteink cloud sync implementation: bind envelope, signed token, task download/import.
 *
 * 冻结：只连官方 HTTPS；身份走请求头；令牌只落卡；校验失败不上架；不静默关闭 TLS/SHA 校验。
 * Frozen: official HTTPS only; identity in headers; tokens stay on the card; failed checks never
 *      reach the shelf; TLS and integrity checks are never silently disabled.
 */

#include "book_xteink_sync.h"
// 网络/页面控制按体积编译，保留Pico原4MiB应用槽；协议、校验与任务时序不变。
// Size-optimize IO/page control to retain Pico's4MiB slots without changing protocol,verification or task timing.
#if defined(ESP_PLATFORM) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("Os")
#endif

#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
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
#include "esp_timer.h"
#include "esp_tls_errors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "read_pico_transfer.h"
#include "read_pico_sd.h"
#include "zip_reader.h"

#include "book_cover.h"
#include "book_store.h"
#include "settings.h"

#define XTEINK_BASE_URL "https://api-prod.xteink.cn"
#define XTEINK_APP_ID "cl-20260508"
#define XTEINK_APP_SECRET "3d3f2e6d-9745-79eb-ad19-9b33e5b4a424"
#define XTEINK_TOKEN_PATH "/api/v1/third-party-device/token"
#define XTEINK_PULL_PATH "/api/v1/third-party-device/tasks/pull?limit=20"
#define XTEINK_TASK_PATH "/api/v1/third-party-device/tasks/"
#define XTEINK_HTTP_TIMEOUT_MS 5000
#define XTEINK_DOWNLOAD_TIMEOUT_MS 30000
#define XTEINK_RESPONSE_MAX (16U * 1024U)
#define XTEINK_LIST_RESPONSE_MAX (64U * 1024U)
#define XTEINK_ENVELOPE_MAX 1024U
#define XTEINK_PICTURES_DIR "/sdcard/Pictures"

static const char* TAG = "xteink";

typedef struct {
    char chip_id[24];
    char bluetooth_mac[20];
    char wifi_mac[20];
    char device_type[32];
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
static atomic_bool s_cancel, s_running, s_bound;
static bool s_owns_network;
static uint32_t s_bind_utc;
static int64_t s_bind_clock_us;
static atomic_uint s_import_count;
static xteink_sync_status_t s_status = {.state = XTEINK_SYNC_IDLE};
static xteink_state_t s_state;
static bool s_state_loaded;
static char s_http_error[96];
static char s_download_error[96];

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

static bool state_file_path(char *out,size_t cap,const char *extension) {
    char suffix[80];
    if(!s_state.id.chip_id[0])return false;
    snprintf(suffix,sizeof(suffix),"/xteink_%s.json%s",s_state.id.chip_id,extension?extension:"");
    return state_path(out,cap,suffix);
}
static bool state_load(void) {
    if(s_state_loaded)return true;
    for(int backup=0;backup<2;++backup) {
        char path[BOOK_STORE_PATH_MAX+96];
        if(!state_file_path(path,sizeof(path),backup?".previous":""))return false;
        FILE *file=fopen(path,"rb");if(!file)continue;
        char *text=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM);
        if(!text){fclose(file);return false;}
        size_t length=fread(text,1,4095,file);bool complete=!ferror(file)&&fgetc(file)==EOF;
        fclose(file);text[length]=0;
        cJSON *object=complete?cJSON_Parse(text):NULL;heap_caps_free(text);
        if(!object)continue;
        cJSON *chip=cJSON_GetObjectItem(object,"chip_id");
        bool same=cJSON_IsString(chip)&&!strcmp(chip->valuestring,s_state.id.chip_id);
        if(same) {
            cJSON *token=cJSON_GetObjectItem(object,"access_token");
            cJSON *refresh=cJSON_GetObjectItem(object,"refresh_token");
            cJSON *expires=cJSON_GetObjectItem(object,"access_expires_at");
            if(cJSON_IsString(token)&&strlen(token->valuestring)<sizeof(s_state.access_token))
                snprintf(s_state.access_token,sizeof(s_state.access_token),"%s",token->valuestring);
            if(cJSON_IsString(refresh)&&strlen(refresh->valuestring)<sizeof(s_state.refresh_token))
                snprintf(s_state.refresh_token,sizeof(s_state.refresh_token),"%s",refresh->valuestring);
            if(cJSON_IsNumber(expires))s_state.access_expires_at=(int64_t)expires->valuedouble;
        }
        cJSON_Delete(object);
        if(same){s_state_loaded=true;s_bound=s_state.access_token[0]!=0;return true;}
    }
    s_state_loaded=true;return false;
}

static bool state_save(void) {
    char dir[BOOK_STORE_PATH_MAX + 64], path[BOOK_STORE_PATH_MAX + 96];
    if (!state_path(dir, sizeof(dir), "")) return false;
    ensure_parent_dirs();
    if (!state_file_path(path, sizeof(path), "")) return false;
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
    if (!state_file_path(tmp, sizeof(tmp), ".tmp")) {
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
    char backup[BOOK_STORE_PATH_MAX+96];
    if(!ok||!state_file_path(backup,sizeof(backup),".previous")){unlink(tmp);return false;}
    struct stat st;bool previous=stat(path,&st)==0;
    if(previous) {
        unlink(backup);
        if(rename(path,backup)!=0){unlink(tmp);return false;}
    }
    ok=rename(tmp,path)==0;
    if(!ok){if(previous)(void)rename(backup,path);unlink(tmp);}
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
    snprintf(s_state.id.device_type, sizeof(s_state.id.device_type), "%s", XTEINK_SYNC_DEVICE_TYPE);
    snprintf(s_state.id.brand, sizeof(s_state.id.brand), "%s", "kiikoread");
    snprintf(s_state.id.firmware_name, sizeof(s_state.id.firmware_name), "%s", XTEINK_SYNC_FIRMWARE_NAME);
    const esp_app_desc_t* app = esp_app_get_description();
    snprintf(s_state.id.version, sizeof(s_state.id.version), "%s", app ? app->version : "0");
}

// ---- HTTP -------------------------------------------------------------------
static esp_err_t http_collect(esp_http_client_event_t* event) {
    http_sink_t* sink = (http_sink_t*)event->user_data;
    if(s_cancel)return ESP_FAIL;
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
    s_http_error[0]=0;
    if (s_cancel||!url||strncmp(url,"https://",8)) return false;
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = XTEINK_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .event_handler = http_collect,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client){snprintf(s_http_error,sizeof(s_http_error),"网络内存不足");return false;}
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
    if(err!=ESP_OK&&!s_cancel) {
        int tls_code=0,verify_flags=0;
        esp_err_t tls_error=esp_http_client_get_and_clear_last_tls_error(client,&tls_code,&verify_flags);
        if(verify_flags)snprintf(s_http_error,sizeof(s_http_error),"HTTPS证书校验失败 (0x%x)",verify_flags);
        else if(tls_error!=ESP_OK||tls_code)snprintf(s_http_error,sizeof(s_http_error),"HTTPS连接失败 (0x%x)",(unsigned)(tls_code?-tls_code:tls_error));
        else snprintf(s_http_error,sizeof(s_http_error),"网络连接失败 (0x%x)",(unsigned)err);
        ESP_LOGE(TAG,"request failed: err=0x%x tls=0x%x code=%d verify=0x%x",(unsigned)err,(unsigned)tls_error,tls_code,verify_flags);
    } else if(sink.overflow)snprintf(s_http_error,sizeof(s_http_error),"云端响应过长");
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

// 年份有效不代表签名时间新鲜；绑定必须使用本轮联网校时与单调时钟。
// A plausible year may still be stale; use this binding's online UTC plus monotonic elapsed time.
static uint32_t current_time_or_zero(void) {
    int64_t elapsed=(esp_timer_get_time()-s_bind_clock_us)/1000000;
    if(!s_bind_utc||elapsed<0||(uint64_t)elapsed>UINT32_MAX-s_bind_utc)return 0;
    return s_bind_utc+(uint32_t)elapsed;
}
static bool sync_bind_time(void) {
    uint32_t utc=0;s_bind_utc=0;
    publish(XTEINK_SYNC_BINDING,"正在同步绑定时间");
    if(!s_cancel&&read_pico_transfer_sync_time_online(&utc)==ESP_OK&&utc>1700000000u&&!s_cancel) {
        s_bind_utc=utc;s_bind_clock_us=esp_timer_get_time();return true;
    }
    publish(s_cancel?XTEINK_SYNC_CANCELLED:XTEINK_SYNC_FAILED,s_cancel?"已取消":"绑定校时失败，请重试");
    return false;
}

// 本地信封先于令牌请求发布；未绑定/HTTP失败也不能隐藏可扫码的身份。
// Publish the local envelope before requesting a token; unbound/HTTP errors must not hide it.
static bool prepare_bind_qr(char *qr,size_t cap,uint32_t timestamp) {
    if(qr[0])return true;
    if(!timestamp||!build_bind_envelope(timestamp,qr,cap)) {
        publish(XTEINK_SYNC_FAILED,"二维码生成失败");return false;
    }
    set_qr(qr);
    publish(XTEINK_SYNC_WAITING,"请用 App 扫码绑定");
    return true;
}

// ---- 令牌 / token -----------------------------------------------------------
static bool request_token(char* qr, size_t qr_cap, int* poll_interval, bool* bound) {
    char* response = heap_caps_calloc(1, XTEINK_RESPONSE_MAX, MALLOC_CAP_SPIRAM);
    if (!response) {
        publish(XTEINK_SYNC_FAILED, "内存不足");
        return false;
    }
    for (;;) {
        if (s_cancel) {heap_caps_free(response);return false;}
        uint32_t timestamp = current_time_or_zero();
        if (!timestamp) {
            heap_caps_free(response);
            publish(XTEINK_SYNC_FAILED, "系统时间未同步，请联网后重试");
            return false;
        }
        if(!prepare_bind_qr(qr,qr_cap,timestamp)){heap_caps_free(response);return false;}
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
            heap_caps_free(response);return false;
        }
        char body[384];
        snprintf(body, sizeof(body),
                 "{\"appid\":\"%s\",\"timestamp\":%u,\"nonce\":\"%s\",\"sign\":\"%s\"}",
                 XTEINK_APP_ID, (unsigned)timestamp, nonce_hex, signature);
        int status = 0;
        if (!http_request(XTEINK_BASE_URL XTEINK_TOKEN_PATH, true, body, NULL, response,
                          XTEINK_RESPONSE_MAX, &status)) {
            heap_caps_free(response);
            publish(s_cancel?XTEINK_SYNC_CANCELLED:XTEINK_SYNC_FAILED,s_cancel?"已取消":s_http_error[0]?s_http_error:"网络连接失败");
            return false;
        }
        if(status!=200&&status!=202) {
            heap_caps_free(response);
            char message[96];snprintf(message,sizeof(message),"绑定请求失败 (HTTP %d)",status);
            publish(XTEINK_SYNC_FAILED,message);return false;
        }
        cJSON* root = cJSON_Parse(response);
        if (!root) {
            heap_caps_free(response);
            publish(XTEINK_SYNC_FAILED, "响应解析失败");
            return false;
        }
        cJSON* token = json_find(root, "access_token");
        if (status == 200 && cJSON_IsString(token) && token->valuestring[0] &&
            strlen(token->valuestring)<sizeof(s_state.access_token)) {
            snprintf(s_state.access_token, sizeof(s_state.access_token), "%s", token->valuestring);
            cJSON* refresh = json_find(root, "refresh_token");
            if (cJSON_IsString(refresh))
                snprintf(s_state.refresh_token, sizeof(s_state.refresh_token), "%s", refresh->valuestring);
            cJSON* expires = json_find(root, "expires_in");
            int64_t seconds = cJSON_IsNumber(expires) ? (int64_t)expires->valuedouble : 3600;
            s_state.access_expires_at = (int64_t)time(NULL) + seconds;
            s_bound=true;
            state_save();
            cJSON_Delete(root);
            heap_caps_free(response);
            *bound = true;
            return true;
        }
        s_bound=false;
        cJSON* poll = json_find(root, "poll_interval");
        *poll_interval = cJSON_IsNumber(poll) ? (int)poll->valuedouble : 5;
        if (*poll_interval < 2) *poll_interval = 2;
        if (*poll_interval > 30) *poll_interval = 30;
        const char* note = json_string(root, "message");
        publish(XTEINK_SYNC_WAITING, note && note[0] ? note : "等待手机扫码绑定");
        cJSON_Delete(root);
        for (int waited = 0; waited < *poll_interval * 10 && !s_cancel; ++waited)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (s_cancel) {heap_caps_free(response);return false;}
    }
}

// ---- 下载与导入 / download and import --------------------------------------
static xteink_task_t* s_tasks;
static atomic_int s_task_count;
static int s_work_mode;   // 0 = 只看列表 / list only, 1 = 下载一本 / one download
static int s_work_index;
static int s_active_index = -1;

int xteink_sync_task_count(void) {
    return s_task_count;
}

bool xteink_sync_task_get(int index,xteink_task_t *out) {
    if(!out||!s_lock)return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool valid=index>=0&&index<s_task_count&&s_tasks;
    if(valid)*out=s_tasks[index];
    xSemaphoreGive(s_lock);return valid;
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
    XTEINK_FILE_PNG,
    XTEINK_FILE_JPEG,
    XTEINK_FILE_XTH,
    XTEINK_FILE_OTHER,
} xteink_file_kind_t;

static xteink_file_kind_t file_kind(const char* name) {
    const char* dot = name ? strrchr(name, '.') : NULL;
    if (!dot) return XTEINK_FILE_OTHER;
    if (!strcasecmp(dot, ".epub")) return XTEINK_FILE_EPUB;
    if (!strcasecmp(dot, ".ttf") || !strcasecmp(dot, ".otf")) return XTEINK_FILE_FONT;
    if (!strcasecmp(dot, ".txt")) return XTEINK_FILE_TEXT;
    if (!strcasecmp(dot, ".png")) return XTEINK_FILE_PNG;
    if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) return XTEINK_FILE_JPEG;
    // 星瞳云端壁纸为 XTH（4 级灰阶），保存到 Pictures 供壁纸/锁屏直接选用。
    // Xteink cloud wallpapers arrive as XTH (4-level gray) and land in Pictures for wallpapers/locks.
    if (!strcasecmp(dot, ".xth")) return XTEINK_FILE_XTH;
    return XTEINK_FILE_OTHER;
}

static uint32_t font_be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static bool verify_font(const char *path) {
    struct stat st;
    if(stat(path,&st)||st.st_size<12)return false;
    FILE *file=fopen(path,"rb");if(!file)return false;
    uint8_t header[16]={0};uint32_t base=0;
    bool ok=fread(header,1,12,file)==12;
    if(ok&&!memcmp(header,"ttcf",4)) {
        ok=font_be32(header+8)>0&&fread(header+12,1,4,file)==4;
        if(ok){base=font_be32(header+12);ok=(uint64_t)base+12<=(uint64_t)st.st_size&&fseek(file,(long)base,SEEK_SET)==0&&fread(header,1,12,file)==12;}
    }
    if(ok)ok=font_be32(header)==0x00010000||!memcmp(header,"OTTO",4)||!memcmp(header,"true",4);
    unsigned tables=(unsigned)header[4]<<8|header[5];
    if(ok)ok=tables>0&&(uint64_t)base+12+(uint64_t)tables*16<=(uint64_t)st.st_size;
    bool cmap=false,head=false,metrics=false,outline=false;
    for(unsigned i=0;ok&&i<tables;++i) {
        ok=fread(header,1,16,file)==16;
        if(!ok)break;
        uint32_t offset=font_be32(header+8),size=font_be32(header+12);
        ok=(uint64_t)offset+size<=(uint64_t)st.st_size;
        if(!memcmp(header,"cmap",4))cmap=true;
        if(!memcmp(header,"head",4))head=true;
        if(!memcmp(header,"hmtx",4))metrics=true;
        if(!memcmp(header,"glyf",4)||!memcmp(header,"CFF ",4)||!memcmp(header,"CFF2",4))outline=true;
    }
    fclose(file);return ok&&cmap&&head&&metrics&&outline;
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

// 用现有流式解码器验证整图，只分配小尺寸输出，不改写原图。
// Validate the image with the existing streaming decoder and a small output, preserving the original.
static bool verify_image(const char *path, bool png) {
    struct stat st;
    if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>BOOK_IMAGE_FILE_MAX)return false;
    FILE *file=fopen(path,"rb");if(!file)return false;
    uint8_t signature[8];
    size_t got=fread(signature,1,sizeof(signature),file);fclose(file);
    bool matches=png ? got==8&&!memcmp(signature,"\x89PNG\r\n\x1a\n",8)
                     : got>=3&&signature[0]==0xff&&signature[1]==0xd8&&signature[2]==0xff;
    unsigned width=0,height=0;
    if(!matches||!book_image_file_dimensions(path,png,&width,&height))return false;
    uint8_t *sample=heap_caps_malloc(32u*32u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!sample)return false;
    bool ok=book_image_file_grayscale(path,png,32,32,sample);
    heap_caps_free(sample);return ok;
}

// 云端 XTH 壁纸：校验头/尺寸并做一次小尺寸解码，原文件不改写。
// Validate an XTH wallpaper header/dimensions with one small decode; the original file stays untouched.
static bool verify_xth(const char *path) {
    struct stat st;
    if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>BOOK_IMAGE_FILE_MAX)return false;
    unsigned width=0,height=0;
    if(!book_image_xth_dimensions(path,&width,&height)||!width||!height)return false;
    uint8_t *sample=heap_caps_malloc(32u*32u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!sample)return false;
    const bool ok=book_image_file_xth_grayscale(path,32,32,sample);
    heap_caps_free(sample);return ok;
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
    int64_t last_activity_us;
} download_sink_t;

static esp_err_t download_event(esp_http_client_event_t* event) {
    download_sink_t* sink = (download_sink_t*)event->user_data;
    if (!sink) return ESP_OK;
    if(event->event_id==HTTP_EVENT_ON_CONNECTED||event->event_id==HTTP_EVENT_ON_HEADER||event->event_id==HTTP_EVENT_ON_DATA)
        sink->last_activity_us=esp_timer_get_time();
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        if (s_cancel) {
            sink->failed = true;
            return ESP_FAIL;
        }
        if(esp_http_client_get_status_code(event->client)!=200)return ESP_OK;
        if (event->data_len > 0 &&
            fwrite(event->data, 1, (size_t)event->data_len, sink->file) != (size_t)event->data_len) {
            sink->failed = true;
            return ESP_FAIL;
        }
        sink->written += (size_t)event->data_len;
        // 进度条只在跨过 10% 时触发一次重绘，避免每块数据都刷屏。
        // Redraw at most once per 10% to keep e-ink traffic sane.
        if (s_active_index >= 0 && s_active_index < s_task_count && s_tasks) {
            xSemaphoreTake(s_lock,portMAX_DELAY);
            xteink_task_t* item = &s_tasks[s_active_index];
            item->received = (uint32_t)sink->written;
            uint32_t percent = item->size ? (uint32_t)((uint64_t)sink->written * 100u / item->size) : 0;
            static uint32_t last_bucket;
            if (percent / 10 != last_bucket / 10) {
                last_bucket = percent;
                ++s_status.revision;
            }
            xSemaphoreGive(s_lock);
        }
    }
    return ESP_OK;
}

// 使用SDK非阻塞HTTPS状态机；无进展30秒终止，等待期间可取消。
// Drive the SDK nonblocking HTTPS state machine; stop after30s without progress and allow cancellation.
static esp_err_t download_perform(esp_http_client_handle_t client,download_sink_t *sink) {
    for(;;) {
        if(s_cancel)return ESP_ERR_INVALID_STATE;
        esp_err_t err=esp_http_client_perform(client);
        if(err!=ESP_ERR_HTTP_EAGAIN)return err;
        if(esp_timer_get_time()-sink->last_activity_us>=(int64_t)XTEINK_DOWNLOAD_TIMEOUT_MS*1000)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// 保留签名查询串原字节，已有百分号转义不二次编码；HTTP链接仅升级HTTPS。
// Preserve signed query bytes and existing escapes; HTTP links are upgraded to HTTPS only.
static char *download_url_prepare(const char *url) {
    if(!url||!url[0]||strnlen(url,XTEINK_SYNC_URL_MAX)>=XTEINK_SYNC_URL_MAX)return NULL;
    const char *rest=!strncmp(url,"https://",8)?url+8:!strncmp(url,"http://",7)?url+7:NULL;
    if(!rest)return NULL;
    size_t host=strcspn(rest,"/?#");if(!host)return NULL;
    for(size_t i=0;i<host;++i)if((unsigned char)rest[i]<=0x20||rest[i]=='@')return NULL;
    size_t cap=XTEINK_SYNC_URL_MAX*3u+16u;
    char *out=heap_caps_malloc(cap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!out)return NULL;
    size_t authority=host;
    if(!strncmp(url,"http://",7)&&host>=3&&!memcmp(rest+host-3,":80",3))host-=3;
    memcpy(out,"https://",8);memcpy(out+8,rest,host);size_t n=8+host;
    const unsigned char *p=(const unsigned char *)rest+authority;
    if(*p!='/')out[n++]='/';
    static const char hex[]="0123456789ABCDEF";
    for(;*p&&*p!='?'&&*p!='#';++p) {
        if(uri_unreserved(*p)||*p=='/'||*p=='%')out[n++]=(char)*p;
        else {out[n++]='%';out[n++]=hex[*p>>4];out[n++]=hex[*p&15];}
    }
    for(;*p&&*p!='#';++p){if(*p<=0x20){heap_caps_free(out);return NULL;}out[n++]=(char)*p;}
    out[n]=0;return out;
}

// 0成功/1网络/2校验/3写卡/4类型。/ 0 success,1 network,2 verification,3 storage,4 type.
static int download_file(const char* url, const char* save_path, const char* incoming,
                         char* saved_path, size_t saved_cap) {
    if(s_cancel)return 1;
    const char* name = save_path && save_path[0] ? strrchr(save_path, '/') : NULL;
    name = name ? name + 1 : (save_path && save_path[0] ? save_path : "book.epub");
    if(!name[0]||!strcmp(name,".")||!strcmp(name,"..")||strchr(name,'\\')) {
        snprintf(s_download_error,sizeof(s_download_error),"文件名无效");return 1;
    }
    const xteink_file_kind_t kind=file_kind(name);
    if(kind==XTEINK_FILE_OTHER)return 4;
    const bool image=kind==XTEINK_FILE_PNG||kind==XTEINK_FILE_JPEG||kind==XTEINK_FILE_XTH;
    if(image&&!read_pico_sd_io_ready()) {
        snprintf(s_download_error,sizeof(s_download_error),"请先插入并挂载TF卡");return 3;
    }
    char *encoded=download_url_prepare(url);
    if(!encoded){snprintf(s_download_error,sizeof(s_download_error),"下载链接无效或内存不足");return 1;}
    char temp[BOOK_STORE_PATH_MAX + 96];
    if (snprintf(temp,sizeof(temp),"%s/download.part",incoming)>=(int)sizeof(temp)) {
        heap_caps_free(encoded);snprintf(s_download_error,sizeof(s_download_error),"下载缓存路径过长");return 3;
    }
    mkdir(incoming, 0777);

    FILE* file = fopen(temp, "wb");
    if (!file) {
        heap_caps_free(encoded);
        snprintf(s_download_error,sizeof(s_download_error),"TF卡无法写入下载缓存");return 3;
    }
    download_sink_t ctx = {.file=file,.written=0,.failed=false,.last_activity_us=esp_timer_get_time()};
    esp_http_client_config_t config = {
        .url = encoded,
        .timeout_ms = XTEINK_DOWNLOAD_TIMEOUT_MS,
        .is_async=true,
        .addr_type=HTTP_ADDR_TYPE_INET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx=8192,
        .event_handler = download_event,
        .user_data = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(!client) {
        heap_caps_free(encoded);fclose(file);unlink(temp);
        snprintf(s_download_error,sizeof(s_download_error),"下载内存不足");return 1;
    }
    esp_http_client_set_header(client, "User-Agent", "kiikoread/" XTEINK_SYNC_FIRMWARE_NAME);
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = download_perform(client,&ctx);
    int status = esp_http_client_get_status_code(client);
    int64_t declared=esp_http_client_get_content_length(client);
    if(ctx.failed)snprintf(s_download_error,sizeof(s_download_error),"TF卡写入失败");
    else if(err!=ESP_OK&&!s_cancel) {
        int tls=0,flags=0;esp_err_t detail=esp_http_client_get_and_clear_last_tls_error(client,&tls,&flags);
        if(err==ESP_ERR_TIMEOUT||detail==ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) {
            size_t host=strcspn(encoded+8,"/?#");
            snprintf(s_download_error,sizeof(s_download_error),"%s: %.*s (0x8006)",ctx.written?"下载等待超时":"连接超时",(int)(host<60?host:60),encoded+8);
        } else if(flags)snprintf(s_download_error,sizeof(s_download_error),"下载证书校验失败 (0x%x)",flags);
        else snprintf(s_download_error,sizeof(s_download_error),"下载连接失败 (0x%x)",(unsigned)(tls?-tls:detail!=ESP_OK?detail:err));
    } else if(status!=200)snprintf(s_download_error,sizeof(s_download_error),"下载 HTTP %d，请刷新列表",status);
    else if(!ctx.written||(declared>0&&(uint64_t)declared!=ctx.written))
        snprintf(s_download_error,sizeof(s_download_error),"下载不完整，请重试");
    esp_http_client_cleanup(client);heap_caps_free(encoded);
    if (err != ESP_OK || status != 200 || ctx.failed || ctx.written == 0 ||
        (declared>0&&(uint64_t)declared!=ctx.written)) {
        ESP_LOGE(TAG, "download failed: err=%s status=%d written=%u failed=%d",
                 esp_err_to_name(err), status, (unsigned)ctx.written, ctx.failed ? 1 : 0);
        fclose(file);unlink(temp);
        return ctx.failed?3:1;
    }
    if (fclose(file) != 0) {
        unlink(temp);
        return 3;
    }
    if(s_cancel){unlink(temp);return 1;}
    bool verified = kind == XTEINK_FILE_EPUB   ? verify_epub(temp)
                    : kind == XTEINK_FILE_FONT ? verify_font(temp)
                    : kind == XTEINK_FILE_XTH  ? verify_xth(temp)
                    : image ? verify_image(temp,kind==XTEINK_FILE_PNG)
                                               : verify_text(temp);
    if (!verified||s_cancel) {
        ESP_LOGE(TAG, "verify failed: %s kind=%d", name, (int)kind);
        unlink(temp);
        if(image&&!s_cancel)snprintf(s_download_error,sizeof(s_download_error),
            kind==XTEINK_FILE_XTH?"XTH 壁纸格式错误":"图片解码失败，请使用非交错PNG或JPG");
        return 2;
    }
    const char* target_dir = image ? XTEINK_PICTURES_DIR
                             : kind == XTEINK_FILE_FONT ? app_settings_fonts_dir() : s_state.root;
    if (!target_dir || !target_dir[0]) target_dir = s_state.root;
    if((image&&!read_pico_sd_io_ready())||(mkdir(target_dir,0777)!=0&&errno!=EEXIST)) {
        snprintf(s_download_error,sizeof(s_download_error),"TF卡目标目录不可用");
        unlink(temp);return 3;
    }
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
    char* response = heap_caps_calloc(1, XTEINK_LIST_RESPONSE_MAX, MALLOC_CAP_SPIRAM);
    if (!response) {
        publish(XTEINK_SYNC_FAILED, "内存不足");
        return;
    }
    int status = 0;
    bool ok = http_request(XTEINK_BASE_URL XTEINK_PULL_PATH, false, NULL, s_state.access_token,
                           response, XTEINK_LIST_RESPONSE_MAX, &status);
    if (ok && status == 401) {
        // 令牌过期：用签名重新换一次令牌，再重试一次拉取。
        // Expired token: obtain a fresh one with the signed request, then retry once.
        ESP_LOGI(TAG, "token expired, refreshing");
        s_state.access_token[0] = 0;s_bound=false;
        char qr[XTEINK_ENVELOPE_MAX] = {0};
        int interval = 5;
        bool bound = false;
        if (sync_bind_time() && request_token(qr, sizeof(qr), &interval, &bound) && bound) {
            ok = http_request(XTEINK_BASE_URL XTEINK_PULL_PATH, false, NULL, s_state.access_token,
                              response, XTEINK_LIST_RESPONSE_MAX, &status);
        }
    }
    if (!ok || status != 200) {
        ESP_LOGE(TAG, "pull failed: status=%d", status);
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
    bool allocated=tasks_alloc();
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if (tasks && cJSON_IsArray(tasks) && allocated) {
        cJSON* task = NULL;
        cJSON_ArrayForEach(task, tasks) {
            if (count >= XTEINK_SYNC_TASK_MAX) break;
            const char* task_id = json_string(task, "task_id");
            const char* url = json_string(task, "file_url");
            if (!task_id || !url) continue;
            xteink_task_t* item = &s_tasks[count];
            memset(item, 0, sizeof(*item));
            snprintf(item->task_id, sizeof(item->task_id), "%s", task_id);
            if(strlen(url)<sizeof(item->file_url))strcpy(item->file_url,url);
            else {item->failed=true;snprintf(item->error,sizeof(item->error),"下载链接过长，请刷新列表");}
            const char* save = json_string(task, "save_path");
            bool from_url=!save||!save[0];
            if (from_url) {
                const char* slash = strrchr(url, '/');
                save = slash ? slash + 1 : url;
            }
            size_t name_length=from_url?strcspn(save,"?#"):strlen(save);
            if(name_length>=sizeof(item->save_path)) {
                item->failed=true;item->file_url[0]=0;
                snprintf(item->error,sizeof(item->error),"文件名过长，请重命名后重新推送");
            }
            snprintf(item->save_path,sizeof(item->save_path),"%.*s",(int)(name_length<sizeof(item->save_path)?name_length:sizeof(item->save_path)-1),save);
            const char* name = strrchr(item->save_path, '/');
            snprintf(item->name, sizeof(item->name), "%.140s", name ? name + 1 : item->save_path);
            const char *basename=strrchr(item->save_path,'/');
            xteink_file_kind_t kind=file_kind(basename?basename+1:item->save_path);
            item->is_font=kind==XTEINK_FILE_FONT;
            item->is_image=kind==XTEINK_FILE_PNG||kind==XTEINK_FILE_JPEG||kind==XTEINK_FILE_XTH;
            cJSON* size = json_find(task, "size");
            if (cJSON_IsNumber(size)) item->size = (uint32_t)size->valuedouble;
            ++count;
        }
    }
    cJSON_Delete(root);
    s_task_count = count;
    xSemaphoreGive(s_lock);
    publish_book(NULL, count);
    char message[96];
    if (count)
        snprintf(message, sizeof(message), "云端待同步 %d 项", count);
    else
        snprintf(message, sizeof(message), "云端没有待下载的内容");
    publish(XTEINK_SYNC_DONE, message);
}

static bool download_one(int index) {
    xteink_task_t *item=heap_caps_malloc(sizeof(*item),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!item){publish(XTEINK_SYNC_FAILED,"内存不足");return false;}
    int reason=1;
    if(!xteink_sync_task_get(index,item)) {
        publish(XTEINK_SYNC_FAILED,"任务已失效，请刷新列表");goto done;
    }
    xSemaphoreTake(s_lock,portMAX_DELAY);
    s_tasks[index].downloading=true;s_tasks[index].failed=false;
    s_tasks[index].imported=false;s_tasks[index].received=0;s_tasks[index].error[0]=0;
    xSemaphoreGive(s_lock);
    publish(XTEINK_SYNC_DOWNLOADING,"正在下载");publish_book(item->name,-1);
    char incoming[BOOK_STORE_PATH_MAX+64],saved[BOOK_STORE_PATH_MAX+64];
    s_download_error[0]=0;
    if(!item->file_url[0]&&item->error[0])snprintf(s_download_error,sizeof(s_download_error),"%s",item->error);
    else if(ensure_network()&&state_path(incoming,sizeof(incoming),"/incoming")) {
        s_active_index=index;
        reason=download_file(item->file_url,item->save_path,incoming,saved,sizeof(saved));
        s_active_index=-1;
    }
    char error[96];
    if(reason==1&&!s_download_error[0]) {
        xteink_sync_status_t status;xteink_sync_get_status(&status);
        if(status.state==XTEINK_SYNC_FAILED)snprintf(s_download_error,sizeof(s_download_error),"%s",status.message);
    }
    snprintf(error,sizeof(error),"%s",s_download_error[0]?s_download_error:reason==4?"不支持的类型":reason==2?"校验失败":reason==3?"TF卡保存失败":"下载失败");
    xSemaphoreTake(s_lock,portMAX_DELAY);
    s_tasks[index].downloading=false;s_tasks[index].failed=reason!=0;
    s_tasks[index].imported=reason==0;
    if(reason)snprintf(s_tasks[index].error,sizeof(s_tasks[index].error),"%s",error);
    else s_tasks[index].received=s_tasks[index].size?s_tasks[index].size:s_tasks[index].received;
    xSemaphoreGive(s_lock);
    if(reason) {
        publish(s_cancel?XTEINK_SYNC_CANCELLED:XTEINK_SYNC_FAILED,s_cancel?"已取消":error);
        complete_task(item->task_id,false,error);
    } else {
        ++s_import_count;complete_task(item->task_id,true,"");
        publish(XTEINK_SYNC_DONE,item->is_image?"图片已保存到 Pictures":item->is_font?"已导入字体":"已导入 1 本图书");
    }
done:
    heap_caps_free(item);return reason==0;
}

// 用户明确点击一键/所选后才按快照串行下载；逐项取消，失败保留并继续其余选择。
// Download a user-approved snapshot sequentially; check cancellation between items and retain individual failures.
static void download_selected(uint32_t selected) {
    unsigned done=0,failed=0;
    for(int i=0;i<XTEINK_SYNC_TASK_MAX&&!s_cancel;++i) {
        uint32_t bit=UINT32_C(1)<<i;
        if(!(selected&bit)||!(xteink_sync_pending_mask()&bit))continue;
        if(download_one(i))++done;else if(!s_cancel)++failed;
    }
    if(!s_cancel){char message[96];snprintf(message,sizeof(message),"同步完成：成功%u项，失败%u项",done,failed);
        publish(failed?XTEINK_SYNC_FAILED:XTEINK_SYNC_DONE,message);}
}

// ---- 后台任务 / worker -------------------------------------------------------
// 绑定二维码的大帧只在拉列表路径存在，下载/TLS调用不继承它。
// Keep the binding QR frame out of the download/TLS worker call chain.
static void __attribute__((noinline)) refresh_cloud(void) {
        time_t now = time(NULL);
        if (s_state.access_token[0] && s_state.access_expires_at > 0 && now > 1700000000 &&
            (int64_t)now >= s_state.access_expires_at)
            s_state.access_token[0] = 0;
        s_bound=s_state.access_token[0]!=0;
        publish(s_state.access_token[0] ? XTEINK_SYNC_PULLING : XTEINK_SYNC_BINDING,
                s_state.access_token[0] ? "正在拉取列表" : "正在申请绑定");
        char qr[XTEINK_ENVELOPE_MAX]={0};
        if (ensure_network()) {
            if (!s_state.access_token[0]) {
                int interval = 5;
                bool bound = false;
                if (sync_bind_time() && request_token(qr, sizeof(qr), &interval, &bound) && bound)
                    publish(XTEINK_SYNC_PULLING, "正在拉取列表");
            }
            if (s_state.access_token[0]) pull_tasks();
        }

}
static void worker(void* argument) {
    (void)argument;
    int mode=s_work_mode,index=s_work_index;
    if(mode==1)download_one(index);
    else if(mode==2)download_selected((uint32_t)index);
    else refresh_cloud();
    // 云页面持有自己建立的STA到离页/锁屏，不在列表和下载之间拆网。
    // Retain an owned STA for the cloud page; do not reconnect between list/download jobs.
    if(s_cancel)publish(XTEINK_SYNC_CANCELLED,"已取消");
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.busy = false;
    if (s_status.state != XTEINK_SYNC_FAILED && s_status.state != XTEINK_SYNC_CANCELLED)
        s_status.state = XTEINK_SYNC_DONE;
    ++s_status.revision;
    if (s_lock) xSemaphoreGive(s_lock);
    s_running=false;
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

bool xteink_sync_bound(void) { return s_bound; }

static esp_err_t start_work(int mode, int index) {
    init_once();
    if (!s_lock || !s_done) return ESP_ERR_NO_MEM;
    if (s_running) return ESP_ERR_INVALID_STATE;
    if(s_task){xSemaphoreTake(s_done,portMAX_DELAY);s_task=NULL;}
    if (!s_state.root[0]) load_root();
    if (!s_state.root[0]) {
        publish(XTEINK_SYNC_FAILED, "没有可用的存储");
        return ESP_ERR_NOT_FOUND;
    }
    identity_init();
    if (!state_load()) (void)state_save();
    s_bound=s_state.access_token[0]!=0;
    if(!s_bound)set_qr(NULL);
    s_cancel = false;
    s_work_mode = mode;
    s_work_index = index;
    while (s_done && xSemaphoreTake(s_done, 0) == pdTRUE) {
    }
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = mode != 0 ? XTEINK_SYNC_DOWNLOADING
                               : (s_state.access_token[0] ? XTEINK_SYNC_PULLING : XTEINK_SYNC_BINDING);
    s_status.busy = true;
    s_status.imported = 0;
    s_status.revision++;
    if (s_lock) xSemaphoreGive(s_lock);
    s_running=true;
    BaseType_t created = xTaskCreate(worker, "xteink_sync", 24576, NULL, 5, &s_task);
    if (created != pdPASS) {
        s_running=false;s_task = NULL;
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

uint32_t xteink_sync_pending_mask(void) {
    uint32_t pending=0;
    if(s_lock)xSemaphoreTake(s_lock,portMAX_DELAY);
    for(int i=0;i<s_task_count&&i<XTEINK_SYNC_TASK_MAX;++i)
        if(!s_tasks[i].imported)pending|=UINT32_C(1)<<i;
    if(s_lock)xSemaphoreGive(s_lock);
    return pending;
}
esp_err_t xteink_sync_download_selected(uint32_t selected) {
    if(!selected||(selected&~xteink_sync_pending_mask()))return ESP_ERR_INVALID_ARG;
    return start_work(2,(int)selected);
}

esp_err_t xteink_sync_unbind(void) {
    init_once();
    xteink_sync_cancel_join();
    if (!s_state.root[0]) load_root();
    identity_init();
    s_state.access_token[0] = 0;
    s_state.refresh_token[0] = 0;
    s_state.access_expires_at = 0;
    s_bound=false;s_task_count = 0;
    set_qr(NULL);
    // 解绑同时清除回滚副本，避免主文件损坏后恢复旧令牌。
    // Unbinding also removes rollback copies so corruption cannot revive an old token.
    char path[BOOK_STORE_PATH_MAX+96];
    const char *extensions[]={".previous",".tmp",""};
    for(unsigned i=0;i<3;++i)if(state_file_path(path,sizeof(path),extensions[i]))unlink(path);
    s_state_loaded=true;
    publish(XTEINK_SYNC_IDLE, "已解绑，请重新扫码");
    return ESP_OK;
}

bool xteink_sync_busy(void) {
    return s_running;
}

void xteink_sync_get_status(xteink_sync_status_t* status) {
    if (!status) return;
    init_once();
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    if (s_lock) xSemaphoreGive(s_lock);
    status->busy = s_running;
    status->bound = s_bound;
}

void xteink_sync_cancel_join(void) {
    if(s_task) {
        s_cancel=true;
        // 先等网络/文件关闭，不能提前清除取消标志。/ Join network/files before clearing cancellation.
        if(s_done)xSemaphoreTake(s_done,portMAX_DELAY);
        s_task=NULL;s_running=false;s_cancel=false;
    }
    if(s_owns_network){read_pico_transfer_stop();s_owns_network=false;}
}
void xteink_sync_close(void) {
    xteink_sync_cancel_join();
    if(s_tasks){heap_caps_free(s_tasks);s_tasks=NULL;}
    s_task_count=0;s_bound=false;s_state_loaded=false;
    memset(&s_state,0,sizeof(s_state));
    if(s_lock)xSemaphoreTake(s_lock,portMAX_DELAY);
    memset(&s_status,0,sizeof(s_status));
    if(s_lock)xSemaphoreGive(s_lock);
}

esp_err_t xteink_sync_forget(void) {
    return xteink_sync_unbind();
}

unsigned xteink_sync_import_count(void) {
    return s_import_count;
}

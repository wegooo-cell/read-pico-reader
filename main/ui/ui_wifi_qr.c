/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 编码手机可识别的WiFi二维码，缓存后纯绘制。/ Encode phone-compatible WiFi QR codes and cache for pure drawing.
 * 冻结：使用标准QR而非Data Matrix；不向日志输出载荷。/ Frozen: use standard QR, not Data Matrix; never log payloads.
 */
#include "ui_wifi_qr.h"
#include <string.h>
#include "qrcode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

// 绑定信封约 500-700 字节，需要 40 版二维码；点阵放 PSRAM，避免占内部 RAM。
// The bind envelope is ~500-700 bytes and needs version 40; keep the bitmap in PSRAM.
#define QR_SIDE_MAX 177
static uint8_t* s_modules;
static int s_side;

static bool modules_ensure(void) {
    if (!s_modules)
        s_modules = heap_caps_calloc((QR_SIDE_MAX * QR_SIDE_MAX + 7) / 8, 1,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_modules != NULL;
}

void ui_wifi_qr_clear(void) {
    s_side = 0;
    if (s_modules) memset(s_modules, 0, (QR_SIDE_MAX * QR_SIDE_MAX + 7) / 8);
}

static void cache_qr(esp_qrcode_handle_t qr) {
    int side = esp_qrcode_get_size(qr);
    if (side > QR_SIDE_MAX || !modules_ensure()) return;
    s_side = side;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            int bit = y * side + x;
            if (esp_qrcode_get_module(qr, x, y)) s_modules[bit / 8] |= 1u << (bit % 8);
        }
}

static char* escape_field(char* out, const char* in) {
    while (*in) {
        if (strchr("\\;,:\"", *in)) *out++ = '\\';
        *out++ = *in++;
    }
    return out;
}

static bool prepare_payload(const char* payload) {
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = cache_qr;
    cfg.max_qrcode_version = 40;
    // 组件默认会打印载荷；编码期间保留错误日志，不输出连接口令。
    // The component logs payloads by default; retain errors without printing the connection password.
    esp_log_level_t previous = esp_log_level_get("QRCODE");
    esp_log_level_set("QRCODE", ESP_LOG_WARN);
    esp_err_t err = esp_qrcode_generate(&cfg, payload);
    esp_log_level_set("QRCODE", previous);
    if (err != ESP_OK) ui_wifi_qr_clear();
    return err == ESP_OK && s_side != 0;
}

bool ui_wifi_qr_prepare(const char* ssid, const char* password) {
    ui_wifi_qr_clear();
    if (!ssid || !password || !*ssid || strlen(ssid) > 32 || strlen(password) > 64) return false;
    char payload[224];
    strcpy(payload, "WIFI:T:WPA;S:");
    char* end = escape_field(payload + strlen(payload), ssid);
    memcpy(end, ";P:", 3); end += 3;
    end = escape_field(end, password);
    memcpy(end, ";;", 3);
    bool ok = prepare_payload(payload);
    volatile char* clear = payload;
    for (size_t i = 0; i < sizeof(payload); ++i) clear[i] = 0;
    return ok;
}

bool ui_wifi_qr_prepare_url(const char* url) {
    ui_wifi_qr_clear();
    if (!url) return false;
    size_t length = strnlen(url, 320);
    if (!length || length >= 320) return false;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)url[i] < 0x21 || (unsigned char)url[i] > 0x7e) return false;

    // 传书二维码只接受本机 IPv4 地址，拒绝外部站点。
    // Transfer QR codes accept only local IPv4 hosts, never external sites.
    if (strncmp(url, "http://", 7)) return false;
    const char* p = url + 7;
    for (int i = 0; i < 4; ++i) {
        unsigned value = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return false;
            value = value * 10 + (unsigned)(*p++ - '0');
        }
        if (!digits || value > 255) return false;
        if (i < 3 && *p++ != '.') return false;
    }
    if (*p == '/') ++p;
    if (*p) return false;
    return prepare_payload(url);
}

bool ui_wifi_qr_prepare_text(const char* text) {
    ui_wifi_qr_clear();
    if (!text) return false;
    size_t length = strnlen(text, 1000);
    if (!length || length >= 1000) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c > 0x7e) return false;
    }
    return prepare_payload(text);
}

void ui_wifi_qr_draw(uint8_t* fb, EpdRect area) {
    epd_fill_rect(area, 0xff, fb);
    if (!s_side || !s_modules) return;
    int width = area.width < area.height ? area.width : area.height;
    int scale = width / (s_side + 8);
    if (scale < 1) return;
    int x0 = area.x + (area.width - s_side * scale) / 2;
    int y0 = area.y + (area.height - s_side * scale) / 2;
    for (int y = 0; y < s_side; ++y)
        for (int x = 0; x < s_side; ++x) {
            int bit = y * s_side + x;
            if (s_modules[bit / 8] & (1u << (bit % 8)))
                epd_fill_rect((EpdRect){x0 + x * scale, y0 + y * scale, scale, scale}, 0, fb);
        }
}

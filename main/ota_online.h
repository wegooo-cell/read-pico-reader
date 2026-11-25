/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：联网更新的版本清单与可取消后台任务。/ English: release metadata and cancellable online update jobs.
 * 冻结：只写空闲应用槽，完整校验后由 UI 确认启动。/ Frozen: only write the inactive app slot; UI commits boot after verification.
 * 用户修订：固件擦写与屏幕推送互斥，进度只追加变化像素；不并行驱动 PSRAM 画面和闪存擦写。
 * User revision: serialize firmware flash operations with display output; append progress changes without concurrent PSRAM scans and flash writes.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#if KIIKO_BOARD_METALIO47
#define PICO_OTA_IMAGE_MAX 0x500000
#define PICO_OTA_PROJECT "Kiikoread_Metalio_S31"
#define PICO_OTA_LAYOUT "metalio-s31-dual-5m-v1"
#define PICO_OTA_BOARD "Metalio-EINK4-Plus-S31"
#define PICO_OTA_CAPABILITY "METALIO_S31_HTTPS_OTA_V1"
#define PICO_OTA_FEED_URL "https://wegooo-cell.github.io/read-pico-reader/metalio-s31-update.json"
#define PICO_OTA_FEED_FALLBACK_URL "https://kiikoread.com/metalio-s31-update.json"
#else
#define PICO_OTA_IMAGE_MAX 0x400000
#define PICO_OTA_PROJECT "Read_Pico"
#define PICO_OTA_LAYOUT "pico-dual-4m-v1"
#define PICO_OTA_BOARD "RDP-G01-W"
#define PICO_OTA_CAPABILITY "PICO_HTTPS_OTA_V1"
#define PICO_MINDRESET_QUERY_URL "https://dot.mindreset.tech/api/authV2/device/firmware/query"
#define PICO_MINDRESET_FIRMWARE_ID "6b05776be73f24cc56953283"
#define PICO_OTA_FEED_URL "https://wegooo-cell.github.io/read-pico-reader/update.json"
#define PICO_OTA_FEED_FALLBACK_URL PICO_OTA_FEED_URL
#define PICO_MINDRESET_DOWNLOAD_PREFIX "https://dot.mindreset.tech/download/"
#define PICO_MINDRESET_CDN_PREFIX "https://cdn.mindreset.tech/"
#define PICO_MINDRESET_CDN_URL_MAX 2048
#endif

typedef enum { PICO_UPDATE_IDLE, PICO_UPDATE_CHECKING, PICO_UPDATE_AVAILABLE,
               PICO_UPDATE_LATEST, PICO_UPDATE_DOWNLOADING, PICO_UPDATE_READY,
               PICO_UPDATE_FAILED, PICO_UPDATE_CANCELLED } pico_update_state_t;
typedef struct {
    char version[33], project[33], board[24], layout[32], minimum_base_version[33];
    char url[256], sha256[65], notes[384];
    uint32_t size;
} pico_release_t;
typedef struct {
    pico_update_state_t state;
    pico_release_t release;
    uint32_t revision, received;
    char message[96];
    bool busy;
} pico_update_status_t;

/// 有界解析、型号与版本验证；不接触网络。/ Bounded parsing, board and version validation without networking.
esp_err_t pico_release_parse(const char *json, size_t length, pico_release_t *release);
#if !KIIKO_BOARD_METALIO47
/// 构造匿名型号查询；不发送组织密钥或设备身份。/ Build an anonymous model query without organization secrets or device identity.
esp_err_t pico_mindreset_query_body(const char *current, char *out, size_t capacity);
/// 校验软件、设备、渠道、版本与OTA元数据。/ Validate software, device, channel, versions and OTA metadata.
esp_err_t pico_mindreset_parse(const char *json, size_t length, const char *current,
                             pico_release_t *release, bool *need_update);
/// 仅允许官方CDN的有界HTTPS签名链接。/ Accept only bounded HTTPS signed links on the official CDN.
const char *pico_mindreset_channel(const char *version);
bool pico_ota_same_channel(const char *candidate, const char *current);
const char *pico_github_feed_url(const char *current);
bool pico_github_url_valid(const char *url);
bool pico_mindreset_url_valid(const char *url);
bool pico_mindreset_cdn_url_valid(const char *url);
#endif
int pico_version_compare(const char *a, const char *b);
bool pico_release_url_valid(const char *url);
/// 检查或下载唯一后台任务；离页、锁屏先取消并等待。/ Start one worker; cancel and join on exit or lock.
esp_err_t pico_online_check(void);
esp_err_t pico_online_download(void);
bool pico_online_busy(void);
void pico_online_get_status(pico_update_status_t *status);
void pico_online_cancel_join(void);
/// 刷屏与 OTA 擦写共用递归锁；未初始化 OTA 时不分配内存。/ Recursive display/OTA exclusion; no allocation before OTA initialization.
bool pico_online_display_begin(void);
void pico_online_display_end(bool held);
/// 已验证镜像切换启动槽；失败不切换。/ Select a verified image; failures keep the existing boot slot.
esp_err_t pico_online_commit(void);

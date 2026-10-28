/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：星曈云同步的设备端协议与后台任务：绑定二维码、令牌、拉取任务、下载、校验和导入。
 *      只负责协议与存储，不画界面；界面在 main/apps/app_cloud_sync.c。
 * English: Device-side Xteink cloud sync: bind QR, tokens, task pull, download, verification and
 *      import. Protocol and storage only; the UI lives in main/apps/app_cloud_sync.c.
 *
 * 冻结：只访问官方 HTTPS 域名；身份走请求头；令牌只落 TF 卡；校验失败的文件不上架。
 * Frozen: official HTTPS origin only; identity in request headers; tokens stay on the SD card;
 *      a failed verification never reaches the shelf.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define XTEINK_SYNC_STATE_DIR "/.readpico/cloud"
#define XTEINK_SYNC_FIRMWARE_NAME "kiikoread-pico"
#define XTEINK_SYNC_TASK_MAX 24

typedef enum {
    XTEINK_SYNC_IDLE = 0,  ///< 未启动 / Not started
    XTEINK_SYNC_BINDING,   ///< 申请绑定 / Requesting bind
    XTEINK_SYNC_WAITING,   ///< 等待手机扫码 / Waiting for the phone to scan
    XTEINK_SYNC_PULLING,   ///< 拉取任务 / Pulling tasks
    XTEINK_SYNC_DOWNLOADING, ///< 下载图书 / Downloading a book
    XTEINK_SYNC_IMPORTING, ///< 校验并导入 / Verifying and importing
    XTEINK_SYNC_DONE,      ///< 本轮完成 / This round finished
    XTEINK_SYNC_FAILED,    ///< 失败 / Failed
    XTEINK_SYNC_CANCELLED, ///< 已取消 / Cancelled
} xteink_sync_state_t;

typedef struct {
    char task_id[64];  ///< 云端任务 ID / Cloud task id
    char name[160];    ///< 书名 / Book name
    char save_path[256]; ///< 云端保存路径 / Cloud save path
    char file_url[512];  ///< 下载地址 / Download URL
    uint32_t size;     ///< 字节数 / Size in bytes
    uint32_t received; ///< 已下载字节 / Bytes received
    bool imported;     ///< 本轮已导入 / Imported this session
    bool is_font;      ///< 字体文件，导入 fonts 目录 / Font file, imported into the fonts folder
    bool downloading;  ///< 正在下载 / Download in progress
    bool failed;       ///< 上次下载失败 / Last attempt failed
    char error[24];    ///< 失败原因 / Failure reason
} xteink_task_t;

typedef struct {
    xteink_sync_state_t state; ///< 当前状态 / Current state
    char message[96];          ///< 给界面的一行说明 / One-line UI message
    char qr[1024];             ///< 绑定二维码原文，空表示无 / Bind QR payload, empty when none
    char book[160];            ///< 当前处理的图书名 / Current book name
    int imported;              ///< 本轮已导入数量 / Books imported this round
    int pending;               ///< 服务端待处理数量 / Pending tasks on the server
    unsigned revision;         ///< 每次变化自增 / Incremented on every change
    bool busy;                 ///< 后台任务在跑 / Worker running
    bool bound;                ///< 已有令牌 / Tokens present
} xteink_sync_status_t;

/// 未绑定则先绑定，然后只拉取列表（不自动下载）。
/// Bind when needed, then refresh the list only (never auto-download).
esp_err_t xteink_sync_start(void);
/// 下载列表中指定的一本。/ Download one book from the list.
esp_err_t xteink_sync_download(int index);
/// 清除绑定并回到二维码。/ Forget the binding and return to the QR flow.
esp_err_t xteink_sync_unbind(void);
/// 当前列表数量与条目。/ List size and one entry.
int xteink_sync_task_count(void);
bool xteink_sync_task_get(int index, xteink_task_t* out);
bool xteink_sync_busy(void);
void xteink_sync_get_status(xteink_sync_status_t* status);
/// 取消并等待任务退出；离页、锁屏前调用。/ Cancel and join; call before leaving or locking.
void xteink_sync_cancel_join(void);
/// 是否已有可用令牌（读缓存，不碰网络）。/ Whether tokens exist (cached, no networking).
bool xteink_sync_bound(void);
/// 清除令牌重新绑定；返回后需要重新扫码。/ Forget tokens; a new scan is required.
esp_err_t xteink_sync_forget(void);
/// 本轮导入过图书的次数；界面据此通知书架重扫。/ Import counter for the UI to trigger a shelf rescan.
unsigned xteink_sync_import_count(void);

// ---- 纯函数（宿主测试可单独编译）/ Pure helpers (host-testable) -----------------
/// 路径百分号编码：保留 / 和已有 %xx，编码空格与 UTF-8。/ Percent-encode a path.
size_t xteink_url_encode_path(const char* in, char* out, size_t cap);
/// 小写 hex。/ Lowercase hex.
size_t xteink_hex_encode(const uint8_t* in, size_t length, char* out, size_t cap);
/// 无 padding 的 base64url。/ Unpadded base64url.
size_t xteink_base64url_encode(const uint8_t* in, size_t length, char* out, size_t cap);

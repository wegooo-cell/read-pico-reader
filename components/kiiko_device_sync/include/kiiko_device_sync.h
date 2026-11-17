/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：两台设备在同一 ESP-NOW 频道上互传设置备份流；协议自带配对码、逐片确认与整体校验。
 * English: Move the settings backup stream between two devices over one ESP-NOW channel with a
 *          pairing code, per-chunk acknowledgement and a whole-stream check.
 *
 * 冻结：只在设置页占用射频期间工作；不落盘任何传输内容，不修改设备身份、分区或供电。
 * Frozen: Work only while the settings page owns the radio; never persist transferred content,
 *         change identity, partitions or power sequencing.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 本机在同步里的角色 / Local role in one synchronization.
typedef enum {
    KIIKO_SYNC_ROLE_SEND = 0, ///< 发送本机设置 / Send local settings
    KIIKO_SYNC_ROLE_RECEIVE,  ///< 接收并应用对端设置 / Receive and apply peer settings
} kiiko_sync_role_t;

/// 页面观察得到的阶段 / Phase the settings page observes.
typedef enum {
    KIIKO_SYNC_IDLE = 0,     ///< 未启动 / Not started
    KIIKO_SYNC_SCANNING,     ///< 射频已起，等待对端 / Radio up, waiting for the peer
    KIIKO_SYNC_PAIRED,       ///< 已配对，显示配对码 / Paired, pairing code shown
    KIIKO_SYNC_TRANSFERRING, ///< 正在分片传输 / Chunk transfer in flight
    KIIKO_SYNC_APPLYING,     ///< 接收端正在应用设置 / Receiver applying the stream
    KIIKO_SYNC_DONE,         ///< 完成 / Finished successfully
    KIIKO_SYNC_FAILED,       ///< 失败或已取消 / Failed or cancelled
} kiiko_sync_state_t;

/// 跨任务状态快照 / Consistent status snapshot across tasks.
typedef struct {
    kiiko_sync_state_t state; ///< 当前阶段 / Current phase
    kiiko_sync_role_t role;   ///< 本机角色 / Local role
    unsigned code;            ///< 配对码，未配对为 0 / Pairing code, 0 before pairing
    unsigned percent;         ///< 传输进度 0..100 / Transfer progress 0..100
    size_t done;              ///< 已传字节 / Bytes transferred
    size_t total;             ///< 总字节 / Total bytes
    esp_err_t error;          ///< 失败原因 / Failure reason
} kiiko_sync_status_t;

/// 设置流的注入点；组件只搬运字节，格式由 main 拥有。
/// / Injection points for the settings stream; the component only moves bytes.
typedef struct {
    esp_err_t (*export_settings)(uint8_t **data, size_t *len);  ///< 分配并返回本机设置流 / Allocate the local stream
    esp_err_t (*import_settings)(const uint8_t *data, size_t len); ///< 校验并应用对端设置流 / Validate and apply a peer stream
} kiiko_sync_io_t;

/// 启动一次同步；任务异步运行，页面轮询状态。/ Start one sync; the worker runs asynchronously.
esp_err_t kiiko_sync_start(kiiko_sync_role_t role, const kiiko_sync_io_t *io);
/// 接收端确认配对码，允许对端开始传输。/ Receiver accepts the pairing code so the peer may send.
void kiiko_sync_confirm(void);
/// 请求取消；不等待任务退出。/ Request cancellation without waiting for the worker.
void kiiko_sync_cancel(void);
/// 请求取消并等待任务释放射频；可重复调用。/ Cancel, join, and release the radio; repeatable.
void kiiko_sync_stop(void);
/// 任务是否仍在运行 / Whether the worker is still running.
bool kiiko_sync_active(void);
/// 复制状态快照 / Copy the status snapshot.
void kiiko_sync_get_status(kiiko_sync_status_t *out);

#ifdef __cplusplus
}
#endif

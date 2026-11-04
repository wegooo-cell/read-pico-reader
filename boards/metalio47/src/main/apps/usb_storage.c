/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：卸载本机 FAT 后用 TinyUSB MSC 向电脑共享 TF 卡，退出时重新挂载。
 * English: Unmount local FAT before sharing SD by TinyUSB MSC, then remount on exit.
 * 冻结：固件和电脑绝不同时挂载 TF 卡；主机弹出时也不自动挂载到第二路径；不自动格式化。
 * Frozen: Firmware and host never mount SD together, including host eject; never auto-format.
 */
#include "usb_storage.h"
#include "ui_avatar.h"
#include "book_cover_jobs.h"
#include "book_lock_collage.h"
#include "metalio_hw.h"
#include <stdlib.h>
#include <stdatomic.h>
#include "device/usbd_pvt.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_sd.h"
#include "read_pico_sd_msc.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "ttf_font.h"

static const char *TAG = "usb_storage";
static sdmmc_card_t *s_card;
static tinyusb_msc_storage_handle_t s_storage;

static bool s_usb_ready, s_msc_ready;
static bool s_active;
static bool s_cleanup_pending;
enum { USB_RUNNING, USB_DRAINING, USB_PARKED };
static atomic_int s_usb_stop_state;
static atomic_bool s_usb_park_queued;

// 在USB任务内确认已接纳的事件全部退出，再停车；不强删持有SD锁的任务。
// Acknowledge accepted events on the USB task before parking; never kill a task holding SD locks.
static void park_usb_task(void *unused) {
    (void)unused;
    if (atomic_load(&s_usb_stop_state) != USB_DRAINING) {
        atomic_store(&s_usb_park_queued, false);
        // 超时与新请求交错时补发一次，避免丢失排空请求。
        // Requeue a raced retry after clearing the stale callback's ownership.
        if (atomic_load(&s_usb_stop_state) == USB_DRAINING && !atomic_exchange(&s_usb_park_queued, true))
            usbd_defer_func(park_usb_task, NULL, false);
        return;
    }
    if (tud_task_event_ready()) { usbd_defer_func(park_usb_task, NULL, false); return; }
    int expected = USB_DRAINING;
    if (atomic_compare_exchange_strong(&s_usb_stop_state, &expected, USB_PARKED)) {
        atomic_store(&s_usb_park_queued, false);
        vTaskSuspend(NULL);
    } else {
        atomic_store(&s_usb_park_queued, false);
        if (atomic_load(&s_usb_stop_state) == USB_DRAINING && !atomic_exchange(&s_usb_park_queued, true))
            usbd_defer_func(park_usb_task, NULL, false);
    }
}
static esp_err_t quiesce_usb_task(void) {
    if (atomic_load(&s_usb_stop_state) == USB_PARKED) return ESP_OK;
    atomic_store(&s_usb_stop_state, USB_DRAINING);
    tud_disconnect();
    // 旧超时请求可能尚在USB队列；不能另加一个相互排不空的停车回调。
    // A timed-out callback may still be queued; never create mutually requeued parking callbacks.
    if (!atomic_exchange(&s_usb_park_queued, true)) usbd_defer_func(park_usb_task, NULL, false);
    TickType_t start = xTaskGetTickCount();
    while (atomic_load(&s_usb_stop_state) != USB_PARKED) {
        if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(2000)) {
            int expected = USB_DRAINING;
            if (atomic_compare_exchange_strong(&s_usb_stop_state, &expected, USB_RUNNING)) {
                tud_connect();
                s_cleanup_pending = false;
                return ESP_ERR_TIMEOUT;
            }
        }
        vTaskDelay(1);
    }
    return ESP_OK;
}

// S31的USJ/UTMI独立，GPIO36只切物理通路，不做S3共享PHY切换。
// S31 USJ/UTMI are separate; GPIO36 switches the board route, not an S3 shared PHY.
static esp_err_t restore_serial_phy(void) { return metalio_usb_route_serial(true); }
static esp_err_t release_serial_phy(void) { return metalio_usb_route_serial(false); }

bool usb_storage_active(void) { return s_active; }
bool usb_storage_connected(void) { return s_active && !s_cleanup_pending && tud_mounted(); }

void usb_storage_phy_init(void) {
    metalio_usb_serial_startup();
}

static void restore_local(void) {
    esp_err_t err = read_pico_sd_remount();
    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) ESP_LOGW(TAG, "SD remount: %s", esp_err_to_name(err));
}

esp_err_t usb_storage_start(void) {
    book_lock_collage_invalidate();
    if (!(book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000))) return ESP_ERR_NOT_FINISHED;
    if (s_cleanup_pending) return ESP_ERR_INVALID_STATE;
    if (s_active) return ESP_OK;
    read_pico_sd_info_t info = {0};
    if (read_pico_sd_get_info(&info) != ESP_OK || !info.mounted) return ESP_ERR_INVALID_STATE;
    // 字体文件在 SD 上，先关闭句柄再卸载。/ Close the SD-backed font before unmounting.
    if (!ttf_font_is_builtin()) ttf_font_open_builtin();
    esp_err_t err = read_pico_sd_usb_acquire(&s_card);
    if (err != ESP_OK) {
        read_pico_sd_info_t retained={0};
        (void)read_pico_sd_get_info(&retained);
        if(retained.mounted)return err;
        goto fail;
    }
    // 电脑弹出磁盘时不可让 MSC 驱动自动挂载 /data；本机只通过 read_pico_sd 恢复 /sdcard。
    // Never auto-mount /data on host eject; only read_pico_sd may restore /sdcard.
    tinyusb_msc_driver_config_t msc = {.user_flags.auto_mount_off = 1};
    err = tinyusb_msc_install_driver(&msc);
    if (err != ESP_OK) goto fail;
    s_msc_ready = true;
    tinyusb_msc_storage_config_t storage = {
        .medium.card = s_card,
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .fat_fs = { .do_not_format = true, .config.max_files = 4 },
    };
    err = tinyusb_msc_new_storage_sdmmc(&storage, &s_storage);
    if (err != ESP_OK) goto fail;
    tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
    err = release_serial_phy();
    if (err != ESP_OK) goto fail;
    err = tinyusb_driver_install(&usb);
    if (err != ESP_OK) goto fail;
    s_usb_ready = true;
    s_active = true;
    ESP_LOGI(TAG, "TF card exposed as USB MSC");
    return ESP_OK;
fail:
    ESP_LOGE(TAG, "USB start failed: %s", esp_err_to_name(err));
    // 启动失败也走同一个收尾路径，失败的存储句柄必须保留供重试。
    // Failed starts use the same teardown; retain any storage handle that still needs cleanup.
    s_active = true;
    (void)usb_storage_stop();
    return err;
}

esp_err_t usb_storage_stop(void) {
    if (!s_active && !s_storage && !s_usb_ready && !s_msc_ready) {
        return restore_serial_phy();
    }
    s_cleanup_pending = true;
    esp_err_t err = ESP_OK;
    esp_err_t route_err;
    // 先排空并拆USB任务，再删除MSC对象；失败保留卡、对象与清理状态供重试。
    // Drain and remove the USB task before MSC objects; retain card/handles on failure for retry.
    if (s_usb_ready) {
        err = quiesce_usb_task();
        if (err != ESP_OK) return err;
        err = tinyusb_driver_uninstall();
        if (err != ESP_OK) goto cleanup;
        s_usb_ready = false;
        atomic_store(&s_usb_stop_state, USB_RUNNING);
        atomic_store(&s_usb_park_queued, false);
    }
    for (int attempt = 0; s_storage && attempt < 20; ++attempt) {
        err = tinyusb_msc_delete_storage(s_storage);
        if (err == ESP_OK) { s_storage = NULL; break; }
        if (err != ESP_ERR_INVALID_STATE) goto cleanup;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_storage) goto cleanup;
    if (s_msc_ready) {
        err = tinyusb_msc_uninstall_driver();
        if (err != ESP_OK) goto cleanup;
        s_msc_ready = false;
    }

cleanup:
    // 所有退出路径都尝试归还串口；只有 USB/MSC 完全退出后才释放卡和恢复本机挂载。
    // Reclaim the console on every exit; free the card and remount only after USB/MSC fully stop.
    route_err = restore_serial_phy();
    if (s_storage || s_usb_ready || s_msc_ready) {
        // 暂停主循环的卡访问与自动休眠，保留句柄供“结束 USB 读卡”再次收尾。
        // Keep card access and idle sleep paused until End USB retries the retained handles.
        s_active = true;
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    if (route_err != ESP_OK) return route_err;
    // 原始卡/DMA由统一驱动回收；失败继续保留清理状态。/ Unified driver reclaims raw card/DMA; failures retain cleanup state.
    err=read_pico_sd_usb_release();
    if(err!=ESP_OK)return err;
    s_card=NULL;
    s_active = false;
    s_cleanup_pending = false;
    restore_local();
    return ESP_OK;
}

/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：设置页的 ESP-NOW 近场同步。发送端广播会话，接收端自动选中首个对端并给出配对码；
 *       接收端确认后按 200 字节分片停等传输，整段 CRC 校验通过才交给设置层应用。
 * English: Settings-page ESP-NOW sync. The sender announces a session, the receiver auto-selects
 *          the first peer and derives a pairing code; after confirmation it moves 200-byte chunks
 *          stop-and-wait and only hands a CRC-checked stream to the settings layer.
 *
 * 冻结：射频只在本组件任务内开关；不缓存或落盘传输内容，不改设备身份、分区和供电顺序。
 * Frozen: Only this task owns the radio; never cache or persist transferred content, and never
 *         change identity, partitions or power sequencing.
 */
#include "kiiko_device_sync.h"

#include <string.h>

#include "ble_page_turner.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAG "kiiko_sync"

#define SYNC_MAGIC       0x4B53u
#define SYNC_PROTOCOL    1u
#define SYNC_CHANNEL     6u
#define SYNC_CHUNK       200u
#define SYNC_HEADER      32u
#define SYNC_QUEUE_DEPTH 12u
#define SYNC_MAX_BYTES   (2u * 1024u * 1024u)
#define SYNC_ACK_MS      160
#define SYNC_RETRY_MAX   14u
#define SYNC_IDLE_MS     20000
#define SYNC_DONE_LINGER_MS  1200
#define SYNC_FAIL_LINGER_MS  400

enum {
    SYNC_FRAME_ANNOUNCE = 1,
    SYNC_FRAME_FOUND = 2,
    SYNC_FRAME_CONFIRM = 3,
    SYNC_FRAME_DATA = 4,
    SYNC_FRAME_ACK = 5,
    SYNC_FRAME_FINISH = 6,
    SYNC_FRAME_RESULT = 7,
    SYNC_FRAME_ABORT = 8,
};

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t role;
    uint8_t flags;
    uint8_t mac[6];
    uint32_t nonce;
    uint32_t offset;
    uint32_t total;
    uint32_t value;
    uint16_t length;
    uint16_t spare;
} sync_frame_t;

_Static_assert(sizeof(sync_frame_t) == SYNC_HEADER, "sync frame header must stay 32 bytes");

typedef struct {
    uint8_t mac[6];
    uint16_t length;
    uint8_t data[SYNC_HEADER + SYNC_CHUNK];
} sync_rx_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static kiiko_sync_status_t s_status;

static TaskHandle_t s_task;
static QueueHandle_t s_queue;
static kiiko_sync_io_t s_io;
static kiiko_sync_role_t s_role;
static kiiko_sync_state_t s_state;
static volatile bool s_cancel;
static volatile bool s_confirm;

static bool s_radio_loop;
static bool s_radio_wifi;
static bool s_radio_started;
static bool s_radio_now;
static bool s_radio_ble;

static uint8_t s_own[6];
static uint8_t s_peer[6];
static bool s_peer_known;
static uint32_t s_nonce;
static uint32_t s_code;
static uint32_t s_total;
static uint32_t s_crc;

static uint8_t *s_out;
static size_t s_out_len;
static uint8_t *s_in;
static size_t s_done;

static bool s_awaiting_ack;
static int64_t s_ack_deadline;
static int64_t s_next_tx_ms;
static int64_t s_last_rx_ms;
static unsigned s_retries;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

// 两端用同一份材料算码：会话随机数、发送端 MAC、接收端 MAC。
// Both sides hash the same material: session nonce, sender MAC, receiver MAC.
static uint32_t pair_code(uint32_t nonce, const uint8_t *sender, const uint8_t *receiver) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 4; ++i) hash = (hash ^ ((nonce >> (8 * i)) & 0xffu)) * 16777619u;
    for (unsigned i = 0; i < 6; ++i) hash = (hash ^ sender[i]) * 16777619u;
    for (unsigned i = 0; i < 6; ++i) hash = (hash ^ receiver[i]) * 16777619u;
    return 1000u + (hash % 9000u);
}

static void state_set(kiiko_sync_state_t state, esp_err_t error) {
    s_state = state;
    taskENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.error = error;
    s_status.role = s_role;
    taskEXIT_CRITICAL(&s_lock);
    if (state == KIIKO_SYNC_DONE) ESP_LOGI(TAG, "sync done");
    else if (state == KIIKO_SYNC_FAILED) ESP_LOGW(TAG, "sync failed: %s", esp_err_to_name(error));
}

static void progress_set(size_t done, size_t total) {
    taskENTER_CRITICAL(&s_lock);
    s_status.done = done;
    s_status.total = total;
    s_status.percent = total ? (unsigned)((done * 100u) / total) : 0;
    taskEXIT_CRITICAL(&s_lock);
}

static void code_set(uint32_t code) {
    s_code = code;
    taskENTER_CRITICAL(&s_lock);
    s_status.code = code;
    taskEXIT_CRITICAL(&s_lock);
}

static uint8_t *stream_alloc(size_t length) {
    uint8_t *buffer = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) buffer = heap_caps_malloc(length, MALLOC_CAP_8BIT);
    return buffer;
}

static void peer_add(const uint8_t *mac, bool broadcast) {
    if (!s_radio_now) return;
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    if (broadcast) memset(peer.peer_addr, 0xff, sizeof(peer.peer_addr));
    else memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
    peer.channel = (uint8_t)SYNC_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    const esp_err_t error = esp_now_add_peer(&peer);
    if (error != ESP_OK && error != ESP_ERR_ESPNOW_EXIST)
        ESP_LOGW(TAG, "peer: %s", esp_err_to_name(error));
}

static bool emit(uint8_t type, uint32_t offset, uint32_t total, uint32_t value,
                 const uint8_t *payload, uint16_t length, bool broadcast) {
    if (!s_radio_now) return false;
    static const uint8_t k_broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t *destination;
    if (broadcast) destination = k_broadcast;
    else if (s_peer_known) destination = s_peer;
    else return false;
    uint8_t buffer[SYNC_HEADER + SYNC_CHUNK];
    sync_frame_t *frame = (sync_frame_t *)buffer;
    memset(buffer, 0, SYNC_HEADER);
    frame->magic = SYNC_MAGIC;
    frame->version = (uint8_t)SYNC_PROTOCOL;
    frame->type = type;
    frame->role = (uint8_t)s_role;
    frame->nonce = s_nonce;
    frame->offset = offset;
    frame->total = total;
    frame->value = value;
    frame->length = length;
    memcpy(frame->mac, s_own, sizeof(frame->mac));
    if (length && payload) memcpy(buffer + SYNC_HEADER, payload, length);
    const esp_err_t error = esp_now_send(destination, buffer, SYNC_HEADER + length);
    if (error != ESP_OK) ESP_LOGD(TAG, "send %u: %s", (unsigned)type, esp_err_to_name(error));
    return error == ESP_OK;
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
    if (!info || !info->src_addr || !data || !s_queue) return;
    if (length < (int)SYNC_HEADER) return;
    const sync_frame_t *frame = (const sync_frame_t *)data;
    if (frame->magic != SYNC_MAGIC || frame->version != (uint8_t)SYNC_PROTOCOL) return;
    if (frame->length > SYNC_CHUNK) return;
    if ((int)SYNC_HEADER + (int)frame->length > length) return;
    sync_rx_t item;
    memcpy(item.mac, info->src_addr, sizeof(item.mac));
    item.length = (uint16_t)(SYNC_HEADER + frame->length);
    memcpy(item.data, data, item.length);
    (void)xQueueSend(s_queue, &item, 0);
}

static esp_err_t radio_up(void) {
    esp_err_t error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    // 射频事件要有人收；我们自己建就自己拆。
    // Somebody must drain radio events; delete the loop we created on teardown.
    error = esp_event_loop_create_default();
    if (error == ESP_OK) s_radio_loop = true;
    else if (error != ESP_ERR_INVALID_STATE) return error;
    error = ble_pt_network_acquire();
    if (error != ESP_OK) return error;
    s_radio_ble = true;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;
    error = esp_wifi_init(&init);
    if (error != ESP_OK) goto fail;
    s_radio_wifi = true;
    error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (error != ESP_OK) goto fail;
    error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error != ESP_OK) goto fail;
    error = esp_wifi_start();
    if (error != ESP_OK) goto fail;
    s_radio_started = true;
    error = esp_wifi_set_channel(SYNC_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (error != ESP_OK) goto fail;
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    (void)esp_wifi_set_max_tx_power(48);
    error = esp_now_init();
    if (error != ESP_OK) goto fail;
    s_radio_now = true;
    error = esp_now_register_recv_cb(on_recv);
    if (error != ESP_OK) goto fail;
    peer_add(NULL, true);
    return ESP_OK;
fail:
    ESP_LOGE(TAG, "radio: %s", esp_err_to_name(error));
    return error;
}

static void radio_down(void) {
    if (s_radio_now) {
        (void)esp_now_unregister_recv_cb();
        esp_now_deinit();
        s_radio_now = false;
    }
    if (s_radio_started) {
        (void)esp_wifi_stop();
        s_radio_started = false;
    }
    if (s_radio_wifi) {
        (void)esp_wifi_deinit();
        s_radio_wifi = false;
    }
    if (s_radio_loop) {
        (void)esp_event_loop_delete_default();
        s_radio_loop = false;
    }
    if (s_radio_ble) {
        ble_pt_network_release();
        s_radio_ble = false;
    }
}

static void fail(esp_err_t error) {
    if (s_state == KIIKO_SYNC_DONE || s_state == KIIKO_SYNC_FAILED) return;
    if (s_radio_now && s_peer_known)
        (void)emit(SYNC_FRAME_ABORT, 0, 0, (uint32_t)error, NULL, 0, false);
    state_set(KIIKO_SYNC_FAILED, error);
}

static void send_chunk(int64_t now) {
    size_t remaining = (size_t)s_total - s_done;
    uint16_t length = remaining > SYNC_CHUNK ? (uint16_t)SYNC_CHUNK : (uint16_t)remaining;
    if (!emit(SYNC_FRAME_DATA, (uint32_t)s_done, s_total, 0, s_out + s_done, length, false)) return;
    s_awaiting_ack = true;
    s_ack_deadline = now + SYNC_ACK_MS;
}

static void sender_tick(int64_t now) {
    if (s_state == KIIKO_SYNC_SCANNING) {
        if (now >= s_next_tx_ms) {
            (void)emit(SYNC_FRAME_ANNOUNCE, 0, s_total, 0, NULL, 0, true);
            s_next_tx_ms = now + 200;
        }
    } else if (s_state == KIIKO_SYNC_PAIRED) {
        if (now >= s_next_tx_ms) {
            (void)emit(SYNC_FRAME_ANNOUNCE, 0, s_total, s_code, NULL, 0, false);
            s_next_tx_ms = now + 300;
        }
    } else if (s_state == KIIKO_SYNC_TRANSFERRING) {
        if (s_done >= s_total) {
            if (now >= s_next_tx_ms) {
                (void)emit(SYNC_FRAME_FINISH, s_total, s_total, s_crc, NULL, 0, false);
                s_next_tx_ms = now + 300;
                if (++s_retries > 40) fail(ESP_ERR_TIMEOUT);
            }
        } else if (s_awaiting_ack) {
            if (now >= s_ack_deadline) {
                if (++s_retries > SYNC_RETRY_MAX) fail(ESP_ERR_TIMEOUT);
                else send_chunk(now);
            }
        } else {
            send_chunk(now);
        }
    }
}

static void receiver_tick(int64_t now) {
    if (s_state == KIIKO_SYNC_PAIRED) {
        if (s_confirm) {
            s_confirm = false;
            s_in = stream_alloc(s_total);
            if (!s_in) {
                fail(ESP_ERR_NO_MEM);
                return;
            }
            s_done = 0;
            s_last_rx_ms = now;
            state_set(KIIKO_SYNC_TRANSFERRING, ESP_OK);
            (void)emit(SYNC_FRAME_CONFIRM, 0, s_total, s_code, NULL, 0, false);
            (void)emit(SYNC_FRAME_CONFIRM, 0, s_total, s_code, NULL, 0, false);
            (void)emit(SYNC_FRAME_ACK, 0, s_total, 0, NULL, 0, false);
        } else if (now >= s_next_tx_ms) {
            (void)emit(SYNC_FRAME_FOUND, 0, s_total, s_code, NULL, 0, false);
            s_next_tx_ms = now + 250;
        }
    } else if (s_state == KIIKO_SYNC_TRANSFERRING) {
        if (now - s_last_rx_ms > SYNC_IDLE_MS) fail(ESP_ERR_TIMEOUT);
    }
}

static void handle_frame(const sync_rx_t *item) {
    const sync_frame_t *frame = (const sync_frame_t *)item->data;
    const int64_t now = now_ms();
    switch (frame->type) {
    case SYNC_FRAME_ANNOUNCE:
        if (s_role != KIIKO_SYNC_ROLE_RECEIVE) return;
        if (s_state == KIIKO_SYNC_SCANNING) {
            if (frame->role != (uint8_t)KIIKO_SYNC_ROLE_SEND) return;
            if (frame->total == 0 || frame->total > SYNC_MAX_BYTES) {
                fail(ESP_ERR_INVALID_SIZE);
                return;
            }
            memcpy(s_peer, item->mac, sizeof(s_peer));
            s_peer_known = true;
            s_nonce = frame->nonce;
            s_total = frame->total;
            code_set(pair_code(s_nonce, s_peer, s_own));
            peer_add(s_peer, false);
            state_set(KIIKO_SYNC_PAIRED, ESP_OK);
            s_next_tx_ms = now;
        } else if (s_state == KIIKO_SYNC_TRANSFERRING) {
            // 配对的确认可能丢；发送端重复广播时补发确认与当前进度。
            // The confirmation may be lost; re-answer a repeated announce with confirm and progress.
            if (frame->nonce != s_nonce) return;
            (void)emit(SYNC_FRAME_CONFIRM, 0, s_total, s_code, NULL, 0, false);
            (void)emit(SYNC_FRAME_ACK, (uint32_t)s_done, s_total, 0, NULL, 0, false);
        }
        return;

    case SYNC_FRAME_FOUND:
        if (s_role != KIIKO_SYNC_ROLE_SEND || s_state != KIIKO_SYNC_SCANNING) return;
        if (frame->nonce != s_nonce) return;
        memcpy(s_peer, item->mac, sizeof(s_peer));
        s_peer_known = true;
        if (frame->value != pair_code(s_nonce, s_own, s_peer)) {
            fail(ESP_ERR_INVALID_RESPONSE);
            return;
        }
        code_set(frame->value);
        peer_add(s_peer, false);
        state_set(KIIKO_SYNC_PAIRED, ESP_OK);
        s_next_tx_ms = now;
        return;

    case SYNC_FRAME_CONFIRM:
        if (s_role != KIIKO_SYNC_ROLE_SEND || s_state != KIIKO_SYNC_PAIRED) return;
        s_done = 0;
        s_awaiting_ack = false;
        s_retries = 0;
        s_last_rx_ms = now;
        state_set(KIIKO_SYNC_TRANSFERRING, ESP_OK);
        return;

    case SYNC_FRAME_ACK:
        if (s_role != KIIKO_SYNC_ROLE_SEND || s_state != KIIKO_SYNC_TRANSFERRING) return;
        if (frame->offset > s_done && frame->offset <= s_total) {
            s_done = frame->offset;
            s_retries = 0;
            s_awaiting_ack = false;
            s_last_rx_ms = now;
            progress_set(s_done, s_total);
        }
        return;

    case SYNC_FRAME_DATA: {
        if (s_role != KIIKO_SYNC_ROLE_RECEIVE || s_state != KIIKO_SYNC_TRANSFERRING) return;
        const uint32_t offset = frame->offset;
        const uint16_t length = frame->length;
        if (!length || (size_t)offset + length > s_total) {
            (void)emit(SYNC_FRAME_ACK, (uint32_t)s_done, s_total, 0, NULL, 0, false);
            return;
        }
        if (offset == s_done) {
            memcpy(s_in + offset, item->data + SYNC_HEADER, length);
            s_done += length;
            s_last_rx_ms = now;
            progress_set(s_done, s_total);
        }
        (void)emit(SYNC_FRAME_ACK, (uint32_t)s_done, s_total, 0, NULL, 0, false);
        return;
    }

    case SYNC_FRAME_FINISH: {
        if (s_role != KIIKO_SYNC_ROLE_RECEIVE) return;
        if (s_state == KIIKO_SYNC_DONE) {
            (void)emit(SYNC_FRAME_RESULT, 0, 0, (uint32_t)ESP_OK, NULL, 0, false);
            return;
        }
        if (s_state != KIIKO_SYNC_TRANSFERRING) return;
        if (frame->total != s_total || s_done != s_total ||
            crc32_bytes(s_in, s_total) != frame->value) {
            (void)emit(SYNC_FRAME_RESULT, 0, 0, (uint32_t)ESP_ERR_INVALID_CRC, NULL, 0, false);
            fail(ESP_ERR_INVALID_CRC);
            return;
        }
        state_set(KIIKO_SYNC_APPLYING, ESP_OK);
        const esp_err_t error = s_io.import_settings ? s_io.import_settings(s_in, s_total)
                                                     : ESP_ERR_INVALID_STATE;
        free(s_in);
        s_in = NULL;
        (void)emit(SYNC_FRAME_RESULT, 0, 0, (uint32_t)error, NULL, 0, false);
        if (error == ESP_OK) state_set(KIIKO_SYNC_DONE, ESP_OK);
        else state_set(KIIKO_SYNC_FAILED, error);
        return;
    }

    case SYNC_FRAME_RESULT:
        if (s_role != KIIKO_SYNC_ROLE_SEND) return;
        if ((esp_err_t)frame->value == ESP_OK) state_set(KIIKO_SYNC_DONE, ESP_OK);
        else state_set(KIIKO_SYNC_FAILED, (esp_err_t)frame->value);
        return;

    case SYNC_FRAME_ABORT:
        fail((esp_err_t)(frame->value ? frame->value : (uint32_t)ESP_ERR_INVALID_STATE));
        return;

    default:
        return;
    }
}

static void sync_task(void *arg) {
    (void)arg;
    if (s_queue) xQueueReset(s_queue);
    s_state = KIIKO_SYNC_IDLE;
    s_cancel = false;
    s_confirm = false;
    s_peer_known = false;
    s_awaiting_ack = false;
    s_retries = 0;
    s_done = 0;
    s_out = NULL;
    s_out_len = 0;
    s_in = NULL;
    s_total = 0;
    s_nonce = 0;
    s_crc = 0;
    code_set(0);
    progress_set(0, 0);

    if (esp_read_mac(s_own, ESP_MAC_WIFI_STA) != ESP_OK) memset(s_own, 0, sizeof(s_own));

    esp_err_t error = ESP_OK;
    if (s_role == KIIKO_SYNC_ROLE_SEND) {
        error = s_io.export_settings ? s_io.export_settings(&s_out, &s_out_len)
                                     : ESP_ERR_INVALID_STATE;
        if (error == ESP_OK && (s_out_len == 0 || s_out_len > SYNC_MAX_BYTES))
            error = ESP_ERR_INVALID_SIZE;
        if (error == ESP_OK) {
            s_total = (uint32_t)s_out_len;
            s_nonce = esp_random();
            s_crc = crc32_bytes(s_out, s_out_len);
            progress_set(0, s_total);
        } else if (s_out) {
            free(s_out);
            s_out = NULL;
        }
    }

    if (error == ESP_OK) error = radio_up();
    if (error == ESP_OK) {
        state_set(KIIKO_SYNC_SCANNING, ESP_OK);
        s_next_tx_ms = 0;
        s_last_rx_ms = now_ms();
        int64_t settled_at = 0;
        while (!s_cancel) {
            sync_rx_t item;
            if (xQueueReceive(s_queue, &item, pdMS_TO_TICKS(4)) == pdTRUE) handle_frame(&item);
            const int64_t now = now_ms();
            if (s_role == KIIKO_SYNC_ROLE_SEND) sender_tick(now);
            else receiver_tick(now);
            if (s_state == KIIKO_SYNC_DONE || s_state == KIIKO_SYNC_FAILED) {
                if (!settled_at) settled_at = now;
                const int64_t linger = s_state == KIIKO_SYNC_DONE ? SYNC_DONE_LINGER_MS
                                                                 : SYNC_FAIL_LINGER_MS;
                if (now - settled_at > linger) break;
            }
        }
        if (s_cancel && s_state != KIIKO_SYNC_DONE) fail(ESP_ERR_INVALID_STATE);
    } else {
        state_set(KIIKO_SYNC_FAILED, error);
    }

    radio_down();
    if (s_out) {
        free(s_out);
        s_out = NULL;
    }
    s_out_len = 0;
    if (s_in) {
        free(s_in);
        s_in = NULL;
    }
    s_peer_known = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t kiiko_sync_start(kiiko_sync_role_t role, const kiiko_sync_io_t *io) {
    if (!io || !io->export_settings || !io->import_settings) return ESP_ERR_INVALID_ARG;
    if (role != KIIKO_SYNC_ROLE_SEND && role != KIIKO_SYNC_ROLE_RECEIVE) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_ERR_INVALID_STATE;
    s_role = role;
    s_io = *io;
    s_cancel = false;
    s_confirm = false;
    s_state = KIIKO_SYNC_IDLE;
    taskENTER_CRITICAL(&s_lock);
    memset(&s_status, 0, sizeof(s_status));
    s_status.role = role;
    s_status.state = KIIKO_SYNC_IDLE;
    taskEXIT_CRITICAL(&s_lock);
    if (!s_queue) s_queue = xQueueCreate(SYNC_QUEUE_DEPTH, sizeof(sync_rx_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sync_task, "kiiko_sync", 6144, NULL, 5, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void kiiko_sync_confirm(void) {
    if (s_role == KIIKO_SYNC_ROLE_RECEIVE) s_confirm = true;
}

void kiiko_sync_cancel(void) { s_cancel = true; }

void kiiko_sync_stop(void) {
    if (!s_task) return;
    s_cancel = true;
    for (int i = 0; i < 400 && s_task; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_task) ESP_LOGW(TAG, "sync task did not stop in time");
}

bool kiiko_sync_active(void) { return s_task != NULL; }

void kiiko_sync_get_status(kiiko_sync_status_t *out) {
    if (!out) return;
    taskENTER_CRITICAL(&s_lock);
    *out = s_status;
    taskEXIT_CRITICAL(&s_lock);
}

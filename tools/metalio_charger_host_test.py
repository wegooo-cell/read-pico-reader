"""Compile the real CX25601N charge driver against stub SDK/bus headers and exercise it."""
from pathlib import Path
import os, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-g']

STUB_HEADERS = {
    'esp_err.h': r'''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NOT_FINISHED 0x109
const char *esp_err_to_name(esp_err_t err);
''',
    'esp_log.h': r'''#pragma once
void host_log(int level, const char *tag, const char *format, ...);
#define ESP_LOGI(...) host_log(0, __VA_ARGS__)
#define ESP_LOGW(...) host_log(1, __VA_ARGS__)
#define ESP_LOGE(...) host_log(2, __VA_ARGS__)
''',
    'freertos/FreeRTOS.h': r'''#pragma once
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
''',
    'freertos/semphr.h': r'''#pragma once
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
''',
    'freertos/task.h': r'''#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint32_t stack, void *arg,
                       UBaseType_t priority, TaskHandle_t *handle);
''',
    'metalio_hw.h': r'''#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t metalio_read(uint8_t address, uint8_t reg, void *data, size_t size);
esp_err_t metalio_write8(uint8_t address, uint8_t reg, uint8_t data);
''',
}

UNIT = r'''
#include "DRIVER"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- 桩总线与调度器状态 / Stub bus and scheduler state ---- */
static uint8_t regs[256];
static uint8_t gauge[256];
static int read_count;
static int write_count;
static uint8_t write_regs[512];
static uint8_t write_values[512];
static int fail_read_address = -1;
static int fail_read_reg = -1;
static int corrupt_read_reg = -1;
static uint8_t corrupt_read_mask;
static int write_fail_reg = -1;
static int unlock_never;
static int task_creates;
static int mutex_creates;
static int binary_creates;
static int run_sem_gives;
static char task_name[32];
static int delay_count;
static uint32_t delays[128];
static char logs[65536];
static size_t logs_len;
static int mutex_object;
static int binary_object;
static int task_object;

/* ---- 桩 SDK / 总线实现 / Stub SDK and bus implementations ---- */
esp_err_t metalio_read(uint8_t address, uint8_t reg, void *data, size_t size) {
    ++read_count;
    if ((int)address == fail_read_address || (int)reg == fail_read_reg) return ESP_FAIL;
    uint8_t *out = (uint8_t *)data;
    if (!out || (size != 1 && size != 2)) return ESP_ERR_INVALID_ARG;
    assert(address == 0x6B || address == 0x55);
    const uint8_t *source = address == 0x6B ? regs : gauge;
    for (size_t i = 0; i < size; ++i) out[i] = source[(reg + i) & 0xFF];
    if ((int)reg == corrupt_read_reg) out[0] ^= corrupt_read_mask;
    return ESP_OK;
}

esp_err_t metalio_write8(uint8_t address, uint8_t reg, uint8_t data) {
    ++write_count;
    if (write_count <= 512) {
        write_regs[write_count - 1] = reg;
        write_values[write_count - 1] = data;
    }
    if ((int)reg == write_fail_reg) return ESP_FAIL;
    if (address != 0x6B) return ESP_ERR_INVALID_ARG;
    regs[reg] = data;
    // 模拟私有解锁：0x70 写入 0x44 后回读 0x03；unlock_never 保持未解锁。
    // Emulate the unlock handshake: 0x70 reads 0x03 after writing 0x44 unless unlock_never is set.
    if (reg == 0x70) regs[0x70] = unlock_never ? 0x02 : (uint8_t)(data == 0x44 ? 0x03 : data);
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    ++mutex_creates;
    return (SemaphoreHandle_t)&mutex_object;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    ++binary_creates;
    return (SemaphoreHandle_t)&binary_object;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    (void)semaphore;
    (void)ticks;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore == (SemaphoreHandle_t)&binary_object) ++run_sem_gives;
    return pdTRUE;
}

void vTaskDelay(TickType_t ticks) {
    if (delay_count < 128) delays[delay_count++] = (uint32_t)ticks;
}

BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint32_t stack, void *arg,
                       UBaseType_t priority, TaskHandle_t *handle) {
    (void)function;
    (void)stack;
    (void)arg;
    (void)priority;
    ++task_creates;
    snprintf(task_name, sizeof(task_name), "%s", name ? name : "");
    if (handle) *handle = (TaskHandle_t)&task_object;
    return pdPASS;
}

const char *esp_err_to_name(esp_err_t err) {
    switch (err) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    default: return "ESP_ERR_UNKNOWN";
    }
}

void host_log(int level, const char *tag, const char *format, ...) {
    (void)level;
    (void)tag;
    if (logs_len >= sizeof(logs) - 1) return;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(logs + logs_len, sizeof(logs) - logs_len, format, args);
    va_end(args);
    if (written > 0) {
        const size_t capacity = sizeof(logs) - logs_len - 1;
        const size_t amount = (size_t)written < capacity ? (size_t)written : capacity;
        logs_len += amount;
    }
}

/* ---- 测试辅助 / Test helpers ---- */
static void host_reset(void) {
    memset(regs, 0, sizeof(regs));
    memset(gauge, 0, sizeof(gauge));
    read_count = write_count = 0;
    fail_read_address = fail_read_reg = corrupt_read_reg = write_fail_reg = -1;
    corrupt_read_mask = 0;
    unlock_never = 0;
    task_creates = mutex_creates = binary_creates = run_sem_gives = 0;
    task_name[0] = 0;
    delay_count = 0;
    logs_len = 0;
    logs[0] = 0;
    s_ready = false;
    s_suspended = false;
    s_monitor_task = NULL;
    s_ichg_ma = 0;
    s_vreg_target_mv = CX_DEFAULT_VREG_MV;
    s_vreg_applied_mv = 0;
    s_fast_charge = false;
    s_batfet_shutdown_done = false;
    s_last_vbat_mv = CX_FIRST_PACE_VBAT_MV;
    s_desired_valid = false;
    s_desired_ma = 0;
    s_desired_fast = false;
    atomic_store(&s_monitor_running, true);
    s_lock = NULL;
    s_run_sem = NULL;
}

static void set_gauge(uint16_t mv, int16_t ma) {
    gauge[0x08] = (uint8_t)(mv & 0xFFu);
    gauge[0x09] = (uint8_t)(mv >> 8);
    gauge[0x0C] = (uint8_t)((uint16_t)ma & 0xFFu);
    gauge[0x0D] = (uint8_t)(((uint16_t)ma >> 8) & 0xFFu);
}

static void set_status(uint8_t charge_stat, uint8_t vbus_stat) {
    regs[0x1E] = (uint8_t)(((charge_stat & 0x03u) << 3) | (vbus_stat & 0x07u));
}

static int writes_to(uint8_t reg) {
    int count = 0;
    for (int i = 0; i < write_count && i < 512; ++i)
        if (write_regs[i] == reg) ++count;
    return count;
}

static bool host_running(void) { return atomic_load(&s_monitor_running); }
static int32_t host_last_vbat(void) { return s_last_vbat_mv; }
static void monitor_sample(void) { monitor_work(); }
static uint32_t monitor_pace_for(int32_t mv) { return monitor_pace_ms(mv); }

/* ---- 用例 / Test cases ---- */
static void test_not_ready(void) {
    host_reset();
    assert(!metalio_charger_ready());
    assert(metalio_charger_current_ma() == 0);
    assert(!metalio_charger_fast_charge());
    assert(metalio_charger_set_current_ma(1000) == ESP_ERR_INVALID_STATE);
    assert(metalio_charger_set_fast_charge(true) == ESP_ERR_INVALID_STATE);
}

// 未就绪时的设置只记录目标；初始化成功后补应用，适配器30s重试成功也不会丢掉偏好。
// Settings made before init are recorded and applied after init, so the adapter's30s retry
// cannot lose the saved preference.
static void test_pending_current(void) {
    host_reset();
    assert(metalio_charger_set_current_ma(1000) == ESP_ERR_INVALID_STATE);
    assert(metalio_charger_init() == ESP_OK);
    assert(metalio_charger_current_ma() == 960u);
    assert(!metalio_charger_fast_charge());
    assert(strstr(logs, "applied pending preference") != NULL);
    // 只补一次：再次 init 不重写。/ Applied once only; a repeated init writes nothing.
    const int writes_before = write_count;
    assert(metalio_charger_init() == ESP_OK);
    assert(write_count == writes_before);
}

static void test_pending_fast_charge(void) {
    host_reset();
    assert(metalio_charger_set_fast_charge(true) == ESP_ERR_INVALID_STATE);
    assert(metalio_charger_init() == ESP_OK);
    assert(metalio_charger_ready());
    assert(metalio_charger_fast_charge());
    assert(metalio_charger_current_ma() == 1440u);
    const uint32_t code = 1440u / 80u;
    assert(((regs[0x02] & 0xC0u) >> 6) == (code & 0x03u));
    assert((regs[0x03] & 0x0Fu) == ((code >> 2) & 0x0Fu));
}

static void test_pending_survives_failed_init(void) {
    host_reset();
    assert(metalio_charger_set_fast_charge(true) == ESP_ERR_INVALID_STATE);
    fail_read_reg = 0x38;   // 探测失败：偏好必须保留到下一次重试 / probe fails; keep the preference
    assert(metalio_charger_init() != ESP_OK);
    fail_read_reg = -1;
    assert(metalio_charger_init() == ESP_OK);
    assert(metalio_charger_fast_charge());
    assert(metalio_charger_current_ma() == 1440u);
}

static void test_probe_failure(void) {
    host_reset();
    fail_read_reg = 0x38;
    assert(metalio_charger_init() == ESP_FAIL);
    assert(!metalio_charger_ready());
    assert(write_count == 0);
    assert(task_creates == 0);
    assert(strstr(logs, "probe PART_INFO") != NULL);
}

static void test_write_failure(void) {
    host_reset();
    write_fail_reg = 0x12;  // ITERM 写失败 / ITERM write fails
    assert(metalio_charger_init() != ESP_OK);
    assert(!metalio_charger_ready());
    assert(task_creates == 0);
    assert(strstr(logs, "failed") != NULL);
}

static void test_readback_failure(void) {
    host_reset();
    corrupt_read_reg = 0x03;  // ICHG 高位回读错误 / corrupt ICHG high readback
    corrupt_read_mask = 0x0F;
    assert(metalio_charger_init() == ESP_ERR_INVALID_RESPONSE);
    assert(!metalio_charger_ready());
    assert(strstr(logs, "readback mismatch") != NULL);
}

// 厂商 hw_init_defaults 的完整写序列；重启顺序变化会在此暴露。
// Full vendor hw_init_defaults write order; any reordering shows up here.
static const uint8_t EXPECTED_INIT_REGS[] = {
    0x15, 0x16, 0x16, 0x10, 0x11, 0x12, 0x13, 0x04, 0x05,
    0x02, 0x03, 0x06, 0x07,
    0x70, 0x70, 0x70, 0x70, 0x86, 0x3A, 0x46, 0x70,
    0x14, 0x18, 0x1A, 0x23, 0x24,
    0x16,
    0x02, 0x03, 0x06, 0x07,
};

static void test_init_sequence(void) {
    host_reset();
    assert(metalio_charger_init() == ESP_OK);
    assert(metalio_charger_ready());
    assert(task_creates == 1);
    assert(strcmp(task_name, "metalio_chg") == 0);
    assert(mutex_creates == 1 && binary_creates == 1);
    assert(write_count == (int)(sizeof(EXPECTED_INIT_REGS) / sizeof(EXPECTED_INIT_REGS[0])));
    for (int i = 0; i < write_count; ++i) assert(write_regs[i] == EXPECTED_INIT_REGS[i]);

    assert((regs[0x15] & 0x40u) == 0);                  // DPDM 位清 0 / DPDM bit cleared
    assert((regs[0x16] & 0x33u) == 0x20u);              // HIZ 0, WDT 0, EN_CHG 1
    assert((regs[0x10] & 0xF0u) == 0xC0u);              // IPRECHG 0x0C
    assert((regs[0x11] & 0x01u) == 0);
    assert((regs[0x12] & 0xF8u) == 0x30u);              // ITERM 0x06 = 60mA
    assert((regs[0x13] & 0x01u) == 0);
    const uint32_t vreg_code = 4400u / 10u;             // VREG 4400mV
    assert((regs[0x04] & 0xF8u) == ((vreg_code & 0x1Fu) << 3));
    assert((regs[0x05] & 0x0Fu) == ((vreg_code >> 5) & 0x0Fu));
    const uint32_t ichg_code = 960u / 80u;              // 标准 1000mA 向下量化为 960mA
    assert(((regs[0x02] & 0xC0u) >> 6) == (ichg_code & 0x03u));
    assert((regs[0x03] & 0x0Fu) == ((ichg_code >> 2) & 0x0Fu));
    const uint32_t iindpm_code = 960u / 20u;            // IINDPM 随 ICHG 同步
    assert(((regs[0x06] & 0xF0u) >> 4) == (iindpm_code & 0x0Fu));
    assert((regs[0x07] & 0x0Fu) == ((iindpm_code >> 4) & 0x0Fu));
    assert(regs[0x86] == 0x06 && regs[0x3A] == 0x10 && regs[0x46] == 0x20);
    assert(regs[0x70] == 0x00);                          // 私有寄存器重新上锁 / private regs re-locked
    assert((regs[0x14] & 0x01u) == 0x01u);              // CHG_CTRL0 bit0
    assert((regs[0x18] & 0x04u) == 0);                  // CHG_CTRL3 bit2 清 0
    assert((regs[0x1A] & 0x80u) == 0x80u);
    assert((regs[0x23] & 0x1Cu) == 0x1Cu);              // bits2..4 = 0x07
    assert((regs[0x24] & 0x08u) == 0x08u);
    assert(metalio_charger_current_ma() == 960u);
    assert(!metalio_charger_fast_charge());

    // 重复 init 不得重写寄存器或另起任务。/ A repeated init writes nothing and starts no task.
    const int writes_before = write_count;
    assert(metalio_charger_init() == ESP_OK);
    assert(write_count == writes_before);
    assert(task_creates == 1);
}

static void expect_current(uint32_t request, uint32_t expected) {
    const int before = write_count;
    assert(metalio_charger_set_current_ma(request) == ESP_OK);
    assert(metalio_charger_current_ma() == expected);
    assert(write_count > before);
    const uint32_t code = expected / 80u;
    assert(((regs[0x02] & 0xC0u) >> 6) == (code & 0x03u));
    assert((regs[0x03] & 0x0Fu) == ((code >> 2) & 0x0Fu));
    uint32_t i_code = expected / 20u;
    if (i_code < 5u) i_code = 5u;
    if (i_code > 150u) i_code = 150u;
    assert(((regs[0x06] & 0xF0u) >> 4) == (i_code & 0x0Fu));
    assert((regs[0x07] & 0x0Fu) == ((i_code >> 4) & 0x0Fu));
}

static void test_quantization(void) {
    expect_current(1000, 960);
    expect_current(1500, 1440);
    expect_current(200, 160);
    expect_current(85, 80);
    expect_current(80, 80);
    expect_current(79, 80);     // 低于下限夹到 80mA / clamped up to the 80mA floor
    expect_current(0, 80);
    expect_current(3040, 3040);
    expect_current(3041, 3040);
    expect_current(5000, 3040);
}

static void test_current_errors(void) {
    expect_current(1440, 1440);
    write_fail_reg = 0x02;
    assert(metalio_charger_set_current_ma(1000) == ESP_FAIL);
    assert(metalio_charger_current_ma() == 1440u);
    write_fail_reg = -1;
    corrupt_read_reg = 0x02;    // ICHG 回读错误 / corrupt ICHG readback
    corrupt_read_mask = 0xC0;
    assert(metalio_charger_set_current_ma(1000) == ESP_ERR_INVALID_RESPONSE);
    assert(metalio_charger_current_ma() == 1440u);
    corrupt_read_reg = 0x06;    // IINDPM 回读错误 / corrupt IINDPM readback
    corrupt_read_mask = 0xF0;
    assert(metalio_charger_set_current_ma(1000) == ESP_ERR_INVALID_RESPONSE);
    assert(metalio_charger_current_ma() == 1440u);
    corrupt_read_reg = -1;
    corrupt_read_mask = 0;
}

static void test_fast_charge(void) {
    assert(metalio_charger_set_fast_charge(true) == ESP_OK);
    assert(metalio_charger_fast_charge());
    assert(metalio_charger_current_ma() == 1440u);
    assert(metalio_charger_set_fast_charge(false) == ESP_OK);
    assert(!metalio_charger_fast_charge());
    assert(metalio_charger_current_ma() == 960u);
    // 切换失败保留原状态与电流。/ A failed switch keeps both the state and the current.
    write_fail_reg = 0x02;
    assert(metalio_charger_set_fast_charge(true) == ESP_FAIL);
    assert(!metalio_charger_fast_charge());
    assert(metalio_charger_current_ma() == 960u);
    write_fail_reg = -1;
}

static void test_monitor_en_term(void) {
    set_status(1, 0);       // CC，避免 kick / CC, no kick
    set_gauge(4000, 100);   // 低于截止 / below cutoff
    monitor_sample();
    assert((regs[0x14] & 0x04u) == 0);
    // 延时节奏改用锁内采样缓存，不再额外读总线。/ The pace uses the in-lock sample cache.
    assert(host_last_vbat() == 4000);
    assert(monitor_pace_for(host_last_vbat()) == 2000u);
    set_status(2, 0);
    set_gauge(4450, 100);   // 高于截止 / above cutoff
    monitor_sample();
    assert((regs[0x14] & 0x04u) == 0x04u);
    assert(host_last_vbat() == 4450);
    assert(monitor_pace_for(host_last_vbat()) == 10000u);
}

static void test_monitor_kick(void) {
    set_status(0, 0);       // CHG_STAT=NOT
    set_gauge(4000, 0);     // 低于截止且无电流 / below cutoff and no current
    logs_len = 0;
    logs[0] = 0;
    delay_count = 0;
    const int before = write_count;
    monitor_sample();
    assert(write_count > before);
    assert(strstr(logs, "recharge kick") != NULL);
    assert(strstr(logs, "recharge after") != NULL);
    assert(delay_count == 2);
    assert(delays[0] == 200u && delays[1] == 500u);
    assert((regs[0x16] & 0x20u) == 0x20u);   // EN_CHG 已恢复 / EN_CHG restored
    assert((regs[0x14] & 0x04u) == 0);       // EN_TERM 仍关 / EN_TERM stays off

    delay_count = 0;
    set_gauge(4000, 6);     // 有充电电流不 kick / current flows, no kick
    monitor_sample();
    assert(delay_count == 0);
    set_gauge(4000, 0);
    set_status(1, 0);       // 非 NOT 状态不 kick / not terminated, no kick
    monitor_sample();
    assert(delay_count == 0);
    set_gauge(4450, 0);
    set_status(0, 0);       // 不低于截止不 kick / not below cutoff, no kick
    monitor_sample();
    assert(delay_count == 0);
}

static void test_monitor_uv(void) {
    set_status(1, 0);       // CC，避免 kick / CC, avoid the kick path
    set_gauge(3000, 0);
    const int before = writes_to(0x18);
    monitor_sample();
    assert(regs[0x18] == 0x01);
    assert(writes_to(0x18) == before + 1);
    monitor_sample();       // 同一上电周期只做一次 / one shot per power cycle
    assert(writes_to(0x18) == before + 1);
    set_gauge(3201, 0);     // 回升到滞回点以上重新武装 / recover past hysteresis
    monitor_sample();
    set_gauge(3000, 0);
    monitor_sample();
    assert(writes_to(0x18) == before + 2);
    set_gauge(3201, 0);
    monitor_sample();       // 重新武装 / re-arm
    regs[0x18] = 0;
    set_gauge(3000, 0);
    set_status(1, 3);       // 有 VBUS 不触发 / VBUS present, no shutdown
    monitor_sample();
    assert(regs[0x18] == 0);
}

static void test_pace(void) {
    assert(monitor_pace_for(4399) == 2000u);
    assert(monitor_pace_for(4000) == 2000u);
    assert(monitor_pace_for(4400) == 10000u);
    assert(monitor_pace_for(4450) == 10000u);
    assert(monitor_pace_for(2499) == 10000u);
    assert(monitor_pace_for(0) == 10000u);
}

static void test_suspend_resume(void) {
    assert(host_running());
    metalio_charger_suspend();
    assert(!host_running());
    // 挂起后监控采样不得访问总线。/ A suspended monitor sample must not touch the bus.
    const int reads_before = read_count;
    const int writes_before = write_count;
    monitor_sample();
    assert(read_count == reads_before && write_count == writes_before);
    metalio_charger_suspend();  // 幂等 / idempotent
    assert(!host_running());
    const int gives_before = run_sem_gives;
    metalio_charger_resume();
    assert(host_running());
    assert(run_sem_gives == gives_before + 1);
    metalio_charger_resume();   // 幂等 / idempotent
    assert(host_running());
    assert(run_sem_gives == gives_before + 1);
}

static void test_private_unlock_failure(void) {
    host_reset();
    unlock_never = 1;
    assert(metalio_charger_init() == ESP_OK);
    assert(metalio_charger_ready());
    assert(strstr(logs, "private register unlock failed") != NULL);
    assert(writes_to(0x70) == 41);  // 10 轮 ×4 + 结束上锁 / 10 rounds x4 plus relock
    assert(regs[0x86] == 0x06 && regs[0x3A] == 0x10 && regs[0x46] == 0x20);
    assert(metalio_charger_current_ma() == 960u);
}

static void test_private_write_failure(void) {
    host_reset();
    write_fail_reg = 0x86;  // 私有写失败不能阻断公开配置 / private failure must not block public setup
    assert(metalio_charger_init() == ESP_OK);
    assert(metalio_charger_ready());
    assert(strstr(logs, "private write 0x86") != NULL);
    assert(regs[0x86] != 0x06);
    assert(metalio_charger_current_ma() == 960u);
}

int main(void) {
    test_not_ready();
    test_pending_current();
    test_pending_fast_charge();
    test_pending_survives_failed_init();
    test_probe_failure();
    test_write_failure();
    test_readback_failure();
    test_init_sequence();
    test_quantization();
    test_current_errors();
    test_fast_charge();
    test_monitor_en_term();
    test_monitor_kick();
    test_monitor_uv();
    test_pace();
    test_suspend_resume();
    test_private_unlock_failure();
    test_private_write_failure();
    puts("PASS: CX25601N init write sequence, 80mA quantization with IINDPM sync and readback, "
         "fast-charge switching, EN_TERM/kick/UV monitor branches, 2s/10s pace, "
         "suspend/resume bus invariant, post-init pending preference and private-register "
         "failure tolerance");
    return 0;
}
'''


def build(source, output, includes, sanitize):
    command = BASE + (['-fsanitize=address,undefined'] if sanitize else [])
    command += ['-I' + str(include) for include in includes]
    command += [str(source), '-o', str(output)]
    # 首次尝试静默失败，由回退构建负责报告真正的编译错误。
    # Keep the first attempt quiet; the fallback build reports real compile errors.
    subprocess.run(command, check=True, capture_output=not sanitize)


with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    for name, content in STUB_HEADERS.items():
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    driver = (ROOT / 'components/metalio_hw/metalio_charger.c').as_posix()
    (work / 'test.c').write_text(UNIT.replace('DRIVER', driver))
    includes = [work, ROOT / 'components/metalio_hw/include']
    try:
        build(work / 'test.c', work / 'test', includes, True)
    except subprocess.CalledProcessError:
        # 本机 MinGW 可能没有 libasan/libubsan；回退到不带 sanitizer 的构建。
        # The local MinGW may lack libasan/libubsan; retry without sanitizers.
        print('note: -fsanitize=address,undefined unavailable, building without it')
        build(work / 'test.c', work / 'test', includes, False)
    subprocess.run([str(work / 'test')], check=True)

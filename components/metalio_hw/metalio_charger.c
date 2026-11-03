/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：CX25601N 可编程充电驱动。负责初始化默认充电参数、按 80mA 步进设置
 * ICHG 并同步 IINDPM、后台维持 VREG/EN_TERM/再充 kick/欠压 BATFET 保护，
 * 并为睡眠与断电事务提供监控挂起。参考官方 S31 仓库的 cx25601n 实现。
 * English: Programmable CX25601N charger driver. Applies default charge settings,
 * sets ICHG in 80mA steps with IINDPM kept in sync, runs the VREG/EN_TERM/recharge-kick/
 * undervoltage-BATFET monitor, and offers monitor suspension for sleep/power transactions.
 * Ported from the vendor S31 repository's cx25601n implementation.
 *
 * 冻结：不移植厂商电量显示策略，不做 IR 自适应抬压；不写永久 OTP/eFuse，
 * 不碰 TPS65185 VCOM、供电顺序与面板波形。寄存器读写统一走 metalio_hw 的总线互斥。
 * Frozen: Do not port the vendor SOC/voltage display policy or IR-compensated VREG;
 * never write permanent OTP/eFuse, touch TPS65185 VCOM, rail ordering or panel waveforms.
 * All register I/O goes through the metalio_hw bus mutex.
 */
#include "metalio_charger.h"

#include <stdatomic.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "metalio_hw.h"

/* ---- 常量 / Constants ---- */
static const char *TAG = "metalio_charger";

#define CX_I2C_ADDRESS 0x6Bu
// BQ27220 电量计与 CX25601N 同总线；监控任务直接读电压/电流，不依赖 read_pico_pmu。
// The BQ27220 gauge shares the bus; the monitor reads voltage/current directly without read_pico_pmu.
#define CX_GAUGE_ADDRESS 0x55u
#define CX_GAUGE_VOLTAGE 0x08u
#define CX_GAUGE_CURRENT 0x0Cu

#define CX_REG_ICHG_LO   0x02u
#define CX_REG_ICHG_HI   0x03u
#define CX_REG_VREG_LO   0x04u
#define CX_REG_VREG_HI   0x05u
#define CX_REG_IINDPM_LO 0x06u
#define CX_REG_IINDPM_HI 0x07u
#define CX_REG_IPRECHG_LO 0x10u
#define CX_REG_IPRECHG_HI 0x11u
#define CX_REG_ITERM_LO  0x12u
#define CX_REG_ITERM_HI  0x13u
#define CX_REG_CHG_CTRL0 0x14u
#define CX_REG_CHG_TMR   0x15u
#define CX_REG_CHG_CTRL1 0x16u
#define CX_REG_CHG_CTRL3 0x18u
#define CX_REG_STATUS1   0x1Eu
#define CX_REG_PART_INFO 0x38u
#define CX_REG_UNLOCK    0x70u

#define CX_ICHG_MIN_MA    80u
#define CX_ICHG_MAX_MA    3040u
#define CX_ICHG_STEP_MA   80u
#define CX_ICHG_CODE_MAX  38u
#define CX_IINDPM_MIN_MA  100u
#define CX_IINDPM_MAX_MA  3000u
#define CX_IINDPM_STEP_MA 20u
#define CX_VREG_MIN_MV    3840u
#define CX_VREG_MAX_MV    4800u
#define CX_VREG_STEP_MV   10u
#define CX_DEFAULT_VREG_MV 4400u
#define CX_DEFAULT_ICHG_MA 500u
#define CX_CHRG_STAT_NOT  0u
// 无输入且 VBAT≤3.1V 进入 BATFET Shutdown；回升到滞回点以上才允许再次触发。
// Enter BATFET shutdown without input at VBAT≤3.1V; re-arm only after the hysteresis point.
#define CX_UV_BATFET_MV   3100
#define CX_UV_HYSTERESIS_MV 100

/* ---- 状态 / State ---- */
static SemaphoreHandle_t s_lock;       // 串行化整段读-改-写序列 / Serializes whole read-modify-write sequences
static SemaphoreHandle_t s_run_sem;    // 恢复时唤醒监控任务 / Wakes the monitor on resume
static TaskHandle_t s_monitor_task;
static bool s_ready;
static bool s_suspended;
static atomic_bool s_monitor_running = true;
static uint32_t s_ichg_ma;
static uint32_t s_vreg_target_mv = CX_DEFAULT_VREG_MV;
static uint32_t s_vreg_applied_mv;
static bool s_fast_charge;
static bool s_batfet_shutdown_done;
// 最近一次在锁内采到的电压，只用于延时节奏；初值低于截止，首次巡检按快速节奏开始。
// Last voltage sampled under the lock, used only to pick the pace; the initial value sits below
// the cutoff so the first poll starts at the fast pace.
#define CX_FIRST_PACE_VBAT_MV 3700
static int32_t s_last_vbat_mv = CX_FIRST_PACE_VBAT_MV;
// 未就绪时记录的期望设置（例如开机时先应用快充偏好、驱动稍后由适配器重试成功）。
// 初始化成功写完默认序列后补应用一次，避免保存的偏好丢失。
// Settings requested before initialization (for example the boot-time fast-charge preference
// applied before the adapter's retry succeeds). Applied once right after the init defaults so a
// saved preference is never lost.
static bool s_desired_valid;
static uint32_t s_desired_ma;
static bool s_desired_fast;

/* ---- 寄存器访问 / Register access ---- */
// 以下函数都要求调用方已持有 s_lock；整段序列必须原子，避免两个任务交错。
// Callers must hold s_lock: a whole sequence has to stay atomic against other tasks.
static bool lock_take(void) {
    return s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}
static void lock_give(void) { xSemaphoreGive(s_lock); }

static esp_err_t read_byte(uint8_t reg, uint8_t *val) {
    if (!val) return ESP_ERR_INVALID_ARG;
    return metalio_read(CX_I2C_ADDRESS, reg, val, 1);
}

static esp_err_t write_byte(uint8_t reg, uint8_t val) {
    return metalio_write8(CX_I2C_ADDRESS, reg, val);
}

static esp_err_t update_bits(uint8_t reg, uint8_t mask, uint8_t shift, uint8_t field) {
    uint8_t current = 0;
    esp_err_t err = read_byte(reg, &current);
    if (err != ESP_OK) return err;
    const uint8_t bits = (uint8_t)(mask << shift);
    const uint8_t next = (uint8_t)((current & (uint8_t)~bits) | (uint8_t)((field << shift) & bits));
    return write_byte(reg, next);
}

static esp_err_t read_bits(uint8_t reg, uint8_t mask, uint8_t shift, uint8_t *field) {
    uint8_t current = 0;
    if (!field) return ESP_ERR_INVALID_ARG;
    esp_err_t err = read_byte(reg, &current);
    if (err != ESP_OK) return err;
    *field = (uint8_t)((current >> shift) & mask);
    return ESP_OK;
}

/* ---- 电量计采样 / Gauge sampling ---- */
// 电压 0x55/0x08 为无符号 mV，电流 0x55/0x0C 为有符号 mA；读失败按 0 处理。
// Voltage 0x55/0x08 is unsigned mV and current 0x55/0x0C signed mA; failed reads return 0.
static int32_t gauge_voltage_mv(void) {
    uint8_t raw[2] = {0, 0};
    if (metalio_read(CX_GAUGE_ADDRESS, CX_GAUGE_VOLTAGE, raw, sizeof(raw)) != ESP_OK) return 0;
    return (int32_t)((uint16_t)raw[0] | (uint16_t)((uint16_t)raw[1] << 8));
}

static int32_t gauge_current_ma(void) {
    uint8_t raw[2] = {0, 0};
    if (metalio_read(CX_GAUGE_ADDRESS, CX_GAUGE_CURRENT, raw, sizeof(raw)) != ESP_OK) return 0;
    return (int32_t)(int16_t)((uint16_t)raw[0] | (uint16_t)((uint16_t)raw[1] << 8));
}

/* ---- 充电参数 / Charge parameters ---- */
static esp_err_t set_vreg_mv_locked(uint32_t mv) {
    if (mv < CX_VREG_MIN_MV) mv = CX_VREG_MIN_MV;
    if (mv > CX_VREG_MAX_MV) mv = CX_VREG_MAX_MV;
    const uint32_t code = mv / CX_VREG_STEP_MV;
    esp_err_t err = update_bits(CX_REG_VREG_LO, 0x1Fu, 3, (uint8_t)(code & 0x1Fu));
    if (err == ESP_OK) err = update_bits(CX_REG_VREG_HI, 0x0Fu, 0, (uint8_t)((code >> 5) & 0x0Fu));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "set VREG=%lu mV (code=%lu)", (unsigned long)(code * CX_VREG_STEP_MV),
                 (unsigned long)code);
    } else {
        ESP_LOGW(TAG, "set VREG failed: %s", esp_err_to_name(err));
    }
    return err;
}

// 目标已写成功才更新缓存；监控任务只在目标与缓存不同时补写。
// Cache only successful writes; the monitor rewrites only when target and cache differ.
static void apply_vreg_fixed_locked(uint32_t mv) {
    if (mv < CX_VREG_MIN_MV) mv = CX_VREG_MIN_MV;
    if (mv > CX_VREG_MAX_MV) mv = CX_VREG_MAX_MV;
    if (mv == s_vreg_applied_mv) return;
    if (set_vreg_mv_locked(mv) == ESP_OK) s_vreg_applied_mv = mv;
}

static uint32_t iindpm_code_for(uint32_t ma) {
    if (ma < CX_IINDPM_MIN_MA) ma = CX_IINDPM_MIN_MA;
    if (ma > CX_IINDPM_MAX_MA) ma = CX_IINDPM_MAX_MA;
    uint32_t code = ma / CX_IINDPM_STEP_MA;
    if (code < 5) code = 5;
    if (code > 150) code = 150;
    return code;
}

// 写 ICHG 时同步 IINDPM，避免输入限流低于充电电流。/ Keep IINDPM at or above ICHG.
static esp_err_t set_iindpm_ma_locked(uint32_t ma) {
    const uint32_t code = iindpm_code_for(ma);
    esp_err_t err = update_bits(CX_REG_IINDPM_LO, 0x0Fu, 4, (uint8_t)(code & 0x0Fu));
    if (err == ESP_OK) err = update_bits(CX_REG_IINDPM_HI, 0x0Fu, 0, (uint8_t)((code >> 4) & 0x0Fu));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "set IINDPM=%lu mA (code=%lu)", (unsigned long)(code * CX_IINDPM_STEP_MA),
                 (unsigned long)code);
    }
    return err;
}

// ICHG 按 80mA 向下量化，写入后回读 ICHG 与 IINDPM；芯片未接受时返回错误而不是虚报成功。
// Quantize ICHG down in 80mA steps, then verify both ICHG and IINDPM by readback.
static esp_err_t set_ichg_ma_locked(uint32_t ma, uint32_t *applied_ma) {
    if (ma < CX_ICHG_MIN_MA) ma = CX_ICHG_MIN_MA;
    if (ma > CX_ICHG_MAX_MA) ma = CX_ICHG_MAX_MA;
    uint32_t code = ma / CX_ICHG_STEP_MA;
    if (code < 1) code = 1;
    if (code > CX_ICHG_CODE_MAX) code = CX_ICHG_CODE_MAX;
    const uint32_t applied = code * CX_ICHG_STEP_MA;

    esp_err_t err = update_bits(CX_REG_ICHG_LO, 0x03u, 6, (uint8_t)(code & 0x03u));
    if (err == ESP_OK) err = update_bits(CX_REG_ICHG_HI, 0x0Fu, 0, (uint8_t)((code >> 2) & 0x0Fu));
    if (err == ESP_OK) err = set_iindpm_ma_locked(applied);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set ICHG=%lu mA failed: %s", (unsigned long)applied, esp_err_to_name(err));
        return err;
    }

    uint8_t lo = 0, hi = 0;
    err = read_bits(CX_REG_ICHG_LO, 0x03u, 6, &lo);
    if (err == ESP_OK) err = read_bits(CX_REG_ICHG_HI, 0x0Fu, 0, &hi);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ICHG readback failed: %s", esp_err_to_name(err));
        return err;
    }
    if (((uint32_t)lo | ((uint32_t)hi << 2)) != code) {
        ESP_LOGW(TAG, "ICHG readback mismatch: code=%lu", (unsigned long)(lo | ((uint32_t)hi << 2)));
        return ESP_ERR_INVALID_RESPONSE;
    }
    const uint32_t iindpm_code = iindpm_code_for(applied);
    err = read_bits(CX_REG_IINDPM_LO, 0x0Fu, 4, &lo);
    if (err == ESP_OK) err = read_bits(CX_REG_IINDPM_HI, 0x0Fu, 0, &hi);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "IINDPM readback failed: %s", esp_err_to_name(err));
        return err;
    }
    if (((uint32_t)lo | ((uint32_t)hi << 4)) != iindpm_code) {
        ESP_LOGW(TAG, "IINDPM readback mismatch: code=%lu",
                 (unsigned long)(lo | ((uint32_t)hi << 4)));
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "set ICHG=%lu mA (code=%lu)", (unsigned long)applied, (unsigned long)code);
    if (applied_ma) *applied_ma = applied;
    return ESP_OK;
}

static esp_err_t read_chrg_stat_locked(uint8_t *stat) {
    return read_bits(CX_REG_STATUS1, 0x03u, 3, stat);
}

static esp_err_t read_vbus_stat_locked(uint8_t *stat) {
    return read_bits(CX_REG_STATUS1, 0x07u, 0, stat);
}

// EN_TERM 是 CHG_CTRL0 的 bit2；电压未到截止前关闭，避免 ITERM 提前终止。
// EN_TERM is CHG_CTRL0 bit2; clear it below the cutoff so ITERM cannot terminate early.
static void set_en_term_locked(bool enable) {
    (void)update_bits(CX_REG_CHG_CTRL0, 0x01u, 2, enable ? 1u : 0u);
}

static esp_err_t enter_batfet_shutdown_locked(void) {
    esp_err_t err = write_byte(CX_REG_CHG_CTRL3, 0x01);
    if (err == ESP_OK) ESP_LOGI(TAG, "REG0x18=0x01 (BATFET shutdown)");
    return err;
}

/* ---- 初始化 / Initialization ---- */
static void write_private_locked(uint8_t reg, uint8_t value) {
    // 私有寄存器失败只告警，不阻断公开充电配置。
    // A private-register failure only warns; public charging setup continues.
    esp_err_t err = write_byte(reg, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "private write 0x%02X=0x%02X failed: %s", (unsigned)reg, (unsigned)value,
                 esp_err_to_name(err));
    }
}

// 照搬厂商解锁序列；失败只告警，后续私有写入可能不生效但公开配置仍可完成。
// Vendor unlock sequence; a failure only warns, leaving public setup intact.
static void unlock_private(bool enable) {
    if (!enable) {
        (void)write_byte(CX_REG_UNLOCK, 0x00);
        return;
    }
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        (void)write_byte(CX_REG_UNLOCK, 0x00);
        (void)write_byte(CX_REG_UNLOCK, 0x50);
        (void)write_byte(CX_REG_UNLOCK, 0x57);
        (void)write_byte(CX_REG_UNLOCK, 0x44);
        uint8_t value = 0;
        if (read_byte(CX_REG_UNLOCK, &value) == ESP_OK && value == 0x03) return;
    }
    ESP_LOGW(TAG, "private register unlock failed");
}

// 顺序与厂商 hw_init_defaults 一致：DPDM、HIZ、WDT、IPRECHG、ITERM、VREG、ICHG 500、
// 私有解锁与 0x86/0x3A/0x46、CHG_CTRL0/3 及补充位、最后 EN_CHG。
// Order matches vendor hw_init_defaults: DPDM, HIZ, WDT, IPRECHG, ITERM, VREG, ICHG 500,
// private unlock plus 0x86/0x3A/0x46, CHG_CTRL0/3 extras, then EN_CHG last.
#define CX_RETURN_ON_ERROR(call)                                                          \
    do {                                                                                  \
        esp_err_t cx_err_ = (call);                                                       \
        if (cx_err_ != ESP_OK) {                                                          \
            ESP_LOGE(TAG, "%s failed: %s", #call, esp_err_to_name(cx_err_));              \
            return cx_err_;                                                               \
        }                                                                                 \
    } while (0)

static esp_err_t hw_init_defaults_locked(void) {
    // 厂商 set_dis_dpdm(true) 实际把 CHG_TMR bit6 清 0，此处按代码复刻。
    // Vendor set_dis_dpdm(true) actually clears CHG_TMR bit6; replicate the code as-is.
    CX_RETURN_ON_ERROR(update_bits(CX_REG_CHG_TMR, 0x01u, 6, 0));
    CX_RETURN_ON_ERROR(update_bits(CX_REG_CHG_CTRL1, 0x01u, 4, 0));  // HIZ 关 / HIZ off
    CX_RETURN_ON_ERROR(update_bits(CX_REG_CHG_CTRL1, 0x03u, 0, 0));  // WDT 关 / WDT off
    CX_RETURN_ON_ERROR(update_bits(CX_REG_IPRECHG_LO, 0x0Fu, 4, 0x0Cu));
    CX_RETURN_ON_ERROR(update_bits(CX_REG_IPRECHG_HI, 0x01u, 0, 0x00u));
    CX_RETURN_ON_ERROR(update_bits(CX_REG_ITERM_LO, 0x1Fu, 3, 0x06u));  // ITERM 60mA
    CX_RETURN_ON_ERROR(update_bits(CX_REG_ITERM_HI, 0x01u, 0, 0x00u));
    CX_RETURN_ON_ERROR(set_vreg_mv_locked(CX_DEFAULT_VREG_MV));
    CX_RETURN_ON_ERROR(set_ichg_ma_locked(CX_DEFAULT_ICHG_MA, NULL));

    unlock_private(true);
    write_private_locked(0x86u, 0x06u);
    write_private_locked(0x3Au, 0x10u);
    write_private_locked(0x46u, 0x20u);
    unlock_private(false);

    (void)update_bits(CX_REG_CHG_CTRL0, 0x01u, 0, 1);
    (void)update_bits(CX_REG_CHG_CTRL3, 0x01u, 2, 0);
    (void)update_bits(0x1Au, 0x01u, 7, 1);
    (void)update_bits(0x23u, 0x07u, 2, 0x07u);
    (void)update_bits(0x24u, 0x01u, 3, 1);

    CX_RETURN_ON_ERROR(update_bits(CX_REG_CHG_CTRL1, 0x01u, 5, 1));  // EN_CHG
    ESP_LOGI(TAG, "hw defaults applied");
    return ESP_OK;
}

#undef CX_RETURN_ON_ERROR

/* ---- 监控任务 / Monitor task ---- */
static void monitor_wait_running(void) {
    // 挂起期间阻塞，不空转；resume 给出信号后重新检查运行标志。
    // Block while suspended instead of spinning; resume signals and the guard re-checks.
    while (!atomic_load(&s_monitor_running)) {
        (void)xSemaphoreTake(s_run_sem, portMAX_DELAY);
    }
}

// 低于截止电压时约2s巡检一次，否则约10s；节奏与厂商驱动一致。
// Poll about every2s below the cutoff and about every10s otherwise, as in the vendor driver.
static uint32_t monitor_pace_ms(int32_t vbat) {
    const bool below_cutoff = s_vreg_target_mv >= CX_VREG_MIN_MV &&
        s_vreg_target_mv <= CX_VREG_MAX_MV && vbat >= 2500 &&
        (uint32_t)vbat < s_vreg_target_mv;
    return below_cutoff ? 2000u : 10000u;
}

// 已终止但电压仍低于截止时，关 EN_TERM 并 toggle EN_CHG 强制再开一轮；
// 整段持有 s_lock（含两次短延时），保证睡眠挂起不会落在半完成状态。
// Terminated below the cutoff: drop EN_TERM and toggle EN_CHG to force another cycle.
// The whole sequence holds s_lock (including two short delays) so a sleep suspend never
// lands on a half-finished kick.
static void kick_recharge_locked(int32_t vbat, uint32_t vreg_target) {
    ESP_LOGI(TAG, "recharge kick: vbat=%ldmV < VREG=%lumV", (long)vbat,
             (unsigned long)vreg_target);
    apply_vreg_fixed_locked(vreg_target);
    set_en_term_locked(false);
    (void)update_bits(CX_REG_CHG_CTRL1, 0x01u, 5, 0);
    (void)update_bits(CX_REG_CHG_CTRL1, 0x01u, 4, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    (void)update_bits(CX_REG_CHG_CTRL1, 0x01u, 4, 0);
    (void)update_bits(CX_REG_CHG_CTRL1, 0x01u, 5, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t stat = 0;
    const bool ok = read_chrg_stat_locked(&stat) == ESP_OK;
    ESP_LOGI(TAG, "recharge after: stat=%u (%s)", (unsigned)stat,
             ok ? "read ok" : "read fail");
}

// 一次完整采样：固定 VREG、动态 EN_TERM、再充 kick、一次欠压 BATFET 保护。
// 所有总线访问都发生在 monitor_work 内部：先持 s_lock，再在锁内复查挂起标志；
// suspend 置标志后，监控任务不会再发起任何 I2C。
// One full sample: fixed VREG, dynamic EN_TERM, recharge kick and one-shot UV BATFET protection.
// Every bus access happens inside monitor_work: it takes s_lock first and re-checks the suspend
// flag under that lock, so after suspend() sets the flag the monitor issues no further I2C.
static void monitor_work(void) {
    if (!lock_take()) return;
    // 挂起可能恰好发生在调用前；持锁后再确认一次，保证挂起返回后不再有总线访问。
    // A suspend may have landed just before this call; re-check under the lock so no bus access
    // happens after suspend() returns.
    if (!atomic_load(&s_monitor_running)) {
        lock_give();
        return;
    }
    const uint32_t vreg_target = s_vreg_target_mv;
    if (vreg_target < CX_VREG_MIN_MV || vreg_target > CX_VREG_MAX_MV) {
        lock_give();
        return;
    }
    apply_vreg_fixed_locked(vreg_target);

    const int32_t vbat = gauge_voltage_mv();
    s_last_vbat_mv = vbat;
    const int32_t ima = gauge_current_ma();
    uint8_t stat = 0;
    const bool have_stat = read_chrg_stat_locked(&stat) == ESP_OK;
    const bool idle = have_stat && stat == CX_CHRG_STAT_NOT;
    const bool vbat_below = vbat >= 2500 && (uint32_t)vbat < vreg_target;

    // 欠压保护：无 VBUS 且 VBAT≤3.1V → REG0x18=0x01 关 BATFET；每个上电周期只触发一次。
    // UV protection: no VBUS and VBAT≤3.1V → REG0x18=0x01 BATFET shutdown; one shot per power cycle.
    if (!s_batfet_shutdown_done && vbat >= 2500 && vbat <= CX_UV_BATFET_MV) {
        uint8_t vbus = 0;
        const bool have_vbus = read_vbus_stat_locked(&vbus) == ESP_OK;
        if (have_vbus && vbus == 0) {
            ESP_LOGW(TAG, "UV protect: vbat=%ldmV, no VBUS → BATFET shutdown", (long)vbat);
            if (enter_batfet_shutdown_locked() == ESP_OK) s_batfet_shutdown_done = true;
        }
    } else if (s_batfet_shutdown_done && vbat > CX_UV_BATFET_MV + CX_UV_HYSTERESIS_MV) {
        // 电压回升到滞回点以上才重新允许一次欠压保护。
        // Re-arm only after the voltage recovers above the hysteresis point.
        s_batfet_shutdown_done = false;
    }

    set_en_term_locked(!vbat_below);
    if (idle && ima < 5 && vbat_below) kick_recharge_locked(vbat, vreg_target);
    lock_give();
}

static void charger_monitor_task(void *arg) {
    (void)arg;
    for (;;) {
        monitor_wait_running();
        // 节奏只使用上一轮锁内采样值，此处不访问总线：所有 I2C 都发生在 monitor_work
        // 持锁并复查挂起标志之后，suspend 返回后不可能再有监控总线访问。
        // Pace from the previous in-lock sample only; no bus access here. All I2C happens inside
        // monitor_work after taking the lock and re-checking the suspend flag, so no monitor bus
        // access can follow a returned suspend().
        vTaskDelay(pdMS_TO_TICKS(monitor_pace_ms(s_last_vbat_mv)));
        monitor_wait_running();
        monitor_work();
    }
}

/* ---- 公开 API / Public API ---- */
esp_err_t metalio_charger_init(void) {
    if (s_ready) return ESP_OK;
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }
    if (!s_run_sem) {
        s_run_sem = xSemaphoreCreateBinary();
        if (!s_run_sem) return ESP_ERR_NO_MEM;
    }

    uint8_t part = 0;
    esp_err_t err = read_byte(CX_REG_PART_INFO, &part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "probe PART_INFO(0x38) failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "PART_INFO=0x%02X @0x%02X", (unsigned)part, (unsigned)CX_I2C_ADDRESS);

    if (!lock_take()) return ESP_ERR_TIMEOUT;
    err = hw_init_defaults_locked();
    uint32_t applied = 0;
    bool pending_applied = false;
    bool pending_fast = false;
    if (err == ESP_OK) err = set_ichg_ma_locked(METALIO_CHARGER_STD_MA, &applied);
    if (err == ESP_OK && s_desired_valid) {
        // 初始化成功后再补写未就绪期间记录的偏好；补写失败只告警，默认标准电流仍生效。
        // Apply the pre-init preference after the defaults; a failed write only warns and keeps
        // the initialized standard current.
        const uint32_t desired = s_desired_ma;
        pending_fast = s_desired_fast;
        uint32_t desired_applied = 0;
        const esp_err_t pending = set_ichg_ma_locked(desired, &desired_applied);
        s_desired_valid = false;  // 无论成败只补一次 / consume once regardless of the result
        if (pending == ESP_OK) {
            applied = desired_applied;
            pending_applied = true;
            ESP_LOGI(TAG, "applied pending preference: %lu mA (fast %s)",
                     (unsigned long)applied, pending_fast ? "on" : "off");
        } else {
            ESP_LOGW(TAG, "pending preference apply failed: %s", esp_err_to_name(pending));
        }
    }
    lock_give();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ichg_ma = applied;
    s_fast_charge = pending_applied ? pending_fast : false;
    s_vreg_target_mv = CX_DEFAULT_VREG_MV;
    s_vreg_applied_mv = CX_DEFAULT_VREG_MV;
    s_batfet_shutdown_done = false;
    s_last_vbat_mv = CX_FIRST_PACE_VBAT_MV;
    s_suspended = false;
    atomic_store(&s_monitor_running, true);
    if (xTaskCreate(charger_monitor_task, "metalio_chg", 3072, NULL, 5, &s_monitor_task) != pdPASS) {
        ESP_LOGE(TAG, "monitor task unavailable");
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    ESP_LOGI(TAG, "ready @0x%02X, ICHG=%lu mA (fast %s)", (unsigned)CX_I2C_ADDRESS,
             (unsigned long)s_ichg_ma, s_fast_charge ? "on" : "off");
    return ESP_OK;
}

bool metalio_charger_ready(void) { return s_ready; }

esp_err_t metalio_charger_set_current_ma(uint32_t ma) {
    if (!s_ready) {
        // 未就绪时只记录目标，等初始化成功后补写；调用方仍得到 INVALID_STATE。
        // Not ready: record the target for the post-init apply and still report INVALID_STATE.
        s_desired_valid = true;
        s_desired_ma = ma;
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock_take()) return ESP_ERR_TIMEOUT;
    uint32_t applied = 0;
    const esp_err_t err = set_ichg_ma_locked(ma, &applied);
    lock_give();
    if (err == ESP_OK) s_ichg_ma = applied;
    return err;
}

uint32_t metalio_charger_current_ma(void) { return s_ready ? s_ichg_ma : 0; }

esp_err_t metalio_charger_set_fast_charge(bool on) {
    if (!s_ready) {
        // 开机时偏好可能早于驱动就绪；记录快充目标，init 成功后补应用。
        // At boot the preference may arrive before the driver is ready; record it for the
        // post-init apply.
        s_desired_valid = true;
        s_desired_ma = on ? METALIO_CHARGER_FAST_MA : METALIO_CHARGER_STD_MA;
        s_desired_fast = on;
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = metalio_charger_set_current_ma(
        on ? METALIO_CHARGER_FAST_MA : METALIO_CHARGER_STD_MA);
    if (err == ESP_OK) {
        s_fast_charge = on;
        ESP_LOGI(TAG, "fast charge %s (%lu mA)", on ? "on" : "off",
                 (unsigned long)s_ichg_ma);
    } else {
        ESP_LOGW(TAG, "fast charge %s failed: %s", on ? "on" : "off", esp_err_to_name(err));
    }
    return err;
}

bool metalio_charger_fast_charge(void) { return s_fast_charge; }

void metalio_charger_suspend(void) {
    if (!s_ready || s_suspended) return;
    // 在模块锁内清除运行标志：监控任务每次 I2C 序列前都会在锁内自检，
    // 因此本函数返回后不会再有监控访问总线。
    // Clear the run flag under the module lock; the monitor re-checks it under the same lock
    // before every I2C sequence, so no monitor bus access survives this call.
    if (lock_take()) {
        atomic_store(&s_monitor_running, false);
        lock_give();
    }
    s_suspended = true;
    // 锁外给监控任务一个调度点收尾，不把延时放在持锁状态。
    // Give the monitor a scheduling point outside the lock.
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_LOGI(TAG, "monitor suspended");
}

void metalio_charger_resume(void) {
    if (!s_ready || !s_suspended) return;
    s_suspended = false;
    atomic_store(&s_monitor_running, true);
    (void)xSemaphoreGive(s_run_sem);
    ESP_LOGI(TAG, "monitor resumed");
}

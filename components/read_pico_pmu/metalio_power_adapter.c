/* SPDX-License-Identifier: Apache-2.0
 * 中文：BQ27220、PCF8563 和电源键接入 RC90，不访问 CW32 寄存器。
 * English: BQ27220, PCF8563 and the power key adapt RC90 without CW32 access.
 */
#include "read_pico_pmu.h"
#include "power_double_click.h"
#include "metalio_charger.h"
#include "metalio_soc_filter.h"
#include "metalio_hw.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <string.h>
static pmu_snapshot_t state;
static metalio_soc_filter_t s_soc_filter;
static bool ready,held,long_sent,candidate;
static bool keys_paused;
static uint32_t key_epoch;
static uint32_t s_lock_key_serial;
static int64_t candidate_since,down_since,battery_at;
static bool charger_initialized;
static int64_t charger_retry_at,charge_valid_at;
static read_pico_pmu_key_action_t action;
static power_double_click_t s_wake_click;
static TaskHandle_t key_task;
static portMUX_TYPE key_lock=portMUX_INITIALIZER_UNLOCKED;
// 由原主线程在初始化/唤醒边界重置显示滤波；原始电压仍供硬件状态读取。
// The original main thread resets display filtering at init/wake boundaries; raw voltage remains available.
static void battery_reset(void) {
    metalio_soc_filter_reset(&s_soc_filter,esp_timer_get_time());
    state.flags&=~(PMU_STATUS_BATTERY_VALID|PMU_STATUS_CHARGING_ACTIVE);battery_at=0;
}
static unsigned unbcd(unsigned v) { return (v&15)>9 || (v>>4)>9 ? 255 : (v>>4)*10+(v&15); }
static unsigned bcd(unsigned v) { return (v/10<<4)|(v%10); }
static esp_err_t rtc_read(void) {
    uint8_t regs[9]; esp_err_t err=metalio_read(0x51,0,regs,sizeof(regs)); state.time_ok=false; state.time_synced=0;
    const uint8_t *r=regs+2;
    if (err!=ESP_OK) return err;
    if ((regs[0]&0x20) || (r[0]&0x80)) return ESP_ERR_INVALID_STATE;
    struct tm t={.tm_sec=unbcd(r[0]&0x7f),.tm_min=unbcd(r[1]&0x7f),.tm_hour=unbcd(r[2]&0x3f),
        .tm_mday=unbcd(r[3]&0x3f),.tm_mon=unbcd(r[5]&0x1f)-1,.tm_year=100+unbcd(r[6]),.tm_isdst=0};
    if (t.tm_sec>59 || t.tm_min>59 || t.tm_hour>23 || t.tm_mday<1 || t.tm_mday>31 || t.tm_mon<0 || t.tm_mon>11) return ESP_ERR_INVALID_RESPONSE;
    if (t.tm_year>199) return ESP_ERR_INVALID_RESPONSE;
    const struct tm requested=t;
    time_t sec=timegm(&t); if (sec<946684800 || sec>4102444799LL) return ESP_ERR_INVALID_STATE;
    // timegm会归一化2月31日，RTC坏日期必须拒绝。/ Reject calendar normalization of invalid RTC dates.
    if (t.tm_mday!=requested.tm_mday || t.tm_mon!=requested.tm_mon || t.tm_year!=requested.tm_year)
        return ESP_ERR_INVALID_RESPONSE;
    state.unix_sec=(uint32_t)sec; state.time_synced=1; state.time_ok=true; return ESP_OK;
}
static esp_err_t rtc_write(uint32_t sec) {
    time_t seconds=sec; struct tm t; gmtime_r(&seconds,&t);
    if (t.tm_year<100 || t.tm_year>199) return ESP_ERR_INVALID_ARG;
    uint8_t bytes[]={bcd(t.tm_sec),bcd(t.tm_min),bcd(t.tm_hour),bcd(t.tm_mday),t.tm_wday,bcd(t.tm_mon+1),bcd(t.tm_year-100)};
    uint8_t ctrl;
    esp_err_t err=metalio_read(0x51,0,&ctrl,1); if(err!=ESP_OK) return err;
    // 写时暂时停表，完成或失败都恢复振荡器；保留其他控制位。
    // Stop briefly for the write, then restore the oscillator on success or failure, preserving other control bits.
    err=metalio_write8(0x51,0,ctrl|0x20); if(err!=ESP_OK) return err;
    esp_err_t written=metalio_write(0x51,2,bytes,sizeof(bytes));
    esp_err_t resumed=metalio_write8(0x51,0,ctrl&~0x20);
    err=written!=ESP_OK?written:resumed;
    if(err==ESP_OK) err=rtc_read();
    ESP_LOGI("metalio_rtc","sync utc=%lu oscillator=%s result=%s",(unsigned long)sec,
        resumed==ESP_OK?"running":"unknown",esp_err_to_name(err));
    return err;
}
static void keys(void) {
    portENTER_CRITICAL(&key_lock);
    bool paused=keys_paused; uint32_t epoch=key_epoch;
    portEXIT_CRITICAL(&key_lock);
    if (paused) return;
    // 独立GPIO先采样，扩展口读错不吞AI键；暂停事务仍由epoch及清队列保护。
    // Sample the dedicated pin before I2C, so a failed expander read cannot swallow AI input.
    bool ai_down=gpio_get_level(GPIO_NUM_61)==0;
    portENTER_CRITICAL(&key_lock);
    if(!keys_paused&&epoch==key_epoch)metalio_ai_sample(ai_down,esp_timer_get_time()/1000);
    portEXIT_CRITICAL(&key_lock);
    uint16_t inputs;
    if (metalio_io_read(&inputs)!=ESP_OK) return;
    int64_t now=esp_timer_get_time()/1000; bool down=!(inputs&(1u<<15));
    portENTER_CRITICAL(&key_lock);
    if (keys_paused || epoch!=key_epoch) { portEXIT_CRITICAL(&key_lock); return; }
    if (down!=candidate) { candidate=down; candidate_since=now; }
    if (candidate!=held && now-candidate_since>=25) {
        held=candidate;
        if (held) { down_since=now; long_sent=false; }
        else if (!long_sent && now-down_since>=25) {
            action=READ_PICO_PMU_KEY_SHORT;
            ++s_lock_key_serial;
            power_double_click_push(&s_wake_click,(uint32_t)now);
        }
    }
    if (held && !long_sent && now-down_since>=700) { action=READ_PICO_PMU_KEY_LONG; s_wake_click=(power_double_click_t){0}; long_sent=true; }
    state.key_state=held?1:0;
    portEXIT_CRITICAL(&key_lock);
    metalio_volume_sample(inputs,now);
}
// 仅观察真实去抖短按，不消费双击或清空队列。/ Observe debounced short releases without consuming wake clicks.
uint32_t read_pico_pmu_lock_key_serial(void) {
    portENTER_CRITICAL(&key_lock);
    uint32_t serial=s_lock_key_serial;
    portEXIT_CRITICAL(&key_lock);
    return serial;
}
esp_err_t read_pico_pmu_keys_pause(void) {
    portENTER_CRITICAL(&key_lock);
    bool can_pause=ready && !held && !candidate && action==READ_PICO_PMU_KEY_NONE;
    if (can_pause) { keys_paused=true; ++key_epoch; }
    portEXIT_CRITICAL(&key_lock);
    if(can_pause)metalio_ai_clear();
    return can_pause?ESP_OK:ESP_ERR_INVALID_STATE;
}
void read_pico_pmu_keys_resume(void) {
    // 唤醒后重新等待3秒采样，避免继续使用锁屏前的显示电量；硬件充电策略不变。
    // Recalibrate display SOC after a3s wake delay instead of reusing stale lock-time values; charging hardware is unchanged.
    battery_reset();
    metalio_ai_clear();
    // 改方向后须两次稳定低电平才承接按住；单次毛刺/读错不能在松开时伪造解锁。
    // Require two stable low samples after direction restore; a glitch/read fault must not synthesize an unlock on release.
    vTaskDelay(pdMS_TO_TICKS(20));
    uint16_t inputs=0xffff; bool valid=metalio_io_read(&inputs)==ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(30));
    uint16_t again=0xffff; bool valid_again=metalio_io_read(&again)==ESP_OK;
    int64_t now=esp_timer_get_time()/1000;
    portENTER_CRITICAL(&key_lock);
    held=candidate=valid && valid_again && !(inputs&(1u<<15)) && !(again&(1u<<15));
    down_since=candidate_since=now; long_sent=false;
    state.key_state=held?1:0;
    ++key_epoch; keys_paused=false;
    portEXIT_CRITICAL(&key_lock);
}
static void key_feed(void *arg) {
    (void)arg;
    for (;;) { keys(); vTaskDelay(pdMS_TO_TICKS(10)); }
}
// 原厂浅睡把EXT1解释为P17按键；TF恢复前按键已松开时仍保留一次短按。
// Vendor light sleep identifies EXT1 as P17; retain one short click when it was released before media restoration.
void read_pico_pmu_latch_power_wakeup(void) {
    portENTER_CRITICAL(&key_lock);
    if (!keys_paused && !held && action==READ_PICO_PMU_KEY_NONE) {
        action=READ_PICO_PMU_KEY_SHORT;
        ++s_lock_key_serial;
        power_double_click_push(&s_wake_click,(uint32_t)(esp_timer_get_time()/1000));
    }
    portEXIT_CRITICAL(&key_lock);
}
static esp_err_t charger_init(void) {
    // 初始化与寄存器策略在 metalio_charger 内；适配器只保留失败重试与日志。
    // Initialization and register policy live in metalio_charger; this adapter only retries and logs.
    esp_err_t err=metalio_charger_init();
    charger_initialized=metalio_charger_ready();
    ESP_LOGI("metalio_power","CX25601N init=%s ready=%u",esp_err_to_name(err),(unsigned)charger_initialized);
    return err;
}
static void battery(void) {
    int64_t now=esp_timer_get_time(); if (battery_at && now-battery_at<1000000) return;
    battery_at=now;
    if (!charger_initialized && now>=charger_retry_at) {
        (void)charger_init(); charger_retry_at=now+30000000;
    }
    const bool was_charging=(state.flags&PMU_STATUS_CHARGING_ACTIVE)!=0;
    uint8_t r[2]; state.flags &= ~(PMU_STATUS_BATTERY_VALID|PMU_STATUS_CHARGING_ACTIVE); state.quick_ok=false;
    if (metalio_read(0x55,0x08,r,2)==ESP_OK) {
        unsigned mv=r[0]|r[1]<<8;
        if (mv>=2500 && mv<=5000) {
            // 只把电压作为本轮原始采样；显示百分比由官方曲线/滤波得到，不读未标定的SOC寄存器。
            // Keep raw voltage here; the vendor curve/filter computes display SOC without the uncalibrated SOC register.
            state.battery_mv=mv;state.flags|=PMU_STATUS_BATTERY_VALID;
        }
    }
    // 官方BQ27220说明：0x0C为有符号mA，±5mA死区；VBUS_STAT因关闭DPDM不能判断插电。
    // Vendor BQ27220 guidance:0x0C is signed mA with a±5mA deadband; disabled DPDM makes VBUS_STAT unsuitable for detecting input.
    const bool current_ok=metalio_read(0x55,0x0c,r,2)==ESP_OK;
    const int current_ma=current_ok?(int16_t)((unsigned)r[0]|(unsigned)r[1]<<8):0;
    int level=0;bool display_charging=false;
    const bool display_valid=metalio_soc_filter_update(&s_soc_filter,
        (state.flags&PMU_STATUS_BATTERY_VALID)&&current_ok,state.battery_mv,(int16_t)current_ma,
        now,&level,&display_charging);
    if(display_valid)state.soc_permille=(unsigned)level*10;
    else state.flags&=~PMU_STATUS_BATTERY_VALID;
    uint8_t charger=0,control=0,otg=0;
    const bool status_ok=metalio_read(0x6b,0x1e,&charger,1)==ESP_OK;
    const bool control_ok=metalio_read(0x6b,0x16,&control,1)==ESP_OK;
    const bool otg_ok=metalio_read(0x6b,0x18,&otg,1)==ESP_OK;
    const bool charger_ok=status_ok&&control_ok&&otg_ok;
    const bool disabled=(control_ok&&!(control&0x20))||(otg_ok&&(otg&0x40));
    bool known=true,charging=false;
    if(disabled)charging=false;
    else if(charger_ok&&((charger>>3)&3)!=0)charging=true;
    else if(current_ok&&current_ma>5)charging=true;
    else if(current_ok&&current_ma< -5)charging=false;
    else if(charger_ok)charging=((charger>>3)&3)!=0;
    else if(!current_ok)known=false;
    if(known)charge_valid_at=now;
    else charging=was_charging&&charge_valid_at&&now-charge_valid_at<6000000;
    state.charge_state=known?(charging?PMU_CHARGE_CHARGING:PMU_CHARGE_NOT_CHARGING):PMU_CHARGE_UNKNOWN;
    // 充电芯片状态用于诊断；状态栏按官方原始电流方向，显示满电时隐藏闪电。
    // Retain charger state for diagnostics; the UI follows vendor current direction and hides lightning at displayed full.
    if(display_valid&&display_charging)state.flags|=PMU_STATUS_CHARGING_ACTIVE;
    state.status_ok=(state.flags&PMU_STATUS_BATTERY_VALID)!=0 || charger_ok || current_ok || (!known&&charging);
    // 单次I2C失败仅短暂保留已确认图标；明确拔电/停止/OTG立即清除，六秒内无有效依据则过期。
    // Briefly retain a confirmed icon through an I2C fault; explicit discharge/disable/OTG clears it immediately, and evidence expires within six seconds.
    static uint32_t previous=UINT32_MAX;
    const uint32_t status=(charger_ok?(uint32_t)charger|((uint32_t)control<<8)|((uint32_t)otg<<16):0x00ffffffu)|
        (charging?0x01000000u:0)|(current_ok?0x02000000u:0);
    if(status!=previous){
        previous=status;
        ESP_LOGI("metalio_power","battery valid=%u mv=%u soc=%u current=%s/%d mA charging=%u charger=%s status=%02x control=%02x otg=%02x",
            (unsigned)((state.flags&PMU_STATUS_BATTERY_VALID)!=0),state.battery_mv,state.soc_permille,
            current_ok?"valid":"unavailable",current_ma,(unsigned)charging,
            charger_ok?"online":"unavailable",charger,control,otg);
    }
}
esp_err_t read_pico_pmu_init(i2c_master_bus_handle_t bus,const read_pico_pmu_config_t *cfg) {
    (void)bus;(void)cfg; ready=metalio_ioe()!=NULL; state.present=ready;
    battery_reset();
    if (ready) { held=candidate=metalio_key_down(); down_since=candidate_since=esp_timer_get_time()/1000; battery(); (void)rtc_read(); }
    if (ready && !key_task && xTaskCreate(key_feed,"metalio_keys",2048,NULL,5,&key_task)!=pdPASS) ready=false;
    return ready?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t read_pico_pmu_deinit(void) { if(key_task) {vTaskDelete(key_task);key_task=NULL;} ready=false; return ESP_OK; }
bool read_pico_pmu_ready(void) { return ready; }
const pmu_snapshot_t *read_pico_pmu_get(void) { return &state; }
esp_err_t read_pico_pmu_poll(void) { if (!ready) return ESP_ERR_INVALID_STATE; battery(); return ESP_OK; }
esp_err_t read_pico_pmu_refresh(void) { esp_err_t e=read_pico_pmu_poll(); (void)rtc_read(); return e; }
esp_err_t read_pico_pmu_cmd(uint16_t code,const uint8_t *p,uint8_t n) {
    if (code==PMU_CMD_TIME_GET) return rtc_read();
    if (code==PMU_CMD_TIME_SYNC && p && n==4) return rtc_write((uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24);
    if (code==PMU_CMD_HOST_READY) return ESP_OK;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t read_pico_pmu_vcom_get(int *mv) { if (!mv) return ESP_ERR_INVALID_ARG; *mv=1200; return ESP_OK; }
esp_err_t read_pico_pmu_vcom_set(int mv) { (void)mv; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t read_pico_pmu_uid_get(uint8_t out[PMU_CHIP_UID_LEN]) {
    uint8_t uid[PMU_CHIP_UID_LEN]={'M','E','T','A','L','I'};
    esp_err_t err=esp_efuse_mac_get_default(uid+PMU_CHIP_UID_LEN-6);
    if (err==ESP_OK) { memcpy(state.uid,uid,sizeof(uid)); state.uid_ok=true; if(out)memcpy(out,uid,sizeof(uid)); }
    return err;
}
esp_err_t read_pico_pmu_event_ack(uint16_t id) { (void)id; return ESP_OK; }
esp_err_t read_pico_pmu_action(uint8_t a,uint16_t delay,uint16_t reason) { (void)a;(void)delay;(void)reason; return ESP_ERR_NOT_SUPPORTED; }
void read_pico_pmu_drain_events(void) { metalio_volume_clear();
    metalio_ai_clear(); portENTER_CRITICAL(&key_lock); action=READ_PICO_PMU_KEY_NONE; s_wake_click=(power_double_click_t){0}; portEXIT_CRITICAL(&key_lock); }
read_pico_pmu_key_action_t read_pico_pmu_take_key_action(void) {
    portENTER_CRITICAL(&key_lock);
    read_pico_pmu_key_action_t a=action; action=READ_PICO_PMU_KEY_NONE;s_wake_click=(power_double_click_t){0};
    portEXIT_CRITICAL(&key_lock); return a;
}
bool read_pico_pmu_take_key_short(void) { return read_pico_pmu_take_key_action()==READ_PICO_PMU_KEY_SHORT; }
bool read_pico_pmu_take_key_wakeup(void) {
    portENTER_CRITICAL(&key_lock);
    // S31后台已记录去抖短按/长按；持有电平不是新动作，否则锁屏长按会再次唤醒。
    // The S31 sampler queues debounced clicks/holds; a held level is not a new wake action.
    bool wake=power_double_click_take(&s_wake_click); action=READ_PICO_PMU_KEY_NONE;
    portEXIT_CRITICAL(&key_lock); return wake;
}
// 单击模式仍只消费已去抖的新动作；绝不使用当前 held 电平。
// Single-press mode consumes debounced actions, never the current held level.
bool read_pico_pmu_take_key_wakeup_single(void) {
    portENTER_CRITICAL(&key_lock);
    bool wake=action==READ_PICO_PMU_KEY_SHORT || action==READ_PICO_PMU_KEY_LONG;
    action=READ_PICO_PMU_KEY_NONE; s_wake_click=(power_double_click_t){0};
    portEXIT_CRITICAL(&key_lock);
    return wake;
}
esp_err_t read_pico_pmu_report_sleep(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t read_pico_pmu_report_ready(void) { return ready?ESP_OK:ESP_ERR_INVALID_STATE; }
esp_err_t read_pico_pmu_power_off(void) { metalio_poweroff(); return ESP_OK; }
const char *pmu_power_name(uint8_t v) { (void)v; return "Metalio"; }
const char *pmu_charge_name(uint8_t v) { return v==PMU_CHARGE_CHARGING?"charging":v==PMU_CHARGE_NOT_CHARGING?"idle":"unknown"; }
const char *pmu_event_name(uint8_t v) { (void)v; return "Metalio"; }
const char *pmu_status_name(uint16_t v) { (void)v; return "Metalio"; }
const char *pmu_led_name(uint8_t v) { (void)v; return "unsupported"; }
const char *pmu_action_name(uint8_t v) { (void)v; return "unsupported"; }
uint16_t pmu_crc16_ccitt_false(const uint8_t *data,uint16_t length) {
    uint16_t crc=0xffff;
    for (unsigned i=0;i<length;i++) { crc^=(uint16_t)data[i]<<8; for (unsigned b=0;b<8;b++) crc=(crc&0x8000)?(crc<<1)^0x1021:crc<<1; }
    return crc;
}

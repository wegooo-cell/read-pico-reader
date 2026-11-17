/* SPDX-License-Identifier: Apache-2.0
 * 中文：新 I2C 驱动统一管理 S31 外设；失败时关闭 TPS，不保留调试供电。
 * English: Use one new I2C bus for S31 peripherals and shut TPS down on errors.
 */
#include "metalio_hw.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"
#include <string.h>
#include "nvs.h"
#include "soc/cnnt_io_mux_struct.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "esp_private/esp_gpio_reserve.h"
#include "soc/gpio_sig_map.h"
#include <stdatomic.h>
#include "sdkconfig.h"
#include "metalio_early_key_record.h"
#if CONFIG_METALIO_EARLY_POWER_KEY
#include "esp_attr.h"
static RTC_NOINIT_ATTR metalio_early_key_record_t s_boot_key_handoff;
const __attribute__((section(".rodata_custom_desc"),used)) metalio_early_key_app_desc_t metalio_early_key_app_desc={
    .magic=METALIO_EARLY_KEY_DESC_MAGIC,.version=1,.board=METALIO_EARLY_KEY_BOARD,
    .address=(uint32_t)(uintptr_t)&s_boot_key_handoff,.size=sizeof(s_boot_key_handoff)};
#endif

static const char *TAG="metalio_hw";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t devices[128];
static fca9555_handle_t ioe;
static SemaphoreHandle_t mutex;
// 参考板启动时保持 P05 屏电、P12 触摸复位、P14 VOUT 为高。
// Match reference boot rails: P05 display, P12 touch reset, P14 VOUT high.
static uint8_t outputs[2]={0x20,0x14};
static bool tps_on;
static bool boot_power_key;
static bool early_boot_power_key;
static bool tps_prepared;
static int64_t tps_wake_us;
static int tps_last_vcom_mv=-1;
static bool fl_ready;
static uint8_t saved_brightness=30,saved_warmth=50;
static uint8_t last_on_brightness=30;
static uint16_t brightness_position=72,warmth_position=120,last_on_position=72;
static bool light_enabled=true;
static bool light_dirty;
static atomic_bool haptic_enabled=true;
static atomic_bool haptic_stopped;
static SemaphoreHandle_t haptic_mutex;
static TaskHandle_t haptic_task;
static int64_t last_haptic_us;
static portMUX_TYPE haptic_lock=portMUX_INITIALIZER_UNLOCKED;
static void haptic_worker(void *arg);
static void frontlight_load(void);
static portMUX_TYPE volume_lock=portMUX_INITIALIZER_UNLOCKED;
static int volume_events[8];
static unsigned volume_head,volume_count;

i2c_master_bus_handle_t metalio_bus(void) { return bus; }
fca9555_handle_t metalio_ioe(void) { return ioe; }
static esp_err_t get_device(uint8_t address, i2c_master_dev_handle_t *dev) {
    if (!bus || address>=128) return ESP_ERR_INVALID_STATE;
    if (!devices[address]) {
        i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,
            .device_address=address,.scl_speed_hz=100000};
        esp_err_t err=i2c_master_bus_add_device(bus,&cfg,&devices[address]);
        if (err!=ESP_OK) return err;
    }
    *dev=devices[address]; return ESP_OK;
}
esp_err_t metalio_read(uint8_t a,uint8_t r,void *data,size_t n) {
    if (!mutex || !data || !n) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return ESP_ERR_TIMEOUT;
    i2c_master_dev_handle_t dev;
    esp_err_t err=get_device(a,&dev);
    if (err==ESP_OK && a==0x68) {
        // TPS65185 参考驱动在地址写与数据读之间发 STOP，不使用重复 START。
        // The TPS65185 reference driver uses STOP between pointer write and data read.
        err=i2c_master_transmit(dev,&r,1,50);
        if (err==ESP_OK) err=i2c_master_receive(dev,data,n,50);
    } else if (err==ESP_OK) err=i2c_master_transmit_receive(dev,&r,1,data,n,50);
    xSemaphoreGive(mutex); return err;
}
esp_err_t metalio_write(uint8_t a,uint8_t r,const void *data,size_t n) {
    if (!mutex || !data || !n || n>16) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return ESP_ERR_TIMEOUT;
    uint8_t bytes[17]; bytes[0]=r; memcpy(bytes+1,data,n);
    i2c_master_dev_handle_t dev;
    esp_err_t err=get_device(a,&dev);
    if (err==ESP_OK) err=i2c_master_transmit(dev,bytes,n+1,50);
    xSemaphoreGive(mutex); return err;
}
esp_err_t metalio_write8(uint8_t a,uint8_t r,uint8_t value) { return metalio_write(a,r,&value,1); }
// 同一端口的多位在持锁状态一次写入，避免马达/前光任务覆盖TPS位。
// Write a masked port atomically under the shared lock; preserve motor/frontlight bits.
static esp_err_t metalio_io_mask(unsigned port,uint8_t mask,uint8_t value) {
    if(!ioe||port>1)return ESP_ERR_INVALID_STATE;
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE)return ESP_ERR_TIMEOUT;
    uint8_t next=(outputs[port]&~mask)|(value&mask);
    esp_err_t err=fca9555_set_output(ioe,port,next);
    if(err==ESP_OK)outputs[port]=next;
    xSemaphoreGive(mutex);return err;
}
esp_err_t metalio_io_set(unsigned pin,bool high) {
    if(pin>15)return ESP_ERR_INVALID_STATE;
    uint8_t bit=1u<<(pin%8);
    return metalio_io_mask(pin/8,bit,high?bit:0);
}
esp_err_t metalio_io_read(uint16_t *inputs) {
    if (!ioe || !inputs) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return ESP_ERR_TIMEOUT;
    uint8_t p0,p1;
    esp_err_t err=fca9555_read_input(ioe,0,&p0);
    if (err==ESP_OK) err=fca9555_read_input(ioe,1,&p1);
    if (err==ESP_OK) *inputs=(uint16_t)p0|((uint16_t)p1<<8);
    xSemaphoreGive(mutex);
    return err;
}
esp_err_t metalio_prepare_key_wake(void) {
    if (!ioe) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return ESP_ERR_TIMEOUT;
    // 对齐原厂 ioexp_mask_for_sleep / enable_power_wake_pin；不改变正常输出影子。
    // Match vendor ioexp_mask_for_sleep / enable_power_wake_pin, retaining normal output shadows.
    uint8_t ignored;
    esp_err_t err=fca9555_set_output(ioe,0,outputs[0]&0x38);
    if (err==ESP_OK) err=fca9555_set_config(ioe,0,0x00);
    if (err==ESP_OK) err=fca9555_set_output(ioe,1,(outputs[1]&0x14)|0x83);
    if (err==ESP_OK) err=fca9555_set_config(ioe,1,0x00);
    if (err==ESP_OK) err=fca9555_read_input(ioe,0,&ignored);
    if (err==ESP_OK) err=fca9555_read_input(ioe,1,&ignored);
    if (err==ESP_OK) err=fca9555_set_config(ioe,1,0x80);
    xSemaphoreGive(mutex); return err;
}
esp_err_t metalio_restore_key_io(void) {
    if (!ioe) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return ESP_ERR_TIMEOUT;
    // 回退时所有方向都尝试恢复，不能因第一笔写失败而留下输出态按键。
    // Attempt every direction restore even if an earlier transfer fails.
    esp_err_t err=ESP_OK,next;
    uint8_t ignored;
    next=fca9555_set_output(ioe,0,outputs[0]); if(err==ESP_OK) err=next;
    next=fca9555_set_config(ioe,0,0xc0); if(err==ESP_OK) err=next;
    next=fca9555_set_output(ioe,1,outputs[1]); if(err==ESP_OK) err=next;
    next=fca9555_set_config(ioe,1,0xe3); if(err==ESP_OK) err=next;
    next=fca9555_read_input(ioe,0,&ignored); if(err==ESP_OK) err=next;
    next=fca9555_read_input(ioe,1,&ignored); if(err==ESP_OK) err=next;
    xSemaphoreGive(mutex); return err;
}
bool metalio_key_down(void) {
    uint16_t inputs=0xffff;
    return metalio_io_read(&inputs)==ESP_OK && !(inputs&(1u<<15));
}
bool metalio_confirm_power_key(void) {
    uint16_t first=0xffff,again=0xffff;
    if(metalio_io_read(&first)!=ESP_OK || (first&(1u<<15))) return false;
    vTaskDelay(pdMS_TO_TICKS(30));
    return metalio_io_read(&again)==ESP_OK && !(again&(1u<<15));
}
bool metalio_confirm_light_wake_key(void) {
    // 睡眠期间仍保留P17输入；只读端口1，尽量在短按松手前完成确认。
    // P17 remains an input in sleep; read only port 1 to confirm a tap before release.
    if (!ioe || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    uint8_t input = 0xff;
    bool low = fca9555_read_input(ioe, 1, &input) == ESP_OK && !(input & 0x80);
    if (low) {
        vTaskDelay(pdMS_TO_TICKS(10));
        low = fca9555_read_input(ioe, 1, &input) == ESP_OK && !(input & 0x80);
    }
    xSemaphoreGive(mutex);
    return low;
}
bool metalio_boot_power_key(void) { return boot_power_key; }
void metalio_capture_boot_power_key(void) {
    early_boot_power_key=false;
#if CONFIG_METALIO_EARLY_POWER_KEY
    metalio_early_key_record_t record;
    early_boot_power_key=metalio_early_key_take(&s_boot_key_handoff,
        (unsigned)esp_reset_reason(),&record);
    ESP_LOGI(TAG,"early boot key: pressed=%d protocol=%lu reason=%lu flags=%lu samples=0x%04lx config=0x%04lx",
        early_boot_power_key,(unsigned long)record.version,(unsigned long)record.reason,
        (unsigned long)record.flags,(unsigned long)record.samples,(unsigned long)record.config);
#endif
}
void metalio_volume_sample(uint16_t inputs,int64_t now_ms) {
    static uint16_t stable=0xffff,candidate=0xffff;
    static int64_t since;
    inputs|=~((1u<<8)|(1u<<9));
    if (inputs!=candidate) { candidate=inputs; since=now_ms; }
    if (candidate==stable || now_ms-since<25) return;
    uint16_t down=stable&~candidate; stable=candidate;
    int event=down&(1u<<8)?-1:down&(1u<<9)?1:0;
    if (!event) return;
    portENTER_CRITICAL(&volume_lock);
    if (volume_count<8) { volume_events[(volume_head+volume_count)%8]=event; ++volume_count; }
    portEXIT_CRITICAL(&volume_lock);
}
int metalio_take_volume(void) {
    portENTER_CRITICAL(&volume_lock);
    int event=volume_count?volume_events[volume_head]:0;
    if (volume_count) { volume_head=(volume_head+1)%8; --volume_count; }
    portEXIT_CRITICAL(&volume_lock);
    return event;
}
void metalio_volume_clear(void) {
    portENTER_CRITICAL(&volume_lock);
    volume_head=volume_count=0;
    portEXIT_CRITICAL(&volume_lock);
}
void metalio_tps_off(void) {
    tps_prepared=false;
    // E0470A14-AF：XOE 关闭至少 12us 后才撤掉屏幕电压。
    // E0470A14-AF requires XOE low at least 12us before display rail shutdown.
    gpio_set_level(GPIO_NUM_18,0); gpio_set_level(GPIO_NUM_19,0);
    esp_rom_delay_us(12);
    // 先关 VCOM/PWRUP，再进入 SLEEP。/ Disable VCOM/PWRUP before entering SLEEP.
    (void)metalio_io_set(0,false); (void)metalio_io_set(1,false);
    vTaskDelay(pdMS_TO_TICKS(5));
    (void)metalio_io_set(2,false); tps_on=false;
}
esp_err_t metalio_tps_verify_off(void) {
    if (tps_on || tps_prepared) return ESP_ERR_INVALID_STATE;
    if (!ioe || !mutex) return ESP_FAIL;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    uint8_t pins = 0;
    esp_err_t err = fca9555_read_input(ioe, 0, &pins);
    xSemaphoreGive(mutex);
    if (err == ESP_ERR_INVALID_STATE) return ESP_FAIL;
    if (err != ESP_OK) return err;
    return (pins & 0x87) ? ESP_ERR_NOT_FINISHED : ESP_OK;
}
bool metalio_tps_ready(void) { return tps_on; }
int metalio_tps_vcom_mv(void) {
    // VCOM1(0x03)=低 8 位、VCOM2(0x04) bit0=最高位，单位 10mV；读不回来返回 -1。
    // VCOM1(0x03) holds the low 8 bits, VCOM2(0x04) bit0 the MSB, LSB = 10 mV.
    if(!tps_on)return tps_last_vcom_mv;
    uint8_t lsb=0, msb=0;
    if (metalio_read(0x68,0x03,&lsb,1)!=ESP_OK) return -1;
    if (metalio_read(0x68,0x04,&msb,1)!=ESP_OK) return -1;
    tps_last_vcom_mv=(int)(((unsigned)(msb&1)<<8 | lsb) & 0x1ff) * 10;
    return tps_last_vcom_mv;
}
// 上电时序对齐厂家 2.0.76 epd_board_poweron：
//   PWRUP低+WAKEUP高+VCOM低 → 50ms → REVID → UPSEQ0/1 → **PWRUP和VCOM_CTRL一起拉高**
//   → 等扩展器 PWR_GOOD → ENABLE=0x3F → **写 VCOM** → 等 PG=0xFA → XOE 由 ctrl 状态打开。
// VCOM 写在 VCOM_CTRL 已经拉高之后，与厂家顺序一致（此前我们是写完 VCOM 才拉 VCOM_CTRL）。
// Power-up order mirrors vendor 2.0.76 epd_board_poweron: PWRUP low + WAKEUP high +
// VCOM low, 50 ms, REVID, UPSEQ0/1, raise PWRUP and VCOM_CTRL together, wait for the
// expander PWR_GOOD, ENABLE=0x3F, then write VCOM (after VCOM_CTRL is high), wait for
// PG=0xFA. XOE is opened through the ctrl state afterwards.
esp_err_t metalio_tps_prepare(void) {
    if(tps_on||tps_prepared)return ESP_OK;
    esp_err_t err=metalio_io_set(0,false);
    if (err==ESP_OK) err=metalio_io_set(1,false);
    if (err==ESP_OK) err=metalio_io_set(2,true);
    if(err!=ESP_OK){metalio_tps_off();return err;}
    tps_wake_us=esp_timer_get_time();tps_prepared=true;
    return ESP_OK;
}
void metalio_tps_cancel_prepare(void) { if(tps_prepared)metalio_tps_off(); }
esp_err_t metalio_tps_on(int mv) {
    if (mv<0 || mv>5110) return ESP_ERR_INVALID_ARG;
    if (tps_on) return ESP_OK;
    esp_err_t err=metalio_tps_prepare();
    if(err!=ESP_OK)return err;
    // 不缩短厂家50ms；把尚未经过的等待向上取整，避免tick截断。/ Retain the vendor50ms, rounding remaining ticks upward.
    int64_t remaining=50000-(esp_timer_get_time()-tps_wake_us);
    if(remaining>0)vTaskDelay((remaining+portTICK_PERIOD_MS*1000-1)/(portTICK_PERIOD_MS*1000));
    tps_prepared=false;
    uint8_t revision=0;
    if (err==ESP_OK) {
        for (unsigned attempt=0;attempt<3;attempt++) {
            err=metalio_read(0x68,0x10,&revision,1);
            if (err==ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ESP_LOGI(TAG,"TPS REVID read: %s value=0x%02x",esp_err_to_name(err),revision);
    }
    if (err==ESP_OK) err=metalio_write8(0x68,0x09,0xe4);
    if (err==ESP_OK) err=metalio_write8(0x68,0x0a,0x55);
    // PWRUP 和 VCOM_CTRL 同时拉高（厂家同一次 ctrl 写入）。
    // Raise PWRUP and VCOM_CTRL together, as the vendor does in one ctrl write.
    if (err==ESP_OK) err=metalio_io_mask(0,3,3);
    if (err==ESP_OK) {
        bool good=false;
        for (unsigned ms=0;ms<300;ms+=5) {
            uint16_t in;
            if (metalio_io_read(&in)==ESP_OK && (in&0x80)) { good=true; break; }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (!good) err=ESP_ERR_TIMEOUT;
    }
    if (err==ESP_OK) err=metalio_write8(0x68,0x01,0x3f);
    // ENABLE 之后再写 VCOM（厂家 tps_set_vcom 在 GPIO PG 成功、ENABLE 之后调用）。
    // Write VCOM after ENABLE, matching the vendor call order.
    const unsigned value=(unsigned)mv/10;
    if (err==ESP_OK) err=metalio_write8(0x68,0x04,(value>>8)&1);
    if (err==ESP_OK) err=metalio_write8(0x68,0x03,value&255);
    if (err==ESP_OK) {
        bool good=false;
        for(unsigned ms=0;ms<200;ms+=5) {
            uint8_t pg;
            if (metalio_read(0x68,0x0f,&pg,1)==ESP_OK && (pg&0xfa)==0xfa) { good=true; break; }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (!good) err=ESP_ERR_TIMEOUT;
    }
    if (err!=ESP_OK) { metalio_tps_off(); ESP_LOGE(TAG,"TPS startup failed; rails off: %s",esp_err_to_name(err)); return err; }
    tps_on=true;
    int readback=metalio_tps_vcom_mv();
    ESP_LOGI(TAG,"TPS ready REVID=0x%02x saved VCOM=%dmV register readback=%dmV",revision,mv,readback);
    return ESP_OK;
}
void metalio_frontlight(unsigned brightness,unsigned warmth) {
    if (!fl_ready) return;
    if (brightness>100) brightness=100;
    if (warmth>100) warmth=100;
    // 参考 76 版限制总功率到半幅。/ Keep the reference 76 half-scale power limit.
    unsigned total=brightness*1023/200;
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,total*(100-warmth)/100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,total*warmth/100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1);
}
void metalio_ana_sw(bool high) {
    static bool configured;
    if (!configured) {
        // 启动首行就会调用，必须先配置输出；不能依赖稍后I2C/板级初始化。
        // This is called before board/I2C initialization; configure the switch output here.
        gpio_set_level(GPIO_NUM_36, high);
        // 需同时启用输入才能回读SEL；仅输出的gpio_get_level恒为0，会误判为OTG。
        // Enable input for SEL readback: gpio_get_level returns zero for output-only pads.
        const gpio_config_t cfg={.pin_bit_mask=1ULL<<36,.mode=GPIO_MODE_INPUT_OUTPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
        ESP_ERROR_CHECK(gpio_config(&cfg));
        // S31 GPIO文档：先配置锁存和方向，再解除复位前的单脚保持，避免SEL掉低。
        // S31 GPIO contract: configure latch/direction before releasing the retained pad to avoid a low SEL glitch.
        gpio_hold_dis(GPIO_NUM_36);
        configured=true;
    }
    gpio_set_level(GPIO_NUM_36,high);
}
void metalio_clear_download_request(void) { REG_CLR_BIT(LP_SYSTEM_REG_SYS_CTRL_REG,LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT); }
void metalio_poweroff(void) {
    metalio_haptic_stop();
    metalio_frontlight(0,50); metalio_tps_off();
    for (unsigned i=0;i<15;i++) {
        (void)metalio_io_set(11,true); vTaskDelay(pdMS_TO_TICKS(100));
        (void)metalio_io_set(11,false); vTaskDelay(pdMS_TO_TICKS(100));
    }
}
esp_err_t metalio_hw_init(void) {
    if (bus) return ESP_OK;
    const int held[]={4,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,35,37,40};
    for(unsigned i=0;i<sizeof(held)/sizeof(held[0]);i++) gpio_hold_dis(held[i]);
    mutex=xSemaphoreCreateMutex(); if (!mutex) return ESP_ERR_NO_MEM;
    haptic_mutex=xSemaphoreCreateMutex(); if(!haptic_mutex) return ESP_ERR_NO_MEM;
    metalio_ana_sw(true);
    gpio_config_t cfg;
    cfg=(gpio_config_t){.pin_bit_mask=(1ULL<<2)|(1ULL<<5)|(1ULL<<61),.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&cfg));
    i2c_master_bus_config_t ic={.i2c_port=1,.sda_io_num=GPIO_NUM_0,.scl_io_num=GPIO_NUM_1,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    esp_err_t err=i2c_new_master_bus(&ic,&bus); if (err!=ESP_OK) return err;
    fca9555_config_t fc=FCA9555_CONFIG_DEFAULT(); fc.i2c_addr=0x20; fc.int_gpio=GPIO_NUM_2; fc.scl_speed_hz=100000;
    err=fca9555_init(bus,&fc,&ioe); if (err!=ESP_OK) return err;
    // 写锁存后改方向；电源/音量感应脚保持输入。/ Write latches before direction; never drive key inputs.
    err=fca9555_set_output(ioe,0,outputs[0]);
    if (err==ESP_OK) err=fca9555_set_output(ioe,1,outputs[1]);
    if (err==ESP_OK) err=fca9555_set_inversion(ioe,0,0);
    if (err==ESP_OK) err=fca9555_set_inversion(ioe,1,0);
    if (err==ESP_OK) err=fca9555_set_config(ioe,0,0xc0);
    if (err==ESP_OK) err=fca9555_set_config(ioe,1,0xe3);
    if (err!=ESP_OK) return err;
    // 硬件/深睡启动尽早采样真实P17，后续挂卡/字体耗时不能用共享IRQ代替按键。
    // Sample real P17 early on hardware/deep boots; later media/font delays cannot turn a shared IRQ into a press.
    if(esp_reset_reason()==ESP_RST_DEEPSLEEP || esp_reset_reason()==ESP_RST_POWERON)
        boot_power_key=(esp_reset_reason()==ESP_RST_POWERON && early_boot_power_key) || metalio_confirm_power_key();
    // P14 VOUT 上电后按参考驱动等待轨稳定。/ Allow reference VOUT rail settling time.
    vTaskDelay(pdMS_TO_TICKS(80));
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,
        .timer_num=LEDC_TIMER_0,.freq_hz=20000,.clk_cfg=LEDC_AUTO_CLK};
    err=ledc_timer_config(&timer);
    for (int i=0;err==ESP_OK && i<2;i++) {
        ledc_channel_config_t ch={.gpio_num=6+i,.speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=i,.timer_sel=LEDC_TIMER_0,.duty=0}; err=ledc_channel_config(&ch);
    }
    fl_ready=err==ESP_OK;
    nvs_handle_t h;
    frontlight_load();
    if(nvs_open("metalio_haptic",NVS_READONLY,&h)==ESP_OK) {
        uint8_t enabled=1; nvs_get_u8(h,"enabled",&enabled); nvs_close(h);
        atomic_store(&haptic_enabled,enabled!=0);
    }
    // 未通过锁屏/密码门禁前保持灭灯，供电复位不能短暂点亮。
    // Stay dark until the boot lock/PIN gate completes; a power reset must not flash the light.
    metalio_frontlight(0,50);
    if(xTaskCreate(haptic_worker,"metalio_haptic",2048,NULL,3,&haptic_task)!=pdPASS)
        ESP_LOGW(TAG,"Haptic worker unavailable");
    ESP_LOGI(TAG,"Metalio V1.5 S31: TCA9555 ready; frontlight=%s",fl_ready?"ready":"unavailable");
    return ESP_OK;
}

unsigned metalio_brightness(void) { return saved_brightness; }
unsigned metalio_warmth(void) { return saved_warmth; }
bool metalio_frontlight_enabled(void) { return light_enabled; }
unsigned metalio_frontlight_position(bool warmth) { return warmth?warmth_position:brightness_position; }
// 像素单位保留原厂10bit/半幅功率，不先量化成整数百分比。
// Pixel units preserve reference 10-bit/half-scale PWM without percent quantization.
static void frontlight_apply_position(unsigned brightness,unsigned warmth) {
    if(!fl_ready) return;
    unsigned total=brightness*1023/(2*METALIO_FRONTLIGHT_RANGE);
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,total*(METALIO_FRONTLIGHT_RANGE-warmth)/METALIO_FRONTLIGHT_RANGE);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,total*warmth/METALIO_FRONTLIGHT_RANGE);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1);
}
void metalio_frontlight_restore(void) {
    if(!fl_ready)return;
    frontlight_apply_position(light_enabled?brightness_position:0,warmth_position);
    // 先恢复目标PWM，再释放浅睡的低电平保持，定时关机不经过这里。
    // Program the desired PWM before releasing sleep's low holds; timed off never calls this.
    gpio_hold_dis(GPIO_NUM_6);gpio_hold_dis(GPIO_NUM_7);
}
// 兼容旧版以亮度0表示关闭的设置；新开关独立保存，重启/唤醒不能擅自开灯。
// Migrate legacy zero-as-off settings; persist the independent switch across boot/wake.
static void frontlight_load(void) {
    nvs_handle_t h;
    uint8_t enabled=2;
    if(nvs_open("metalio_light",NVS_READONLY,&h)==ESP_OK) {
        (void)nvs_get_u8(h,"brightness",&saved_brightness);
        (void)nvs_get_u8(h,"warmth",&saved_warmth);
        (void)nvs_get_u8(h,"last_on",&last_on_brightness);
        (void)nvs_get_u8(h,"enabled",&enabled);
        nvs_close(h);
        if(enabled>1) light_dirty=true;
    }
    if(saved_brightness>100) saved_brightness=30;
    if(saved_warmth>100) saved_warmth=50;
    if(!last_on_brightness || last_on_brightness>100) last_on_brightness=30;
    light_enabled=enabled<=1?enabled!=0:saved_brightness!=0;
    if(enabled>1 && !saved_brightness) saved_brightness=last_on_brightness;
    if(saved_brightness) last_on_brightness=saved_brightness;
    brightness_position=(saved_brightness*METALIO_FRONTLIGHT_RANGE+50)/100;
    warmth_position=(saved_warmth*METALIO_FRONTLIGHT_RANGE+50)/100;
    last_on_position=(last_on_brightness*METALIO_FRONTLIGHT_RANGE+50)/100;
    if(nvs_open("metalio_light",NVS_READONLY,&h)==ESP_OK) {
        uint16_t v;
        if(nvs_get_u16(h,"brightness_px",&v)==ESP_OK && v<=METALIO_FRONTLIGHT_RANGE) brightness_position=v;
        if(nvs_get_u16(h,"warmth_px",&v)==ESP_OK && v<=METALIO_FRONTLIGHT_RANGE) warmth_position=v;
        if(nvs_get_u16(h,"last_on_px",&v)==ESP_OK && v>0 && v<=METALIO_FRONTLIGHT_RANGE) last_on_position=v;
        nvs_close(h);
    }
    saved_brightness=(brightness_position*100+METALIO_FRONTLIGHT_RANGE/2)/METALIO_FRONTLIGHT_RANGE;
    saved_warmth=(warmth_position*100+METALIO_FRONTLIGHT_RANGE/2)/METALIO_FRONTLIGHT_RANGE;
}
void metalio_frontlight_save(unsigned brightness,unsigned warmth) {
    metalio_frontlight_preview(brightness,warmth);
    metalio_frontlight_commit();
}
void metalio_frontlight_preview(unsigned brightness,unsigned warmth) {
    if(brightness>100) brightness=100;
    if(warmth>100) warmth=100;
    metalio_frontlight_preview_position((brightness*METALIO_FRONTLIGHT_RANGE+50)/100,
        (warmth*METALIO_FRONTLIGHT_RANGE+50)/100);
}
void metalio_frontlight_preview_position(unsigned brightness,unsigned warmth) {
    brightness_position=brightness>METALIO_FRONTLIGHT_RANGE?METALIO_FRONTLIGHT_RANGE:brightness;
    warmth_position=warmth>METALIO_FRONTLIGHT_RANGE?METALIO_FRONTLIGHT_RANGE:warmth;
    saved_brightness=(brightness_position*100+METALIO_FRONTLIGHT_RANGE/2)/METALIO_FRONTLIGHT_RANGE;
    saved_warmth=(warmth_position*100+METALIO_FRONTLIGHT_RANGE/2)/METALIO_FRONTLIGHT_RANGE;
    if(brightness_position) { last_on_position=brightness_position; last_on_brightness=saved_brightness; }
    light_dirty=true;
    // 滑块只改参数；关闭时两个PWM通道保持零。/ Sliders never enable an off light.
    metalio_frontlight_restore();
}
void metalio_frontlight_toggle(void) {
    light_enabled=!light_enabled;
    if(light_enabled && !brightness_position) {
        brightness_position=last_on_position;
        saved_brightness=(brightness_position*100+METALIO_FRONTLIGHT_RANGE/2)/METALIO_FRONTLIGHT_RANGE;
    }
    light_dirty=true;
    metalio_frontlight_restore();
    metalio_frontlight_commit();
}
void metalio_frontlight_commit(void) {
    if(!light_dirty) return;
    nvs_handle_t h;
    if(nvs_open("metalio_light",NVS_READWRITE,&h)==ESP_OK) {
        esp_err_t err=nvs_set_u8(h,"brightness",saved_brightness);
        if(err==ESP_OK) err=nvs_set_u8(h,"warmth",saved_warmth);
        if(err==ESP_OK) err=nvs_set_u8(h,"last_on",last_on_brightness);
        if(err==ESP_OK) err=nvs_set_u8(h,"enabled",light_enabled?1:0);
        if(err==ESP_OK) err=nvs_set_u16(h,"brightness_px",brightness_position);
        if(err==ESP_OK) err=nvs_set_u16(h,"warmth_px",warmth_position);
        if(err==ESP_OK) err=nvs_set_u16(h,"last_on_px",last_on_position);
        if(err==ESP_OK) err=nvs_commit(h);
        nvs_close(h); if(err==ESP_OK) light_dirty=false;
    }
}
// 常驻服务串行发送马达脉冲，不按点击分配任务。/ One worker serializes motor pulses.
static void haptic_worker(void *arg) {
    (void)arg;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        xSemaphoreTake(haptic_mutex,portMAX_DELAY);
        if(!atomic_load(&haptic_enabled) || atomic_load(&haptic_stopped)) {
            xSemaphoreGive(haptic_mutex); continue;
        }
        if(metalio_io_set(3,true)==ESP_OK) vTaskDelay(pdMS_TO_TICKS(25));
        if(metalio_io_set(3,false)!=ESP_OK) (void)metalio_io_set(3,false);
        xSemaphoreGive(haptic_mutex);
    }
}
bool metalio_haptic_enabled(void) { return atomic_load(&haptic_enabled); }
void metalio_haptic_set_enabled(bool enabled) {
    atomic_store(&haptic_enabled,enabled);
    if(!enabled && haptic_mutex) {
        xSemaphoreTake(haptic_mutex,portMAX_DELAY);
        (void)metalio_io_set(3,false); xSemaphoreGive(haptic_mutex);
    }
    nvs_handle_t h;
    if(nvs_open("metalio_haptic",NVS_READWRITE,&h)==ESP_OK) {
        nvs_set_u8(h,"enabled",enabled); nvs_commit(h); nvs_close(h);
    }
}
// 关机/重启前等待当前脉冲结束，拒绝新脉冲。/ Drain the motor before shutdown or restart.
void metalio_haptic_stop(void) {
    atomic_store(&haptic_stopped,true);
    if(haptic_mutex) {
        xSemaphoreTake(haptic_mutex,portMAX_DELAY);
        if(metalio_io_set(3,false)!=ESP_OK) (void)metalio_io_set(3,false);
        xSemaphoreGive(haptic_mutex);
    }
}
void metalio_haptic_resume(void) { atomic_store(&haptic_stopped, false); }
esp_err_t metalio_haptic_click(void) {
    if(!metalio_haptic_enabled() || atomic_load(&haptic_stopped)) return ESP_OK;
    if(!haptic_task) return ESP_ERR_INVALID_STATE;
    const int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&haptic_lock);
    const bool accepted=now-last_haptic_us>=100000;
    if(accepted) last_haptic_us=now;
    portEXIT_CRITICAL(&haptic_lock);
    if(accepted) xTaskNotifyGive(haptic_task);
    return ESP_OK;
}
void metalio_sd_release_hold(void) { for (int pin=20;pin<=25;pin++) gpio_hold_dis(pin); }
esp_err_t metalio_sd_single_line_idle(void) {
    // S31休眠曾把这些脚保持低；单线SDK不会重新配置未使用的D1～D3。
    // S31 sleep held these pins low; single-line SDK setup does not reconfigure unusedD1–D3.
    gpio_config_t config={.pin_bit_mask=(1ULL<<21)|(1ULL<<22)|(1ULL<<23),
        .mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,
        .pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    esp_err_t err=gpio_config(&config);
    if(err!=ESP_OK)return err;
    CNNT_PAD_CTRL.sdio_data1.sdio_data1_fun_wpd=0;CNNT_PAD_CTRL.sdio_data1.sdio_data1_fun_wpu=1;
    CNNT_PAD_CTRL.sdio_data2.sdio_data2_fun_wpd=0;CNNT_PAD_CTRL.sdio_data2.sdio_data2_fun_wpu=1;
    CNNT_PAD_CTRL.sdio_data3.sdio_data3_fun_wpd=0;CNNT_PAD_CTRL.sdio_data3.sdio_data3_fun_wpu=1;
    return ESP_OK;
}
static const int light_sleep_pins[]={4,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,35,37,40};
void metalio_release_light_sleep_pins(void) {
    for (unsigned i=0;i<sizeof(light_sleep_pins)/sizeof(light_sleep_pins[0]);i++)
        gpio_hold_dis(light_sleep_pins[i]);
}
esp_err_t metalio_prepare_light_sleep_pins(void) {
    // 中文：仅在 SD 卸载且 LCD 停止后调用；GPIO6/7保留LEDC映射，关灯由PWM零值保证。
    // English: Call after SD unmount/LCD shutdown; keep LEDC mapping on GPIO6/7 with zero duty.
    CNNT_PAD_CTRL.sdio_clk.sdio_clk_fun_wpu=0; CNNT_PAD_CTRL.sdio_clk.sdio_clk_fun_wpd=1;
    CNNT_PAD_CTRL.sdio_cmd.sdio_cmd_fun_wpu=0; CNNT_PAD_CTRL.sdio_cmd.sdio_cmd_fun_wpd=1;
    CNNT_PAD_CTRL.sdio_data0.sdio_data0_fun_wpu=0; CNNT_PAD_CTRL.sdio_data0.sdio_data0_fun_wpd=1;
    CNNT_PAD_CTRL.sdio_data1.sdio_data1_fun_wpu=0; CNNT_PAD_CTRL.sdio_data1.sdio_data1_fun_wpd=1;
    CNNT_PAD_CTRL.sdio_data2.sdio_data2_fun_wpu=0; CNNT_PAD_CTRL.sdio_data2.sdio_data2_fun_wpd=1;
    CNNT_PAD_CTRL.sdio_data3.sdio_data3_fun_wpu=0; CNNT_PAD_CTRL.sdio_data3.sdio_data3_fun_wpd=1;
    CNNT_PAD_CTRL.ctrl.sdio_pad_pin_ctrl_ded_sel=0;
    esp_err_t result=ESP_OK;
    for (unsigned i=0;i<sizeof(light_sleep_pins)/sizeof(light_sleep_pins[0]);i++) {
        int pin=light_sleep_pins[i];
        gpio_hold_dis(pin); esp_gpio_revoke(1ULL<<pin);
        esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);
        gpio_config_t config={.pin_bit_mask=1ULL<<pin,.mode=GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
        esp_err_t err=gpio_config(&config);
        if(err==ESP_OK) err=gpio_set_level(pin,0);
        if(result==ESP_OK) result=err;
    }
    return result;
}
esp_err_t metalio_hold_for_light_sleep(void) {
    esp_err_t result=ESP_OK;
    // 入睡前PWM已置零并等待120ms，LEDC脚只保持，不改其复用配置。
    // PWM has settled at zero for120ms; hold LEDC pads without remapping them.
    esp_err_t light= gpio_hold_en(GPIO_NUM_6);
    if(light==ESP_OK)light=gpio_hold_en(GPIO_NUM_7);
    if(light!=ESP_OK)result=light;
    for(unsigned i=0;i<sizeof(light_sleep_pins)/sizeof(light_sleep_pins[0]);i++) {
        esp_err_t err=gpio_hold_en(light_sleep_pins[i]);
        if(result==ESP_OK) result=err;
    }
    return result;
}
esp_err_t metalio_sleep_cut_rails(uint16_t *saved) {
    if (!saved) return ESP_ERR_INVALID_ARG;
    *saved=(uint16_t)outputs[0]|((uint16_t)outputs[1]<<8);
    esp_err_t err=metalio_io_set(5,false);
    if(err==ESP_OK) err=metalio_io_set(12,false);
    return err;
}
esp_err_t metalio_sleep_restore_rails(uint16_t saved) {
    // 中文：P14先于P5恢复，任何一笔失败仍尝试下一笔。
    // English: Restore P14 before P5, attempting both even after an error.
    esp_err_t first=metalio_io_set(12,(saved&(1u<<12))!=0);
    esp_err_t next=metalio_io_set(5,(saved&(1u<<5))!=0);
    return first!=ESP_OK?first:next;
}
void metalio_hold_for_deep_sleep(void) {
    metalio_haptic_stop();
    // 专用 SDIO 先交还 GPIO，再保持低。/ Release dedicated SDIO before holding pins low.
    CNNT_PAD_CTRL.ctrl.sdio_pad_pin_ctrl_ded_sel=0;
    const int pins[]={4,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,35,37,40};
    for(unsigned i=0;i<sizeof(pins)/sizeof(pins[0]);i++) {
        gpio_hold_dis(pins[i]); gpio_reset_pin(pins[i]);
        gpio_set_level(pins[i],0); gpio_set_direction(pins[i],GPIO_MODE_OUTPUT); gpio_hold_en(pins[i]);
    }
}

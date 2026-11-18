/* SPDX-License-Identifier: Apache-2.0
 * 中文：RC90 板级契约在 Metalio V1.5 的实现，无 CW32/SY7636A 写入。
 * English: RC90 board contract implemented for Metalio V1.5, without CW32/SY7636A access.
 */
#include "read_pico_board.h"
#include "read_pico_epd_timing.h"
#include "metalio_hw.h"
#include "epdiy.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
// 阅读GL16验证版缺省-1500mV，启动时仍优先读取本机有效标定。
// Reader GL16 builds default to -1500mV; boot still prioritizes valid local calibration.
static int vcom=1500;
static read_pico_i2c_census_t census;
void read_pico_epd_restore_lcd(void) {
    const int pins[]={18,19,35,37};
    for (unsigned i=0;i<sizeof(pins)/sizeof(pins[0]);i++) {
        gpio_hold_dis(pins[i]); gpio_set_level(pins[i],0); gpio_set_direction(pins[i],GPIO_MODE_OUTPUT);
    }
    lcd_bus_config_t bus={.data={8,9,10,11,12,13,14,15},.clock=16,.ckv=40,.leh=17,.start_pulse=4,.stv=35};
    read_pico_epd_scan_t scan; read_pico_epd_scan(20,READ_PICO_EPD_SCAN_FULL,&scan);
    read_pico_epd_init_lcd(&bus,8,1216,684,&scan);
}
static void board_init(uint32_t width) {
    (void)width;
    ESP_ERROR_CHECK(metalio_hw_init());
    const int pins[]={18,19,35,37};
    for (unsigned i=0;i<sizeof(pins)/sizeof(pins[0]);i++) {
        gpio_hold_dis(pins[i]); gpio_set_level(pins[i],0); gpio_set_direction(pins[i],GPIO_MODE_OUTPUT);
    }
    lcd_bus_config_t bus={.data={8,9,10,11,12,13,14,15},.clock=16,.ckv=40,.leh=17,.start_pulse=4,.stv=35};
    read_pico_epd_scan_t scan; read_pico_epd_scan(20,READ_PICO_EPD_SCAN_FULL,&scan);
    read_pico_epd_init_lcd(&bus,8,1216,684,&scan);
}
static void board_set_ctrl(epd_ctrl_state_t *s,const epd_ctrl_state_t *mask) {
    if (mask->ep_output_enable) gpio_set_level(18,s->ep_output_enable && metalio_tps_ready());
    if (mask->ep_mode) gpio_set_level(19,s->ep_mode && metalio_tps_ready());
    if (mask->ep_stv) gpio_set_level(35,s->ep_stv);
}
static void board_on(epd_ctrl_state_t *s) {
    s->ep_output_enable=false; s->ep_mode=false; s->ep_stv=true;
    epd_ctrl_state_t mask={.ep_output_enable=true,.ep_mode=true,.ep_stv=true};
    board_set_ctrl(s,&mask);
    if (metalio_tps_on(vcom)!=ESP_OK) return;
}
// S31的XOE/MODE为直连GPIO，不使用Pico扩展器或SY7636A VCOM位。
// S31 XOE/MODE are direct GPIOs; no Pico IO-expander/SY7636A VCOM write.
bool read_pico_epd_release_drive(void) {
    epd_ctrl_state_t *state=epd_ctrl_state();
    state->ep_output_enable=false;state->ep_mode=false;
    esp_err_t xoe=gpio_set_level(18,0),mode=gpio_set_level(19,0);
    return xoe==ESP_OK && mode==ESP_OK;
}
static void board_off(epd_ctrl_state_t *s) {
    s->ep_output_enable=false; s->ep_mode=false; s->ep_stv=false;
    epd_ctrl_state_t mask={.ep_output_enable=true,.ep_mode=true,.ep_stv=true};
    board_set_ctrl(s,&mask); metalio_tps_off();
}
static void board_deinit(void) { board_off(epd_ctrl_state()); epd_lcd_deinit(); }
// VCOM 运行范围放宽到 0.6..4.0V：标定页要能扫到面板真正的最优点；TPS65185 本身
// 支持到 -5.11V，面板长期直流不平衡的风险由标定流程控制（扫完停在最优值）。
// Runtime VCOM window widened to 0.6..4.0 V so the calibration page can reach the
// panel's real optimum; the TPS65185 itself goes to -5.11 V.
static void set_vcom(int mv) { if (mv>=600 && mv<=4000) vcom=mv; }
static float temperature(void) { return 25; }
static void no_kickback(epd_ctrl_state_t *s) { (void)s; }
const EpdBoardDefinition epd_board_read_pico={.init=board_init,.deinit=board_deinit,
    .set_ctrl=board_set_ctrl,.poweron=board_on,.poweroff=board_off,
    .set_vcom=set_vcom,.get_temperature=temperature,.measure_vcom=no_kickback};
i2c_master_bus_handle_t read_pico_i2c_bus(void) { return metalio_bus(); }
fca9555_handle_t read_pico_fca9555(void) { return metalio_ioe(); }
sy7636a_handle_t read_pico_sy7636a(void) { return NULL; }
int read_pico_ioe_int_level(void) { return gpio_get_level(2); }
void read_pico_clear_ioe_int(void) { uint16_t input; (void)metalio_io_read(&input); }
bool read_pico_rails_on(void) { return metalio_tps_ready(); }
esp_err_t read_pico_get_ioe_status_full(read_pico_status_t *s) {
    if (!s) return ESP_ERR_INVALID_ARG;
    memset(s,0,sizeof(*s));
    fca9555_map_t map; esp_err_t err=fca9555_read_all(metalio_ioe(),&map);
    if (err==ESP_OK) { s->ioe_input=map.input; s->ioe_output=map.output; s->ioe_invert=map.invert; s->ioe_config=map.config; }
    s->rails_on=metalio_tps_ready(); s->ioe_int_level=gpio_get_level(2); return err;
}
esp_err_t read_pico_get_ioe_status(read_pico_status_t *s) { return read_pico_get_ioe_status_full(s); }
esp_err_t read_pico_get_status(read_pico_status_t *s) { return read_pico_get_ioe_status_full(s); }
esp_err_t read_pico_toggle_vcomctl(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t read_pico_sd_detect(bool *present) { (void)present; return ESP_ERR_NOT_SUPPORTED; }
bool read_pico_sd_present(void) { return true; }
esp_err_t read_pico_tp_rst(bool high) { return metalio_io_set(10,high); }
esp_err_t read_pico_touch_reset(void) {
    esp_err_t err=read_pico_tp_rst(false); if (err!=ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10)); err=read_pico_tp_rst(true); vTaskDelay(pdMS_TO_TICKS(300)); return err;
}
void read_pico_i2c_census_take(void) {
    const uint8_t addresses[]={0x20,0x38,0x19,0x55,0x51};
    const char *names[]={"TCA9555","FT6336U","SC7A20H","BQ27220","PCF8563"};
    census=(read_pico_i2c_census_t){.all_online=true};
    for (int i=0;i<READ_PICO_I2C_DEV_N;i++) {
        bool online=i2c_master_probe(metalio_bus(),addresses[i],30)==ESP_OK;
        census.dev[i]=(read_pico_i2c_dev_t){names[i],addresses[i],online};
        if (!online) { census.offline_n++; census.all_online=false; }
    }
}
const read_pico_i2c_census_t *read_pico_i2c_census(void) { return &census; }

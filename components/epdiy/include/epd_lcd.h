#pragma once

#include <driver/gpio.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Metalio 板用的是厂商示例固件的 epdiy，它的 LcdEpdConfig_t 是扁平结构（ckv_high_time 等直接
// 放在顶层），和本仓库 fork 的嵌套结构不同名同义。两套不能同时定义，所以这里直接引例程的头。
// The Metalio board uses the vendor demo firmware's epdiy, whose LcdEpdConfig_t is flat
// (ckv_high_time and friends sit at the top level) rather than this fork's nested shape. The two
// cannot both be defined, so on that board this header just pulls in the vendor's.
#if defined(PICO_BOARD_METALIO_EINK4_PLUS)
#include "lcd_driver.h"
#else

typedef struct {
    gpio_num_t data[16];
    gpio_num_t clock;
    gpio_num_t ckv;
    gpio_num_t start_pulse;
    gpio_num_t leh;
    gpio_num_t stv;
} lcd_bus_config_t;

/// 一行四段 + CKV。行长 = L_SL + L_BL + L_DL + L_EL，必须在改像素钟之前写好。
typedef struct {
    int le_high_time;      ///< L_SL 钟
    int line_front_porch;  ///< L_BL 钟
    int line_end;          ///< L_EL 钟
    int ckv_high_time;     ///< CKV 高电平，0.1µs
} LcdLineTiming_t;

typedef struct {
    size_t pixel_clock;
    LcdLineTiming_t line;
    int bus_width;
    lcd_bus_config_t bus;
} LcdEpdConfig_t;

void epd_lcd_set_line_timing(const LcdLineTiming_t* timing);

#endif  // PICO_BOARD_METALIO_EINK4_PLUS

void epd_lcd_init(const LcdEpdConfig_t* config, int display_width, int display_height);
void epd_lcd_deinit(void);
void epd_lcd_set_pixel_clock_MHz(int frequency);

#ifdef __cplusplus
}
#endif

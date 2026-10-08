#include <stddef.h>

#include "epd_display.h"

// E0470A01 / 684×1216 40pin。总线按本板 16bit，不是原厂 demo 板的 8bit/20MHz。
// 波形在应用侧 epd_hl_init(&E0470_WAVEFORM)，这里不挂社区 LUT。
// 同一块 ED047TC2，两块板的接线方式不同：Read Pico 是 16 位/24MHz，
// Metalio 是 8 位/20MHz（与例程 ED047TC2_1216 一致）。位宽写错会让 LCD 在 8 条数据线上
// 按 16 位推数据，表现为 line buffer underrun。
// The same ED047TC2 panel is wired differently on the two boards: Read Pico runs 16-bit at
// 24 MHz, the Metalio board 8-bit at 20 MHz (matching the demo firmware's ED047TC2_1216). A
// wrong width makes the LCD push 16 bits per pixel clock onto an 8-line bus, which shows up as
// a line buffer underrun.
#if defined(PICO_BOARD_METALIO_EINK4_PLUS)
const EpdDisplay_t E0470_DISPLAY = {
    .width = 1216,
    .height = 684,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = NULL,
};
#else
const EpdDisplay_t E0470_DISPLAY = {
    .width = 1216,
    .height = 684,
    .bus_width = 16,
    .bus_speed = 24,
    .default_waveform = NULL,
};
#endif

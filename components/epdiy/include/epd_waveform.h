#pragma once

// Metalio 板用厂商示例固件的 epdiy，波形类型由它自己的 epdiy.h / epd_internals.h 定义，
// 与本仓库 fork 的这一套同名不同结构。两套同时可见会直接编译报错，所以本板让这里空掉。
//
// The Metalio board uses the vendor demo firmware's epdiy, whose waveform types come from its own
// epdiy.h / epd_internals.h and share the names but not the layout of this fork's. Having both
// visible is a compile error, so on that board this header is empty.
#if !defined(PICO_BOARD_METALIO_EINK4_PLUS)

#pragma once

#include <stdint.h>

typedef struct {
    int phases;
    const uint8_t* luts;
    const int* phase_times;
} EpdWaveformPhases;

typedef struct {
    uint8_t type;
    uint8_t temp_ranges;
    EpdWaveformPhases const** range_data;
} EpdWaveformMode;

typedef struct {
    int min;
    int max;
} EpdWaveformTempInterval;

typedef struct {
    uint8_t num_modes;
    uint8_t num_temp_ranges;
    EpdWaveformMode const** mode_data;
    EpdWaveformTempInterval const* temp_intervals;
} EpdWaveform;

#endif  // PICO_BOARD_METALIO_EINK4_PLUS

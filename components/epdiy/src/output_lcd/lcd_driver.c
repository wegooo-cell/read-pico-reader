#include "lcd_driver.h"

// ESP32-S3 的 LCD 由 PLL240M 驱动；ESP32-S31 上没有这个源，默认是 PLL160M。
// 像素时钟不受影响：lcd.src_clk_hz 从时钟树读，分频由 lcd_hal_cal_pclk_freq() 按实际源算。
// ESP32-S3 drives the LCD from PLL240M; ESP32-S31 has no such source and defaults to
// PLL160M. The pixel clock is unaffected: lcd.src_clk_hz is read from the clock tree and the
// divider is derived from it by lcd_hal_cal_pclk_freq().
#if defined(LCD_CLK_SRC_PLL240M)
#define EPD_LCD_CLK_SRC LCD_CLK_SRC_PLL240M
#define EPD_LCD_SRC_CLK_HZ 240000000u
#else
#define EPD_LCD_CLK_SRC LCD_CLK_SRC_DEFAULT
#define EPD_LCD_SRC_CLK_HZ 160000000u
#endif
#include "epdiy.h"

#include "../output_common/render_method.h"
#include "../output_common/rmt_compat.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "hal/gpio_types.h"

#ifdef RENDER_METHOD_LCD

#include <assert.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <esp_private/periph_ctrl.h>
#include <soc/clk_tree_defs.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#include <esp_clk_tree.h>
// esp-idf-configdep may strip esp_private headers; declare the needed API locally.
esp_err_t esp_clk_tree_enable_src(soc_module_clk_t clk_src, bool enable);
#endif

#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_cache.h>
#include <esp_err.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_private/gdma.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <hal/dma_types.h>
#include <hal/gdma_channel.h>
#include <hal/gdma_ll.h>
#include <hal/rmt_periph.h>
#include <soc/rmt_struct.h>
#include "epdiy_idf6_compat.h"

// S31/P4：LCD 挂在 AXI-GDMA 上；S3：AHB-GDMA。通道分配器、描述符类型与对齐三者必须配套，
// 只改其中一样会在 gdma_connect 报 "peripheral and DMA system bus mismatch" 然后 abort。
// S31/P4 hang the LCD off the AXI GDMA while S3 uses the AHB one. The channel allocator, the
// descriptor type and its alignment have to agree; changing only one of them makes gdma_connect
// report "peripheral and DMA system bus mismatch" and abort.
// 这里必须按芯片型号判断，不能用总线宏：SOC_GDMA_TRIG_PERIPH_LCD0_BUS 在 S3 上也等于
// SOC_GDMA_BUS_AXI，照它分支会让 S3 去调只有 S31 才有的 gdma_new_axi_channel，链接期报
// undefined reference。哪块板实测走哪条总线是确定的，所以直接写型号。
// The guard has to name the chip rather than a bus macro: SOC_GDMA_TRIG_PERIPH_LCD0_BUS equals
// SOC_GDMA_BUS_AXI on S3 too, so branching on it sends S3 after gdma_new_axi_channel, which only
// exists on S31, and the link fails with an undefined reference.
#if defined(CONFIG_IDF_TARGET_ESP32S31) || defined(CONFIG_IDF_TARGET_ESP32P4)
#define LCD_GDMA_NEW_CHANNEL gdma_new_axi_channel
#define LCD_GDMA_DESC_ALIGN 8
typedef dma_descriptor_align8_t lcd_dma_desc_t;
#else
#define LCD_GDMA_NEW_CHANNEL gdma_new_ahb_channel
#define LCD_GDMA_DESC_ALIGN 4
typedef dma_descriptor_t lcd_dma_desc_t;
#endif
#include <hal/gpio_hal.h>
#include <hal/lcd_hal.h>
#include <hal/lcd_ll.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#if __has_include(<rom/cache.h>)
#include <rom/cache.h>
#elif __has_include(<esp32s3/rom/cache.h>)
#include <esp32s3/rom/cache.h>
#endif
typedef struct {
    const shared_periph_module_t module;
    const int irq_id;
    const int data_sigs[LCD_LL_GET(RGB_BUS_WIDTH)];
    const int hsync_sig;
    const int vsync_sig;
    const int pclk_sig;
    const int de_sig;
    const int disp_sig;
} soc_lcd_rgb_signal_desc_t;
extern const soc_lcd_rgb_signal_desc_t soc_lcd_rgb_signals[LCD_LL_GET(RGB_PANEL_NUM)];
#else
#include <rom/cache.h>
#include <soc/lcd_periph.h>
// S31 的数据脚 IOMUX 描述表在 hal/lcd_periph.h 里，soc/lcd_periph.h 没有。
// S31's data-pin IOMUX table lives in hal/lcd_periph.h; soc/lcd_periph.h does not carry it.
#include <hal/lcd_periph.h>
#endif

#include "hal/gpio_hal.h"

gpio_hal_context_t hal = { .dev = GPIO_HAL_GET_HW(GPIO_PORT_0) };

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#undef __DECLARE_RCC_ATOMIC_ENV
#endif

#define TAG "epdiy"

// In IDF 5.3.2+, lcd_periph_signals was renamed to lcd_periph_rgb_signals
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#define LCD_PERIPH_SIG(member) soc_lcd_rgb_signals[0].member
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 2)
#define LCD_PERIPH_SIG(member) lcd_periph_rgb_signals.panels[0].member
#else
#define LCD_PERIPH_SIG(member) lcd_periph_signals.panels[0].member
#endif

static inline int min(int x, int y) {
    return x < y ? x : y;
}
static inline int max(int x, int y) {
    return x > y ? x : y;
}

#define S3_LCD_PIN_NUM_BK_LIGHT -1
// #define S3_LCD_PIN_NUM_MODE           4

#define LINE_BATCH 1000
// 每块回弹缓冲覆盖的行数。8 位总线下 line_bytes 只有 16 位的一半，缓冲总量跟着减半，
// 补充周期缩短到会欠载（真机首次点亮就报 line buffer underrun）。例程用 8：
// 行扫描 16us 时对应 EOF 约 128us。
// Lines per bounce buffer. An 8-bit bus halves line_bytes against a 16-bit one, which halves the
// total buffer and shortens the refill period until it underruns (the first hardware boot reported
// exactly that). The demo firmware uses 8: about 128 us between EOFs at a 16 us line.
#define BOUNCE_BUF_LINES 8

#define RMT_CKV_CHAN RMT_COMPAT_CHANNEL_1

#if defined(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE)
#define EPDIY_DATA_CACHE_LINE_SIZE CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE
#elif defined(CONFIG_DATA_CACHE_LINE_SIZE)
#define EPDIY_DATA_CACHE_LINE_SIZE CONFIG_DATA_CACHE_LINE_SIZE
#else
#define EPDIY_DATA_CACHE_LINE_SIZE 64
#endif

// spinlock for protecting the critical section at frame start
static portMUX_TYPE frame_start_spinlock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    lcd_hal_context_t hal;
    intr_handle_t vsync_intr;
    intr_handle_t done_intr;

    frame_done_func_t frame_done_cb;
    line_cb_func_t line_source_cb;
    void* line_cb_payload;
    void* frame_cb_payload;

    int line_length_us;
    int line_time_01us;
    int line_cycles;
    int lcd_res_h;

    /// 实际发给 RMT 的 CKV 高电平时长，单位 0.1us。行时间会随 pclk 变化，
    /// 所以不能直接用 config 里那个按默认 pclk 标定的固定值。
    int ckv_high_time;

    LcdEpdConfig_t config;

    uint8_t* bounce_buffer[2];
    // size of a single bounce buffer
    size_t bb_size;
    /// 回弹缓冲是否位于 cacheable 区：是则每次填充后必须 C2M 同步，否则 DMA 读到旧数据。
    /// / Whether the bounce buffers are cacheable. If so each fill needs a C2M sync, or the DMA
    /// reads stale data.
    bool bb_behind_cache;
    size_t bb_eof_count;
    size_t batches;

    // Number of DMA descriptors that used to carry the frame buffer
    size_t num_dma_nodes;
    // DMA channel handle
    gdma_channel_handle_t dma_chan;
    // DMA descriptors pool
    lcd_dma_desc_t* dma_nodes;

    /// LCD peripheral source clock frequency (Hz), from clock tree when available.
    uint32_t src_clk_hz;

    /// The number of bytes in a horizontal display register line.
    int line_bytes;

    // With 8 bit bus width, we need a dummy cycle before the actual data,
    // because the LCD peripheral behaves weirdly.
    // Also see:
    // https://blog.adafruit.com/2022/06/14/esp32uesday-hacking-the-esp32-s3-lcd-peripheral/
    int dummy_bytes;

    /// The number of lines of the display
    int display_lines;
} s3_lcd_t;

static s3_lcd_t lcd = { 0 };

void IRAM_ATTR epd_lcd_line_source_cb(line_cb_func_t line_source, void* payload) {
    lcd.line_source_cb = line_source;
    lcd.line_cb_payload = payload;
}

void IRAM_ATTR epd_lcd_frame_done_cb(frame_done_func_t cb, void* payload) {
    lcd.frame_done_cb = cb;
    lcd.frame_cb_payload = payload;
}

static IRAM_ATTR bool fill_bounce_buffer(uint8_t* buffer) {
    bool task_awoken = false;

    for (int i = 0; i < BOUNCE_BUF_LINES; i++) {
        if (lcd.line_source_cb != NULL) {
            // 8-bit needs a true dummy byte in FIFO; 16-bit still needs a dummy cycle but the
            // first FIFO byte is already correct (read_pico / epdiy historical behavior).
            int buffer_offset = i * (lcd.line_bytes + lcd.dummy_bytes) + (lcd.dummy_bytes % 2);
            task_awoken |= lcd.line_source_cb(lcd.line_cb_payload, &buffer[buffer_offset]);
        } else {
            memset(&buffer[i * lcd.line_bytes], 0x00, lcd.line_bytes);
        }
    }
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // CPU 刚写完这一段，DMA 马上要读：S31 内部 SRAM 也在 cache 后面，必须推下去。
    // The CPU just wrote this range and the DMA reads it next; on S31 internal SRAM sits behind
    // the cache too, so it has to be pushed out.
    if (lcd.bb_behind_cache) {
        esp_cache_msync(
            buffer, lcd.bb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED
        );
    }
#endif
    return task_awoken;
}

static void IRAM_ATTR start_ckv_cycles(int cycles) {
    rmt_compat_tx_configure_finite_loop(RMT_CKV_CHAN, cycles);
    rmt_compat_tx_reset_mem(RMT_CKV_CHAN);
    rmt_compat_tx_start(RMT_CKV_CHAN);
}

/**
 * Build the RMT signal according to the timing set in the lcd object.
 */
// RMT 的时间单位是 0.1us（80MHz 时钟 8 分频）。config 里的 ckv_high_time 是按
// 默认 pclk 的行时间标定的，pclk 调高后行时间变短，高电平会顶满甚至超过一整行，
// 低电平算出负数——RMT 的 duration 是无符号字段，信号会彻底跑飞，面板收不到行
// 时钟，表现是画面完全不动而 MCU 侧毫无报错。所以这里给低电平留出下限，
// 让高电平跟着行时间一起收缩。
#define CKV_MIN_LOW_TIME 9

static void rebuild_line_geometry(int pclk_mhz) {
    if (pclk_mhz < 1) pclk_mhz = 1;
    const LcdLineTiming_t* t = &lcd.config.line;
    int end = t->line_end > 0 ? t->line_end : 4;
    lcd.line_cycles = lcd.lcd_res_h + t->le_high_time + t->line_front_porch + end;
    lcd.line_time_01us = (lcd.line_cycles * 10 + pclk_mhz / 2) / pclk_mhz;
    if (lcd.line_time_01us < 1) lcd.line_time_01us = 1;
    lcd.line_length_us = (lcd.line_time_01us + 9) / 10;
}

#ifndef RMT_IDLE_LEVEL_LOW
#define RMT_IDLE_LEVEL_LOW 0
#endif

// RMTMEM 的正式声明在 IDF 的 driver/deprecated/rmt_legacy.c 里，它通过私有的 rmt_private.h
// 访问，SDK 外拿不到，所以自己 extern 一份（例程同样做法）。新结构字段名不同但数据相同，
// typedef 一下即可。
// RMTMEM's real declaration lives in IDF's driver/deprecated/rmt_legacy.c, which reaches it
// through the private rmt_private.h and cannot be used outside the SDK, so declare it here; the
// demo firmware does the same. The new struct names its fields differently but holds the same
// data, so a typedef is enough.
typedef rmt_mem_t rmt_block_mem_t;
extern rmt_block_mem_t RMTMEM;

static void ckv_rmt_build_signal() {
    // 门极一行周期必须等于 LCD 一行时间；偏短会出现「一行数据多次 CKV」，格子被拉折。
    // The gate line period has to equal the LCD line time; too short and one line of data gets
    // several CKV edges, which stretches the image.
    // 本 fork 里门极一行周期叫 line_length_us（例程叫 ckv_period_us）。
    // The gate-line period is line_length_us in this fork; the demo calls it ckv_period_us.
    const int period_us = lcd.line_length_us > 0 ? lcd.line_length_us : 1;
    // 本 fork 把 ckv_high_time 放在 lcd 上而不是 config 里，单位同样是 0.1us。
    // This fork keeps ckv_high_time on the lcd object rather than in the config; same 0.1 us.
    const int high_us = (lcd.ckv_high_time + 5) / 10;
    int high = high_us > 0 ? high_us : 1;
    if (high >= period_us) {
        high = period_us - 1;
    }
    const int low = period_us - high;

    volatile rmt_item32_t* rmt_mem_ptr = &(RMTMEM.chan[RMT_CKV_CHAN].data32[0]);
    rmt_mem_ptr->duration0 = high;
    rmt_mem_ptr->level0 = 1;
    rmt_mem_ptr->duration1 = low;
    rmt_mem_ptr->level1 = 0;
    rmt_mem_ptr[1].val = 0;
}

/**
 * Configure the RMT peripheral for use as the CKV clock.
 */
static void init_ckv_rmt() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    // IDF6 / S31：RMT 不在 shared_periph_module 列表里，必须走 LL 的总线时钟，
    // 而且要显式给 RMT 内存上电——这一步漏了 CKV 就一个边沿都发不出来。
    // IDF6 / S31: RMT is not in the shared_periph_module list, so it has to go through the LL bus
    // clock, and the RMT memory needs an explicit power-on. Miss that and CKV emits nothing at all.
    PERIPH_RCC_ATOMIC() {
        rmt_ll_enable_bus_clock(0, true);
        rmt_ll_reset_register(0);
    }
    rmt_ll_mem_force_power_on(&RMT);
    rmt_ll_enable_mem_access_nonfifo(&RMT, true);
    esp_clk_tree_enable_src((soc_module_clk_t)RMT_CLK_SRC_DEFAULT, true);
    PERIPH_RCC_ATOMIC() {
        // rmt_sclk = src / (1 + (integral-1) + num/den) = src
        rmt_ll_set_group_clock_src(&RMT, RMT_CKV_CHAN, RMT_CLK_SRC_DEFAULT, 1, 1, 0);
        rmt_ll_enable_group_clock(&RMT, true);
    }
#else
    periph_module_reset(PERIPH_RMT_MODULE);
    periph_module_enable(PERIPH_RMT_MODULE);
    rmt_ll_enable_periph_clock(&RMT, true);
    rmt_ll_set_group_clock_src(&RMT, RMT_CKV_CHAN, RMT_CLK_SRC_DEFAULT, 1, 0, 0);
    rmt_ll_enable_mem_access_nonfifo(&RMT, true);
#endif

    uint32_t src_hz = 80000000;
    esp_err_t clk_err =
        esp_clk_tree_src_get_freq_hz((soc_module_clk_t)RMT_CLK_SRC_DEFAULT,
                                     ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &src_hz);
    if (clk_err != ESP_OK || src_hz == 0) {
        src_hz = 80000000;
    }
    // 目标 1MHz 通道时钟：1 tick = 1us，不必再假设「APB/8 → 0.1us」。
    // Aim for a 1 MHz channel clock so one tick is one microsecond, instead of assuming APB/8.
    uint32_t chan_div = (src_hz + 500000) / 1000000;
    if (chan_div < 1) {
        chan_div = 1;
    }
    if (chan_div > 255) {
        chan_div = 255;
    }
    rmt_ll_tx_set_channel_clock_div(&RMT, RMT_CKV_CHAN, chan_div);
    // 与厂商一致：2 个 mem block（S31 每通道 48 word，够用）。
    // Two memory blocks, as the vendor does; 48 words per channel is plenty on S31.
    rmt_ll_tx_set_mem_blocks(&RMT, RMT_CKV_CHAN, 2);
    rmt_ll_tx_fix_idle_level(&RMT, RMT_CKV_CHAN, RMT_IDLE_LEVEL_LOW, true);
    rmt_ll_tx_enable_carrier_modulation(&RMT, RMT_CKV_CHAN, false);
    rmt_ll_tx_enable_loop(&RMT, RMT_CKV_CHAN, true);

    gpio_hal_func_sel(&hal, lcd.config.bus.ckv, PIN_FUNC_GPIO);
    gpio_set_direction(lcd.config.bus.ckv, GPIO_MODE_OUTPUT);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    esp_rom_gpio_connect_out_signal(
        lcd.config.bus.ckv, soc_rmt_signals[0].channels[RMT_CKV_CHAN].tx_sig, false, 0
    );
#else
    esp_rom_gpio_connect_out_signal(
        lcd.config.bus.ckv, rmt_periph_signals.groups[0].channels[RMT_CKV_CHAN].tx_sig, false, 0
    );
#endif
    ESP_LOGI(
        TAG, "CKV RMT src=%uHz div=%u -> %uHz (1 tick=1us)", (unsigned)src_hz, (unsigned)chan_div,
        (unsigned)(src_hz / chan_div)
    );

    ckv_rmt_build_signal();
}

/**
 * Reset the CKV RMT configuration.
 */
static void deinit_ckv_rmt() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ATOMIC() {
        rmt_ll_reset_register(0);
        rmt_ll_enable_bus_clock(0, false);
    }
#else
    periph_module_reset(PERIPH_RMT_MODULE);
    periph_module_disable(PERIPH_RMT_MODULE);
#endif

    gpio_reset_pin(lcd.config.bus.ckv);
}

__attribute__((optimize("O3"))) IRAM_ATTR static void lcd_isr_vsync(void* args) {
    bool need_yield = false;

    uint32_t intr_status = lcd_ll_get_interrupt_status(lcd.hal.dev);
    lcd_ll_clear_interrupt_status(lcd.hal.dev, intr_status);

    if (intr_status & LCD_LL_EVENT_VSYNC_END) {
        // start_frame() already kicked off the first batch. This counter is the
        // number of *additional* full LINE_BATCH chunks still needed after that
        // first batch (integer division, not ceil). Using ceil made short panels
        // (e.g. 688 < 1000) run a duplicate second batch and roughly 2x frame time.
        int batches_needed = lcd.display_lines / LINE_BATCH;
        if (lcd.batches >= batches_needed) {
            lcd_ll_stop(lcd.hal.dev);
            if (lcd.frame_done_cb != NULL) {
                (*lcd.frame_done_cb)(lcd.frame_cb_payload);
            }
        } else {
            int ckv_cycles = 0;
            // last batch
            if (lcd.batches == batches_needed - 1) {
                int last_lines = lcd.display_lines % LINE_BATCH;
                if (last_lines == 0) {
                    last_lines = LINE_BATCH;
                }
                lcd_ll_enable_auto_next_frame(lcd.hal.dev, false);
                lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 0, last_lines, 10);
                ckv_cycles = last_lines + 10;
            } else {
                lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 0, LINE_BATCH, 1);
                ckv_cycles = LINE_BATCH + 1;
            }
            // apparently, this is needed for the new timing to take effect.
            lcd_ll_start(lcd.hal.dev);

            // skip the LCD front porch line, which is not actual data
            esp_rom_delay_us(lcd.line_length_us);
            start_ckv_cycles(ckv_cycles);
        }

        lcd.batches += 1;
    }

    if (need_yield) {
        portYIELD_FROM_ISR();
    }
};

// ISR handling bounce buffer refill
static IRAM_ATTR bool lcd_rgb_panel_eof_handler(
    gdma_channel_handle_t dma_chan, gdma_event_data_t* event_data, void* user_data
) {
    (void)dma_chan;
    (void)event_data;
    (void)user_data;

    int bb = lcd.bb_eof_count % 2;
    lcd.bb_eof_count++;
    return fill_bounce_buffer(lcd.bounce_buffer[bb]);
}

static esp_err_t init_dma_trans_link() {
    lcd.dma_nodes[0].dw0.suc_eof = 1;
    lcd.dma_nodes[0].dw0.size = lcd.bb_size;
    lcd.dma_nodes[0].dw0.length = lcd.bb_size;
    lcd.dma_nodes[0].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_CPU;
    lcd.dma_nodes[0].buffer = lcd.bounce_buffer[0];

    lcd.dma_nodes[1].dw0.suc_eof = 1;
    lcd.dma_nodes[1].dw0.size = lcd.bb_size;
    lcd.dma_nodes[1].dw0.length = lcd.bb_size;
    lcd.dma_nodes[1].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_CPU;
    lcd.dma_nodes[1].buffer = lcd.bounce_buffer[1];

    // loop end back to start
    lcd.dma_nodes[0].next = &lcd.dma_nodes[1];
    lcd.dma_nodes[1].next = &lcd.dma_nodes[0];

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    gdma_channel_alloc_config_t dma_chan_config = { 0 };
    ESP_RETURN_ON_ERROR(
        LCD_GDMA_NEW_CHANNEL(&dma_chan_config, &lcd.dma_chan, NULL), TAG, "alloc DMA channel failed"
    );
    // IDF 6.0's gdma_connect no longer implicitly resets the channel (removed).
    // Do it explicitly to match IDF 5.4 behavior.
    gdma_reset(lcd.dma_chan);
#else
    // alloc DMA channel and connect to LCD peripheral
    gdma_channel_alloc_config_t dma_chan_config = {
        .direction = GDMA_CHANNEL_DIRECTION_TX,
    };
    ESP_RETURN_ON_ERROR(
        gdma_new_channel(&dma_chan_config, &lcd.dma_chan), TAG, "alloc DMA channel failed"
    );
#endif
    gdma_trigger_t trigger = GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0);
    ESP_RETURN_ON_ERROR(gdma_connect(lcd.dma_chan, trigger), TAG, "dma connect error");
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    gdma_strategy_config_t dma_strategy = {
        .eof_till_data_popped = false,
    };
    gdma_apply_strategy(lcd.dma_chan, &dma_strategy);

    gdma_transfer_config_t trans_cfg = {
        .max_data_burst_size = 64,
        .access_ext_mem = true,
    };
    ESP_RETURN_ON_ERROR(gdma_config_transfer(lcd.dma_chan, &trans_cfg), TAG, "dma setup error");
#else
    gdma_transfer_ability_t ability = {
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    ESP_RETURN_ON_ERROR(gdma_set_transfer_ability(lcd.dma_chan, &ability), TAG, "dma setup error");
#endif

    gdma_tx_event_callbacks_t cbs = {
        .on_trans_eof = lcd_rgb_panel_eof_handler,
    };
    ESP_RETURN_ON_ERROR(
        gdma_register_tx_event_callbacks(lcd.dma_chan, &cbs, NULL), TAG, "dma setup error"
    );

    return ESP_OK;
}

void deinit_dma_trans_link() {
    gdma_reset(lcd.dma_chan);
    gdma_disconnect(lcd.dma_chan);
    gdma_del_channel(lcd.dma_chan);
}

/**
 * Configure LCD peripheral and auxiliary GPIOs
 */
static esp_err_t init_bus_gpio() {
    const int DATA_LINES[16] = {
        lcd.config.bus.data[14], lcd.config.bus.data[15], lcd.config.bus.data[12],
        lcd.config.bus.data[13], lcd.config.bus.data[10], lcd.config.bus.data[11],
        lcd.config.bus.data[8],  lcd.config.bus.data[9],  lcd.config.bus.data[6],
        lcd.config.bus.data[7],  lcd.config.bus.data[4],  lcd.config.bus.data[5],
        lcd.config.bus.data[2],  lcd.config.bus.data[3],  lcd.config.bus.data[0],
        lcd.config.bus.data[1],
    };

    // connect peripheral signals via GPIO matrix
    for (size_t i = (16 - lcd.config.bus_width); i < 16; i++) {
        gpio_hal_func_sel(&hal, DATA_LINES[i], PIN_FUNC_GPIO);
        gpio_set_direction(DATA_LINES[i], GPIO_MODE_OUTPUT);
        esp_rom_gpio_connect_out_signal(DATA_LINES[i], LCD_PERIPH_SIG(data_sigs[i]), false, false);
    }
    gpio_hal_func_sel(&hal, lcd.config.bus.leh, PIN_FUNC_GPIO);
    gpio_set_direction(lcd.config.bus.leh, GPIO_MODE_OUTPUT);
    gpio_hal_func_sel(&hal, lcd.config.bus.clock, PIN_FUNC_GPIO);
    gpio_set_direction(lcd.config.bus.clock, GPIO_MODE_OUTPUT);
    gpio_hal_func_sel(&hal, lcd.config.bus.start_pulse, PIN_FUNC_GPIO);
    gpio_set_direction(lcd.config.bus.start_pulse, GPIO_MODE_OUTPUT);

    esp_rom_gpio_connect_out_signal(lcd.config.bus.leh, LCD_PERIPH_SIG(hsync_sig), false, false);
    esp_rom_gpio_connect_out_signal(lcd.config.bus.clock, LCD_PERIPH_SIG(pclk_sig), false, false);
    esp_rom_gpio_connect_out_signal(
        lcd.config.bus.start_pulse, LCD_PERIPH_SIG(de_sig), false, false
    );

    gpio_config_t vsync_gpio_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ull << lcd.config.bus.stv,
    };
    gpio_config(&vsync_gpio_conf);
    gpio_set_level(lcd.config.bus.stv, 1);
    return ESP_OK;
}

/**
 * Reset bus GPIO pin functions.
 */
static void deinit_bus_gpio() {
    for (size_t i = (16 - lcd.config.bus_width); i < 16; i++) {
        gpio_reset_pin(lcd.config.bus.data[i]);
    }

    gpio_reset_pin(lcd.config.bus.leh);
    gpio_reset_pin(lcd.config.bus.clock);
    gpio_reset_pin(lcd.config.bus.start_pulse);
    gpio_reset_pin(lcd.config.bus.stv);
}

/**
 * Check if the PSRAM cache is properly configured.
 */
static void check_cache_configuration() {
    if (EPDIY_DATA_CACHE_LINE_SIZE < 64) {
        ESP_LOGE(
            "epdiy",
            "cache line size is set to %d (< 64B)! This will degrade performance, please update "
            "this option in menuconfig.",
            EPDIY_DATA_CACHE_LINE_SIZE
        );
        ESP_LOGE(
            "epdiy",
            "If you are on arduino, you can't set this option yourself, you'll need to use a lower "
            "speed."
        );
        ESP_LOGE(
            "epdiy",
            "Reducing the pixel clock from %d MHz to %d MHz for now!",
            lcd.config.pixel_clock / 1000 / 1000,
            lcd.config.pixel_clock / 1000 / 1000 / 2
        );
        lcd.config.pixel_clock = lcd.config.pixel_clock / 2;

        // fixme: this would be nice, but doesn't work :(
        // uint32_t d_autoload = Cache_Suspend_DCache();
        /// Cache_Set_DCache_Mode(CACHE_SIZE_FULL, CACHE_4WAYS_ASSOC, CACHE_LINE_SIZE_32B);
        // Cache_Invalidate_DCache_All();
        // Cache_Resume_DCache(d_autoload);
    }
}

/**
 * Assign LCD configuration parameters from a given configuration, without allocating memory or
 * touching the LCD peripheral config.
 */
static void assign_lcd_parameters_from_config(
    const LcdEpdConfig_t* config, int display_width, int display_height
) {
    // copy over the configuraiton object
    memcpy(&lcd.config, config, sizeof(LcdEpdConfig_t));

    // Make sure the bounce buffers divide the display height evenly.
    lcd.display_lines = (((display_height + 7) / 8) * 8);

    lcd.line_bytes = display_width / 4;
    lcd.lcd_res_h = lcd.line_bytes / (lcd.config.bus_width / 8);

    // With 8 bit bus width, we need a dummy cycle before the actual data,
    // because the LCD peripheral behaves weirdly.
    // Also see:
    // https://blog.adafruit.com/2022/06/14/esp32uesday-hacking-the-esp32-s3-lcd-peripheral/
    lcd.dummy_bytes = lcd.config.bus_width / 8;

    // each bounce buffer holds a number of lines with data + dummy bytes each
    lcd.bb_size = BOUNCE_BUF_LINES * (lcd.line_bytes + lcd.dummy_bytes);

    // 本 fork 把 CKV 高电平放在 config.line 里，而 s3_lcd_t 另有一个同名字段。两边不打通的话
    // 后者一直是 0，CKV 脉冲宽度为零——面板电源全对也一个像素都画不出来。
    // This fork keeps the CKV high time in config.line while s3_lcd_t has a field of the same name.
    // Leaving them unlinked keeps the latter at zero, which makes the CKV pulse zero wide: the
    // panel can be powered perfectly and still show nothing.
    lcd.ckv_high_time = lcd.config.line.ckv_high_time;

    check_cache_configuration();

    ESP_LOGI(TAG, "using resolution %dx%d", lcd.lcd_res_h, lcd.display_lines);
}

/**
 * Allocate buffers for LCD driver operation.
 */
static esp_err_t allocate_lcd_buffers() {
    uint32_t dma_flags = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;

    // allocate bounce buffers
    for (int i = 0; i < 2; i++) {
        // S31/P4 需要 64 字节对齐：AXI-GDMA 的突发按 64 对齐，且 cache 行也是 64。
        // S31/P4 want 64-byte alignment: the AXI-GDMA burst is 64-aligned and so are the cache
        // lines.
#if defined(CONFIG_IDF_TARGET_ESP32S31) || defined(CONFIG_IDF_TARGET_ESP32P4)
        const size_t bb_align = 64;
        lcd.bb_behind_cache = true;
#else
        const size_t bb_align = 4;
        lcd.bb_behind_cache = false;
#endif
        lcd.bounce_buffer[i] = heap_caps_aligned_calloc(bb_align, 1, lcd.bb_size, dma_flags);
        ESP_RETURN_ON_FALSE(lcd.bounce_buffer[i], ESP_ERR_NO_MEM, TAG, "install interrupt failed");
#if defined(CONFIG_IDF_TARGET_ESP32S31) || defined(CONFIG_IDF_TARGET_ESP32P4)
        if (lcd.bb_behind_cache) {
            esp_cache_msync(
                lcd.bounce_buffer[i], lcd.bb_size,
                ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED
            );
        }
#endif
    }

    // So far, I haven't seen any displays with > 4096 pixels per line,
    // so we only need one DMA node for now.
    assert(lcd.bb_size < DMA_DESCRIPTOR_BUFFER_MAX_SIZE);
    // 描述符必须按总线要求的边界对齐，分配时就要给对。
    // Descriptors must sit on the alignment their bus requires, so the allocation has to ask.
    lcd.dma_nodes
        = heap_caps_aligned_calloc(LCD_GDMA_DESC_ALIGN, 2, sizeof(lcd_dma_desc_t), dma_flags);
    ESP_RETURN_ON_FALSE(lcd.dma_nodes, ESP_ERR_NO_MEM, TAG, "no mem for dma nodes");
    return ESP_OK;
}

static void free_lcd_buffers() {
    for (int i = 0; i < 2; i++) {
        uint8_t* buf = lcd.bounce_buffer[i];
        if (buf != NULL) {
            heap_caps_free(buf);
            lcd.bounce_buffer[i] = NULL;
        }
    }

    if (lcd.dma_nodes != NULL) {
        heap_caps_free(lcd.dma_nodes);
        lcd.dma_nodes = NULL;
    }
}

/**
 * Initialize the LCD peripheral itself and install interrupts.
 */
static esp_err_t init_lcd_peripheral() {
    esp_err_t ret = ESP_OK;

    // enable APB to access LCD registers
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ACQUIRE_ATOMIC(soc_lcd_rgb_signals[0].module, ref_count) {
        if (ref_count == 0) {
            lcd_ll_enable_bus_clock(0, true);
            lcd_ll_reset_register(0);
        }
    }
#else
    periph_module_enable(PERIPH_LCD_CAM_MODULE);
    periph_module_reset(PERIPH_LCD_CAM_MODULE);
#endif

    lcd_hal_init(&lcd.hal, 0);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    // IDF 6 requires explicit clock-tree enable; otherwise PCLK can fall back to a slow source.
    ESP_RETURN_ON_ERROR(
        esp_clk_tree_enable_src((soc_module_clk_t)EPD_LCD_CLK_SRC, true),
        TAG,
        "enable lcd clk src failed"
    );
    ESP_RETURN_ON_ERROR(
        esp_clk_tree_src_get_freq_hz(
            (soc_module_clk_t)EPD_LCD_CLK_SRC,
            ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
            &lcd.src_clk_hz
        ),
        TAG,
        "get lcd clk freq failed"
    );
    PERIPH_RCC_ATOMIC() {
        lcd_ll_enable_clock(lcd.hal.dev, true);
        lcd_ll_select_clk_src(lcd.hal.dev, EPD_LCD_CLK_SRC);
    }
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // 与 esp_lcd RGB 对齐：LCD 的 SRAM 单独供电，并打开传输缓冲防 FIFO 欠载。
    // Match esp_lcd RGB: power the LCD's SRAM separately and enable the transmit buffer so the
    // FIFO cannot underrun.
    lcd_ll_mem_set_low_power_mode(lcd.hal.dev, LCD_LL_MEM_LP_MODE_SHUT_DOWN);
    lcd_ll_mem_power_by_pmu(lcd.hal.dev);
    lcd_ll_enable_trans_buffer(lcd.hal.dev, true);
#endif
#else
    lcd.src_clk_hz = EPD_LCD_SRC_CLK_HZ;
    lcd_ll_enable_clock(lcd.hal.dev, true);
    lcd_ll_select_clk_src(lcd.hal.dev, EPD_LCD_CLK_SRC);
#endif
    ESP_LOGI(TAG, "lcd src clk: %u Hz", (unsigned)lcd.src_clk_hz);
    ESP_RETURN_ON_ERROR(ret, TAG, "set source clock failed");

    lcd_ll_fifo_reset(lcd.hal.dev);
    lcd_ll_reset(lcd.hal.dev);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // reset 会把传输缓冲清掉，这里补开一次。
    // The reset clears the transmit buffer, so it is re-enabled here.
    lcd_ll_enable_trans_buffer(lcd.hal.dev, true);
#endif

    // install interrupt service, (LCD peripheral shares the interrupt source with Camera by
    // different mask)
    int flags = ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED
                | ESP_INTR_FLAG_LOWMED;

    int source = LCD_PERIPH_SIG(irq_id);
    uint32_t status = (uint32_t)lcd_ll_get_interrupt_status_reg(lcd.hal.dev);
    ret = esp_intr_alloc_intrstatus(
        source, flags, status, LCD_LL_EVENT_VSYNC_END, lcd_isr_vsync, NULL, &lcd.vsync_intr
    );
    ESP_RETURN_ON_ERROR(ret, TAG, "install interrupt failed");

    status = (uint32_t)lcd_ll_get_interrupt_status_reg(lcd.hal.dev);
    ret = esp_intr_alloc_intrstatus(
        source, flags, status, LCD_LL_EVENT_TRANS_DONE, lcd_isr_vsync, NULL, &lcd.done_intr
    );
    ESP_RETURN_ON_ERROR(ret, TAG, "install interrupt failed");

    // pixel clock phase and polarity
    lcd_ll_set_clock_idle_level(lcd.hal.dev, false);
    lcd_ll_set_pixel_clock_edge(lcd.hal.dev, false);

    // enable RGB mode and set data width
    lcd_ll_enable_rgb_mode(lcd.hal.dev, true);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    lcd_ll_set_dma_read_stride(lcd.hal.dev, lcd.config.bus_width);
    lcd_ll_set_data_wire_width(lcd.hal.dev, lcd.config.bus_width);
#else
    lcd_ll_set_data_width(lcd.hal.dev, lcd.config.bus_width);
#endif
    lcd_ll_set_phase_cycles(lcd.hal.dev, 0, (lcd.dummy_bytes > 0), 1);  // enable data phase only

    // number of data cycles is controlled by DMA buffer size
    lcd_ll_enable_output_always_on(lcd.hal.dev, true);
    lcd_ll_set_idle_level(lcd.hal.dev, false, true, true);

    // configure blank region timing
    // RGB panel always has a front and back blank (porch region)
    lcd_ll_set_blank_cycles(lcd.hal.dev, 1, 1);

    // output hsync even in porch region?
    lcd_ll_enable_output_hsync_in_porch_region(lcd.hal.dev, false);
    // send next frame automatically in stream mode
    lcd_ll_enable_auto_next_frame(lcd.hal.dev, false);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
    PERIPH_RCC_ATOMIC() {
        lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_VSYNC_END, true);
        lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_TRANS_DONE, true);
    }
#else
    lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_VSYNC_END, true);
    lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_TRANS_DONE, true);
#endif

    // clear any stale interrupt events before enabling the ISR
    lcd_ll_clear_interrupt_status(lcd.hal.dev, UINT32_MAX);
    // enable intr
    esp_intr_enable(lcd.vsync_intr);
    esp_intr_enable(lcd.done_intr);
    return ret;
}

static void deinit_lcd_peripheral() {
    // disable and free interrupts
    esp_intr_disable(lcd.vsync_intr);
    esp_intr_disable(lcd.done_intr);
    esp_intr_free(lcd.vsync_intr);
    esp_intr_free(lcd.done_intr);

    lcd_ll_stop(lcd.hal.dev);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    lcd_ll_enable_trans_buffer(lcd.hal.dev, false);
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ATOMIC() {
        lcd_ll_enable_clock(lcd.hal.dev, false);
    }
#else
    lcd_ll_enable_clock(lcd.hal.dev, false);
#endif

    lcd_ll_fifo_reset(lcd.hal.dev);
    lcd_ll_reset(lcd.hal.dev);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_RELEASE_ATOMIC(soc_lcd_rgb_signals[0].module, ref_count) {
        if (ref_count == 0) {
            lcd_ll_enable_bus_clock(0, false);
        }
    }
#else
    periph_module_reset(PERIPH_LCD_CAM_MODULE);
    periph_module_disable(PERIPH_LCD_CAM_MODULE);
#endif
}

/**
 * Configure the LCD driver for epdiy.
 */
void epd_lcd_init(const LcdEpdConfig_t* config, int display_width, int display_height) {
    esp_err_t ret = ESP_OK;
    assign_lcd_parameters_from_config(config, display_width, display_height);

    ret = allocate_lcd_buffers();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "lcd buffer allocation failed");

    ret = init_lcd_peripheral();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "lcd peripheral init failed");

    ret = init_dma_trans_link();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "install DMA failed");

    ret = init_bus_gpio();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "configure GPIO failed");

    init_ckv_rmt();

    // setup driver state
    epd_lcd_set_pixel_clock_MHz(lcd.config.pixel_clock / 1000 / 1000);
    epd_lcd_line_source_cb(NULL, NULL);

    ESP_LOGI(TAG, "LCD init done.");
    return;
err:
    ESP_LOGE(TAG, "LCD initialization failed!");
    abort();
}

/**
 * Deinitializue the LCD driver, i.e., free resources and peripherals.
 */
void epd_lcd_deinit() {
    epd_lcd_line_source_cb(NULL, NULL);

    deinit_bus_gpio();
    deinit_lcd_peripheral();
    deinit_dma_trans_link();
    free_lcd_buffers();
    deinit_ckv_rmt();

    ESP_LOGI(TAG, "LCD deinitialized.");
}

void epd_lcd_set_line_timing(const LcdLineTiming_t* timing) {
    if (timing == NULL) return;
    lcd.config.line = *timing;
}

void epd_lcd_set_pixel_clock_MHz(int frequency) {
    lcd.config.pixel_clock = frequency * 1000 * 1000;
    if (lcd.src_clk_hz == 0) {
        lcd.src_clk_hz = EPD_LCD_SRC_CLK_HZ;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    hal_utils_clk_div_t clk_div = {};
/**
 * There was a change in the parameters of this function in this commit:
 * https://github.com/espressif/esp-idf/commit/d39388fe4f4c5bfb0b52df9177307b1688f41016#diff-2df607d77e3f6e350bab8eb31cfd914500ae42744564e1640cec47006cc17a9c
 * There are different builds with the same IDF minor version, some with, some without the commit.
 * So we try to select the correct one by checking if the flag value is defined.
 */
#ifdef LCD_HAL_PCLK_FLAG_ALLOW_EQUAL_SYSCLK
    uint32_t freq
        = lcd_hal_cal_pclk_freq(&lcd.hal, lcd.src_clk_hz, lcd.config.pixel_clock, 0, &clk_div);
#else
    uint32_t freq
        = lcd_hal_cal_pclk_freq(&lcd.hal, lcd.src_clk_hz, lcd.config.pixel_clock, &clk_div);
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ATOMIC() {
        lcd_ll_set_group_clock_coeff(
            lcd.hal.dev, (int)clk_div.integer, (int)clk_div.denominator, (int)clk_div.numerator
        );
    }
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // HAL 只写了 HP 分频。LCD 自己的寄存器里 clk_en 默认是 0（时钟门关着），
    // 而 lcd_clkm_div_num 默认 4 会再除一次，两者都会让 PCLK 出不来。
    // The HAL only writes the HP divider. In the LCD's own registers clk_en defaults to 0 (the
    // gate is shut) and lcd_clkm_div_num defaults to 4, which divides again - either one leaves
    // the pixel clock dead.
    lcd.hal.dev->lcd_clock.clk_en = 1;
    lcd.hal.dev->lcd_clock.lcd_clk_sel = 2;  // 2 = CLK160 / PLL160M
    lcd.hal.dev->lcd_clock.lcd_clkm_div_num = 1;
    lcd.hal.dev->lcd_clock.lcd_clkm_div_a = 0;
    lcd.hal.dev->lcd_clock.lcd_clkm_div_b = 0;
    ESP_LOGI(
        TAG, "S31 PCLK gate: clk_en=%u sel=%u clkm_div=%u",
        (unsigned)lcd.hal.dev->lcd_clock.clk_en, (unsigned)lcd.hal.dev->lcd_clock.lcd_clk_sel,
        (unsigned)lcd.hal.dev->lcd_clock.lcd_clkm_div_num
    );
#endif
#else
    lcd_ll_set_group_clock_coeff(
        &LCD_CAM, (int)clk_div.integer, (int)clk_div.denominator, (int)clk_div.numerator
    );
#endif
#else
    uint32_t freq = lcd_hal_cal_pclk_freq(&lcd.hal, lcd.src_clk_hz, lcd.config.pixel_clock, 0);
#endif

    ESP_LOGI(TAG, "pclk freq: %u Hz (src %u Hz)", (unsigned)freq, (unsigned)lcd.src_clk_hz);
    rebuild_line_geometry(frequency);
    ckv_rmt_build_signal();
    ESP_LOGI(
        TAG, "line width: %d.%dus, %d cycles, ckv high %d.%dus", lcd.line_time_01us / 10,
        lcd.line_time_01us % 10, lcd.line_cycles, lcd.ckv_high_time / 10, lcd.ckv_high_time % 10
    );
}

void IRAM_ATTR epd_lcd_start_frame() {
    int initial_lines = min(LINE_BATCH, lcd.display_lines);

    // hsync: pulse width, back porch, active width, front porch
    const LcdLineTiming_t* t = &lcd.config.line;
    int end_line = lcd.line_cycles - lcd.lcd_res_h - t->le_high_time - t->line_front_porch;
    lcd_ll_set_horizontal_timing(
        lcd.hal.dev,
        t->le_high_time - (lcd.dummy_bytes > 0),
        t->line_front_porch,
        // a dummy byte is neeed in 8 bit mode to work around LCD peculiarities
        lcd.lcd_res_h + (lcd.dummy_bytes > 0),
        end_line
    );
    lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 0, initial_lines, 1);

    // generate the hsync at the very beginning of line
    lcd_ll_set_hsync_position(lcd.hal.dev, 1);

    // reset FIFO of DMA and LCD, incase there remains old frame data
    gdma_reset(lcd.dma_chan);
    lcd_ll_stop(lcd.hal.dev);
    lcd_ll_reset(lcd.hal.dev);
    lcd_ll_fifo_reset(lcd.hal.dev);
    lcd_ll_enable_auto_next_frame(lcd.hal.dev, true);

    lcd.batches = 0;
    lcd.bb_eof_count = 0;
    fill_bounce_buffer(lcd.bounce_buffer[0]);
    fill_bounce_buffer(lcd.bounce_buffer[1]);

    // the start of DMA should be prior to the start of LCD engine
    gdma_start(lcd.dma_chan, (intptr_t)&lcd.dma_nodes[0]);

    // enter a critical section to ensure the frame start timing is correct
    taskENTER_CRITICAL(&frame_start_spinlock);

    // delay 1us is sufficient for DMA to pass data to LCD FIFO
    // in fact, this is only needed when LCD pixel clock is set too high
    gpio_set_level(lcd.config.bus.stv, 0);
    // esp_rom_delay_us(1);
    //  for picture clarity, it seems to be important to start CKV at a "good"
    //  time, seemingly start or towards end of line.
    start_ckv_cycles(initial_lines + 5);
    esp_rom_delay_us(lcd.line_length_us);
    gpio_set_level(lcd.config.bus.stv, 1);
    esp_rom_delay_us(lcd.line_length_us);
    esp_rom_delay_us(lcd.ckv_high_time / 10);

    // start LCD engine
    lcd_ll_start(lcd.hal.dev);

    taskEXIT_CRITICAL(&frame_start_spinlock);
}

#else

/// Dummy implementation to link on the old ESP32
void epd_lcd_init(const LcdEpdConfig_t* config, int display_width, int display_height) {
    assert(false);
}

#endif  // S3 Target

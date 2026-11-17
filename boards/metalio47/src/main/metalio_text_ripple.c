/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：参考CrossLink观测到的22相/10带/四级文字分流，独立生成本地实验序列。
 * 黑白端点预算及中灰擦白次数来自本板GL48；中灰12相复位为实验参数，尚未光学校准。
 * 用户最新指定普通文字也复用22相，同步整页；水波纹沿用8带并合并尾带。
 * 用户报告白底闪动后，所有等值像素保持，不再追加白底补推；变化的旧字仍擦白。图片不使用，默认构建关闭。
 * English: Independently build a local experiment inspired by CrossLink's observed
 * 22-phase/10-band/four-level text path. Endpoint budgets and gray erase counts
 * come from this board's GL48; the12-phase gray reset remains optically uncalibrated.
 * The latest user request shares these22 phases with synchronous ordinary text;
 * ripple uses eight bands with a paired tail. After reported background flicker,
 * all equal pairs hold without white maintenance; changed old glyphs still erase.
 * Pictures and default builds exclude this path.
 * Do not copy binary tables or change VCOM/scanning/power.
 */
#include "metalio_text_ripple.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include <stddef.h>

#if KIIKO_S31_TEXT_RIPPLE
#include <string.h>
#include "esp_log.h"

static uint8_t s_data[METALIO_TEXT_RIPPLE_PHASES][16][4];
static bool s_ready, s_disabled;
static const EpdWaveformPhases s_phases = {
    .phases = METALIO_TEXT_RIPPLE_PHASES, .luts = (const uint8_t*)s_data,
};
static const EpdWaveformPhases* s_ranges[] = {&s_phases};
static const EpdWaveformMode s_mode = {.type = MODE_GL16, .temp_ranges = 1, .range_data = s_ranges};
static const EpdWaveformMode* s_modes[] = {&s_mode};
static EpdWaveform s_waveform;
static const uint8_t s_gray[] = {0,0,0,5,5,5,5,5,10,10,10,10,10,15,15,15};

static int pulse_count(const EpdWaveformPhases* source, int to, int from, int action) {
    int count = 0;
    for (int f = 0; f < source->phases; ++f)
        count += e0470_phase_action(source, f, to, from) == action;
    return count;
}

static void push(int to, int from, int start, int count, int action) {
    for (int f = start; f < start + count; ++f)
        s_data[f][to][from / 4] |= (uint8_t)(action << (6 - 2 * (from % 4)));
}

const EpdWaveform* metalio_text_ripple_waveform(void) {
    if (s_disabled) return NULL;
    if (s_ready) return &s_waveform;
    const EpdWaveformPhases* source = e0470_waveform_phases(&E0470_FULL_WAVEFORM, MODE_GL16);
    if (!source || source->phases != E0470_FULL_GL16_FRAMES || !source->luts || source->phase_times)
        return NULL;
    const int black = pulse_count(source, 0, 15, 1);
    const int white = pulse_count(source, 15, 0, 2);
    const int gray_reset = 12;
    const int gray5 = pulse_count(source, 5, 15, 2);
    const int gray10 = pulse_count(source, 10, 15, 2);
    // 只接受本板已核对的端点预算；原厂表变化时拒绝启用实验。/ Reject the experiment if this board's verified endpoint budgets change.
    if (black != 18 || white != 18 || gray5 != 3 || gray10 != 5 ||
        gray_reset + gray10 > METALIO_TEXT_RIPPLE_PHASES) return NULL;
    memset(s_data, 0, sizeof(s_data));
    for (int from = 0; from < 16; ++from) {
        const int targets[] = {0, 5, 10, 15};
        for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
            const int to = targets[i];
            if (to == from) continue;
            if (to == 0) push(to, from, 0, black, 1);
            else if (to == 15) push(to, from, 0, white, 2);
            else if (from == 15) {
                // 白底字缘沿用已有FOLLOW预算作短黑推；光学灰阶仍须验证。/ White-origin edges use the existing FOLLOW budget for a short black push; optical tones remain unverified.
                const int edge = (17 - to) * E0470_FOLLOW_BLACK_FRAMES / 15;
                push(to, from, 0, edge, 1);
            } else {
                push(to, from, 0, gray_reset, 1);
                push(to, from, gray_reset, to == 5 ? gray5 : gray10, 2);
            }
        }
    }
    // 白→白与其他等值迁移全保持；不让文字外未变化的白底每页重新充白。
    // Hold white-to-white like every other equal pair; unchanged page background gets no maintenance pulse.
    s_waveform = E0470_TEXTTURN_WAVEFORM;
    s_waveform.mode_data = s_modes;
    s_ready = true;
    ESP_LOGI("text_ripple", "%s", "LOCAL EXPERIMENT gray=4 phases=22 ordinary=1band ripple=8bands; panel power unchanged");
    return &s_waveform;
}

const EpdWaveform* metalio_text_ripple_clean_waveform(void) {
    enum { EXTRA=6, HOLD=4, COUNT=METALIO_TEXT_RIPPLE_PHASES+EXTRA };
    static uint8_t data[COUNT][16][4];
    static const EpdWaveformPhases phases={COUNT,(const uint8_t*)data,NULL};
    static const EpdWaveformPhases* ranges[]={&phases};
    static const EpdWaveformMode mode={MODE_GL16,1,ranges};
    static const EpdWaveformMode* modes[]={&mode};
    static EpdWaveform wave;static bool ready;
    const EpdWaveform* original=metalio_text_ripple_waveform();
    if(!original)return NULL;
    if(ready)return &wave;
    const EpdWaveformPhases* source=e0470_waveform_phases(original,MODE_GL16);
    if(!source||source->phases!=METALIO_TEXT_RIPPLE_PHASES)return NULL;
    const int drive=source->phases-HOLD;
    for(int f=drive;f<source->phases;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from)
        if(e0470_phase_action(source,f,to,from))return NULL;
    memcpy(data,source->luts,(size_t)drive*64);
    // 每带的白底与新字同时驱动，已在退白的旧字保留原有的尾段擦白。
    // Drive blank white during the band's text pulse; retain the erase tail for changed whites.
    for (int i = 0; i < EXTRA; ++i) {
        data[i][15][3] |= 2; // White to white occupies the lowest two bits.
        memset(data[drive + i][15], 0xaa, 4);
        data[drive + i][15][3] &= 0xfc;
    }
    memcpy(data+drive+EXTRA,source->luts+(size_t)drive*64,HOLD*64);
    wave=*original;wave.num_modes=1;wave.mode_data=modes;ready=true;return &wave;
}

bool display_reader_fast_ripple_enabled(void) { return metalio_text_ripple_waveform() != NULL; }

bool metalio_text_ripple_prepare(EpdiyHighlevelState* hl, EpdRect area) {
    if (!hl || !hl->front_fb || !hl->back_fb || hl->front_fb == hl->back_fb ||
        area.width <= 0 || area.height <= 0 || !display_reader_fast_ripple_enabled()) return false;
    const int pw = epd_width(), ph = epd_height();
    if (pw <= 0 || ph <= 0 || (pw & 1)) return false;
    const enum EpdRotation rotation = epd_get_rotation();
    const bool portrait = rotation == EPD_ROT_PORTRAIT || rotation == EPD_ROT_INVERTED_PORTRAIT;
    const int lw = portrait ? ph : pw, lh = portrait ? pw : ph;
    int64_t right = (int64_t)area.x + area.width, bottom = (int64_t)area.y + area.height;
    int x = area.x < 0 ? 0 : area.x, y = area.y < 0 ? 0 : area.y;
    if (right > lw) right = lw;
    if (bottom > lh) bottom = lh;
    if (right <= x || bottom <= y) return false;
    int w = (int)right - x, h = (int)bottom - y;
    EpdRect phys;
    switch (rotation) {
        case EPD_ROT_LANDSCAPE: phys = (EpdRect){x,y,w,h}; break;
        case EPD_ROT_PORTRAIT: phys = (EpdRect){pw-y-h,x,h,w}; break;
        case EPD_ROT_INVERTED_LANDSCAPE: phys = (EpdRect){pw-x-w,ph-y-h,w,h}; break;
        case EPD_ROT_INVERTED_PORTRAIT: phys = (EpdRect){y,ph-x-w,h,w}; break;
        default: return false;
    }
    for (int row = phys.y; row < phys.y + phys.height; ++row) {
        uint8_t* pixels = hl->front_fb + (size_t)row * (pw / 2);
        int col = phys.x, end = phys.x + phys.width;
        if (col & 1) {
            uint8_t v = pixels[col / 2];
            pixels[col++ / 2] = (uint8_t)((v & 15) | (s_gray[v >> 4] << 4));
        }
        for (; col + 1 < end; col += 2) {
            uint8_t v = pixels[col / 2];
            pixels[col / 2] = (uint8_t)(s_gray[v & 15] | (s_gray[v >> 4] << 4));
        }
        if (col < end) {
            uint8_t v = pixels[col / 2];
            pixels[col / 2] = (uint8_t)((v & 0xf0) | s_gray[v & 15]);
        }
    }
    return true;
}

void metalio_text_ripple_disable(void) { s_disabled = true; }
#else
bool display_reader_fast_ripple_enabled(void) { return false; }
const EpdWaveform* metalio_text_ripple_waveform(void) { return NULL; }
const EpdWaveform* metalio_text_ripple_clean_waveform(void) { return NULL; }
bool metalio_text_ripple_prepare(EpdiyHighlevelState* hl, EpdRect area) { (void)hl; (void)area; return false; }
void metalio_text_ripple_disable(void) {}
#endif

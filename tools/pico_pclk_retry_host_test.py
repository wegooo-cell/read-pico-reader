#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：执行 Pico 欠载降频后的回升：干净扫屏够数才试回默认频率，再次欠载所需次数翻倍并封顶。
# English: Execute the Pico clock retry after an underrun drop: retry the default clock only after enough
# clean scans, and double the requirement (capped) after each further underrun.
from pathlib import Path
import re, subprocess, tempfile
from home_cover_cache_host_test import function

R = Path(__file__).resolve().parents[1]
display = R / 'boards/pico/src/main/display.c'
text = display.read_text()
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define TAG "t"
#define ESP_LOGI(tag, ...) ((void)0)
#define DISPLAY_PCLK_DEFAULT_MHZ 18
#define DISPLAY_PCLK_SAFE_MHZ 12
static uint32_t clean;
static int applied;
uint32_t epd_lcd_clean_scans(void) { return clean; }
static void read_pico_epd_set_pclk(int mhz) { applied = mhz; }
'''
unit += ''.join(re.findall(r'#define PCLK_RETRY_\w+ [^\n]*\n', text))
unit += 'static int s_pclk_mhz = DISPLAY_PCLK_DEFAULT_MHZ;\nstatic uint32_t s_pclk_safe_mark;\nstatic uint32_t s_pclk_retry_scans;\n'
unit += function('pclk_drop_to_safe', display) + '\n' + function('pclk_retry_default', display) + '\n'
unit += r'''
int main(void) {
    pclk_retry_default();
    assert(s_pclk_mhz == 18 && !applied);
    clean = 5; pclk_drop_to_safe();
    assert(s_pclk_mhz == 12 && applied == 12 && s_pclk_retry_scans == PCLK_RETRY_FIRST_SCANS);
    // 恢复本身的扫屏刚结束时不能立即回升。/ Do not climb back right after the recovery scan itself.
    clean += PCLK_RETRY_FIRST_SCANS - 1; pclk_retry_default(); assert(s_pclk_mhz == 12);
    clean += 1; pclk_retry_default(); assert(s_pclk_mhz == 18 && applied == 18);
    // 回升后再欠载：要求翻倍。/ Another underrun after the climb doubles the requirement.
    pclk_drop_to_safe(); assert(s_pclk_retry_scans == 2 * PCLK_RETRY_FIRST_SCANS);
    clean += PCLK_RETRY_FIRST_SCANS; pclk_retry_default(); assert(s_pclk_mhz == 12);
    clean += PCLK_RETRY_FIRST_SCANS; pclk_retry_default(); assert(s_pclk_mhz == 18);
    for (int i = 0; i < 20; ++i) pclk_drop_to_safe();
    assert(s_pclk_retry_scans == PCLK_RETRY_MAX_SCANS);
    // 计数回绕时仍按差值判断。/ Counter wraparound still compares by difference.
    clean = UINT32_MAX - 3; pclk_drop_to_safe(); clean += PCLK_RETRY_MAX_SCANS; pclk_retry_default();
    assert(s_pclk_mhz == 18);
    puts("PASS: Pico retries the default pixel clock after clean scans and backs off on repeated underruns");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 't.c'; exe = Path(tmp) / 't'
    c.write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

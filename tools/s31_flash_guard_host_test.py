#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：验证 SDK 关缓存补丁的顺序和重复构建。/ English: Check cache-guard ordering and repeat builds."""
from pathlib import Path
from tempfile import TemporaryDirectory
from prepare_s31_flash_guard import prepare


SOURCE = '''#include "esp_attr.h"
void IRAM_ATTR spi_flash_op_block_func(void *arg)
{
    esp_intr_noniram_disable();
    uint32_t cpuid = (uint32_t) arg;
    s_flash_op_complete = false;
    s_flash_op_can_start = true;
    while (!s_flash_op_complete) {}
    spi_flash_restore_cache(cpuid, s_flash_op_cache_state[cpuid]);
}
void IRAM_ATTR spi_flash_restore_cache(uint32_t cpuid, uint32_t saved_state)
{
    esp_cache_resume_ext_mem_cache();
    esp_cpu_branch_prediction_enable();
}
'''

with TemporaryDirectory() as temporary:
    sdk = Path(temporary)
    file = sdk / 'components/spi_flash/cache_utils.c'
    file.parent.mkdir(parents=True)
    file.write_text(SOURCE)
    prepare(sdk)
    patched = file.read_text()
    assert '#if CONFIG_IDF_TARGET_ESP32S31 && SOC_BRANCH_PREDICTOR_SUPPORTED' in patched
    assert patched.index('esp_intr_noniram_disable();') < patched.index('esp_cpu_branch_prediction_disable();')
    assert patched.index('esp_cpu_branch_prediction_disable();') < patched.index('s_flash_op_can_start = true;')
    assert patched.count('#include "esp_cpu.h"') == 1
    prepare(sdk)
    assert file.read_text() == patched
    upstream = SOURCE.replace('    uint32_t cpuid',
                              '    esp_cpu_branch_prediction_disable();\n    uint32_t cpuid')
    file.write_text(upstream)
    prepare(sdk)
    assert file.read_text() == upstream
    file.write_text(SOURCE.replace('esp_cache_resume_ext_mem_cache();', 'unknown_cache_restore();'))
    try:
        prepare(sdk)
    except ValueError:
        pass
    else:
        raise AssertionError('An unknown SDK restore sequence must require review')
print('PASS: S31 parked-core guard before readiness, repeatability, upstream fix and SDK restore validation')

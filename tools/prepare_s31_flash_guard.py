#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：补齐 S31 另一核的 Flash 关缓存保护。/ English: Guard the parked S31 core during flash operations."""
from pathlib import Path
import sys


def prepare(sdk: Path):
    path = sdk / 'components/spi_flash/cache_utils.c'
    source = path.read_text()
    start = source.index('void IRAM_ATTR spi_flash_op_block_func(void *arg)')
    publish = source.index('    s_flash_op_can_start = true;', start)
    restore = source.index('void IRAM_ATTR spi_flash_restore_cache(')
    resume = source.index('    esp_cache_resume_ext_mem_cache();', restore)
    enable = source.index('    esp_cpu_branch_prediction_enable();', resume)
    assert resume < enable, 'Review the SDK cache/predictor restore order'
    if 'esp_cpu_branch_prediction_disable();' not in source[start:publish]:
        anchor = '    esp_intr_noniram_disable();\n'
        at = source.index(anchor, start) + len(anchor)
        assert at < publish, 'Review the SDK parked-core handshake'
        source = source[:at] + (
            '#if CONFIG_IDF_TARGET_ESP32S31 && SOC_BRANCH_PREDICTOR_SUPPORTED\n'
            '    // 发布就绪前禁止另一核推测读取 Flash，恢复缓存后由 SDK 重新开启。\n'
            '    // Disable speculative flash fetches before readiness; the SDK restores prediction after cache.\n'
            '    esp_cpu_branch_prediction_disable();\n'
            '#endif\n'
        ) + source[at:]
        if '#include "esp_cpu.h"' not in source:
            source = source.replace('#include "esp_attr.h"', '#include "esp_cpu.h"\n#include "esp_attr.h"', 1)
        path.write_text(source)
    print('S31 flash guard verified: parked-core prediction stops before cache-off readiness')


if __name__ == '__main__':
    prepare(Path(sys.argv[1]).resolve())

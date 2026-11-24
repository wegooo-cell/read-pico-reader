#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 中文：编译并运行阅读翻页波形的 DC 审计（只用主机替身，不碰实屏）。
# English: Build and run the reader-turn DC audit with host shims only; no panel access.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/book-tests

link_gc=(-Wl,--gc-sections)
if [[ "$(uname -s)" == "Darwin" ]]; then link_gc=(-Wl,-dead_strip); fi

cc -std=c11 -Wall -Wextra -Werror -g -O1 \
    -ffunction-sections -fdata-sections "${link_gc[@]}" \
    -Itools/reader_waveform_dc_stubs -Itools/waveform_host_stubs \
    -Icomponents/epdiy/include -Icomponents/epdiy/src \
    -Icomponents/e0470_epaper_waveform/include -Icomponents/e0470_epaper_waveform/waveforms \
    -Imain \
    tools/reader_waveform_dc_host_test.c \
    boards/pico/src/main/display_reader_waveform.c \
    boards/pico/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c \
    components/e0470_epaper_waveform/e0470_waveform_trim.c \
    -o build/book-tests/reader-waveform-dc
build/book-tests/reader-waveform-dc

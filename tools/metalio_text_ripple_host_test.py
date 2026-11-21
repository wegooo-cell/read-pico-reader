#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Compile the real S31 text experiment and stagger engine with ASan/UBSan."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='s31-text-ripple-') as folder:
    temp = Path(folder)
    (temp/'esp_log.h').write_text('#pragma once\n#include <stdio.h>\n#undef ESP_LOGI\n'
        '#define ESP_LOGI(tag,fmt,...) do { char buf[256]; (void)(tag); '
        '(void)snprintf(buf,sizeof(buf),fmt,__VA_ARGS__); } while(0)\n')
    command = ['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
               '-Wno-unknown-attributes','-g','-fsanitize=address,undefined',
               '-DKIIKO_BOARD_METALIO47=1','-I'+str(temp),
               '-I'+str(root/'components/epdiy/src'),'-I'+str(root/'components/epdiy/include'),
               '-I'+str(root/'components/e0470_epaper_waveform/include'),
               '-I'+str(root/'components/e0470_epaper_waveform/metalio'),
               '-I'+str(root/'components/e0470_page_turn/include'),
               '-I'+str(root/'main'),'-I'+str(root/'boards/metalio47/src/main'),
               '-I'+str(root/'tools/water_turn_stubs'),'-I'+str(root/'tools/metalio_stubs'),
               str(root/'tools/metalio_text_ripple_host_test.c'),
               str(root/'boards/metalio47/src/main/metalio_text_ripple.c'),
               str(root/'boards/metalio47/src/components/e0470_page_turn/e0470_page_turn.c'),
               str(root/'boards/metalio47/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c'),
               str(root/'components/e0470_epaper_waveform/e0470_waveform_trim.c')]
    for enabled,rc110 in ((0,0),(1,0),(1,1)):
        binary=temp/f'ripple-{enabled}-{rc110}'
        subprocess.run(command+[f'-DKIIKO_S31_TEXT_RIPPLE={enabled}',f'-DKIIKO_S31_RC110_RIPPLE={rc110}','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

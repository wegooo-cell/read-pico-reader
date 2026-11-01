#!/usr/bin/env python3
"""实际S31 GL37表、原厂逐迁移对照和OOM；不连接设备。
Actual S31 GL37 LUT, optional frozen vendor oracle and OOM; no device access.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
if 'display_reader_panel37_waveform(void)' not in (root / 'main/display_main_waveform.c').read_text():
    print('SKIP: this board does not use the S31 GL37 panel experiment')
    raise SystemExit(0)
oracle = os.environ.get('KIIKO_METALIO_OFFICIAL_TRIM')
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epdiy.h"
#include "e0470_epaper_waveform.h"
#include "display_main_waveform.h"
#ifdef HAVE_VENDOR_ORACLE
#include "my_waveform_trim.h"
#endif
extern const EpdWaveformPhases my_gl_25_0,my_gc_25_0,my_du_25_0;
static unsigned allocations;
static bool oom;
void *heap_caps_malloc(size_t bytes,int caps){
    assert(bytes==48*16*4&&caps==3);++allocations;
    return oom?NULL:malloc(bytes);
}
int main(int argc,char **argv){
    assert(argc==3);oom=atoi(argv[1]);e0470_waveform_init();
    const EpdWaveformPhases *gl=e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_GL16);
    const EpdWaveformPhases *gc=e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_GC16);
    const EpdWaveformPhases *du=e0470_waveform_phases(&E0470_WAVEFORM,MODE_DU);
    assert(gl&&gc&&du&&gl->phases==48&&gc->phases==48&&du->phases==20);
    unsigned char raw_gl[48*64],raw_gc[48*64],raw_du[20*64];
    unsigned char live_gl[48*64],live_gc[48*64],live_du[20*64];
    memcpy(raw_gl,my_gl_25_0.luts,sizeof(raw_gl));
    memcpy(raw_gc,my_gc_25_0.luts,sizeof(raw_gc));
    memcpy(raw_du,my_du_25_0.luts,sizeof(raw_du));
    memcpy(live_gl,gl->luts,sizeof(live_gl));memcpy(live_gc,gc->luts,sizeof(live_gc));
    memcpy(live_du,du->luts,sizeof(live_du));
    const EpdWaveform *wave=display_reader_panel37_waveform();
#if KIIKO_S31_READER_PANEL37
    if(oom){
        assert(!wave&&allocations==1);
        for(int n=0;n<10;++n)assert(!display_reader_panel37_waveform());
        assert(allocations==1);puts("PASS: OOM returns native-fallback sentinel without repeated allocation");
    }else{
        assert(wave&&allocations==1&&wave->num_modes==1&&wave->num_temp_ranges==1);
        assert(wave->temp_intervals==E0470_FULL_WAVEFORM.temp_intervals);
        const EpdWaveformPhases *p=e0470_waveform_phases(wave,MODE_GL16);
        assert(p&&p->phases==37&&!p->phase_times&&p->luts);
        assert(!e0470_waveform_phases(wave,MODE_GC16)&&!e0470_waveform_phases(wave,MODE_DU));
        for(int n=0;n<10;++n)assert(display_reader_panel37_waveform()==wave);
        assert(allocations==1);
#ifdef HAVE_VENDOR_ORACLE
        uint8_t expected[48][16][4];
        const my_waveform_trim_t trim={11,5,0,3};
        assert(my_waveform_trim(&my_gl_25_0,&trim,expected)==37);
        const EpdWaveformPhases vendor={37,(const uint8_t*)expected,NULL};
#endif
        for(int to=0;to<16;++to)for(int from=0;from<16;++from){
            unsigned pushes=0;
            for(int f=0;f<37;++f){
                int action=e0470_phase_action(p,f,to,from);
                assert(action>=0&&action<3);
                if(from==to||f>=34)assert(action==0);
                pushes+=action!=0;
#ifdef HAVE_VENDOR_ORACLE
                assert(action==(from==to?0:e0470_phase_action(&vendor,f,to,from)));
#endif
            }
            if(from!=to)assert(pushes>0);
        }
        FILE *file=fopen(argv[2],"wb");assert(file);
        assert(fwrite(p->luts,1,37*64,file)==37*64&&fclose(file)==0);
        puts("PASS: 256 gray pairs/37 phases, all equal pairs hold, three final holds, one bounded PSRAM allocation");
    }
#else
    assert(!wave&&!allocations);
    for(int n=0;n<10;++n)assert(!display_reader_panel37_waveform());
    assert(!allocations);puts("PASS: experiment OFF has no LUT allocation and requires the active default fallback");
#endif
    assert(!memcmp(raw_gl,my_gl_25_0.luts,sizeof(raw_gl)));
    assert(!memcmp(raw_gc,my_gc_25_0.luts,sizeof(raw_gc)));
    assert(!memcmp(raw_du,my_du_25_0.luts,sizeof(raw_du)));
    assert(!memcmp(live_gl,gl->luts,sizeof(live_gl))&&!memcmp(live_gc,gc->luts,sizeof(live_gc)));
    assert(!memcmp(live_du,du->luts,sizeof(live_du)));
}
'''
with tempfile.TemporaryDirectory(prefix='s31-panel37-') as temporary:
    folder = Path(temporary)
    test = folder / 'test.c'
    test.write_text(unit)
    flags = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
             '-Imain', '-Itools/shelf_waveform_host_stubs', '-Itools/waveform_host_stubs',
             '-Icomponents/epdiy/include', '-Icomponents/epdiy/src',
             '-Icomponents/e0470_epaper_waveform/include', '-Icomponents/e0470_epaper_waveform/metalio']
    sources = [str(test), 'main/display_main_waveform.c',
               'components/e0470_epaper_waveform/e0470_epaper_waveform.c',
               'components/e0470_epaper_waveform/e0470_waveform_trim.c']
    if oracle:
        vendor = Path(oracle)
        assert (vendor / 'my_waveform_trim.c').is_file() and (vendor / 'my_waveform_trim.h').is_file()
        (folder / 'epd_internals.h').write_text('#include "epd_waveform.h"\n')
        flags += ['-DHAVE_VENDOR_ORACLE=1', '-I' + str(folder), '-I' + str(vendor)]
        sources += [str(vendor / 'my_waveform_trim.c')]
    for enabled in (1, 0):
        exe = folder / ('panel37-' + str(enabled))
        subprocess.run(flags + ['-DKIIKO_S31_READER_PANEL37=' + str(enabled), *sources, '-o', str(exe)],
                       cwd=root, check=True)
        dump = folder / 'panel37.lut'
        for fail in (0, 1):
            subprocess.run([str(exe), str(fail), str(dump)], check=True)
        if enabled:
            digest = hashlib.sha256(dump.read_bytes()).hexdigest()
            print('GL37 equal-held LUT SHA256:', digest, 'vendor oracle:', bool(oracle))
            # 由固定原厂GL48及官方11/5/0/3算法独立核对；不把光学画质视为已验证。
            # Pinned vendor GL48 and independent11/5/0/3 output; this does not verify optical quality.
            assert digest == 'ca1416ac598d063c5403ae498da9d1423f5e50a619a9bab8520c75cdff5ad1ea'

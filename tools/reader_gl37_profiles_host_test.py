#!/usr/bin/env python3
"""执行真实阅读GL16派生表，核对全部灰阶迁移与共享表隔离。
Execute reader GL16 builders, checking every gray transition and shared-table isolation.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
if not (root / 'main/display_reader_gl37.c').is_file():
    print('SKIP: reader GL37 profiles are S31-only')
    raise SystemExit(0)
unit = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
extern const EpdWaveformPhases my_gl_25_0,my_gc_25_0,my_du_25_0;
int main(int argc,char **argv){
 assert(argc==2); e0470_waveform_init();
 const EpdWaveformPhases *gl=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);
 const EpdWaveformPhases *gc=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GC16);
 const EpdWaveformPhases *du=e0470_waveform_phases(&E0470_WAVEFORM,MODE_DU);
 unsigned char old_gl[48*64],old_gc[48*64],old_du[20*64],raw[116*64];
 assert(gl->phases<=48&&gc->phases<=48&&du->phases<=20);
 memcpy(old_gl,gl->luts,gl->phases*64);memcpy(old_gc,gc->luts,gc->phases*64);memcpy(old_du,du->luts,du->phases*64);
 memcpy(raw,my_gl_25_0.luts,48*64);memcpy(raw+48*64,my_gc_25_0.luts,48*64);memcpy(raw+96*64,my_du_25_0.luts,20*64);
 for(int i=0;i<DISPLAY_READER_GRAY_COUNT;++i){
  const EpdWaveform *w=display_reader_gray_waveform(i);
  const EpdWaveformPhases *p=e0470_waveform_phases(w,MODE_GL16);
  assert(w&&p&&p->luts&&p->phases>0&&p->phases<=37&&!p->phase_times);
  assert(!e0470_waveform_phases(w,MODE_DU)&&!e0470_waveform_phases(w,MODE_GC16));
  for(int n=0;n<8;++n)assert(display_reader_gray_waveform(i)==w);
  char path[1024];snprintf(path,sizeof(path),"%s/%d.lut",argv[1],i);
  FILE *f=fopen(path,"wb");assert(f&&fwrite(p->luts,1,p->phases*64,f)==(size_t)p->phases*64&&fclose(f)==0);
  for(int to=0;to<16;++to)for(int from=0;from<16;++from)for(int phase=0;phase<p->phases;++phase){
   int a=e0470_phase_action(p,phase,to,from);assert(a>=0&&a<3);
   if(i==DISPLAY_READER_GRAY_TEXT&&to==from)assert(!a);
   if(i==DISPLAY_READER_GRAY_NORMALIZE&&to==0)assert(a==(phase<18?2:phase<36?1:0));
   if(i==DISPLAY_READER_GRAY_NORMALIZE_GRAY&&to==from&&to>0)assert(!a);
  }
 }
 const EpdWaveformPhases *water=e0470_waveform_phases(display_reader_gray_waveform(DISPLAY_READER_GRAY_WATER),MODE_GL16);
 const EpdWaveformPhases *tail=e0470_waveform_phases(display_reader_gray_waveform(DISPLAY_READER_GRAY_WATER_TAIL),MODE_GL16);
 int white_ticks=0;
 assert(tail->phases==37);
 for(int to=0;to<16;++to)for(int from=0;from<16;++from)for(int phase=0;phase<37;++phase){
  int action=e0470_phase_action(tail,phase,to,from);
  if(to==15){
   if(phase>=33&&phase<36)assert(action==2);
   else assert(action==(from==15?0:e0470_phase_action(water,phase,to,from)));
   if(from==15)white_ticks+=action==2;
  }else if(to==from)assert(!action);
  else assert(action==e0470_phase_action(water,phase,to,from));
  if(phase>=34)assert(action==(phase<36&&to==15?2:0));
 }
 assert(white_ticks==3);
 assert(!display_reader_gray_waveform(-1)&&!display_reader_gray_waveform(DISPLAY_READER_GRAY_COUNT));
 assert(!memcmp(old_gl,gl->luts,gl->phases*64)&&!memcmp(old_gc,gc->luts,gc->phases*64)&&!memcmp(old_du,du->luts,du->phases*64));
 assert(!memcmp(raw,my_gl_25_0.luts,48*64)&&!memcmp(raw+48*64,my_gc_25_0.luts,48*64)&&!memcmp(raw+96*64,my_du_25_0.luts,20*64));
 puts("PASS: eight bounded profiles, nonwhite changes preserved, equal ink held, all white targets reinforced at the tail, final phase held, unchanged original seven/shared/raw LUTs");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-gl37-') as temporary:
    folder = Path(temporary)
    test = folder / 'test.c'; test.write_text(unit)
    executable = folder / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
        '-fsanitize=address,undefined', '-DKIIKO_BOARD_METALIO47=1', '-Imain',
        '-Itools/waveform_host_stubs', '-Icomponents/epdiy/include', '-Icomponents/epdiy/src',
        '-Icomponents/e0470_epaper_waveform/include', '-Icomponents/e0470_epaper_waveform/metalio',
        str(test), 'main/display_reader_gl37.c',
        'components/e0470_epaper_waveform/e0470_epaper_waveform.c',
        'components/e0470_epaper_waveform/e0470_waveform_trim.c', '-o', str(executable)], cwd=root, check=True)
    subprocess.run([str(executable), str(folder)], check=True)
    # 独立已核对的逐相输出，覆盖全部灰阶对，避免仅测试宏或函数名。
    # Independently verified phase outputs cover every pair rather than testing names/macros.
    expected = [
        '077468e611b3c23d73d5574f3b1bc6f03605215ddbc22a8076254081f3a8679a',
        '82f9ea379cbd5ff31b7fb72437e553b1617101457226285f63e15dcbf95685b3',
        'bf165bcb3e27e9df228e638d19dc912e31e6df907ea91d2ebfd7e47adba7572a',
        '46681c819bcd1fbe5619b2a2348dd67d97e2479fdefea42c4fc12ec7e1f0a78b',
        'e48b1bc28e00dea76f52e905c1383a4578f00d25a9d7371aeb4833cc7b5b25d4',
        'cfc1cdcfa2c295543e8d1a618c69b3d93c6e4019eaf116cbc66db1adf8270cde',
        '751c8a965e0d471b3ae7e374c6c6e91b2624cd3c7ee1b6ae389179b8f2f3c54c',
    ]
    for i in range(7):
        data = (folder / f'{i}.lut').read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        print(i, len(data)//64, digest)
        assert digest == expected[i], f'profile {i} changed'
        oracle = os.environ.get('KIIKO_READER_GL37_ORACLE')
        if oracle:
            before = (Path(oracle) / f'{i}.lut').read_bytes()
            if i != 1:
                assert data == before, f'profile {i} differs from independent oracle'
            else:
                def actions(blob, to, origin):
                    return [(blob[phase*64+to*4+origin//4] >> (6-2*(origin%4))) & 3 for phase in range(37)]
                for to in range(16):
                    for origin in range(16):
                        old = actions(before, to, origin)
                        target = actions(data, to, origin)
                        lead = next((n for n,a in enumerate(old) if a),37) if to < 15 else 0
                        assert target == old[lead:]+[0]*lead
                        assert [a for a in target if a] == [a for a in old if a]
                print('PASS: all256 pairs preserve active pulse order/counts; only stripped leading holds advance')

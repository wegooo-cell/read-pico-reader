#!/usr/bin/env python3
"""真实S31水波纹引擎验证状态栏/页脚不驱动、不回写。
Run the real S31 ripple engine to exclude status/footer from both drive masks and commits.
"""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
fixture=(root/'tools/water_turn_host_test.c').read_text()
prefix=fixture[:fixture.index('int main(void) {')]
anchor='    assert(staged_luts && (staged_lines != NULL) != (staged_bands != NULL));'
prefix=prefix.replace(anchor,anchor+r'''
    for(int y=0;y<HEIGHT;++y)if(lines[y])assert(y>=difference_crop.y&&y<difference_crop.y+difference_crop.height);
    for(int x=0;x<WIDTH;++x)if((columns[x/2]>>(4*(x&1)))&15)
        assert(x>=difference_crop.x&&x<difference_crop.x+difference_crop.width);
''')
source=(root/'boards/metalio47/src/components/e0470_page_turn/e0470_page_turn.c').read_text()
core=source[source.index('static const char* TAG'):source.index('static enum EpdDrawError text_turn(')]
unit=prefix+'\n#include "esp_heap_caps.h"\n#include "esp_attr.h"\n#include "esp_log.h"\n'+core+r'''
int main(void){
 EpdiyHighlevelState hl={front,back,diff,dirty_lines,dirty_columns};
 const int tops[]={176,80,0},bottoms[]={1048,1168,1204};
 e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
 test_bands=8;test_launch_step=1;check_direction=false;
 for(int rot=1;rot<=3;rot+=2)for(int dir=0;dir<2;++dir)for(int style=0;style<3;++style){
  rotation=(enum EpdRotation)rot;direction=(e0470_turn_dir_t)dir;
  EpdRect body={0,tops[style],HEIGHT,bottoms[style]-tops[style]};
  memset(front,0x24,sizeof(front));memset(back,0xff,sizeof(back));memset(phase_hits,0,sizeof(phase_hits));
  scans=differences=powerons=fail_at=0;now_us=0;
  assert(e0470_page_turn_with_waveform_compact(&hl,body,direction,&soft_wave,8)==EPD_DRAW_SUCCESS);
  assert(scans==44&&differences==1&&!staged_luts);
  for(int p=0;p<37;++p)assert(phase_hits[p]==8);
  check_crop_baseline(body);
  memset(back,0xff,sizeof(back));scans=differences=0;fail_at=13;
  assert(e0470_page_turn_with_waveform_compact(&hl,body,direction,&soft_wave,8)==EPD_DRAW_OTHER_ERROR);
  assert(scans==13&&!staged_luts);
  for(size_t i=0;i<sizeof(back);++i)assert(back[i]==0xff);
 }
 e0470_page_turn_release();
 puts("PASS: real S31 compact37/8-band ripple, portrait/inverted, forward/back, standard/fullscreen/immersive; every scan excludes metadata and failures keep old history");
}
'''
with tempfile.TemporaryDirectory(prefix='s31-ripple-crop-') as temporary:
    folder=Path(temporary);source=folder/'test.c';source.write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
        '-fsanitize=address,undefined','-DTEST_PHASES=37','-I'+str(root/'tools/water_turn_stubs'),
        '-I'+str(root/'components/e0470_page_turn/include'),str(source),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)

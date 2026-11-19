#!/usr/bin/env python3
"""真实S31水波纹比较八带基线与四带合尾，保留相位和状态栏/页脚边界。
Compare eight-band and four-band paired-tail S31 ripple with complete phases and metadata crops.
"""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
fixture=(root/'tools/water_turn_host_test.c').read_text()
prefix=fixture[:fixture.index('int main(void) {')].replace('37','TEST_PHASES')
anchor='    assert(staged_luts && (staged_lines != NULL) != (staged_bands != NULL));'
assert prefix.count(anchor)==1
prefix=prefix.replace(anchor,anchor+r'''
    for(int y=0;y<HEIGHT;++y)if(lines[y])assert(y>=difference_crop.y&&y<difference_crop.y+difference_crop.height);
    for(int x=0;x<WIDTH;++x)if((columns[x/2]>>(4*(x&1)))&15)
        assert(x>=difference_crop.x&&x<difference_crop.x+difference_crop.width);
''')
prefix=prefix.replace('static int test_bands=8, test_launch_step=1;',
    'static int test_bands=8, test_launch_step=1; static bool paired_tail; static int band_hits[8][TEST_PHASES]; static EpdRect logical_body;')
start=prefix.index('    int expected = 0;')
end=prefix.index('    if (tick == 0 && check_direction)')
assert 'assert(active == expected);' in prefix[start:end]
prefix=prefix[:start]+r'''
    int last_launch=test_bands-1-(paired_tail?1:0);
    int expected=0;bool expected_seen[TEST_PHASES]={0};
    for(int b=0;b<test_bands;++b){
        int launch=b<last_launch?b:last_launch;
        int phase=tick-launch;
        if(phase>=0&&phase<TEST_PHASES&&!expected_seen[phase]){expected_seen[phase]=true;++expected;}
        // 每带取中心点，独立核对方向、启动顺序以及所有有效相位。
        // Sample each band center to check direction, launches and every active phase independently.
        int lx=logical_body.x+logical_body.width*(2*b+1)/(2*test_bands);
        if(direction==E0470_TURN_RTL)lx=logical_body.x+logical_body.width-1-(lx-logical_body.x);
        int py=rotation==EPD_ROT_PORTRAIT?lx:HEIGHT-1-lx;
        assert(staged_lines);int actual=staged_lines[py];
        if(phase>=0&&phase<TEST_PHASES){assert(actual==phase);++band_hits[b][phase];}
        else assert(actual<0);
    }
    assert(active==expected);
    for(int p=0;p<TEST_PHASES;++p){assert(phase_seen[p]==expected_seen[p]);phase_hits[p]+=phase_seen[p];}
'''+prefix[end:]
source=(root/'boards/metalio47/src/components/e0470_page_turn/e0470_page_turn.c').read_text()
core=source[source.index('static const char* TAG'):source.index('static enum EpdDrawError text_turn(')]
unit=prefix+'\n#include "esp_heap_caps.h"\n#include "esp_attr.h"\n#include "esp_log.h"\n'+core+r'''
int main(void){
 EpdiyHighlevelState hl={front,back,diff,dirty_lines,dirty_columns};
 const int tops[]={176,80,0},bottoms[]={1048,1168,1204};
 e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
 test_launch_step=1;check_direction=false;
 for(int profile=0;profile<2;++profile)for(int rot=1;rot<=3;rot+=2)for(int dir=0;dir<2;++dir)for(int style=0;style<3;++style){
  test_bands=profile?4:8;paired_tail=profile;
  int expected_ticks=TEST_PHASES+(profile?2:7);
  rotation=(enum EpdRotation)rot;direction=(e0470_turn_dir_t)dir;
  EpdRect body={0,tops[style],HEIGHT,bottoms[style]-tops[style]};logical_body=body;
  memset(front,0x24,sizeof(front));memset(back,0xff,sizeof(back));memset(phase_hits,0,sizeof(phase_hits));memset(band_hits,0,sizeof(band_hits));
  scans=differences=powerons=fail_at=0;now_us=0;
  enum EpdDrawError result=profile?e0470_reader_ripple_with_waveform(&hl,body,direction,&soft_wave,4):
      e0470_page_turn_with_waveform_compact(&hl,body,direction,&soft_wave,8);
  assert(result==EPD_DRAW_SUCCESS&&scans==expected_ticks&&differences==1&&!staged_luts);
  assert(now_us==(int64_t)expected_ticks*E0470_TURN_FAST_TICK_US);
  for(int p=0;p<TEST_PHASES;++p){
   assert(phase_hits[p]==(profile?3:8));
   for(int b=0;b<test_bands;++b)assert(band_hits[b][p]==1);
  }
  check_crop_baseline(body);
  // 各扫描位置注入失败，不得回写半成品历史或遗留相位指针。
  // Fail each scan in turn without committing partial history or retaining phase pointers.
  for(int failure=1;failure<=expected_ticks;++failure){
   memset(back,0xff,sizeof(back));scans=differences=0;fail_at=failure;
   result=profile?e0470_reader_ripple_with_waveform(&hl,body,direction,&soft_wave,4):
      e0470_page_turn_with_waveform_compact(&hl,body,direction,&soft_wave,8);
   assert(result==EPD_DRAW_OTHER_ERROR&&scans==failure&&!staged_luts);
   for(size_t i=0;i<sizeof(back);++i)assert(back[i]==0xff);
  }
 }
 e0470_page_turn_release();
 printf("PASS: S31 %d phases, eight-band baseline vs four-band paired tail; per-band phases, crops, timing and every failed scan\n",TEST_PHASES);
}
'''
with tempfile.TemporaryDirectory(prefix='s31-ripple-crop-') as temporary:
    folder=Path(temporary);source=folder/'test.c';source.write_text(unit)
    for phases in (37,33,29):
        binary=folder/('test-'+str(phases))
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
            '-fsanitize=address,undefined','-DTEST_PHASES='+str(phases),'-I'+str(root/'tools/water_turn_stubs'),
            '-I'+str(root/'components/e0470_page_turn/include'),str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
// 默认表测试隔离可选GL37分配，真实GL37由专项测试覆盖。/ Isolate optional GL37 allocation here; dedicated tests cover the real profile.
const EpdWaveform* display_reader_panel37_waveform(void){return NULL;}
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
enum EpdRotation epd_get_rotation(void){return EPD_ROT_INVERTED_PORTRAIT;}
int main(void){
 e0470_waveform_init();
 const EpdWaveform *wave=display_reader_text_waveform();
 const EpdWaveformPhases *actual=e0470_waveform_phases(wave,MODE_GL16);
 const EpdWaveformPhases *vendor=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);
 assert(actual&&vendor&&actual->phases==E0470_GL16_FRAMES+16+4&&vendor->phases==E0470_GL16_FRAMES);
 assert(e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_GL16)->phases==48);
 int hold=0;
 while(hold<vendor->phases){
  const int f=vendor->phases-1-hold;
  int nonzero=0;
  for(int to=0;to<16&&!nonzero;++to)
   for(int from=0;from<16;++from)
    if(e0470_phase_action(vendor,f,to,from)){nonzero=1;break;}
  if(nonzero)break;
  ++hold;
 }
 assert(hold>0);
 const int drive=vendor->phases-hold;
 // 前段保留原表：等灰（除白）保持，其余迁移按原顺序。/ Keep the original front: equal non-white pairs hold, other transitions stay ordered.
 for(int f=0;f<drive;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to)
  assert(e0470_phase_action(actual,f,to,from)==(from==to&&to!=15?0:e0470_phase_action(vendor,f,to,from)));
 // 16相全屏擦白（含本来白的空白区）后接4相重推黑，最后是原定稿保持。
 // Sixteen whole-panel white phases (blanks included), four black re-drive phases, then the original settle holds.
 for(int f=drive;f<drive+16;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to)
  assert(e0470_phase_action(actual,f,to,from)==(to==15?2:0));
 for(int f=drive+16;f<drive+20;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to)
  assert(e0470_phase_action(actual,f,to,from)==(to==0?1:0));
 for(int f=drive+20;f<actual->phases;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to)
  assert(e0470_phase_action(actual,f,to,from)==0);
 puts("PASS: reader keeps every changed default-GL transition in order, adds sixteen whole-white phases and four black re-drive phases before the original holds, and retains full48 recovery");
 return 0;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
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
 assert(actual&&vendor&&actual->phases==E0470_GL16_FRAMES&&vendor->phases==E0470_GL16_FRAMES);
 assert(e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_GL16)->phases==48);
 int equal_black=0;
 for(int f=0;f<E0470_GL16_FRAMES;++f)for(int from=0;from<16;++from)for(int to=0;to<16;++to){
  int expected=from==to&&to!=15?0:e0470_phase_action(vendor,f,to,from);
#if E0470_WAVEFORM_TRIM
  // PR64在最后保持相之前重推黑；黑等值也按用户批准的作者实现驱动。
  // PR64 re-drives black before the trailing holds, including equal black as approved by the user.
  if(to==0&&f>=E0470_GL16_FRAMES-E0470_TRIM_HOLD-3&&f<E0470_GL16_FRAMES-E0470_TRIM_HOLD)expected=1;
#endif
  int action=e0470_phase_action(actual,f,to,from);assert(action==expected);
  if(to==0&&from==0)equal_black+=action==1;
 }
 assert(equal_black==(E0470_WAVEFORM_TRIM?3:0));
 puts("PASS: reader preserves changed default-GL transitions, PR64 black re-drive, equal-gray holds, white maintenance and full48 recovery");
}

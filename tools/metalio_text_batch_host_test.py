#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实普通翻页引擎/renderer入口/逐相LUT，硬件扫描替身验证提交、相序、像素和失败基准。
Run the real text engine/renderer entry/phase LUT; a scan sink checks batching, phases, pixels and failure history.
"""
from pathlib import Path
import re,subprocess,tempfile
from home_cover_cache_host_test import function as extract
root=Path(__file__).resolve().parents[1]
def function(name,path):
    with tempfile.TemporaryDirectory() as folder:
        p=Path(folder)/'source.c'
        s=re.sub(r'__attribute__\(\(optimize\("O3"\)\)\)', '', path.read_text())
        s=re.sub(r'\bIRAM_ATTR\b','',s)
        p.write_text('\n'.join(line.lstrip() for line in s.splitlines()))
        return extract(name,p)
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epdiy.h"
#include "e0470_page_turn.h"
#include "display_reader_waveform.h"
#include "metalio_text_ripple.h"
#include "e0470_epaper_waveform.h"
#include "output_common/lut.h"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define DEFAULT_FRAME_TIME 120
#define MONOCHROME_FRAME_TIME 120
#define NUM_RENDER_THREADS 2
enum{W=1216,H=684,BYTES=W*H/2};
static uint8_t front[BYTES],back[BYTES],saved[BYTES],diff[W*H],columns[W/2];
static bool rows[H];
static _Alignas(16) uint8_t lut[1024];
static uint8_t mask[W/4],ready[688],threads[688];
typedef struct{
 EpdRect area,crop_to;int waveform_range,waveform_index;enum EpdDrawMode mode;
 const EpdWaveform* waveform;enum EpdDrawError error;const bool* drawn_lines;const uint8_t* data_ptr;
 lut_build_func_t lut_build_func;lut_func_t lut_lookup_func;const uint8_t*const* phase_luts;
 const int8_t*line_phase;const int*col_band_x0,*col_band_x1;const int8_t*col_band_phase;int col_band_n;
 int lines_prepared,lines_consumed,lines_total,display_width,display_height,current_frame,cycle_frames;
 const int*phase_times;size_t conversion_lut_size;uint8_t*conversion_lut,*line_mask,*line_ready,*line_threads;
 int frame_time;
}RenderContext_t;
static RenderContext_t render_context={.conversion_lut_size=1024,.conversion_lut=lut,.line_mask=mask,
 .display_width=W,.display_height=H,.line_ready=ready,.line_threads=threads};
static const uint8_t*const*s_phase_luts;static const int8_t*s_line_phase;
static const int*s_col_band_x0,*s_col_band_x1;static const int8_t*s_col_band_phase;static int s_col_band_n;
static bool s_scan_active,s_leading_skip_enabled;static int s_last_leading_skip;static const uint8_t*s_present_for;
static enum EpdRotation rotation;static int calls,scans,fail_at,mask_calls,first_seen,auto_skip;
static unsigned phase_hits[64];static int64_t now;
static bool expect_staged;static const uint8_t*staged_test_luts[1];static int8_t staged_test_lines[H];
int water_test_alloc_fail=1;
int epd_width(void){return W;}int epd_height(void){return H;}
enum EpdRotation epd_get_rotation(void){return rotation;}
EpdRect epd_full_screen(void){return(EpdRect){0,0,W,H};}
void epd_poweron(void){}
int64_t esp_timer_get_time(void){return now;}
void esp_rom_delay_us(uint32_t us){now+=us;}
void epd_clear_phase_luts(void){s_phase_luts=NULL;s_line_phase=NULL;s_col_band_x0=s_col_band_x1=NULL;s_col_band_phase=NULL;s_col_band_n=0;}
void epd_set_line_phase_luts(const uint8_t*const*l,const int8_t*p){epd_clear_phase_luts();s_phase_luts=l;s_line_phase=p;}
void epd_set_col_phase_luts(const uint8_t*const*l,const int*a,const int*b,const int8_t*p,int n){epd_clear_phase_luts();s_phase_luts=l;s_col_band_x0=a;s_col_band_x1=b;s_col_band_phase=p;s_col_band_n=n;}
static int leading_skip_frames(const EpdWaveformPhases*p,const uint8_t*d,EpdRect a){(void)p;(void)d;(void)a;return auto_skip;}
static void lookup(const uint32_t*a,uint8_t*b,const uint8_t*c,uint32_t d){(void)a;(void)b;(void)c;(void)d;}
LutFunctionPair find_lut_functions(enum EpdDrawMode mode,uint32_t size){
 assert((mode==(MODE_PACKING_1PPB_DIFFERENCE|MODE_GL16)||mode==(MODE_PACKING_1PPB_DIFFERENCE|MODE_DU))&&size==1024);
 return(LutFunctionPair){epd_build_1ppB_lut_1k,lookup};
}
static void lcd_do_update(RenderContext_t*ctx);
'''
for name,path in [
 ('epd_build_1ppB_lut_1k','boards/metalio47/src/components/epdiy/src/output_common/lut.c'),
 ('epd_populate_line_mask','boards/metalio47/src/components/epdiy/src/output_common/render_context.c'),
 ('prepare_context_for_next_frame','boards/metalio47/src/components/epdiy/src/output_common/render_context.c'),
 ('waveform_temp_range_index','boards/metalio47/src/components/epdiy/src/render.c'),
 ('get_waveform_index','boards/metalio47/src/components/epdiy/src/render.c'),
 ('rounded_display_height','boards/metalio47/src/components/epdiy/src/render.c'),
 ('epd_draw_base_impl','boards/metalio47/src/components/epdiy/src/render.c'),
 ('epd_draw_base','boards/metalio47/src/components/epdiy/src/render.c'),
 ('epd_draw_base_from_phase','boards/metalio47/src/components/epdiy/src/render.c')]:
 unit+=function(name,root/path)+'\n'
unit+=r'''
static EpdRect physical_crop;
EpdRect epd_difference_image_cropped(const uint8_t*to,const uint8_t*from,EpdRect a,uint8_t*d,bool*r,uint8_t*c){
 assert(to==front&&from==back&&d==diff&&r==rows&&c==columns);physical_crop=a;
 memset(diff,0,sizeof(diff));
 for(int y=a.y;y<a.y+a.height;++y)for(int x=a.x;x<a.x+a.width;++x){
  size_t i=(size_t)y*W/2+x/2;int shift=4*(x&1);
  d[(size_t)y*W+x]=(uint8_t)((((to[i]>>shift)&15)<<4)|((from[i]>>shift)&15));
 }
 return a;
}
static void lcd_do_update(RenderContext_t*ctx){
 ++calls;assert(s_scan_active);
 if(expect_staged){
  assert(ctx->phase_luts==staged_test_luts&&ctx->line_phase==staged_test_lines);
  assert(ctx->current_frame==0&&ctx->cycle_frames==1&&!ctx->col_band_n);
  ++scans;return;
 }
 assert(!ctx->phase_luts&&!ctx->line_phase&&!ctx->col_band_n);
 assert(ctx->conversion_lut==lut&&ctx->data_ptr==diff&&ctx->area.width==W&&ctx->area.height==H);
 assert(ctx->drawn_lines==rows&&ctx->lines_total==688);
 const EpdWaveformPhases*p=ctx->waveform->mode_data[ctx->waveform_index]->range_data[ctx->waveform_range];
 first_seen=ctx->current_frame;assert(ctx->cycle_frames==p->phases-first_seen);
 for(int y=0;y<H;++y)assert(rows[y]==(y>=physical_crop.y&&y<physical_crop.y+physical_crop.height));
 for(int x=0;x<W/2;++x){
  unsigned expected=0;
  if(2*x>=physical_crop.x&&2*x<physical_crop.x+physical_crop.width)expected|=15;
  if(2*x+1>=physical_crop.x&&2*x+1<physical_crop.x+physical_crop.width)expected|=240;
  assert(columns[x]==expected);
 }
 for(int k=0;k<ctx->cycle_frames;++k){
  prepare_context_for_next_frame(ctx);int phase=ctx->current_frame;assert(phase==first_seen+k);
  for(int pair=0;pair<256;++pair)for(int lane=0;lane<4;++lane){
   unsigned action=e0470_phase_action(p,phase,pair>>4,pair&15);
   assert(lut[pair*4+lane]==(action<<(2*lane)));
  }
  ++phase_hits[phase];++scans;now+=15000;
  if(scans==fail_at){ctx->error=EPD_DRAW_EMPTY_LINE_QUEUE;break;}
  ++ctx->current_frame;
 }
}
static void reset(void){calls=scans=0;now=0;memset(phase_hits,0,sizeof(phase_hits));}
static EpdRect logical(void){return rotation&1?(EpdRect){0,0,H,W}:(EpdRect){0,0,W,H};}
static void check_crop(void){
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){
  bool in=x>=physical_crop.x&&x<physical_crop.x+physical_crop.width&&y>=physical_crop.y&&y<physical_crop.y+physical_crop.height;
  size_t i=(size_t)y*W/2+x/2;int shift=4*(x&1);
  assert(((back[i]>>shift)&15)==(((in?front[i]:saved[i])>>shift)&15));
 }
}
int main(void){
 EpdiyHighlevelState hl={.front_fb=front,.back_fb=back,.difference_fb=diff,.dirty_lines=rows,.dirty_columns=columns};
 e0470_waveform_init();const EpdWaveformPhases*p=e0470_waveform_phases(&E0470_TEXTTURN_WAVEFORM,MODE_GL16);
 uint8_t raw[E0470_GL16_FRAMES*64];memcpy(raw,p->luts,sizeof(raw));rotation=EPD_ROT_INVERTED_PORTRAIT;
 for(int from=0;from<16;++from)for(int to=0;to<16;++to){
  memset(front,to|(to<<4),BYTES);memset(back,from|(from<<4),BYTES);memcpy(saved,back,BYTES);reset();fail_at=0;
  int first=0;while(first<p->phases&&!e0470_phase_action(p,first,to,from))++first;
  int skip=first==p->phases?p->phases-1:first?first-1:0;
  assert(e0470_text_turn_with_waveform(&hl,logical(),E0470_TURN_RTL,&E0470_TEXTTURN_WAVEFORM)==EPD_DRAW_SUCCESS);
  assert(calls==1&&first_seen==skip&&scans==p->phases-skip&&!memcmp(front,back,BYTES)&&!s_scan_active);
  for(int phase=0;phase<p->phases;++phase){assert(phase_hits[phase]==(unsigned)(phase>=skip));if(phase<skip)assert(!e0470_phase_action(p,phase,to,from));}
 }
 for(int rot=0;rot<4;++rot){
  rotation=rot;
  for(int fail=1;fail<=p->phases;++fail){
   memset(front,0x57,BYTES);memset(back,0xff,BYTES);memcpy(saved,back,BYTES);reset();fail_at=fail;
   enum EpdDrawError err=e0470_text_turn_with_waveform(&hl,logical(),E0470_TURN_LTR,&E0470_TEXTTURN_WAVEFORM);
   if(fail<=scans)assert(err==EPD_DRAW_EMPTY_LINE_QUEUE&&!memcmp(back,saved,BYTES));else assert(err==EPD_DRAW_SUCCESS);
   assert(calls==1&&!s_scan_active);
  }
  for(int odd=0;odd<2;++odd){
   for(size_t i=0;i<BYTES;++i){front[i]=(uint8_t)(i*37+21);back[i]=(uint8_t)(i*19+3);}memcpy(saved,back,BYTES);reset();fail_at=0;
   EpdRect a={odd?3:32,odd?17:64,160,320};
   assert(e0470_text_turn_with_waveform(&hl,a,E0470_TURN_RTL,&E0470_TEXTTURN_WAVEFORM)==EPD_DRAW_SUCCESS);
   assert(calls==1);check_crop();
  }
 }
 // 用户要求普通也用水波纹22相；验证同一表同步输出、四级目标和所有尾相。
 // Ordinary shares the ripple22-phase table; verify synchronous output,four-level targets and every tail phase.
 const EpdWaveform*short_gray=metalio_text_ripple_waveform();
 const EpdWaveformPhases*short_phases=e0470_waveform_phases(short_gray,MODE_GL16);
 assert(short_gray&&short_phases->phases==22);
 // 未变化白底在真实转换表中必须全保持；旧字擦白仍保留完整18次。
 // The real conversion table must hold unchanged white, retaining all18 erase pulses for old black glyphs.
 const EpdWaveformPhases*native_du=e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_DU);
 for(int f=0;f<22;++f){
  assert(!e0470_phase_action(short_phases,f,15,15));
  if(f<20)assert(e0470_phase_action(short_phases,f,15,0)==e0470_phase_action(native_du,f,15,0));
  else assert(!e0470_phase_action(short_phases,f,15,0));
 }
 for(int from=0;from<16;++from)for(int to=0;to<16;++to){
  rotation=EPD_ROT_INVERTED_PORTRAIT;memset(front,to|(to<<4),BYTES);memset(back,from|(from<<4),BYTES);
  memcpy(saved,back,BYTES);reset();fail_at=0;
  assert(metalio_text_ripple_prepare(&hl,logical())&&!memcmp(back,saved,BYTES));
  int target=(to+2)/5*5;
  for(size_t i=0;i<BYTES;++i)assert(front[i]==(target|(target<<4)));
  int first=0;while(first<22&&!e0470_phase_action(short_phases,first,target,from))++first;
  int skip=first==22?21:first?first-1:0;
  assert(e0470_text_turn_with_waveform(&hl,logical(),E0470_TURN_RTL,short_gray)==EPD_DRAW_SUCCESS);
  assert(calls==1&&first_seen==skip&&scans==22-skip&&!memcmp(front,back,BYTES));
  for(int f=0;f<22;++f){assert(phase_hits[f]==(unsigned)(f>=skip));if(f<skip)assert(!e0470_phase_action(short_phases,f,target,from));}
 }
 for(int rot=0;rot<4;++rot){rotation=rot;for(int fail=1;fail<=22;++fail){
  memset(front,0,BYTES);memset(back,255,BYTES);memcpy(saved,back,BYTES);reset();fail_at=fail;
  assert(e0470_text_turn_with_waveform(&hl,logical(),E0470_TURN_RTL,short_gray)==EPD_DRAW_EMPTY_LINE_QUEUE);
  assert(calls==1&&scans==fail&&!memcmp(back,saved,BYTES));
 }}
 puts("PASS: ordinary shares actual ripple22-phase generator/quantizer,all256 target/history pairs,one batch,every active/tail phase,all22 failure ticks/four rotations retain history");
 // 原DU20完整相序加四相白，原尾部保持不丢；目标量化不改历史。/ Complete DU20 plus four whites, original tail holds and real history retained.
 const EpdWaveform*fast=display_reader_fast_text_waveform();
 const EpdWaveformPhases*du=e0470_waveform_phases(fast,MODE_GL16);
 assert(du&&du->phases==(KIIKO_S31_DU14?18:24));
 const int fast_frames=du->phases;
 for(int from=0;from<16;++from)for(int to=0;to<16;++to){
  rotation=EPD_ROT_INVERTED_PORTRAIT;memset(front,to|(to<<4),BYTES);memset(back,from|(from<<4),BYTES);
  memcpy(saved,back,BYTES);reset();fail_at=0;
  assert(display_reader_fast_text_prepare(&hl,logical())&&!memcmp(back,saved,BYTES));
  int target=to<8?0:15;for(size_t i=0;i<BYTES;++i)assert(front[i]==(target|(target<<4)));
  int first=0;while(first<fast_frames&&!e0470_phase_action(du,first,target,from))++first;
  int skip=first==fast_frames?fast_frames-1:first?first-1:0;
  assert(e0470_text_turn_with_waveform(&hl,logical(),E0470_TURN_RTL,fast)==EPD_DRAW_SUCCESS);
  assert(calls==1&&first_seen==skip&&scans==fast_frames-skip&&!memcmp(front,back,BYTES));
  for(int f=0;f<fast_frames;++f){assert(phase_hits[f]==(unsigned)(f>=skip));if(f<skip)assert(!e0470_phase_action(du,f,target,from));}
 }
 static uint8_t before_front[BYTES];
 for(int rot=0;rot<4;++rot){rotation=rot;
  EpdRect area={3,17,161,321};
  for(size_t i=0;i<BYTES;++i){front[i]=(uint8_t)(i*37+21);back[i]=(uint8_t)(i*19+3);}
  memcpy(before_front,front,BYTES);memcpy(saved,back,BYTES);
  assert(display_reader_fast_text_prepare(&hl,area)&&!memcmp(saved,back,BYTES));
  int px=0,py=0,pw=0,ph=0;
  switch(rotation){case EPD_ROT_LANDSCAPE:px=3;py=17;pw=161;ph=321;break;
   case EPD_ROT_PORTRAIT:px=W-17-321;py=3;pw=321;ph=161;break;
   case EPD_ROT_INVERTED_LANDSCAPE:px=W-3-161;py=H-17-321;pw=161;ph=321;break;
   default:px=17;py=H-3-161;pw=321;ph=161;break;}
  for(int y=0;y<H;++y)for(int x=0;x<W;++x){size_t i=(size_t)y*W/2+x/2;int shift=(x&1)*4;
   int old=(before_front[i]>>shift)&15;bool in=x>=px&&x<px+pw&&y>=py&&y<py+ph;
   assert(((front[i]>>shift)&15)==(in?(old<8?0:15):old));}
  for(int fail=1;fail<=fast_frames;++fail){
   memset(front,0,BYTES);memset(back,255,BYTES);memcpy(saved,back,BYTES);reset();fail_at=fail;
   assert(e0470_text_turn_with_waveform(&hl,logical(),E0470_TURN_RTL,fast)==EPD_DRAW_EMPTY_LINE_QUEUE);
   assert(calls==1&&scans==fail&&!memcmp(back,saved,BYTES));
  }
 }
 assert(!display_reader_fast_text_prepare(NULL,logical()));
 memcpy(saved,back,BYTES);memcpy(before_front,front,BYTES);
 assert(!display_reader_fast_text_prepare(&hl,(EpdRect){INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX}));
 assert(!memcmp(saved,back,BYTES)&&!memcmp(before_front,front,BYTES));
 puts("PASS: native DU20 plus four whites and retained settling,all256 target/history transitions,one batch,complete active/tail phases,four rotations/odd crops,every failed phase preserves history,no out-of-crop quantization");
 // 入场26相包括原来完全相同的中灰；灰色滑条统一白基线，区域外半字节不得驱动。
 // Entry26 includes identical midgrays, giving gray controls one white baseline without driving neighboring nibbles.
 const EpdWaveform *panel=display_reader_panel_gray4_waveform(true);assert(panel);
 for(int rot=0;rot<4;++rot){rotation=rot;
  EpdRect area={3,17,161,321};
  for(int to=5;to<=10;to+=5)for(int from=0;from<16;++from){
   memset(front,to*17,BYTES);memset(back,from*17,BYTES);memcpy(saved,back,BYTES);
   reset();fail_at=0;
   assert(e0470_text_turn_with_waveform(&hl,area,E0470_TURN_RTL,panel)==EPD_DRAW_SUCCESS);
   assert(calls==1&&first_seen==0&&scans==26);check_crop();
  }
  for(int fail=1;fail<=26;++fail){
   memset(front,0xaa,BYTES);memset(back,0xaa,BYTES);memcpy(saved,back,BYTES);
   reset();fail_at=fail;
   assert(e0470_text_turn_with_waveform(&hl,area,E0470_TURN_RTL,panel)==EPD_DRAW_EMPTY_LINE_QUEUE);
   assert(calls==1&&scans==fail&&!memcmp(back,saved,BYTES));
  }
 }
 puts("PASS: actual26-phase panel batch/all old grays into both midgrays including equal/white targets,one scan,four rotations/odd crop masks exact,all26 failed phases preserve back");
 reset();fail_at=0;int before=calls;
 assert(epd_draw_base_from_phase(epd_full_screen(),diff,epd_full_screen(),MODE_PACKING_1PPB_DIFFERENCE|MODE_GL16,25,rows,columns,&E0470_TEXTTURN_WAVEFORM,-1)==EPD_DRAW_INVALID_CROP);
 assert(epd_draw_base_from_phase(epd_full_screen(),diff,epd_full_screen(),MODE_PACKING_1PPB_DIFFERENCE|MODE_GL16,25,rows,columns,&E0470_TEXTTURN_WAVEFORM,p->phases)==EPD_DRAW_INVALID_CROP);
 assert(calls==before);
 s_leading_skip_enabled=true;auto_skip=2;
 assert(epd_draw_base(epd_full_screen(),diff,epd_full_screen(),MODE_PACKING_1PPB_DIFFERENCE|MODE_GL16,25,rows,columns,&E0470_TEXTTURN_WAVEFORM)==EPD_DRAW_SUCCESS&&first_seen==2);
 expect_staged=true;reset();s_leading_skip_enabled=false;
 staged_test_luts[0]=lut;epd_set_line_phase_luts(staged_test_luts,staged_test_lines);
 assert(epd_draw_base(epd_full_screen(),diff,epd_full_screen(),MODE_PACKING_1PPB_DIFFERENCE|MODE_DU,25,rows,columns,&E0470_APPLY_WAVEFORM)==EPD_DRAW_SUCCESS);
 assert(calls==1&&scans==1&&!s_phase_luts&&!render_context.phase_luts&&!s_scan_active);
 assert(!memcmp(raw,p->luts,sizeof(raw)));
 puts("PASS: real text engine + renderer + internal1KiB LUT: all256 transitions use one batch, every active/tail phase retained with one setup hold, four rotations/odd crops, every phase failure preserves back, no64KiB allocation, default entry and invalid-phase guards");
}
'''
with tempfile.TemporaryDirectory(prefix='s31-text-batch-') as folder:
 temp=Path(folder);c=temp/'test.c';c.write_text(unit);binary=temp/'test'
 (temp/'esp_heap_caps.h').write_text((root/'tools/water_turn_stubs/esp_heap_caps.h').read_text()+'\nstatic inline void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n); }\n')
 (temp/'esp_log.h').write_text('#pragma once\n#include <stdio.h>\n#undef ESP_LOGI\n#define ESP_LOGI(tag,fmt,...) do{char msg[256];(void)(tag);(void)snprintf(msg,sizeof(msg),fmt,__VA_ARGS__);}while(0)\n')
 command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unknown-attributes','-fsanitize=address,undefined','-DKIIKO_BOARD_METALIO47=1','-DKIIKO_S31_TEXT_RIPPLE=1','-DKIIKO_S31_FAST_READER_PANELS=1','-I'+str(temp),
 '-I'+str(root/'boards/metalio47/src/main'),'-I'+str(root/'main'),'-I'+str(root/'components/epdiy/src'),'-I'+str(root/'components/epdiy/include'),'-I'+str(root/'components/e0470_epaper_waveform/include'),'-I'+str(root/'components/e0470_epaper_waveform/metalio'),'-I'+str(root/'components/e0470_page_turn/include'),'-I'+str(root/'tools/water_turn_stubs'),'-I'+str(root/'tools/metalio_stubs'),
 str(c),str(root/'boards/metalio47/src/main/metalio_text_ripple.c'),str(root/'boards/metalio47/src/main/display_reader_waveform.c'),str(root/'boards/metalio47/src/main/display_main_waveform.c'),str(root/'boards/metalio47/src/components/e0470_page_turn/e0470_page_turn.c'),str(root/'boards/metalio47/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c'),str(root/'components/e0470_epaper_waveform/e0470_waveform_trim.c'),'-o',str(binary)]
 for du14 in (0,1):
  subprocess.run(command[:1]+[f'-DKIIKO_S31_DU14={du14}']+command[1:],check=True)
  subprocess.run([str(binary)],check=True,timeout=90)

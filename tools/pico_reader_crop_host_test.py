#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Exercise real Pico interlacing and highlevel masks/commits against exact pixel bounds."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
from test_epd_difference_bounds import PREFIX
ROOT = Path(__file__).resolve().parents[1]
render = ROOT/'boards/pico/src/components/epdiy/src/render.c'
highlevel = ROOT/'components/epdiy/src/highlevel.c'
text = render.read_text()
start = text.index('__attribute__((optimize("O3"))) static inline int _interlace_line_unaligned(')
end = text.index('\nEpdRect epd_difference_image(\n', start)
unit = PREFIX + text[start:end] + r'''
#define W 1216
#define H 684
#define B (W*H/2)
#define ESP_LOGI(...) ((void)0)
#define _swap_int(a,b) do{int saved=(a);(a)=(b);(b)=saved;}while(0)
enum EpdRotation{EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
enum EpdDrawMode{MODE_DU=1,MODE_GL16=5,MODE_PACKING_1PPB_DIFFERENCE=0x100};
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_EMPTY_LINE_QUEUE};
typedef struct{int unused;}EpdWaveform;
typedef struct{uint8_t *front_fb,*back_fb,*difference_fb;bool *dirty_lines;uint8_t *dirty_columns;const EpdWaveform *waveform;}EpdiyHighlevelState;
static _Alignas(16) uint8_t front[B],back[B],diff[W*H],cols[W/2],old[B];
static bool rows[H],full,fail,same;
static int rotation,scans,checks;
static EpdRect expected;
static int epd_height(void){return H;}
static enum EpdRotation epd_get_rotation(void){return rotation;}
static EpdRect epd_full_screen(void){return(EpdRect){0,0,W,H};}
static int64_t esp_timer_get_time(void){return 0;}
static void hl_record_timing(int a,int b,int c){(void)a;(void)b;(void)c;}
static void epd_leading_skip_discard(void){}
static EpdRect epd_difference_image_cropped(const uint8_t*t,const uint8_t*f,EpdRect a,uint8_t*d,bool*r,uint8_t*c){return epd_difference_image_base(t,f,a,W,H,d,r,c);}
static unsigned pixel(const uint8_t*p,int x,int y){return(p[y*W/2+x/2]>>(4*(x&1)))&15;}
static bool inside(int x,int y){return x>=expected.x&&x<expected.x+expected.width&&y>=expected.y&&y<expected.y+expected.height;}
static enum EpdDrawError epd_draw_base(EpdRect a,const uint8_t*d,EpdRect crop,enum EpdDrawMode m,int t,const bool*r,const uint8_t*c,const EpdWaveform*w){
 (void)a;(void)crop;(void)m;(void)t;(void)w;
 assert(!memcmp(back,old,B));++scans;
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){
  bool driven=r[y]&&((c[x/2]>>(4*(x&1)))&15);
  // Gray-to-gray GL16 can drive even unchanged pixels: check the mask, not only framebuffer values.
  assert(!driven||inside(x,y));
  if(full&&inside(x,y))assert(driven);
  if(driven)assert(d[y*W+x]==((pixel(front,x,y)<<4)|pixel(old,x,y)));
 }
 return fail?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS;
}
'''
unit += function('_inverse_rotated_area', highlevel)+'\n'+function('hl_update_area', highlevel)
unit += r'''
static void check(EpdRect logical){
 // Independent rotation oracle; no use of the production transform for expected bounds.
 switch(rotation){
 case 0:expected=logical;break;
 case 1:expected=(EpdRect){W-logical.y-logical.height,logical.x,logical.height,logical.width};break;
 case 2:expected=(EpdRect){W-logical.x-logical.width,H-logical.y-logical.height,logical.width,logical.height};break;
 default:expected=(EpdRect){logical.y,H-logical.x-logical.width,logical.height,logical.width};break;
 }
 memset(back,0x99,B);memset(front,0x99,B);
 // Pending changes inside and outside the crop must not be committed together.
 if(!same)for(int y=0;y<H;++y)for(int x=0;x<W;x+=7)front[y*W/2+x/2]^=(uint8_t)(15u<<(4*(x&1)));
 if(!same)front[expected.y*W/2+expected.x/2]^=(uint8_t)(7u<<(4*(expected.x&1)));
 memcpy(old,back,B);scans=0;
 EpdiyHighlevelState h={front,back,diff,rows,cols,NULL};
 assert(hl_update_area(&h,MODE_GL16,25,logical,full)==(fail?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS));
 assert(scans==1);
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){
  assert(pixel(back,x,y)==(!fail&&inside(x,y)?pixel(front,x,y):pixel(old,x,y)));
 }
 ++checks;
}
int main(void){
 for(rotation=0;rotation<4;++rotation)for(int f=0;f<2;++f)for(int failure=0;failure<2;++failure){
  full=f;fail=failure;
  if(rotation&1){
   check((EpdRect){0,0,H,1204}); // Immersive body: gray progress at1207 must stay untouched.
   check((EpdRect){0,80,H,1088});check((EpdRect){0,176,H,872});
   check((EpdRect){0,1204,H,12}); // Subsequent idle footer owns the deferred pixels.
  }
  check((EpdRect){11,13,61,35});check((EpdRect){32,32,64,64});
  for(int x=0;x<4;++x)for(int n=1;n<=3;++n)check((EpdRect){x,9,n,3});
 }
 full=true;fail=false;same=true;
 for(rotation=1;rotation<4;rotation+=2)check((EpdRect){0,0,H,1204});
 // No-op/empty updates must not submit a scan or alter either framebuffer.
 EpdiyHighlevelState h={front,back,diff,rows,cols,NULL};
 rotation=0;scans=0;
 assert(hl_update_area(&h,MODE_GL16,25,(EpdRect){0,0,W,H},false)==EPD_DRAW_SUCCESS);
 assert(hl_update_area(&h,MODE_GL16,25,(EpdRect){0,0,0,3},true)==EPD_DRAW_SUCCESS);
 assert(!scans&&!memcmp(front,back,B));
 puts("PASS: real Pico diff/highlevel exact scan masks and success-only commits; immersive/footer/standard crops,4 rotations,odd nibbles,narrow/aligned areas and failures");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-reader-crop-') as temp:
    folder=Path(temp);(folder/'esp_attr.h').write_text('#define IRAM_ATTR\n')
    c=folder/'test.c';c.write_text(unit);exe=folder/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unknown-attributes',
                    '-Wno-pointer-to-int-cast','-fsanitize=address,undefined','-I'+temp,str(c),
                    str(ROOT/'components/epdiy/src/riscv_pixels.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

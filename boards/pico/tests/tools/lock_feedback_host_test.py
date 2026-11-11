#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：验证实际清白、圆锁顺序和后缓冲历史；四旋转边界、失败及忙时不误提交。
# English: Actual white/badge ordering and real history: rotation bounds, failures and busy guards.
from pathlib import Path
import subprocess
import tempfile
import sys
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_lock_feedback.h"
#include "ui_kit.h"
typedef struct {uint8_t *back_fb;} EpdiyHighlevelState;
#define MODE_GC16 2
#define READ_PICO_TRANSFER_UPLOADING 3
typedef struct {int state;} read_pico_transfer_status_t;
static uint8_t frame[684u*1216u/2u], original[sizeof(frame)], back[sizeof(frame)];
static unsigned style=2, pushes, white_pushes, cancels, touches;
static int rotation, transfer, result, white_result;
static bool online, missing, small, s_lock_feedback_visible, s_lock_feedback_failed, fallback_frame;
#define EPD_DRAW_SUCCESS 0
static const int E0470_WAVEFORM=1;
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
enum EpdRotation epd_get_rotation(void){return EPD_ROT_LANDSCAPE;}
int epd_rotated_display_width(void){return small?70:rotation&1?684:1216;}
int epd_rotated_display_height(void){return small?70:rotation&1?1216:684;}
static size_t offset(int x,int y){
 int px=x,py=y;
 if(rotation==1){px=1215-y;py=x;}
 else if(rotation==2){px=1215-x;py=683-y;}
 else if(rotation==3){px=y;py=683-x;}
 return (size_t)py*1216+px;
}
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *fb){
 assert(x>=0&&x<epd_rotated_display_width()&&y>=0&&y<epd_rotated_display_height());
 size_t at=offset(x,y);unsigned shift=(at&1)*4;
 fb[at/2]=(fb[at/2]&~(15u<<shift))|((c>>4)<<shift);++touches;
}
static unsigned pixel(const uint8_t *fb,int x,int y){size_t at=offset(x,y);return(fb[at/2]>>((at&1)*4))&15;}
void epd_fill_circle(int cx,int cy,int radius,uint8_t gray,uint8_t *fb){
 for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)
  if(x*x+y*y<=radius*radius)epd_draw_pixel(cx+x,cy+y,gray,fb);
}
uint8_t epd_get_pixel(int x,int y,int width,int height,const uint8_t *fb){
 (void)width;(void)height;return (uint8_t)(pixel(fb,x,y)<<4);
}
int app_settings_system_contrast(void){return 100;}
static void verify_badge(void){
 int cx=(epd_rotated_display_width()-97)/2+48,cy=(epd_rotated_display_height()-97)/2+48;
 int ring=0,glyph=0,mid=0,hole=0;
 for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x){
  int dx=x-cx,dy=y-cy,d=dx*dx+dy*dy,v=(int)pixel(frame,x,y);
  if(d>52*52)assert((unsigned)v==(fallback_frame?15u:pixel(original,x,y)));
  else if(d>=44*44&&d<=50*50){if(!v)++ring;}
  else if(d<40*40){if(!v)++glyph;else if(v<15)++mid;else ++hole;}
 }
 assert(ring>300&&glyph>200&&mid>100&&hole>1500);
}
static unsigned app_settings_lock_style(void){return style;}
static bool pico_online_busy(void){return online;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *s){s->state=transfer;}
static uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState *h){assert(h);return missing?NULL:frame;}
static void display_main_transition_cancel(void){++cancels;}
static int update_display_area_full_with(EpdiyHighlevelState *h,const int *w,int mode,EpdRect area){
 assert(h&&w==&E0470_WAVEFORM&&mode==MODE_GC16&&!white_pushes&&!pushes&&cancels==1);
 assert(area.width==97&&area.height==97&&area.x==(epd_rotated_display_width()-97)/2&&area.y==(epd_rotated_display_height()-97)/2);
 assert(!memcmp(back,original,sizeof(frame)));
 for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x)
  if(x<area.x||x>=area.x+area.width||y<area.y||y>=area.y+area.height)assert(pixel(frame,x,y)==pixel(original,x,y));
 ++pushes;if(!result)memcpy(back,frame,sizeof(frame));return result;
}
'''
for name in ('ui_read_pixel','icon_nibble','ui_draw_icon','ui_contrast_gray'):
    unit += function(name,ROOT/'main/ui/ui_kit.c')+'\n'
unit += function('epd_draw_circle',ROOT/'components/epdiy/src/epdiy.c')+'\n'
unit += function('app_lock_feedback', ROOT/'main/sleep.c')
unit += r'''
static void reset(void){memset(frame,0x57,sizeof(frame));memcpy(original,frame,sizeof(frame));memcpy(back,original,sizeof(frame));pushes=white_pushes=cancels=touches=0;style=2;online=missing=small=s_lock_feedback_visible=s_lock_feedback_failed=fallback_frame=false;transfer=result=white_result=0;}
int main(int argc,char **argv){
 EpdiyHighlevelState hl={back};
 for(rotation=0;rotation<4;++rotation){
  reset();memset(frame,0xa5,sizeof(frame));app_lock_feedback(&hl);assert(pushes==1&&!white_pushes&&cancels==1&&touches>0&&s_lock_feedback_visible&&!memcmp(frame,back,sizeof(frame)));
  int cx=(epd_rotated_display_width()-97)/2+48,cy=(epd_rotated_display_height()-97)/2+48;
  verify_badge();
  assert(pixel(frame,cx,cy-30)==15&&pixel(frame,cx+47,cy)==0);

  if(rotation==1&&argc>1){FILE *f=fopen(argv[1],"wb");assert(f);for(int y=0;y<1216;++y)for(int x=0;x<684;++x)assert(fputc((int)pixel(frame,x,y)*17,f)!=EOF);assert(!fclose(f));}
 }
 for(unsigned s=0;s<3;++s){reset();style=s;app_lock_feedback(&hl);assert(pushes==(s==2)&&!white_pushes&&cancels==(s==2));}
 reset();online=true;app_lock_feedback(&hl);assert(!pushes&&!white_pushes&&!touches&&!memcmp(frame,original,sizeof(frame)));
 reset();transfer=READ_PICO_TRANSFER_UPLOADING;app_lock_feedback(&hl);assert(!pushes&&!white_pushes&&!touches&&!memcmp(frame,original,sizeof(frame)));
 reset();missing=true;app_lock_feedback(&hl);assert(!pushes&&!white_pushes&&!touches);
 reset();app_lock_feedback(NULL);assert(!pushes&&!white_pushes&&!touches);
 reset();result=1;app_lock_feedback(&hl);assert(!white_pushes&&pushes==1&&cancels==1&&!s_lock_feedback_visible&&s_lock_feedback_failed&&!memcmp(back,original,sizeof(frame)));
 reset();small=true;app_lock_feedback(&hl);assert(!pushes&&!white_pushes&&!touches&&!cancels);
 reset();online=true;s_lock_feedback_visible=true;app_lock_feedback(&hl);assert(!s_lock_feedback_visible&&!white_pushes);
 for(rotation=0;rotation<4;++rotation){
  reset();fallback_frame=true;ui_lock_fallback_draw(frame);
  verify_badge();assert(!white_pushes&&!pushes);
 }
 ui_lock_fallback_draw(NULL);
 puts("PASS: one local GC16 badge preserves all outside pixels and true history without a full-screen white pass in four rotations; real back history; white/icon faults never commit readiness; other-style/OTA/upload/missing-frame/undersize guards");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kiikoread-lock-feedback-') as folder:
    work = Path(folder)
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-I'+str(ROOT/'tools/ui_gesture_stubs'),'-I'+str(ROOT/'main/ui'),
                    str(work/'test.c'),str(ROOT/'main/ui/ui_lock_feedback.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')] + sys.argv[1:],check=True)

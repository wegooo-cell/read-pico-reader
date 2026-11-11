#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：真实首页封面固定框、灰阶/1bit裁剪、极小源图和脏区边界。
# English: Real fixed Home frame, gray/BW crops, tiny sources and dirty-region bounds.
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
r=Path(__file__).resolve().parents[1]
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{unsigned x,y,width,height;}book_crop_t;
typedef struct{uint8_t*cover;unsigned cover_width,cover_height;bool cover_fast;}home_book_t;
static EpdRect recorded;
static unsigned writes;
static uint8_t ui_contrast_gray(uint8_t g){return g;}
static uint8_t ui_image_dither_gray(uint8_t g,int x,int y){(void)x;(void)y;return g;}
static void epd_draw_pixel(int x,int y,uint8_t g,uint8_t*fb){assert(x>=60&&x<236&&y>=274&&y<518);fb[y*684+x]=g;++writes;}
static void home_frame(uint8_t*f,EpdRect r,int radius){(void)f;assert(radius==12);recorded=r;}
static void ui_fill_round_rect(uint8_t*f,EpdRect r,int radius,int gray){(void)f;(void)r;assert(radius==12&&gray==0xe0);}
'''
u+=function('book_cover_crop',r/'main/book/book_cover.c')+'\n'+function('draw_cover',r/'main/apps/app_dashboard.c')+'\n'
u+=r'''
int main(void){
 uint8_t*fb=malloc(684*1216);assert(fb);
 const unsigned sizes[][2]={{1,1},{6,9},{80,120},{320,80},{64,512},{176,240},{8,240},{1200,1800}};
 for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);++i)for(int fast=0;fast<2;++fast)for(int press=0;press<2;++press){
  unsigned w=sizes[i][0],h=sizes[i][1],stride=fast?(w+7)/8:w;uint8_t*data=malloc(stride*h);assert(data);
  for(unsigned k=0;k<stride*h;++k)data[k]=(uint8_t)(k*29u+53u);
  home_book_t b={data,w,h,fast};EpdRect frame={60,press?274:278,176,240};
  memset(fb,173,684*1216);writes=0;draw_cover(fb,&b,frame);
  assert(!memcmp(&recorded,&frame,sizeof(frame))&&writes>40000);
  for(int y=0;y<1216;++y)for(int x=0;x<684;++x)if(x<frame.x||x>=frame.x+frame.width||y<frame.y||y>=frame.y+frame.height)assert(fb[y*684+x]==173);
  book_crop_t crop=book_cover_crop(w,h,176,240);assert(crop.width&&crop.height&&crop.x+crop.width<=w&&crop.y+crop.height<=h);
  for(unsigned y=12;y<228;y+=23)for(unsigned x=12;x<164;x+=19){
   unsigned sx=crop.x+(uint64_t)x*crop.width/176,sy=crop.y+(uint64_t)y*crop.height/240;
   uint8_t expected=fast?(data[sy*stride+sx/8]&(1u<<(sx&7))?255:0):data[sy*w+sx];
   assert(fb[(frame.y+y)*684+frame.x+x]==expected);
  }
  free(data);
 }
 home_book_t empty={0};EpdRect target={60,278,176,240};draw_cover(fb,&empty,target);assert(!memcmp(&recorded,&target,sizeof(target)));
 free(fb);puts("PASS: fixed176x240 resume frame for tiny/wide/tall images, proportional centre crops, gray/1bit strides, press lift, no-cover placeholder and unchanged outside pixels");
}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d)/'test.c';p.write_text(u);out=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)

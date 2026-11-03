#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：插图缓存失效、旋转和非整字节铺帧回归。/ English: Image-cache loss, rotation and partial-byte blit regression."""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
book=R/'main/apps/app_book.c'
unit=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint8_t *gray;int width,height;} reader_image_t;
enum {READING=1,READER_PANEL_NONE=0,EPD_ROT_LANDSCAPE=0,EPD_ROT_PORTRAIT=1,
EPD_ROT_INVERTED_LANDSCAPE=2,EPD_ROT_INVERTED_PORTRAIT=3};
static int s_view=READING,s_reader_panel,s_page_image_count,rotation,slots=1;
static bool s_toolbar,hidden;static const char *s_text="x";
static size_t s_page=2,s_page_images_for=2;
static uint32_t s_page_images_generation=3,generation=3;
static reader_image_t *s_page_images;static bool s_zoom_active;
static int s_zoom_slot;
static bool app_settings_reader_hide_images(void){return hidden;}
static uint32_t book_layout_generation(void){return generation;}
static int book_layout_page_image(size_t page){(void)page;return slots==1?0:-1;}
static int reader_image_slots(size_t page){(void)page;return slots;}
static int epd_width(void){return 24;}static int epd_height(void){return 16;}
static int epd_get_rotation(void){return rotation;}
static EpdRect book_layout_image_rect(void){return(EpdRect){0,0,24,16};}
static int reader_image_top(void){return 0;}
static EpdRect reader_area(void){return book_layout_image_rect();}
static bool reader_image_slot(size_t p,int s,int*i,int*y,int*w,int*h){
 (void)p;(void)i;if(s>=slots)return false;*y=0;*w=24;*h=16;return true;}
static const uint8_t s_bayer4[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
#define ZOOM_BITS_STEP 1
'''
for name in ('reader_sqrt','reader_image_cache_ready','reader_single_image_page',
             'reader_zoom_image','reader_zoom_mode','reader_inline_image_at','reader_page_image_hit',
             'reader_image_clamp_center','reader_sample_bilinear','reader_logical_to_phys',
             'reader_phys_to_logical','zoom_bits_build','zoom_blit_bits'):
    unit+=function(name,book)+'\n'
unit+=r'''
static int pixel(uint8_t *fb,int x,int y){return(fb[y*12+x/2]>>((x&1)*4))&15;}
int main(void){
 int slot=-1;
 assert(!reader_inline_image_at(4,4,&slot));assert(!reader_zoom_mode());
 uint8_t gray[16]={0,255,0,255,255,0,255,0,0,255,0,255,255,0,255,0};
 reader_image_t image={gray,4,4};s_page_images=&image;s_page_image_count=1;
 assert(reader_inline_image_at(11,1,&slot)&&slot==0);assert(reader_single_image_page());
 assert(!reader_inline_image_at(11,1,NULL));
 for(int bad=0;bad<8;++bad){
  s_view=READING;s_toolbar=hidden=false;s_page_images_for=2;s_page_images_generation=3;
  s_page_image_count=1;image.gray=gray;image.width=image.height=4;s_reader_panel=0;
  switch(bad){case 0:s_view=0;break;case 1:s_toolbar=true;break;case 2:hidden=true;break;
  case 3:s_page_images_for=1;break;case 4:s_page_images_generation=2;break;
  case 5:s_page_image_count=0;break;case 6:image.gray=NULL;break;case 7:image.width=0;break;}
  assert(!reader_inline_image_at(11,1,&slot));assert(!reader_zoom_mode());
 }
 // A panel blocks input, but keeps the decoded zoom target available for unchanged underlay painting.
 image.width=image.height=4;image.gray=gray;s_toolbar=true;s_reader_panel=1;
 assert(reader_image_cache_ready()&&reader_zoom_image()==&image&&!reader_zoom_mode());
 s_reader_panel=0;
 image=(reader_image_t){gray,4,4};s_page_images_for=2;s_page_images_generation=3;
 s_view=READING;s_toolbar=hidden=false;s_page_image_count=1;
 slots=20;assert(!reader_inline_image_at(0,15,&slot)); // 布局槽多于实际缓存。/ Layout outnumbers decoded slots.
 slots=1;
 assert(reader_sample_bilinear(&image,0,0)==0);
 assert(reader_sample_bilinear(&image,1<<16,0)==255);
 assert(reader_sample_bilinear(&image,1<<15,0)==127);
 for(uint32_t n=0;n<50000;++n){uint32_t q=reader_sqrt(n);assert(q*q<=n&&(q+1)*(q+1)>n);}
 uint8_t framebuffer[24*16/2],expected[sizeof(framebuffer)];
 for(int x=-2;x<24;x++)for(int w=1;w<12;w++){
  EpdRect p={x,3,w,3};uint8_t bits[12];memset(bits,0xa6,sizeof(bits));
  memset(framebuffer,0x55,sizeof(framebuffer));memcpy(expected,framebuffer,sizeof(expected));
  zoom_blit_bits(framebuffer,bits,p);
  for(int y=0;y<16;y++)for(int xx=0;xx<24;xx++){
   int v=5;if(y>=3&&y<6&&xx>=x&&xx<x+w){int k=xx-x;
    v=bits[(y-3)*((w+7)/8)+k/8]&(0x80>>(k&7))?0:15;}
   assert(pixel(framebuffer,xx,y)==v);
  }
 }
 for(rotation=0;rotation<4;rotation++){
  EpdRect band=(rotation&1)?(EpdRect){1,2,11,19}:(EpdRect){2,1,19,11};
  int x0,y0,x1,y1;reader_logical_to_phys(band.x,band.y,&x0,&y0);
  reader_logical_to_phys(band.x+band.width-1,band.y+band.height-1,&x1,&y1);
  EpdRect p={x0<x1?x0:x1,y0<y1?y0:y1,abs(x1-x0)+1,abs(y1-y0)+1};
  uint8_t bits[128];memset(bits,0,sizeof(bits));
  zoom_bits_build(bits,p,band,4,4,band.width,band.height,band.x,band.y);
  for(int y=0;y<p.height;y++)for(int x=0;x<p.width;x++){
   int lx,ly;reader_phys_to_logical(p.x+x,p.y+y,&lx,&ly);
   int sx=((int64_t)(lx-band.x)*((4<<16)/band.width))>>16;
   int sy=((int64_t)(ly-band.y)*((4<<16)/band.height))>>16;
   bool black=gray[sy*4+sx]<s_bayer4[(((p.y+y)&3)<<2)|((p.x+x)&3)]*16+8;
   assert(!!(bits[y*((p.width+7)/8)+x/8]&(0x80>>(x&7)))==black);
  }
 }
 puts("PASS: null/stale/short image caches, OOM fallback guards, bilinear samples, four rotations and clipped/odd-width packed blits");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
                    '-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

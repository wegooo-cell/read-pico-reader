#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实渐进JPEG、硬解替身、缩放与失败清理；不声称硬件性能。
English: Real progressive JPEG, hardware fixtures, resampling and failure cleanup; no hardware performance claims.
"""
from pathlib import Path
import argparse, subprocess, tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--jpeg-source',type=Path,required=True);p.add_argument('--jpeg-build',type=Path,required=True)
a=p.parse_args()
unit=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
static FILE *backings[3];static unsigned live_backings,peak_backings;static bool backing_fail,write_fail;
static FILE *test_tmpfile(void){if(backing_fail)return NULL;FILE*f=tmpfile();if(f){assert(live_backings<3);backings[live_backings++]=f;if(live_backings>peak_backings)peak_backings=live_backings;}return f;}
static int test_fclose(FILE*f){for(unsigned i=0;i<live_backings;++i)if(backings[i]==f){backings[i]=backings[--live_backings];break;}return fclose(f);}
static size_t test_fwrite(const void*p,size_t n,size_t count,FILE*f){return write_fail?0:fwrite(p,n,count,f);}
#define tmpfile test_tmpfile
#define fclose test_fclose
#define fwrite test_fwrite
#include "metalio_jpeg.c"
'''+function('book_cover_crop',R/'main/book/book_cover.c')+r'''
static uint8_t *load(const char *p,size_t *n){FILE *f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);uint8_t *b=malloc(*n);assert(fread(b,1,*n,f)==*n);fclose(f);return b;}
static void reference(const uint8_t *data,size_t size,unsigned dw,unsigned dh,uint8_t *out){
 struct jpeg_decompress_struct j={0};struct jpeg_error_mgr err;j.err=jpeg_std_error(&err);jpeg_create_decompress(&j);
 jpeg_mem_src(&j,data,size);assert(jpeg_read_header(&j,TRUE)==JPEG_HEADER_OK);
 unsigned scale=1;for(unsigned n=2;n<=8;n*=2){book_crop_t c=book_cover_crop((j.image_width+n-1)/n,(j.image_height+n-1)/n,dw,dh);if(c.width<dw||c.height<dh)break;scale=n;}
 j.scale_num=1;j.scale_denom=scale;j.out_color_space=JCS_GRAYSCALE;assert(jpeg_start_decompress(&j));
 uint8_t *plane=malloc((size_t)j.output_width*j.output_height);assert(plane);
 while(j.output_scanline<j.output_height){JSAMPROW row=plane+(size_t)j.output_scanline*j.output_width;assert(jpeg_read_scanlines(&j,&row,1)==1);}
 assert(jpeg_finish_decompress(&j));assert(jpeg_scale(plane,j.output_width,j.output_height,j.output_width,dw,dh,out));
 free(plane);jpeg_destroy_decompress(&j);
}
int main(int argc,char **argv){
 assert(argc==8);unsigned w,h,pw,ph;bool gray;
 uint8_t a[64],b[64],canary[64];memset(canary,0x91,sizeof(canary));
 size_t n;uint8_t *jpg=load(argv[1],&n);
 assert(jpeg_hw_shape(jpg,n,&w,&h,&pw,&ph,&gray)&&w==34&&h==28&&pw==48&&ph==32&&!gray);
 assert(metalio_jpeg_gray(jpg,n,NULL,8,8,a)&&!test_engines&&test_hw_calls==1);
 // 每个像素与相同路径的文件解码一致，MCU填充不成为图像的一部分。
 // File and memory routes match; MCU padding must never leak into the visible image.
 FILE *f=fopen(argv[1],"rb");assert(metalio_jpeg_gray(NULL,n,f,8,8,b));assert(!memcmp(a,b,64));fclose(f);
 f=fopen(argv[6],"rb");fseek(f,0,SEEK_END);size_t exif=ftell(f);rewind(f);
 assert(metalio_jpeg_gray(NULL,exif,f,8,8,b)&&!memcmp(a,b,64));fclose(f);
 // 纯红/蓝检查不能只测中性灰；彩色权重不能反转。
 // Red/blue checks expose swapped color channels that neutral gray cannot detect.
 free(jpg);jpg=load(argv[2],&n);assert(metalio_jpeg_gray(jpg,n,NULL,8,8,a));
 for(int i=0;i<64;i++)assert(a[i]>70&&a[i]<82);
 test_hw_error=-1;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary)&&!test_engines);test_hw_error=0;
 test_alloc_fail=test_allocs+1;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary)&&!test_engines);test_alloc_fail=0;
 test_alloc_fail=test_allocs+2;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary)&&!test_engines);test_alloc_fail=0;
 test_free=JPEG_RESERVE;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary));test_free=32u*1024u*1024u;
 free(jpg);jpg=load(argv[3],&n);assert(metalio_jpeg_gray(jpg,n,NULL,8,8,a));
 for(int i=0;i<64;i++)assert(a[i]>22&&a[i]<36);
 free(jpg);jpg=load(argv[4],&n);assert(jpeg_hw_shape(jpg,n,&w,&h,&pw,&ph,&gray)&&gray&&pw==40&&ph==32);
 assert(metalio_jpeg_gray(jpg,n,NULL,8,8,a));free(jpg);
 jpg=load(argv[5],&n);assert(!jpeg_hw_shape(jpg,n,&w,&h,&pw,&ph,&gray));
 assert(metalio_jpeg_gray(jpg,n,NULL,8,8,a));
 unsigned min=255,max=0;for(int i=0;i<64;i++){if(a[i]<min)min=a[i];if(a[i]>max)max=a[i];}assert(max-min>150);
 f=fopen(argv[5],"rb");assert(metalio_jpeg_gray(NULL,n,f,8,8,b)&&!memcmp(a,b,64));fclose(f);
 test_time_step=3000000;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary));test_time_step=0;
 assert(!metalio_jpeg_gray(jpg,n/3,NULL,8,8,canary));
 test_free=JPEG_RESERVE;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary));test_free=32u*1024u*1024u;free(jpg);
 for(int i=0;i<64;i++)assert(canary[i]==0x91);
 // 面积缩小保留平均灰度；常量放大和居中裁切不读边界外。
 // Area reduction preserves average gray; enlargement/cropping never reads outside the source.
 uint8_t checker[256];for(int y=0;y<16;y++)for(int x=0;x<16;x++)checker[y*16+x]=(x+y)%2?255:0;
 assert(jpeg_scale(checker,16,16,16,8,8,a));for(int i=0;i<64;i++)assert(a[i]==128);
 memset(checker,37,256);assert(jpeg_scale(checker,2,2,16,8,8,a));for(int i=0;i<64;i++)assert(a[i]==37);
 assert(!jpeg_scale(checker,1,1,0,8,8,a));assert(!jpeg_hw_shape((uint8_t*)"bad",3,&w,&h,&pw,&ph,&gray));
 assert(!metalio_jpeg_gray(NULL,20,NULL,8,8,a));assert(!test_engines);
 jpg=load(argv[7],&n);assert(n>0);
 unsigned dims[][2]={{23,31},{176,240},{684,1216},{1216,684}};
 for(unsigned mode=0;mode<4;++mode){unsigned dw=dims[mode][0],dh=dims[mode][1];size_t bytes=(size_t)dw*dh;
  uint8_t *actual=malloc(bytes),*expected=malloc(bytes);assert(actual&&expected);
  reference(jpg,n,dw,dh,expected);peak_backings=0;
  assert(metalio_jpeg_gray(jpg,n,NULL,dw,dh,actual)&&!memcmp(actual,expected,bytes));
  assert(peak_backings==3&&!live_backings);
  f=fopen(argv[7],"rb");assert(metalio_jpeg_gray(NULL,n,f,dw,dh,actual)&&!memcmp(actual,expected,bytes));fclose(f);
  assert(!live_backings);free(actual);free(expected);
 }
 backing_fail=true;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary)&&!live_backings&&!metalio_jpeg_fallback_allowed());backing_fail=false;
 write_fail=true;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary)&&!live_backings);write_fail=false;
 test_time_step=1000;test_cancel_after_yields=1;assert(!metalio_jpeg_gray(jpg,n,NULL,8,8,canary));
 assert(metalio_jpeg_cancelled()&&!metalio_jpeg_fallback_allowed()&&!live_backings);test_time_step=0;test_cancel_after_yields=0;
 for(int i=0;i<64;i++)assert(canary[i]==0x91);
 assert(metalio_jpeg_gray(jpg,n,NULL,8,8,a)&&!metalio_jpeg_cancelled()&&!live_backings);free(jpg);
 puts("PASS: real bounded progressive bands/SD backing, pixel-identical full-plane reference at4 target sizes, cooperative cancellation/retry, backing/write failure cleanup; full-progressive file/memory decode; padded RGB/gray, red/blue order, area resampling, corruption/OOM/timeout fallback and resource cleanup (hardware mocked)");
}
'''
with tempfile.TemporaryDirectory() as d:
    d=Path(d)
    generator=r"""
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "jpeglib.h"
static void fixture(const char *dir,const char *name,unsigned w,unsigned h,int mode,int progressive){
 char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,name);FILE*f=fopen(path,"wb");if(!f)exit(1);
 struct jpeg_compress_struct j;struct jpeg_error_mgr error;j.err=jpeg_std_error(&error);jpeg_create_compress(&j);jpeg_stdio_dest(&j,f);
 j.image_width=w;j.image_height=h;j.input_components=mode==3||mode==4?1:3;j.in_color_space=j.input_components==1?JCS_GRAYSCALE:JCS_RGB;
 jpeg_set_defaults(&j);jpeg_set_quality(&j,mode==0?95:mode==5?90:100,TRUE);if(progressive)jpeg_simple_progression(&j);jpeg_start_compress(&j,TRUE);
 unsigned char *pixels=malloc(w*3);if(!pixels)exit(2);
 while(j.next_scanline<h){unsigned y=j.next_scanline;for(unsigned x=0;x<w;++x){
  if(mode==3)pixels[x]=128;
  else if(mode==4)pixels[x]=(x+y)%2?255:0;
  else{pixels[3*x]=mode==1?255:mode==2?0:mode==5?(x*13+y*3)%256:x*7;
   pixels[3*x+1]=mode==1||mode==2?0:mode==5?(x*5+y*17)%256:y*9;
   pixels[3*x+2]=mode==2?255:mode==1?0:mode==5?(x*7+y*11)%256:(x+y)*4;}
 }JSAMPROW row=pixels;jpeg_write_scanlines(&j,&row,1);}
 jpeg_finish_compress(&j);jpeg_destroy_compress(&j);fclose(f);free(pixels);
}
int main(int argc,char**argv){if(argc!=2)return 3;
 fixture(argv[1],"padded.jpg",34,28,0,0);fixture(argv[1],"red.jpg",32,32,1,0);
 fixture(argv[1],"blue.jpg",32,32,2,0);fixture(argv[1],"gray.jpg",34,28,3,0);
 fixture(argv[1],"progressive.jpg",8,8,4,1);fixture(argv[1],"large-progressive.jpg",1920,1080,5,1);return 0;}
"""
    (d/'generate.c').write_text(generator)
    subprocess.run(['cc','-std=c11','-I'+str(a.jpeg_source),'-I'+str(a.jpeg_build),str(d/'generate.c'),str(a.jpeg_build/'libjpeg.a'),'-o',str(d/'generate')],check=True)
    subprocess.run([str(d/'generate'),str(d)],check=True)
    raw=(d/'padded.jpg').read_bytes();(d/'exif.jpg').write_bytes(raw[:2]+b'\xff\xe1\x13\x8a'+b'X'*5000+raw[2:])
    (d/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
        '-I'+str(R/'tools/metalio_jpeg_stubs'),'-I'+str(R/'main/book'),'-I'+str(a.jpeg_source),'-I'+str(a.jpeg_build),
        str(d/'test.c'),str(a.jpeg_build/'libjpeg.a'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test'),*[str(d/n) for n in ('padded.jpg','red.jpg','blue.jpg','gray.jpg','progressive.jpg','exif.jpg','large-progressive.jpg')]],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：编译真实PNG行解码与透明书封，验证任意当前底图、alpha、黑色主体和原画布比例。
# English: Compile the real PNG row decoder and cover compositor; verify arbitrary current backgrounds, alpha, black subjects and canvas fit.
from pathlib import Path
import os,re,struct,subprocess,tempfile,zlib
from home_cover_cache_host_test import function
r=Path(__file__).resolve().parents[1]
board=os.environ.get("KIIKO_TEST_BOARD","pico")
assert board in ("pico","metalio47")
c=r/'main/book/book_cover.c'
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#include "png.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BYTES (684u*1216u/2u)
#define BOOK_SEAL_CACHE_PARENT CACHE_ROOT
#define BOOK_IMAGE_FILE_MAX (50u*1024u*1024u)
typedef struct {int x,y,width,height;}EpdRect;
enum {EPD_ROT_PORTRAIT=1,EPD_ROT_INVERTED_PORTRAIT=2,EPD_ROT_INVERTED_LANDSCAPE=3};
static bool fat_replace=true;static unsigned fail_publish;
static int fat_rename(const char *src,const char *dst){
 struct stat exists;bool occupied=!stat(dst,&exists);
 if(fat_replace&&occupied){errno=EEXIST;return -1;}
 if(fail_publish&&strstr(src,".packed")&&!occupied){--fail_publish;errno=EIO;return -1;}
 return rename(src,dst);
}
#define rename fat_rename
static unsigned budget=8u*1024u*1024u;
static size_t used,peak;
static size_t fail_alloc_above;
static size_t read_bytes,read_calls,fail_read_at;
static size_t measured_read(void *p,size_t bytes,size_t n,FILE *f){
 ++read_calls;if(fail_read_at&&read_calls==fail_read_at)return 0;
 size_t count=fread(p,bytes,n,f);read_bytes+=bytes*count;return count;
}
#define fread measured_read
static struct {void *p;size_t n;} allocs[64];
static void test_free(void *p){for(unsigned i=0;i<64;++i)if(allocs[i].p==p&&p){used-=allocs[i].n;allocs[i].p=NULL;break;}free(p);}
#define free test_free
static size_t heap_caps_get_free_size(unsigned flags){(void)flags;return budget>used?budget-used:0;}
static void *heap_caps_malloc(size_t n,unsigned flags){(void)flags;if(used+n>budget||(fail_alloc_above&&n>=fail_alloc_above))return NULL;
 void *p=malloc(n);if(!p)return NULL;
 for(unsigned i=0;i<64;++i)if(!allocs[i].p){allocs[i].p=p;allocs[i].n=n;used+=n;if(used>peak)peak=used;return p;}
 abort();}
static int rotation;
static int epd_get_rotation(void){return rotation;}
static int epd_width(void){return rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_INVERTED_PORTRAIT?1216:684;}
static int epd_height(void){return rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_INVERTED_PORTRAIT?684:1216;}
static uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){assert(x>=0&&y>=0&&x<w&&y<h);int i=y*w+x;return ((fb[i/2]>>(i%2?4:0))&15)<<4;}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){
 assert(x>=0&&y>=0&&x<684&&y<1216);int px=x,py=y;
 if(rotation==EPD_ROT_PORTRAIT){px=epd_width()-y-1;py=x;}
 else if(rotation==EPD_ROT_INVERTED_PORTRAIT){px=y;py=epd_height()-x-1;}
 else if(rotation==EPD_ROT_INVERTED_LANDSCAPE){px=epd_width()-x-1;py=epd_height()-y-1;}
 int i=py*epd_width()+px;fb[i/2]=(fb[i/2]& (i%2?15:240))|((gray>>4)<<(i%2?4:0));
}

static void *cover_alloc(size_t n){return heap_caps_malloc(n,0);}
static void decode_yield(unsigned at){(void)at;}
typedef struct{unsigned x,y,width,height;}book_crop_t;
'''
for n in ('luminance','scale_axis'):u+=function(n,c)+'\n'
u+='typedef struct{uint16_t at;uint8_t frac;}scale_step_t;\n'
for n in ('scale_map','scale_next','bilinear4','book_cover_crop'):u+=function(n,c)+'\n'
u+='typedef struct{const uint8_t *data;size_t size,pos;FILE *file;}png_input_t;\n'
for n in ('png_read_mem','png_gray_rows','png_gray_input','book_png_file_layer','book_png_file_layer_rows'):u+=function(n,c)+'\n'
u+=r'''
static bool book_image_file_dimensions(const char *path,bool png,unsigned *w,unsigned *h){
 (void)png;uint8_t b[24];FILE *f=fopen(path,"rb");if(!f)return false;bool ok=fread(b,1,24,f)==24&&!memcmp(b,"\211PNG\r\n\032\n",8);fclose(f);
 *w=(uint32_t)b[16]<<24|(uint32_t)b[17]<<16|(uint32_t)b[18]<<8|b[19];*h=(uint32_t)b[20]<<24|(uint32_t)b[21]<<16|(uint32_t)b[22]<<8|b[23];return ok;
}
'''
source=(r/'main/book/book_lock_bookmark.c').read_text()
source=re.sub(r'^#include[^\n]*\n','',source,flags=re.M)
source=source.replace('#define ROOT "/sdcard/.read-pico-cache"','#define ROOT CACHE_ROOT')
u+=function("ui_image_dither_gray",r/"main/ui/ui_image_dither.c")+"\n"
u+=source
u+=r'''

static void check_white_regions(void){
 uint8_t gray[31*19],before[31*19],alpha[31*19],reach[31*19];unsigned queue[31*19],seed=47;
 // 和独立像素BFS对照，覆盖分岔、窄缝、封闭白岛及单行单列。
 // Compare against an independent pixel BFS across branches, narrow passages, enclosed islands and one-row/column inputs.
 for(unsigned trial=0;trial<400;++trial){
  unsigned w=trial%31+1,h=trial%19+1,n=w*h,head=0,tail=0;
  memset(alpha,255,n);memset(reach,0,n);
  for(unsigned at=0;at<n;++at){seed=seed*1664525u+1013904223u;gray[at]=(seed>>29)<5?255:(seed&1?249:80);}
  memcpy(before,gray,n);
  for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned at=y*w+x;if((!x||!y||x+1==w||y+1==h)&&gray[at]>=250&&!reach[at]){reach[at]=1;queue[tail++]=at;}}
  while(head<tail){unsigned at=queue[head++],x=at%w,y=at/w,adj[4]={x?at-1:at,x+1<w?at+1:at,y?at-w:at,y+1<h?at+w:at};for(unsigned d=0;d<4;++d){unsigned next=adj[d];if(!reach[next]&&gray[next]>=250){reach[next]=1;queue[tail++]=next;}}}
  assert(remove_white_background(gray,alpha,w,h));
  for(unsigned at=0;at<n;++at){assert(alpha[at]==(reach[at]?0:255));assert(gray[at]==(reach[at]?0:before[at]));}
 }
 // 真实透明度不作颜色键，保留白色主体；内存不足不修改输入。
 // Existing alpha bypasses color-keying and preserves white art; allocation failure leaves inputs untouched.
 memset(gray,255,sizeof(gray));memset(alpha,255,sizeof(alpha));alpha[100]=128;
 assert(remove_white_background(gray,alpha,31,19)&&alpha[0]==255&&alpha[100]==128&&gray[0]==255);
 alpha[100]=255;budget=0;assert(!remove_white_background(gray,alpha,31,19));budget=8u*1024u*1024u;
 for(unsigned at=0;at<sizeof(gray);++at)assert(gray[at]==255&&alpha[at]==255);
 uint8_t *large=malloc(2*684u*1216u);assert(large);memset(large,255,2*684u*1216u);
 assert(remove_white_background(large,large+684u*1216u,684,1216));
 for(unsigned at=0;at<2*684u*1216u;++at)assert(!large[at]);free(large);
}
static void check_pack_rows(void){
 uint8_t row[684*2],decoded[684*2];unsigned seed=41;
 for(unsigned w=1;w<=684;++w)for(unsigned pattern=0;pattern<4;++pattern){
  for(unsigned x=0;x<w*2;++x){seed=seed*1664525u+1013904223u;row[x]=pattern==0?0:pattern==1?(x&1?255:0):pattern==2?x/17:(seed>>24);}
  FILE *f=tmpfile();assert(f&&seal_write_row(f,row,w)&&!fseek(f,0,SEEK_SET));
  assert(seal_read_row(f,decoded,w)&&!memcmp(row,decoded,w*2)&&fgetc(f)==EOF);fclose(f);
 }
 const uint8_t corrupt[][8]={{255,255},{1,0,255},{3,0,255,1,2},{2,0,0,1},{4,0,1,2,3,4}};
 for(unsigned i=0;i<sizeof(corrupt)/sizeof(corrupt[0]);++i){FILE *f=tmpfile();assert(f&&fwrite(corrupt[i],1,8,f)==8&&!fseek(f,0,SEEK_SET));assert(!seal_read_row(f,decoded,4));fclose(f);}
}
int main(int argc,char **argv){
 assert(argc>=4);check_white_regions();check_pack_rows();
 uint8_t *fb=malloc(BYTES),*original=malloc(BYTES);assert(fb&&original);memset(original,0xaa,BYTES);
 memcpy(fb,original,BYTES);
 assert(book_lock_bookmark_draw(fb,argv[2]));
 // 整张正方形PNG上下留白，黑色保留不透明；透明区保留原文字底图。
 // A square PNG has vertical canvas margins; black stays opaque, transparency preserves reading pixels.
 assert(pixel(fb,300,100)==170&&pixel(fb,300,1150)==170);
 assert(pixel(fb,80,350)==170);assert(pixel(fb,520,350)==0);
 assert(pixel(fb,80,800)>=187&&pixel(fb,80,800)<=221);assert(pixel(fb,520,800)==255);
 if(argc>=6){
  FILE *bg=fopen(argv[4],"rb");assert(bg&&fread(original,1,BYTES,bg)==BYTES);fclose(bg);
  memcpy(fb,original,BYTES);
  assert(book_lock_bookmark_draw(fb,argv[5]));
  FILE *out=fopen("preview.pgm","wb");assert(out);fprintf(out,"P5\n684 1216\n255\n");
  for(int y=0;y<1216;++y)for(int x=0;x<684;++x)fputc(pixel(fb,x,y),out);fclose(out);
  unsigned sw,sh;assert(book_image_file_dimensions(argv[5],true,&sw,&sh));
  unsigned w=684,h=(uint64_t)sh*w/sw;if(h>1216){h=1216;w=(uint64_t)sw*h/sh;}
  size_t count=(size_t)w*h;uint8_t *layers=malloc(count*2),*white=malloc(BYTES);assert(layers&&white);
  assert(book_png_file_layer(argv[5],w,h,layers,layers+count));
  out=fopen("sample-alpha.pgm","wb");assert(out);fprintf(out,"P5\n%u %u\n255\n",w,h);
  assert(fwrite(layers+count,1,count,out)==count);fclose(out);
  memset(fb,255,BYTES);assert(book_lock_bookmark_draw(fb,argv[5]));memcpy(white,fb,BYTES);
  memset(fb,0,BYTES);assert(book_lock_bookmark_draw(fb,argv[5]));
  unsigned near_opaque=0,background_dependent=0;int ox=(684-(int)w)/2,oy=(1216-(int)h)/2;
  for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
   size_t at=(size_t)y*w+x;unsigned a=layers[count+at];
   if(a>=250){++near_opaque;bool different=pixel(fb,ox+x,oy+y)!=pixel(white,ox+x,oy+y);background_dependent+=different;}
  }
  memcpy(fb,original,BYTES);assert(book_lock_bookmark_draw(fb,argv[5]));memcpy(white,fb,BYTES);
  remove(SEAL_CACHE);budget=256*1024;memcpy(fb,original,BYTES);
  assert(book_lock_bookmark_draw(fb,argv[5])&&!memcmp(fb,white,BYTES));
  budget=0;memcpy(fb,original,BYTES);assert(book_lock_bookmark_draw(fb,argv[5])&&!memcmp(fb,white,BYTES));
  budget=8u*1024u*1024u;
#if KIIKO_BOARD_METALIO47
  assert(!background_dependent);
#endif
  printf("SAMPLE: near-opaque=%u background-dependent=%u; memory/stream/warm-cache pixels identical\n",near_opaque,background_dependent);
  free(layers);free(white);
 }
 // 近满值alpha在S31不应渗入底图；半透明和透明角仍保留真实合成。
 // Near-full S31 alpha must reject the backdrop; genuinely translucent/clear corners retain compositing.
 char near[512];snprintf(near,sizeof(near),"%s/near.png",argv[1]);
 memset(fb,0,BYTES);assert(book_lock_bookmark_draw(fb,near));
 memcpy(original,fb,BYTES);unsigned half=pixel(fb,80,800);
 assert(pixel(fb,80,350)==0&&half>=119&&half<=136);
 memset(fb,255,BYTES);assert(book_lock_bookmark_draw(fb,near));
 assert(pixel(fb,80,350)==255&&pixel(fb,80,800)==255);
 unsigned leaked=0;
 for(unsigned y=300;y<440;++y)for(unsigned x=400;x<600;++x)leaked+=pixel(fb,x,y)!=pixel(original,x,y);
 for(unsigned y=700;y<850;++y)for(unsigned x=400;x<600;++x)leaked+=pixel(fb,x,y)!=pixel(original,x,y);
#if KIIKO_BOARD_METALIO47
 assert(!leaked);
#else
 assert(leaked>0);
#endif
 memcpy(original,fb,BYTES);remove(SEAL_CACHE);fail_publish=1;
 memset(fb,255,BYTES);assert(book_lock_bookmark_draw(fb,near)&&!memcmp(fb,original,BYTES));
 assert(!fail_publish);fail_publish=0;
 // 首页、设置、阅读等任意页面直接作为底图；没有书籍文件或阅读记录前提。
 // Any Home/settings/reader frame works without a book file or reading-history prerequisite.
 for(unsigned shade=0;shade<16;++shade){
  memset(fb,shade*17u,BYTES);assert(book_lock_bookmark_draw(fb,argv[2]));
  assert(pixel(fb,300,100)==shade*17u&&pixel(fb,80,350)==shade*17u&&pixel(fb,520,350)==0&&pixel(fb,520,800)==255);
 }
 // 不透明白底PNG：边缘显示当前页面，黑色外框及内部白色图案保持不透明。
 // Opaque white-background PNG: reveal the current page at the edges, retaining the black border and enclosed white artwork.
 memset(fb,0xaa,BYTES);assert(book_lock_bookmark_draw(fb,argv[3]));
 assert(pixel(fb,10,300)==170&&pixel(fb,100,400)==0&&pixel(fb,342,600)==255);
 // 高内存首建与低内存行首建生成完全相同的压缩缓存，前者不创建未压缩临时文件。
 // High-memory and bounded-row cold builds produce identical packed bytes; the high-memory path creates no raw intermediate.
 char portrait[512];snprintf(portrait,sizeof(portrait),"%s/portrait.png",argv[1]);
 const char *fixtures[]={argv[2],argv[3],portrait,near};
 for(unsigned i=0;i<4;++i){
  remove(SEAL_CACHE);budget=8u*1024u*1024u;assert(book_lock_bookmark_draw(fb,fixtures[i])&&used==0);
  struct stat st;assert(stat(SEAL_TEMP,&st)&&!stat(SEAL_CACHE,&st));
  FILE *f=fopen(SEAL_CACHE,"rb");assert(f);size_t packed=(size_t)st.st_size;uint8_t *a=malloc(packed),*b=malloc(packed);assert(a&&b&&fread(a,1,packed,f)==packed);fclose(f);
  remove(SEAL_CACHE);budget=256*1024;assert(book_lock_bookmark_draw(fb,fixtures[i])&&used==0);
  f=fopen(SEAL_CACHE,"rb");assert(f&&fread(b,1,packed,f)==packed&&fgetc(f)==EOF&&!memcmp(a,b,packed));fclose(f);free(a);free(b);
 }
 // 冷缓存只允许256KiB工作区；热缓存零图像堆分配，底图逐次变化。
 // Cold-cache composition fits a 256KiB external workspace; a warm cache needs no image heap.
 remove(SEAL_CACHE);peak=0;budget=256*1024;memset(fb,0xbb,BYTES);
 assert(book_lock_bookmark_draw(fb,portrait)&&used==0&&peak<256*1024);
 budget=0;assert(book_lock_bookmark_draw(fb,portrait)&&used==0);
 remove(SEAL_CACHE);peak=0;budget=256*1024;memset(fb,0xaa,BYTES);
 assert(book_lock_bookmark_draw(fb,argv[3])&&used==0&&peak<256*1024);
 assert(pixel(fb,10,300)==170&&pixel(fb,342,600)==255);
 budget=0;memset(fb,0xbb,BYTES);assert(book_lock_bookmark_draw(fb,argv[3])&&used==0);
 assert(pixel(fb,10,300)==187&&pixel(fb,342,600)==255);
 // SEA1旧格式在动底图前失效；低内存不破坏当前页，有内存时重建SEA2。
 // Reject an old raw-layer format before painting; retain the current page on OOM and rebuild when memory returns.
 FILE *legacy=fopen(SEAL_CACHE,"rb+");seal_header_t legacy_header;
 assert(legacy&&fread(&legacy_header,1,sizeof(legacy_header),legacy)==sizeof(legacy_header));
 legacy_header.magic=UINT32_C(0x53454131);assert(!fseek(legacy,0,SEEK_SET)&&fwrite(&legacy_header,1,sizeof(legacy_header),legacy)==sizeof(legacy_header));fclose(legacy);
 memcpy(original,fb,BYTES);assert(!book_lock_bookmark_draw(fb,argv[3])&&!memcmp(fb,original,BYTES));
 budget=256*1024;assert(book_lock_bookmark_draw(fb,argv[3])&&used==0);
 legacy=fopen(SEAL_CACHE,"rb");assert(legacy&&fread(&legacy_header,1,sizeof(legacy_header),legacy)==sizeof(legacy_header)&&legacy_header.magic==UINT32_C(0x53454132));fclose(legacy);budget=0;
 // 截断和校验损坏必须在修改底图之前被拒绝；有内存时重建。
 // Reject truncated/corrupt cache before touching the current background; rebuild when memory permits.
 FILE *cached=fopen(SEAL_CACHE,"rb+");assert(cached);assert(!fseek(cached,sizeof(seal_header_t)+10,SEEK_SET));fputc(255,cached);fclose(cached);
 memcpy(original,fb,BYTES);assert(!book_lock_bookmark_draw(fb,argv[3])&&!memcmp(fb,original,BYTES));
 budget=256*1024;assert(book_lock_bookmark_draw(fb,argv[3])&&used==0);
 cached=fopen(SEAL_CACHE,"wb");assert(cached);fclose(cached);budget=0;
 memcpy(original,fb,BYTES);assert(!book_lock_bookmark_draw(fb,argv[3])&&!memcmp(fb,original,BYTES));
 budget=256*1024;assert(book_lock_bookmark_draw(fb,argv[3])&&used==0);
 memcpy(original,fb,BYTES);budget=0;
 assert(!book_lock_bookmark_draw(fb,argv[2])&&!memcmp(fb,original,BYTES));budget=8u*1024u*1024u;
 assert(!book_lock_bookmark_draw(fb,"file.jpg")&&!memcmp(fb,original,BYTES));
 char invalid[512];snprintf(invalid,sizeof(invalid),"%s/broken.png",argv[1]);
 FILE *bad=fopen(invalid,"wb"),*good=fopen(argv[2],"rb");assert(bad&&good);
 uint8_t header[24];assert(fread(header,1,sizeof(header),good)==sizeof(header));fclose(good);
 assert(fwrite(header,1,sizeof(header),bad)==sizeof(header));fclose(bad);
 assert(!book_lock_bookmark_draw(fb,invalid)&&!memcmp(fb,original,BYTES));
 // 同一缓存与同一底图对照原低内存双遍，包含四旋转、非均匀背景和底部阴影裁剪。
 // Compare the same cache/background against the original low-memory two-pass path across rotations, patterned pages and clipped shadows.
 uint8_t *expected=malloc(BYTES);assert(expected);
 for(unsigned fixture=0;fixture<3;++fixture){
  char odd[512];snprintf(odd,sizeof(odd),"%s/odd.png",argv[1]);
  const char *path=fixture==0?argv[2]:fixture==1?portrait:odd;
  budget=256*1024;assert(book_lock_bookmark_draw(fb,path));
  for(rotation=0;rotation<4;++rotation)for(unsigned seed=0;seed<4;++seed){
   for(unsigned at=0;at<BYTES;++at)original[at]=(uint8_t)(at*37u+seed*51u);
   memcpy(fb,original,BYTES);budget=0;read_bytes=read_calls=0;
   assert(book_lock_bookmark_draw(fb,path));memcpy(expected,fb,BYTES);size_t old_bytes=read_bytes,old_calls=read_calls;
   memcpy(fb,original,BYTES);budget=256*1024;read_bytes=read_calls=0;
   assert(book_lock_bookmark_draw(fb,path)&&!memcmp(fb,expected,BYTES)&&used==0);
   assert(read_bytes<old_bytes&&read_calls<old_calls);
#if !KIIKO_BOARD_METALIO47
   size_t row_bytes=read_bytes,row_calls=read_calls;
   memcpy(fb,original,BYTES);budget=8u*1024u*1024u;read_bytes=read_calls=0;peak=0;
   assert(book_lock_bookmark_draw(fb,path)&&!memcmp(fb,expected,BYTES)&&used==0);
   assert(read_bytes<row_bytes&&read_calls<row_calls&&peak<=684u*1216u*2u);
   if(fixture==0&&rotation==0&&seed==0)
    printf("Validated memory layer: fread calls %zu -> %zu, bytes %zu -> %zu; identical output\n",row_calls,read_calls,row_bytes,read_bytes);
   // Fragmentation/allocation failure must retain the original row path and exact output.
   memcpy(fb,original,BYTES);fail_alloc_above=512u*1024u;read_bytes=read_calls=0;
   assert(book_lock_bookmark_draw(fb,path)&&!memcmp(fb,expected,BYTES)&&used==0);
   assert(read_bytes==row_bytes&&read_calls==row_calls);fail_alloc_above=0;
#endif
   if(fixture==0&&rotation==0&&seed==0){
    struct stat packed;assert(!stat(SEAL_CACHE,&packed));
    size_t raw_size=sizeof(seal_header_t)+684u*684u*2u;
    assert((size_t)packed.st_size<raw_size/3);
    printf("Compressed seal cache: %lld bytes vs raw %zu; fused composition SD read bytes %zu vs low-memory %zu\n",(long long)packed.st_size,raw_size,read_bytes,old_bytes);
   }
  }
 }
 rotation=0;budget=256*1024;assert(book_lock_bookmark_draw(fb,argv[2]));
 seal_header_t probe;FILE *pf=fopen(SEAL_CACHE,"rb");assert(pf&&fread(&probe,1,sizeof(probe),pf)==sizeof(probe));fclose(pf);
#if !KIIKO_BOARD_METALIO47
 // Validate fully before exposing decoded bytes; failed reads/checksum free temporary memory.
 budget=8u*1024u*1024u;uint8_t *layer=NULL;
 read_calls=0;pf=seal_open(&probe,&layer);assert(pf&&layer&&used==(size_t)probe.width*probe.height*2);fclose(pf);free(layer);
 assert(!used);read_calls=0;fail_read_at=5;layer=(uint8_t*)1;
 assert(!seal_open(&probe,&layer)&&!layer&&!used);fail_read_at=0;
 seal_header_t wrong=probe;wrong.key++;assert(!seal_open(&wrong,&layer)&&!layer&&!used);
 FILE *corrupt_file=fopen(SEAL_CACHE,"rb+");assert(corrupt_file);seal_header_t corrupt_header=probe;corrupt_header.checksum^=1;
 assert(fwrite(&corrupt_header,1,sizeof(corrupt_header),corrupt_file)==sizeof(corrupt_header));fclose(corrupt_file);
 assert(!seal_open(&probe,&layer)&&!layer&&!used);
 corrupt_file=fopen(SEAL_CACHE,"rb+");assert(corrupt_file&&fwrite(&probe,1,sizeof(probe),corrupt_file)==sizeof(probe));fclose(corrupt_file);
 budget=256*1024;
#endif
 read_calls=0;
#if KIIKO_BOARD_METALIO47
 pf=seal_open(&probe);
#else
 pf=seal_open(&probe,NULL);
#endif
 assert(pf);fclose(pf);size_t verified_reads=read_calls;
 memcpy(fb,original,BYTES);read_calls=read_bytes=0;fail_read_at=verified_reads+1+35;peak=0;
 assert(!book_lock_bookmark_draw(fb,argv[2])&&used==0&&peak<=684u*2u*23u);
 fail_read_at=0;
 // 模拟FAT禁止覆盖并注入最终发布失败：回滚旧层，下次锁屏仍可直接复用。
 // Simulate FAT no-replace semantics plus a final publish failure; roll back the valid layer for the next lock.
 budget=256*1024;assert(book_lock_bookmark_draw(fb,argv[2]));
 memcpy(fb,original,BYTES);fail_publish=1;
 assert(!book_lock_bookmark_draw(fb,portrait)&&!memcmp(fb,original,BYTES)&&used==0);
 budget=0;assert(book_lock_bookmark_draw(fb,argv[2]));
 budget=256*1024;assert(book_lock_bookmark_draw(fb,portrait));
 budget=0;assert(book_lock_bookmark_draw(fb,portrait));
 free(expected);
 puts("PASS: fused shadow/art is byte-identical to two passes for48 rotated/patterned cases including odd width; one compressed layer pass saved and fewer SD reads; exact row codec and FAT rollback verified; mid-draw failure does not re-overlay or retain memory");
 free(fb);free(original);
 puts("PASS: real PNG alpha/premultiplication, opaque black, aspect-fit canvas, all 16 current-page backgrounds, 400 flood/BFS comparisons, white-edge removal with enclosed whites and original alpha preserved, low-memory fallback and no internal-RAM allocation");
}
'''
def chunk(k,b):return struct.pack('>I',len(b))+k+b+struct.pack('>I',zlib.crc32(k+b))
raw=b''.join(b'\0'+b''.join(bytes((255,255,255,0)) if x<16 and y<16 else bytes((0,0,0,255)) if y<16 else bytes((255,255,255,128)) if x<16 else bytes((255,255,255,255)) for x in range(32)) for y in range(32))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',32,32,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(u);(d/'fixture.png').write_bytes(png)
 white_raw=b''.join(b'\0'+b''.join(bytes((0,0,0)) if 3<=x<=28 and 3<=y<=28 and not (9<=x<=22 and 9<=y<=22) else bytes((255,255,255)) for x in range(32)) for y in range(32))
 (d/'white.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',32,32,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(white_raw))+chunk(b'IEND',b''))
 portrait=b''.join(b'\0'+b''.join(bytes((0,0,0)) if 5<=x<=26 and 5<=y<=51 else bytes((255,255,255)) for x in range(32)) for y in range(57))
 (d/'portrait.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',32,57,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(portrait))+chunk(b'IEND',b''))
 odd=b''.join(b'\0'+b''.join(bytes(((x*17)%256,(y*19)%256,((x+y)*31)%256,(x*13+y*23)%256)) for x in range(31)) for y in range(57))
 (d/'odd.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',31,57,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(odd))+chunk(b'IEND',b''))
 near=b''.join(b'\0'+b''.join(bytes((255,255,255,0)) if x<16 and y<16 else bytes((96,96,96,252)) if y<16 else bytes((255,255,255,128)) if x<16 else bytes((192,192,192,250)) for x in range(32)) for y in range(32))
 (d/'near.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',32,32,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(near))+chunk(b'IEND',b''))
 vendor=r/'managed_components/espressif__libpng/libpng'
 if not vendor.exists():vendor=Path(os.environ.get('KIIKO_LIBPNG_PATH','/private/tmp/kiikoread-rc90-lock-build-20261011/managed_components/espressif__libpng/libpng'))
 (d/'pnglibconf.h').write_bytes((vendor/'scripts/pnglibconf.h.prebuilt').read_bytes())
 names='png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwrite pngwio pngwtran pngwutil'.split()
 subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-DKIIKO_BOARD_METALIO47='+str(int(board=='metalio47')),'-DPNG_ARM_NEON_OPT=0','-DPNG_INTEL_SSE_OPT=0','-DCACHE_ROOT="'+str(d/'cache')+'"','-I'+str(d),'-I'+str(vendor),str(d/'test.c')]+[str(vendor/(n+'.c')) for n in names]+['-lz','-lm','-o',str(d/'test')],check=True)
 args=[str(d/'test'),str(d),str(d/'fixture.png'),str(d/'white.png')]
 if os.environ.get('BOOKMARK_SAMPLE') and os.environ.get('BOOKMARK_BACKGROUND'):args += [os.environ['BOOKMARK_BACKGROUND'],os.environ['BOOKMARK_SAMPLE']]
 subprocess.run(args,check=True)

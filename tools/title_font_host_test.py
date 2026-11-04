#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：标题使用真实活动TTF，检查原字号、文件句柄、缓存复用和字库资产不变。
# English: Exercise real active TTF titles, original sizes, file handles, cache reuse and unchanged font assets.
import os
import struct
import subprocess
import tempfile
from pathlib import Path
from font_hanzi_host_support import font_hanzi_host_sources
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
font = (ROOT / 'main/assets/builtin.ttf').read_bytes()
# 宿主绝对路径不受工程嵌套深度影响，避免超过设备路径字段后误判字库切换。
# Keep host fixture paths independent of checkout nesting and within device path storage.
with tempfile.TemporaryDirectory(prefix='kiiko-title-') as folder:
    work = Path(folder)
    (work / 'system.ttf').write_bytes(font)
    # 同一轮廓、不同字宽的字体可验证切页后确实采用新字体，而不是缓存上一份。
    # Equal outlines with distinct advances verify the new face is used after switching, not stale cached metrics.
    reader = bytearray(font)
    for i in range(struct.unpack_from('>H', font, 4)[0]):
        at = 12 + 16 * i
        if font[at:at + 4] == b'hmtx':
            offset, size = struct.unpack_from('>II', font, at + 8)
            for p in range(offset, offset + size - 3, 4):
                advance = struct.unpack_from('>H', reader, p)[0]
                struct.pack_into('>H', reader, p, advance + advance // 4)
    (work / 'reader.ttf').write_bytes(reader)
    (work / 'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_INVALID_RESPONSE 0x108
''')
    (work / 'esp_log.h').write_text('''#pragma once
static inline void title_test_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGI(...) title_test_log(__VA_ARGS__)
#define ESP_LOGW(...) title_test_log(__VA_ARGS__)
#define ESP_LOGE(...) title_test_log(__VA_ARGS__)
''')
    (work / 'esp_timer.h').write_text('''#pragma once
#include <stdint.h>
static inline int64_t esp_timer_get_time(void){return 0;}
''')
    (work / 'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t n,int caps);
void *heap_caps_calloc(size_t n,size_t s,int caps);
void heap_caps_free(void *p);
static inline size_t heap_caps_get_largest_free_block(int caps){(void)caps;return 0;}
''')
    assets = '#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={'
    assets += ','.join(map(str, font)) + '};\n'
    assets += f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(font)}");\n'
    (work / 'assets.c').write_text(assets)
    unit = r'''
#include "ttf_font.h"
#include "ui_kit.h"
#include <assert.h>
#include <fcntl.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef union {max_align_t align;size_t bytes;} allocation_t;
static size_t live,peak;
static unsigned opens,active_fd,max_fd,painted,ui_scale=100;
unsigned app_settings_system_font_size(void){return ui_scale;}
uint8_t app_settings_system_contrast(void){return 100;}
void *heap_caps_malloc(size_t n,int caps){(void)caps;allocation_t *p=malloc(sizeof(*p)+n);assert(p);p->bytes=n;live+=n;if(live>peak)peak=live;return p+1;}
void *heap_caps_calloc(size_t n,size_t s,int caps){void *p=heap_caps_malloc(n*s,caps);memset(p,0,n*s);return p;}
void heap_caps_free(void *p){if(!p)return;allocation_t *a=(allocation_t *)p-1;live-=a->bytes;free(a);}
int title_test_open(const char *path,int flags,...){int fd=open(path,flags,0600);if(fd>=0){++opens;++active_fd;if(active_fd>max_fd)max_fd=active_fd;}return fd;}
int title_test_close(int fd){assert(active_fd);--active_fd;return close(fd);}
const char *app_settings_fonts_dir(void){return "/absent";}
const char *app_settings_font_path(void){return "builtin";}
bool ui_font_has_text(const char *s){(void)s;return true;}
int ui_font_title_px(int px,const char *s){(void)s;return px;}
int ui_font_text_width_px(int px,const char *s){(void)px;(void)s;assert(0);return 0;}
void ui_font_measure_line_px(int px,const char *s,int *above,int *below){(void)px;(void)s;(void)above;(void)below;assert(0);}
void ui_font_draw_text_px(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags a,uint8_t fg,uint8_t bg,bool bw){(void)fb;(void)x;(void)y;(void)px;(void)s;(void)a;(void)fg;(void)bg;(void)bw;assert(0);}
void ui_font_draw_title_px(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags a){(void)fb;(void)x;(void)y;(void)px;(void)s;(void)a;assert(0);}
static uint8_t pixels[400*200];
static bool record_coverage;
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *fb){assert(x>=0&&x<400&&y>=0&&y<200);if(record_coverage)fb[y*400+x]=c>>4;else{assert(c==0);fb[y*400+x]=1;}++painted;}
// 用真实字体逐词独立绘制期望图：扩展只落在空格，词内字形位置保持不变。
// Independently rasterize words with the real face: only spaces expand, preserving all within-word glyph positions.
static void english_justification(void){
 const int px=30,x=20,y=80;record_coverage=true;ttf_set_stroke_adjust(0);ttf_set_antialias(true);
 for(int variant=0;variant<2;++variant)for(int words=2;words<=3;++words)for(int direction=-1;direction<=1;direction+=2){
  const char *word=variant?"it’s":"red";char text[64],chunk[24];
  snprintf(text,sizeof(text),words==2?"%s %s":"%s %s %s",word,word,word);
  snprintf(chunk,sizeof(chunk),"%s ",word);
  int natural=ttf_text_width_px(px,text),extra=direction<0?-1:5;
  uint8_t expected[sizeof(pixels)];memset(expected,15,sizeof(expected));
  int advance=ttf_text_width_px(px,chunk);
  for(int index=0;index<words;++index)
   ttf_draw_text_px(expected,x+index*(advance+extra),y,px,word,EPD_DRAW_ALIGN_LEFT,0,15);
  memset(pixels,15,sizeof(pixels));
  ttf_draw_text_px_fitted(pixels,x,y,px,text,0,natural+(words-1)*extra,0,15);
  assert(!memcmp(pixels,expected,sizeof(pixels)));
  // 过度撑宽退回原字距；不在无空格或不换行空格处强行拉开。
  // Excessive expansion falls back; never stretch inside a word or across a nonbreaking space.
  memset(expected,15,sizeof(expected));ttf_draw_text_px(expected,x,y,px,text,EPD_DRAW_ALIGN_LEFT,0,15);
  memset(pixels,15,sizeof(pixels));
  ttf_draw_text_px_fitted(pixels,x,y,px,text,0,natural+(words-1)*3*(ttf_text_width_px(px," ")/2)+1,0,15);
  assert(!memcmp(pixels,expected,sizeof(pixels)));
 }
 const char *joined[]={"readers","red red","reader’s"};
 for(unsigned i=0;i<sizeof(joined)/sizeof(joined[0]);++i){
  uint8_t expected[sizeof(pixels)];memset(expected,15,sizeof(expected));
  ttf_draw_text_px(expected,x,y,px,joined[i],EPD_DRAW_ALIGN_LEFT,0,15);
  memset(pixels,15,sizeof(pixels));
  ttf_draw_text_px_fitted(pixels,x,y,px,joined[i],0,ttf_text_width_px(px,joined[i])+6,0,15);
  assert(!memcmp(pixels,expected,sizeof(pixels)));
 }
 record_coverage=false;
}
int main(int argc,char **argv){
 assert(argc==3&&ttf_get_antialias());const char *text="字体设置";unsigned widths[2];
 for(int face=0;face<2;++face){
  assert(ttf_font_open(argv[face+1])==ESP_OK&&ttf_font_ready()&&!ttf_font_is_builtin());
  assert(ttf_font_has_text(text)&&!ttf_font_has_text("\xf4\x8f\xbf\xbf"));
  unsigned initial_opens=opens;size_t resident=live;unsigned old_height=0;
  ui_text_set_system_font(!face);ui_text_set_system_scale(!face);
  const int sizes[]={32,40,48};
  for(unsigned i=0;i<3;++i){
   int px=sizes[i];char title[64];strcpy(title,text);
   assert(ui_text_title_fit(title,px,300,text)==px&&!strcmp(title,text));
   assert(ui_text_title_width_px(px,text,text)==ttf_text_width_px(px,text));
   int above,below;ttf_measure_line_px(px,text,&above,&below);
   assert((unsigned)(above+below)>old_height);old_height=above+below;
   record_coverage=true;memset(pixels,15,sizeof(pixels));painted=0;
   ui_text_title_vc(pixels,200,80,px,title,text,EPD_DRAW_ALIGN_CENTER);assert(painted>0);
   uint8_t expected[sizeof(pixels)];memset(expected,15,sizeof(expected));
   ttf_draw_text_px(expected,200,80+(above-below)/2,px,title,EPD_DRAW_ALIGN_CENTER,0,15);
   assert(!memcmp(pixels,expected,sizeof(pixels)));
   size_t warm=live;
   for(unsigned n=0;n<15;++n)ui_text_title_vc(pixels,200,80,px,title,text,EPD_DRAW_ALIGN_CENTER);
   assert(live==warm&&opens==initial_opens&&active_fd==1&&!strcmp(ttf_font_path(),argv[face+1]));
  }
  // 设置字体名称复用实际活动字库；AA开关恢复，字号适配而不换字库。
  // The settings name reuses the real face, restores AA and fits UI sizes without opening another font.
  for(ui_scale=100;ui_scale<=200;ui_scale+=50){
   ui_text_set_system_scale(true);ttf_set_antialias(false);
   EpdRect box={30,30,190,80};memset(pixels,15,sizeof(pixels));painted=0;
   draw_reading_font_name(pixels,box,text);assert(painted&&!ttf_get_antialias()&&ttf_font_selected()==0);
   uint8_t actual[sizeof(pixels)];memcpy(actual,pixels,sizeof(pixels));
   int px=ui_text_effective_px(27),above,below;char fitted[64];strcpy(fitted,text);
   while(ttf_text_width_px(px,fitted)>box.width){size_t n=strlen(fitted)-1;while(n&&((unsigned char)fitted[n]&0xc0)==0x80)--n;fitted[n]=0;}
   ttf_measure_line_px(px,fitted,&above,&below);ttf_set_antialias(true);memset(pixels,15,sizeof(pixels));
   ttf_draw_text_px(pixels,box.x,box.y+box.height/2+(above-below)/2,px,fitted,EPD_DRAW_ALIGN_LEFT,0,15);
   assert(!memcmp(actual,pixels,sizeof(pixels))&&opens==initial_opens&&active_fd==1);
  }
  ui_scale=100;ui_text_set_system_scale(!face);
  english_justification();
  // 目录标题同一活动字库原生绘制，混合白/选中灰底，临时AA不能改变正文开关。
  // Directory titles rasterize in the same active face on white/selected gray, restoring body AA afterward.
  for(ui_scale=100;ui_scale<=200;ui_scale+=100)for(int background=0xc0;background<=0xff;background+=0x3f){
   ui_text_set_system_scale(true);ui_text_set_system_font(!face);ttf_set_stroke_adjust(0);ttf_set_antialias(false);
   uint8_t back=background>>4;EpdRect box={20,30,300,100};
   memset(pixels,back,sizeof(pixels));record_coverage=true;painted=0;
   toc_title(pixels,box,30,text,(uint8_t)background);
   assert(painted&&!ttf_get_antialias()&&opens==initial_opens&&active_fd==1);
   uint8_t actual[sizeof(pixels)];memcpy(actual,pixels,sizeof(pixels));
   int px=ui_text_effective_px(30),above,below;char shown[64];strcpy(shown,text);
   px=ui_text_title_fit(shown,px,box.width,text);ttf_measure_line_px(px,shown,&above,&below);
   memset(pixels,back,sizeof(pixels));ttf_set_antialias(true);
   ttf_draw_text_px(pixels,box.x,box.y+box.height/2+(above-below)/2,px,shown,EPD_DRAW_ALIGN_LEFT,0,back);
   assert(!memcmp(actual,pixels,sizeof(pixels)));
   unsigned edges=0;for(unsigned n=0;n<sizeof(pixels);++n){assert(pixels[n]<=back);edges+=pixels[n]>0&&pixels[n]<back;}
   assert(edges);
  }
  ui_scale=100;ui_text_set_system_scale(!face);
  // Static imported faces must respond too, without changing advances or native normal strokes.
  int native_width=ttf_text_width_px(48,text),axis=ttf_get_weight();
  uint8_t native_mask[300*100],weighted[300*100];
  memset(native_mask,0,sizeof(native_mask));
  assert(ttf_text_mask_px(native_mask,300,100,20,70,48,text));
  size_t last_coverage=0;
  for(int level=-2;level<=2;++level){
   ttf_set_stroke_adjust(level);assert(ttf_get_stroke_adjust()==level&&ttf_get_weight()==axis);
   memset(weighted,0,sizeof(weighted));assert(ttf_text_mask_px(weighted,300,100,20,70,48,text));
   size_t coverage=0;for(unsigned n=0;n<sizeof(weighted);++n)coverage+=weighted[n];
   assert(coverage>last_coverage);last_coverage=coverage;
   assert(ttf_text_width_px(48,text)==native_width&&active_fd==1);
   if(!level)assert(!memcmp(native_mask,weighted,sizeof(weighted)));
   size_t warm=live;memset(weighted,0,sizeof(weighted));
   assert(ttf_text_mask_px(weighted,300,100,20,70,48,text)&&live==warm);
  }
  // 真实TTF普通、带字距、两端对齐绘制：关闭AA只改变覆盖率，字号/字宽/字库/缓存不变。
  record_coverage=true;
  for(int level=-2;level<=2;++level){
   ttf_set_stroke_adjust(level);ttf_set_antialias(true);
   memset(pixels,15,sizeof(pixels));ttf_draw_text_px(pixels,20,80,48,text,EPD_DRAW_ALIGN_LEFT,0,15);
   uint8_t smooth[sizeof(pixels)];memcpy(smooth,pixels,sizeof(smooth));size_t warm=live;
   unsigned mid=0;for(unsigned n=0;n<sizeof(pixels);++n)if(pixels[n]>0&&pixels[n]<15)++mid;
   assert(mid);
   ttf_set_antialias(false);assert(!ttf_get_antialias());
   for(int style=0;style<3;++style){
    memset(pixels,15,sizeof(pixels));
    if(!style)ttf_draw_text_px(pixels,20,80,48,text,EPD_DRAW_ALIGN_LEFT,0,15);
    else if(style==1)ttf_draw_text_px_spaced(pixels,20,80,48,text,2,0,15);
    else ttf_draw_text_px_fitted(pixels,20,80,48,text,0,native_width+9,0,15);
    unsigned ink=0;for(unsigned n=0;n<sizeof(pixels);++n){assert(pixels[n]==0||pixels[n]==15);ink+=pixels[n]==0;}
    assert(ink&&ttf_text_width_px(48,text)==native_width&&live==warm&&active_fd==1);
   }
   ttf_set_antialias(true);memset(pixels,15,sizeof(pixels));
   ttf_draw_text_px(pixels,20,80,48,text,EPD_DRAW_ALIGN_LEFT,0,15);assert(!memcmp(smooth,pixels,sizeof(smooth))&&live==warm);
  }
  record_coverage=false;
  ttf_set_stroke_adjust(0);assert(opens==initial_opens);
  widths[face]=ttf_text_width_px(40,text);
  assert(live-resident<128*1024&&peak<1024*1024);
 }
 assert(widths[1]>widths[0]&&max_fd==1);
 ttf_font_unload();assert(!active_fd&&!ttf_font_ready()&&!ttf_font_has_text(text));
 puts("PASS: actual imported system/reader TTF at 32/40/48px, direct raster identity, five stroke levels with native normal identity and fixed advances, English word-space raster alignment/bounded expansion, single file/face, bounded warm cache and no duplicate font load");
}
'''
    helper='static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}'+'\n'+function('draw_reading_font_name',ROOT/'main/apps/app_book.c')+'\n'
    for name in ('one_line','toc_title'):
        helper += function(name,ROOT/'main/apps/book_toc.c')+'\n'
    unit=unit.replace('int main(int argc,char **argv){',helper+'int main(int argc,char **argv){')
    # Pico没有S31专用的标题宽度包装；其余真实字体与排版检查仍完整执行。
    # Pico has no S31-only title-width wrapper; retain all real-font/raster checks.
    if 'int ui_text_title_width_px(' not in (ROOT / 'main/ui/ui_kit.c').read_text():
        unit = unit.replace('assert(ui_text_title_width_px(px,text,text)==ttf_text_width_px(px,text));', 'assert(ttf_text_width_px(px,text)>0);')
    (work / 'test.c').write_text(unit)
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
             '-Wno-unused-variable', '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
             '-I' + str(work), '-I' + str(ROOT / 'main/font'), '-I' + str(ROOT / 'main/ui'),
             '-I' + str(ROOT / 'main'), '-I' + str(ROOT / 'tools/ui_gesture_stubs')]
    subprocess.run(flags + ['-Dopen=title_test_open', '-Dclose=title_test_close', '-c',
                            str(ROOT / 'main/font/ttf_font.c'), '-o', str(work / 'ttf.o')], check=True)
    gc = '-Wl,-dead_strip' if os.uname().sysname == 'Darwin' else '-Wl,--gc-sections'
    subprocess.run(flags +font_hanzi_host_sources(ROOT,work)+ [str(work / 'test.c'), str(work / 'assets.c'), str(work / 'ttf.o'),
                            str(ROOT / 'main/ui/ui_kit.c'), '-lm', gc, '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test'), str(work / 'system.ttf'), str(work / 'reader.ttf')], check=True)

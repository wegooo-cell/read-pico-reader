#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Render actual TTF/UI glyphs; missing imported glyphs must match per-glyph fallback pixels."""
from pathlib import Path
import argparse,os,struct,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--font',type=Path);p.add_argument('--baseline-ui',type=Path);p.add_argument('--preview',type=Path);args=p.parse_args()
from font_hanzi_host_support import subset_without_xing
with tempfile.TemporaryDirectory(prefix='title-glyph-') as folder:
 w=Path(folder);font=args.font
 if font is None:font=w/'subset.ttf';font.write_bytes(subset_without_xing((ROOT/'main/assets/builtin.ttf').read_bytes()))
 (w/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_INVALID_RESPONSE 0x108\n')
 (w/'esp_log.h').write_text('#pragma once\nstatic inline void test_log(const char*t,const char*f,...){(void)t;(void)f;}\n#define ESP_LOGI(...) test_log(__VA_ARGS__)\n#define ESP_LOGW(...) test_log(__VA_ARGS__)\n#define ESP_LOGE(...) test_log(__VA_ARGS__)\n')
 (w/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n')
 (w/'esp_heap_caps.h').write_text('#pragma once\n#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nstatic inline void *heap_caps_malloc(size_t n,int c){(void)c;return malloc(n);}\nstatic inline void *heap_caps_calloc(size_t n,size_t s,int c){(void)c;return calloc(n,s);}\nstatic inline void heap_caps_free(void*p){free(p);}\nstatic inline size_t heap_caps_get_free_size(int c){(void)c;return 16u*1024u*1024u;}\nstatic inline size_t heap_caps_get_largest_free_block(int c){(void)c;return 16u*1024u*1024u;}\n')
 (w/'miniz.h').write_text('#pragma once\n#include <stddef.h>\n#include <zlib.h>\ntypedef struct {int state;} tinfl_decompressor;\ntypedef int tinfl_status;\n#define TINFL_STATUS_DONE 0\n#define TINFL_FLAG_PARSE_ZLIB_HEADER 1\n#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4\n#define tinfl_init(s) ((s)->state=0)\nstatic inline tinfl_status tinfl_decompress(tinfl_decompressor*s,const unsigned char*in,size_t*n,unsigned char*base,unsigned char*out,size_t*m,int flags){(void)s;(void)base;(void)flags;uLongf z=*m;int r=uncompress(out,&z,in,*n);*m=z;return r==Z_OK?0:-1;}\n')
 assets='#include <stdint.h>\n'
 for filename,symbol in [('builtin.ttf','_binary_builtin_ttf'),('ui-hanzi.bin','_binary_ui_hanzi_bin')]:
  data=(ROOT/'main/assets'/filename).read_bytes();var=filename.replace('.','_').replace('-','_')
  assets+=f'const uint8_t {var}[] asm("{symbol}_start")={{'+','.join(map(str,data))+'};\n'
  assets+=f'asm(".globl {symbol}_end\\n.set {symbol}_end, {symbol}_start + {len(data)}");\n'
 (w/'assets.c').write_text(assets)
 unit=r'''
#include "ttf_font.h"
#include "ui_font.h"
#include "ui_kit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define W 684
#define H 400
static unsigned scale=100;
unsigned app_settings_system_font_size(void){return scale;}
uint8_t app_settings_system_contrast(void){return 100;}
const char *app_settings_fonts_dir(void){return "/absent";}
const char *app_settings_font_path(void){return "builtin";}
static uint8_t actual[W*H],expected[W*H];
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t*fb){assert(x>=0&&x<W&&y>=0&&y<H);fb[y*W+x]=c>>4;}
static const char *cp_next(const char*s,char g[5]){unsigned n=1;while(n<4&&s[n]&&((unsigned char)s[n]&0xc0)==0x80)++n;memcpy(g,s,n);g[n]=0;return s+n;}
static bool fallback(const char*g){return !ttf_font_has_text(g)&&ui_font_has_text(g);}
static int width(int px,const char*s){int n=0;while(*s){char g[5];s=cp_next(s,g);n+=fallback(g)?ui_font_text_width_px(px,g):ttf_text_width_px(px,g);}return n;}
static void reference(uint8_t*fb,int x,int cy,int px,const char*s,int align,uint8_t bg){
 int above=0,below=0;const char*p=s;
 while(*p){char g[5];p=cp_next(p,g);int a=0,b=0;if(fallback(g))ui_font_measure_line_px(px,g,&a,&b);else ttf_measure_line_px(px,g,&a,&b);if(a>above)above=a;if(b>below)below=b;}
 if(align==EPD_DRAW_ALIGN_CENTER)x-=width(px,s)/2;else if(align==EPD_DRAW_ALIGN_RIGHT)x-=width(px,s);
 int baseline=cy+(above-below)/2;bool aa=ttf_get_antialias();ttf_set_antialias(true);
 while(*s){char g[5];s=cp_next(s,g);if(fallback(g)){ui_font_draw_text_px(fb,x,baseline,px,g,EPD_DRAW_ALIGN_LEFT,0,bg,false);x+=ui_font_text_width_px(px,g);}else{ttf_draw_text_px(fb,x,baseline,px,g,EPD_DRAW_ALIGN_LEFT,0,bg);x+=ttf_text_width_px(px,g);}}
 ttf_set_antialias(aa);
}
'''
 from home_cover_cache_host_test import function
 for n in ('heading_prefix','ticket_heading'):unit+=function(n,ROOT/'main/book/book_ticket.c')+'\n'
 unit+=r'''
int main(int argc,char**argv){
 assert(argc==3&&ttf_font_open(argv[1])==ESP_OK&&!ttf_font_is_builtin());
 assert(!ttf_font_has_text("行")&&ui_font_has_text("行"));
 const char *title="十八岁出门远行";char saved[288];snprintf(saved,sizeof(saved),"%s",ttf_font_path());
 ui_text_set_system_font(true);ui_text_set_system_scale(true);
 for(int px=24;px<=60;px+=12)for(int align=0;align<3;++align)for(int bg=12;bg<=15;bg+=3){
  memset(actual,bg,sizeof(actual));memset(expected,bg,sizeof(expected));
  int x=align==EPD_DRAW_ALIGN_CENTER?342:align==EPD_DRAW_ALIGN_RIGHT?602:82;
  ttf_set_antialias(false);
  ui_text_title_background_vc(actual,x,100,px,title,title,align,bg<<4);
  reference(expected,x,100,px,title,align,bg);
  if(memcmp(actual,expected,sizeof(actual))){puts("FAIL: missing-glyph title differs from real fallback raster (including final U+884C)");return 7;}
  assert(ui_text_title_width_px(px,title,title)==width(px,title)&&!ttf_get_antialias());
  assert(!strcmp(saved,ttf_font_path())&&ttf_font_selected()==0);
 }
 for(scale=100;scale<=200;scale+=25){
  memset(actual,15,sizeof(actual));memset(expected,15,sizeof(expected));char name[256];strcpy(name,title);
  assert(ticket_heading(actual,202,112,50,name)==1&&!strcmp(name,title));
  int px=ui_text_effective_px(50);if(px>62)px=62;px=ui_text_title_fit(NULL,px,520,title);
  while(px>28&&width(px,title)>520)--px;
  reference(expected,82,234,px,title,EPD_DRAW_ALIGN_LEFT,15);
  assert(!memcmp(actual,expected,sizeof(actual)));
 }
 if(*argv[2]){FILE*f=fopen(argv[2],"wb");assert(f);fprintf(f,"P5\n%d %d\n255\n",W,H);for(unsigned i=0;i<sizeof(actual);++i)fputc(actual[i]*17,f);assert(!fclose(f));}
 ttf_font_unload();puts("PASS: actual imported font missing U+884C; full7-glyph raster, mixed baseline/advances, left/center/right, white/gray, AA restoration,100–200% ticket, unchanged font path/slot");
}
'''
 (w/'test.c').write_text(unit)
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-I'+str(w),'-I'+str(ROOT/'main/font'),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'tools/ui_gesture_stubs')]
 gc='-Wl,-dead_strip' if os.uname().sysname=='Darwin' else '-Wl,--gc-sections'
 subprocess.run(flags+[str(w/'test.c'),str(w/'assets.c'),str(ROOT/'main/font/ttf_font.c'),str(ROOT/'main/font/ui_font.c'),str(ROOT/'main/font/ui_hanzi.c'),str(args.baseline_ui or ROOT/'main/ui/ui_kit.c'),'-lm','-lz',gc,'-o',str(w/'test')],check=True)
 preview=w/'title.pgm'
 r=subprocess.run([str(w/'test'),str(font),str(preview)])
 if args.baseline_ui:
  assert r.returncode==7,'Baseline must reproduce the missing-glyph raster defect'
  print('Confirmed baseline missing-glyph defect')
 else:
  r.check_returncode()
  if args.preview:
   data=preview.read_bytes().split(b'\n',3)[3]
   def chunk(kind,b):return struct.pack('>I',len(b))+kind+b+struct.pack('>I',zlib.crc32(kind+b))
   png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',684,400,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+data[y*684:(y+1)*684] for y in range(400))))+chunk(b'IEND',b'')
   args.preview.parent.mkdir(parents=True,exist_ok=True);args.preview.write_bytes(png)

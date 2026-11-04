#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Exercise the real detached S31 font loader concurrently with active rasterization."""
from pathlib import Path
import os,struct,subprocess,tempfile
from font_hanzi_host_support import font_hanzi_host_sources
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='s31-font-stage-') as folder:
 w=Path(folder);font=(R/'main/assets/builtin.ttf').read_bytes();face=bytearray(font)
 for i in range(struct.unpack_from('>H',font,4)[0]):
  at=12+i*16
  if font[at:at+4]==b'hmtx':
   off,n=struct.unpack_from('>II',font,at+8)
   for p in range(off,off+n-3,4):
    a=struct.unpack_from('>H',face,p)[0];struct.pack_into('>H',face,p,a+a//4)
 (w/'new.ttf').write_bytes(face);(w/'bad.ttf').write_bytes(b'invalid')
 (w/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_INVALID_RESPONSE 0x108\n')
 (w/'esp_log.h').write_text('#pragma once\nstatic inline void log_stub(const char*t,const char*f,...){(void)t;(void)f;}\n#define ESP_LOGI(...) log_stub(__VA_ARGS__)\n#define ESP_LOGW(...) log_stub(__VA_ARGS__)\n#define ESP_LOGE(...) log_stub(__VA_ARGS__)\n')
 (w/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n')
 (w/'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t,int);void *heap_caps_calloc(size_t,size_t,int);void heap_caps_free(void*);
static inline size_t heap_caps_get_free_size(int c){(void)c;return 16u*1024u*1024u;}
static inline size_t heap_caps_get_largest_free_block(int c){(void)c;return 0;}
''')
 assets='#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={'+','.join(map(str,font))+'};\n'
 assets+=f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(font)}");\n'
 (w/'assets.c').write_text(assets)
 unit=r'''
#include "ttf_font.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
typedef union{max_align_t a;size_t n;} allocation_t;
static atomic_size_t live,allocations,fail_at;
static atomic_bool paused,arrived,cancelled;
void *heap_caps_malloc(size_t n,int caps){(void)caps;size_t serial=atomic_fetch_add(&allocations,1)+1;if(serial==atomic_load(&fail_at))return NULL;allocation_t*p=malloc(sizeof(*p)+n);if(!p)return NULL;p->n=n;atomic_fetch_add(&live,n);return p+1;}
void *heap_caps_calloc(size_t n,size_t m,int caps){void*p=heap_caps_malloc(n*m,caps);if(p)memset(p,0,n*m);return p;}
void heap_caps_free(void*p){if(p){allocation_t*a=(allocation_t*)p-1;atomic_fetch_sub(&live,a->n);free(a);}}
ssize_t stage_read(int fd,void*p,size_t n){if(atomic_load(&paused)){atomic_store(&arrived,true);while(atomic_load(&paused))sched_yield();}return read(fd,p,n);}
const char *app_settings_fonts_dir(void){return "/missing";}const char *app_settings_font_path(void){return "builtin";}
static uint8_t pixels[400*200];
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t*fb){assert(x>=0&&x<400&&y>=0&&y<200);fb[y*400+x]=c;}
static ttf_font_prepared_t *prepared;static esp_err_t error;static const char *target;
static void *loader(void*unused){(void)unused;prepared=ttf_font_prepare(target,&cancelled,&error);return NULL;}
static void render(uint8_t*out){memset(pixels,255,sizeof(pixels));ttf_draw_text_px(pixels,20,80,40,"字体设置",EPD_DRAW_ALIGN_LEFT,0,15);memcpy(out,pixels,sizeof(pixels));}
int main(int argc,char**argv){assert(argc==3);target=argv[1];assert(ttf_font_open_builtin()==ESP_OK);
 uint8_t original[sizeof(pixels)],actual[sizeof(pixels)];render(original);int width=ttf_text_width_px(40,"字体设置");size_t baseline=atomic_load(&live);
 pthread_t thread;atomic_store(&paused,true);assert(!pthread_create(&thread,NULL,loader,NULL));
 while(!atomic_load(&arrived))sched_yield();
 for(int i=0;i<20;++i){assert(ttf_font_is_builtin());render(actual);assert(!memcmp(original,actual,sizeof(actual)));}
 atomic_store(&cancelled,true);atomic_store(&paused,false);assert(!pthread_join(thread,NULL));
 assert(!prepared&&error!=ESP_OK&&ttf_font_is_builtin()&&atomic_load(&live)==baseline);
 atomic_store(&cancelled,false);
 prepared=ttf_font_prepare(argv[2],&cancelled,&error);assert(!prepared&&error!=ESP_OK&&ttf_font_is_builtin());
 for(int failure=1;failure<=3;++failure){atomic_store(&fail_at,atomic_load(&allocations)+failure);
  prepared=ttf_font_prepare(target,&cancelled,&error);ttf_font_prepared_free(prepared);assert(ttf_font_is_builtin());}
 atomic_store(&fail_at,0);
 for(int i=0;i<4;++i){prepared=ttf_font_prepare(target,&cancelled,&error);assert(prepared&&error==ESP_OK&&ttf_font_is_builtin());
  render(actual);assert(!memcmp(original,actual,sizeof(actual)));ttf_font_publish(prepared);
  assert(!strcmp(ttf_font_path(),target)&&ttf_text_width_px(40,"字体设置")>width);
  ttf_font_publish(prepared);assert(ttf_font_is_builtin());render(actual);assert(!memcmp(original,actual,sizeof(actual)));
  ttf_font_prepared_free(prepared);assert(atomic_load(&live)==baseline);
 }
 prepared=ttf_font_prepare(target,&cancelled,&error);assert(prepared);ttf_font_publish(prepared);render(actual);
 assert(memcmp(original,actual,sizeof(actual)));ttf_font_prepared_free(prepared);
 assert(!strcmp(ttf_font_path(),target));ttf_font_unload();assert(!ttf_font_ready());
 puts("PASS: actual detached S31 TTF parser with concurrent old-face raster identity, cancellation, malformed/OOM rejection, atomic publication, rollback/retirement and bounded resource reuse");}
'''
 (w/'test.c').write_text(unit)
 flags=['cc','-std=gnu11','-DKIIKO_BOARD_METALIO47=1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-g','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-pthread','-I'+str(w),'-I'+str(R/'main/font'),'-I'+str(R/'main/book'),'-I'+str(R/'main'),'-I'+str(R/'tools/ui_gesture_stubs')]
 subprocess.run(flags+['-Dread=stage_read','-c',str(R/'main/font/ttf_font.c'),'-o',str(w/'font.o')],check=True)
 gc='-Wl,-dead_strip' if os.uname().sysname=='Darwin' else '-Wl,--gc-sections'
 subprocess.run(flags+[str(w/'test.c'),str(w/'font.o'),str(w/'assets.c')]+font_hanzi_host_sources(R,w)+['-lm',gc,'-o',str(w/'test')],check=True)
 subprocess.run([str(w/'test'),str(w/'new.ttf'),str(w/'bad.ttf')],check=True)

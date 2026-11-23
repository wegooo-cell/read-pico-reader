#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：执行实际lowzag流式适配，覆盖裁切、缩放、损坏和内存失败。
English: Execute the actual lowzag streaming adapter across crop/scale, corruption and allocation failures.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--component', type=Path, required=True)
args = parser.parse_args()
component = args.component.resolve()
source = ROOT / 'main/book/book_cover.c'
text = source.read_text()
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lowzag.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct {unsigned x,y,width,height;} book_crop_t;
typedef struct {uint16_t at;uint8_t frac;} scale_step_t;
static size_t budget=8u*1024u*1024u,allocations,fail_at;
static size_t heap_caps_get_free_size(unsigned caps){assert(caps==3);return budget;}
static void *heap_caps_malloc(size_t n,unsigned caps){assert(caps==3);if(++allocations==fail_at)return NULL;return malloc(n);}
static void heap_caps_free(void *p){free(p);}
'''
for name in ('cover_alloc', 'luminance', 'scale_axis', 'scale_map', 'scale_next', 'bilinear4', 'book_cover_crop'):
    unit += function(name, source) + '\n'
# 适配器经 image_work_budget() 读取剩余 PSRAM；它只在 ESP_PLATFORM 下查询，这里打开以覆盖低内存失败。
# The adapter reads free PSRAM through image_work_budget(), which only queries under ESP_PLATFORM; enable it to cover low-memory failure.
unit += '#define ESP_PLATFORM 1\n' + function('image_work_budget', source) + '\n#undef ESP_PLATFORM\n'
start = text.index('typedef struct { const uint8_t *data; size_t size, pos; } lowzag_mem_input_t;')
end = text.index('#endif', start)
unit += text[start:end]
unit += r'''
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));
 size_t size=(size_t)ftell(f);rewind(f);uint8_t *data=malloc(size);assert(data);
 assert(fread(data,1,size,f)==size);fclose(f);
 const unsigned shapes[][2]={{48,32},{12,8},{96,64},{12,64},{64,12},{1,1}};
 for(unsigned i=0;i<sizeof(shapes)/sizeof(shapes[0]);++i){
  unsigned w=shapes[i][0],h=shapes[i][1];uint8_t *out=malloc((size_t)w*h);assert(out);
  memset(out,0x91,(size_t)w*h);assert(lowzag_gray(data,size,w,h,out));
  if(w>1&&h>1){unsigned low=255,high=0;for(unsigned p=0;p<w*h;++p){if(out[p]<low)low=out[p];if(out[p]>high)high=out[p];}assert(high-low>80);assert(out[0]<out[w*h-1]);}
  free(out);
 }
 uint8_t out[64];assert(!lowzag_gray(data,size/3,8,8,out));
 budget=64u*1024u;assert(!lowzag_gray(data,size,8,8,out));budget=8u*1024u*1024u;
 for(unsigned n=1;n<=6;++n){fail_at=allocations+n;assert(!lowzag_gray(data,size,8,8,out));fail_at=0;}
 free(data);puts("PASS: actual progressive JPEG streaming, crop/scale/upscale, one-pixel target, corruption, low-memory and allocation-failure cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-lowzag-host-') as directory:
    work = Path(directory)
    c = work / 'test.c'
    binary = work / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(component / 'include'), str(c), str(component / 'src/lowzag.c'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(ROOT / 'tools/data/lowzag/gradient-progressive.jpg')], check=True)

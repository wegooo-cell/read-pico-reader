#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""真实GIF首帧解码的边界、交错与透明测试。/ Real GIF decoder bounds, interlace and transparency checks."""
from pathlib import Path
import struct
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]


def gif(width, height, pixels, *, left=0, top=0, interlace=False, transparent=False, local=False):
    palette = b'\0\0\0\xff\xff\xff'
    header = b'GIF89a' + struct.pack('<HHBBB', width, height, 0 if local else 0x80, 0, 0)
    if not local:
        header += palette
    if transparent:
        header += b'\x21\xf9\x04\x01\0\0\0\0'
    rows = list(range(height))
    if interlace:
        rows = [y for start, step in ((0, 8), (4, 8), (2, 4), (1, 2)) for y in range(start, height, step)]
    # 每像素前清空字典，使测试流固定为3位码。/ Clear before each pixel to keep fixture codes at three bits.
    codes = [code for y in rows for pixel in pixels[y * width:(y + 1) * width] for code in (4, pixel)] + [5]
    packed = sum(code << (3 * i) for i, code in enumerate(codes)).to_bytes((len(codes) * 3 + 7) // 8, 'little')
    blocks = b''.join(bytes([len(packed[i:i + 255])]) + packed[i:i + 255] for i in range(0, len(packed), 255))
    return header + b'\x2c' + struct.pack('<HHHHB', left, top, width, height, (0x40 if interlace else 0) | (0x80 if local else 0)) + (palette if local else b'') + b'\x02' + blocks + b'\0\x3b'


def array(name, data):
    return 'static const uint8_t ' + name + '[]={' + ','.join(map(str, data)) + '};\n'


unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void *cover_alloc(size_t n){return malloc(n);}
// 保留原解码/合成，只替代最终缩放。/ Exercise real decode/composition with identity output scaling.
static bool jpegdec_plane_gray(const uint8_t *p,unsigned w,unsigned h,unsigned ow,unsigned oh,uint8_t *out){
 assert(w==ow && h==oh);memcpy(out,p,(size_t)w*h);return true;
}
typedef struct {const uint8_t *data;size_t size,at;uint8_t block[256];int block_len,block_pos;} gif_stream_t;
'''
for name in ('book_gif_is', 'book_gif_frame'):
    unit += function(name, ROOT / 'main/book/book_image_header.c') + '\n'
for name in ('gif_stream_byte', 'gif_lzw_decode', 'gif_gray'):
    unit += function(name, ROOT / 'main/book/book_cover.c') + '\n'
pixels = [(x + y) % 2 for y in range(9) for x in range(3)]
for name, data in [('one', gif(1, 1, [0])), ('below', gif(1, 1, [0], top=1)),
                   ('right', gif(1, 1, [0], left=1)), ('alpha', gif(1, 1, [0], transparent=True)),
                   ('plain', gif(3, 9, pixels)), ('interlaced', gif(3, 9, pixels, interlace=True)),
                   ('local', gif(3, 9, pixels, local=True)),
                   ('short_pixels', gif(1, 1, []) )]:
    unit += array(name, data)
unit += r'''
int main(void){
 uint8_t out[27],expected[27];unsigned w,h;
 assert(!book_gif_is(NULL,20));assert(book_gif_frame(one,sizeof(one),&w,&h)&&w==1&&h==1);
 assert(gif_gray(one,sizeof(one),1,1,out)&&out[0]==0);
 assert(gif_gray(below,sizeof(below),1,1,out)&&out[0]==255);
 assert(gif_gray(right,sizeof(right),1,1,out)&&out[0]==255);
 assert(gif_gray(alpha,sizeof(alpha),1,1,out)&&out[0]==255);
 assert(!gif_gray(short_pixels,sizeof(short_pixels),1,1,out));
 assert(gif_gray(plain,sizeof(plain),3,9,expected));
 for(int y=0;y<9;++y)for(int x=0;x<3;++x)assert(expected[y*3+x]==((x+y)%2?255:0));
 assert(gif_gray(interlaced,sizeof(interlaced),3,9,out)&&!memcmp(out,expected,27));
 assert(gif_gray(local,sizeof(local),3,9,out)&&!memcmp(out,expected,27));
 // 使用精确大小的分配，令ASan能捕获所有截断头/子块的越界读取。/ Exact allocations expose truncated header/sub-block reads to ASan.
 for(size_t n=0;n<sizeof(one);++n){
  uint8_t *p=malloc(n?n:1);memcpy(p,one,n);
  if(n<13)assert(!book_gif_frame(p,n,&w,&h));
  bool ok=gif_gray(p,n,1,1,out);if(n<sizeof(one)-2)assert(!ok);
  free(p);
 }
 uint8_t large[sizeof(one)];memcpy(large,one,sizeof(one));large[6]=large[8]=0xff;large[7]=large[9]=0x1f;
 assert(!gif_gray(large,sizeof(large),1,1,out));
 puts("gif_host_test: PASS (ASan/UBSan)");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-gif-') as temp:
    source = Path(temp) / 'gif.c'
    binary = Path(temp) / 'gif'
    source.write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

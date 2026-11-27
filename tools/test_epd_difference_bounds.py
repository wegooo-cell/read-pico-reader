#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：用独立逐像素预期验证双板真实差分边界。/ English: Check both real difference implementations against a pixel oracle."""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int phases;const uint8_t*luts;const int*phase_times;} EpdWaveformPhases;
static int width=1216;
int epd_width(void){return width;}
static bool s_leading_skip_enabled;
static uint8_t s_present[256];
static const uint8_t *s_present_for;
static const EpdWaveformPhases *s_present_resolved_for __attribute__((unused));
static void mark_present_line(const uint8_t*a,const uint8_t*b,int c){(void)a;(void)b;(void)c;abort();}
uint32_t epd_interlace_4bpp_line_VE(const uint8_t*,const uint8_t*,uint8_t*,uint8_t*,int);
'''
UNIT = r'''
#define H 7
#define W 1216
static _Alignas(16) uint8_t target[W*H/2],previous[W*H/2],interlaced[W*H],columns[W/2];
static bool rows[H];
static unsigned checks;
static uint32_t rng=0xa511e9b3u;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned pixel(const uint8_t *p,int x,int y){return (p[(size_t)y*width/2+x/2]>>(4*(x&1)))&15;}
static void set(int x,int y,unsigned v){size_t at=(size_t)y*width/2+x/2;unsigned shift=4*(x&1);target[at]=(target[at]&~(15u<<shift))|(v<<shift);}
static void check(EpdRect crop){
    memset(interlaced,0xa5,sizeof(interlaced));
    EpdRect got=epd_difference_image_base(target,previous,crop,width,H,interlaced,rows,columns);
    int x_end=min(width,crop.x+crop.width),y_end=min(H,crop.y+crop.height);
    int x0=max(0,crop.x/32*32),x1=min(width,(crop.x+crop.width+31)/32*32);
    int left=x_end,right=crop.x-1,top=y_end,bottom=crop.y-1;
    uint8_t expected_columns[W/2]={0};
    for(int y=0;y<H;++y){
        bool dirty=false;
        if(y>=crop.y&&y<y_end)for(int x=x0;x<x1;++x){
            unsigned to=pixel(target,x,y),from=pixel(previous,x,y),delta=to^from;
            assert(interlaced[(size_t)y*width+x]==(to<<4|from));
            expected_columns[x/2]|=delta<<(4*(x&1));
            dirty|=delta!=0;
            if(delta&&x>=crop.x&&x<x_end){left=min(left,x);right=max(right,x);}
        }
        assert(rows[y]==dirty);
        if(dirty){top=min(top,y);bottom=max(bottom,y);}
        for(int x=0;x<width;++x)if(y<crop.y||y>=y_end||x<x0||x>=x1)
            assert(interlaced[(size_t)y*width+x]==0xa5);
    }
    assert(!memcmp(columns,expected_columns,(size_t)width/2));
    assert(got.x==left&&got.y==top&&got.width==max(0,right-left+1)&&got.height==max(0,bottom-top+1));
    assert(!s_present_for);++checks;
}
int main(void){
    for(int panel=0;panel<2;++panel){
        width=panel?W:64;
        memset(previous,0xff,sizeof(previous));
        for(int x=0;x<width;++x){
            memcpy(target,previous,sizeof(target));set(x,3,0);
            check((EpdRect){0,0,width,H});
            check((EpdRect){x,3,1,1});
        }
        // 两端奇偶与32列边界组合必须完整覆盖，防止右边界读左端的半字节。
        // Cover endpoint parity and32-column boundaries so the right edge never reads the left edge's nibble.
        for(int a=0;a<64;++a)for(int b=a;b<64;++b){
            memcpy(target,previous,sizeof(target));set(a,1,0);set(b,5,7);
            check((EpdRect){0,0,width,H});check((EpdRect){a,1,b-a+1,5});
        }
        for(int i=0;i<1200;++i){
            for(size_t j=0;j<(size_t)width*H/2;++j)previous[j]=(uint8_t)next();
            memcpy(target,previous,sizeof(target));
            EpdRect crop={(int)(next()%width),(int)(next()%H),0,0};
            crop.width=1+next()%(width-crop.x);crop.height=1+next()%(H-crop.y);
            for(unsigned n=next()%40;n;--n)set(next()%width,next()%H,next()&15);
            check(crop);
        }
        memcpy(target,previous,sizeof(target));check((EpdRect){0,0,width,H});
        check((EpdRect){31,2,0,0});
    }
    printf("PASS: %u independent pixel-oracle cases (endpoint parity, crop,32-column alignment, unchanged/empty, interlace and guards)\n",checks);
}
'''

def run(board, directory, negative=False):
    source = (ROOT/f'boards/{board}/src/components/epdiy/src/render.c').read_text()
    start = source.index('__attribute__((optimize("O3"))) static inline int _interlace_line_unaligned(')
    end = source.index('\nEpdRect epd_difference_image(\n', start)
    functions = source[start:end]
    if negative:
        functions = functions.replace('uint8_t mask = max_x % 2 ?', 'uint8_t mask = min_x % 2 ?')
    unit = directory/f'{board}.c'
    unit.write_text(PREFIX+functions+UNIT)
    binary = directory/board
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unknown-attributes',
                    '-Wno-pointer-to-int-cast','-fsanitize=address,undefined','-I'+str(directory),
                    str(unit),str(ROOT/'components/epdiy/src/riscv_pixels.c'),'-o',str(binary)],check=True)
    result = subprocess.run([str(binary)],capture_output=True,text=True)
    if negative:
        assert result.returncode != 0, 'Regression oracle did not reject the old code'
        print(f'{board}: pre-fix negative control rejected')
    else:
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        print(f'{board}: {result.stdout.strip()}')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-regression',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        directory=Path(temp);(directory/'esp_attr.h').write_text('#define IRAM_ATTR\n')
        for board in ('pico','metalio47'):
            run(board,directory)
            if args.check_regression:run(board,directory,True)

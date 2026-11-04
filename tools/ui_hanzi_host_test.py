#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：验证实际汉字补充解码、字形覆盖与栈界限。/ English: Test actual Han supplement decoding, coverage and bounded stack use.
import re,struct,subprocess,tempfile,zlib,hashlib,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
asset=(ROOT/'main/assets'/'ui-hanzi.bin').read_bytes()
magic,count,px,block=struct.unpack_from('<4sIII',asset)
assert magic in (b'PIF1',b'PIF4') and (px,block)==(24,32)
wire_record=77 if magic==b'PIF1' else 293
def legacy_record(record):
 if magic==b'PIF1':return record
 out=bytearray(record[:5]+bytes(72))
 for i in range(576):
  if ((record[5+i//2]>>((i%2)*4))&15)>=6:out[5+i//8]|=1<<(i%8)
 return bytes(out)
raw_records=[]
chars=struct.unpack_from('<'+'H'*count,asset,16);assert list(chars)==sorted(set(chars))
start=16+count*2;offsets=struct.unpack_from('<'+'I'*((count+31)//32+1),asset,start);data=start+len(offsets)*4
assert offsets[0]==0 and data+offsets[-1]==len(asset)
for i in range(len(offsets)-1):
 decoded=zlib.decompress(asset[data+offsets[i]:data+offsets[i+1]])
 assert len(decoded)==min(32,count-i*32)*wire_record
 for at in range(0,len(decoded),wire_record):
  record=legacy_record(decoded[at:at+wire_record]);raw_records.append(record);assert 0<record[2]<=24 and 0<record[3]<=24 and 0<record[4]==17 and any(record[5:])
if count>5637:
 extra_path=ROOT/'main/font/reader_extra_hanzi.txt'
 extras={int(line.split('#',1)[0].strip(),16) for line in extra_path.read_text().splitlines() if line.split('#',1)[0].strip()} if extra_path.exists() else set()
 legacy=[r for cp,r in zip(chars,raw_records) if cp not in extras]
 assert len(legacy)==5637 and hashlib.sha256(b''.join(legacy)).hexdigest()=='15e5824573335a02be0e90ea63f5ee5d3614831295dbfe80e2605b9c436afa8c', 'UI pixels/metrics changed'
with tempfile.TemporaryDirectory() as temp:
 work=Path(temp)
 (work/'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int fail;
static inline void *heap_caps_malloc(size_t n,int c){(void)c;return fail?NULL:malloc(n);}
static inline void heap_caps_free(void *p){free(p);}
static inline void *heap_caps_calloc(size_t n,size_t s,int c){(void)c;return fail?NULL:calloc(n,s);}
''')
 (work/'miniz.h').write_text('''#pragma once
#include <stddef.h>
#include <zlib.h>
typedef struct {int state;} tinfl_decompressor;
typedef int tinfl_status;
#define TINFL_STATUS_DONE 0
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4
#define tinfl_init(s) ((s)->state=0)
static inline tinfl_status tinfl_decompress(tinfl_decompressor *s,const unsigned char *in,size_t *n,unsigned char *base,unsigned char *out,size_t *m,int flags){(void)s;(void)base;(void)flags;uLongf size=*m;int result=uncompress(out,&size,in,*n);*m=size;return result==Z_OK?0:-1;}
''')
 assets='#include <stdint.h>\n'
 for filename,symbol in [('builtin.ttf','_binary_builtin_ttf'),('ui-hanzi.bin','_binary_ui_hanzi_bin')]:
  payload=(ROOT/'main/assets'/filename).read_bytes();var=filename.replace('.','_').replace('-','_')
  assets+=f'const uint8_t {var}[] asm("{symbol}_start")={{'+','.join(map(str,payload))+'};\n'
  assets+=f'asm(".globl {symbol}_end\\n.set {symbol}_end, {symbol}_start + {len(payload)}");\n'
 (work/'assets.c').write_text(assets)
 unit='''#include "ui_hanzi.h"
#include "ui_font.h"
#include <zlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
int fail;
typedef struct {const uint16_t*cps;const uint32_t*hashes;size_t count;unsigned lane;ui_hanzi_cache_t cache;} job_t;
static void*decode_lane(void*arg){
 job_t*job=arg;uint8_t r[77];
 for(unsigned n=0;n<3000;++n){size_t i=(n*131u+job->lane*29u)%job->count;
  bool ok=job->lane?ui_hanzi_get_cached(&job->cache,job->cps[i],r):ui_hanzi_get(job->cps[i],r);
  assert(ok&&crc32(0,r,77)==job->hashes[i]);
 }
 ui_hanzi_cache_release(&job->cache);assert(!job->cache.block&&!job->cache.inflate);return NULL;
}

static unsigned painted;
static bool capture;
static uint8_t actual[192*192];
void epd_draw_pixel(int x,int y,uint8_t color,uint8_t *fb){(void)fb;painted++;if(capture){assert(x>=0&&x<192&&y>=0&&y<192);assert(color==0);actual[y*192+x]=1;}}
int main(void){
 uint8_t record[77],fb=0;
 fail=1;assert(!ui_hanzi_get(TEST_CP,record));fail=0;
 assert(ui_hanzi_get(TEST_CP,record));assert(!ui_hanzi_get(0x10ffff,record));
 static const uint16_t cps[]={CPS};
 for(unsigned i=0;i<sizeof(cps)/sizeof(cps[0]);i++){
  assert(ui_hanzi_has(cps[i])&&ui_hanzi_get(cps[i],record));
  char utf[4]={0xe0|(cps[i]>>12),0x80|((cps[i]>>6)&63),0x80|(cps[i]&63),0};assert(ui_font_has_text(utf));
  if(i%487==0)for(int px=18;px<=64;px+=7){int top,bottom;ui_font_measure_line_px(px,utf,&top,&bottom);assert(top>0&&bottom>=0);assert(ui_font_text_width_px(px,utf)>0);
   painted=0;ui_font_draw_text_px(&fb,100,100,px,utf,EPD_DRAW_ALIGN_LEFT,0,15,false);assert(painted>0);
   assert(ui_font_title_px(px,utf)==24);memset(actual,0,sizeof(actual));capture=true;
   ui_font_draw_title_px(&fb,100,100,px,utf,EPD_DRAW_ALIGN_LEFT);capture=false;
   for(int y=0;y<192;++y)for(int x=0;x<192;++x){int xx=x-100-(int8_t)record[0],yy=y-100-(int8_t)record[1];
    unsigned expected=0;if(xx>=0&&yy>=0&&xx<record[2]&&yy<record[3]){unsigned bit=yy*24+xx;expected=(record[5+bit/8]>>(bit&7))&1u;}
    assert(actual[y*192+x]==expected);}}
 }
 static const uint32_t hashes[]={HASHES};job_t jobs[3]={0};pthread_t threads[3];
 for(unsigned lane=0;lane<3;++lane){jobs[lane].cps=cps;jobs[lane].hashes=hashes;jobs[lane].count=sizeof(cps)/sizeof(cps[0]);jobs[lane].lane=lane;assert(!pthread_create(&threads[lane],NULL,decode_lane,&jobs[lane]));}
 for(unsigned lane=0;lane<3;++lane)assert(!pthread_join(threads[lane],NULL));
 assert(ui_font_title_px(48,"ABC")==48);
 puts("Han supplement: PASS (actual decoder/cache, common glyphs, rendering, OOM and metrics)");
}
'''.replace('TEST_CP',str(chars[0])).replace('CPS',','.join(map(str,chars))).replace('HASHES',','.join(str(zlib.crc32(raw_records[i]))+'u' for i in range(count)))
 (work/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-pthread','-I'+str(work),'-I'+str(ROOT/'main/font'),'-I'+str(ROOT/'tools/ui_gesture_stubs'),str(work/'test.c'),str(work/'assets.c'),str(ROOT/'main/font/ui_font.c'),str(ROOT/'main/font/ui_hanzi.c'),'-lz','-o',str(work/'test')],check=True)
 subprocess.run([str(work/'test')],check=True)
print(f'Compressed blocks: PASS ({count} glyphs, {len(asset)} bytes; bounded decoding)')

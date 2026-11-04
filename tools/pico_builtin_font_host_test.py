#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：普通简体书籍覆盖与原有全部轮廓像素不变验证。
English: Verify ordinary-book coverage and pixel/metric identity of every original outline.
"""
from pathlib import Path
import ctypes,hashlib,json,struct,subprocess,tempfile
from font_hanzi_host_support import font_codepoints
ROOT=Path(__file__).resolve().parents[1]
marker=ROOT/'.kiiko-source-view.json';SOURCE=Path(json.loads(marker.read_text())['source']) if marker.exists() else ROOT
baseline=(SOURCE/'main/assets/builtin.ttf').read_bytes()
current_path=ROOT/'main/assets/builtin.ttf' if marker.exists() else ROOT/'boards/pico/src/main/assets/builtin.ttf'
current=current_path.read_bytes();old=font_codepoints(baseline);new=font_codepoints(current);assert old<=new
hanzi=(ROOT/'main/assets/ui-hanzi.bin' if marker.exists() else ROOT/'boards/pico/src/main/assets/ui-hanzi.bin').read_bytes();count=struct.unpack_from('<I',hanzi,4)[0];supplement=set(struct.unpack_from('<'+'H'*count,hanzi,16));covered=new|supplement
read=lambda path:{int(line.split('#',1)[0].strip(),16) for line in path.read_text().splitlines() if line.split('#',1)[0].strip()}
common=read(ROOT/'tools/fixtures/reader-common-6500.txt');assert len(common)==6500 and common<=covered
for filename in ['reader_extra_hanzi.txt','reader_extra_outlines.txt']:assert read(ROOT/'main/font'/filename)<=covered
assert all(ord(ch) in covered for ch in '《》‘’『』【】①②⑩ⅠⅡⅢāáǎàéöü喆玥珺璟彧祎禛堃芃赟𠳐𥻗𬉼')
with tempfile.TemporaryDirectory(prefix='pico-font-core-') as d:
 d=Path(d);c=d/'core.c';c.write_text('''#include <stdint.h>
#include <math.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
static stbtt_fontinfo f;
int init(const unsigned char*p){return stbtt_InitFont(&f,p,stbtt_GetFontOffsetForIndex(p,0));}
int render(uint32_t cp,int px,int*m,uint8_t*out){int g=stbtt_FindGlyphIndex(&f,cp);if(!g)return -1;float s=stbtt_ScaleForPixelHeight(&f,px);int a,l;stbtt_GetGlyphHMetrics(&f,g,&a,&l);stbtt_GetGlyphBitmapBox(&f,g,s,s,m,m+1,m+2,m+3);m[4]=(int)lroundf(a*s);m[5]=l;int w=m[2]-m[0],h=m[3]-m[1];if(w<0||h<0||w*h>65536)return -2;if(w*h)stbtt_MakeGlyphBitmap(&f,out,w,h,w,s,s,g);return w*h;}
''')
 libpath=d/'core.dylib';subprocess.run(['cc','-O2','-shared','-fPIC','-I'+str(ROOT/'main/font'),str(c),'-lm','-o',str(libpath)],check=True)
 lib=ctypes.CDLL(str(libpath));buffers=[ctypes.create_string_buffer(b) for b in [baseline,current]];metrics=(ctypes.c_int*6)();pixels=(ctypes.c_uint8*65536)();snapshots=[]
 for buffer in buffers:
  assert lib.init(buffer);result={}
  for px in [12,24,43,72,160]:
   digest=hashlib.sha256()
   for cp in sorted(old):
    n=lib.render(cp,px,metrics,pixels);assert n>=0,(hex(cp),px,n)
    digest.update(struct.pack('<I6i',cp,*metrics));digest.update(bytes(pixels[:n]))
   result[px]=digest.hexdigest()
  snapshots.append(result)
 assert snapshots[0]==snapshots[1],snapshots
print(f'PASS: {len(covered)} embedded characters, all6500 common standardized Han, {len(covered-(old|supplement))} native-only additions; all{len(old)} original outlines/advances/bearings pixel-identical at12/24/43/72/160px; sparse native non-BMP coverage')

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：从同源Medium轮廓生成两板正文灰阶汉字，保留界面单色像素。
English: Generate both boards' reader Han coverage from the same Medium outlines, retaining UI pixels.
"""
from pathlib import Path
import argparse,ctypes,hashlib,json,struct,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
BASE=24;BLOCK=32;RECORD=293

def main():
 p=argparse.ArgumentParser();p.add_argument('--src',type=Path,required=True);p.add_argument('--out',type=Path,default=ROOT/'boards/pico/src/main/assets/ui-hanzi.bin');p.add_argument('--mono-out',type=Path,help='Optional compact PIF1 reference output with identical UI pixels');p.add_argument('--extra',type=Path,default=ROOT/'main/font/reader_extra_hanzi.txt');a=p.parse_args()
 raw=a.src.read_bytes();n=struct.unpack_from('>H',raw,4)[0]
 tables={raw[12+i*16:16+i*16]:struct.unpack_from('>II',raw,20+i*16) for i in range(n)}
 with tempfile.TemporaryDirectory(prefix='reader-hanzi-generator-') as d:
  d=Path(d)
  if b'fvar' in tables:
   from fontTools.ttLib import TTFont
   from fontTools.varLib.instancer import instantiateVariableFont
   f=TTFont(a.src);f=instantiateVariableFont(f,{'wght':500},inplace=True);f.save(d/'medium.ttf');raw=(d/'medium.ttf').read_bytes()
  helper=d/'raster.c';helper.write_text('''#include <stdint.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
static stbtt_fontinfo f;
int init(const unsigned char*d){return stbtt_InitFont(&f,d,stbtt_GetFontOffsetForIndex(d,0));}
int denominator(void){int a,d,g;stbtt_GetFontVMetrics(&f,&a,&d,&g);return a-d;}
int render(uint32_t cp,uint8_t*out){int gid=stbtt_FindGlyphIndex(&f,cp);if(!gid)return 0;float s=stbtt_ScaleForPixelHeight(&f,24);int x,y,r,b,adv,left;stbtt_GetGlyphBitmapBox(&f,gid,s,s,&x,&y,&r,&b);stbtt_GetGlyphHMetrics(&f,gid,&adv,&left);int w=r-x,h=b-y;if(w<1||h<1||w>24||h>24)return 0;out[0]=x;out[1]=y;out[2]=w;out[3]=h;out[4]=(int)(adv*s+0.5f);memset(out+5,0,576);stbtt_MakeGlyphBitmap(&f,out+5,w,h,24,s,s,gid);return adv;}
''')
  libpath=d/'raster.dylib';subprocess.run(['cc','-O2','-shared','-fPIC','-I'+str(ROOT/'main/font'),str(helper),'-lm','-o',str(libpath)],check=True)
  lib=ctypes.CDLL(str(libpath));buf=ctypes.create_string_buffer(raw);assert lib.init(buf)
  original=(ROOT/'main/assets/ui-hanzi.bin').read_bytes();count=struct.unpack_from('<I',original,4)[0]
  core=struct.unpack_from('<'+'H'*count,original,16);assert count==5637
  extras={int(line.split('#',1)[0].strip(),16) for line in a.extra.read_text().splitlines() if line.split('#',1)[0].strip()} if a.extra.exists() else set()
  assert not extras.intersection(core) and all(0x3400<=cp<=0xffff for cp in extras)
  chars=sorted(set(core)|extras);assert len(chars)<=6763
  start=16+count*2;old_offsets=struct.unpack_from('<'+'I'*((count+31)//32+1),original,start);start+=len(old_offsets)*4
  legacy=b''.join(zlib.decompress(original[start+old_offsets[i]:start+old_offsets[i+1]]) for i in range(len(old_offsets)-1))
  reconstructed=bytearray()
  out=(ctypes.c_uint8*581)();rows=[];mono_rows=[]
  for cp in chars:
   assert lib.render(cp,out)==1000,(hex(cp),'nonuniform advance')
   record=bytearray(bytes(out[:5])+bytes(288));binary=bytearray(bytes(out[:5])+bytes(72))
   for i,alpha in enumerate(out[5:]):
    q=(alpha*15+127)//255
    if alpha<96 and q>=6:q=5
    record[5+i//2]|=q<<((i%2)*4)
    if q>=6:binary[5+i//8]|=1<<(i%8)
   if cp in core:reconstructed.extend(binary)
   rows.append(record);mono_rows.append(binary)
  assert reconstructed==legacy, "UI glyphs changed"
  offsets=[0];data=bytearray()
  for i in range(0,len(rows),BLOCK):data.extend(zlib.compress(b''.join(rows[i:i+BLOCK]),9));offsets.append(len(data))
  denominator=lib.denominator();assert denominator==1448
  asset=struct.pack('<4sIII',b'PIF4',len(chars),BASE,BLOCK)+struct.pack('<'+'H'*len(chars),*chars)+struct.pack('<'+'I'*len(offsets),*offsets)+data
  a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_bytes(asset)
  if a.mono_out:
   mono_offsets=[0];mono_data=bytearray()
   for i in range(0,len(mono_rows),BLOCK):mono_data.extend(zlib.compress(b''.join(mono_rows[i:i+BLOCK]),9));mono_offsets.append(len(mono_data))
   mono_asset=struct.pack('<4sIII',b'PIF1',len(chars),BASE,BLOCK)+struct.pack('<'+'H'*len(chars),*chars)+struct.pack('<'+'I'*len(mono_offsets),*mono_offsets)+mono_data
   a.mono_out.parent.mkdir(parents=True,exist_ok=True);a.mono_out.write_bytes(mono_asset)
  print(json.dumps(dict(glyphs=len(chars),added_glyphs=len(extras),bytes=len(asset),sha256=hashlib.sha256(asset).hexdigest(),source_sha256=hashlib.sha256(raw).hexdigest(),base=BASE,coverage_bits=4,advance_units=1000,scale_denominator=denominator)))
if __name__=='__main__':main()

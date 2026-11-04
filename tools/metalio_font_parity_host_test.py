#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：S31/Pico补字逐字节相同、全部旧UI像素不变，以及真实启动画布检查。
English: S31/Pico supplement identity, unchanged legacy UI pixels and the actual startup canvas check.
"""
from pathlib import Path
import hashlib,json,struct,subprocess,tempfile,zlib
from font_hanzi_host_support import font_codepoints
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
marker=R/'.kiiko-source-view.json';source=Path(json.loads(marker.read_text())['source']) if marker.exists() else R
pico=source/'boards/pico/src/main/assets';s31=source/'boards/metalio47/src/main/assets'
aa=(s31/'ui-hanzi.bin').read_bytes();native=(s31/'builtin.ttf').read_bytes()
assert aa==(pico/'ui-hanzi.bin').read_bytes() and native==(pico/'builtin.ttf').read_bytes()
magic,count,px,block=struct.unpack_from('<4sIII',aa);assert (magic,count,px,block)==(b'PIF4',6030,24,32)
chars=struct.unpack_from('<'+'H'*count,aa,16);at=16+count*2
index=struct.unpack_from('<'+'I'*((count+31)//32+1),aa,at);at+=len(index)*4
mono=bytearray()
for n in range(len(index)-1):
 raw=zlib.decompress(aa[at+index[n]:at+index[n+1]])
 for off in range(0,len(raw),293):
  record=raw[off:off+293];old=bytearray(record[:5]+bytes(72))
  for bit in range(576):
   if ((record[5+bit//2]>>((bit%2)*4))&15)>=6:old[5+bit//8]|=1<<(bit%8)
  mono.extend(old)
assert hashlib.sha256(mono).hexdigest()=='c9ab41911c03df18540b84ed6aee9121022f59f758d3b5ae3b288005218c97b3'
covered=font_codepoints(native)|set(chars);assert len(covered)==7668
common={int(line.split('#',1)[0].strip(),16) for line in (source/'tools/fixtures/reader-common-6500.txt').read_text().splitlines() if line.split('#',1)[0].strip()};assert common<=covered
for name in ['reader_missing_advance','ttf_text_left_bearing_px']:
 assert function(name,source/'main/font/ttf_font.c')==function(name,source/'boards/pico/src/main/font/ttf_font.c'),name
cmake=(source/'main/CMakeLists.txt').read_text().split('\nelse()',1)[0];assert '"assets/lock_4bpp.bin"' not in cmake
boot=source/'boards/metalio47/src/main/app_main.c';assert '_binary_lock_4bpp' not in boot.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define ESP_LOGE(...) ((void)0)
static int w,h;
static int epd_width(void){return w;}static int epd_height(void){return h;}
'''+function('canvas_matches_panel',boot)+r'''
int main(void){w=1216;h=684;assert(canvas_matches_panel());w=684;h=1216;assert(canvas_matches_panel());
 w=2432;h=342;assert(!canvas_matches_panel());w=1215;h=684;assert(!canvas_matches_panel());w=0;h=0;assert(!canvas_matches_panel());
 puts("PASS: actual S31 startup keeps both native orientations and rejects incompatible canvases without unused full-screen art");}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: S31/Pico identical PIF4 and native face;7668 characters/all common6500; all6030 legacy UI glyph pixels/metrics unchanged; exact advances/bearings shared')

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""实际EPUB图片尺寸探测：小头部IO、晚JPEG/SVG回退及完整提取CRC。"""
from pathlib import Path
import argparse,json,random,struct,subprocess,tempfile,zipfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--epub',type=Path);args=p.parse_args()
unit=r'''
#include "book_epub.h"
#include "book_cover.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t read_bytes,full_reads,probes;
size_t measured_fread(void *p,size_t n,size_t m,FILE *f){size_t got=fread(p,n,m,f);read_bytes+=got*n;return got;}
int ttf_font_open_mem(uint8_t *p,size_t n,const char *label){(void)n;(void)label;free(p);return -1;}
void ttf_font_close_embedded(void){}
static book_epub_t *book;
typedef struct{size_t chapter;char **images;size_t count;const html_note_t *notes;size_t note_count;}reader_dims_ctx_t;
static int s_px=20;
static int book_chapter_image_dimensions(size_t ch,const char *src,unsigned *w,unsigned *h){++probes;return book_epub_image_dimensions(book,ch,src,w,h);}
'''+function('reader_image_dims',ROOT/'main/apps/app_book.c')+r'''
int main(int argc,char **argv){assert(argc==3&&book_epub_open(argv[1],&book)==ESP_OK);book_epub_set_fonts_enabled(book,false);
 uint64_t total_reads=0,full_bytes=0,checksum=0;unsigned images=0;
 for(size_t ch=0;ch<book_epub_chapter_count(book);++ch){html_text_t text={0};assert(book_epub_load(book,ch,&text)==ESP_OK);
  reader_dims_ctx_t ctx={ch,text.images,text.image_count,text.notes,text.note_count};assert(!reader_image_dims(NULL,0,NULL,NULL));assert(!reader_image_dims(&ctx,-1,NULL,NULL));assert(!reader_image_dims(&ctx,(int)text.image_count,NULL,NULL));
  for(size_t i=0;i<text.image_count;++i){unsigned w=0,h=0;int pw=0,ph=0;
   read_bytes=0;assert(reader_image_dims(&ctx,(int)i,&pw,&ph));total_reads+=read_bytes;
   uint8_t *data=NULL;size_t n=0;bool png=false;
   ++full_reads;read_bytes=0;assert(book_epub_image(book,ch,text.images[i],&data,&n,&png)==ESP_OK);full_bytes+=read_bytes;
   assert(book_image_dimensions(data,n,png,&w,&h)&&w==(unsigned)pw&&h==(unsigned)ph);free(data);
   checksum+=(uint64_t)w*h;++images;
  }
  html_text_free(&text);
 }
 if(!strcmp(argv[2],"png")){assert(images==8&&total_reads<8*2048&&full_bytes>8*1024*1024);}
 if(!strcmp(argv[2],"compat")){
  assert(images==6);unsigned w=0,h=0;
  assert(book_epub_image_dimensions(book,0,"missing.png",&w,&h)!=ESP_OK);
  assert(book_epub_image_dimensions(book,0,"broken.png",&w,&h)!=ESP_OK);
  assert(book_epub_image_dimensions(book,0,"huge.png",&w,&h)!=ESP_OK);
  // 尺寸仅为提示，完整读取仍拒绝损坏CRC；不能用头部探测绕过显示校验。
  // Dimensions are advisory; full extraction must still reject a corrupt payload CRC.
  assert(book_epub_image_dimensions(book,0,"corrupt.png",&w,&h)==ESP_OK&&w==80&&h==20);
  uint8_t *data=NULL;size_t n=0;bool png=false;
  assert(book_epub_image(book,0,"corrupt.png",&data,&n,&png)!=ESP_OK&&!data&&!n);
 }
 book_epub_close(book);assert(full_reads==images&&probes==images);
 printf("{\"images\":%u,\"dimension_checksum\":%llu,\"probe_read_bytes\":%llu,\"full_read_bytes\":%llu}\n",images,(unsigned long long)checksum,(unsigned long long)total_reads,(unsigned long long)full_bytes);
}
'''
def png(w=80,h=20):return b'\x89PNG\r\n\x1a\n'+struct.pack('>I',13)+b'IHDR'+struct.pack('>II',w,h)+b'\x10\x02\0\0\0'
def jpeg(extra=0,progressive=False):
    data=b'\xff\xd8'
    while extra:
        n=min(extra,60000);data+=b'\xff\xe1'+struct.pack('>H',n+2)+b'x'*n;extra-=n
    return data+b'\xff'+(b'\xc2' if progressive else b'\xc0')+b'\0\x11\x08\0\x14\0\x50'+bytes(10)+b'\xff\xd9'
def epub(path,files,names,method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path,'w',compression=method) as z:
        z.writestr('META-INF/container.xml','<container><rootfiles><rootfile full-path="book.opf"/></rootfiles></container>')
        z.writestr('book.opf','<package><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        z.writestr('body.xhtml','<html><body>'+''.join(f'<p><img src="{n}"/></p>' for n in names)+'</body></html>')
        for name,data in files.items():z.writestr(name,data)
with tempfile.TemporaryDirectory(prefix='comic-header-') as folder:
    work=Path(folder);(work/'test.c').write_text(unit)
    (work/'io.h').write_text('#include <stdio.h>\nsize_t measured_fread(void *,size_t,size_t,FILE *);\n')
    flags=['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(ROOT/'tools/book_epub_stubs'),'-I'+str(ROOT/'tools/zip_host_stubs'),'-I'+str(ROOT/'main/book')]
    subprocess.run(flags+['-include',str(work/'io.h'),'-Dfread=measured_fread','-c',str(ROOT/'main/book/zip_reader.c'),'-o',str(work/'zip.o')],check=True)
    subprocess.run(flags+[str(work/'test.c'),str(work/'zip.o')]+[str(ROOT/'main/book'/n) for n in ('book_epub.c','html_text.c','book_index_cache.c','book_image_header.c')]+['-lz','-o',str(work/'test')],check=True)
    # 可重复的大PNG负载不包含用户漫画内容。/ Reproducible large PNG payloads contain no user comic artwork.
    payload=random.Random(12345).randbytes(1024*1024)
    files={f'p{i}.png':png()+payload for i in range(8)}
    for method in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED):
        path=work/f'png{method}.epub';epub(path,files,list(files),method);subprocess.run([str(work/'test'),str(path),'png'],check=True)
    files={'small.png':png()[:24],'normal.jpg':jpeg(700),'late.jpg':jpeg(6000,True),'verylate.jpg':jpeg(70000),
           'normal.png':png()+payload,'wrapper.svg':b'<svg><image href="normal.png"/></svg>',
           'broken.png':b'bad','huge.png':png(9000,20)+payload,'corrupt.png':png()+payload}
    names=['small.png','normal.jpg','late.jpg','verylate.jpg','normal.png','wrapper.svg']
    path=work/'compat.epub';epub(path,files,names,zipfile.ZIP_STORED)
    with zipfile.ZipFile(path) as z:entry=z.getinfo('corrupt.png')
    data=bytearray(path.read_bytes());offset=entry.header_offset+30+len(entry.filename.encode())+len(entry.extra)+entry.file_size-1;data[offset]^=0x20;path.write_bytes(data)
    subprocess.run([str(work/'test'),str(path),'compat'],check=True)
    if args.epub:subprocess.run([str(work/'test'),str(args.epub),'external'],check=True)
print('PASS: actual reader uses advisory headers, PNG IO stays bounded, exact full dimensions match; stored/deflated, baseline/progressive/late JPEG and SVG retain compatibility; full payload CRC still rejects corruption')

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实TXT流式UTF16、边界、索引和缓存迁移回归。
English: Actual streaming UTF16, scalar boundaries, indexes and cache migration.
"""
from pathlib import Path
import hashlib
import subprocess
import tempfile

r = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "book_index_cache.h"
static int fail_allocation;
static size_t largest;
static bool disable_cache;
static unsigned cache_hits;
void *heap_caps_malloc(size_t bytes,int caps){
 assert(caps==3);if(bytes>largest)largest=bytes;
 if(fail_allocation>0&&!--fail_allocation)return NULL;
 return malloc(bytes);
}
#include "book_txt.c"
#include "book_policy.h"
// 缓存外壳定向到临时目录，实际TXT载荷读写及版本校验仍由生产代码执行。
// Redirect the cache shell into the fixture directory; production code handles TXT payloads/version requests.
bool book_index_cache_prepare(const char *source,const char *tag,uint32_t version,
 char *path,size_t cap,book_index_cache_header_t *key){
 if(disable_cache)return false;assert(!strcmp(tag,"txt")&&version==6);
 struct stat st;assert(!stat(source,&st));
 *key=(book_index_cache_header_t){.magic=123,.version=version,.source_size=(uint32_t)st.st_size,.modified=st.st_mtime,.path_hash=456};
 return snprintf(path,cap,"%s.idx",source)>0;
}
bool book_index_cache_prepare_file(const char *source,const char *tag,uint32_t version,
 uint64_t size,int64_t modified,char *path,size_t cap,book_index_cache_header_t *key){
 return book_index_cache_prepare(source,tag,version,path,cap,key) &&
        key->source_size==size && key->modified==modified;
}
FILE *book_index_cache_open_read(const char *path,const book_index_cache_header_t *key){
 FILE *f=fopen(path,"rb");if(!f)return NULL;book_index_cache_header_t found;
 if(fread(&found,1,sizeof(found),f)!=sizeof(found)||found.magic!=key->magic||found.version!=key->version||
    found.source_size!=key->source_size||found.modified!=key->modified||found.path_hash!=key->path_hash){fclose(f);return NULL;}
 ++cache_hits;return f;
}
FILE *book_index_cache_open_write(const char *path,const book_index_cache_header_t *key,char *temp,size_t cap){
 assert((size_t)snprintf(temp,cap,"%s.tmp",path)<cap);FILE *f=fopen(temp,"wb");assert(f);
 assert(fwrite(key,1,sizeof(*key),f)==sizeof(*key));return f;
}
bool book_index_cache_finish_write(FILE *f,const char *temp,const char *path,bool ok){
 assert(!fclose(f));if(ok){assert(!rename(temp,path));return true;}remove(temp);return false;
}
static void close_book(book_txt_t *b){book_txt_close(b);}
static unsigned char *bytes(const char *path,size_t *size){
 FILE *f=fopen(path,"rb");assert(f&&!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0&&!fseek(f,0,SEEK_SET));
 unsigned char *out=malloc((size_t)n+1);assert(out&&fread(out,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));out[n]=0;*size=(size_t)n;return out;
}
static void verify(const char *path,const char *expected,int encoding,int bom){
 book_txt_t b={0};assert(book_txt_open(&b,path)==ESP_OK);
 assert(b.utf16==(encoding==3?0:encoding)&&b.gbk==(encoding==3)&&b.bom==(unsigned)bom&&b.count&&b.count<=2048);
 size_t n;unsigned char *want=bytes(expected,&n);size_t used=0;
 for(size_t i=0;i<b.count;++i){
  uint32_t start=b.entries[i].offset,end=i+1<b.count?b.entries[i+1].offset:b.total;
  assert((i||!start)&&end>=start&&end-start<=BOOK_CHAPTER_BYTES_MAX);
  if(b.utf16)assert(!(start%2));
  char *out=NULL;size_t len=0;assert(book_txt_load(&b,i,&out,&len)==ESP_OK&&out&&out[len]==0);
  assert(used+len<=n&&!memcmp(out,want+used,len));used+=len;
  assert(book_position_bytes(start,end,0,len)==start);
  if(len)assert(book_position_bytes(start,end,len,len)==end);
  free(out);
 }
 assert(used==n);free(want);close_book(&b);
}
int main(int argc,char **argv){
 assert(argc==2);FILE *manifest=fopen(argv[1],"r");assert(manifest);
 char path[256],expected[272],cache[272];int encoding,bom;unsigned fixtures=0;
 while(fscanf(manifest,"%255s %d %d",path,&encoding,&bom)==3){
  snprintf(expected,sizeof(expected),"%s.utf8",path);snprintf(cache,sizeof(cache),"%s.idx",path);
  verify(path,expected,encoding,bom);unsigned hits=cache_hits;
  // 空文件目录不命中缓存，沿用原行为；其他正常/损坏字符均能从已确认编码重载。
  // Empty indexes keep their existing miss behavior; all other fixtures reload the confirmed encoding.
  verify(path,expected,encoding,bom);
  struct stat st;assert(!stat(path,&st));if(st.st_size)assert(cache_hits>hits);
  FILE *f=fopen(cache,"r+b");assert(f);book_index_cache_header_t header;assert(fread(&header,1,sizeof(header),f)==sizeof(header));
  header.version=1;assert(!fseek(f,0,SEEK_SET)&&fwrite(&header,1,sizeof(header),f)==sizeof(header)&&!fclose(f));
  verify(path,expected,encoding,bom);
  f=fopen(cache,"r+b");assert(f&&!fseek(f,sizeof(header)+offsetof(txt_cache_payload_t,reserved),SEEK_SET));
  assert(fputc(3,f)!=EOF&&!fclose(f));verify(path,expected,encoding,bom);
  ++fixtures;
 }
 assert(!fclose(manifest)&&fixtures>=20);
 // 真实打开/按章输出的各层内存失败，均允许调用方关闭后重新打开。
 // Fail real index/reader/output allocations, allowing caller cleanup and a later reopen.
 disable_cache=true;
 for(int fail=1;fail<=2;++fail){book_txt_t b={0};fail_allocation=fail;assert(book_txt_open(&b,path)==ESP_ERR_NO_MEM);close_book(&b);fail_allocation=0;}
 book_txt_t b={0};assert(book_txt_open(&b,path)==ESP_OK);
 for(int fail=1;fail<=2;++fail){char *out=NULL;size_t n=0;fail_allocation=fail;assert(book_txt_load(&b,0,&out,&n)==ESP_ERR_NO_MEM&&!out);fail_allocation=0;}
 close_book(&b);assert(largest<=3*BOOK_CHAPTER_BYTES_MAX+1);
 printf("PASS: %u real TXT fixtures,UTF16 LE/BE,BOM/no-BOM,surrogate/block/odd-byte boundaries,UTF8/GBK,source-offset parts,cache v1/corruption rebuild,OOM and bounded allocation\n",fixtures);
}
'''
with tempfile.TemporaryDirectory(prefix='s31-txt16-') as folder:
    t = Path(folder)
    (t/'esp_heap_caps.h').write_text('#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,int);\n')
    rows, hashes = [], {}

    def fixture(label, data, text, encoding, bom):
        p = t/(label+'.txt')
        p.write_bytes(data)
        Path(str(p)+'.utf8').write_bytes(text.encode('utf-8'))
        rows.append(f'{p} {encoding} {bom}\n')
        hashes[p] = hashlib.sha256(data).hexdigest()

    for endian, codec, marker in ((1,'utf-16-le',b'\xff\xfe'), (2,'utf-16-be',b'\xfe\xff')):
        for bom in (0,2):
            for label, text in (('english','Chapter 1 开始\r\n你好😀𠮷\nChapter 2 结束\n再见\n'),
                                ('chinese','第一章 开始\n正文中文\n第二章 终章\n结尾abcdef\n')):
                fixture(f'{label}-{endian}-{bom}',(marker if bom else b'')+text.encode(codec),text,endian,bom)
        for boundary in (32766,32767,32768):
            text='A'*boundary+'😀𠮷中文\n'+'长章节'*40000
            fixture(f'block-{endian}-{boundary}',marker+text.encode(codec),text,endian,2)
        units=[0xd800,0x41,0xdc00,0xd800]
        raw=b''.join(v.to_bytes(2,'big' if endian==2 else 'little') for v in units)+b'\x7f'
        fixture(f'bad-{endian}',marker+raw,'�A���',endian,2)
        fixture(f'bom-only-{endian}',marker,'',endian,2)
    for label,data,text,bom in (
        ('utf8','第一章 开始\n中文😀\n第二章 结束'.encode(),'第一章 开始\n中文😀\n第二章 结束',0),
        ('utf8-bom',b'\xef\xbb\xbfChapter 1\r\ntext\nChapter 2\nend','Chapter 1\r\ntext\nChapter 2\nend',3),
        ('empty',b'','',0)):
        fixture(label,data,text,0,bom)
    fixture('gbk','第一章 起点\n中文\n第二章 终点\n再见'.encode('gbk'),'第一章 起点\n中文\n第二章 终点\n再见',3,0)
    manifest=t/'manifest';manifest.write_text(''.join(rows))
    c=t/'test.c';c.write_text(unit);exe=t/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-DKIIKO_BOARD_METALIO47=1','-I'+str(t),'-I'+str(r/'main/book'),'-I'+str(r/'main'),
                    '-I'+str(r/'tools/book_epub_stubs'),str(c),str(r/'main/book/gbk.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(manifest)],check=True)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())

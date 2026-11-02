#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际元数据缓存读出复用目录stat，保持命中、失效和损坏边界。
English: Actual metadata-cache reads reuse directory stat while preserving hits, invalidation and corruption gates.
"""
from pathlib import Path
import re,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
def function(name,path):
 s=path.read_text();scan=re.sub(r"""/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""",lambda m:' '*len(m[0]),s)
 m=re.search(r'^(?:static )?(?:[A-Za-z_]\w*(?:\s+|\s*\*\s*))+'+name+r'\([^;{}]*?\)\s*\{',scan,re.M);assert m,name
 i=m.end();depth=1
 while depth:depth+=(scan[i]=='{')-(scan[i]=='}');i+=1
 return s[m.start():i]
header=(R/'main/book/book_index_cache.h').read_text();header='\n'.join(x for x in header.splitlines() if not x.startswith(('#include','#pragma')))
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef int esp_err_t;
enum{ESP_OK,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_SIZE,ESP_ERR_NOT_FOUND};
#define CACHE_MAGIC UINT32_C(0x52494331)
#define EPUB_META_CACHE_VERSION 1u
typedef struct{char title[256],author[160];}epub_meta_cache_payload_t;
static unsigned stats,reads;
static const char*source="/sdcard/books/book.epub";
static char physical[256],expected_path[112];
static int fake_stat(const char*p,struct stat*out){assert(!strcmp(p,source));++stats;memset(out,0,sizeof(*out));out->st_mode=S_IFREG;out->st_size=1234;out->st_mtime=5678;return 0;}
static FILE*fake_fopen(const char*p,const char*mode){assert(!strcmp(mode,"rb"));++reads;if(strcmp(p,expected_path))return NULL;return fopen(physical,mode);}
#define stat(p,s) fake_stat(p,s)
#define fopen(p,m) fake_fopen(p,m)
'''+header+'\n'
for name in ['cache_hash','book_index_cache_prepare_file','book_index_cache_prepare','book_index_cache_open_read']:u+=function(name,R/'main/book/book_index_cache.c')+'\n'
for name in ['metadata_cached_read','book_epub_metadata_cached','book_epub_metadata_cached_for_file']:u+=function(name,R/'main/book/book_epub.c')+'\n'
u+=r'''
#undef stat
#undef fopen
static void write_cache(book_index_cache_header_t header,epub_meta_cache_payload_t payload,size_t bytes){FILE*f=fopen(physical,"wb");assert(f&&fwrite(&header,1,sizeof(header),f)==sizeof(header)&&fwrite(&payload,1,bytes,f)==bytes&&!fclose(f));}
int main(int argc,char**argv){
 assert(argc==2);snprintf(physical,sizeof(physical),"%s/cache.bin",argv[1]);
 book_index_cache_header_t old,key;char old_path[112];
 assert(book_index_cache_prepare(source,"epmeta",1,old_path,sizeof(old_path),&old)&&stats==1);
 assert(book_index_cache_prepare_file(source,"epmeta",1,1234,5678,expected_path,sizeof(expected_path),&key));
 assert(!strcmp(old_path,expected_path)&&old.magic==key.magic&&old.version==key.version&&old.source_size==key.source_size&&old.modified==key.modified&&old.path_hash==key.path_hash);
 epub_meta_cache_payload_t payload={"The title","Author"};write_cache(key,payload,sizeof(payload));
 char title[256],author[160];stats=reads=0;
 assert(book_epub_metadata_cached(source,title,sizeof(title),author,sizeof(author))==ESP_OK&&stats==1&&reads==1&&!strcmp(title,payload.title)&&!strcmp(author,payload.author));
 stats=reads=0;
 assert(book_epub_metadata_cached_for_file(source,1234,5678,title,sizeof(title),author,sizeof(author))==ESP_OK&&stats==0&&reads==1&&!strcmp(title,payload.title)&&!strcmp(author,payload.author));
 assert(book_epub_metadata_cached_for_file(source,1235,5678,title,sizeof(title),author,sizeof(author))==ESP_ERR_NOT_FOUND&&!title[0]&&!author[0]);
 assert(book_epub_metadata_cached_for_file(source,1234,5679,title,sizeof(title),author,sizeof(author))==ESP_ERR_NOT_FOUND);
 assert(book_epub_metadata_cached_for_file(source,UINT64_MAX,5678,title,sizeof(title),author,sizeof(author))==ESP_ERR_NOT_FOUND&&stats==0);
 assert(book_epub_metadata_cached_for_file("/flash/books/book.epub",1234,5678,title,sizeof(title),author,sizeof(author))==ESP_ERR_NOT_FOUND&&stats==0);
 assert(book_epub_metadata_cached_for_file(source,1234,5678,title,2,author,sizeof(author))==ESP_ERR_INVALID_SIZE);
 key.version++;write_cache(key,payload,sizeof(payload));assert(book_epub_metadata_cached_for_file(source,1234,5678,title,sizeof(title),author,sizeof(author))==ESP_ERR_NOT_FOUND);key.version--;
 write_cache(key,payload,10);assert(book_epub_metadata_cached_for_file(source,1234,5678,title,sizeof(title),author,sizeof(author))==ESP_ERR_NOT_FOUND);
 memset(payload.author,'x',sizeof(payload.author));write_cache(key,payload,sizeof(payload));assert(book_epub_metadata_cached_for_file(source,1234,5678,title,sizeof(title),author,sizeof(author))==ESP_ERR_NOT_FOUND);
 assert(book_epub_metadata_cached_for_file(NULL,1,2,title,sizeof(title),author,sizeof(author))==ESP_ERR_INVALID_ARG);
 assert(!unlink(physical));
 puts("PASS: same on-disk metadata key/payload, legacy1 stat versus directory reuse0; size/mtime/version/root/overflow/truncation/termination/capacity gates; no EPUB parse or cache writes");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(u)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),tmp],check=True)

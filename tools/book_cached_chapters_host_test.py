#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：真实索引计数读取、身份失效与损坏回退；不打开书源。
English: Actual cached chapter counting, identity invalidation and corruption fallback without opening a source.
"""
from pathlib import Path
import re,subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
def typedef(path,name):
    s=path.read_text();end=s.index('} '+name+';')+len('} '+name+';')
    start=list(re.finditer(r'typedef struct(?:\s+\w+)?\s*\{',s[:end]))[-1].start()
    return s[start:end]+'\n'
txt=R/'main/book/book_txt.c';epub=R/'main/book/book_epub.c';cache=R/'main/book/book_index_cache.c'
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include "book_index_cache.h"
#include "book_source_internal.h"
#include "book_epub.h"
static const char *directory;
static FILE *cache_open(const char *path,const char *mode){
 char mapped[1024];const char *name=strrchr(path,'/');assert(name);
 snprintf(mapped,sizeof(mapped),"%s/%s",directory,name+1);return fopen(mapped,mode);
}
static int cache_rename(const char *a,const char *b){char x[1024],y[1024];snprintf(x,sizeof(x),"%s/%s",directory,strrchr(a,'/')+1);snprintf(y,sizeof(y),"%s/%s",directory,strrchr(b,'/')+1);return rename(x,y);}
static int cache_remove(const char *a){char x[1024];snprintf(x,sizeof(x),"%s/%s",directory,strrchr(a,'/')+1);return remove(x);}
#define fopen cache_open
#define rename cache_rename
#define remove cache_remove
#define mkdir(p,m) (0)
'''
for path,names in [(txt,['TXT_CACHE_VERSION']),(epub,['EPUB_CACHE_VERSION','EPUB_CHAPTER_MAX','EPUB_NAV_MAX','EPUB_TITLE_CAP']),(cache,['CACHE_MAGIC'])]:
    s=path.read_text()
    for name in names:u+=re.search(r'^#define '+name+r' .+',s,re.M)[0]+'\n'
for path,name in [(txt,'txt_cache_payload_t'),(txt,'txt_nav_t'),(epub,'epub_cache_payload_t'),(epub,'chapter_t'),(epub,'nav_entry_t')]:u+=typedef(path,name)
for path,names in [(cache,['cache_hash','book_index_cache_prepare_file','book_index_cache_open_read','book_index_cache_open_write','book_index_cache_finish_write']),(txt,['index_hash','book_txt_cached_chapter_count']),(epub,['epub_cache_skip_text','book_epub_cached_chapter_count']),(R/'main/book/book_source.c',['chapter_summary_save','chapter_summary_read','book_cached_chapter_count'])]:
    for name in names:u+=function(name,path)+'\n'
u+=r'''
static uint16_t raw_count(const char*p,uint32_t n,int64_t m){if(!p)return 0;const char*e=strrchr(p,'.');return e&&!strcasecmp(e,".txt")?book_txt_cached_chapter_count(p,n,m):e&&!strcasecmp(e,".epub")?book_epub_cached_chapter_count(p,n,m):0;}
static char cache_path[112];
static const char *txt_path="/sdcard/books/a.TXT",*epub_path="/sdcard/books/b.epub";
static void txt_fixture(void){
 book_index_cache_header_t key;assert(book_index_cache_prepare_file(txt_path,"txt",TXT_CACHE_VERSION,100,42,cache_path,sizeof(cache_path),&key));
 txt_cache_payload_t p={.total=100,.count=2};book_entry_t entries[2]={{0,UINT32_MAX},{50,UINT32_MAX}};
 uint32_t h=index_hash(UINT32_C(2166136261),&p,sizeof(p));p.checksum=index_hash(h,entries,sizeof(entries));
 FILE *f=fopen(cache_path,"wb");assert(f);assert(fwrite(&key,sizeof(key),1,f)==1&&fwrite(&p,sizeof(p),1,f)==1&&fwrite(entries,sizeof(entries),1,f)==1);assert(!fclose(f));
}
static void epub_fixture(void){
 book_index_cache_header_t key;assert(book_index_cache_prepare_file(epub_path,"epub",EPUB_CACHE_VERSION,200,42,cache_path,sizeof(cache_path),&key));
 epub_cache_payload_t p={.count=2,.total=40};FILE *f=fopen(cache_path,"wb");assert(f);
 assert(fwrite(&key,sizeof(key),1,f)==1&&fwrite(&p,sizeof(p),1,f)==1);
 for(int i=0;i<2;++i){chapter_t entry={.zip_index=i,.offset=i*20};uint16_t n=5;assert(fwrite(&entry,sizeof(entry),1,f)==1&&fwrite(&n,sizeof(n),1,f)==1&&fwrite("Title",5,1,f)==1);}
 assert(!fclose(f));
}
static void patch(size_t offset,const void *bytes,size_t n){FILE *f=fopen(cache_path,"r+b");assert(f&&!fseek(f,(long)offset,SEEK_SET)&&fwrite(bytes,1,n,f)==n&&!fclose(f));}
int main(int argc,char **argv){
 assert(argc==2);directory=argv[1];assert(!raw_count(NULL,0,0));
 assert(!raw_count("/sdcard/books/missing.epub",100,42));
 txt_fixture();assert(raw_count(txt_path,100,42)==2);
 assert(!raw_count(txt_path,101,42)&&!book_cached_chapter_count(txt_path,100,43));
 assert(!raw_count("/sdcard/books/other.txt",100,42)&&!book_cached_chapter_count("/flash/a.txt",100,42));
 uint32_t bad=0;patch(sizeof(book_index_cache_header_t)+sizeof(txt_cache_payload_t),&bad,sizeof(bad));
 bad=1;patch(sizeof(book_index_cache_header_t)+sizeof(txt_cache_payload_t),&bad,sizeof(bad));assert(!raw_count(txt_path,100,42));
 txt_fixture();FILE *f=fopen(cache_path,"ab");assert(f&&fputc(0,f)!=EOF&&!fclose(f));assert(!raw_count(txt_path,100,42));
 txt_fixture();f=fopen(cache_path,"wb");assert(f&&!fclose(f));assert(!raw_count(txt_path,100,42));
 epub_fixture();assert(raw_count(epub_path,200,42)==2);
 assert(!raw_count(epub_path,201,42)&&!book_cached_chapter_count(epub_path,200,43));
 bad=99999;patch(sizeof(book_index_cache_header_t),&bad,sizeof(bad));assert(!raw_count(epub_path,200,42));
 epub_fixture();uint16_t huge=EPUB_TITLE_CAP+1;patch(sizeof(book_index_cache_header_t)+sizeof(epub_cache_payload_t)+sizeof(chapter_t),&huge,sizeof(huge));assert(!raw_count(epub_path,200,42));
 epub_fixture();f=fopen(cache_path,"ab");assert(f&&fputc(0,f)!=EOF&&!fclose(f));assert(!raw_count(epub_path,200,42));
 epub_fixture();f=fopen(cache_path,"wb");assert(f&&!fclose(f));assert(!raw_count(epub_path,200,42));
 assert(!raw_count("/sdcard/a.pdf",100,42));
 txt_fixture();assert(book_cached_chapter_count(txt_path,100,42)==2&&chapter_summary_read(txt_path,100,42)==2);
 // Remove the large original index: a warm summary still succeeds.
 assert(!remove(cache_path));assert(book_cached_chapter_count(txt_path,100,42)==2);
 assert(!book_cached_chapter_count(txt_path,101,42)&&!book_cached_chapter_count(txt_path,100,43));
 char summary[112];book_index_cache_header_t summary_key;assert(book_index_cache_prepare_file(txt_path,"chapters",1,100,42,summary,sizeof(summary),&summary_key));
 f=fopen(summary,"r+b");assert(f&&!fseek(f,sizeof(summary_key),SEEK_SET)&&fputc(7,f)!=EOF&&!fclose(f));
 assert(!book_cached_chapter_count(txt_path,100,42));txt_fixture();assert(book_cached_chapter_count(txt_path,100,42)==2);
 assert(chapter_summary_read(txt_path,100,42)==2);
 puts("PASS: tiny warm summaries without original index IO, summary corruption repair and source identity; real TXT/EPUB cached counts, no parser/source side effects, missing/stale/path/size/mtime/version-bounds/truncation/checksum/trailing corruption fallback");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-cached-chapters-') as folder:
    p=Path(folder);(p/'test.c').write_text(u)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'tools/book_epub_stubs'),'-I'+str(R/'main/book'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(p)],check=True)

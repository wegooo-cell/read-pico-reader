#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际TXT目录与旧分段、精确跳转、有界定位页、缓存和故障回归；可附私有TXT样本。
English: Actual TXT navigation, legacy segments, exact jumps, bounded pages, cache and fault regressions; optional private TXT input.
"""
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_index_cache.h"
#include "book_epub.h"
static struct {void *p;size_t n;} allocations[256];
static size_t live,peak,largest;
static unsigned fail_allocation,cache_hits;
static bool disable_cache,cache_writable=true;
void *heap_caps_malloc(size_t n,int caps){
 assert(caps==3);if(fail_allocation&&!--fail_allocation)return NULL;
 void *p=malloc(n);if(!p)return NULL;
 for(unsigned i=0;i<256;++i)if(!allocations[i].p){
  allocations[i].p=p;allocations[i].n=n;live+=n;if(live>peak)peak=live;if(n>largest)largest=n;return p;
 }assert(!"too many tracked allocations");return NULL;
}
static void tracked_free(void *p){
 if(!p)return;
 for(unsigned i=0;i<256;++i)if(allocations[i].p==p){live-=allocations[i].n;allocations[i].p=NULL;break;}
 free(p);
}
#define free tracked_free
#include "book_txt.c"
#include "book_source.c"
void html_text_free(html_text_t *text){(void)text;assert(!"TXT test entered EPUB cleanup");}
bool book_index_cache_prepare(const char *source,const char *tag,uint32_t version,char *path,size_t cap,book_index_cache_header_t *key){
 if(disable_cache)return false;
 assert(!strcmp(tag,"txt")&&version==7);struct stat st;assert(!stat(source,&st));
 *key=(book_index_cache_header_t){.magic=123,.version=version,.source_size=(uint32_t)st.st_size,.modified=st.st_mtime,.path_hash=456};
 return (size_t)snprintf(path,cap,"%s.idx",source)<cap;
}
bool book_index_cache_prepare_file(const char *source,const char *tag,uint32_t version,
 uint64_t size,int64_t modified,char *path,size_t cap,book_index_cache_header_t *key){
 return !strcmp(tag,"txt")&&book_index_cache_prepare(source,tag,version,path,cap,key)&&
        key->source_size==size&&key->modified==modified;
}
FILE *book_index_cache_open_read(const char *path,const book_index_cache_header_t *key){
 FILE *f=fopen(path,"rb");if(!f)return NULL;book_index_cache_header_t found;
 if(fread(&found,1,sizeof(found),f)!=sizeof(found)||found.magic!=key->magic||found.version!=key->version||found.source_size!=key->source_size||found.modified!=key->modified||found.path_hash!=key->path_hash){fclose(f);return NULL;}
 ++cache_hits;return f;
}
FILE *book_index_cache_open_write(const char *path,const book_index_cache_header_t *key,char *temp,size_t cap){
 if(!cache_writable)return NULL;
 assert((size_t)snprintf(temp,cap,"%s.tmp",path)<cap);FILE *f=fopen(temp,"wb");assert(f);
 assert(fwrite(key,1,sizeof(*key),f)==sizeof(*key));return f;
}
bool book_index_cache_finish_write(FILE *f,const char *temp,const char *path,bool ok){
 assert(!fclose(f));if(ok){assert(!rename(temp,path));return true;}remove(temp);return false;
}
static uint32_t word(FILE *f){uint32_t n;assert(fread(&n,1,4,f)==4);return n;}
static void verify(const char *path,const char *expected){
 assert(book_open(path)==ESP_OK);
 FILE *f=fopen(expected,"rb");assert(f);uint32_t count=word(f);
 if(book_navigation_count()!=count)fprintf(stderr,"COUNT %s expected%u actual%zu\n",path,count,book_navigation_count());
 assert(book_navigation_count()==count);
 for(size_t i=0;i<count;++i){
  uint32_t offset=word(f),bytes=word(f);char title[2048];assert(bytes<sizeof(title));
  assert(fread(title,1,bytes,f)==bytes);title[bytes]=0;
  char label[160];assert(book_navigation_title(i,label,sizeof(label))==ESP_OK);
  size_t n=bytes;if(n>=sizeof(label)){n=sizeof(label)-1;while(n&&((unsigned char)title[n]&0xc0)==0x80)--n;}
  assert(strlen(label)==n&&!memcmp(label,title,n));
  size_t chapter=book_navigation_chapter(i),source=book_navigation_source_offset(i);
  assert(chapter<book_chapter_count());
  assert(book_chapter_byte_offset(chapter)+source==offset);
  if(i==0||i+1==count||i%97==0){
   html_text_t text={0};size_t target=SIZE_MAX;
   assert(book_chapter_load_blocks_target(chapter,NULL,source,&target,&text)==ESP_OK);
   assert(target<=text.len);assert(book_navigation_position(chapter,target)==i);
   const char *at=skip_space(text.utf8+target);
   size_t prefix=bytes<40?bytes:40;
   assert((size_t)(text.utf8+text.len-at)>=prefix&&!memcmp(at,title,prefix));
   bool found=false;
   for(size_t j=0;j<text.count;++j)
    if(text.blocks[j].offset==target&&text.blocks[j].reader_heading==BOOK_HEADING_TXT)found=true;
   if(strcspn(text.utf8+target,"\r\n")<=TXT_LINE_BYTES){
    bool information=strstr(path,"list-only")||!strcmp(title,"简介")||!strcmp(title,"前言")||!strcmp(title,"序言");
    if(found==information)fprintf(stderr,"HEADING %s %s found=%d information=%d\n",path,title,found,information);
    assert(found==!information);
   }
   free(text.blocks);
   free(text.utf8);
  }
 }
 uint32_t legacy=word(f);
 if(legacy){assert(book_chapter_count()==legacy);for(size_t i=0;i<legacy;++i)assert(book_chapter_byte_offset(i)==word(f));}
 assert(fgetc(f)==EOF&&!fclose(f));
 assert(book_navigation_chapter(count)==SIZE_MAX&&book_navigation_source_offset(count)==SIZE_MAX);
 size_t pages=0;for(size_t i=0;i<TXT_NAV_PAGES;++i)pages+=s_book.navigation[i]!=NULL;
 assert(pages==(s_book.nav_count+TXT_NAV_PAGE-1)/TXT_NAV_PAGE);
 assert(pages*TXT_NAV_PAGE*sizeof(txt_nav_t)<=128*1024);
 if(count>8){
  char title[160];assert(book_navigation_title(0,title,sizeof(title))==ESP_OK);
#if KIIKO_BOARD_METALIO47
  assert(book_suspend_io()==ESP_OK);assert(!s_book.file);assert(book_navigation_title(0,title,sizeof(title))==ESP_OK);
  if(count>18)assert(book_navigation_title(count/2,title,sizeof(title))==ESP_FAIL);
  assert(book_resume_io()==ESP_OK);
#endif
  char small[13];assert(book_navigation_title(count-1,small,sizeof(small))==ESP_OK);
  assert(strlen(small)<sizeof(small));
 }
 printf("TXT %s: %zu reading segments, %u navigation entries, %zu location bytes, peak %zu bytes\n",path,book_chapter_count(),count,pages*TXT_NAV_PAGE*sizeof(txt_nav_t),peak);
 book_close();assert(!live);
}
int main(int argc,char **argv){
 assert(argc==2);FILE *manifest=fopen(argv[1],"r");assert(manifest);
 char path[256],expected[272],last[256]={0};unsigned fixtures=0;
 while(fscanf(manifest,"%255s %271s",path,expected)==2){
  verify(path,expected);unsigned hits=cache_hits;verify(path,expected);assert(cache_hits>hits);
  char cache[272];snprintf(cache,sizeof(cache),"%s.idx",path);
  FILE *f=fopen(cache,"r+b");assert(f&&!fseek(f,sizeof(book_index_cache_header_t)+offsetof(txt_cache_payload_t,checksum),SEEK_SET));
  int c=fgetc(f);assert(c!=EOF&&!fseek(f,-1,SEEK_CUR)&&fputc(c^1,f)!=EOF&&!fclose(f));
  verify(path,expected);
  struct stat st;assert(!stat(cache,&st)&&st.st_size>3&&!truncate(cache,st.st_size-3));
  verify(path,expected);strcpy(last,path);++fixtures;
 }
 assert(!fclose(manifest)&&fixtures>=5);
 char cache[272];snprintf(cache,sizeof(cache),"%s.idx",last);assert(!remove(cache));
 cache_writable=false;verify(last,expected);cache_writable=true;
 // 第三个分配起为定位页，失败不能导致持续扩容、泄漏或丢掉正文阅读。
 // Location-page allocation failures must not cause growth loops, leaks or loss of body reading.
 disable_cache=true;
 for(unsigned fail=1;fail<=6;++fail){
  fail_allocation=fail;esp_err_t err=book_open(last);fail_allocation=0;
  assert(err==ESP_OK||err==ESP_ERR_NO_MEM);
  if(err==ESP_OK){char *out=NULL;size_t n=0;assert(book_chapter_load(0,&out,&n)==ESP_OK);free(out);}
  book_close();assert(!live);
 }
 assert(book_open(last)==ESP_OK);size_t baseline=live;
 for(unsigned fail=1;fail<=2;++fail){
  char *out=(char *)1;size_t n=9;fail_allocation=fail;
  assert(book_chapter_load(0,&out,&n)==ESP_ERR_NO_MEM&&!out&&!n);fail_allocation=0;assert(live==baseline);
 }
 fail_allocation=1;char label[160];assert(book_navigation_title(0,label,sizeof(label))==ESP_OK);fail_allocation=0;
 assert(!s_book.titles);book_close();assert(!live);
 // 超过定位页硬上限继续按旧分段读正文，绝不无限增加目录内存。
 // Over-cap navigation retains legacy body segments and never grows index memory indefinitely.
 FILE *f=fopen(last,"wb");assert(f);
 for(unsigned i=1;i<TXT_NAV_MAX+20;++i)fprintf(f,"Chapter %u\nbody\n\n",i);
 assert(!fclose(f)&&book_open(last)==ESP_OK&&s_book.nav_incomplete&&!s_book.nav_count);
 assert(book_chapter_count()>0&&book_chapter_count()<=BOOK_CHAPTER_MAX);
 book_close();assert(!live&&peak<512*1024);
 printf("PASS: %u fixtures,full offset/title mapping,exact UTF8/GBK/UTF16 targets,legacy progress boundaries,cache checksum/truncation,reopen,card pause,OOM and128KiB location cap; peak%zu/max allocation%zu\n",fixtures,peak,largest);
}
'''

def legacy_utf8_offsets(data):
    """Independent legacy fallback for the supplied >2048-heading UTF8 fixture."""
    starts = [0]
    part = 0
    for match in re.finditer(b'\n', data):
        end = match.end()
        if end - part >= 48 * 1024 and end < len(data):
            assert end-part < 96*1024
            starts.append(end)
            part = end
    return starts


def expected_file(path, text, codec, bom, titles, legacy=(), separated_only=False, min_offset=0):
    positions = []
    offset = len(bom)
    separated=False
    for line in text.splitlines(keepends=True):
        title = line.strip()
        if title in titles and offset>=min_offset and (not separated_only or separated):
            positions.append((offset, title))
        if len(title)>=6 and set(title)=={'-'}:
            separated=True
        elif title:
            separated=False
        offset += len(line.encode(codec))
    result = Path(str(path)+'.expected')
    with result.open('wb') as out:
        out.write(struct.pack('<I',len(positions)))
        for position,title in positions:
            b=title.encode();out.write(struct.pack('<II',position,len(b)));out.write(b)
        out.write(struct.pack('<I',len(legacy)))
        for n in legacy:out.write(struct.pack('<I',n))
    return result


with tempfile.TemporaryDirectory(prefix='kiiko-txt-nav-') as folder:
    t=Path(folder)
    (t/'esp_heap_caps.h').write_text('#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,int);\n')
    rows=[]
    originals=[]
    examples=[
        ('long','\n'.join(['简介','正文','☆、第一章\u3000今天吃咖喱','正文',' 第01章 '+('长章名'*35),'正文','chapter 2 End','正文','后记','正文'])+'\n'),
        ('numbers','\n\n'.join([f'{i}、标题{i}\n正文普通内容' for i in range(1,9)])+'\n'),
        ('bare','\n\n'.join([f'{i}\n正文普通内容' for i in range(1,9)])+'\n'),
        ('separators','\n\n'.join(['------------\n明月在天','------------\n第一章 开始','正文','------------\n第五章 你如果经历过我所经历的一切','正文','第一章我一个字都没动，这是作者正文说明。','------------\n番外·除夕（平等国篇）','正文'])+'\n'),
        ('many',''.join(f'------------\n\n第{i}章 这是很长很长且超过四十字节的标题{i}\n\n正文\n\n' for i in range(1,6001))),
    ]
    for name,text in examples:
        titles={l.strip() for l in text.splitlines() if l.strip() and l.strip() not in {'正文','正文普通内容','------------'} and not l.startswith('第一章我')}
        p=t/(name+'.txt');p.write_bytes(text.encode());e=expected_file(p,text,'utf-8',b'',titles);rows.append((p,e))
    extra_cases=[
        ('roman', 'Part IV: Journey\nbody\nChapter XII Beginning\nbody\nchapter xiii. Return\nbody\nEpilogue\nbody\nAppendix A: Notes\nbody\n',
         {'Part IV: Journey','Chapter XII Beginning','chapter xiii. Return','Epilogue','Appendix A: Notes'},0),
        ('markdown', '# 起程\n正文\n## Chapter of memories\n正文\n### 最后一节\n正文\n#### 四级不索引\n#include <stdio.h>\n#nospace\n',
         {'# 起程','## Chapter of memories','### 最后一节'},0),
        ('units', '序章\n正文\n\n第廿一册：远方\n正文\n\n第一章起程\n正文\n\n附录：资料\n正文\n',
         {'序章','第廿一册：远方','第一章起程','附录：资料'},0),
        ('false-positives', 'Chapter 1 Start\nbody\nChapter 123abc describes a variable\nChapter CIVIL is prose\nChapter IIII is invalid\n第一章我一个字都没动，这是正文。\nChapter 2 End\nbody\n',
         {'Chapter 1 Start','Chapter 2 End'},0),
        ('front-list', 'Contents\n\nChapter I Beginning\nChapter II End\n\nChapter I Beginning\nactual body\nChapter II End\nactual body\n',
         {'Chapter I Beginning','Chapter II End'},len('Contents\n\nChapter I Beginning\nChapter II End\n\n'.encode())),
        ('list-only', '目录\n第一章 开始\n第二章 结束\n', {'第一章 开始','第二章 结束'},0),
        ('front-after-title', '作品书名\n作者名字\n目录\n第一章 开始\n第二章 结束\n\n第一章 开始\n实际正文。\n第二章 结束\n实际正文。\n',
         {'第一章 开始','第二章 结束'},len('作品书名\n作者名字\n目录\n第一章 开始\n第二章 结束\n\n'.encode())),
        ('single-chapter', '序言里的文字。\r\n\r\n第一章 唯一的一章\r\n正文。\r\n', {'第一章 唯一的一章'},0),
        ('chinese-numbers', '一、启程\n正文\n\n二、过桥\n正文\n\n三、归来\n正文\n', {'一、启程','二、过桥','三、归来'},0),
    ]
    for name,text,titles,minimum in extra_cases:
        p=t/(name+'.txt');p.write_bytes(text.encode());e=expected_file(p,text,'utf-8',b'',titles,min_offset=minimum);rows.append((p,e))
    enc_text='\ufeff'.lstrip('\ufeff')+'------------\r\n第一章 开始\r\n中文前言😀\r\n------------\r\n第2章 继续阅读\r\n正文\r\n'
    codecs=[('utf-8',b'\xef\xbb\xbf'),('gbk',b'')]
    s31='b->utf16' in (ROOT/'main/book/book_txt.c').read_text()
    if s31:
        codecs += [('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff')]
    for codec,bom in codecs:
        text=enc_text.replace('😀','好') if codec=='gbk' else enc_text
        p=t/(codec+'.txt');p.write_bytes(bom+text.encode(codec));e=expected_file(p,text,codec,bom,{'第一章 开始','第2章 继续阅读'});rows.append((p,e))
    for index,source in enumerate(map(Path,sys.argv[1:])):
        raw=source.read_bytes();digest=hashlib.sha256(raw).hexdigest();text=raw.decode('utf-8-sig');originals.append((source,digest))
        p=t/f'private-{index}.txt';p.write_bytes(raw)
        titles=set();separated=False
        for line in text.splitlines():
            if len(line.strip())>=6 and set(line.strip())=={'-'}:separated=True
            elif line.strip():
                if separated:titles.add(line.strip())
                separated=False
        e=expected_file(p,text,'utf-8',b'',titles,legacy_utf8_offsets(raw),True);rows.append((p,e))
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
    header=(ROOT/'main/book/book_epub.h').read_text()
    stubs=[]
    for m in re.finditer(r'^(?:const char\s*\*|size_t|uint32_t|uint16_t|bool|void|int|esp_err_t)\s*book_epub_\w+\([^;]*\);',header,re.M):
        proto=m.group()[:-1];ret=proto.split('book_epub_')[0].strip()
        stubs.append(proto+' {'+('' if ret=='void' else 'return 0;')+'}')
    c=t/'test.c';c.write_text(UNIT+'\n'+'\n'.join(stubs))
    manifest=t/'manifest';manifest.write_text(''.join(f'{p} {e}\n' for p,e in rows))
    exe=t/'test'
    subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-DKIIKO_BOARD_METALIO47='+str(int(s31)),'-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g','-fsanitize=address,undefined',
        '-I'+str(t),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'tools/book_epub_stubs'),
        str(c),str(ROOT/'main/book/gbk.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(manifest)],check=True)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in originals)

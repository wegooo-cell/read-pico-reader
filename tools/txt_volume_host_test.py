#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际分卷、原偏移、目录展开、缓存/OOM与可选私有TXT验证。
English: Actual volume parsing, source positions, accordion, cache/OOM and optional private TXT checks.
"""
from pathlib import Path
import ast
import hashlib
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def constant(path, name):
    tree = ast.parse(path.read_text())
    for item in tree.body:
        if isinstance(item, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in item.targets):
            return ast.literal_eval(item.value)
    raise ValueError(name)

base = constant(ROOT/'tools/txt_navigation_host_test.py', 'UNIT').split('int main(int argc,char **argv){')[0]
# UI章节类型可切换；真实书源的同名出口单独保留，避免测试桩覆盖实现。
# Keep the real source kind outlet separate from the switchable UI fixture.
base = base.replace('#include "book_source.c"', '#define book_kind source_book_kind\n#include "book_source.c"\n#undef book_kind')
base = base.replace('static unsigned fail_allocation,cache_hits;', 'static unsigned fail_allocation,cache_hits; static bool fail_volumes;')
base = base.replace('assert(caps==3);', 'assert(caps==3);if(fail_volumes&&n==2048)return NULL;')
base = base.replace('assert(book_navigation_count()==count);', 'if(book_navigation_count()!=count){for(size_t g=0;g<book_navigation_volume_count();++g){char title[160];book_navigation_volume_title(g,title,sizeof(title));fprintf(stderr,"GROUP%zu: %s\\n",g,title);}}assert(book_navigation_count()==count);')

ui = constant(ROOT/'tools/test_book_toc.py', 'HARNESS').split('size_t book_navigation_volume_count(void){')[0].split('esp_err_t book_navigation_title(')[0]
ui = ui.replace('if(px==30){assert(background==0xc0||background==255);', 'if(px==30){assert(background==0xc0||background==255||background==0xe0||background==0xd8);')
ui = ui.replace('else assert(background==0xe0);', 'else assert(background==0xe0||background==0xd8||background==255||background==0xc0);')
ui = ui.replace('int ui_text_effective_px(int px){return px;}', 'static int test_scale=100;int ui_text_effective_px(int px){return px*test_scale/100;}')

HARNESS = r'''
static void check_groups(const char *path,const char *expected){
 assert(book_open(path)==ESP_OK);
 FILE *f=fopen(expected,"rb");assert(f);unsigned count=word(f);
 if(book_navigation_volume_count()!=count)fprintf(stderr,"VOLUMES %s expected%u actual%zu\n",path,count,book_navigation_volume_count());
 assert(book_navigation_volume_count()==count);
 for(size_t i=0;i<count;++i){
  book_volume_t v;assert(book_navigation_volume(i,&v));
  size_t begin=word(f),end=word(f),title=word(f),number=word(f);
  if(title==UINT32_MAX)title=SIZE_MAX;
  assert(v.begin==begin&&v.end==end&&v.title==title&&v.number==number);
  char name[160];assert(book_navigation_volume_title(i,name,sizeof(name))==ESP_OK&&*name);
  for(size_t j=begin;j<end;++j)assert(book_navigation_volume_for(j)==i);
  size_t child=begin+(title==begin);
  for(size_t j=child;j<end;++j){
   int page=book_toc_open(j);assert(page>=0&&page<book_toc_pages(book_navigation_count()));
   assert(s_open_volume==i);
   size_t row=i+1+j-child;
   assert(page==(int)(row/BOOK_TOC_ROWS));
   assert(visible_target(row,book_navigation_count())==(int)j);
   assert(book_toc_hit(100,TOC_ROW_TOP+(int)(row%BOOK_TOC_ROWS)*TOC_ROW_STEP+10,page,book_navigation_count())==(int)j);
  }
  if(title!=SIZE_MAX)assert(visible_target(i,book_navigation_count())==TOC_VOLUME_HIT-(int)i);
 }
 assert(book_navigation_volume_for(book_navigation_count())==SIZE_MAX);
 book_volume_t invalid;assert(!book_navigation_volume(count,&invalid)&&!book_navigation_volume(0,NULL));
 assert(fgetc(f)==EOF&&!fclose(f));
 if(count){
  int page=book_toc_open(0);uint8_t fb=0;
  for(int scale=100;scale<=150;scale+=50){test_scale=scale;book_toc_render(&fb,"书名",book_navigation_count(),0,page,"");book_toc_render_jump(&fb,book_navigation_count(),100);}
  test_scale=100;
  for(size_t i=0;i<count;++i){
   assert(book_toc_toggle(TOC_VOLUME_HIT-(int)i,&page));
   assert(s_open_volume==i||s_open_volume==SIZE_MAX);
   assert(page==(int)(i/BOOK_TOC_ROWS));
   assert(book_toc_hit(100,TOC_ROW_TOP+(int)(i%BOOK_TOC_ROWS)*TOC_ROW_STEP+10,page,book_navigation_count())==TOC_VOLUME_HIT-(int)i);
   if(s_open_volume!=SIZE_MAX)assert(book_toc_toggle(TOC_VOLUME_HIT-(int)i,&page));
   assert(s_open_volume==SIZE_MAX&&visible_count(book_navigation_count())==count);
  }
  assert(!book_toc_toggle(TOC_VOLUME_HIT-(int)count,&page)&&!book_toc_toggle(-1,&page)&&!book_toc_toggle(TOC_VOLUME_HIT,NULL));
  book_volume_t last;assert(book_navigation_volume(count-1,&last));
  size_t selected=last.end-1;page=book_toc_open(selected);int saved=page;
  assert(book_toc_hit(100,1130,page,book_navigation_count())==BOOK_TOC_SELECT_VOLUME);
  assert(book_toc_hit(400,1130,page,book_navigation_count())==BOOK_TOC_JUMP);
  assert(book_toc_hit(240,1130,page,book_navigation_count())==-1);
  long cursor=ftell(s_book.file);size_t allocated=live;
  assert(book_toc_picker_toggle(&page)&&s_volume_picker);
  assert(page==(int)((count-1)/BOOK_TOC_ROWS));
  assert(visible_count(book_navigation_count())==count);
  assert(book_toc_pages(book_navigation_count())==(int)((count+BOOK_TOC_ROWS-1)/BOOK_TOC_ROWS));
  for(size_t i=0;i<count;++i){
   assert(visible_target(i,book_navigation_count())==TOC_VOLUME_HIT-(int)i);
   assert(book_toc_hit(100,TOC_ROW_TOP+(int)(i%BOOK_TOC_ROWS)*TOC_ROW_STEP+10,(int)(i/BOOK_TOC_ROWS),book_navigation_count())==TOC_VOLUME_HIT-(int)i);
  }
  assert(visible_target(count,book_navigation_count())==-1);
  assert(book_toc_hit(600,1130,page,book_navigation_count())==BOOK_TOC_SELECT_VOLUME);
  assert(book_toc_hit(58,110,page,book_navigation_count())==BOOK_TOC_BACK);
  assert(book_toc_picker_close(&page)&&page==saved&&s_open_volume==count-1);
  assert(!book_toc_picker_close(&page));
  assert(book_toc_picker_toggle(&page));
  assert(book_toc_toggle(TOC_VOLUME_HIT-(int)(count-1),&page));
  assert(!s_volume_picker&&s_open_volume==count-1); // 再选原卷也保持展开。/ Reselecting the same volume keeps it expanded.
  assert(book_toc_picker_toggle(&page)&&book_toc_toggle(TOC_VOLUME_HIT,&page));
  assert(!s_volume_picker&&s_open_volume==0&&page==0);
  assert(ftell(s_book.file)==cursor&&live==allocated); // 选择只改可见行，不读正文、不分配。/ Selection changes visible rows without body IO or allocation.
  assert(book_toc_picker_toggle(&page));
  for(int scale=100;scale<=150;scale+=50){test_scale=scale;book_toc_render(&fb,"书名",book_navigation_count(),selected,page,"");}
  test_scale=100;
  assert(book_toc_picker_toggle(&page)&&!s_volume_picker);
  assert(book_toc_picker_toggle(&page));book_toc_open(selected);assert(!s_volume_picker);
  assert(!book_toc_picker_toggle(NULL)&&!book_toc_picker_close(NULL));
 }
 printf("VOLUME %s: %zu groups, %zu source navigation entries, bounded2048B metadata\n",path,book_navigation_volume_count(),book_navigation_count());
 book_close();assert(!live);
}
static void damage_group_cache(const char *path){
 char cache[512];snprintf(cache,sizeof(cache),"%s.idx",path);FILE *f=fopen(cache,"r+b");assert(f);
 book_index_cache_header_t key;txt_cache_payload_t p;
 assert(fread(&key,1,sizeof(key),f)==sizeof(key)&&fread(&p,1,sizeof(p),f)==sizeof(p));
 size_t n=p.count*sizeof(book_entry_t)+p.nav_count*sizeof(txt_nav_t)+(p.reserved[1]+((unsigned)p.reserved[2]<<8))*sizeof(book_txt_volume_t);
 unsigned char *data=malloc(n);assert(data&&fread(data,1,n,f)==n);
 book_txt_volume_t *groups=(book_txt_volume_t *)(data+p.count*sizeof(book_entry_t)+p.nav_count*sizeof(txt_nav_t));
 groups[0].end=UINT16_MAX;
 p.checksum=0;uint32_t h=index_hash(UINT32_C(2166136261),&p,sizeof(p));p.checksum=index_hash(h,data,n);
 assert(!fseek(f,sizeof(key),SEEK_SET)&&fwrite(&p,1,sizeof(p),f)==sizeof(p)&&fwrite(data,1,n,f)==n&&!fclose(f));free(data);
}
int main(int argc,char **argv){
 assert(argc==3);FILE *f=fopen(argv[1],"r");assert(f);char path[256],expected[272],groups[272],first[256]={0};unsigned fixtures=0;
 assert(navigation_numbering("第一卷总结兼感言")==0);
 assert(navigation_numbering("第一卷（完）")==0);
 assert(navigation_numbering("【第二卷·终】")==0);
 assert(navigation_numbering("第二卷 终局")==((2u<<NAV_ORDINAL_SHIFT)|NAV_VOLUME));
 assert(navigation_numbering("第二卷的开始，我大概展现了这个世界。")==0);
 assert(navigation_numbering("第廿一册：远方")==((21u<<NAV_ORDINAL_SHIFT)|NAV_VOLUME));
 assert(navigation_numbering("Chapter XII Start")==((12u<<NAV_ORDINAL_SHIFT)|NAV_CHAPTER|NAV_ENGLISH));
 while(fscanf(f,"%255s %271s %271s",path,expected,groups)==3){
  verify(path,expected);check_groups(path,groups);unsigned hits=cache_hits;check_groups(path,groups);assert(cache_hits>hits);
  if(!fixtures)strcpy(first,path);
  if(strstr(path,"explicit")){damage_group_cache(path);check_groups(path,groups);}
  ++fixtures;
 }
 assert(!fclose(f)&&fixtures>=10);
 disable_cache=true;fail_volumes=true;
 assert(book_open(first)==ESP_OK&&book_navigation_count()>0&&!book_navigation_volume_count()&&s_book.volumes_incomplete);
 char name[160];assert(book_navigation_title(0,name,sizeof(name))==ESP_OK);book_close();fail_volumes=false;assert(!live);
 // 卷数超限只回退平铺；索引和正文仍可读，不缓存残缺卷表。
 // Excess volumes fall back to flat navigation without losing body/index access or caching partial groups.
 assert(book_open(argv[2])==ESP_OK&&s_book.volumes_incomplete&&!book_navigation_volume_count());
 assert(book_navigation_count()==257*4);char *text=NULL;size_t len;
 assert(book_chapter_load(0,&text,&len)==ESP_OK&&len);free(text);book_close();assert(!live);
 printf("PASS: %u volume fixtures, real parser/source/TOC, duplicate chapter numbers, cache structural corruption with valid checksum, OOM/cap fallback, full source offsets, volume-only picker select/reselect/cancel/paging/no body IO or allocation,100/150%% title scaling\n",fixtures);
}
'''

def write_expected(path, text, codec, bom, titles, volumes):
    offset=len(bom);records=[]
    for line in text.splitlines(keepends=True):
        if line.strip() in titles: records.append((offset,line.strip()))
        offset+=len(line.encode(codec))
    expected=Path(str(path)+'.expected')
    with expected.open('wb') as f:
        f.write(struct.pack('<I',len(records)))
        for off,title in records:
            b=title.encode();f.write(struct.pack('<II',off,len(b)));f.write(b)
        f.write(struct.pack('<I',0))
    groups=Path(str(path)+'.groups')
    with groups.open('wb') as f:
        f.write(struct.pack('<I',len(volumes)))
        for v in volumes:f.write(struct.pack('<IIII',*v))
    return expected,groups

with tempfile.TemporaryDirectory(prefix='kiiko-volumes-') as tmp:
    t=Path(tmp);rows=[];originals=[]
    (t/'esp_heap_caps.h').write_text('#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,int);\n')
    for name,content in constant(ROOT/'tools/test_book_toc.py','STUBS').items():
        if name!='book_source.h':(t/name).write_text(content)
    AUTO=0xffffffff
    def add(name,titles,groups,codec='utf-8',bom=b'',body='正文普通内容',source=None):
        text='\n\n'.join(title+'\n'+body for title in titles)+'\n'
        if source is not None:text=source.decode(codec)
        path=t/(name+'.txt');path.write_bytes(bom+text.encode(codec) if source is None else source)
        expected,g=write_expected(path,text,codec,bom,set(titles),groups);rows.append((path,expected,g))
    a=['第1卷 明月在天']+[f'第{i}章 章节{i}' for i in range(1,7)]+['第一卷总结兼感言']
    b=['第2卷 向前']+[f'第{i}章 章节{i}' for i in range(1,5)]
    add('explicit',a+b,[(0,len(a),0,1),(len(a),len(a+b),len(a),2)])
    add('front-explicit',['序章']+a+b,[(0,1,AUTO,0),(1,1+len(a),1,1),(1+len(a),1+len(a+b),1+len(a),2)])
    a=[f'第{i}章 标题{i}' for i in range(1,7)]+['第一卷总结兼感言']
    b=[f'第{i}章 标题{i}' for i in range(1,5)]
    add('restart',a+b,[(0,len(a),AUTO,1),(len(a),len(a+b),AUTO,2)])
    add('large-explicit-ordinal',['第65535卷 远方']+a+b,[(0,1+len(a),0,65535),(1+len(a),1+len(a+b),AUTO,2)])
    mixed=['第1卷 开始']+[f'第{i}章 开始' for i in range(1,7)]+['第2卷 终局']+[f'第{i}章 开始' for i in range(1,7)]
    mixed_text='\n\n'.join(('' if '卷' in t else '------------\n')+t+'\n正文' for t in mixed)+'\n'
    add('separated-explicit',mixed,[(0,7,0,1),(7,14,7,2)],source=mixed_text.encode())
    reduced=''
    for i,title in enumerate(a+b):
        reduced+='------------\n'+title+'\n正文\n\n'
        if i==len(a)-1:reduced+='【第一卷·终】\n正文\nPart1:我为什么要写这篇文章\n说明\nPart3：成绩说明\n说明\n'
    add('author-note-parts',a+b,[(0,len(a),AUTO,1),(len(a),len(a+b),AUTO,2)],source=reduced.encode())

    add('bad-sequence',a+['第一章 插叙','第二章 插叙','第四章 插叙'],[])
    add('short-before-reset',b+b,[])
    add('single-repeated',a+['第一章 插叙'],[])
    add('skipped-before-reset',[f'第{i}章 标题' for i in [1,2,3,8,10,1,2,3]],[(0,5,AUTO,1),(5,8,AUTO,2)])
    add('notes-stay',a+b+['第二卷总结兼感言','番外 附录'],[(0,len(a),AUTO,1),(len(a),len(a+b)+2,AUTO,2)])
    add('roman',['Part I: Journey','Chapter I Start','Chapter II Continue','Part IV: Return','Chapter I Start','Chapter II Continue'],[(0,3,0,1),(3,6,3,4)],body='Body text')
    add('markdown',['# 第一卷 开始','## 第1章 标题','## 第2章 标题','# 第二卷 远方','## 第1章 标题','## 第2章 标题'],[(0,3,0,1),(3,6,3,2)])
    for codec,bom in [('gbk',b''),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff')]:
        if codec.startswith('utf-16') and '--s31' not in sys.argv:continue
        add(codec,a+b,[(0,len(a),AUTO,1),(len(a),len(a+b),AUTO,2)],codec,bom)
    for index,arg in enumerate(a for a in sys.argv[1:] if not a.startswith('--')):
        source=Path(arg);raw=source.read_bytes();originals.append((source,hashlib.sha256(raw).hexdigest()))
        text=raw.decode('utf-8-sig');titles=[];separated=False;offset=0
        for line in text.splitlines(keepends=True):
            title=line.strip()
            if len(title)>=6 and set(title)=={'-'}:separated=True
            elif title:
                if separated:titles.append(title)
                separated=False
        starts=[i for i,title in enumerate(titles) if re.match(r'^第一章[\s\u3000\xa0]',title)]
        assert len(starts)==16 and starts[0]==1
        # 明月在天卷前的首个独立题头归第一卷；不从正文猜测卷名。
        # Keep the isolated leading heading in volume1; never guess volume names from prose.
        firsts=[0]+starts[1:]
        groups=[(n,firsts[i+1] if i+1<len(firsts) else len(titles),AUTO,i+1) for i,n in enumerate(firsts)]
        path=t/f'private-{index}.txt';path.write_bytes(raw)
        expected,g=write_expected(path,text,'utf-8',b'',set(titles),groups)
        # 只采集分隔线后的真实目录项，正文中偶然同名不进入期望表。
        # Expected navigation only includes separator heads, not coincident prose titles.
        offset=0;records=[];separated=False
        for line in text.splitlines(keepends=True):
            title=line.strip()
            if len(title)>=6 and set(title)=={'-'}:separated=True
            elif title:
                if separated:records.append((offset,title))
                separated=False
            offset+=len(line.encode())
        with expected.open('wb') as f:
            f.write(struct.pack('<I',len(records)))
            for off,title in records:
                z=title.encode();f.write(struct.pack('<II',off,len(z)));f.write(z)
            f.write(struct.pack('<I',0))
        rows.append((path,expected,g))
    cap=t/'cap.txt';cap.write_text(''.join(f'第{v}卷 远方\n正文\n\n'+''.join(f'第{i}章 标题\n正文\n\n' for i in range(1,4)) for v in range(1,258)))
    manifest=t/'manifest';manifest.write_text(''.join(f'{p} {e} {g}\n' for p,e,g in rows))
    header=(ROOT/'main/book/book_epub.h').read_text();stubs=[]
    for m in re.finditer(r'^(?:const char\s*\*|size_t|uint32_t|uint16_t|bool|void|int|esp_err_t)\s*book_epub_\w+\([^;]*\);',header,re.M):
        proto=m.group()[:-1];ret=proto.split('book_epub_')[0].strip();stubs.append(proto+' {'+('' if ret=='void' else 'return 0;')+'}')
    c=t/'test.c';c.write_text(base+'\n'+ui+'\n'+HARNESS+'\n'+'\n'.join(stubs));exe=t/'test'
    subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-DKIIKO_BOARD_METALIO47='+str(int('--s31' in sys.argv)),'-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-I'+str(t),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main/apps'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'tools/book_epub_stubs'),str(c),str(ROOT/'main/book/gbk.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(manifest),str(cap)],check=True)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in originals)

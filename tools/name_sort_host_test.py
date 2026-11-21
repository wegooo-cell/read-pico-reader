#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run actual pinyin/natural-name, shelf and file comparators with independent oracles."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
from home_cover_cache_host_test import function

R = Path(__file__).resolve().parents[1]
folder = R / 'components/read_pico_search'
board = int(os.environ.get('KIIKO_TEST_BOARD') == 'metalio47')
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include PRODUCTION_SOURCE
typedef struct {char name[256],path[288];bool favorite,has_progress,search_match,selected,is_dir;unsigned pct,recent;uint64_t added;} shelf_entry_t;
static unsigned s_shelf_order;
static bool s_manage_books,s_import_candidates;
static bool shelf_matches(const shelf_entry_t *item){return item->search_match;}
'''
for name in ['shelf_order_mode', 'shelf_order_desc', 'compare_books']:
    unit += function(name, R/'main/apps/app_book.c') + '\n'
unit += r'''
typedef struct {char path[288];int64_t size,modified;bool is_dir,selected;} file_item_t;
static int s_file_sort;static bool s_file_desc;
'''
for name in ['file_item_name', 'compare_files']:
    unit += function(name, R/'main/apps/app_files.c') + '\n'
unit += r'''
static int sign(int x){return (x>0)-(x<0);}
static int by_name(const void *a,const void *b){return read_pico_name_compare(*(const char*const*)a,*(const char*const*)b);}
static uint32_t random_state=42;
static unsigned next_random(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static const char *names[]={"2号","10号","Alice","被讨厌的勇气","边城","红楼梦","活着","呐喊","三体","我与地坛","夏天、烟火和我的尸体","乡土中国","月亮与六便士","追风筝的人"};
int main(int argc,char **argv){
 if(argc>1){assert(!strcmp(argv[1],"dictionary"));
  for(size_t i=0;i<SEARCH_CODEPOINT_COUNT;++i){uint32_t cp=search_codepoint_at(i);printf("%u:%s\n",cp,sort_reading(cp,lookup(cp)));}return 0;}
 const size_t n=sizeof(names)/sizeof(names[0]);
 for(size_t i=0;i<n;++i)for(size_t j=0;j<n;++j)assert(sign(read_pico_name_compare(names[i],names[j]))==sign((int)i-(int)j));
 assert(read_pico_name_compare("书2","书10")<0);
 assert(read_pico_name_compare("书0002","书10")<0);
 assert(read_pico_name_compare("书999999999999999999999","书1000000000000000000000")<0);
 assert(read_pico_name_compare("《夏天》","乡土中国")<0);
 assert(read_pico_name_compare(" Ａlpha ","beta")<0);
 const char *edges[]={"","  ","《》","a","A","02","2","0","000","0a","00b","2a","2b","10","书2.1","书21","《书2》","夏天","𠀀","😀","\xff","\xf0\x80\x80","a\xc2"};
 size_t e=sizeof(edges)/sizeof(edges[0]);
 for(size_t i=0;i<e;++i)for(size_t j=0;j<e;++j)for(size_t k=0;k<e;++k){
  int ab=read_pico_name_compare(edges[i],edges[j]),bc=read_pico_name_compare(edges[j],edges[k]);
  assert(sign(ab)==-sign(read_pico_name_compare(edges[j],edges[i])));
  if(ab<=0&&bc<=0)assert(read_pico_name_compare(edges[i],edges[k])<=0);
 }
 shelf_entry_t books[14];
 for(size_t i=0;i<n;++i){books[i]=(shelf_entry_t){.search_match=true,.favorite=i==10,.has_progress=true,.pct=(unsigned)i*7,.recent=100+(unsigned)i,.added=UINT64_C(0x100000000)+(uint64_t)i*100,.selected=i==10};strcpy(books[i].name,names[i]);snprintf(books[i].path,sizeof(books[i].path),"/%02zu",i);}
 for(s_shelf_order=0;s_shelf_order<8;++s_shelf_order){
  shelf_entry_t copy[14];memcpy(copy,books,sizeof(copy));qsort(copy,n,sizeof(*copy),compare_books);
  for(size_t i=0;i<n;++i){size_t expected=shelf_order_desc()?n-1-i:i;assert(!strcmp(copy[i].path,books[expected].path));assert(copy[i].selected==(expected==10));}
  // Matching results form one contiguous prefix, independently of primary order.
  copy[0].search_match=false;qsort(copy,n,sizeof(*copy),compare_books);assert(!copy[n-1].search_match);
  // Missing metadata stays last; zero progress remains a real sortable value.
  shelf_entry_t a=books[0],b=books[1];a.added=0;a.recent=0;
  if(shelf_order_mode()==1||shelf_order_mode()==2)assert(compare_books(&a,&b)>0);
  a=b;a.favorite=!b.favorite;assert(compare_books(&a,&b)==0);
  a=b;a.pct=255;b.pct=100;
  if(shelf_order_mode()==3)assert(compare_books(&a,&b)==0);
 }
 // Directory/import name order uses real basenames, independent of shelf aliases.
 s_shelf_order=0;s_manage_books=true;
 shelf_entry_t directory[3]={{.search_match=true,.is_dir=true},{.search_match=true,.is_dir=true},{.search_match=true,.has_progress=true,.recent=999}};
 strcpy(directory[0].name,"文学");strcpy(directory[1].name,"科幻");strcpy(directory[2].name,"阿");
 s_manage_books=true;for(unsigned order=0;order<8;++order){s_shelf_order=order;assert(compare_books(&directory[0],&directory[2])<0&&compare_books(&directory[1],&directory[0])<0);}s_manage_books=true;s_shelf_order=0;
 shelf_entry_t raw_a=books[0],raw_b=books[1];strcpy(raw_a.path,"/夏天.txt");strcpy(raw_b.path,"/边城.txt");strcpy(raw_a.name,"阿");strcpy(raw_b.name,"朱");
 raw_a.recent=999;raw_b.recent=1;raw_a.added=1;raw_b.added=999;raw_a.pct=100;raw_b.pct=1;
 for(s_shelf_order=0;s_shelf_order<8;++s_shelf_order)assert(compare_books(&raw_a,&raw_b)>0);
 s_shelf_order=0;s_manage_books=false;assert(compare_books(&raw_a,&raw_b)<0);
 s_import_candidates=true;s_shelf_order=7;assert(compare_books(&raw_a,&raw_b)>0);s_import_candidates=false;
 // Equal fields use displayed names, then full paths, giving deterministic pages.
 books[1]=books[0];strcpy(books[1].path,"/other");
 for(s_shelf_order=0;s_shelf_order<8;++s_shelf_order)assert(compare_books(&books[0],&books[1])<0);
 file_item_t files[]={{.path="/夏天.txt",.size=30,.modified=300},{.path="/边城.txt",.size=10,.modified=100},{.path="/我与地坛.epub",.size=20,.modified=200},{.path="/目录",.size=99,.modified=999,.is_dir=true}};
 const char *expected[]={"/边城.txt","/我与地坛.epub","/夏天.txt"};
 for(s_file_sort=0;s_file_sort<3;++s_file_sort)for(int direction=0;direction<2;++direction){s_file_desc=direction;file_item_t copy[4];memcpy(copy,files,sizeof(copy));qsort(copy,4,sizeof(*copy),compare_files);assert(copy[0].is_dir);for(int i=0;i<3;++i)assert(!strcmp(copy[i+1].path,expected[direction?2-i:i]));}
 // Stress real comparator transitivity and shuffled 5,000-title batches.
 shelf_entry_t *large=calloc(5000,sizeof(*large));assert(large);
 for(int i=0;i<5000;++i){snprintf(large[i].name,sizeof(large[i].name),"%s 第%u部",names[next_random()%n],next_random());snprintf(large[i].path,sizeof(large[i].path),"/books/%d",i);large[i].search_match=next_random()%4;large[i].pct=next_random()%256;large[i].has_progress=next_random()%2;large[i].recent=next_random();large[i].added=((uint64_t)next_random()<<32)|next_random();}
 clock_t start=clock();
 for(s_shelf_order=0;s_shelf_order<8;++s_shelf_order){
  for(int j=0;j<10000;++j){shelf_entry_t *a=&large[next_random()%5000],*b=&large[next_random()%5000],*c=&large[next_random()%5000];int ab=compare_books(a,b),bc=compare_books(b,c);assert(sign(ab)==-sign(compare_books(b,a)));if(ab<=0&&bc<=0)assert(compare_books(a,c)<=0);}
  qsort(large,5000,sizeof(*large),compare_books);for(int i=1;i<5000;++i)assert(compare_books(&large[i-1],&large[i])<=0);
 }
 free(large);printf("PASS: pinyin/default readings, numeric/case/punctuation/UTF8 edges; 8 shelf + 6 file orders; missing/tied/extreme metadata, unpinned favorites, selection, comparator laws and 5000-title stress (%.3fs host CPU)\n",(double)(clock()-start)/CLOCKS_PER_SEC);
}
'''
with tempfile.TemporaryDirectory(prefix='name-sort-') as directory:
    work=Path(directory)
    (work/'test.c').write_text(unit)
    flags=['cc','-std=gnu11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function',
           '-fsanitize=address,undefined',f'-DKIIKO_BOARD_METALIO47={board}',
           '-I'+str(folder/'include'),'-DPRODUCTION_SOURCE="'+str(folder/'read_pico_search.c')+'"']
    subprocess.run(flags+[str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
    data=subprocess.check_output([str(work/'test'),'dictionary'])
    # Independent oracle: SHA256 of cp:normalized-first-reading for all41,923
    # entries in the already pinned upstream0.55.0 wheel, not the generated table.
    assert len(data.splitlines())==41923
    assert hashlib.sha256(data).hexdigest()=='0759b56ba042f2e4289d3130717503bfce77154f92e6723f02ef0c159a4ebd53'
    print('PASS: every dictionary-default reading equals the pinned upstream oracle')

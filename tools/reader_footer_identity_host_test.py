#!/usr/bin/env python3
"""实际章名查找和页脚裁剪：连续页、裸章号、副题、目录跳转与跨 TXT 分段。
Exercise actual chapter lookup/footer stripping across pages, subtitles and jumps.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
book = root / 'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "html_text.h"
#define ESP_OK 0
#define BOOK_KIND_TXT 1
static bool txt;
static int book_kind(void){return txt;}
static const char *s_text="第一章\n海边的信\n正文甲\n第二章 雨夜\n正文乙";
static blk_t blocks[5],*s_blocks=blocks;
static size_t s_block_count=5,s_chapter,s_page,s_selected_toc=SIZE_MAX,s_selected_toc_offset;
static const char *fallback="第一章";
static size_t reader_page_offset(size_t page){return page;}
static size_t book_navigation_count(void){return 2;}
static size_t book_navigation_chapter(size_t index){(void)index;return 0;}
static size_t book_navigation_position(size_t chapter,size_t offset){(void)chapter;return offset>=blocks[3].offset?1:0;}
static int book_navigation_title(size_t index,char *out,size_t cap){snprintf(out,cap,"%s",index?"第二章 雨夜":fallback);return ESP_OK;}
static int book_chapter_title(size_t chapter,char *out,size_t cap){(void)chapter;snprintf(out,cap,"%s",fallback);return ESP_OK;}
'''
unit += function('copy_text', book) + '\n'
unit += function('reader_footer_strip_number', book) + '\n'
unit += '#include "main/apps/book_reader_chapter.inc"\n'
unit += function('reader_footer_chapter_name', book) + '\n'
unit += r'''
int main(void){
 size_t offset=0;
 for(size_t i=0;i<5;++i){
  const char *end=strchr(s_text+offset,'\n');
  blocks[i]=(blk_t){.offset=offset,.len=end?(size_t)(end-s_text-offset):strlen(s_text+offset),.image=-1};
  offset+=blocks[i].len+1;
 }
 blocks[0].chapter_start=blocks[3].chapter_start=true;blocks[1].heading_subtitle=true;
 char name[128];
 for(size_t i=0;i<blocks[3].offset;++i){s_page=i;reader_footer_chapter_name(name,sizeof(name));assert(!strcmp(name,"海边的信"));}
 for(size_t i=blocks[3].offset;i<strlen(s_text);++i){s_page=i;reader_footer_chapter_name(name,sizeof(name));assert(!strcmp(name,"雨夜"));}
 s_selected_toc=1;s_selected_toc_offset=blocks[3].offset;s_page=blocks[4].offset;
 reader_footer_chapter_name(name,sizeof(name));assert(!strcmp(name,"雨夜"));
 s_selected_toc=SIZE_MAX;s_page=blocks[2].offset;blocks[1].heading_subtitle=false;
 reader_footer_chapter_name(name,sizeof(name));assert(!strcmp(name,"第一章"));
 fallback="第一章 海边的信";
 reader_footer_chapter_name(name,sizeof(name));assert(!strcmp(name,"海边的信"));
 txt=true;
 for(s_chapter=0;s_chapter<3;++s_chapter){s_page=blocks[2].offset;reader_footer_chapter_name(name,sizeof(name));assert(!strcmp(name,"海边的信"));}
 reader_footer_strip_number(name,sizeof(name),"第53章");assert(!strcmp(name,"第53章"));
 reader_footer_strip_number(name,sizeof(name),"53");assert(!strcmp(name,"53"));
 reader_footer_strip_number(name,sizeof(name),"本章");assert(!name[0]);
 reader_current_chapter(name,1);assert(!name[0]);reader_current_chapter(NULL,0);
 puts("PASS: actual shared footer identity on every page, paired titles, bare numbers, fuller TOC labels, backward jumps and TXT segments");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-footer-identity-') as temporary:
    folder=Path(temporary)
    (folder/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(root),'-I'+str(root/'main/book'),'-I'+str(root/'tools/html_text_stubs'),
        str(folder/'test.c'),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real nested and 5000-book imports, checked membership, cancellation and allocation failure."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='book-import-') as d:
 p=Path(d);sd=p/'sd';flash=p/'flash';(sd/'books/A/Deep').mkdir(parents=True);(sd/'books/B').mkdir();(sd/'books/Empty').mkdir();(sd/'books/Many').mkdir();(sd/'books/New').mkdir();flash.mkdir()
 for f in ['old.txt','A/a.txt','A/Deep/b.epub','A/skip.png','A/.hidden.txt','B/c.TXT','New/new.txt']:(sd/'books'/f).write_text('book')
 for i in range(5001):(sd/'books/Many'/f'{i}.txt').write_text('book')
 (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\nenum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=257,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_INVALID_RESPONSE,ESP_ERR_NOT_FOUND,ESP_ERR_NOT_FINISHED,ESP_ERR_INVALID_SIZE};\n')
 (p/'setup_state.h').write_text('#pragma once\n#include <stdbool.h>\n#include "esp_err.h"\nbool app_setup_library_pending(void);\nesp_err_t app_setup_library_complete(void);\n')
 unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "book_import.h"
#include "book_library.h"
#include "read_pico_sd.h"
static bool fresh,hide=true,unhide_fail;static unsigned allocs,fail_at;
void *import_test_realloc(void *p,size_t n){if(++allocs==fail_at)return NULL;return realloc(p,n);}
bool app_setup_library_pending(void){return fresh;}
esp_err_t app_setup_library_complete(void){fresh=false;return ESP_OK;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t*i){memset(i,0,sizeof(*i));i->present=i->mounted=true;return ESP_OK;}
esp_err_t book_store_prepare_flash(void){return ESP_OK;}
void book_store_notify_changed(void){}
static bool hidden(const char*p){return hide&&strstr(p,"/A/a.txt")!=NULL;}
static bool unhide(const char*p){assert(hidden(p));if(unhide_fail)return false;hide=false;return true;}
static unsigned count_on_disk(void){FILE*f=fopen(LIBRARY_SD_ROOT "/.kiikoread/bookshelf-v1","rb");assert(f);unsigned char b[20];assert(fread(b,1,20,f)==20&&!fclose(f));return b[12]|b[13]<<8|b[14]<<16|b[15]<<24;}
static int finish(unsigned nodes){int r=ESP_ERR_NOT_FINISHED;unsigned ticks=0;while(r==ESP_ERR_NOT_FINISHED){r=book_import_step(nodes);assert(++ticks<100000);}return r;}
int main(void){
 assert(book_library_include(LIBRARY_SD_ROOT "/books/old.txt")==ESP_OK&&count_on_disk()==1);
 assert(book_import_select(LIBRARY_SD_ROOT "/books/A/Deep/b.epub",false,true)==ESP_OK);
 assert(book_import_select(LIBRARY_SD_ROOT "/books/A",true,true)==ESP_OK&&book_import_select(LIBRARY_SD_ROOT "/books/B",true,true)==ESP_OK);
 assert(book_import_choice_count()==3&&book_import_selection(LIBRARY_SD_ROOT "/books/A/Deep/b.epub")==BOOK_IMPORT_COVERED);
 assert(book_import_selection(LIBRARY_SD_ROOT "/books/AB/not.txt")==BOOK_IMPORT_NONE);
 assert(book_import_begin(hidden,unhide)==ESP_OK&&book_import_running());
 assert(book_import_select(LIBRARY_SD_ROOT "/books/old.txt",false,true)==ESP_ERR_INVALID_STATE);
 assert(book_import_step(1)==ESP_ERR_NOT_FINISHED&&count_on_disk()==1);
 book_import_cancel();assert(!book_import_running()&&book_import_choice_count()==3&&count_on_disk()==1);
 assert(book_import_begin(hidden,unhide)==ESP_OK&&finish(2)==ESP_OK&&book_import_result_count()==3&&!book_import_unhide_failures()&&!hide);
 assert(count_on_disk()==4&&book_library_contains(LIBRARY_SD_ROOT "/books/old.txt")&&book_library_contains(LIBRARY_SD_ROOT "/books/A/Deep/b.epub")&&!book_library_contains(LIBRARY_SD_ROOT "/books/A/skip.png"));
 assert(book_import_select(LIBRARY_SD_ROOT "/books/A",true,false)==ESP_OK&&book_import_selection(LIBRARY_SD_ROOT "/books/A/Deep/b.epub")==BOOK_IMPORT_EXPLICIT);
 book_import_reset();assert(!book_import_choice_count());
 fail_at=allocs+1;assert(book_import_select(LIBRARY_SD_ROOT "/books/A",true,true)==ESP_ERR_NO_MEM&&!book_import_choice_count());fail_at=0;
 assert(book_import_select(LIBRARY_SD_ROOT "/books/New",true,true)==ESP_OK);
 assert(book_import_select(LIBRARY_SD_ROOT "/books/missing",true,true)==ESP_OK&&book_import_begin(hidden,unhide)==ESP_OK&&finish(1)==ESP_FAIL&&count_on_disk()==4&&!book_library_contains(LIBRARY_SD_ROOT "/books/New/new.txt"));
 book_import_reset();fresh=true;book_library_invalidate();assert(book_import_select(LIBRARY_SD_ROOT "/books/Empty",true,true)==ESP_OK&&book_import_begin(hidden,unhide)==ESP_OK&&finish(3)==ESP_ERR_NOT_FOUND&&fresh&&count_on_disk()==4);
 book_import_reset();fresh=false;book_library_invalidate();hide=unhide_fail=true;
 assert(book_import_select(LIBRARY_SD_ROOT "/books/A",true,true)==ESP_OK&&book_import_begin(hidden,unhide)==ESP_OK&&finish(32)==ESP_OK&&book_import_unhide_failures()==1&&hide);
 unhide_fail=false;assert(book_import_begin(hidden,unhide)==ESP_OK&&finish(32)==ESP_OK&&!book_import_unhide_failures()&&!hide);
 book_import_reset();assert(book_import_select(LIBRARY_SD_ROOT "/books/Many",true,true)==ESP_OK&&book_import_begin(hidden,unhide)==ESP_OK&&finish(32)==ESP_OK&&book_import_result_count()==5001&&count_on_disk()==5005);

 // 重新扫描清空后，取消和递归读取失败都保持空书架，只提交新选择。
 // After rescan, cancellation and traversal failures keep an empty shelf; only new choices commit.
 book_import_reset();assert(book_library_reset()==ESP_OK);book_library_invalidate();assert(book_library_mode()==BOOK_LIBRARY_UNSET&&count_on_disk()==0);
 assert(book_import_select(LIBRARY_SD_ROOT "/books/A",true,true)==ESP_OK&&book_import_begin(hidden,unhide)==ESP_OK);
 assert(book_import_step(1)==ESP_ERR_NOT_FINISHED);book_import_cancel();assert(count_on_disk()==0&&book_library_mode()==BOOK_LIBRARY_UNSET);
 book_import_reset();assert(book_import_select(LIBRARY_SD_ROOT "/books/missing",true,true)==ESP_OK&&book_import_begin(hidden,unhide)==ESP_OK&&finish(1)==ESP_FAIL&&count_on_disk()==0);
 book_import_reset();assert(book_import_select(LIBRARY_SD_ROOT "/books/New",true,true)==ESP_OK&&book_import_begin(hidden,unhide)==ESP_OK&&finish(1)==ESP_OK&&count_on_disk()==1);
 assert(book_library_contains(LIBRARY_SD_ROOT "/books/New/new.txt")&&!book_library_contains(LIBRARY_SD_ROOT "/books/old.txt"));
 struct stat kept;assert(!stat(LIBRARY_SD_ROOT "/books/old.txt",&kept));
 book_import_reset();book_library_invalidate();puts("PASS: real nested/overlapping folders, recursive TXT/EPUB-only enrollment, 5001-book walk, cancel/empty/missing/OOM and hidden retry preserve existing shelf; choices persist independently of directories/pages");
}
'''
 (p/'test.c').write_text(unit)
 common=['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(R/'main/book'),'-I'+str(R/'components/read_pico/include')]
 subprocess.run(common+['-Drealloc=import_test_realloc','-c',str(R/'main/book/book_import.c'),'-o',str(p/'import.o')],check=True)
 subprocess.run(common+[f'-DLIBRARY_SD_ROOT="{sd}"',f'-DLIBRARY_FLASH_ROOT="{flash}"',str(p/'test.c'),str(p/'import.o'),str(R/'main/book/book_library.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)

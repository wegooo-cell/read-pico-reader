#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real filesystem: selected libraries, 5000 paths, abort, previous generation, media readiness."""
from pathlib import Path
import subprocess,tempfile,sys
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='book-library-') as directory:
 p=Path(directory);sd=p/'sd';flash=p/'flash';sd.mkdir();flash.mkdir()
 (p/'setup_state.h').write_text('#pragma once\n#include <stdbool.h>\n#include \"esp_err.h\"\nbool app_setup_library_pending(void);\nesp_err_t app_setup_library_complete(void);\n')
 (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\nenum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=257,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_INVALID_RESPONSE,ESP_ERR_NOT_FOUND,ESP_ERR_NOT_FINISHED};\n')
 unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_library.h"
#include "read_pico_sd.h"
static bool first_import;static int finish_error;
bool app_setup_library_pending(void){return first_import;}
esp_err_t app_setup_library_complete(void){if(finish_error)return finish_error;first_import=false;return ESP_OK;}
static int status;static bool present=true,mounted=true;static unsigned revisions;
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info){memset(info,0,sizeof(*info));info->present=present;info->mounted=mounted;return status;}
esp_err_t book_store_prepare_flash(void){return ESP_OK;}
void book_store_notify_changed(void){++revisions;}
int main(void){
 assert(book_library_mode()==BOOK_LIBRARY_UNSET&&book_library_error()==ESP_OK);
 assert(!book_library_contains(LIBRARY_SD_ROOT "/books/zero.txt"));
 assert(book_library_begin(false)==ESP_OK);
 char path[288];for(unsigned i=0;i<5001;++i){snprintf(path,sizeof(path),LIBRARY_SD_ROOT "/books/title-%u.txt",i);assert(book_library_add(path)==ESP_OK);}
 assert(book_library_add(LIBRARY_SD_ROOT "/books/../outside.txt")==ESP_ERR_INVALID_ARG);
 assert(book_library_add(LIBRARY_SD_ROOT "/books/..")==ESP_ERR_INVALID_ARG);
 assert(book_library_commit()==ESP_OK&&revisions==1);
 book_library_invalidate();assert(book_library_mode()==BOOK_LIBRARY_SELECTED);
 for(unsigned i=0;i<5001;++i){snprintf(path,sizeof(path),LIBRARY_SD_ROOT "/books/title-%u.txt",i);assert(book_library_contains(path));}
 assert(!book_library_contains(LIBRARY_SD_ROOT "/books/extra.epub"));
 assert(book_library_begin(false)==ESP_OK&&book_library_add(LIBRARY_SD_ROOT "/books/cancel.txt")==ESP_OK);book_library_abort();
 assert(!book_library_contains(LIBRARY_SD_ROOT "/books/cancel.txt"));
 assert(book_library_include(LIBRARY_SD_ROOT "/books/extra.epub")==ESP_OK);
 book_library_invalidate();assert(book_library_contains(LIBRARY_SD_ROOT "/books/extra.epub"));
 assert(book_library_move(LIBRARY_SD_ROOT "/books/extra.epub",LIBRARY_SD_ROOT "/elsewhere/moved.epub")==ESP_OK);
 assert(!book_library_contains(LIBRARY_SD_ROOT "/books/extra.epub")&&book_library_contains(LIBRARY_SD_ROOT "/elsewhere/moved.epub"));
 assert(book_library_move(LIBRARY_SD_ROOT "/elsewhere/moved.epub",LIBRARY_SD_ROOT "/elsewhere/moved.epub")==ESP_OK);
 assert(!mkdir(LIBRARY_SD_ROOT "/.kiikoread/bookshelf-v1.tmp",0700));
 assert(book_library_move(LIBRARY_SD_ROOT "/elsewhere/moved.epub",LIBRARY_SD_ROOT "/elsewhere/failed.epub")!=ESP_OK);
 assert(book_library_contains(LIBRARY_SD_ROOT "/elsewhere/moved.epub")&&!book_library_contains(LIBRARY_SD_ROOT "/elsewhere/failed.epub"));
 assert(!rmdir(LIBRARY_SD_ROOT "/.kiikoread/bookshelf-v1.tmp"));
 // Interrupted/corrupt active index restores the previous complete generation, never an empty library.
 FILE *file=fopen(LIBRARY_SD_ROOT "/.kiikoread/bookshelf-v1","r+b");assert(file&&fputc(0,file)!=EOF&&!fclose(file));
 book_library_invalidate();assert(book_library_mode()==BOOK_LIBRARY_SELECTED&&book_library_error()==ESP_OK);
 assert(book_library_contains(LIBRARY_SD_ROOT "/books/title-5000.txt"));
 // Re-saving after recovery retains the previous valid generation as the fallback.
 assert(book_library_include(LIBRARY_SD_ROOT "/books/recovered.txt")==ESP_OK);
 file=fopen(LIBRARY_SD_ROOT "/.kiikoread/bookshelf-v1","r+b");assert(file&&fputc(0,file)!=EOF&&!fclose(file));
 book_library_invalidate();assert(book_library_contains(LIBRARY_SD_ROOT "/books/title-5000.txt"));
 status=ESP_ERR_NOT_FINISHED;mounted=false;
 assert(!book_library_contains(LIBRARY_SD_ROOT "/books/title-5000.txt")&&book_library_error()==ESP_ERR_NOT_FINISHED);
 status=ESP_OK;mounted=true;assert(book_library_contains(LIBRARY_SD_ROOT "/books/title-5000.txt"));
 assert(book_library_begin(true)==ESP_OK&&book_library_commit()==ESP_OK);book_library_invalidate();
 assert(book_library_mode()==BOOK_LIBRARY_ALL&&book_library_contains(LIBRARY_SD_ROOT "/books/new.epub"));
 assert(book_library_begin_snapshot()==ESP_OK);
 assert(book_library_add(LIBRARY_SD_ROOT "/books/existing.txt")==ESP_OK&&book_library_commit()==ESP_OK);
 book_library_invalidate();assert(book_library_mode()==BOOK_LIBRARY_SELECTED);
 assert(book_library_contains(LIBRARY_SD_ROOT "/books/existing.txt")&&!book_library_contains(LIBRARY_SD_ROOT "/books/new.epub"));
 // Full capacity moves replace in place; one batched removal compacts once and permits reuse.
 assert(book_library_begin(true)==ESP_OK&&book_library_commit()==ESP_OK);
 assert(book_library_begin_snapshot()==ESP_OK);
 for(unsigned i=0;i<8192;++i){snprintf(path,sizeof(path),LIBRARY_SD_ROOT "/books/full-%u.txt",i);assert(book_library_add(path)==ESP_OK);}
 assert(book_library_commit()==ESP_OK);
 assert(book_library_move(LIBRARY_SD_ROOT "/books/full-0.txt",LIBRARY_SD_ROOT "/outside/new.txt")==ESP_OK);
 assert(book_library_contains(LIBRARY_SD_ROOT "/outside/new.txt")&&!book_library_contains(LIBRARY_SD_ROOT "/books/full-0.txt"));
 assert(book_library_begin_snapshot()==ESP_OK);
 for(unsigned i=1;i<8192;++i){snprintf(path,sizeof(path),LIBRARY_SD_ROOT "/books/full-%u.txt",i);assert(book_library_remove(path)==ESP_OK);}
 assert(book_library_remove(LIBRARY_SD_ROOT "/outside/new.txt")==ESP_OK&&book_library_commit()==ESP_OK);
 assert(!book_library_contains(LIBRARY_SD_ROOT "/outside/new.txt"));
 assert(book_library_include(LIBRARY_SD_ROOT "/books/readded.txt")==ESP_OK&&book_library_contains(LIBRARY_SD_ROOT "/books/readded.txt"));
 // A fresh NVS onboarding ignores even a valid prior card index and legacy markers.
 first_import=true;book_library_invalidate();assert(book_library_mode()==BOOK_LIBRARY_UNSET&&!book_library_contains(LIBRARY_SD_ROOT "/books/readded.txt"));
 assert(book_library_begin_snapshot()==ESP_OK&&book_library_add(LIBRARY_SD_ROOT "/books/chosen.txt")==ESP_OK);book_library_abort();assert(first_import&&book_library_mode()==BOOK_LIBRARY_UNSET);
 finish_error=ESP_FAIL;assert(book_library_include(LIBRARY_SD_ROOT "/books/chosen.txt")==ESP_FAIL&&first_import);assert(book_library_mode()==BOOK_LIBRARY_UNSET);
 finish_error=0;assert(book_library_include(LIBRARY_SD_ROOT "/books/chosen.txt")==ESP_OK&&!first_import);
 book_library_invalidate();assert(book_library_contains(LIBRARY_SD_ROOT "/books/chosen.txt")&&!book_library_contains(LIBRARY_SD_ROOT "/books/readded.txt"));

 // 重扫持久化待导入，旧added索引不能使旧书重新上架；文件与进度保持。
 // Rescan persists pending import despite legacy journals and preserves book/progress bytes.
 assert(!mkdir(LIBRARY_SD_ROOT "/books",0700));
 file=fopen(LIBRARY_SD_ROOT "/books/keep.txt","wb");assert(file&&fputs("keep book",file)>=0&&!fclose(file));
 file=fopen(LIBRARY_SD_ROOT "/.progress-keep","wb");assert(file&&fputs("82%",file)>=0&&!fclose(file));
 assert(!mkdir(LIBRARY_SD_ROOT "/.kiikoread/bookshelf-v1.tmp",0700));
 unsigned before_reset=revisions;assert(book_library_reset()!=ESP_OK&&revisions==before_reset);
 assert(book_library_contains(LIBRARY_SD_ROOT "/books/chosen.txt"));assert(!rmdir(LIBRARY_SD_ROOT "/.kiikoread/bookshelf-v1.tmp"));
 for(int all=0;all<2;++all){
  if(all)assert(book_library_begin(true)==ESP_OK&&book_library_commit()==ESP_OK);
  file=fopen(LIBRARY_SD_ROOT "/.kiiko-added-v1","wb");assert(file&&fputc(1,file)!=EOF&&!fclose(file));
  assert(book_library_reset()==ESP_OK);book_library_invalidate();
  assert(book_library_mode()==BOOK_LIBRARY_UNSET&&book_library_error()==ESP_OK&&!book_library_contains(LIBRARY_SD_ROOT "/books/chosen.txt")&&!book_library_contains(LIBRARY_SD_ROOT "/books/keep.txt"));
  assert(book_library_begin_snapshot()==ESP_OK&&book_library_add(LIBRARY_SD_ROOT "/books/cancelled.txt")==ESP_OK);book_library_abort();
  assert(book_library_mode()==BOOK_LIBRARY_UNSET&&!book_library_contains(LIBRARY_SD_ROOT "/books/cancelled.txt"));
 }
 char preserved[16]={0};file=fopen(LIBRARY_SD_ROOT "/books/keep.txt","rb");assert(file&&fread(preserved,1,9,file)==9&&!memcmp(preserved,"keep book",9)&&!fclose(file));
 memset(preserved,0,sizeof(preserved));file=fopen(LIBRARY_SD_ROOT "/.progress-keep","rb");assert(file&&fread(preserved,1,3,file)==3&&!memcmp(preserved,"82%",3)&&!fclose(file));
 first_import=true;book_library_invalidate();assert(book_library_reset()==ESP_OK&&first_import&&book_library_mode()==BOOK_LIBRARY_UNSET);
 assert(book_library_include(LIBRARY_SD_ROOT "/books/new-selection.txt")==ESP_OK&&!first_import);
 book_library_invalidate();assert(book_library_contains(LIBRARY_SD_ROOT "/books/new-selection.txt")&&!book_library_contains(LIBRARY_SD_ROOT "/books/chosen.txt"));
 // Switching media cannot reuse a card's whitelist on the internal store.
 present=mounted=false;book_library_invalidate();assert(book_library_mode()==BOOK_LIBRARY_UNSET);
 file=fopen(LIBRARY_FLASH_ROOT "/.kiiko-added-v1","wb");assert(file&&fputc(1,file)!=EOF&&!fclose(file));
 book_library_invalidate();assert(book_library_mode()==BOOK_LIBRARY_ALL);
 puts("PASS: 5001 selected paths/hash lookup, checked persistence, additive import, abort, previous-generation recovery, transient mount refusal, medium separation and legacy library retention");
 book_library_invalidate();
}
'''
 (p/'test.c').write_text(unit)
 cmd=['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',f'-DLIBRARY_SD_ROOT="{sd}"',f'-DLIBRARY_FLASH_ROOT="{flash}"','-I'+str(p),'-I'+str(R/'main/book'),'-I'+str(R/'components/read_pico/include'),str(p/'test.c'),str(R/'main/book/book_library.c'),'-o',str(p/'test')]
 subprocess.run(cmd,check=True);subprocess.run([str(p/'test')],check=True)

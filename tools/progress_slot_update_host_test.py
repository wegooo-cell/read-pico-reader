#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual PR100 slot writes: in-place reuse, size fallback and interrupted-write recovery."""
from pathlib import Path
import subprocess
import tempfile
R=Path(__file__).resolve().parents[1]
prefix=(R/'tools/book_storage_host_test.c').read_text().split('#ifdef ESP_PLATFORM',1)[0].split('int main(',1)[0]
unit=prefix+r'''
#include <unistd.h>
#include <sys/stat.h>
esp_err_t nvs_get_stats(const char *part,nvs_stats_t *out){(void)part;out->available_entries=128;return ESP_OK;}
static unsigned truncate_opens,update_opens;
static bool sync_failure,short_write;
esp_err_t book_store_prepare_flash(void){return ESP_OK;}
bool book_store_flash_ready(void){return true;}
static FILE *slot_fopen(const char *path,const char *mode){
 if(!strcmp(mode,"wb"))++truncate_opens;
 if(!strcmp(mode,"r+b"))++update_opens;
 return fopen(path,mode);
}
static int slot_fsync(int fd){return sync_failure?-1:fsync(fd);}
static size_t slot_fwrite(const void *data,size_t size,size_t count,FILE *file){
 if(short_write){short_write=false;return fwrite(data,size,count/2,file);}
 return fwrite(data,size,count,file);
}
#define fopen slot_fopen
#define fsync slot_fsync
#define fwrite slot_fwrite
#include PROGRESS_SOURCE
#undef fopen
#undef fsync
#undef fwrite
int main(void){
 const char *path="/sdcard/books/position.txt";
 book_progress_t p={.file_size=10000,.chapter=8,.byte_off=8200,.px=48,.pct=82},q;
 assert(book_progress_save(path,&p)==ESP_OK&&truncate_opens==1);
 p.byte_off=8300;p.pct=83;assert(book_progress_save(path,&p)==ESP_OK&&truncate_opens==2);
 char key[11],slot[PROGRESS_FILE_MAX];make_key(path,key);slot_path(slot,key,0);
 struct stat before,after;assert(!stat(slot,&before));
 p.byte_off=8400;p.pct=84;assert(book_progress_save(path,&p)==ESP_OK&&truncate_opens==2&&update_opens==3);
 assert(!stat(slot,&after)&&before.st_size==after.st_size&&before.st_ino==after.st_ino);
 assert(book_progress_load(path,p.file_size,&q)&&q.pct==84);
 // Wrong-sized inactive files use wb; the active checked copy remains available.
 slot_path(slot,key,1);FILE *file=fopen(slot,"wb");assert(file&&fputc(0,file)!=EOF&&!fclose(file));
 p.byte_off=8500;p.pct=85;assert(book_progress_save(path,&p)==ESP_OK&&truncate_opens==3);
 // fsync failure and a partial write invalidate only the target inactive slot.
 sync_failure=true;p.byte_off=8600;p.pct=86;assert(book_progress_save(path,&p)!=ESP_OK);sync_failure=false;
 assert(book_progress_load_checked(path,p.file_size,&q)==ESP_OK&&q.pct==85&&q.byte_off==8500);
 short_write=true;assert(book_progress_save(path,&p)!=ESP_OK);
 assert(book_progress_load_checked(path,p.file_size,&q)==ESP_OK&&q.pct==85&&q.byte_off==8500);
 uint32_t recent=q.last_open_s,reserved=sequence;
 const char *moved="/sdcard/Elsewhere/moved-position.txt";
 assert(book_progress_copy_to(path,moved,p.file_size)==ESP_OK&&sequence==reserved);
 assert(book_progress_load_checked(moved,p.file_size,&q)==ESP_OK&&q.last_open_s==recent&&q.pct==85&&q.byte_off==8500);
 assert(book_progress_load_checked(path,p.file_size,&q)==ESP_OK&&q.last_open_s==recent);
 p.pct=88;p.byte_off=8800;assert(book_progress_save(path,&p)==ESP_OK);
 assert(book_progress_copy_to(path,moved,p.file_size)==BOOK_PROGRESS_MOVE_CONFLICT);
 assert(book_progress_load_checked(moved,p.file_size,&q)==ESP_OK&&q.pct==85&&q.byte_off==8500);
 puts("PASS: actual PR100 reuses equal-size inactive files, falls back for wrong sizes; fsync/short-write failures retain the active position; relocated progress preserves recent order and the recovery source");
}
'''
with tempfile.TemporaryDirectory(prefix='progress-slot-') as directory:
    work=Path(directory)
    (work/'test.c').write_text(unit)
    flags=['cc','-std=gnu11','-D_DEFAULT_SOURCE','-DESP_PLATFORM','-DKIIKO_BOARD_METALIO47=0',
           '-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
           '-fsanitize=address,undefined','-I'+str(R/'main/book'),'-I'+str(R/'tools/book_storage_stubs'),
           '-DPROGRESS_SOURCE="'+str(R/'main/book/book_progress.c')+'"',
           '-DBOOK_PROGRESS_DIR="'+str(work/'progress')+'"']
    subprocess.run(flags+[str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)

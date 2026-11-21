#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual flash mount/progress paths retain storage when memory becomes scarce after boot."""
from pathlib import Path
import subprocess,tempfile,sys
R=Path(__file__).resolve().parents[1]
prefix=(R/'tools/book_storage_host_test.c').read_text().split('#ifdef ESP_PLATFORM',1)[0].split('int main(',1)[0]
prefix=prefix.replace('assert(!strcmp(ns,"rp_books"))','assert(!strcmp(ns,"rp_books")||!strcmp(ns,"mt_books"))')
unit=prefix+r'''
#include "esp_vfs_fat.h"
#include "read_pico_sd.h"
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101
#endif
static bool scarce,no_filesystem,nonblank,read_failed,format_failed;
static unsigned mounts,formats,live_handles,reads;
static int64_t now_us;
int64_t esp_timer_get_time(void){return now_us;}
esp_err_t wl_unmount(wl_handle_t handle){assert(handle==9&&live_handles);--live_handles;return ESP_OK;}
size_t wl_size(wl_handle_t handle){assert(handle==9);return 12288;}
esp_err_t wl_read(wl_handle_t handle,size_t off,void *data,size_t size){
 assert(handle==9&&off+size<=12288);++reads;if(read_failed)return ESP_FAIL;
 memset(data,255,size);if(nonblank&&off+size==12288)((uint8_t*)data)[size-1]=0x42;return ESP_OK;
}
esp_err_t nvs_get_stats(const char *part,nvs_stats_t *stats){assert(!part);stats->available_entries=128;return ESP_OK;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info){memset(info,0,sizeof(*info));return ESP_OK;}
esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(const char *path,const char *part,
 const esp_vfs_fat_mount_config_t *cfg,wl_handle_t *wl){
 assert(!strcmp(path,"/flash")&&!strcmp(part,"storage"));
 assert(cfg->max_files==4);++mounts;
 if(scarce)return ESP_ERR_NO_MEM;
 *wl=9;++live_handles;
 if(cfg->format_if_mount_failed){assert(no_filesystem&&!nonblank&&!read_failed&&reads==24);++formats;if(format_failed)return ESP_FAIL;no_filesystem=false;}
 return no_filesystem?ESP_FAIL:ESP_OK;
}
static uint8_t saved_record[512];static size_t saved_len;static char saved_key[11];
static esp_err_t copy_record(const char *key,const uint8_t *data,size_t len,void *ctx){
 (void)ctx;assert(len<sizeof(saved_record));strcpy(saved_key,key);memcpy(saved_record,data,len);saved_len=len;return ESP_OK;
}
int main(int argc,char **argv){
 assert(argc==2);bool early=strcmp(argv[1],"late")!=0;
 no_filesystem=strcmp(argv[1],"early")&&early;
 nonblank=!strcmp(argv[1],"damaged");read_failed=!strcmp(argv[1],"read-error");format_failed=!strcmp(argv[1],"format-error");
 if(nonblank||read_failed||format_failed){
  assert(book_progress_prepare_storage()!=ESP_OK&&!book_store_flash_ready()&&!live_handles);
  unsigned before=mounts;
  for(int i=0;i<100;++i)assert(book_progress_prepare_storage()!=ESP_OK);
  assert(mounts==before&&!live_handles&&formats==(unsigned)format_failed);
  if(nonblank){now_us=11000000;assert(book_progress_prepare_storage()!=ESP_OK&&mounts==before+1&&!formats&&!live_handles);}
  puts("PASS: nonblank/read-error data never formats; failed mounts release every WL handle and background retries are throttled");return 0;
 }
 const char *path="/sdcard/books/example.txt";
 book_progress_t value={.file_size=10000,.chapter=2,.byte_off=8200,.px=40,.pct=82},actual;
 if(early){
  book_progress_prepare_at_boot();assert(book_store_flash_ready()&&mounts==(!strcmp(argv[1],"empty")?2u:1u)&&live_handles==1);
 }
 unsigned expected_mounts=mounts;
 scarce=true; // Model the EPD/DMA/font allocations observed before the failing late mount.
 esp_err_t err=book_progress_save(path,&value);
 if(!early){assert(err!=ESP_OK&&!book_store_flash_ready());puts("PASS: reproduce late mount allocation failure with plenty of free NVS entries");return 0;}
 assert(err==ESP_OK&&mounts==expected_mounts);
 assert(book_progress_load(path,value.file_size,&actual)&&actual.byte_off==value.byte_off);
 assert(book_progress_records(copy_record,NULL)==ESP_OK&&saved_len);
 // Populate both slots, then make both unreadable as records. A checked load must not become zero.
 assert(book_progress_save(path,&value)==ESP_OK&&book_progress_save(path,&value)==ESP_OK);
 uint8_t copies[2][512];size_t lengths[2];char filenames[2][256];
 for(unsigned slot=0;slot<2;++slot){
  snprintf(filenames[slot],sizeof(filenames[slot]),"%s/%.8s.%u",BOOK_PROGRESS_DIR,saved_key+2,slot);
  FILE *f=fopen(filenames[slot],"rb");assert(f);lengths[slot]=fread(copies[slot],1,sizeof(copies[slot]),f);assert(lengths[slot]&&fclose(f)==0);
  f=fopen(filenames[slot],"ab");assert(f&&fputc(0,f)!=EOF&&fclose(f)==0);
 }
 actual.byte_off=123;
 assert(book_progress_load_checked(path,value.file_size,&actual)==ESP_ERR_INVALID_STATE&&actual.byte_off==123);
 book_progress_t zero=value;zero.byte_off=0;zero.pct=0;
 assert(book_progress_save(path,&zero)==ESP_ERR_INVALID_STATE);
 for(unsigned slot=0;slot<2;++slot){
  FILE *f=fopen(filenames[slot],"rb");assert(f&&fseek(f,0,SEEK_END)==0&&ftell(f)==(long)lengths[slot]+1&&fclose(f)==0);
  f=fopen(filenames[slot],"wb");assert(f&&fwrite(copies[slot],1,lengths[slot],f)==lengths[slot]&&fclose(f)==0);
 }
 assert(book_progress_load_checked(path,value.file_size,&actual)==ESP_OK&&actual.pct==82&&actual.byte_off==8200);
 puts("PASS: bad progress reads never turn 82% into a zero write; rejected writes preserve both slots, and recovery reads the saved position");
 // The same filesystem is used by settings restore: preflight, clear and restore remain writable.
 assert(book_progress_prepare_storage()==ESP_OK&&book_progress_clear_records()==ESP_OK);
 assert(book_progress_restore_record(saved_key,saved_record,saved_len)==ESP_OK);
 assert(book_progress_load(path,value.file_size,&actual)&&actual.byte_off==value.byte_off&&mounts==expected_mounts);
 puts("PASS: early verified storage with 128 free NVS entries survives low-memory saves and configuration-history restore without remounting");
}
'''
with tempfile.TemporaryDirectory(prefix='progress-boot-') as d:
 p=Path(d);(p/'test.c').write_text(unit)
 (p/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
 flags=['cc','-std=gnu11','-D_DEFAULT_SOURCE','-DESP_PLATFORM','-DKIIKO_BOARD_METALIO47=1',
  '-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',
  '-ffunction-sections','-fdata-sections','-I'+str(p),f'-DBOOK_PROGRESS_DIR="{p}/progress"',
  '-I'+str(R/'tools/book_storage_stubs'),'-I'+str(R/'main/book'),'-I'+str(R/'main'),
  '-I'+str(R/'components/read_pico/include')]
 link=['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections']
 subprocess.run(flags+[str(p/'test.c'),str(R/'main/book/book_progress.c'),str(R/'main/book/book_store.c'),*link,'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),'late'],check=True)
 subprocess.run([str(p/'test'),'early'],check=True)
 subprocess.run([str(p/'test'),'empty'],check=True)
 subprocess.run([str(p/'test'),'damaged'],check=True)
 subprocess.run([str(p/'test'),'read-error'],check=True)
 subprocess.run([str(p/'test'),'format-error'],check=True)

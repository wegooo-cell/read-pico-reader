#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Real dual-slot progress: failed internal storage, TF fallback, staged restore and unchanged originals."""
from pathlib import Path
import subprocess,tempfile,json
R=Path(__file__).resolve().parents[1]
prefix=(R/'tools/book_storage_host_test.c').read_text().split('#ifdef ESP_PLATFORM',1)[0]
prefix=prefix.replace('assert(!strcmp(ns,"rp_books"))','assert(!strcmp(ns,"rp_books")||!strcmp(ns,"mt_books"))')
unit=prefix+r'''
static bool primary_ready;
esp_err_t book_store_prepare_flash(void){return primary_ready?ESP_OK:ESP_FAIL;}
bool book_store_flash_ready(void){return primary_ready;}
esp_err_t nvs_get_stats(const char *p,nvs_stats_t*s){(void)p;s->available_entries=128;return ESP_OK;}
#include PROGRESS_SOURCE
static void raw_record(uint8_t *data,const char *path,unsigned pct,unsigned order){
 memset(data,0,RECORD_CAP);memcpy(data,"RPB",3);data[3]=2;put32(data+4,10000);data[14]=40;data[15]=pct;put32(data+10,pct*100);put32(data+16,order);
 size_t n=strlen(path)+1;data[22]=(uint8_t)n;data[23]=(uint8_t)(n>>8);strcpy((char*)data+24,path);
}
static size_t raw_len(const char*p){return 24+strlen(p)+1;}
static void check(const char *path,unsigned pct){book_progress_t p={0};esp_err_t err=book_progress_load_checked(path,10000,&p);if(err!=ESP_OK||p.pct!=pct||p.byte_off!=pct*100)fprintf(stderr,"load %s expected=%u got=%u offset=%u err=%d\n",path,pct,p.pct,(unsigned)p.byte_off,err);assert(err==ESP_OK&&p.pct==pct&&p.byte_off==pct*100);}
int main(void){
 primary_ready=true;test_sd_mounted=true;
 const char *path="/sdcard/books/old.epub",*new_path="/sdcard/books/new.txt",*other="/sdcard/books/untouched.epub";
 // Mounted-but-unreadable internal directory plus valid TF data must not erase legacy NVS.
 const char *migration="/sdcard/books/migration-original.epub";char migration_key[11];make_key(migration,migration_key);
 uint8_t legacy_migration[RECORD_CAP],card_migration[RECORD_CAP];raw_record(legacy_migration,migration,73,1);raw_record(card_migration,migration,83,2);
 assert(nvs_set_blob(1,migration_key,legacy_migration,raw_len(migration))==ESP_OK);
 assert(progress_card_prepare()==ESP_OK&&disk_write_at(BOOK_PROGRESS_SD_DIR,migration_key,card_migration,raw_len(migration))==ESP_OK);
 FILE *blocked=fopen(BOOK_PROGRESS_DIR,"wb");assert(blocked&&fputs("KEEP",blocked)>=0&&!fclose(blocked));
 book_progress_prepare();size_t retained=sizeof(card_migration);assert(nvs_get_blob(1,migration_key,card_migration,&retained)==ESP_OK&&!memcmp(card_migration,legacy_migration,retained));
 assert(!remove(BOOK_PROGRESS_DIR));assert(nvs_erase_key(1,migration_key)==ESP_OK&&disk_erase_at(BOOK_PROGRESS_SD_DIR,migration_key)==ESP_OK);
 assert(book_progress_prepare_storage()==ESP_OK);
 book_progress_t p={.file_size=10000,.px=40,.pct=73,.byte_off=7300};assert(book_progress_save(path,&p)==ESP_OK);check(path,73);
 char key[11];make_key(path,key);uint8_t original[RECORD_CAP];size_t original_len=sizeof(original);
 assert(disk_read_at(BOOK_PROGRESS_DIR,key,original,&original_len,NULL)==ESP_OK);
 assert(nvs_set_blob(1,key,original,original_len)==ESP_OK);
 primary_ready=false;assert(book_progress_prepare_storage()==ESP_OK);check(path,73);
 p.pct=82;p.byte_off=8200;assert(book_progress_save(path,&p)==ESP_OK);check(path,82);
 uint8_t after[RECORD_CAP];size_t n=sizeof(after);assert(disk_read_at(BOOK_PROGRESS_DIR,key,after,&n,NULL)==ESP_OK&&n==original_len&&!memcmp(after,original,n));
 primary_ready=true;check(path,82);
 primary_ready=false;book_progress_t q={0};assert(book_progress_load_checked(new_path,10000,&q)==ESP_ERR_NVS_NOT_FOUND);
 p.pct=25;p.byte_off=2500;assert(book_progress_save(new_path,&p)==ESP_OK);check(new_path,25);
 char fresh_key[11];make_key(new_path,fresh_key);n=sizeof(after);assert(disk_read_at(BOOK_PROGRESS_SD_DIR,fresh_key,after,&n,NULL)==ESP_OK&&record_uncertain(after));
 // 原记录重新可读时优先原副本；临时TF记录不覆盖它。
 // A readable original outranks the provisional TF session; retain both copies.
 uint8_t old[RECORD_CAP];raw_record(old,new_path,70,1);assert(disk_write_at(BOOK_PROGRESS_DIR,fresh_key,old,raw_len(new_path))==ESP_OK);
 primary_ready=true;check(new_path,70);
 p.pct=64;p.byte_off=6400;assert(book_progress_save(other,&p)==ESP_OK);check(other,64);
 primary_ready=true;
 assert(book_progress_restore_begin()==ESP_OK);raw_record(old,path,37,1);assert(book_progress_restore_record(key,old,raw_len(path))==ESP_OK);check(path,82);
 assert(book_progress_restore_commit()==ESP_OK);primary_ready=false;check(path,37);check(other,64);
 n=sizeof(after);assert(disk_read_at(BOOK_PROGRESS_DIR,key,after,&n,NULL)==ESP_OK&&n==original_len&&!memcmp(after,original,n));
 assert(book_progress_restore_begin()==ESP_OK);raw_record(old,path,12,2);assert(book_progress_restore_record(key,old,raw_len(path))==ESP_OK);book_progress_restore_abort();check(path,37);
 assert(book_progress_restore_begin()==ESP_OK);raw_record(old,path,44,3);assert(book_progress_restore_record(key,old,raw_len(path))==ESP_OK);assert(book_progress_restore_commit()==ESP_OK);check(path,44);
 char active[PROGRESS_ROOT_MAX];uint32_t id=0;assert(progress_active_dir(active,&id));
 char meta[PROGRESS_FILE_MAX];for(unsigned slot=0;slot<2;++slot){uint8_t b[16];snprintf(meta,sizeof(meta),BOOK_PROGRESS_SD_ROOT "/progress.active.%u",slot);FILE*f=fopen(meta,"rb");if(!f)continue;bool found=fread(b,1,16,f)==16&&get32(b+4)==id;fclose(f);if(found)break;}
 FILE *file=fopen(meta,"r+b");assert(file&&fputc(0,file)!=EOF&&!fclose(file));check(path,37);
 // 无可用持久存储时明确失败，不能声称保存成功或改写旧数据。
 // With no writable store, fail explicitly without claiming a save or replacing legacy data.
 test_sd_mounted=false;raw_record(old,path,91,200);assert(nvs_set_blob(1,key,old,raw_len(path))==ESP_OK);check(path,91);
 p.pct=0;p.byte_off=0;assert(book_progress_save(path,&p)!=ESP_OK);n=sizeof(after);assert(nvs_get_blob(1,key,after,&n)==ESP_OK&&!memcmp(after,old,n));
 puts("PASS: actual dual-board progress resumes/saves on TF after internal mount failure; checked legacy copies, newer-source choice, provisional-origin protection, staged restore/abort, corrupt-pointer recovery and original bytes retained");
}
'''
with tempfile.TemporaryDirectory(prefix='progress-failover-') as d:
 p=Path(d);(p/'test.c').write_text(unit)
 for board in [0,1]:
  root=p/str(board);root.mkdir();card=root/'card';card.mkdir()
  cmd=['cc','-std=gnu11','-D_DEFAULT_SOURCE','-DESP_PLATFORM',f'-DKIIKO_BOARD_METALIO47={board}','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-I'+str(R/'tools/book_storage_stubs'),'-I'+str(R/'main/book'),f'-DPROGRESS_SOURCE="{R}/main/book/book_progress.c"',f'-DBOOK_PROGRESS_DIR="{root}/internal"',f'-DBOOK_PROGRESS_SD_ROOT="{card}"',str(p/'test.c'),'-o',str(root/'test')]
  subprocess.run(cmd,check=True);subprocess.run([str(root/'test')],check=True)

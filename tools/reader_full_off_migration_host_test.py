#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：两板一次性关闭周期全刷迁移，失败不消耗标记。
English: Exercise the one-time full-refresh migration and retryable read/write failures on both boards.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int nvs_handle_t;typedef int esp_err_t;
enum{ESP_OK,ESP_ERR_NVS_NOT_FOUND,ESP_FAIL,NVS_READWRITE};
#define NVS_NS "board"
#define NVS_KEY_READER_FULL "full"
#define NVS_KEY_FULL_OFF_MIGRATION "migration"
static uint8_t s_reader_full_pages=15,stored_full=15,draft_full;
static bool marker,draft_marker;static int failure,writes,commits;
static int nvs_get_u8(int h,const char *k,uint8_t *v){assert(h==1&&!strcmp(k,"migration"));*v=marker;return failure==1?ESP_FAIL:marker?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
static int nvs_open(const char *ns,int mode,int *h){assert(!strcmp(ns,NVS_NS)&&mode==NVS_READWRITE);*h=2;draft_full=stored_full;draft_marker=marker;return failure==2?ESP_FAIL:ESP_OK;}
static int nvs_set_u8(int h,const char *k,uint8_t v){assert(h==2);++writes;
 if(!strcmp(k,"full")){assert(v==0);if(failure==3)return ESP_FAIL;draft_full=v;}
 else{assert(!strcmp(k,"migration")&&v==1);if(failure==4)return ESP_FAIL;draft_marker=true;}return ESP_OK;}
static int nvs_commit(int h){assert(h==2);++commits;if(failure==5)return ESP_FAIL;stored_full=draft_full;marker=draft_marker;return ESP_OK;}
static void nvs_close(int h){assert(h==2);}
'''
cases=r'''
int main(void){
 for(failure=1;failure<=5;++failure){
  marker=false;stored_full=s_reader_full_pages=15;writes=commits=0;
  migrate_reader_full_off(1);assert(!marker&&stored_full==15);
  assert(s_reader_full_pages==(failure==1?15:0));
  if(failure<5)assert(!commits);
 }
 failure=0;marker=false;stored_full=s_reader_full_pages=30;migrate_reader_full_off(1);
 assert(marker&&!stored_full&&!s_reader_full_pages);
 stored_full=s_reader_full_pages=10;writes=commits=0;migrate_reader_full_off(1);
 assert(stored_full==10&&s_reader_full_pages==10&&!writes&&!commits);
 puts("PASS: one-time periodic-refresh migration, preserved later user choices, read/open/set/marker/commit failures and retry");
}
'''
for relative in ['boards/pico/src/main/settings.c','main/settings.c']:
 with tempfile.TemporaryDirectory(prefix='kiiko-full-off-') as d:
  p=Path(d);(p/'test.c').write_text(unit+function('migrate_reader_full_off',R/relative)+cases)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""真实引导NVS事务：新机/恢复出厂、升级、断电及保存失败。 / Actual setup NVS lifecycle and failure transactions."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder)
 (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NVS_NOT_FOUND 1\n')
 (p/'nvs.h').write_text('''#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_set_u8(nvs_handle_t,const char*,uint8_t);
int nvs_get_u8(nvs_handle_t,const char*,uint8_t*);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
int nvs_get_used_entry_count(nvs_handle_t,size_t*);
''')
 (p/'test.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "setup_state.h"
#include "kiiko_board_config.h"
#include "nvs.h"
#ifdef KIIKO_UI_LOCALIZATION
#include "ui_locale.h"
bool ui_locale_test_readonly(const void *p){(void)p;return true;}
#define CHECK_LOCALE() assert(ui_locale_english()==app_setup_english())
#else
#define CHECK_LOCALE() ((void)0)
#endif
static bool exists,has_state,has_lang,has_library,commit_fail,open_fail,read_fail,count_fail;
static unsigned entries,erases,writes;
static uint8_t state,language,draft_state,draft_lang,library,draft_library;
int nvs_open(const char*n,int mode,nvs_handle_t*h){assert(!strcmp(n,KIIKO_BOARD_SETTINGS_NAMESPACE));*h=1;if(open_fail)return 8;if(!exists&&mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;if(mode==NVS_READWRITE){exists=true;draft_state=state;draft_lang=language;draft_library=library;}return 0;}
int nvs_set_u8(nvs_handle_t h,const char*k,uint8_t v){assert(h==1);++writes;if(!strcmp(k,"setup_v1"))draft_state=v;else if(!strcmp(k,"shelf_init")){draft_library=v;has_library=true;}else {assert(!strcmp(k,"ui_lang"));draft_lang=v;}return 0;}
int nvs_get_u8(nvs_handle_t h,const char*k,uint8_t*v){assert(h==1);if(read_fail)return 8;if(!strcmp(k,"setup_v1")){if(!has_state)return 1;*v=state;}else if(!strcmp(k,"shelf_init")){if(!has_library)return 1;*v=library;}else {assert(!strcmp(k,"ui_lang"));if(!has_lang)return 1;*v=language;}return 0;}
int nvs_commit(nvs_handle_t h){assert(h==1);if(commit_fail)return 9;state=draft_state;language=draft_lang;library=draft_library;has_state=has_lang=true;entries=2;return 0;}
void nvs_close(nvs_handle_t h){assert(h==1);}
int nvs_get_used_entry_count(nvs_handle_t h,size_t*c){assert(h==1);*c=entries;return count_fail?8:0;}
static void reset(void){exists=has_state=has_lang=has_library=commit_fail=open_fail=read_fail=count_fail=false;entries=erases=writes=state=language=library=0;}
int main(void){
 reset();app_setup_init();assert(app_setup_pending()&&state==0&&!app_setup_english());CHECK_LOCALE();
 assert(app_setup_set_english(true)==0&&app_setup_english());CHECK_LOCALE();app_setup_init();assert(app_setup_pending()&&app_setup_english());CHECK_LOCALE();
 commit_fail=true;assert(app_setup_finish()==9&&app_setup_pending());assert(app_setup_set_english(false)==9&&app_setup_english());CHECK_LOCALE();
 commit_fail=false;assert(app_setup_finish()==0&&!app_setup_pending());app_setup_init();assert(!app_setup_pending()&&app_setup_english());CHECK_LOCALE();
 assert(app_setup_library_pending());commit_fail=true;assert(app_setup_library_complete()==9&&app_setup_library_pending());commit_fail=false;assert(app_setup_library_complete()==0&&!app_setup_library_pending());app_setup_init();assert(!app_setup_library_pending());
 reset();exists=true;entries=12;app_setup_init();assert(!app_setup_pending()&&writes==0); // legacy upgrade / 老配置升级
 reset();exists=true;app_setup_init();assert(app_setup_pending()&&state==0); // erased but namespace kept / 出厂重置后空命名空间
 reset();exists=true;entries=1;count_fail=true;app_setup_init();assert(!app_setup_pending()&&writes==0);
 reset();exists=has_state=true;state=7;entries=3;app_setup_init();assert(!app_setup_pending()&&writes==0); // corrupt is not fresh / 损坏不是新机
 reset();open_fail=true;app_setup_init();assert(!app_setup_pending()&&writes==0);CHECK_LOCALE();
 reset();exists=true;read_fail=true;app_setup_init();assert(!app_setup_pending()&&writes==0);
 assert(erases==0);puts("PASS: fresh/reset/pending/completed/legacy NVS, persisted language, interrupted setup, commit/read failures without erasure");
}
''')
 for localized in [False,True]:
  exe=p/('test-en' if localized else 'test')
  flags=['-DKIIKO_UI_LOCALIZATION=1','-DKIIKO_LOCALE_TEST=1','-I'+str(root/'components/kiiko_locale/include'),str(root/'components/kiiko_locale/ui_locale.c')] if localized else []
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(root/'main'),str(p/'test.c'),str(root/'main/setup_state.c'),*flags,'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)

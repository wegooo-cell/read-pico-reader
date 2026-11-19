#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the ESP storage preflight in the real settings restore transaction."""
from pathlib import Path
import json,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
view=R/'.kiiko-source-view.json'
board=json.loads(view.read_text())['board'] if view.exists() else 'metalio47'
fixture=R/('tools/settings_backup_host_test.c' if view.exists() else 'boards/metalio47/tests/tools/settings_backup_host_test.c')
legacy=fixture.read_text().replace('#include "../main/settings.c"','#include '+json.dumps(str(R/'main/settings.c')))
unit=r'''
#define ESP_PLATFORM 1
#include "esp_err.h"
static esp_err_t storage_error;
static unsigned storage_calls;
esp_err_t book_progress_prepare_storage(void){++storage_calls;return storage_error;}
#define main legacy_settings_tests
'''+legacy+r'''
#undef main
int main(void){
 legacy_settings_tests();
 app_settings_set_book_px(40);assert(app_settings_backup_save()==ESP_OK);
 app_settings_set_book_px(44);int before=commit_count;storage_calls=0;storage_error=ESP_ERR_NO_MEM;
 assert(app_settings_backup_restore()==ESP_ERR_NO_MEM&&storage_calls==1);
 assert(commit_count==before&&app_settings_book_px()==44);
 storage_error=ESP_OK;assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_px()==40);
 puts("PASS: full legacy settings suite with ESP preflight; storage OOM aborts before any NVS commit or live setting change, recovery restores normally");
}
'''
with tempfile.TemporaryDirectory(prefix='settings-storage-') as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 # Localization is tested separately; this fixture isolates actual persistence and validation.
 (p/'ui_locale_format.h').write_text('#pragma once\n')
 (R/'build/book-tests/settings-card').mkdir(parents=True,exist_ok=True)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
  f'-DKIIKO_BOARD_METALIO47={int(board=="metalio47")}','-I'+str(p),'-I'+str(R/'tools/settings_backup_stubs'),
  '-I'+str(R/'components/read_pico_transfer/include'),str(p/'test.c'),'-o',str(p/'test')],cwd=R,check=True)
 subprocess.run([str(p/'test')],cwd=R,check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：真实设置的全档保存、重启、完整备份及旧格式投影。
English: Exhaustive real-settings persistence, reload, full backup and legacy projection."""
from pathlib import Path
import json,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
view=R/'.kiiko-source-view.json'
board=json.loads(view.read_text())['board'] if view.exists() else 'metalio47'
fixture=R/('tools/settings_backup_host_test.c' if view.exists() else 'boards/metalio47/tests/tools/settings_backup_host_test.c')
unit=fixture.read_text().replace('#include "../main/settings.c"','#include '+json.dumps(str(R/'main/settings.c')))
unit='#define main historical_settings_tests\n'+unit+'\n#undef main\n#undef app_settings_backup_save\n'
unit+=r'''
static settings_backup_v1_t read_base(const char *path){
 FILE *file=fopen(path,"rb");settings_backup_v1_t value;
 assert(file&&fread(&value,1,sizeof(value),file)==sizeof(value)&&fclose(file)==0);
 assert(backup_valid(&value));return value;
}
int main(void){
 remove_backup_files();app_settings_init();
 assert(BOOK_READER_PX_MIN==20&&BOOK_READER_PX_MAX==120);
 for(int line=100;line<=300;++line){
  app_settings_set_book_line_spacing(line);assert(app_settings_book_line_spacing()==line);
  assert(loaded_line==book_reader_legacy_line(line)&&loaded_line_full==line-100);
  s_book_line=0;app_settings_init();assert(app_settings_book_line_spacing()==line);
  assert(app_settings_backup_save()==ESP_OK);
  settings_backup_v1_t legacy=read_base(BACKUP_FILE),full=read_base(BACKUP_FULL);
  assert(backup_version(&legacy)==25&&backup_version(&full)==27);
  assert(legacy.flags[BK_LINE_SPACING]==book_reader_legacy_line(line));
  assert(full.flags[BK_LINE_SPACING]==line-100);
  s_book_line=0;assert(app_settings_backup_restore()==ESP_OK&&s_book_line==line);
 }
 for(int para=-100;para<=100;para+=10){
  app_settings_set_book_paragraph_spacing(para);assert(s_book_para==para);
  assert(loaded_para==book_reader_legacy_paragraph(para)&&loaded_para_full==para+100);
  s_book_para=999;app_settings_init();assert(s_book_para==para);
  assert(app_settings_backup_save()==ESP_OK);
  settings_backup_v1_t legacy=read_base(BACKUP_FILE),full=read_base(BACKUP_FULL);
  assert(legacy.flags[BK_PARAGRAPH]==book_reader_legacy_paragraph(para));
  assert(full.flags[BK_PARAGRAPH]==para+100);
  s_book_para=999;assert(app_settings_backup_restore()==ESP_OK&&s_book_para==para);
 }
 for(int px=20;px<=120;++px){
  app_settings_set_book_px(px);assert(s_book_px==px&&app_settings_backup_save()==ESP_OK);
  s_book_px=0;app_settings_init();assert(s_book_px==px);
  settings_backup_v1_t legacy=read_base(BACKUP_FILE),full=read_base(BACKUP_FULL);
  assert(legacy.flags[BK_BOOK_PX]==(px>90?90:px)&&full.flags[BK_BOOK_PX]==px);
  s_book_px=0;assert(app_settings_backup_restore()==ESP_OK&&s_book_px==px);
 }
#if !KIIKO_BOARD_METALIO47
 app_settings_set_reader_turn_effect(3);assert(s_reader_turn_effect==3&&app_settings_backup_save()==ESP_OK);
 settings_backup_v1_t fast_legacy=read_base(BACKUP_FILE),fast_full=read_base(BACKUP_FULL);
 assert(fast_legacy.flags[BK_TURN]==0&&fast_full.flags[BK_TURN]==3);
 s_reader_turn_effect=0;assert(app_settings_backup_restore()==ESP_OK&&s_reader_turn_effect==3);
#endif
 app_settings_set_book_line_spacing(300);app_settings_set_book_paragraph_spacing(-100);
 app_settings_set_book_line_spacing(301);app_settings_set_book_line_spacing(99);
 app_settings_set_book_paragraph_spacing(-110);app_settings_set_book_paragraph_spacing(101);app_settings_set_book_paragraph_spacing(1);
 assert(s_book_line==300&&s_book_para==-100);
 commit_fails=true;assert(!app_settings_try_book_layout(48,24,100,0)&&s_book_px==120&&s_book_line==300&&s_book_para==-100);app_settings_set_book_line_spacing(200);app_settings_set_book_paragraph_spacing(50);commit_fails=false;
 assert(s_book_line==300&&s_book_para==-100);
 uint8_t *wire=NULL;size_t size=0;
 assert(app_settings_sync_export(&wire,&size)==ESP_OK);
 s_book_line=130;s_book_para=50;s_book_px=48;
 assert(app_settings_sync_import(wire,size)==ESP_OK&&s_book_line==300&&s_book_para==-100&&s_book_px==120);
 ((settings_backup_v1_t*)wire)->flags[BK_LINE_SPACING]=201;
 backup_seal((settings_backup_v1_t*)wire);
 assert(app_settings_sync_import(wire,size)!=ESP_OK&&s_book_line==300&&s_book_para==-100);free(wire);
 loaded_book_px=18;app_settings_init();assert(s_book_px==20);
 loaded_book_px=19;app_settings_init();assert(s_book_px==20);
 // 旧固件改变兼容键后，不能回放过期新范围值。/ A legacy edit invalidates the new-range NVS companion.
 loaded_line=120;loaded_line_full=200;loaded_para=50;loaded_para_full=0;has_para=true;
 app_settings_init();assert(s_book_line==120&&s_book_para==50);
 remove_backup_files();
 puts("PASS: 201 line heights, 21 signed paragraph stops, 101 type sizes; actual NVS/reload/backup/wire import, v25 projection, invalid/corrupt values, failed commits and legacy edits");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-range-settings-') as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 (R/'build/book-tests/settings-card').mkdir(parents=True,exist_ok=True)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
  f'-DKIIKO_BOARD_METALIO47={int(board=="metalio47")}','-I'+str(R/'tools/settings_backup_stubs'),
  '-I'+str(R/'components/read_pico_transfer/include'),str(p/'test.c'),'-o',str(p/'test')],cwd=R,check=True)
 subprocess.run([str(p/'test')],cwd=R,check=True)

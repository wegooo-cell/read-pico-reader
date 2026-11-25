#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise real compatible/full card backups and stale-companion protection."""
from pathlib import Path
import json,subprocess,tempfile,sys,struct
R=Path(__file__).resolve().parents[1]
view=R/'.kiiko-source-view.json'
board=json.loads(view.read_text())['board'] if view.exists() else 'metalio47'
fixture=R/('tools/settings_backup_host_test.c' if view.exists() else 'boards/metalio47/tests/tools/settings_backup_host_test.c')
unit=fixture.read_text().replace('#include "../main/settings.c"','#include '+json.dumps(str(R/'main/settings.c')))
unit='#define main legacy_settings_tests\n'+unit+'\n#undef main\n#undef app_settings_backup_save\n#undef app_settings_sync_export\n'
unit+=r'''
static unsigned card_version(const char *path){
 FILE *file=fopen(path,"rb");settings_backup_v1_t value;
 assert(file&&fread(&value,1,sizeof(value),file)==sizeof(value)&&fclose(file)==0);
 return backup_version(&value);
}
int main(int argc,char **argv){
 remove_backup_files();app_settings_init();app_settings_set_book_px(40);
 s_book_reading_line=2;s_book_rule_full=true;s_book_rule_offset=7;
 assert(app_settings_save_book_reading_line());uint8_t guide=s_book_rule_saved;
 assert(app_settings_set_shelf_covers(false));
 s_book_reading_line=0;app_settings_set_book_line_spacing(300);app_settings_set_book_paragraph_spacing(-100);
 assert(app_settings_backup_save()==ESP_OK&&card_version(BACKUP_FILE)==25&&card_version(BACKUP_FULL)==29);
 assert(backup_pair_matches(BACKUP_FILE));
 if(argc==2)assert(copy_backup_file(BACKUP_FULL,argv[1]));
 s_book_rule_saved=1;app_settings_set_book_px(48);s_shelf_covers=true;
 assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_px()==40&&!app_settings_shelf_covers()&&s_book_rule_saved==guide&&app_settings_book_line_spacing()==300&&app_settings_book_paragraph_spacing()==-100);
 // An older firmware updates only the interchange file. Never replay its stale full companion.
 app_settings_set_book_px(52);FILE *file=fopen(BACKUP_TEMP,"wb");assert(file);
 assert(backup_write_stream(file,true,true)==ESP_OK&&!backup_pair_matches(BACKUP_FILE));
 app_settings_set_book_px(44);
 // 互换文件没有封面开关，保留当前选择。/ Interchange lacks the cover switch: preserve its current choice.
 assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_px()==52&&!app_settings_shelf_covers());
 // A damaged companion leaves the independently checked interchange fully restorable.
 assert(app_settings_backup_save()==ESP_OK);file=fopen(BACKUP_FULL,"r+b");assert(file&&fputc(0,file)!=EOF&&!fclose(file));
 app_settings_set_book_px(44);assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_px()==52);
 // Invalid primary falls back to the last independently valid card copy.
 assert(copy_backup_file(BACKUP_FILE,BACKUP_PREVIOUS));file=fopen(BACKUP_FILE,"r+b");assert(file&&fputc(0,file)!=EOF&&!fclose(file));
 app_settings_set_book_px(44);assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_px()==52);
 // Pico effect 3 is valid on the wire; an S31 receiver restores ordinary mode.
 s_reader_turn_effect=3;uint8_t *wire=NULL;size_t bytes=0;
 assert(app_settings_sync_export(&wire,&bytes)==ESP_OK);
 s_reader_turn_effect=0;assert(app_settings_sync_import(wire,bytes)==ESP_OK);
 assert(app_settings_reader_turn_effect()==(READER_TURN_EFFECT_MAX>=3?3:0));free(wire);
 remove_backup_files();
 puts("PASS: v25 interchange/v29 full pair, saved guide, legacy-write invalidation, damaged companion/primary fallback, cross-board turn normalization");
}
'''
with tempfile.TemporaryDirectory(prefix='settings-pair-') as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 (R/'build/book-tests/settings-card').mkdir(parents=True,exist_ok=True)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
  f'-DKIIKO_BOARD_METALIO47={int(board=="metalio47")}', '-I'+str(R/'tools/settings_backup_stubs'),'-I'+str(R/'components/read_pico_transfer/include'),str(p/'test.c'),'-o',str(p/'test')],cwd=R,check=True)
 subprocess.run([str(p/'test'),*sys.argv[1:]],cwd=R,check=True)
 if len(sys.argv)==2:
  output=Path(sys.argv[1]);data=output.read_bytes();assert data[-8:]==b'RPHIST1\0'
  output.write_bytes(data+b'\xff'+b'\0'*19+struct.pack('<HI',0,2166136261))

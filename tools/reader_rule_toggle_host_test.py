#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Saved guide gestures exercise real preferences, deferred reflow and guide rasterization."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
unit=(R/'tools/book_layout_host_test.c').read_text().replace('int main(void) {','static void legacy_layout_tests(void) {')+r'''
typedef int nvs_handle_t,esp_err_t;
#define ESP_OK 0
#define NVS_READWRITE 1
#define NVS_NS "settings"
#define NVS_KEY_BOOK_RULE "style"
#define NVS_KEY_BOOK_RULE_FULL "full"
#define NVS_KEY_BOOK_RULE_OFFSET "offset"
static uint8_t s_book_reading_line=1,s_book_rule_offset=4;
static bool s_book_rule_full;
static uint8_t durable[4],transaction[4];
static int fail_step=-1;
static bool nvs_failure(void){if(!fail_step)return true;if(fail_step>0)--fail_step;return false;}
static int key_index(const char *key){return !strcmp(key,"style")?0:!strcmp(key,"full")?1:!strcmp(key,"offset")?2:3;}
static int nvs_open(const char *name,int mode,nvs_handle_t *h){(void)name;(void)mode;*h=1;memcpy(transaction,durable,4);return nvs_failure()?-1:ESP_OK;}
static int nvs_get_u8(int h,const char *key,uint8_t *value){(void)h;*value=durable[key_index(key)];return ESP_OK;}
static int nvs_set_u8(int h,const char *key,uint8_t value){(void)h;if(nvs_failure())return -1;transaction[key_index(key)]=value;return ESP_OK;}
static int nvs_commit(int h){(void)h;if(nvs_failure())return -1;memcpy(durable,transaction,4);return ESP_OK;}
static void nvs_close(int h){(void)h;}
'''+function('shelf_preference_put',R/'main/settings_shelf_followup.inc')+'\n'+(R/'main/settings_reading_line.inc').read_text()+r'''
typedef struct{int64_t now_ms;}app_ctx_t;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_PAGE}app_redraw_t;
enum{READING,SHELF};
static int s_view=READING,s_px=40;
static size_t s_page,s_text_len,s_block_count;
static char *s_text,s_message[128];static blk_t *s_blocks;
static bool s_reader_rule_pending,locked;
static int pauses,invalidations,saves,image_preps;
static unsigned app_settings_book_reading_line(void){return s_book_reading_line;}
static bool app_settings_book_reading_line_full(void){return s_book_rule_full;}
static int app_settings_book_reading_line_offset(void){return ((int)s_book_rule_offset-4)*2;}
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static void pause_reader_prep(void){++pauses;}
static void invalidate_prep(void){++invalidations;}
static EpdRect body_rect(void){return(EpdRect){24,84,636,1068};}
static void save_progress(void){assert(!locked);++saves;}
static void prepare_inline_image(void){assert(!locked);++image_preps;}
static void copy_text(char *to,size_t size,const char *from){snprintf(to,size,"%s",from);}
static void free_book(void){book_layout_free();s_text=NULL;s_reader_rule_pending=false;}
'''+(R/'main/apps/book_reader_rule.inc').read_text()+r'''
static int notices;
static void rc_notice(app_ctx_t *ctx,const char *text){(void)ctx;(void)text;++notices;}
static app_redraw_t gesture(app_ctx_t *ctx){
'''
runtime=(R/'main/apps/book_reader_controls_runtime.inc').read_text()
unit+=runtime.split('    case RC_GUIDE:',1)[1].split('    case RC_CONTENTS:',1)[0]+'}\n'+r'''
int main(void){
 legacy_layout_tests();
 book_layout_set_heading_measure(NULL);book_layout_set_heading_draw(NULL);
 book_layout_set_chapter_viewport(0,0);book_layout_set_chapter_lead(0,0);
 book_layout_set_vertical(false);book_layout_set_vertical_balance(true);
 book_layout_set_typography(0);book_layout_set_first_line_indent(0);
 book_layout_set_spacing(110,0);book_layout_set_reader_spacing(true);
 book_layout_set_inline_images(NULL,0);book_layout_set_image_dims(NULL,NULL);
 book_layout_set_runs(NULL,0);test_slot=0;memset(test_slot_advance,0,sizeof(test_slot_advance));
 test_cjk_advance=0;test_zero_width=false;
 static char prose[8000];memset(prose,'x',sizeof(prose)-1);s_text=prose;s_text_len=strlen(prose);
 assert(book_layout_build_blocks(s_text,s_text_len,NULL,0,body_rect(),s_px));s_page=1;
 app_ctx_t ctx={.now_ms=1000};
 for(unsigned style=1;style<=2;++style)for(unsigned full=0;full<2;++full)for(unsigned off=0;off<9;++off){
  s_book_reading_line=style;s_book_rule_full=full;s_book_rule_offset=off;
  assert(app_settings_save_book_reading_line());uint8_t saved=s_book_rule_saved;
  // Unsaved edits must not overwrite the preset, even when the guide is disabled.
  s_book_rule_full=!full;s_book_rule_offset=8-off;
  for(int toggle=0;toggle<2;++toggle){
   size_t anchor=book_layout_page_start_offset(s_page);uint32_t gen=book_layout_generation();
   assert(gesture(&ctx)==APP_REDRAW_NONE&&s_reader_rule_pending&&book_layout_generation()==gen);
   if(!toggle)assert(!s_book_reading_line);else assert(s_book_reading_line==style&&s_book_rule_full==(bool)full&&s_book_rule_offset==off);
   assert(reader_rule_tick(&ctx)==APP_REDRAW_PAGE&&!s_reader_rule_pending&&!locked);
   assert(s_page==book_layout_page_for_offset(anchor));
   uint8_t fb[32]={0};test_guide_segments=0;drawn[0]=0;captured_count=0;book_layout_draw_page(fb,s_page,body_rect(),s_px);
   assert((test_guide_segments>0)==(toggle!=0));
   // Model a reboot with the active style off/on and reload the independent saved format.
   s_book_rule_saved=0;book_rule_saved_load(1);assert(s_book_rule_saved==saved);
  }
  // Every failing transaction step leaves runtime state and the durable preset untouched.
  for(int step=0;step<6;++step){
   uint8_t previous[4];memcpy(previous,durable,4);fail_step=step;
   assert(gesture(&ctx)==APP_REDRAW_PAGE&&!s_reader_rule_pending&&s_book_reading_line==style);
   assert(!memcmp(previous,durable,4)&&s_book_rule_saved==saved);fail_step=-1;
  }
 }
 assert(pauses==(KIIKO_BOARD_METALIO47?72:0)&&invalidations==(KIIKO_BOARD_METALIO47?72:144)&&saves==72&&image_preps==72);
 s_book_reading_line=0;assert(!app_settings_save_book_reading_line());
 durable[3]=255;s_book_rule_saved=0;book_rule_saved_load(1);assert(book_rule_saved_valid(s_book_rule_saved)&&s_book_rule_saved);
 // Failed allocations return safely to the shelf; no stale layout remains drawable.
 assert(apply_reader_rule(&ctx)==APP_REDRAW_NONE);test_heap_fail_after=0;
 assert(reader_rule_tick(&ctx)==APP_REDRAW_PAGE&&s_view==SHELF&&!s_text&&!locked);
 test_heap_fail_after=-1;
 puts("PASS: 36 saved formats restore through real gesture dispatch, reboot, actual guide raster and anchored shallow-stack reflow; failed NVS transactions and OOM retain a safe state");
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 for board in (0,1):
  subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-Wno-unused-function',
   f'-DKIIKO_BOARD_METALIO47={board}','-fsanitize=address,undefined','-I'+str(R/'tools/book_layout_stubs'),'-I'+str(R/'tools/book_source_host_stubs'),
   '-I'+str(R/'main/book'),'-I'+str(R/'main'),str(p/'test.c'),str(R/'main/book/book_layout.c'),str(R/'main/book/html_text.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

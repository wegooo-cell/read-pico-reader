#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Actual S31 font selection: transient parse errors, bounded retries and suspended wake faces."""
from pathlib import Path
import subprocess,tempfile,json
from home_cover_cache_host_test import function
from board_source_view import select_source
R=Path(__file__).resolve().parents[1]
marker=R/'.kiiko-source-view.json'
if marker.exists():R=Path(json.loads(marker.read_text())['source'])
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define TTF_FONT_PATH_MAX 256
#define TTF_FONT_BUILTIN "builtin"
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
enum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_RESPONSE=0x108};
typedef struct{char path[256];}ttf_font_item_t;
typedef struct{char path[256];bool reading;}app_lock_font_t;
static bool s_reading,pending,sd_ready=true,abnormal,face_ready=true;
static char s_rejected[256],active[256]="builtin";
static time_t s_rejected_mtime,mtime=123;
static off_t s_rejected_size;
static int64_t s_rejected_retry_us,now_us;
static unsigned s_rejected_attempts,external_opens,builtin_opens;
static int next_error,persistent_error;
static const char reader[]="/sdcard/fonts/reader.ttf";
static const char *system_chosen=reader;
static int mock_stat(const char*p,struct stat*s){(void)p;memset(s,0,sizeof(*s));s->st_mode=S_IFREG;s->st_size=100;s->st_mtime=mtime;return 0;}
#define stat(p,s) mock_stat(p,s)
static bool pico_boot_recovery(void){return abnormal;}
static bool app_sleep_media_pending(void){return pending;}
static bool read_pico_sd_io_ready(void){return sd_ready;}
static int64_t esp_timer_get_time(void){return now_us;}
static const char *app_settings_font_path(void){return reader;}
static const char *app_settings_system_font_path(void){return system_chosen;}
static const char *app_settings_fonts_dir(void){return "/sdcard/fonts";}
static int app_settings_book_weight_fine(void){return 0;}
static bool app_settings_book_antialias(void){return true;}
static bool ttf_font_ready(void){return face_ready;}
static const char *ttf_font_path(void){return active;}
static bool ttf_font_path_is_builtin(const char*p){return !p||!*p||!strcmp(p,"builtin");}
static bool ttf_font_is_builtin(void){return ttf_font_path_is_builtin(active);}
static int ttf_font_count(void){return 0;}
static int ttf_font_scan(void){return 0;}
static const ttf_font_item_t *ttf_font_item(int i){(void)i;return NULL;}
static int ttf_font_open(const char*p){
 if(!ttf_font_path_is_builtin(p)){
  ++external_opens;int e=next_error?next_error:persistent_error;next_error=0;
  if(e){strcpy(active,"builtin");face_ready=true;return e;}
 }else ++builtin_opens;
 snprintf(active,sizeof(active),"%s",p);face_ready=true;return ESP_OK;
}
static int ttf_font_open_builtin(void){return ttf_font_open("builtin");}
static void ttf_set_stroke_adjust(int n){(void)n;}
static void ttf_set_stroke_adjust_fine(int n){(void)n;}
static void ttf_set_antialias(bool b){(void)b;}
static void ui_text_set_system_font(bool b){(void)b;}
static void ui_text_set_system_scale(bool b){(void)b;}
static void ui_nav_status_font_context(bool a,bool b){(void)a;(void)b;}
'''
names=['font_file','system_path','activate','app_font_activate_system','app_font_activate_reading','app_font_retry_active','app_font_recover_media','app_font_begin_lock','app_font_end_lock']
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);f=p/'context.c';f.write_text(select_source((R/'main/font/app_font_context.c').read_text(),'metalio47'))
 for name in names:unit+=function(name,f)+'\n'
 unit+=r'''
static void reset(void){pending=abnormal=false;sd_ready=face_ready=true;now_us=0;external_opens=builtin_opens=0;
 s_rejected[0]=0;s_rejected_attempts=s_rejected_retry_us=0;next_error=persistent_error=0;mtime=123;system_chosen=reader;strcpy(active,"builtin");}
int main(void){
 for(int error=ESP_FAIL;error<=ESP_ERR_INVALID_RESPONSE;error+=ESP_ERR_INVALID_RESPONSE+1){
  reset();next_error=error;assert(!app_font_activate_reading()&&ttf_font_is_builtin());
  assert(external_opens==1);now_us=999000;assert(!app_font_retry_active()&&external_opens==1);
  now_us=1000000;assert(app_font_retry_active()&&!ttf_font_is_builtin()&&external_opens==2);
 }
 reset();persistent_error=ESP_ERR_INVALID_RESPONSE;assert(!app_font_activate_reading());
 for(int seconds=1;seconds<=3600;seconds++){now_us=(int64_t)seconds*1000000;assert(!app_font_retry_active());}
 assert(ttf_font_is_builtin()&&external_opens>=6&&external_opens<20);
 persistent_error=0;++mtime;assert(app_font_retry_active()&&!ttf_font_is_builtin());
 // A pending same-font wake used to interpret activate(false) as failure and unload the suspended face.
 reset();assert(app_font_activate_reading());app_lock_font_t saved;app_font_begin_lock(&saved);
 pending=true;face_ready=false;unsigned n=external_opens,b=builtin_opens;
 app_font_end_lock(&saved);assert(!face_ready&&external_opens==n&&builtin_opens==b&&!strcmp(active,reader)&&s_reading);
 pending=false;face_ready=true;assert(!app_font_recover_media()&&!ttf_font_is_builtin());
 reset();system_chosen="/sdcard/fonts/system.ttf";assert(app_font_activate_reading());app_font_begin_lock(&saved);
 assert(!strcmp(active,system_chosen));pending=true;face_ready=false;n=external_opens;b=builtin_opens;
 app_font_end_lock(&saved);assert(external_opens==n&&builtin_opens==b&&s_reading);
 pending=false;face_ready=true;assert(app_font_recover_media()&&!strcmp(active,reader));
 reset();next_error=ESP_ERR_INVALID_RESPONSE;app_font_activate_reading();
 assert(s_rejected[0]&&ttf_font_is_builtin());assert(app_font_recover_media()&&!s_rejected[0]&&!ttf_font_is_builtin());
 reset();abnormal=true;app_font_activate_reading();assert(!external_opens&&ttf_font_is_builtin());
 puts("PASS: actual S31 parse/IO failure retries without reboot, persistent corrupt faces rate-limited, file-change retry, pending wake retains slots, new media session clears rejection, abnormal-boot and saved choices retained");
}
'''
 (p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

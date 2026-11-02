#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""S31 keep-light policy persistence/faults and actual three-row UI geometry."""
from pathlib import Path
import subprocess,tempfile,json
from board_source_view import select_source
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
marker=R/'.kiiko-source-view.json'
if marker.exists():R=Path(json.loads(marker.read_text())['source'])
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;typedef int nvs_handle_t;
enum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_ARG=-2,NVS_READWRITE=1};
#define NVS_NS "metalio_s31"
#define NVS_KEY_SHUTDOWN_MODE "shutdown_mode"
typedef enum{APP_SLEEP_LIGHT,APP_SLEEP_DEEP,APP_SLEEP_OFF}app_sleep_mode_t;
typedef enum{APP_LOCK_SLEEP_TIMED,APP_LOCK_SLEEP_OFF,APP_LOCK_SLEEP_KEEP_LIGHT}app_lock_sleep_mode_t;
static app_sleep_mode_t s_sleep=APP_SLEEP_DEEP;
static bool s_staged_shutdown=true;
static int failure,persisted=1,pending=1,opened,closed;
static int nvs_open(const char*n,int mode,int*h){assert(!strcmp(n,NVS_NS)&&mode==1);if(failure==1)return ESP_FAIL;*h=7;++opened;return ESP_OK;}
static int nvs_set_u8(int h,const char*k,uint8_t v){assert(h==7&&!strcmp(k,NVS_KEY_SHUTDOWN_MODE));if(failure==2)return ESP_FAIL;pending=v;return ESP_OK;}
static int nvs_commit(int h){assert(h==7);if(failure==3)return ESP_FAIL;persisted=pending;return ESP_OK;}
static void nvs_close(int h){assert(h==7);++closed;}
static int nvs_get_u8(int h,const char*k,uint8_t*v){assert(h==7&&!strcmp(k,NVS_KEY_SHUTDOWN_MODE));if(failure)return ESP_FAIL;*v=persisted;return ESP_OK;}
typedef struct{int x,y,width,height;}EpdRect;
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);selected=p/'settings.c';selected.write_text(select_source((R/'main/settings.c').read_text(),'metalio47'))
 for name in ['load_lock_sleep_policy','app_settings_lock_sleep_mode','app_settings_keep_light_sleep','app_settings_set_lock_sleep_mode']:
  u+=function(name,selected)+'\n'
 u+=function('power_sleep_choice_rect',R/'main/apps/app_device_settings.c')
 u+=r'''
int main(void){
 assert(app_settings_lock_sleep_mode()==APP_LOCK_SLEEP_TIMED&&!app_settings_keep_light_sleep());
 assert(app_settings_set_lock_sleep_mode(APP_LOCK_SLEEP_KEEP_LIGHT)==ESP_OK&&persisted==2&&app_settings_keep_light_sleep());
 for(failure=1;failure<=3;failure++){
  assert(app_settings_set_lock_sleep_mode(APP_LOCK_SLEEP_OFF)==ESP_FAIL&&app_settings_keep_light_sleep()&&persisted==2);
 }
 failure=0;assert(app_settings_set_lock_sleep_mode(APP_LOCK_SLEEP_OFF)==ESP_OK&&persisted==0&&!app_settings_keep_light_sleep());
 assert(app_settings_set_lock_sleep_mode(APP_LOCK_SLEEP_TIMED)==ESP_OK&&persisted==1);
 assert(app_settings_set_lock_sleep_mode(3)==ESP_ERR_INVALID_ARG);
 assert(opened==closed);
 for(int raw=0;raw<3;raw++){
  persisted=raw;s_sleep=APP_SLEEP_DEEP;s_staged_shutdown=true;load_lock_sleep_policy(7);
  assert(app_settings_keep_light_sleep()==(raw==2));
  assert(app_settings_lock_sleep_mode()==(raw==0?APP_LOCK_SLEEP_OFF:raw==1?APP_LOCK_SLEEP_TIMED:APP_LOCK_SLEEP_KEEP_LIGHT));
 }
 persisted=255;s_sleep=APP_SLEEP_DEEP;s_staged_shutdown=true;load_lock_sleep_policy(7);
 assert(app_settings_lock_sleep_mode()==APP_LOCK_SLEEP_TIMED);
 int bottom=0;
 for(int i=0;i<3;i++){EpdRect r=power_sleep_choice_rect(i);assert(r.x==36&&r.width==612&&r.height==132&&r.y>=bottom);bottom=r.y+r.height;}
 assert(bottom<788&&power_sleep_choice_rect(3).width==0);
 puts("PASS: actual S31 three-state policy persists/reloads, legacy0/1/default/corrupt values, NVS open/set/commit failure retention, independent three-row paint/hit geometry");
}
'''
 (p/'test.c').write_text(u)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
# Rendering and gesture handling use the same geometry and enumerate all three choices.
s=select_source((R/'main/apps/app_device_settings.c').read_text(),'metalio47')
assert s.count('power_sleep_choice_rect(i)')==2 and '"保持浅睡"' in s
assert 'app_settings_set_lock_sleep_mode((app_lock_sleep_mode_t)i)' in s

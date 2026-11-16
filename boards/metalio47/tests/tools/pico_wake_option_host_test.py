#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实唤醒选项、设置命中与S31唤醒门。/ English: Actual wake preferences, setting hits and the S31 wake consumer."""
from pathlib import Path
import subprocess,tempfile,re
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
settings=ROOT/'main/settings.c'
ui=(ROOT/'main/apps/app_device_settings.c').read_text()
boot_path=ROOT/'boards/pico/src/main/app_main.c'
if not boot_path.exists(): boot_path=ROOT/'main/app_main.c'
boot=boot_path.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define NVS_KEY_WAKE_PRESSES "wake_press"
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
static uint8_t stored;static unsigned writes;

static void nvs_put_u8(const char *key,uint8_t value){assert(!strcmp(key,NVS_KEY_WAKE_PRESSES));stored=value;++writes;}
'''
unit+=re.search(r'^static uint8_t s_wake_presses = [12];',settings.read_text(),re.M)[0]+'\n'
for name in ('app_settings_wake_presses','app_settings_set_wake_presses'):
    unit+=function(name,settings)+'\n'
unit+=r'''
#include "ui_nav_layout.h"
#define KIIKO_BOARD_METALIO47 1
typedef struct {int x,y,width,height;}EpdRect;
enum {SETTINGS_MAIN,SETTINGS_POWER_SLEEP,SETTINGS_AUTO_LOCK,SETTINGS_SYSTEM_FONT,
 SETTINGS_SYSTEM_CONTRAST,SETTINGS_MAIN_REFRESH,SETTINGS_FRONTLIGHT,SETTINGS_LANGUAGE,APP_REDRAW_PAGE,APP_REDRAW_NONE};
static int s_page=SETTINGS_MAIN,s_font_page;static char s_notice[96];
static bool home_full;
static void ttf_font_scan(void){}
static bool app_settings_home_full_refresh(void){return home_full;}
static void app_settings_set_home_full_refresh(bool on){home_full=on;}
'''
for name in ('settings_profile_height','settings_main_row_height'):
    unit+=function(name,ROOT/'main/apps/app_device_settings.c')+'\n'
a=ui.index('#define SETTINGS_ROW_H');b=ui.index('// 设置行图标',a)
unit+=ui[a:b]
a=ui.index('    if(y>=SETTINGS_DISPLAY_Y &&',ui.index('static app_redraw_t on_gesture'))
b=ui.index('    if(y>=SETTINGS_PERSONAL_Y',a)
unit+=r'''
static int setting_tap(int x,int input_y,int scroll){
 int y=input_y,row;
 if (y<160 || y>=UI_NAV_TOP || x<36 || x>=648) return APP_REDRAW_NONE;
 y+=scroll;
'''+ui[a:b]+'return APP_REDRAW_NONE;}\n'
unit+=r'''
static bool load_exists;static uint8_t load_value;
static int nvs_get_u8(int h,const char *key,uint8_t *value){(void)h;assert(!strcmp(key,NVS_KEY_WAKE_PRESSES));if(!load_exists)return -1;*value=load_value;return ESP_OK;}
static void reload_wake(bool exists,uint8_t value){load_exists=exists;load_value=value;int h=0;uint8_t raw;
'''
a=settings.read_text().index('    raw = 1;\n    if (nvs_get_u8(h, NVS_KEY_WAKE_PRESSES,')
b=settings.read_text().index('    s_wake_presses = raw;',a)+len('    s_wake_presses = raw;')
unit+=settings.read_text()[a:b]+'}\n'
unit+=r'''
static bool single,double_key;
static bool read_pico_pmu_take_key_wakeup_single(void){bool value=single;single=false;return value;}
static bool read_pico_pmu_take_key_wakeup(void){bool value=double_key;double_key=false;return value;}
'''
unit+=function('lock_take_key_wakeup',ROOT/'main/sleep.c')+'\n'
unit+=r'''
int main(void){
 assert(app_settings_wake_presses()==1);
 reload_wake(false,2);assert(app_settings_wake_presses()==1);
 app_settings_set_wake_presses(0);assert(writes==0&&app_settings_wake_presses()==1);
 app_settings_set_wake_presses(2);assert(stored==2&&writes==1);
 app_settings_set_wake_presses(2);assert(writes==1);
 reload_wake(true,2);assert(app_settings_wake_presses()==2);
 reload_wake(true,1);assert(app_settings_wake_presses()==1);
 for(unsigned i=0;i<256;i++)if(i!=1&&i!=2){reload_wake(true,i);assert(app_settings_wake_presses()==1);}
 assert(SETTINGS_DISPLAY_ROWS==9);
 const int top=SETTINGS_DISPLAY_Y+5*SETTINGS_ROW_H,scroll=500;
 assert(setting_tap(36,top-scroll,scroll)==APP_REDRAW_PAGE&&s_wake_presses==2&&s_page==SETTINGS_MAIN);
 assert(setting_tap(647,top+SETTINGS_ROW_H-1-scroll,scroll)==APP_REDRAW_PAGE&&s_wake_presses==1&&s_page==SETTINGS_MAIN);
 assert(setting_tap(35,top-scroll,scroll)==APP_REDRAW_NONE&&s_wake_presses==1);
 assert(setting_tap(648,top-scroll,scroll)==APP_REDRAW_NONE&&s_wake_presses==1);
 assert(setting_tap(300,top-1-scroll,scroll)==APP_REDRAW_PAGE&&s_page==SETTINGS_AUTO_LOCK&&s_wake_presses==1);
 s_page=SETTINGS_MAIN;
 assert(setting_tap(300,top+SETTINGS_ROW_H-scroll,scroll)==APP_REDRAW_PAGE&&s_page==SETTINGS_POWER_SLEEP&&s_wake_presses==1);
 assert(setting_tap(300,UI_NAV_TOP,0)==APP_REDRAW_NONE);
 for(unsigned count=1;count<=2;++count){
  s_wake_presses=count;single=double_key=true;
  assert(lock_take_key_wakeup());assert(!lock_take_key_wakeup());
  assert(count==1?double_key:single);
 }
 puts("PASS: single-press default/missing/invalid NVS, explicit double choice retained, root switch below auto sleep, scrolled row/nav boundaries, S31 single/double wake events consumed exactly once");
}
'''
assert 'app_lock_boot_gate(&hl, hw.touch, hw.sensor);' in (ROOT/'main/app_main.c').read_text()
assert 'setting_toggle(fb, 882, "双击解锁"' not in ui
assert 'SETTINGS_UNLOCK_ICON,9,7' in ui
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(ROOT/'main/ui'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

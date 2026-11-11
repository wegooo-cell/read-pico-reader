#!/usr/bin/env python3
"""中文：验证真实配置编码、提交失败、跨板过滤和自动翻页时钟。
English: Exercise real preference encoding, failed commits, board filtering and the auto-turn clock.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reader_controls.h"
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define NVS_NS "test"
static bool exists,fail_commit;
static reader_controls_config_t disk,pending;
static int nvs_open(const char*s,int mode,nvs_handle_t*h){(void)s;(void)mode;*h=1;return 0;}
static void nvs_close(nvs_handle_t h){(void)h;}
static int nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){(void)h;(void)k;if(!exists)return 1;memcpy(out,&disk,*n);return 0;}
static int nvs_set_blob(nvs_handle_t h,const char*k,const void*data,size_t n){(void)h;(void)k;assert(n==sizeof(pending));memcpy(&pending,data,n);return 0;}
static int nvs_commit(nvs_handle_t h){(void)h;if(fail_commit)return 1;disk=pending;exists=true;return 0;}
#include "settings_reader_controls.inc"
int main(void){
 const reader_controls_config_t*c=app_settings_reader_controls();assert(reader_controls_valid(c));
 assert(reader_controls_seconds(c)==10&&c->actions[RC_BOTTOM_CENTER]==RC_TOOLS&&c->actions[RC_BOTTOM_RIGHT]==RC_AUTOTURN);
 assert(c->sides[0]==(KIIKO_BOARD_METALIO47?RC_SIDE_BRIGHTNESS:RC_SIDE_OFF));
 for(size_t i=0;i<sizeof(*c);++i){reader_controls_config_t bad=*c;((uint8_t*)&bad)[i]^=0x80;assert(!reader_controls_valid(&bad));}
 assert(!app_settings_set_reader_control(RC_SLOT_COUNT,RC_NONE));
 assert(!app_settings_set_reader_control(0,RC_ACTION_COUNT));
 assert(!app_settings_set_reader_auto_seconds(0)&&!app_settings_set_reader_auto_seconds(11)&&!app_settings_set_reader_auto_seconds(601));
 assert(app_settings_set_reader_control(RC_BOTTOM_CENTER,RC_EFFECT));assert(app_settings_reader_controls()->actions[RC_BOTTOM_CENTER]==RC_EFFECT);
 reader_controls_config_t before=*app_settings_reader_controls();fail_commit=true;
 assert(!app_settings_set_reader_control(RC_BOTTOM_CENTER,RC_NONE));
 assert(!memcmp(&before,app_settings_reader_controls(),sizeof(before)));fail_commit=false;
 assert(app_settings_set_reader_control(RC_BOTTOM_CENTER,RC_NONE));
 disk.seconds_le[0]=0;disk.seconds_le[1]=0;reader_controls_seal(&disk);s_reader_controls_loaded=false;
 assert(reader_controls_valid(&disk));
 assert(reader_controls_seconds(app_settings_reader_controls())==1&&app_settings_reader_controls()->actions[RC_BOTTOM_CENTER]==RC_NONE);
 assert(app_settings_set_reader_control(RC_BOTTOM_LEFT,RC_LIGHT)==!!KIIKO_BOARD_METALIO47);
 for(unsigned sec=1;sec<=10;++sec){assert(app_settings_set_reader_auto_seconds(sec));s_reader_controls_loaded=false;assert(reader_controls_seconds(app_settings_reader_controls())==sec);}
 disk=reader_controls_defaults(true);disk.seconds_le[0]=88;disk.seconds_le[1]=1;disk.actions[RC_BOTTOM_CENTER]=RC_GUIDE;reader_controls_seal(&disk);s_reader_controls_loaded=false;assert(reader_controls_seconds(app_settings_reader_controls())==10&&app_settings_reader_controls()->actions[RC_BOTTOM_CENTER]==RC_GUIDE);
 disk=reader_controls_defaults(true);disk.actions[RC_BOTTOM_LEFT]=RC_LIGHT;reader_controls_seal(&disk);s_reader_controls_loaded=false;
 assert(app_settings_reader_controls()->actions[RC_BOTTOM_LEFT]==(KIIKO_BOARD_METALIO47?RC_LIGHT:RC_BOOKMARK));
 reader_auto_clock_t clock={0};assert(!reader_auto_due(&clock,999999,5,true,false));
 reader_auto_start(&clock,1000,5);assert(!reader_auto_due(&clock,5999,5,true,false));
 assert(reader_auto_due(&clock,6000,5,true,false));assert(!reader_auto_due(&clock,6000,5,true,false));
 assert(reader_auto_due(&clock,90000,5,true,false));assert(clock.due_ms==95000);assert(!reader_auto_due(&clock,90000,5,true,false));
 assert(!reader_auto_due(&clock,95000,5,false,false)&&clock.running&&clock.due_ms==100000);assert(!reader_auto_due(&clock,99999,5,true,false));assert(reader_auto_due(&clock,100000,5,true,false));
 reader_auto_start(&clock,200000,1);assert(!reader_auto_due(&clock,200999,1,true,false));assert(reader_auto_due(&clock,201000,1,true,false)&&clock.running);reader_auto_stop(&clock);assert(!reader_auto_due(&clock,202000,1,true,false));
 reader_auto_start(&clock,1000,5);assert(!reader_auto_due(&clock,999999,5,true,true)&&!clock.running);
 reader_auto_start(&clock,INT64_C(4294967290),5);assert(!reader_auto_due(&clock,INT64_C(4294967296),5,true,false));
 assert(reader_auto_due(&clock,INT64_C(4294972290),5,true,false));reader_auto_stop(&clock);assert(!clock.running&&!clock.due_ms);
 puts("PASS: config CRC/defaults, editable center swipe, atomic save failures, board filtering, reload, interval bounds, 1-10s/all steps/legacy zero and long-interval migration, deferred menus/explicit off/EOF/no-catch-up/64-bit clock");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';c.write_text(unit)
    for board in (0,1):
        binary=Path(d)/f'test-{board}'
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',f'-DKIIKO_BOARD_METALIO47={board}','-I'+str(ROOT/'main'),str(c),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

"""Actual settings geometry/navigation and opt-in reading-font synchronization.

SPDX-License-Identifier: Apache-2.0
中文：所有系统字号检查布局/触摸边界，确认弹窗不穿透、不提前修改阅读字体。
English: Check every scale, layout/touch bounds, and modal isolation without eagerly changing the reading face.
"""
from pathlib import Path
import re, subprocess, tempfile

root=Path(__file__).resolve().parents[1]
s=(root/'main/apps/app_device_settings.c').read_text()
tokens=r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
scan=re.sub(tokens,lambda m:' '*len(m[0]) if m[0].startswith(('//','/*')) else m[0],s)
def block(start):
    at=scan.index('{',start)+1;depth=1;quote=None;escape=False
    while depth:
        c=scan[at]
        if quote:
            if escape:escape=False
            elif c=='\\':escape=True
            elif c==quote:quote=None
        elif c in '\"\'':quote=c
        elif c=='{':depth+=1
        elif c=='}':depth-=1
        at+=1
    return s[start:at]
def fn(name):
    m=re.search(r'^static [^\n]+\b'+name+r'\([^;{}]*\)\s*\{',s,re.M)
    assert m,name
    return block(m.start())
start=s.index('static app_redraw_t on_gesture(')
modal=block(s.index('    if (s_page==SETTINGS_SYSTEM_FONT && s_font_sync_confirm)',start))
choose=block(s.index('    if (s_page == SETTINGS_SYSTEM_FONT) {',start))
grace=block(s.index('    if(s_page==SETTINGS_LOCK_PIN &&',start))
main=s[s.index('    if (y<160 || y>=UI_NAV_TOP || ev->x0<36',start):s.index('\nstatic app_redraw_t on_key(',start)].rstrip()
assert main.endswith('}')
main=main[:-1]
enum=s[s.index('typedef enum { SETTINGS_MAIN'):s.index('} settings_page_t;')+len('} settings_page_t;')]
macros='\n'.join(re.findall(r'^#define SETTINGS_(?:ROW_H|WIRELESS_ROWS|DISPLAY_ROWS|PERSONAL_ROWS|MAINTENANCE_ROWS|WIRELESS_Y|DISPLAY_Y|PERSONAL_Y|MAINTENANCE_Y)[^\n]*',s,re.M))
layout=s[s.index('typedef struct {\n    EpdRect size, preview;'):s.index('static system_font_layout_t system_font_layout(')]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_AREA}app_redraw_t;
typedef struct{int type,x0,y0;}ui_gesture_event_t;
enum{UI_GESTURE_TAP,UI_GESTURE_SWIPE_U};
typedef struct{int unused;}app_desc_t;
typedef struct{const app_desc_t*request_app;}app_ctx_t;
const app_desc_t app_transfer={0};
typedef struct{int unused;}ble_pt_raw_t;
typedef struct{const char *path;}ttf_font_item_t;
static const ttf_font_item_t fonts[]={{"/sdcard/fonts/A.ttf"},{"/sdcard/fonts/B.ttf"},{"/sdcard/fonts/C.ttf"}};
static int ttf_font_count(void){return 3;}
static const ttf_font_item_t*ttf_font_item(int i){return i>=0&&i<3?&fonts[i]:NULL;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static uint8_t scale=100;
static bool write_fail;
static uint8_t app_settings_system_font_size(void){return scale;}
static void app_settings_set_system_font_size(uint8_t n){if(!write_fail)scale=n;}
static char system_path[192]="/sdcard/fonts/Old.ttf",reader_path[192]="/sdcard/fonts/Reader.ttf";
static unsigned reader_writes,system_activations;
static const char*app_settings_system_font_path(void){return system_path;}
static const char*app_settings_font_path(void){return reader_path;}
static void app_settings_set_system_font_path(const char*p){if(!write_fail)snprintf(system_path,sizeof(system_path),"%s",p);}
static void app_settings_set_font_path(const char*p){++reader_writes;if(!write_fail)snprintf(reader_path,sizeof(reader_path),"%s",p);}
static void app_font_activate_system(void){++system_activations;}
static void ui_text_set_system_scale(bool b){assert(b);}
static int s_font_page,s_main_scroll,s_style_scroll,s_ble_scroll,s_ble_learning;
static bool s_font_sync_confirm,s_config_confirm,s_boot_pending,s_scroll_present_pending,s_reset_confirm,s_reset_pending;
static char s_sd_notice[96],s_reset_notice[96];
static char s_font_sync_path[192],s_notice[96],s_boot_notice[96],s_ble_notice[96],s_ble_feedback[96];
static int font_scans,profile_edits,wifi_requests;
static bool home_full;
static bool pin_enabled=true,pin_available=true;
static unsigned grace_code;
static bool lock_pin_enabled(void){return pin_enabled;}
static bool lock_pin_available(void){return pin_available;}
static unsigned app_settings_pin_grace_code(void){return grace_code;}
static void app_settings_set_pin_grace_code(unsigned n){grace_code=n;}
static uint8_t wake_presses=1;
static uint8_t app_settings_wake_presses(void){return wake_presses;}
static void app_settings_set_wake_presses(uint8_t n){wake_presses=n;}
static void ttf_font_scan(void){++font_scans;}
static void app_transfer_request_wifi_setup(void){++wifi_requests;}
static bool ble_pt_pop_raw(ble_pt_raw_t*p){(void)p;return false;}
static bool app_settings_home_full_refresh(void){return home_full;}
static void app_settings_set_home_full_refresh(bool v){home_full=v;}
static void profile_editor_open(bool signature){assert(signature);++profile_edits;}
static bool fast_charge=true;
static unsigned fast_charge_writes,charger_applies;
static bool app_settings_fast_charge(void){return fast_charge;}
static void app_settings_set_fast_charge(bool v){fast_charge=v;++fast_charge_writes;}
static int metalio_charger_set_fast_charge(bool v){(void)v;++charger_applies;return 0;}
#define UI_NAV_TOP 1096
#define UI_NAV_REFRESH_END 1088
#define UI_LOCK_HEIGHT 1216
'''+enum+'\nstatic settings_page_t s_page;\n'+macros+'''
#define SETTINGS_SCROLL_MAX (SETTINGS_MAINTENANCE_Y+SETTINGS_MAINTENANCE_ROWS*SETTINGS_ROW_H-UI_NAV_TOP+12)
static bool s_panel_repair_pending;
static void upgrade_open(void){s_page=SETTINGS_UPGRADE;}
'''+fn('settings_viewport_end')+'\n'+fn('settings_stacked')+'\n'+fn('settings_profile_height')+'\n'+fn('settings_main_row_height')+'\n'+layout+fn('system_font_layout')+'\n'+fn('system_font_item_rect')+'\n'+fn('system_font_sync_button')+'\n'+fn('system_font_page_button')+'\n'
unit+=fn('pin_grace_row')+'\n'
unit+='static app_redraw_t grace_tap(int x,int y){ui_gesture_event_t e={UI_GESTURE_TAP,x,y};const ui_gesture_event_t*ev=&e;'+grace+'return APP_REDRAW_NONE;}\n'
unit+='static app_redraw_t font_tap(int x,int y){ui_gesture_event_t e={UI_GESTURE_TAP,x,y};const ui_gesture_event_t*ev=&e;'+modal+choose+'return APP_REDRAW_NONE;}\n'
unit+='static app_redraw_t modal_swipe(void){ui_gesture_event_t e={UI_GESTURE_SWIPE_U,400,700};const ui_gesture_event_t*ev=&e;'+modal+'return APP_REDRAW_NONE;}\n'
unit+='static app_redraw_t main_tap(app_ctx_t*ctx,int x,int y){ui_gesture_event_t e={UI_GESTURE_TAP,x,y};const ui_gesture_event_t*ev=&e;'+main+'}\n'
unit+=r'''
int main(void){
 app_ctx_t ctx={0};
 s_page=SETTINGS_LOCK_PIN;EpdRect gr=pin_grace_row();
 for(unsigned i=0;i<5;i++){assert(grace_tap(gr.x+1,gr.y+1)==APP_REDRAW_PAGE&&grace_code==(i+1)%5);}
 assert(grace_tap(gr.x-1,gr.y+1)==APP_REDRAW_NONE&&grace_tap(gr.x,gr.y+gr.height)==APP_REDRAW_NONE);
 pin_enabled=false;assert(grace_tap(300,650)==APP_REDRAW_NONE);pin_enabled=true;
 pin_available=false;assert(grace_tap(300,650)==APP_REDRAW_NONE);pin_available=true;

 for(int percent=100;percent<=200;percent+=10){
   scale=percent;s_font_sync_confirm=false;assert(settings_stacked()==(percent>=150));
   system_font_layout_t l=system_font_layout();
   assert(l.size.y+l.size.height<l.preview_title&&l.preview.y+l.preview.height<l.font_title);
   assert(l.font_title<l.first&&l.first+l.slots*l.row_height<UI_NAV_TOP);
   for(int i=0;i<l.slots;++i){EpdRect r=system_font_item_rect(i);assert(r.y+r.height<UI_NAV_TOP);if(i)assert(system_font_item_rect(i-1).y+system_font_item_rect(i-1).height<r.y);}
   assert(SETTINGS_WIRELESS_Y>=180+settings_profile_height()+70);
   assert(SETTINGS_DISPLAY_Y>SETTINGS_WIRELESS_Y+3*SETTINGS_ROW_H);
   assert(SETTINGS_PERSONAL_Y>SETTINGS_DISPLAY_Y+6*SETTINGS_ROW_H);
   const int tops[]={SETTINGS_WIRELESS_Y,SETTINGS_DISPLAY_Y,SETTINGS_PERSONAL_Y,SETTINGS_MAINTENANCE_Y};
   const int counts[]={3,8,5,6};
   const int destinations[][8]={{-1,SETTINGS_BLUETOOTH,SETTINGS_TIME},
     {SETTINGS_SYSTEM_FONT,SETTINGS_SYSTEM_CONTRAST,SETTINGS_MAIN_REFRESH,-2,SETTINGS_AUTO_LOCK,-2,SETTINGS_POWER_SLEEP,SETTINGS_FRONTLIGHT},
     {SETTINGS_SHELF_STYLE,SETTINGS_LOCK_STYLE,SETTINGS_LOCK_PIN,-3,-5},
     {SETTINGS_UPGRADE,SETTINGS_SD_RECOVERY,SETTINGS_CONFIG,-4,SETTINGS_BOOT,SETTINGS_FACTORY_RESET}};
   for(int group=0;group<4;++group)for(int i=0;i<counts[group];++i){
     s_page=SETTINGS_MAIN;int center=tops[group]+i*SETTINGS_ROW_H+SETTINGS_ROW_H/2;s_main_scroll=center-500;ctx.request_app=NULL;
     bool fast_before=fast_charge;unsigned fast_writes_before=fast_charge_writes;
     main_tap(&ctx,300,500);
     if(destinations[group][i]>=0)assert((int)s_page==destinations[group][i]);
     else if(destinations[group][i]==-1)assert(ctx.request_app==&app_transfer);
     else if(destinations[group][i]==-2)assert(s_page==SETTINGS_MAIN);
     else if(destinations[group][i]==-4)assert(s_panel_repair_pending&&s_page==SETTINGS_MAIN);
     else if(destinations[group][i]==-5)assert(s_page==SETTINGS_MAIN&&fast_charge!=fast_before&&fast_charge_writes==fast_writes_before+1&&charger_applies==fast_charge_writes);
     else assert(profile_edits>0);
     s_page=SETTINGS_MAIN;assert(main_tap(&ctx,10,500)==APP_REDRAW_NONE&&s_page==SETTINGS_MAIN);
   }
   s_main_scroll=SETTINGS_SCROLL_MAX;s_page=SETTINGS_MAIN;
   int last=SETTINGS_MAINTENANCE_Y+SETTINGS_MAINTENANCE_ROWS*SETTINGS_ROW_H-1-s_main_scroll;
   assert(last<UI_NAV_TOP&&main_tap(&ctx,300,last)==APP_REDRAW_PAGE&&s_page==SETTINGS_FACTORY_RESET);
 }
 scale=100;s_page=SETTINGS_SYSTEM_FONT;s_font_page=0;s_main_scroll=0;s_font_sync_confirm=false;
 for(int i=0;i<11;++i){EpdRect r=system_font_layout().size;assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE);assert(scale==((i+1)%11)*10+100);}
 EpdRect r=system_font_item_rect(1);
 assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE&&s_font_sync_confirm);
 assert(!strcmp(system_path,fonts[0].path)&&!strcmp(reader_path,"/sdcard/fonts/Reader.ttf")&&!reader_writes&&system_activations==1);
 assert(modal_swipe()==APP_REDRAW_NONE&&s_font_sync_confirm);
 assert(font_tap(300,220)==APP_REDRAW_NONE&&s_font_sync_confirm&&scale==100);
 assert(font_tap(202,720)==APP_REDRAW_PAGE&&!s_font_sync_confirm&&!reader_writes);
 assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE&&s_font_sync_confirm);
 write_fail=true;assert(font_tap(481,720)==APP_REDRAW_PAGE&&s_font_sync_confirm&&s_notice[0]&&reader_writes==1);
 write_fail=false;assert(font_tap(481,720)==APP_REDRAW_PAGE&&!s_font_sync_confirm&&reader_writes==2&&!strcmp(reader_path,system_path));
 assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE&&!s_font_sync_confirm);
 write_fail=true;unsigned before=system_activations;r=system_font_item_rect(2);
 assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE&&!s_font_sync_confirm&&system_activations==before&&!strcmp(system_path,fonts[0].path));
 write_fail=false;assert(font_tap(300,r.y+r.height+7)==APP_REDRAW_NONE);
 puts("PASS: 11 scales, all current S31 destinations, reachable final row, font-size cycle, modal isolation, opt-in sync, no reading-font load and failed-save retry");
}
'''
with tempfile.TemporaryDirectory() as folder:
    c,b=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(b)],check=True)
    subprocess.run([str(b)],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：验证实际三栏设置入口、题头选择、竖排与计时控件。
English: Test the actual settings tabs, chapter heading choices, display options and timer controls.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1];ui=R/'main/apps/book_reader_controls_ui.inc';runtime=R/'main/apps/book_reader_controls_runtime.inc'
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "reader_controls.h"
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int now_ms;}app_ctx_t;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_PAGE}app_redraw_t;
typedef enum{READER_PANEL_NONE,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_FONT_SETTINGS,READER_PANEL_LAYOUT_SETTINGS,READER_PANEL_READING_DISPLAY,READER_PANEL_HEADING_SETTINGS,READER_PANEL_RULE_SETTINGS,READER_PANEL_TOUCH_SETTINGS,READER_PANEL_TURN_SETTINGS,READER_PANEL_KEY_SETTINGS}reader_panel_t;
static int s_reader_panel=READER_PANEL_REFRESH_SETTINGS,s_rc_menu_slot=-1,s_turns,effect,full_pages=10,last_action,options[4],notices;
static unsigned s_rc_tab;static bool s_rc_font_root,default_full,save_fail;
static reader_controls_config_t config;
const reader_controls_config_t*app_settings_reader_controls(void){return &config;}
bool app_settings_set_reader_auto_seconds(unsigned n){if(save_fail)return false;assert(n>=1&&n<=10);config.seconds_le[0]=n;config.seconds_le[1]=n>>8;return true;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void rc_pause_auto(void){}
static void rc_invalidate(void){}
static int invalidations;
static void invalidate_prep(void){++invalidations;}
static void rc_notice(app_ctx_t*c,const char*t){(void)c;(void)t;++notices;}
static app_redraw_t rc_open(app_ctx_t*c,reader_panel_t p){(void)c;s_reader_panel=p;return APP_REDRAW_PAGE;}
static app_redraw_t rc_to_root(app_ctx_t*c,unsigned tab){s_rc_tab=tab;return rc_open(c,READER_PANEL_REFRESH_SETTINGS);}
static app_redraw_t rc_execute(app_ctx_t*c,unsigned a){(void)c;last_action=a;return APP_REDRAW_PAGE;}
static unsigned app_settings_reader_turn_effect(void){return effect;}
static void app_settings_set_reader_turn_effect(unsigned a){effect=a;}
static unsigned app_settings_reader_full_pages(void){return full_pages;}
static void app_settings_set_reader_full_pages(unsigned a){full_pages=a;}
static bool app_settings_reader_default_fullscreen(void){return default_full;}
static bool app_settings_set_reader_default_fullscreen(bool on){if(save_fail)return false;default_full=on;return true;}
typedef int nvs_handle_t;
typedef int esp_err_t;
enum {ESP_OK,NVS_READWRITE};
#define NVS_NS "test"
static int legacy_style=-1,saved_heading=-1,saved_size=-1,pending_value;
static int *pending_key;
static esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){assert(!strcmp(ns,NVS_NS)&&mode==NVS_READWRITE);*h=1;return ESP_OK;}
static int *heading_key(const char *key) {
 if (!strcmp(key,"bk_heading")) return &legacy_style;
 if (!strcmp(key,"bk_head_v2")) return &saved_heading;
 assert(!strcmp(key,"bk_head_size"));
 return &saved_size;
}
static esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *value){assert(h==1);int saved=*heading_key(key);if(saved<0)return -1;*value=saved;return ESP_OK;}
static esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){assert(h==1);pending_key=heading_key(key);pending_value=value;return ESP_OK;}
static esp_err_t nvs_commit(nvs_handle_t h){assert(h==1);if(save_fail)return -1;*pending_key=pending_value;return ESP_OK;}
static void nvs_close(nvs_handle_t h){assert(h==1);}
static app_redraw_t apply_reader_option(app_ctx_t*c,int kind){(void)c;assert(kind>=1&&kind<=3);++options[kind];return APP_REDRAW_PAGE;}
'''
header=(R/'main/settings.h').read_text()
start=header.rindex('typedef enum {',0,header.index('    APP_BOOK_HEADING_RULE,'))
end=header.index('/// 阅读线相对默认位置',start)
u+=header[start:end]
settings=(R/'main/settings_reader_controls.inc').read_text()
u+=settings[settings.index('#define NVS_KEY_BOOK_HEADING'):settings.index('static void reader_controls_normalize')]
u+=r"""
static app_redraw_t rc_change_heading_size(app_ctx_t *ctx,unsigned size){
 if(!app_settings_set_book_heading_size(size))rc_notice(ctx,"save");else invalidate_prep();
 return APP_REDRAW_PAGE;
}
"""
u+=next(line for line in ui.read_text().splitlines() if line.startswith('enum { RC_HEADING_TOP'))
for n in ['rc_root_top','rc_done_rect','rc_tab_rect','rc_grid_rect','rc_heading_link_rect','rc_heading_align_rect','rc_heading_size_rect','rc_heading_style_rect','rc_heading_back_hit','rc_display_rect','rc_effect_rect','rc_refresh_rect','rc_auto_card','rc_interval_rect']:u+=function(n,ui)+'\n'
action=function('rc_panel_action',runtime)
a=action.index('    if(s_reader_panel==READER_PANEL_READING_DISPLAY){');b=action.index('    if(s_reader_panel==READER_PANEL_FONT_SETTINGS',a)
display=action[a:b]
a=action.index('    if(s_reader_panel!=READER_PANEL_REFRESH_SETTINGS)');root=action[a:]
u+='static app_redraw_t panel(app_ctx_t*ctx,int x,int y,bool*handled){*handled=false;\n'+display+root+'\n'
u+=r'''
static app_redraw_t tap(app_ctx_t*c,EpdRect r){bool handled=false;app_redraw_t out=panel(c,r.x+r.width/2,r.y+r.height/2,&handled);assert(handled);return out;}
int main(void){
 config=reader_controls_defaults(false);app_ctx_t c={0};
 const int expected[]={READER_PANEL_FONT_SETTINGS,READER_PANEL_LAYOUT_SETTINGS,READER_PANEL_READING_DISPLAY,READER_PANEL_RULE_SETTINGS};
 for(int i=0;i<4;++i){s_rc_tab=0;s_reader_panel=READER_PANEL_REFRESH_SETTINGS;EpdRect a=rc_grid_rect(i);assert(a.y>=rc_root_top()+150&&a.y+a.height<rc_done_rect().y);assert(tap(&c,a)==APP_REDRAW_PAGE&&s_reader_panel==expected[i]);
  for(int j=0;j<i;++j){EpdRect b=rc_grid_rect(j);assert(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y);}}
 s_reader_panel=READER_PANEL_REFRESH_SETTINGS;bool h;
 assert(panel(&c,342,900,&h)==APP_REDRAW_NONE&&h&&s_reader_panel==READER_PANEL_REFRESH_SETTINGS);
 EpdRect heading=rc_heading_link_rect(),last_card=rc_grid_rect(3);
 assert(heading.y>last_card.y+last_card.height&&heading.y+heading.height<rc_done_rect().y);
 assert(tap(&c,heading)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_HEADING_SETTINGS);
 assert(rc_heading_back_hit(64,RC_HEADING_TOP+55)&&rc_heading_back_hit(330,RC_HEADING_TOP-1));
 assert(!rc_heading_back_hit(330,rc_heading_style_rect(0).y+1));
 for(int i=0;i<APP_BOOK_HEADING_ALIGN_COUNT;++i){
  EpdRect button=rc_heading_align_rect(i);
  assert(button.y>RC_HEADING_TOP+92&&button.y+button.height<rc_heading_size_rect(0).y);
  assert(i==0||rc_heading_align_rect(i-1).x+rc_heading_align_rect(i-1).width<button.x);
 }
 for (int i=0;i<2;++i) {
  EpdRect button=rc_heading_size_rect(i);
  assert(button.y+button.height<rc_heading_style_rect(0).y);
  assert(button.height>=44&&button.width>=44);
 }
 for(int i=0;i<APP_BOOK_HEADING_COUNT;++i){
  EpdRect sample=rc_heading_style_rect(i);
  assert(sample.height>=114&&sample.y+sample.height<rc_done_rect().y);
  for(int j=0;j<i;++j){EpdRect prior=rc_heading_style_rect(j);
   assert(prior.x+prior.width<sample.x||prior.y+prior.height<sample.y);}
 }
 book_heading_style_load(1);
 assert(app_settings_book_heading_style()==APP_BOOK_HEADING_RULE);
 assert(app_settings_book_heading_align()==APP_BOOK_HEADING_ALIGN_LEFT);
 legacy_style=1;book_heading_style_load(1);
 assert(app_settings_book_heading_style()==APP_BOOK_HEADING_FLYLEAF);
 assert(app_settings_book_heading_align()==APP_BOOK_HEADING_ALIGN_LEFT);
 legacy_style=2;book_heading_style_load(1);
 assert(app_settings_book_heading_style()==APP_BOOK_HEADING_SIDE);
 assert(app_settings_book_heading_align()==APP_BOOK_HEADING_ALIGN_LEFT);
 assert(!app_settings_set_book_heading_style(APP_BOOK_HEADING_COUNT));
 assert(!app_settings_set_book_heading_align(APP_BOOK_HEADING_ALIGN_COUNT));
 assert(tap(&c,rc_heading_style_rect(APP_BOOK_HEADING_SIDE))==APP_REDRAW_NONE&&invalidations==0);
 assert(tap(&c,rc_heading_align_rect(APP_BOOK_HEADING_ALIGN_LEFT))==APP_REDRAW_NONE&&invalidations==0);
 for(int i=0;i<APP_BOOK_HEADING_COUNT;++i){
  assert(tap(&c,rc_heading_style_rect(i))==APP_REDRAW_PAGE&&(int)app_settings_book_heading_style()==i);
  assert(invalidations==i+1&&app_settings_book_heading_align()==APP_BOOK_HEADING_ALIGN_LEFT);
  s_book_heading_style=APP_BOOK_HEADING_FLYLEAF;book_heading_style_load(1);assert((int)app_settings_book_heading_style()==i);
 }
 assert(tap(&c,rc_heading_align_rect(APP_BOOK_HEADING_ALIGN_RIGHT))==APP_REDRAW_PAGE);
 s_book_heading_align=APP_BOOK_HEADING_ALIGN_CENTER;book_heading_style_load(1);
 assert(app_settings_book_heading_style()==APP_BOOK_HEADING_SIDE&&app_settings_book_heading_align()==APP_BOOK_HEADING_ALIGN_RIGHT);
 assert(invalidations==APP_BOOK_HEADING_COUNT+1);
 save_fail=true;
 assert(tap(&c,rc_heading_style_rect(0))==APP_REDRAW_PAGE&&app_settings_book_heading_style()==APP_BOOK_HEADING_SIDE);
 assert(tap(&c,rc_heading_align_rect(0))==APP_REDRAW_PAGE&&app_settings_book_heading_align()==APP_BOOK_HEADING_ALIGN_RIGHT);
 assert(notices==2&&invalidations==APP_BOOK_HEADING_COUNT+1);
 save_fail=false;notices=0;
 assert(APP_BOOK_HEADING_SIZE_DEFAULT==150&&app_settings_book_heading_size()==150);
 assert(!app_settings_set_book_heading_size(105));
 assert(!app_settings_set_book_heading_size(APP_BOOK_HEADING_SIZE_MIN-1));
 assert(!app_settings_set_book_heading_size(APP_BOOK_HEADING_SIZE_MAX+1));
 s_book_heading_size=APP_BOOK_HEADING_SIZE_MAX;
 for (unsigned size=190;size>=80;size-=10) {
  assert(tap(&c,rc_heading_size_rect(0))==APP_REDRAW_PAGE);
  assert(app_settings_book_heading_size()==size&&saved_size==(int)size);
 }
 assert(tap(&c,rc_heading_size_rect(0))==APP_REDRAW_NONE);
 for (unsigned size=90;size<=200;size+=10) {
  assert(tap(&c,rc_heading_size_rect(1))==APP_REDRAW_PAGE);
  assert(app_settings_book_heading_size()==size&&saved_size==(int)size);
 }
 assert(tap(&c,rc_heading_size_rect(1))==APP_REDRAW_NONE);
 assert(invalidations==APP_BOOK_HEADING_COUNT+25);
 save_fail=true;
 assert(tap(&c,rc_heading_size_rect(0))==APP_REDRAW_PAGE);
 assert(app_settings_book_heading_size()==200&&saved_size==200&&notices==1);
 assert(invalidations==APP_BOOK_HEADING_COUNT+25);
 save_fail=false;notices=0;
 s_book_heading_size=100;book_heading_style_load(1);
 assert(app_settings_book_heading_size()==200);
 assert(app_settings_book_heading_style()==APP_BOOK_HEADING_SIDE);
 assert(app_settings_book_heading_align()==APP_BOOK_HEADING_ALIGN_RIGHT);
 saved_size=120;s_book_heading_size=200;book_heading_style_load(1);
 assert(app_settings_book_heading_size()==120);
 saved_size=255;s_book_heading_size=APP_BOOK_HEADING_SIZE_DEFAULT;book_heading_style_load(1);
 assert(app_settings_book_heading_size()==150);
 assert(tap(&c,rc_done_rect())==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_REFRESH_SETTINGS&&s_rc_tab==0);
 s_rc_tab=2;
 const int gesture_top=rc_root_top(), gesture_tabs_y=rc_tab_rect(2).y;
 assert(gesture_top==576);
 for(int tab=0;tab<3;++tab){s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
  assert(tap(&c,rc_tab_rect(tab))==APP_REDRAW_PAGE&&s_rc_tab==(unsigned)tab);
  assert(rc_root_top()==gesture_top&&rc_tab_rect(tab).y==gesture_tabs_y);}
 EpdRect effect=rc_effect_rect(0),refresh=rc_refresh_rect(),auto_card=rc_auto_card(0);
 assert(effect.y>rc_tab_rect(1).y+rc_tab_rect(1).height);
 assert(effect.y+effect.height<refresh.y&&refresh.y+refresh.height<auto_card.y);
 assert(auto_card.y+auto_card.height<rc_done_rect().y);
 EpdRect interval=rc_auto_card(1);
 for(unsigned plus=0;plus<2;++plus){EpdRect step=rc_interval_rect(plus);
  assert(step.x>=interval.x&&step.x+step.width<=interval.x+interval.width);
  assert(step.y>=interval.y&&step.y+step.height<=interval.y+interval.height);}
 s_rc_tab=1;s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
 for(int i=0;i<3;++i){s_turns=99;assert(tap(&c,rc_effect_rect(i))==APP_REDRAW_PAGE&&app_settings_reader_turn_effect()==(unsigned)i&&!s_turns);}
 assert(tap(&c,rc_auto_card(0))==APP_REDRAW_PAGE&&last_action==RC_AUTOTURN);
 for(unsigned n=10;n>1;--n){assert(tap(&c,rc_interval_rect(false))==APP_REDRAW_PAGE&&reader_controls_seconds(&config)==n-1);}
 assert(tap(&c,rc_interval_rect(false))==APP_REDRAW_NONE&&reader_controls_seconds(&config)==1);
 for(unsigned n=1;n<10;++n){assert(tap(&c,rc_interval_rect(true))==APP_REDRAW_PAGE&&reader_controls_seconds(&config)==n+1);}
 assert(tap(&c,rc_interval_rect(true))==APP_REDRAW_NONE&&reader_controls_seconds(&config)==10);
 save_fail=true;assert(tap(&c,rc_interval_rect(false))==APP_REDRAW_PAGE&&reader_controls_seconds(&config)==10&&notices==1);save_fail=false;notices=0;
 s_rc_tab=2;
 const int links[]={READER_PANEL_TOUCH_SETTINGS,READER_PANEL_TURN_SETTINGS,READER_PANEL_KEY_SETTINGS};
 for(int i=0;i<3;++i){s_reader_panel=READER_PANEL_REFRESH_SETTINGS;assert(panel(&c,200,800+i*132,&h)==APP_REDRAW_PAGE&&s_reader_panel==links[i]);}
 s_reader_panel=READER_PANEL_READING_DISPLAY;
 assert(tap(&c,rc_display_rect(0))==APP_REDRAW_PAGE&&default_full);save_fail=true;
 assert(tap(&c,rc_display_rect(0))==APP_REDRAW_PAGE&&default_full&&notices==1);save_fail=false;
 for(int i=1;i<4;++i)assert(tap(&c,rc_display_rect(i))==APP_REDRAW_PAGE&&options[i]==1);
 assert(tap(&c,rc_done_rect())==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_REFRESH_SETTINGS&&s_rc_tab==0);
 assert(tap(&c,rc_done_rect())==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_NONE);
 puts("PASS: settings geometry, four heading styles, heading alignment and font size persistence, time controls and display");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(u)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(R/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

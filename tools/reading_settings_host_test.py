#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：验证已确认的阅读设置共用矩形、点按周期、二级页与间隙不穿透。
# English: Validate approved reading-sheet geometry, cycles, secondary destinations and inert gaps.
from pathlib import Path
import json
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int unused;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
enum {READER_PANEL_NONE,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_TURN_SETTINGS,READER_PANEL_KEY_SETTINGS};
static int s_reader_panel, s_turns, invalidates, toggles[4], full_pages = 10, effect;
static bool ui_rect_hit(EpdRect r,unsigned x,unsigned y){return x>=(unsigned)r.x&&x<(unsigned)(r.x+r.width)&&y>=(unsigned)r.y&&y<(unsigned)(r.y+r.height);}
static unsigned app_settings_reader_full_pages(void){return full_pages;}
static void app_settings_set_reader_full_pages(unsigned n){full_pages=n;}
static unsigned app_settings_reader_turn_effect(void){return effect;}
static void app_settings_set_reader_turn_effect(unsigned n){effect=n;}
static app_redraw_t apply_reader_option(app_ctx_t *ctx,int i){(void)ctx;assert(i>=0&&i<4);++toggles[i];return APP_REDRAW_PAGE;}
static bool default_fullscreen,save_fails;
static bool app_settings_reader_default_fullscreen(void){return default_fullscreen;}
static bool app_settings_set_reader_default_fullscreen(bool on){if(save_fails)return false;default_fullscreen=on;return true;}
static void invalidate_prep(void){++invalidates;}
'''
source = ROOT/'main/apps/app_book.c'
unit+='static unsigned app_settings_system_font_size(void){return 100;}\n'
for name in ('reader_ui_extra', 'reading_settings_top', 'reading_setting_rect', 'reading_toggle_rect', 'reading_settings_action'):
    unit += function(name, source)+'\n'
unit += r'''
int main(void){
    app_ctx_t ctx={0};
    // 9个控件不重叠；默认全屏与沉浸并排，按键控制占末行。
    // Nine controls stay disjoint; fullscreen entry sits beside immersion and keys span the final row.
    for(int i=0;i<9;++i){
        EpdRect a=reading_setting_rect(i);assert(a.x>=36&&a.x+a.width<=648&&a.y>=590&&a.y+a.height<=1122);
        for(int j=i+1;j<9;++j){EpdRect b=reading_setting_rect(j);assert(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y);}
    }
    EpdRect mode=reading_setting_rect(2),keys=reading_setting_rect(8);
    assert(keys.x==mode.x&&keys.y>mode.y&&keys.y==1006&&keys.width==612);
#if KIIKO_BOARD_METALIO47
    const unsigned expected[]={15,20,30,0,5,10};
#else
    const unsigned expected[]={15,30,0,5,10};
#endif
    for(unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);++i){s_turns=100;assert(reading_settings_action(&ctx,120,639)==APP_REDRAW_PAGE);assert(full_pages==(int)expected[i]&&!s_turns);}
    full_pages=77;reading_settings_action(&ctx,120,639);assert(full_pages==(KIIKO_BOARD_METALIO47?0:5));
    for(unsigned i=0;i<6;++i){s_turns=4;reading_settings_action(&ctx,450,639);assert(effect==(int)((i+1)%3)&&s_turns==0);}
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,120,743);assert(s_reader_panel==READER_PANEL_TURN_SETTINGS);
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,120,1055);assert(s_reader_panel==READER_PANEL_KEY_SETTINGS);
    for(int i=0;i<4;++i){EpdRect a=reading_toggle_rect(i);assert(reading_settings_action(&ctx,a.x+a.width-5,a.y+a.height-5)==APP_REDRAW_PAGE&&toggles[i]==1);}
    EpdRect df=reading_toggle_rect(4);
    assert(reading_settings_action(&ctx,df.x+30,df.y+30)==APP_REDRAW_PAGE&&default_fullscreen);
    save_fails=true;assert(reading_settings_action(&ctx,df.x+30,df.y+30)==APP_REDRAW_NONE&&default_fullscreen);save_fails=false;
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;int before=invalidates;
    assert(reading_settings_action(&ctx,342,640)==APP_REDRAW_NONE);
    assert(reading_settings_action(&ctx,120,691)==APP_REDRAW_NONE);
    assert(reading_settings_action(&ctx,120,1130)==APP_REDRAW_NONE);
    assert(s_reader_panel==READER_PANEL_REFRESH_SETTINGS&&invalidates==before);
    reading_settings_action(&ctx,342,1170);assert(s_reader_panel==READER_PANEL_NONE);
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,342,479);assert(s_reader_panel==READER_PANEL_NONE);
    puts("PASS: approved half-sheet bounds, nonoverlapping shared hit areas, key controls below turn mode, full-refresh/effect cycles, toggles, secondary pages, Done and inert gaps");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    c,exe=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    view=ROOT/".kiiko-source-view.json"
    boards=(int(json.loads(view.read_text())["board"]=="metalio47"),) if view.exists() else (0,1)
    for board in boards:
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',f'-DKIIKO_BOARD_METALIO47={board}',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

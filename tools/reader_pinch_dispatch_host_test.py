#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：双指派发和单指抬起隔离，不阻塞其它服务。/ English: Pinch dispatch drains releases without blocking other services."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
source=(R/'main/app/app_loop.c').read_text()
start=source.index('        if (!touch.touched) pinch_drain = false;')
end=source.index('        if (!pinch_input_owned && !power_input_owned && !menu_open',start)
block=source[start:end]
assert 'continue;' not in block
unit=r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define ESP_OK 0
typedef struct{bool active;uint16_t ax,ay,bx,by;}app_pinch_t;
typedef int app_redraw_t;
enum{APP_REDRAW_NONE,APP_REDRAW_DONE};
typedef struct{bool consumed,pressed,released;void*request_app;}app_ctx_t;
static app_ctx_t ctx;
static bool ai_owned;
static bool wants=true,power_input_owned,menu_open,quick_input_owned,pinching,pinch_input_owned,pinch_drain;
static struct{bool open;}s_quick;
static struct{bool touched;int count;struct{int x,y;}points[2];}touch;
static int err,held_key=1,gesture,active_count,end_count,reset_count,cancel_count,presents;
static bool wants_pinch(app_ctx_t*c){(void)c;return wants;}
static app_redraw_t on_pinch(app_ctx_t*c,const app_pinch_t*p){(void)c;if(p->active)active_count++;else end_count++;return APP_REDRAW_DONE;}
static struct{bool(*wants_pinch)(app_ctx_t*);app_redraw_t(*on_pinch)(app_ctx_t*,const app_pinch_t*);}desc={wants_pinch,on_pinch},*current=&desc;
static void app_present(app_ctx_t*c,void*d,int r){(void)c;(void)d;(void)r;presents++;}
static void ui_gesture_reset(int*g){(void)g;reset_count++;}
static void cancel_gesture(app_ctx_t*c,void*d,int*g){(void)c;(void)d;(void)g;cancel_count++;}
static void tick(void){quick_input_owned=false;ctx.consumed=false;ctx.pressed=ctx.released=true;
'''+block+r'''
}
static void pair(void){touch.touched=true;touch.count=2;tick();}
int main(void){
 pair();assert(pinching&&active_count==1&&cancel_count==1&&held_key==-1&&quick_input_owned);
 pair();assert(active_count==2&&cancel_count==1);
 touch.count=1;tick();assert(!pinching&&end_count==1&&pinch_drain&&quick_input_owned);
 tick();assert(end_count==1&&quick_input_owned);
 touch.touched=false;touch.count=0;tick();assert(!pinch_drain&&!pinch_input_owned);
 pair();err=-1;touch.count=0;touch.touched=false;tick();assert(!pinching&&end_count==2);
 err=0;pair();wants=false;tick();assert(!pinching&&end_count==2); // 掉卡不再调用旧图。/ No old-image callback after media loss.
 wants=true;touch.touched=false;touch.count=0;tick();
 power_input_owned=true;pair();assert(!pinching);power_input_owned=false;
 ai_owned=true;pair();assert(!pinching);ai_owned=false;
 menu_open=true;pair();assert(!pinching);menu_open=false;
 s_quick.open=true;pair();assert(!pinching);s_quick.open=false;
 ctx.request_app=&desc;pair();assert(!pinching);
 puts("PASS: pinch start/end, release drain, I2C failure, media loss, overlays and navigation ownership; services are not skipped");
}
'''
# AI键只属于S31，Pico视图不测试不存在的输入所有者。/ AI-key ownership is S31-only and absent from the selected Pico loop.
if 'ai_owned' not in block:
    unit=unit.replace('static bool ai_owned;\n','').replace(' ai_owned=true;pair();assert(!pinching);ai_owned=false;\n','')
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

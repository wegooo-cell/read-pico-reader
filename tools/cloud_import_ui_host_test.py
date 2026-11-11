#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：执行真实导入页和云页面的点击、分页及生命周期。
English: Run production import/cloud UI hit paths, pagination and lifecycle.
"""
from pathlib import Path
import re, subprocess, tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
cloud=(R/'main/apps/app_cloud_sync.c').read_text();cloud=re.sub(r'^#include .*\n','',cloud,flags=re.M);cloud=cloud[:cloud.index('const app_desc_t app_cloud_sync =')]
header=(R/'main/book/book_xteink_sync.h').read_text();header=re.sub(r'^#include .*\n|^#pragma once\n','',header,flags=re.M)
u=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef int esp_err_t;
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{bool request_return,request_menu,consumed;const void*request_app;}app_ctx_t;
typedef int app_redraw_t;
enum{APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL,UI_GESTURE_TAP,UI_PAGER_NONE=-1,UI_PAGER_FIRST=0,UI_PAGER_PREVIOUS,UI_PAGER_NEXT,UI_PAGER_LAST};
typedef struct{int type,x0,y0;}ui_gesture_event_t;
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_ALIGN_CENTER 1
#define EPD_DRAW_ALIGN_RIGHT 2
#define UI_GRAY_WHITE 255
#define UI_W 684
#define UI_PAD_X 36
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void ui_fill_round_rect(uint8_t*f,EpdRect r,int a,int b){(void)f;(void)r;(void)a;(void)b;}
static void ui_draw_control_frame(uint8_t*f,EpdRect r,int a,int b){(void)f;(void)r;(void)a;(void)b;}
static void ui_text_box(uint8_t*f,EpdRect r,int a,const char*s,int b,bool c){(void)f;(void)a;(void)b;(void)c;assert(s&&r.width>0&&r.height>0&&r.x>=36&&r.x+r.width<=648);}
static void epd_draw_rect(EpdRect r,int a,uint8_t*f){(void)r;(void)a;(void)f;}
static void epd_fill_rect(EpdRect r,int a,uint8_t*f){(void)r;(void)a;(void)f;}
static int image_labels,sync_buttons;static bool saw_all,saw_selected;
static void ui_text(uint8_t*f,int x,int y,int px,const char*s,int a,bool b){(void)f;(void)x;(void)y;(void)px;(void)s;(void)a;(void)b;if(strstr(s,"Pictures"))++image_labels;}
static void ui_text_vc(uint8_t*f,int x,int y,int px,const char*s,int a,bool b){ui_text(f,x,y,px,s,a,b);}
static void ui_clear_page(uint8_t*f){(void)f;}
static void ui_nav_status(uint8_t*f){(void)f;}
static void ui_nav_back(uint8_t*f,int x,int y){(void)f;(void)x;(void)y;}
static void ui_draw_button(uint8_t*f,EpdRect r,const char*s,bool v){(void)f;(void)r;(void)v;if(!strcmp(s,"一键同步")){++sync_buttons;saw_all=true;}if(!strcmp(s,"同步所选")){++sync_buttons;saw_selected=true;}}
static void ui_pager_draw_at(uint8_t*f,const char*s,int p,int n,bool b,int c,int top){(void)f;(void)s;(void)b;(void)c;assert(p>=0&&p<n&&top==852);}
static void ui_wifi_qr_draw(uint8_t*f,EpdRect a){(void)f;(void)a;}
static int qr_clears,notifies,starts,joins,closes,unbinds,downloads,last_download;
static void ui_wifi_qr_clear(void){++qr_clears;}
static bool ui_wifi_qr_prepare_text(const char*s){assert(s&&s[0]);return true;}
static void book_store_notify_changed(void){++notifies;}
'''
u+=r'''
typedef struct {
 void(*on_enter)(app_ctx_t*);void(*on_exit)(app_ctx_t*);
 void(*render)(app_ctx_t*,uint8_t*);app_redraw_t(*on_tick)(app_ctx_t*);
 app_redraw_t(*on_gesture)(app_ctx_t*,const ui_gesture_event_t*);app_redraw_t(*on_key)(app_ctx_t*,int);
 bool(*present)(app_ctx_t*,app_redraw_t);bool(*back_swipe_area)(app_ctx_t*,const ui_gesture_event_t*);bool(*edge_back_enabled)(app_ctx_t*);
}app_desc_t;
#define READ_PICO_TRANSFER_MODE_STA 1
#define READ_PICO_TRANSFER_READY 2
static int wifi_setups,wifi_enters,wifi_exits,wifi_renders,wifi_ticks,wifi_keys,wifi_presents;
static bool wifi_ready,wifi_wrong_mode,wifi_return;
typedef struct{bool network_ready;int mode,state;}read_pico_transfer_status_t;
static void read_pico_transfer_get_status(read_pico_transfer_status_t*s){*s=(read_pico_transfer_status_t){wifi_ready,wifi_wrong_mode?0:READ_PICO_TRANSFER_MODE_STA,wifi_ready?READ_PICO_TRANSFER_READY:0};}
static void app_transfer_request_wifi_setup(void){++wifi_setups;}
static void wifi_enter(app_ctx_t*c){(void)c;++wifi_enters;}
static void wifi_exit(app_ctx_t*c){(void)c;++wifi_exits;}
static void wifi_render(app_ctx_t*c,uint8_t*f){(void)c;(void)f;++wifi_renders;}
static app_redraw_t wifi_tick(app_ctx_t*c){++wifi_ticks;if(wifi_return)c->request_return=true;return APP_REDRAW_NONE;}
static app_redraw_t wifi_gesture(app_ctx_t*c,const ui_gesture_event_t*e){(void)e;c->request_return=true;return APP_REDRAW_NONE;}
static app_redraw_t wifi_key(app_ctx_t*c,int k){(void)k;++wifi_keys;c->request_return=true;return APP_REDRAW_NONE;}
static bool wifi_present(app_ctx_t*c,app_redraw_t r){(void)c;(void)r;++wifi_presents;return true;}
static bool wifi_back_area(app_ctx_t*c,const ui_gesture_event_t*e){(void)c;return e->y0<800;}
static bool wifi_edge(app_ctx_t*c){(void)c;return !wifi_wrong_mode;}
const app_desc_t app_transfer={wifi_enter,wifi_exit,wifi_render,wifi_tick,wifi_gesture,wifi_key,wifi_present,wifi_back_area,wifi_edge};
'''
u+=header+r'''
static xteink_sync_status_t mock_status;static unsigned imports;static int count=12;static uint32_t imported_mask,last_selected;
void xteink_sync_get_status(xteink_sync_status_t*s){*s=mock_status;}
bool xteink_sync_busy(void){return mock_status.busy;}
esp_err_t xteink_sync_start(void){++starts;return 0;}
void xteink_sync_close(void){++closes;}
void xteink_sync_cancel_join(void){++joins;}
esp_err_t xteink_sync_unbind(void){++unbinds;return 0;}
unsigned xteink_sync_import_count(void){return imports;}
int xteink_sync_task_count(void){return count;}
bool xteink_sync_task_get(int i,xteink_task_t*t){if(i<0||i>=count)return false;memset(t,0,sizeof(*t));snprintf(t->name,sizeof(t->name),"book-%d.txt",i);t->size=12000;t->imported=(imported_mask&(UINT32_C(1)<<i))!=0;return true;}
esp_err_t xteink_sync_download(int i){++downloads;last_download=i;return 0;}
uint32_t xteink_sync_pending_mask(void){return ((UINT32_C(1)<<count)-1)&~imported_mask;}
esp_err_t xteink_sync_download_selected(uint32_t mask){++downloads;last_selected=mask;return 0;}
'''
for name in ['ui_pager_area_at','ui_pager_rect_at','ui_pager_hit_at','ui_pager_target']:u+=function(name,R/'main/ui/ui_kit.c')+'\n'
u+=cloud+r'''
int main(void){
 app_ctx_t ctx={0};uint8_t fb[1]={0};xteink_task_t wallpaper={.is_image=true};draw_row(fb,0,&wallpaper,false);wallpaper.imported=true;draw_row(fb,0,&wallpaper,false);assert(image_labels==2);
 cloud_enter(&ctx);assert(starts==0&&wifi_enters==1&&wifi_setups==1&&s_wifi_stage&&s_wifi_live);render(&ctx,fb);assert(wifi_renders==1&&cloud_navigation_visible(&ctx));
 for(int i=0;i<10;++i)cloud_tick(&ctx);assert(starts==0&&!wifi_exits);assert(cloud_present(&ctx,APP_REDRAW_PAGE)&&wifi_presents==1);
 ui_gesture_event_t sw={.y0=900};assert(!cloud_back_swipe_area(&ctx,&sw));sw.y0=100;assert(cloud_back_swipe_area(&ctx,&sw));
 wifi_ready=true;wifi_wrong_mode=true;cloud_tick(&ctx);assert(starts==0&&!cloud_edge_back(&ctx));wifi_wrong_mode=false;
 ctx.consumed=true;cloud_tick(&ctx);assert(starts==0);ctx.consumed=false;
 wifi_return=true;cloud_tick(&ctx);assert(starts==0&&ctx.request_return);ctx.request_return=false;wifi_return=false;
 assert(cloud_tick(&ctx)==APP_REDRAW_PAGE&&starts==1&&wifi_exits==1&&!s_wifi_stage&&!s_wifi_live&&wifi_ready);assert(!cloud_present(&ctx,APP_REDRAW_PAGE));
 assert(s_page==0&&!s_selected&&!cloud_navigation_visible(&ctx));cloud_tick(&ctx);cloud_tick(&ctx);assert(starts==1);
 mock_status.state=XTEINK_SYNC_FAILED;mock_status.revision++;for(int i=0;i<20;i++)cloud_tick(&ctx);assert(starts==1);action_at(&ctx,342,1000);assert(starts==2);
 mock_status.bound=true;render(&ctx,fb);assert(saw_all&&!saw_selected);
 action_at(&ctx,342,1000);assert(downloads==1&&last_selected==4095&&!s_selected);
 EpdRect next=ui_pager_rect_at(UI_PAGER_NEXT,852);action_at(&ctx,next.x+10,next.y+10);assert(s_page==1);
 action_at(&ctx,80,240);assert(downloads==1&&s_selected==(1u<<5));render(&ctx,fb);assert(saw_selected);
 action_at(&ctx,342,1000);assert(downloads==2&&last_selected==(1u<<5));
 EpdRect last=ui_pager_rect_at(UI_PAGER_LAST,852);action_at(&ctx,last.x+10,last.y+10);assert(s_page==2);render(&ctx,fb);
 action_at(&ctx,80,360);assert(s_selected==((1u<<5)|(1u<<11)));action_at(&ctx,80,480);assert(downloads==2);
 action_at(&ctx,342,1000);assert(downloads==3&&last_selected==((1u<<5)|(1u<<11)));
 action_at(&ctx,80,360);assert(s_selected==(1u<<5));
 mock_status.busy=true;action_at(&ctx,80,240);action_at(&ctx,342,1000);assert(downloads==3&&s_selected==(1u<<5));
 EpdRect first=ui_pager_rect_at(UI_PAGER_FIRST,852);action_at(&ctx,first.x+10,first.y+10);assert(s_page==0);
 cloud_before_lock(&ctx);assert(joins==1&&!s_selected);cloud_tick(&ctx);assert(starts==2);mock_status.busy=false;cloud_tick(&ctx);assert(s_wifi_stage&&wifi_enters==2&&starts==2);render(&ctx,fb);cloud_tick(&ctx);assert(starts==3&&wifi_exits==2);
 action_at(&ctx,80,240);assert(s_selected==1);imported_mask=1;mock_status.revision++;cloud_tick(&ctx);assert(!s_selected);action_at(&ctx,80,240);assert(!s_selected);
 action_at(&ctx,80,360);assert(s_selected==2);action_at(&ctx,80,1000);assert(starts==4&&!s_selected);
 imported_mask=4095;sync_buttons=0;render(&ctx,fb);assert(!sync_buttons);imported_mask=0;
 imports=1;cloud_tick(&ctx);cloud_tick(&ctx);assert(notifies==1);imports=2;cloud_exit(&ctx);assert(notifies==2&&closes==1&&!s_selected);cloud_media_lost(&ctx);assert(closes==2&&!s_resume_refresh);
 wifi_ready=false;cloud_enter(&ctx);assert(starts==4&&s_wifi_live&&wifi_enters==3);assert(cloud_key(&ctx,0)==APP_REDRAW_NONE&&ctx.request_return&&wifi_keys==1);ctx.request_return=false;cloud_exit(&ctx);assert(wifi_exits==3);
 cloud_enter(&ctx);cloud_before_lock(&ctx);assert(!s_wifi_live&&s_wifi_stage);render(&ctx,fb);cloud_tick(&ctx);assert(s_wifi_live&&starts==4);cloud_media_lost(&ctx);assert(!s_wifi_live&&!s_wifi_stage);cloud_tick(&ctx);assert(starts==4);
 ctx.request_return=false;action_at(&ctx,60,100);assert(ctx.request_return);ui_gesture_event_t e={.type=99};assert(cloud_gesture(&ctx,&e)==APP_REDRAW_NONE);assert(cloud_edge_back(&ctx));
 puts("PASS: WiFi on every entry, no cloud request offline/AP/consumed/cancel, existing keyboard/key/back/partial-present delegates, connected STA retained, wake repeats gate, exit/media cleanup; real cloud UI: all12 pending/selected single or cross-page batch,tap toggles without downloads,busy/empty/imported guards,refresh/wake/exit selection reset,no retries and lifecycle cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='cloud-ui-') as t:
    c=Path(t)/'ui.c';c.write_text(u);exe=Path(t)/'ui'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
# 抽出真实 IMPORT 分支测试五入口，其他书架操作继续由 book_ui_host_test 覆盖。
# Exercise the real IMPORT branch; book_ui_host_test covers other shelf actions.
for rel in ['main/apps/app_book.c','boards/pico/src/main/apps/app_book.c']:
    path=R/rel
    if not path.exists():continue
    s=path.read_text();a=function('action_at',path);a=a[a.index('    if (s_view == IMPORT) {'):a.index('    if (s_view == TOC) {')]
    code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int value;}app_desc_t;
const app_desc_t app_files={1},app_transfer={2},app_cloud_sync={3};
typedef struct{bool request_menu;const app_desc_t*request_app;}app_ctx_t;
enum{IMPORT,SHELF,APP_REDRAW_NONE,APP_REDRAW_PAGE};static int s_view=IMPORT,requested;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static int ui_nav_hit(int x,int y){(void)x;(void)y;return -1;}
static void ui_nav_request(app_ctx_t*c,int t){(void)c;(void)t;}
static int import_finish(app_ctx_t*c){(void)c;return APP_REDRAW_PAGE;}
static void app_transfer_request_wifi_upload(void){requested=1;}
static void app_transfer_request_hotspot_start(void){requested=2;}
static void app_transfer_request_usb_start(void){requested=3;}
'''+function('import_rect',path)+'\nstatic int action_at(app_ctx_t*ctx,int x,int y){\n'+a+'return APP_REDRAW_NONE;}\n'+r'''
int main(void){app_ctx_t ctx={0};for(int i=0;i<5;i++){EpdRect r=import_rect(i);assert(r.y==214+i*150&&r.height==126);ctx.request_app=NULL;action_at(&ctx,r.x+10,r.y+10);assert(ctx.request_app==(i==0?&app_files:i==4?&app_cloud_sync:&app_transfer));if(i>0&&i<4)assert(requested==i);ctx.request_app=NULL;action_at(&ctx,r.x+10,r.y+r.height+5);assert(!ctx.request_app);}puts("PASS: all5 native import cards, original4 routes and cloud route/gaps");}
'''
    assert '阅星瞳APP导入' in function('draw_import',path)
    with tempfile.TemporaryDirectory(prefix='import-entry-') as t:
        c=Path(t)/'test.c';c.write_text(code);exe=Path(t)/'test';subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

# 执行两板文件首页真实命中分支，前三项不能被第四项劫持。
# Run both production file-hub branches; the fourth route never hijacks the first three.
for rel in ['main/apps/app_files.c','boards/pico/src/main/apps/app_files.c']:
    path=R/rel;source=path.read_text();body=function('on_gesture',path)
    a=body.index('    for (int i = 0; !long_press && i < 4; ++i)')
    b=body.index('    if(!long_press&&ui_rect_hit(storage_card_rect()',a)
    code='''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int id;}app_desc_t;
typedef struct{const app_desc_t*request_app;}app_ctx_t;
typedef struct{int x0,y0;}ui_gesture_event_t;
const app_desc_t app_transfer={1},app_cloud_sync={2};
enum{APP_REDRAW_NONE,APP_REDRAW_PAGE};
static int started;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void app_transfer_request_wifi_upload(void){started=1;}
static void app_transfer_request_hotspot_start(void){started=2;}
static void app_transfer_request_usb_start(void){started=3;}
'''
    code+='\n'.join(x for x in (R/'main/ui/ui_files_layout.h').read_text().splitlines() if not x.startswith(('#include','#pragma'))) +'\n'
    code+=function('home_transfer_rect',path)+'\nstatic int route(app_ctx_t*ctx,const ui_gesture_event_t*ev,bool long_press){\n'+body[a:b]+'return APP_REDRAW_PAGE;}\n'+r'''
int main(void){
 for(int i=0;i<4;++i){
  EpdRect r=home_transfer_rect(i);assert(r.y==662+i*102&&r.y+r.height<=UI_FILES_FOOTER_TOP-8);
  app_ctx_t ctx={0};ui_gesture_event_t e={r.x+r.width/2,r.y+r.height/2};started=0;
  assert(route(&ctx,&e,false)==APP_REDRAW_NONE);assert(ctx.request_app==(i==3?&app_cloud_sync:&app_transfer));assert(started==(i==3?0:i+1));
  ctx.request_app=NULL;started=0;assert(route(&ctx,&e,true)==APP_REDRAW_PAGE&&!ctx.request_app&&!started);
 }
 for(int y=1070;y<1216;++y){app_ctx_t ctx={0};ui_gesture_event_t e={342,y};route(&ctx,&e,false);assert(!ctx.request_app);}
 puts("PASS: native file hub all4 routes, App fourth without WiFi/AP/USB side effects, hold ignored and footer protected");
}
'''
    assert '"U 盘模式","阅星瞳APP导入"' in source
    with tempfile.TemporaryDirectory(prefix='file-cloud-entry-') as t:
        c=Path(t)/'test.c';c.write_text(code);exe=Path(t)/'test'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

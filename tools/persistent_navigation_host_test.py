#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际配网页范围/导航保护及常驻底栏结构。/ English: Actual network-page bounds/navigation guards and persistent-tab contracts."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
source=root/'main/apps/app_transfer.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define UI_LOCK_HEIGHT 1216
#define UI_NAV_REFRESH_END 1088
#define READ_PICO_TRANSFER_UPLOADING 3
#define UI_SYSTEM_KEYBOARD_TOP 560
#define TRANSFER_PASSWORD 1
typedef struct{uint16_t y0;}ui_gesture_event_t;
typedef struct{int unused;}app_ctx_t;
static int s_view;
static struct{int state;}s_status;
static bool usb, s_setup_chrome, s_cover_wifi_only;
static int tabs,active;
static bool usb_storage_active(void){return usb;}
static void ui_nav_draw(uint8_t*fb,int index){(void)fb;++tabs;active=index;}
'''+ '\n'.join(function(name,source) for name in ['transfer_content_end','transfer_footer_top','transfer_edge_back','transfer_navigation_enabled','transfer_navigation','transfer_back_swipe_area'])+r'''
int main(void){
 assert(transfer_content_end()==1088&&transfer_footer_top()+76<=1088);
 for(int busy=0;busy<2;++busy)for(int uploading=0;uploading<2;++uploading){
  usb=busy;s_status.state=uploading?READ_PICO_TRANSFER_UPLOADING:0;
  assert(transfer_edge_back(NULL)==(!busy&&!uploading));
 }
 uint8_t fb=0;transfer_navigation(&fb,2);assert(tabs==1&&active==2);
 transfer_navigation(&fb,3);assert(tabs==2&&active==3);
 usb=false;s_status.state=0;s_cover_wifi_only=true;
 assert(transfer_edge_back(NULL)&&!transfer_navigation_enabled(NULL));
 transfer_navigation(&fb,3);assert(tabs==2);
 for(s_view=0;s_view<=1;++s_view){
  assert(transfer_back_swipe_area(NULL,&(ui_gesture_event_t){.y0=559}));
  assert(transfer_back_swipe_area(NULL,&(ui_gesture_event_t){.y0=560})==(s_view!=TRANSFER_PASSWORD));
 }
 puts("PASS: actual transfer gray-navigation calls,bounded footer,USB/upload navigation guards");
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
for name in ['app_dashboard.c','app_files.c','app_device_settings.c','app_transfer.c']:
 assert '.persistent_navigation=true' in (root/'main/apps'/name).read_text(),name
assert '.persistent_navigation=true' not in (root/'main/apps/app_book.c').read_text()
print('PASS: all four system flows opt into persistent navigation; reader retains its own chrome')

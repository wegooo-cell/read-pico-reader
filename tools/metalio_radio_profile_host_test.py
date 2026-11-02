#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""官方S31运行策略及失败传播。/ Vendor S31 runtime profile and failure propagation."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
u=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef struct{int static_rx_buf_num, dynamic_rx_buf_num, rx_ba_win, ampdu_rx_enable, nvs_enable;}wifi_init_config_t;
#include <stdio.h>
typedef int esp_err_t;typedef int wifi_interface_t;
#define ESP_OK 0
#define WIFI_PS_NONE 0
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
static int calls,fail,selected;
static int next(void){return ++calls==fail?-7:0;}
static int esp_wifi_set_ps(int mode){assert(!mode);assert(calls==0);return next();}
static int esp_wifi_set_protocol(int interface,uint8_t protocol){assert(interface==selected&&protocol==7&&calls==1);return next();}
static int esp_wifi_set_max_tx_power(int8_t power){assert(power==48&&calls==2);return next();}
#include "transfer_s31_radio_profile.h"
int main(void){
 for(int bulk=0;bulk<2;++bulk)for(int ampdu=0;ampdu<2;++ampdu){
  wifi_init_config_t init={6,64,12,ampdu,0};
  transfer_s31_bulk_receive_config(&init,bulk);
  int expected_ba=12;
#ifdef CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP
  if(bulk&&ampdu)expected_ba=16;
#endif
  assert(init.static_rx_buf_num==(bulk&&ampdu?expected_ba:6));
  assert(init.rx_ba_win==expected_ba&&init.dynamic_rx_buf_num==64&&!init.nvs_enable);
 }
 for(int n=1;n<=16;++n){wifi_init_config_t init={2,64,n,1,0};transfer_s31_bulk_receive_config(&init,true);
#ifdef CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP
  assert(init.static_rx_buf_num==16&&init.rx_ba_win==16);
#else
  assert(init.static_rx_buf_num==(n<2?2:n));
#endif
 }
 wifi_init_config_t already={16,64,12,1,0};transfer_s31_bulk_receive_config(&already,true);
#ifdef CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP
 assert(already.static_rx_buf_num==16&&already.rx_ba_win==16);
#else
 assert(already.static_rx_buf_num==16&&already.rx_ba_win==12);
#endif
 wifi_init_config_t variants[]={{6,8,12,1,0},{6,64,32,1,0},{6,64,0,1,0}};
 for(unsigned i=0;i<sizeof(variants)/sizeof(variants[0]);++i){wifi_init_config_t old=variants[i];transfer_s31_bulk_receive_config(&variants[i],true);assert(!memcmp(&old,&variants[i],sizeof(old)));}
 transfer_s31_bulk_receive_config(NULL,true);
 for(selected=0;selected<2;++selected)for(fail=0;fail<4;++fail){calls=0;int err=transfer_s31_radio_profile(selected);assert(err==(fail?-7:0));assert(calls==(fail?fail:3));}puts("PASS: bounded bulk RX/BA, PSRAM16 minimum, normal STA/disabled AMPDU/unsupported budgets unchanged; STA/AP PS_NONE, BGN and12dBm unchanged; failures propagate");}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'esp_wifi.h').write_text('');(p/'test.c').write_text(u)
 for flags in ([],['-DCONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=1']):
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*flags,'-I'+str(p),'-I'+str(R/'components/read_pico_transfer'),str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

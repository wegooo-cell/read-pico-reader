#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际配对码重开、旧码忽略、完整接收与失败阶段显示。
English: Exercise actual restarted pairing codes, stale-code handling, receiver transfer and failure-stage messages.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1];source=R/'components/kiiko_device_sync/kiiko_device_sync.c';text=source.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kiiko_device_sync.h"
#define taskENTER_CRITICAL(...) ((void)0)
#define taskEXIT_CRITICAL(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
'''
unit+=text[text.index('#define SYNC_MAGIC'):text.index('static portMUX_TYPE')]
unit+='static kiiko_sync_status_t s_status;static kiiko_sync_io_t s_io;\n'
unit+=text[text.index('static kiiko_sync_role_t s_role;'):text.index('static int64_t now_ms(void)')]
unit+=r'''
static unsigned emissions,import_calls;static esp_err_t import_error;static bool oom;
static int64_t now_ms(void){return 1000;}
static bool emit(uint8_t type,uint32_t offset,uint32_t total,uint32_t value,const uint8_t *payload,uint16_t length,bool broadcast){(void)type;(void)offset;(void)total;(void)value;(void)payload;(void)length;(void)broadcast;++emissions;return true;}
static void peer_add(const uint8_t *mac,bool broadcast){(void)mac;(void)broadcast;}
static uint8_t *stream_alloc(size_t length){return oom?NULL:malloc(length);}
static esp_err_t import_settings(const uint8_t *data,size_t length){assert(length==128);for(unsigned i=0;i<128;++i)assert(data[i]==i);++import_calls;return import_error;}
'''
for name in ['crc32_bytes','pair_code','state_set','progress_set','code_set','fail','receiver_tick','handle_frame']:
 unit+=function(name,source)+'\n'
unit+=function('devsync_failure_text',R/'boards/pico/src/main/apps/app_device_settings.c')+'\n'
unit+=r'''
static void reset(kiiko_sync_role_t role){free(s_in);s_in=NULL;memset(&s_status,0,sizeof(s_status));s_role=role;s_state=KIIKO_SYNC_SCANNING;s_peer_known=s_confirm=false;s_nonce=s_code=s_total=0;s_done=0;oom=false;import_error=ESP_OK;emissions=0;for(int i=0;i<6;++i)s_own[i]=(uint8_t)(i+1);s_io.import_settings=import_settings;}
static void receive(bool reject){
 reset(KIIKO_SYNC_ROLE_RECEIVE);sync_rx_t item={0};for(int i=0;i<6;++i)item.mac[i]=(uint8_t)(8-i);
 sync_frame_t *f=(sync_frame_t*)item.data;f->type=SYNC_FRAME_ANNOUNCE;f->role=KIIKO_SYNC_ROLE_SEND;f->nonce=1;f->total=128;
 handle_frame(&item);assert(s_state==KIIKO_SYNC_PAIRED&&s_total==128);
 unsigned first=s_code;f->nonce=2;handle_frame(&item);assert(s_nonce==2&&s_code!=first&&s_code==pair_code(2,s_peer,s_own));
 s_confirm=true;receiver_tick(1000);assert(s_state==KIIKO_SYNC_TRANSFERRING&&s_in);
 for(unsigned off=0;off<128;off+=64){f->type=SYNC_FRAME_DATA;f->offset=off;f->length=64;for(unsigned j=0;j<64;++j)item.data[SYNC_HEADER+j]=(uint8_t)(off+j);handle_frame(&item);}
 assert(s_done==128&&s_status.percent==100);
 import_error=reject?ESP_ERR_INVALID_ARG:ESP_OK;f->type=SYNC_FRAME_FINISH;f->total=128;f->value=crc32_bytes(s_in,128);handle_frame(&item);
 assert(!s_in&&s_state==(reject?KIIKO_SYNC_FAILED:KIIKO_SYNC_DONE));
 if(reject)assert(s_status.failed_from==KIIKO_SYNC_APPLYING);
}
int main(void){
 receive(false);receive(true);assert(import_calls==2);
 reset(KIIKO_SYNC_ROLE_SEND);s_nonce=99;sync_rx_t item={0};for(int i=0;i<6;++i)item.mac[i]=(uint8_t)(8-i);sync_frame_t *f=(sync_frame_t*)item.data;
 f->type=SYNC_FRAME_FOUND;f->nonce=99;f->value=0;handle_frame(&item);assert(s_state==KIIKO_SYNC_SCANNING&&s_status.error==ESP_OK);
 f->value=pair_code(99,s_own,item.mac);handle_frame(&item);assert(s_state==KIIKO_SYNC_PAIRED);
 fail(ESP_ERR_TIMEOUT);assert(s_status.failed_from==KIIKO_SYNC_PAIRED);fail(ESP_FAIL);assert(s_status.failed_from==KIIKO_SYNC_PAIRED);
 const char *stages[]={"未找到另一台设备","配对后连接中断","传输中断","写入设置失败"};
 for(unsigned i=0;i<4;++i){char out[160];s_status.failed_from=(kiiko_sync_state_t)(i+1);devsync_failure_text(&s_status,ESP_ERR_TIMEOUT,out,sizeof(out));assert(strstr(out,stages[i])&&strstr(out,"等待超时"));}
 char out[160];devsync_failure_text(&s_status,0x1234,out,sizeof(out));assert(strstr(out,"0x1234"));
 puts("PASS: actual pairing restart adopts new nonce/code, stale code ignored, complete128-byte CRC/import flow and four failure-stage messages");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-sync-pair-') as d:
 p=Path(d);(p/'test.c').write_text(unit)
 (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\nenum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_TIMEOUT=1,ESP_ERR_INVALID_CRC,ESP_ERR_INVALID_RESPONSE,ESP_ERR_NO_MEM,ESP_ERR_INVALID_SIZE,ESP_ERR_INVALID_STATE,ESP_ERR_NOT_FOUND,ESP_ERR_INVALID_ARG,ESP_ERR_NOT_SUPPORTED};\n')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-I'+str(p),'-I'+str(R/'components/kiiko_device_sync/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

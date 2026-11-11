#!/usr/bin/env python3
"""Replay vendor reports through the production decoder, queues and GATT discovery chain.

SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import runpy,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
generic=runpy.run_path(str(ROOT/'tools/ble_turner_host_test.py'))
unit=generic['unit'].replace('int main(void)', 'int generic_regression(void)')
unit=unit[:unit.rfind('}')]+ 'return 0;\n'+unit[unit.rfind('}'):]
function=generic['function'];BLE=generic['BLE']
unit+=r'''
#define MAX_INPUT_CHRS 4
#define HID_CHR_CCCD 0x2902
#define HID_REPORT_REFERENCE 0x2908
#define HID_CHR_PROTOCOL_MODE 0x2a4e
#define HID_CHR_REPORT_MAP 0x2a4b
#define HID_CHR_REPORT 0x2a4d
#define HID_CHR_BOOT_KBD_INPUT 0x2a22
#define BLE_GATT_CHR_PROP_WRITE 8
#define BLE_GATT_CHR_PROP_READ 2
#define BLE_GATT_CHR_PROP_NOTIFY 16
#define BLE_HS_EDONE 14
#define BLE_PT_CRUMB_SUBSCRIBE 1
typedef struct {uint16_t u;} ble_uuid16_t;
#define BLE_UUID16_INIT(x) {x}
static int ble_uuid_cmp(const uint16_t *a,const uint16_t *b){return *a!=*b;}
struct ble_gatt_error {int status;};
struct ble_gatt_dsc {ble_uuid16_t uuid;uint16_t handle;};
struct ble_gatt_chr {ble_uuid16_t uuid;uint16_t def_handle,val_handle;unsigned properties;};
struct fake_mbuf {uint8_t first[1],tail[1];unsigned len;};
struct ble_gatt_attr {uint16_t handle;struct fake_mbuf *om;};
#define OS_MBUF_PKTLEN(m) ((m)->len)
static int os_mbuf_copydata(struct fake_mbuf *m,int start,int n,void *dest){assert(!start&&n==2);memcpy(dest,m->first,1);memcpy((char*)dest+1,m->tail,1);return 0;}
static uint16_t s_input_chr_vals[4],s_input_chr_cccds[4],s_input_chr_ends[4],s_input_chr_refs[4];
static uint8_t s_input_report_ids[4],s_input_report_types[4],s_input_chr_count,s_ref_index;
static uint8_t s_dsc_index,s_sub_index,s_sub_started;
static uint16_t s_hid_svc_end=100,s_report_map_handle,s_pmode_handle;
static int reads,writes,passes,finished,report_maps;
static uint16_t pending_handle,pass_start,pass_end;
static int (*read_callback)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*);
static int (*write_callback)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*);
static void *read_arg,*pass_arg;
static int read_fail_handle;
static int ble_gattc_read(uint16_t c,uint16_t h,int(*cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*),void *arg){assert(c==s_conn_handle);++reads;if(h==read_fail_handle)return 1;pending_handle=h;read_callback=cb;read_arg=arg;return 0;}
static int ble_gattc_write_flat(uint16_t c,uint16_t h,const void *v,size_t n,int(*cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*),void *arg){assert(c==s_conn_handle&&h&&v);(void)n;(void)arg;++writes;write_callback=cb;return 0;}
static int ble_gattc_disc_all_dscs(uint16_t c,uint16_t start,uint16_t end,int(*cb)(uint16_t,const struct ble_gatt_error*,uint16_t,const struct ble_gatt_dsc*,void*),void *arg){assert(c==s_conn_handle&&cb&&start<=end);++passes;pass_start=start;pass_end=end;pass_arg=arg;return 0;}
static int cccd_write_cb(uint16_t c,const struct ble_gatt_error*e,struct ble_gatt_attr*a,void*p){(void)c;(void)e;(void)a;(void)p;return 0;}
static void subscribe_finish(void){++finished;}
static void report_map_hints(const uint8_t *d,uint16_t n){(void)d;(void)n;++report_maps;}
#define OS_MBUF_DATA(m,type) ((type)(m)->first)
'''
for name in ['ingest_input_report','ble_pt_take_standby','subscribe_next','subscribe_ready',
             'report_map_cb']:
    unit+='\n'+function(BLE,name)
unit+='\nstatic void report_ref_next(void);\nstatic void dsc_next(void);\n'
for name in ['dsc_cb','report_ref_cb','report_ref_next','dsc_next','pmode_cb','chr_enum_done','chr_cb']:
    unit+='\n'+function(BLE,name)
unit+=r'''
static void setup_model(const char *name){
 reset_input();remote_reset();s_remote_model=pt_model_from_name(name);
 s_input_chr_count=4;s_conn_handle=7;
 memset(s_input_report_types,1,sizeof(s_input_report_types));
 for(int i=0;i<4;++i){s_input_chr_vals[i]=(uint16_t)(10+i*10);s_input_report_ids[i]=(uint8_t)(i+1);}
}
static void send_report(int handle,const uint8_t *d,size_t n){assert(ingest_input_report((uint16_t)handle,d,n));}
static void expect_turn(ble_pt_action_t direction,const uint8_t *payload,size_t n){
 ble_pt_event_t key;ble_pt_raw_t raw;
 assert(ble_pt_pop_key(&key)&&ble_pt_action_for_usage(key.usage,key.mods)==direction);
 assert(!ble_pt_pop_key(&key)&&s_held_usage==0);
 assert(ble_pt_pop_raw(&raw)&&raw.pressed&&raw.frame.len==n&&!memcmp(raw.frame.bytes,payload,n));
 assert(ble_pt_action_for_frame(&raw.frame)==direction);
 assert(ble_pt_pop_raw(&raw)&&!raw.pressed&&!ble_pt_pop_raw(&raw));
}
static void expect_none(void){ble_pt_event_t k;ble_pt_raw_t r;assert(!ble_pt_pop_key(&k)&&!ble_pt_pop_raw(&r)&&!ble_pt_take_standby());}
static void point(int handle,int flag,int x,int y){uint8_t p[4]={(uint8_t)flag,(uint8_t)x,(uint8_t)(((x>>8)&15)|((y&15)<<4)),(uint8_t)(y>>4)};send_report(handle,p,4);}
static void complete_read(int id,int type,int error){
 struct fake_mbuf m={{(uint8_t)id},{(uint8_t)type},2};struct ble_gatt_attr a={pending_handle,&m};struct ble_gatt_error e={error};
 read_callback(s_conn_handle,&e,&a,read_arg);
}
int main(void){
 generic_regression();
 assert(pt_model_from_name(NULL)==PT_GENERIC&&pt_model_from_name("Free 20")==PT_GENERIC);
 assert(pt_model_from_name("Free3-R")==PT_FREE3R&&pt_model_from_name("Smart 1-E")==PT_SMART1E);
 const uint8_t prev=1,next=2,zero3[3]={0},prev3[3]={1,0,0},next3[3]={2,0,0};
 setup_model("Free 2");assert(s_remote_model==PT_FREE2);
 // Every single-byte notification is one click, even within 500 ms and without release packets.
 for(int i=0;i<5;++i){send_report(10,&prev,1);expect_turn(BLE_PT_ACTION_PREV,&prev,1);}
 send_report(10,&next,1);expect_turn(BLE_PT_ACTION_NEXT,&next,1);
 assert(!ingest_input_report(30,prev3,3));expect_none(); // first button channel wins
 setup_model("Free 2");send_report(40,prev3,3);expect_turn(BLE_PT_ACTION_PREV,prev3,3);
 send_report(40,prev3,3);expect_none();send_report(40,zero3,3);
 send_report(40,prev3,3);expect_turn(BLE_PT_ACTION_PREV,prev3,3);
 const uint8_t bad3[3]={2,1,0};send_report(40,bad3,3);expect_none();
 setup_model("Free3-R");send_report(30,next3,3);expect_turn(BLE_PT_ACTION_PREV,next3,3);
 send_report(30,next3,3);expect_none();send_report(30,zero3,3);
 send_report(30,prev3,3);expect_turn(BLE_PT_ACTION_NEXT,prev3,3);
 s_bindings.frames[0]=frame_of(prev3,3);assert(ble_pt_action_for_frame(&s_bindings.frames[0])==BLE_PT_ACTION_PREV);
 const uint8_t sleep3[3]={8,0,0};send_report(30,sleep3,3);assert(ble_pt_take_standby()&&!ble_pt_take_standby());expect_none();
 send_report(30,sleep3,3);expect_none();send_report(30,zero3,3);send_report(30,sleep3,3);assert(ble_pt_take_standby());
 setup_model("Smart 1-E");for(int i=0;i<5;++i){send_report(10,&next,1);expect_turn(BLE_PT_ACTION_NEXT,&next,1);}
 uint8_t sleep=6;send_report(10,&sleep,1);assert(ble_pt_take_standby());expect_none();
 // Reject output reports, unknown handles and mouse channels before decoding.
 s_input_report_types[0]=2;assert(!ingest_input_report(10,&next,1));s_input_report_types[0]=1;
 assert(!ingest_input_report(99,&next,1)&&!ingest_input_report(20,&next,1));expect_none();
 for(int model=0;model<2;++model){
  setup_model(model?"HBTR003-X":"HBTR002-X");
  point(20,7,100,200);point(20,7,180,205);point(20,0,300,220);expect_turn(BLE_PT_ACTION_PREV,&prev,1);
  point(20,2,300,220);expect_none(); // no second event on duplicate lift
  point(20,7,500,200);point(20,2,250,200);expect_turn(BLE_PT_ACTION_NEXT,&next,1);
  point(20,7,100,200);point(20,0,150,201);expect_none(); // jitter
  point(20,7,100,200);point(20,0,300,500);expect_none(); // vertical
  point(20,7,100,200);point(20,1,300,200);point(20,0,300,200);expect_none(); // invalid contact cancels
  point(20,7,100,200);remote_reset();point(20,0,300,200);expect_none(); // interrupted gesture
  uint8_t standby[2]={0,16},released[2]={0};send_report(10,standby,2);assert(ble_pt_take_standby());
  send_report(10,standby,2);expect_none();send_report(10,released,2);send_report(10,standby,2);assert(ble_pt_take_standby());
  remote_reset();expect_none();
 }
 // Discover four Report characteristics; descriptors must remain within their own ranges.
 setup_model("Free 2");s_input_chr_count=0;s_pmode_handle=s_report_map_handle=0;
 struct ble_gatt_error ok={0},done={BLE_HS_EDONE};
 for(int i=0;i<4;++i){struct ble_gatt_chr c={{HID_CHR_REPORT},(uint16_t)(9+i*10),(uint16_t)(10+i*10),BLE_GATT_CHR_PROP_NOTIFY};chr_cb(7,&ok,&c,NULL);}
 assert(s_input_chr_ends[0]==18&&s_input_chr_ends[1]==28&&s_input_chr_ends[2]==38&&s_input_chr_ends[3]==100);
 s_pmode_handle=6;chr_cb(7,&done,NULL,NULL);assert(writes==1&&passes==0);
 write_callback(7,&ok,NULL,NULL);assert(writes==1&&passes==1&&pass_start==10&&pass_end==18); // no repeated protocol write
 for(int i=0;i<4;++i){
  struct ble_gatt_dsc ref={{HID_REPORT_REFERENCE},(uint16_t)(12+i*10)},cccd={{HID_CHR_CCCD},(uint16_t)(11+i*10)},foreign={{HID_CHR_CCCD},(uint16_t)101};
  dsc_cb(7,&ok,pass_start,&foreign,pass_arg);assert(!s_input_chr_cccds[i]);
  dsc_cb(7,&ok,pass_start,&cccd,pass_arg);dsc_cb(7,&ok,pass_start,&ref,pass_arg);dsc_cb(7,&done,pass_start,NULL,pass_arg);
 }
 assert(reads==1&&pending_handle==12&&writes==1);
 // Reference reads are serialized; mouse/output references never get subscriptions.
 complete_read(1,1,0);assert(reads==2&&pending_handle==22);
 complete_read(2,1,0);complete_read(3,2,0);complete_read(4,1,0);
 assert(reads==4&&writes==2&&s_sub_index==1);subscribe_next();assert(writes==3&&s_sub_index==4);subscribe_next();assert(finished);
 // A failed reference read skips that channel and continues; unknown peers keep generic subscriptions.
 s_ref_index=0;read_fail_handle=12;report_ref_next();assert(pending_handle==22);read_fail_handle=0;
 complete_read(2,1,1);complete_read(3,1,0);complete_read(4,1,0);
 s_remote_model=PT_GENERIC;s_ref_index=0;reads=0;report_ref_next();assert(!reads);
 puts("PASS: five vendor profiles, rapid clicks, held-state edges, manual overrides, standby exclusion, HBTR trajectories/cancel, report IDs/types and serialized GATT discovery");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-ble-models-') as directory:
    work=Path(directory)
    (work/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\nenum {ESP_OK,ESP_FAIL,ESP_ERR_NO_MEM,ESP_ERR_TIMEOUT,ESP_ERR_INVALID_STATE,ESP_ERR_INVALID_ARG};\n')
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                    '-fsanitize=address,undefined','-I'+str(work),'-I'+str(ROOT/'components/ble_page_turner/include'),
                    '-I'+str(ROOT/'components/ble_page_turner/src'),str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)

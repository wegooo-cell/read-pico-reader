#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：实际PMU初始化与深睡启动门禁，按键上电无SHORT仍单击恢复。
English: Actual PMU handshake and deep-resume gate accept key power-on without a SHORT event.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
PMU=ROOT/'components/read_pico_pmu/read_pico_pmu.c'
BOOT=ROOT/'boards/pico/src/main/app_main.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "power_double_click.h"
#include "read_pico_pmu_protocol.h"
#define ESP_OK 0
#define PMU_WAKE_POWER_KEY 2u
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_RESPONSE -2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define READ_PICO_PMU_I2C_HZ_DEFAULT 100000
#define I2C_ADDR_BIT_LEN_7 7
#define ESP_RST_POWERON 1
#define ESP_RST_SW 3
#define ESP_RST_PANIC 4
#define ESP_RST_WDT 7
#define ESP_RST_DEEPSLEEP 8
#define ESP_RST_BROWNOUT 9
#define ESP_RST_USB 11
typedef int esp_err_t;
typedef void *i2c_master_bus_handle_t;
typedef struct{unsigned i2c_addr,scl_speed_hz;}read_pico_pmu_config_t;
typedef struct{unsigned dev_addr_length,device_address,scl_speed_hz;}i2c_device_config_t;
static void *s_bus,*s_dev;static int s_soc_filter;static bool s_ready;
static power_double_click_t s_wake_click;
static bool s_scan_short,s_boot_short,s_boot_power_key,s_boot_key_release_pending;
typedef enum {READ_PICO_PMU_KEY_NONE,READ_PICO_PMU_KEY_SHORT,READ_PICO_PMU_KEY_LONG}read_pico_pmu_key_action_t;
static uint32_t s_lock_key_serial;
static struct{
 bool present,identity_ok,status_ok,event_ok;
 unsigned power_state,wake_reason,pending_events,boot_id,key_state,proto_major,proto_minor,fw_major,fw_minor,fw_patch,last_op_status;
 int last_err;
 struct{unsigned type,event_id,timestamp_ms,arg0;}event;
}s_snap;
static typeof(s_snap.event) events[PMU_EVENT_FIFO_DEPTH];static unsigned total,index_at;
static unsigned before_state,before_reason,after_state,after_reason,session_before,session_after;
static bool before_valid,after_valid,identity_valid,handshake_ok,after_handshake,init_failed,poll_failed,ack_failed;
static unsigned polls,ready,held,gate,delays,preference=1;
static bool key_held,event_valid=true;
static int64_t now_ms,s_lock_ignore_until_ms;
static int64_t esp_timer_get_time(void){return now_ms*1000;}
static bool pin,resuming=true;static int reset=ESP_RST_POWERON;
static int i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,void **dev){assert(bus&&cfg->device_address==PMU_I2C_ADDR);*dev=(void*)1;return init_failed?-1:0;}
static int i2c_master_bus_rm_device(void *dev){(void)dev;return 0;}
static int pmu_read_reg(unsigned reg,uint8_t *out,size_t n){assert(reg==PMU_REG_IDENTITY);memset(out,0,n);return 0;}
static bool parse_identity(const uint8_t *raw){(void)raw;s_snap.identity_ok=identity_valid;s_snap.boot_id=session_before;return identity_valid;}
static void recover_seq(void){}
static void vTaskDelay(unsigned ms){delays+=ms;}
static int read_pico_pmu_poll(void){
 ++polls;s_snap.identity_ok=identity_valid;
 s_snap.status_ok=after_handshake?after_valid:before_valid;
 s_snap.power_state=after_handshake?after_state:before_state;
 s_snap.wake_reason=after_handshake?after_reason:before_reason;
 s_snap.boot_id=after_handshake?session_after:session_before;
 s_snap.key_state=key_held?1:0;
 s_snap.event_ok=event_valid&&index_at<total;s_snap.pending_events=total-index_at;
 if(index_at<total)s_snap.event=events[index_at];
 return poll_failed?-1:0;
}
static int read_pico_pmu_event_ack(unsigned id){assert(index_at<total&&id==events[index_at].event_id);if(ack_failed)return -1;++index_at;return 0;}
static int read_pico_pmu_cmd(unsigned cmd,const uint8_t *arg,unsigned n){assert(cmd==PMU_CMD_HOST_READY&&arg&&n==1);after_handshake=true;return handshake_ok?0:-1;}
static int wait_power_state(unsigned state,int ms){assert(state==PMU_PWR_RUNNING&&ms==1000);return read_pico_pmu_poll()==0&&s_snap.power_state==state?0:-1;}
static void add(unsigned type){assert(total<PMU_EVENT_FIFO_DEPTH);events[total]=(typeof(events[0])){type,total+1,total*100+100,before_reason};++total;}
'''
for name in ('read_pico_pmu_take_key_wakeup','read_pico_pmu_take_key_wakeup_single','read_pico_pmu_take_boot_short_press'):
 unit+=function(name,PMU)+'\n'
if 'bool read_pico_pmu_take_boot_power_key(' in PMU.read_text():
 unit+=function('read_pico_pmu_take_boot_power_key',PMU)+'\n'
if 'void read_pico_pmu_rearm_after_boot_key(' in PMU.read_text():
 unit+=function('read_pico_pmu_rearm_after_boot_key',PMU)+'\n'
unit+=function('read_pico_pmu_take_key_action',PMU)+'\n'
unit+=function('app_lock_ignore_for',ROOT/'boards/pico/src/main/app/app_loop.c')+'\n'
unit+=function('read_pico_pmu_deinit',PMU)+'\n'+function('read_pico_pmu_init',PMU)+'\n'
unit+=r'''
static bool lock_pin_enabled(void){return pin;}
static unsigned app_settings_wake_presses(void){return preference;}
static void pico_boot_ready(void){++ready;}
static void pico_boot_hold_resume(void){++held;}
static void app_lock_boot_gate(void *hl,int tp,int acc){(void)hl;(void)tp;(void)acc;++gate;}
static bool app_lock_resume_frame(void *hl){(void)hl;return false;}
static void run_boot(void){int hl=0;bool restored_lock=false;(void)restored_lock;struct{bool pmu_ready;int touch,sensor;}hw={s_ready,0,0};
'''
boot=BOOT.read_text();a=boot.index('    const bool one_press_boot = resuming &&');b=boot.index('    rp_boot_mark("lock");',a)
unit+=boot[a:b]+'}\n'
unit+=r'''
// Main-loop input policy: its 2s deadline is set before slow on_enter/first-frame work.
static read_pico_pmu_key_action_t normal_input_poll(void){
 read_pico_pmu_key_action_t action=read_pico_pmu_take_key_action();
 return now_ms<s_lock_ignore_until_ms?READ_PICO_PMU_KEY_NONE:action;
}
static void reset_test(void){
 memset(&s_snap,0,sizeof(s_snap));total=index_at=polls=ready=held=gate=delays=0;
 s_ready=s_scan_short=s_boot_short=s_boot_power_key=s_boot_key_release_pending=false;s_wake_click=(power_double_click_t){0};
 before_state=PMU_PWR_BOOT_WAIT;after_state=PMU_PWR_RUNNING;before_reason=after_reason=2;
 session_before=session_after=17;before_valid=after_valid=identity_valid=handshake_ok=true;
 after_handshake=init_failed=poll_failed=ack_failed=false;
 preference=1;pin=key_held=false;event_valid=true;resuming=true;reset=ESP_RST_POWERON;now_ms=1000;s_lock_ignore_until_ms=0;
}
static int initialize(void){read_pico_pmu_config_t config={0};return read_pico_pmu_init((void*)1,&config);}
int main(void){
 // No SHORT/LONG is required when the PMU was powering on from a validated key source.
 for(int event=0;event<2;++event)for(int state=PMU_PWR_POWERING_ON;state<=PMU_PWR_BOOT_WAIT;++state){
  reset_test();before_state=state;if(event){add(PMU_EVT_HOST_STARTED);add(PMU_EVT_KEY_UP);}
  assert(initialize()==ESP_OK);run_boot();
  assert(ready==1&&held==1&&!gate&&delays==0);
  ready=held=gate=0;run_boot();assert(gate==1&&!ready&&!held); // consumed once
 }
 // A stale key reason in RUNNING, other wake sources, failed status, or a PMU restart cannot unlock.
 for(unsigned risk=0;risk<16;++risk){
  reset_test();add(PMU_EVT_HOST_STARTED);
  switch(risk){
   case 0:before_state=PMU_PWR_RUNNING;break;
   case 1:before_state=PMU_PWR_SOFT_SLEEP;break;
   case 2:before_state=PMU_PWR_RESETTING;break;
   case 3:before_reason=after_reason=0;break;
   case 4:before_reason=after_reason=1;break;
   case 5:before_reason=after_reason=3;break;
   case 6:before_reason=after_reason=4;break;
   case 7:before_reason=after_reason=5;break;
   case 8:before_reason=after_reason=6;break;
   case 9:before_valid=false;break;
   case 10:after_valid=false;break;
   case 11:session_after=18;break;
   case 12:after_reason=6;break;
   case 13:after_state=PMU_PWR_BOOT_WAIT;break;
   case 14:handshake_ok=false;break;
   case 15:session_before=session_after=0;break;
  }
  (void)initialize();run_boot();assert(gate==1&&!ready&&!held);
 }
 const int resets[]={ESP_RST_SW,ESP_RST_PANIC,ESP_RST_WDT,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT,ESP_RST_USB};
 for(unsigned i=0;i<sizeof(resets)/sizeof(resets[0]);++i){reset_test();reset=resets[i];assert(initialize()==ESP_OK);run_boot();assert(gate&&!ready);}
 for(int password=0;password<2;++password)for(int resume=0;resume<2;++resume)for(int count=1;count<=2;++count){
  reset_test();pin=password;resuming=resume;preference=count;
  assert(initialize()==ESP_OK);run_boot();bool direct=!password&&resume&&count==1;
  assert(ready==direct&&held==direct&&gate==!direct);
 }
 // Failed/repeated initialization must erase evidence from the previous successful handshake.
 reset_test();assert(initialize()==ESP_OK);init_failed=true;assert(initialize()!=ESP_OK);run_boot();assert(gate&&!ready);
 reset_test();assert(initialize()==ESP_OK);assert(read_pico_pmu_deinit()==ESP_OK);run_boot();assert(gate&&!ready);
 reset_test();assert(initialize()==ESP_OK);before_state=PMU_PWR_RUNNING;after_handshake=false;assert(initialize()==ESP_OK);run_boot();assert(gate&&!ready);
 // Ordinary key events and delayed release retain their original route, including a failed ACK retry.
 reset_test();before_state=PMU_PWR_RUNNING;add(PMU_EVT_KEY_SHORT);assert(initialize()==ESP_OK);run_boot();assert(ready&&!gate);
 reset_test();before_state=PMU_PWR_RUNNING;assert(initialize()==ESP_OK);add(PMU_EVT_KEY_LONG);run_boot();assert(ready&&!gate);
 reset_test();before_state=PMU_PWR_RUNNING;assert(initialize()==ESP_OK);add(PMU_EVT_KEY_SHORT);ack_failed=true;assert(!read_pico_pmu_take_key_wakeup_single());ack_failed=false;run_boot();assert(ready&&!gate);
 // Power-on evidence never creates one half of a double click.
 reset_test();assert(initialize()==ESP_OK);add(PMU_EVT_KEY_SHORT);assert(!read_pico_pmu_take_key_wakeup());add(PMU_EVT_KEY_SHORT);assert(read_pico_pmu_take_key_wakeup());
 // The boot gesture may release after the handshake scan or after a very slow first frame.
 const unsigned delayed_keys[]={PMU_EVT_KEY_SHORT,PMU_EVT_KEY_LONG};
 const unsigned frame_times[]={3000,30000};
 for(unsigned k=0;k<2;++k)for(unsigned f=0;f<2;++f)for(unsigned phase=0;phase<2;++phase)
 for(unsigned raw=0;raw<2;++raw){
  reset_test();assert(initialize()==ESP_OK);
  if(!phase)add(delayed_keys[k]);else key_held=true;
  run_boot();assert(ready&&!gate);
  app_lock_ignore_for(2000);now_ms+=frame_times[f];assert(now_ms>s_lock_ignore_until_ms);
  if(phase){
   add(PMU_EVT_KEY_LONG);assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE);
   now_ms+=5000;assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE); // still held, no wall-clock expiry
   key_held=false;if(raw)add(PMU_EVT_KEY_UP);add(delayed_keys[k]);
  }
  assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE&&index_at==total);
  // A subsequent physical gesture still performs its usual action once release is confirmed.
  if(raw)add(PMU_EVT_KEY_DOWN);add(PMU_EVT_KEY_SHORT);
  assert(normal_input_poll()==READ_PICO_PMU_KEY_SHORT);
  assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE);
 }
 // A power-on press with no tail must not swallow the first later deliberate press.
 reset_test();assert(initialize()==ESP_OK);run_boot();now_ms+=5000;
 assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE);add(PMU_EVT_KEY_LONG);
 assert(normal_input_poll()==READ_PICO_PMU_KEY_LONG);
 // Failed reads/CRC/ACK keep the handoff pending; stale events are never returned as actions.
 for(int failure=0;failure<5;++failure){
  reset_test();assert(initialize()==ESP_OK);run_boot();now_ms+=30000;add(PMU_EVT_KEY_SHORT);
  if(failure==0)poll_failed=true;else if(failure==1)after_valid=false;
  else if(failure==2)identity_valid=false;else if(failure==3)event_valid=false;else ack_failed=true;
  assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE&&index_at<total);
  poll_failed=ack_failed=false;after_valid=identity_valid=event_valid=true;now_ms+=5000;
  assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE&&index_at==total);
  add(PMU_EVT_KEY_SHORT);assert(normal_input_poll()==READ_PICO_PMU_KEY_SHORT);
 }
 // A full queue may take another bounded poll to confirm empty; non-key entries cannot rearm a held key.
 reset_test();assert(initialize()==ESP_OK);run_boot();now_ms+=30000;
 for(unsigned i=0;i<PMU_EVENT_FIFO_DEPTH;++i)add(i&1?PMU_EVT_KEY_LONG:PMU_EVT_HOST_READY);
 assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE&&index_at==total);
 assert(normal_input_poll()==READ_PICO_PMU_KEY_NONE);
 total=index_at=0;add(PMU_EVT_KEY_SHORT);assert(normal_input_poll()==READ_PICO_PMU_KEY_SHORT);
 // No boot-key ownership is installed for PIN/double/no-resume boots or non-key sources.
 for(int excluded=0;excluded<4;++excluded){
  reset_test();if(excluded==0)pin=true;else if(excluded==1)preference=2;
  else if(excluded==2)resuming=false;else before_reason=after_reason=6;
  assert(initialize()==ESP_OK);run_boot();assert(gate);now_ms+=30000;
  add(PMU_EVT_KEY_SHORT);assert(normal_input_poll()==READ_PICO_PMU_KEY_SHORT);
 }
 puts("PASS: late SHORT/LONG and held-release tails are discarded across 3s/30s first frames; no raw UP required; read/CRC/ACK failures stay protected; subsequent deliberate presses work");
 puts("PASS: actual handshake and boot gate accept key power-on without SHORT; reject stale/invalid/other-source evidence, reset/reinit failures, PIN/double/cold boots; retain ordinary and delayed key events");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-boot-power-key-') as temp:
 p=Path(temp);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-I'+str(ROOT/'components/read_pico_pmu/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

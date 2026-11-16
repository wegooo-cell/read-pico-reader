"""S31 production key sampler/wake consumption and shared-IRQ release barrier."""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
unit=(ROOT/'components/read_pico_pmu/include/power_double_click.h').read_text().replace('#pragma once','')+r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define pdMS_TO_TICKS(x) (x)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
enum {READ_PICO_PMU_KEY_NONE,READ_PICO_PMU_KEY_SHORT,READ_PICO_PMU_KEY_LONG};
typedef int read_pico_pmu_key_action_t;
typedef int esp_err_t;
static struct {unsigned key_state;} state;
static bool held,long_sent,candidate,ready=true,keys_paused;
static uint32_t key_epoch,s_lock_key_serial;
static int64_t candidate_since,down_since,clock_us;
static int key_lock,action,io_error,poll_error;
static power_double_click_t s_wake_click;
static uint16_t inputs=0xffff;
static unsigned input_reads,fail_read,release_read;
static unsigned drains;
static int ioe=1,mutex;
static void battery_reset(void){}
#define pdTRUE 1
static int xSemaphoreTake(int m,int timeout){(void)m;assert(timeout==100);return pdTRUE;}
static void xSemaphoreGive(int m){(void)m;}
static int64_t esp_timer_get_time(void){return clock_us;}
static int metalio_io_read(uint16_t *value){++input_reads;*value=input_reads==release_read?0xffff:inputs;return input_reads==fail_read?-1:io_error;}
static int fca9555_read_input(int device,int port,uint8_t *value){
 assert(device==ioe&&port==1);
 uint16_t sample;
 int status=metalio_io_read(&sample);
 *value=(uint8_t)(sample>>8);
 return status;
}
static void metalio_volume_sample(uint16_t value,int64_t ms){(void)value;(void)ms;}
static void metalio_volume_clear(void){++drains;}
#define GPIO_NUM_61 61
static int gpio_get_level(int pin){assert(pin==61);return 1;}
static void metalio_ai_sample(bool down,int64_t ms){assert(!down);(void)ms;}
static void metalio_ai_clear(void){}
static int read_pico_pmu_poll(void){return poll_error;}
static const typeof(state) *read_pico_pmu_get(void){return &state;}
static void read_pico_clear_ioe_int(void){}
static void vTaskDelay(int ms){clock_us+=(int64_t)ms*1000;}
'''
for name in ('read_pico_pmu_lock_key_serial','keys','read_pico_pmu_keys_pause','read_pico_pmu_keys_resume','read_pico_pmu_latch_power_wakeup','read_pico_pmu_drain_events','read_pico_pmu_take_key_wakeup','read_pico_pmu_take_key_wakeup_single'):
    unit+=function(name,ROOT/'components/read_pico_pmu/metalio_power_adapter.c')+'\n'
unit+=function('metalio_confirm_power_key',ROOT/'components/metalio_hw/metalio_hw.c')+'\n'
unit+=function('metalio_confirm_light_wake_key',ROOT/'components/metalio_hw/metalio_hw.c')+'\n'
unit+=function('app_lock_wait_key_idle',ROOT/'boards/metalio47/src/main/sleep.c')+'\n'
unit+=r'''
static void sample(bool down,int ms){inputs=down?0x7fff:0xffff;clock_us=(int64_t)ms*1000;keys();}
static void reset(void){s_wake_click=(power_double_click_t){0};held=long_sent=candidate=keys_paused=false;key_epoch=s_lock_key_serial=0;candidate_since=down_since=clock_us=0;state.key_state=0;action=io_error=poll_error=drains=0;inputs=0xffff;input_reads=fail_read=release_read=0;}
int main(void){
 reset();sample(true,0);sample(false,10);sample(false,40);assert(!held&&!read_pico_pmu_take_key_wakeup());
 // Short clicks wake on the debounced release, not a stale held level.
 reset();sample(true,0);sample(true,30);assert(held&&!read_pico_pmu_take_key_wakeup());
 sample(false,80);sample(false,110);assert(!held&&!read_pico_pmu_take_key_wakeup()&&read_pico_pmu_lock_key_serial()==1);
 sample(true,180);sample(true,210);sample(false,260);sample(false,290);assert(read_pico_pmu_take_key_wakeup()&&!read_pico_pmu_take_key_wakeup()&&read_pico_pmu_lock_key_serial()==2);
 // Consume the lock-initiating long action once; its continuing hold/release cannot wake again.
 reset();sample(true,0);sample(true,30);sample(true,730);assert(!read_pico_pmu_take_key_wakeup());
 sample(true,800);assert(!read_pico_pmu_take_key_wakeup());sample(false,810);sample(false,840);assert(!read_pico_pmu_take_key_wakeup()&&!read_pico_pmu_lock_key_serial());
 // A later short click can be queued while collage composition occupies the UI thread.
 read_pico_pmu_drain_events();sample(true,900);sample(true,930);sample(false,1000);sample(false,1030);
 sample(true,1100);sample(true,1130);sample(false,1200);sample(false,1230);
 clock_us=4000000;assert(read_pico_pmu_take_key_wakeup()&&!read_pico_pmu_take_key_wakeup());
 // Release barrier must finish without a GPIO2-high condition (shared touch/sensor IRQ).
 reset();app_lock_wait_key_idle(800);assert(clock_us==40000&&drains==3);
 reset();held=true;state.key_state=1;app_lock_wait_key_idle(800);assert(clock_us==800000);
 reset();poll_error=-1;app_lock_wait_key_idle(800);assert(clock_us==800000);
 reset();io_error=-1;sample(true,0);sample(true,1000);assert(!held&&!read_pico_pmu_take_key_wakeup());
 reset();sample(true,10);assert(candidate&&!held&&read_pico_pmu_keys_pause()==ESP_ERR_INVALID_STATE&&!keys_paused);
 // Masked output latches cannot synthesize a key; resumption starts from the real input level.
 reset();assert(read_pico_pmu_keys_pause()==ESP_OK);sample(true,0);sample(true,1000);assert(!held&&!action);
 inputs=0xffff;read_pico_pmu_keys_resume();assert(!keys_paused&&!held&&!read_pico_pmu_take_key_wakeup());
 sample(true,1100);sample(true,1130);sample(false,1200);sample(false,1230);assert(action==READ_PICO_PMU_KEY_SHORT);
 assert(read_pico_pmu_keys_pause()==ESP_ERR_INVALID_STATE&&action==READ_PICO_PMU_KEY_SHORT&&!keys_paused);
 assert(!read_pico_pmu_take_key_wakeup());sample(true,1300);sample(true,1330);assert(read_pico_pmu_keys_pause()==ESP_ERR_INVALID_STATE);
 // 恢复方向后的一次低电平/读错不得生成短按或长按。/ One low sample or IO fault after restoring directions must not synthesize a short/long press.
 for(unsigned fault=0;fault<3;++fault){
  reset();assert(read_pico_pmu_keys_pause()==ESP_OK);inputs=0x7fff;
  if(fault==0)release_read=2;else fail_read=fault;
  read_pico_pmu_keys_resume();assert(!held&&!candidate&&!state.key_state);
  sample(false,100);sample(false,130);sample(false,1000);
  assert(!read_pico_pmu_take_key_wakeup_single()&&!read_pico_pmu_lock_key_serial());
 }
 // 稳定真实按住仍由松开生成一个动作，不因恢复而重复。/ A stable real hold still generates one release action without a restore duplicate.
 reset();assert(read_pico_pmu_keys_pause()==ESP_OK);inputs=0x7fff;
 read_pico_pmu_keys_resume();assert(held&&candidate&&state.key_state);
 assert(!read_pico_pmu_take_key_wakeup_single());sample(false,100);sample(false,130);
 assert(read_pico_pmu_take_key_wakeup_single()&&!read_pico_pmu_take_key_wakeup_single());
 // Single mode accepts one fresh release or long action, never a continuing held level.
 reset();sample(true,0);sample(true,30);assert(!read_pico_pmu_take_key_wakeup_single());
 sample(false,80);sample(false,110);assert(read_pico_pmu_take_key_wakeup_single()&&!read_pico_pmu_take_key_wakeup_single());
 reset();sample(true,0);sample(true,30);sample(true,730);assert(read_pico_pmu_take_key_wakeup_single());
 sample(true,800);assert(!read_pico_pmu_take_key_wakeup_single());
 sample(false,810);sample(false,840);assert(!read_pico_pmu_take_key_wakeup_single());
 // P17-only EXT1 survives a release during rail restore; one event never bypasses double-click.
 reset();clock_us=1000000;read_pico_pmu_latch_power_wakeup();
 assert(read_pico_pmu_lock_key_serial()==1&&!read_pico_pmu_take_key_wakeup());
 sample(true,1100);sample(true,1130);sample(false,1200);sample(false,1230);
 assert(read_pico_pmu_take_key_wakeup()&&!read_pico_pmu_take_key_wakeup());
 reset();clock_us=1000000;read_pico_pmu_latch_power_wakeup();read_pico_pmu_latch_power_wakeup();
 assert(read_pico_pmu_lock_key_serial()==1&&read_pico_pmu_take_key_wakeup_single());
 reset();assert(read_pico_pmu_keys_pause()==ESP_OK);read_pico_pmu_latch_power_wakeup();assert(!action&&!s_lock_key_serial);
 reset();sample(true,0);sample(true,30);read_pico_pmu_latch_power_wakeup();assert(!action&&!s_lock_key_serial);
 sample(false,80);sample(false,110);read_pico_pmu_latch_power_wakeup();assert(s_lock_key_serial==1&&read_pico_pmu_take_key_wakeup_single());
 // Only two valid physical P17 lows qualify; short glitches, other inputs and either I2C fault do not.
 reset();inputs=0x7fff;assert(metalio_confirm_power_key()&&input_reads==2&&clock_us==30000);
 for(unsigned i=0;i<4;i++){
  reset();inputs=0x7fff;
  if(i<2)fail_read=i+1;else if(i==2)release_read=2;else inputs=0xfffe;
  assert(!metalio_confirm_power_key()&&!s_lock_key_serial&&!action);
 }
 reset();inputs=0x7fff;assert(metalio_confirm_light_wake_key()&&input_reads==2&&clock_us==10000);
 for(unsigned i=0;i<4;i++){
  reset();inputs=0x7fff;
  if(i<2)fail_read=i+1;else if(i==2)release_read=2;else inputs=0xfeff;
  assert(!metalio_confirm_light_wake_key()&&!s_lock_key_serial&&!action);
 }
 puts("PASS: production S31 short/long wake consumption, no held-level duplicate, queued click during collage, shared-IRQ independent release and failed-poll timeout");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

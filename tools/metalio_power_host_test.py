"""Exercise actual S31 gauge/charger adapter and volume queue functions with I2C faults."""
from pathlib import Path
import os, re, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
CC = os.environ.get('CC', 'cc')

def function(path, name):
    src=(root/path).read_text()
    match=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*\)\s*\{',src,re.M)
    assert match, name
    at=match.end(); depth=1
    while depth:
        depth+=(src[at]=='{')-(src[at]=='}'); at+=1
    return src[match.start():at]+'\n'

unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "read_pico_pmu_protocol.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_RESPONSE -2
#define ESP_LOGI(...) ((void)0)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
typedef int esp_err_t;
static struct {bool quick_ok,status_ok;uint32_t flags;unsigned battery_mv,soc_permille,charge_state;}state;
static int64_t battery_at,clock_us;
static bool charger_initialized;
static int64_t charger_retry_at,charge_valid_at;
static int64_t esp_timer_get_time(void){return clock_us;}
static bool current_ok,all_fault;
static int current_ma;
static unsigned mv=4000,soc=65,raw=8,ctrl=0x20,otg;
static int fail_address,fail_reg=-1,reads;
// 充电驱动由 metalio_charger 组件提供；适配器只委托并读取 ready。
// The metalio_charger component owns the driver; the adapter only delegates and reads ready.
static int charger_init_calls;
static esp_err_t charger_init_result;
static bool charger_ready_result;
static const char *esp_err_to_name(int err){(void)err;return "mock";}
static esp_err_t metalio_charger_init(void){++charger_init_calls;return charger_init_result;}
static bool metalio_charger_ready(void){return charger_ready_result;}
static esp_err_t metalio_read(unsigned a,unsigned r,void *out,size_t n){
 ++reads;if(all_fault||(int)a==fail_address||(int)r==fail_reg||(a==0x55&&r==0x0c&&!current_ok))return ESP_FAIL;
 unsigned value= a==0x55?(r==8?mv:r==0x0c?(uint16_t)current_ma:soc):r==0x1e?raw:r==0x16?ctrl:r==0x18?otg:0x42;
 assert((a==0x55&&(r==8||r==0x2c||r==0x0c)&&n==2)||(a==0x6b&&(r==0x1e||r==0x16||r==0x18)&&n==1));
 uint8_t *p=out;p[0]=value&255;if(n==2)p[1]=value>>8;return ESP_OK;
}
static int volume_lock,volume_events[8];
static unsigned volume_head,volume_count;
'''
for name in ('charger_init','battery'):
    unit+=function('components/read_pico_pmu/metalio_power_adapter.c',name)
for name in ('metalio_volume_sample','metalio_take_volume','metalio_volume_clear'):
    unit+=function('components/metalio_hw/metalio_hw.c',name)
unit+=r'''
static void sample(void){clock_us+=2000000;battery();}
int main(void){
 (void)esp_err_to_name;
 // 适配器只委托新驱动：不自行探测寄存器，ready 完全取自驱动。
 // The adapter only delegates to the new driver: no register probing, ready comes from the driver.
 int before=reads;
 charger_init_result=ESP_OK;charger_ready_result=true;charger_initialized=false;
 assert(charger_init()==ESP_OK&&charger_initialized&&charger_init_calls==1&&reads==before);
 charger_init_result=ESP_FAIL;charger_ready_result=false;
 assert(charger_init()==ESP_FAIL&&!charger_initialized&&charger_init_calls==2&&reads==before);
 // 失败后保留30秒重试节奏；2秒采样节流不提前重试，最后一次失败满30秒才重来。
 // A failed init retries after30s; the2s battery cadence must not retry early.
 charger_init_result=ESP_FAIL;charger_ready_result=false;charger_initialized=false;
 charger_retry_at=0;battery_at=0;clock_us=0;
 battery();assert(charger_init_calls==3&&!charger_initialized&&charger_retry_at==30000000);
 clock_us=2000000;battery();assert(charger_init_calls==3);
 clock_us=29999999;battery();assert(charger_init_calls==3);
 charger_init_result=ESP_OK;charger_ready_result=true;
 clock_us=32000000;battery();assert(charger_init_calls==4&&charger_initialized);
 charger_init_result=ESP_FAIL;charger_ready_result=false;
 charger_initialized=false;charger_retry_at=0;battery_at=0;clock_us=0;
 battery();assert(charger_init_calls==5&&!charger_initialized);
 charger_init_result=ESP_OK;charger_ready_result=true;
 clock_us=32000000;battery();assert(charger_init_calls==6&&charger_initialized);
 battery_at=clock_us=0;ctrl=0x20;otg=0;
 sample();assert(state.status_ok&&state.battery_mv==4000&&state.soc_permille==650);
 assert((state.flags&PMU_STATUS_BATTERY_VALID)&&(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 // 相同电压可有任意有效SOC，电压变化也不改变百分比；I2C失败不得以电压补算。
 // Valid SOC is independent of voltage; failed gauge reads never fall back to a voltage curve.
 for(unsigned value=0;value<=100;value++){
  soc=value;mv=4000;sample();assert((state.flags&PMU_STATUS_BATTERY_VALID)&&state.soc_permille==value*10);
 }
 soc=37;for(mv=2500;mv<=5000;mv+=100){sample();assert((state.flags&PMU_STATUS_BATTERY_VALID)&&state.soc_permille==370);}
 mv=4000;fail_reg=0x2c;sample();assert(!(state.flags&PMU_STATUS_BATTERY_VALID));fail_reg=-1;soc=65;sample();
 int reads_before=reads;clock_us+=1999999;battery();assert(reads==reads_before);clock_us++;battery();assert(reads>reads_before);
 for(unsigned stage=0;stage<4;stage++){
  raw=stage<<3;ctrl=0x20;otg=0;sample();assert(!!(state.flags&PMU_STATUS_CHARGING_ACTIVE)==(stage!=0));
  ctrl=0;sample();assert(!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
  ctrl=0x20;otg=0x40;sample();assert(!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 }
 raw=8;ctrl=0x20;otg=0;fail_reg=0x16;sample();assert(state.charge_state==PMU_CHARGE_UNKNOWN&&!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 fail_reg=-1;fail_address=0x55;sample();assert(!(state.flags&PMU_STATUS_BATTERY_VALID)&&state.status_ok);
 fail_address=0;soc=101;sample();assert(!(state.flags&PMU_STATUS_BATTERY_VALID));
 soc=0;sample();assert((state.flags&PMU_STATUS_BATTERY_VALID)&&state.soc_permille==0);
 soc=100;sample();assert(state.soc_permille==1000);
 fail_address=0x6b;mv=2000;sample();assert(state.status_ok&&!(state.flags&PMU_STATUS_BATTERY_VALID)&&(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 // 实际电流独立补足CHG_STAT漏报；明确停止与OTG优先，I2C未知只短暂保留。
 // Actual current covers missing CHG_STAT; explicit stop/OTG wins, and unknown I2C only retains evidence briefly.
 fail_address=0;mv=4000;soc=50;raw=0;ctrl=0x20;otg=0;current_ok=true;current_ma=6;
 sample();assert(state.flags&PMU_STATUS_CHARGING_ACTIVE);
 raw=8;current_ma=-6;sample();assert(state.flags&PMU_STATUS_CHARGING_ACTIVE);
 raw=0;sample();assert(!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 raw=0;current_ma=5;sample();assert(!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 current_ma=6;ctrl=0;sample();assert(!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 ctrl=0x20;otg=0x40;sample();assert(!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 otg=0;fail_reg=0x1e;sample();assert(state.flags&PMU_STATUS_CHARGING_ACTIVE);
 fail_reg=-1;fail_address=0x6b;sample();assert(state.flags&PMU_STATUS_CHARGING_ACTIVE);
 current_ok=false;all_fault=true;sample();assert(state.status_ok&&(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 sample();assert(state.flags&PMU_STATUS_CHARGING_ACTIVE);
 sample();assert(!state.status_ok&&!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 all_fault=false;fail_address=0;raw=8;sample();assert(state.flags&PMU_STATUS_CHARGING_ACTIVE);
 raw=0;sample();assert(!(state.flags&PMU_STATUS_CHARGING_ACTIVE));
 metalio_volume_sample(0xffff,0);metalio_volume_sample(0xfeff,10);metalio_volume_sample(0xfeff,34);assert(!metalio_take_volume());
 metalio_volume_sample(0xfeff,35);assert(metalio_take_volume()==-1&&!metalio_take_volume());
 metalio_volume_sample(0xffff,50);metalio_volume_sample(0xffff,75);
 metalio_volume_sample(0xfdff,80);metalio_volume_sample(0xfdff,105);assert(metalio_take_volume()==1);
 for(int i=0;i<20;i++){
  metalio_volume_sample(0xffff,200+i*100);metalio_volume_sample(0xffff,225+i*100);
  metalio_volume_sample(0xfdff,250+i*100);metalio_volume_sample(0xfdff,275+i*100);
 }
 assert(volume_count==8);metalio_volume_clear();assert(!volume_count&&!metalio_take_volume());
 puts("PASS: actual S31 battery cadence and all charger stages plus valid SOC, adapter delegation with 30s retry, debounced/bounded volume queue and sleep drain");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(unit)
    base=[CC,'-std=c11','-Wall','-Wextra','-Werror','-g']
    includes=['-I'+str(root/'components/read_pico_pmu/include')]
    try:
        subprocess.run(base+['-fsanitize=address,undefined']+includes+[str(p/'test.c'),'-o',str(p/'test')],check=True,capture_output=True)
    except subprocess.CalledProcessError:
        # 本机 MinGW 可能没有 libasan/libubsan；回退到不带 sanitizer 的构建。
        # The local MinGW may lack libasan/libubsan; retry without sanitizers.
        print('note: -fsanitize=address,undefined unavailable, building without it')
        subprocess.run(base+includes+[str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

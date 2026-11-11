#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""S31实际电量算法边界与官方固定源码逐样本对照。/ Actual policy boundaries and optional pinned-vendor oracle."""
from pathlib import Path
import argparse,hashlib,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--vendor',type=Path);args=parser.parse_args()
UNIT=r'''
#include "metalio_soc_filter.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static metalio_soc_filter_t f;static int level;static bool charging;static int64_t now;
static bool sample(bool valid,int mv,int ma){now+=1000000;return metalio_soc_filter_update(&f,valid,mv,ma,now,&level,&charging);}
static void reset(void){f=(metalio_soc_filter_t){0};now=0;metalio_soc_filter_reset(&f,now);}
int main(void){
 const int points[]={3300,3481,3565,3624,3663,3702,3757,3828,3919,4021,4211};
 for(int i=0;i<11;++i)assert(fabsf(metalio_soc_voltage_percent(points[i],false)-i*10)<0.001f);
 assert(fabsf(metalio_soc_voltage_percent(4000,true)-70)<0.001f);
 reset();assert(!sample(true,3702,-100)&&!charging);assert(!sample(true,3702,-100));assert(sample(true,3702,-100)&&level==50&&!charging);
 for(int i=0;i<150;++i){assert(sample(true,3300,-100)&&level>=0&&level%5==0);}
 assert(level==25); // one downward5% step per30s, never a sudden voltage jump.
 assert(sample(true,4300,100)&&level==25); // direction change restarts the step timer and re-primes the curve.
 for(int i=0;i<29;++i)assert(sample(true,4300,100)&&level==25);
 assert(sample(true,4300,100)&&level==30);
 reset();for(int i=0;i<33;++i)assert(sample(true,4300,100)==(i>=2));assert(level==100&&!charging&&f.full_confirmed);
 metalio_soc_filter_reset(&f,now);assert(!sample(true,4280,0)&&!sample(true,4280,0));assert(sample(true,4280,0)&&level==100&&!charging);
 assert(sample(true,4279,-100)&&level==100&&!f.full_confirmed);
 for(int i=0;i<29;++i)assert(sample(true,4279,-100)&&level==100);
 assert(sample(true,4279,-100)&&level==95);
 reset();for(int i=0;i<15;++i)(void)sample(true,4400,100);assert(level==95);
 assert(!sample(false,4400,100)&&!charging);for(int i=0;i<30;++i)assert(sample(true,4400,100)&&level==95);
 assert(sample(true,4400,100)&&level==100&&!charging);
 reset();for(int i=0;i<15;++i)(void)sample(true,4400,100);now+=5000000;
 for(int i=0;i<30;++i)assert(sample(true,4400,100)&&level==95);assert(sample(true,4400,100)&&level==100);
 assert(!sample(true,2499,100)&&!sample(true,5001,100));
 reset();now=3000000;assert(metalio_soc_filter_update(&f,true,3702,-100,now,&level,&charging));
 unsigned index=f.next;for(int i=0;i<100;++i)assert(metalio_soc_filter_update(&f,true,4000,-100,now,&level,&charging));assert(f.next==index);
 metalio_soc_filter_reset(&f,now);assert(!sample(true,3300,-100)&&!sample(true,3300,-100));assert(sample(true,3300,-100)&&level==0);
 puts("PASS: S31 measured curve,3s unknown,60 samples/1Hz,5%/30s direction/timer guards,4300mV30s full,4280 hysteresis,wake recalibration and invalid/gap handling");
}
'''
ORACLE=r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include "bq27220_gauge.h"
#include "metalio_soc_filter.h"
static int64_t now;static uint16_t mv=3702;static int16_t ma=-100;static bool fault;
int64_t esp_timer_get_time(){return now;}
void vTaskDelay(int ticks){now+=int64_t(ticks)*1000;}
int power_i2c_probe(const power_i2c_t*,uint8_t,int){return 0;}
int power_i2c_write_read(const power_i2c_t*,uint8_t a,uint8_t reg,uint8_t*out,size_t n,int){assert(a==0x55&&n==2&&(reg==8||reg==12));if(fault)return -1;uint16_t v=reg==8?mv:static_cast<uint16_t>(ma);out[0]=v;out[1]=v>>8;return 0;}
int main(){
 auto& vendor=Bq27220Gauge::GetInstance();power_i2c_t bus{};assert(vendor.Begin(&bus));
 for(unsigned v=0;v<=6000;++v)for(int c=0;c<2;++c)assert(std::fabs(vendor.VoltageToPercent(v,c)-metalio_soc_voltage_percent(v,c))<0.0001f);
 metalio_soc_filter_t f{};metalio_soc_filter_reset(&f,now);unsigned compared=0;
 for(int phase=0;phase<9;++phase){
  for(int i=0;i<180;++i){
   now+=1000000;fault=false;
   switch(phase){case 0:mv=3300+i*5;ma=-100;break;case 1:mv=3900+i;ma=100;break;case 2:mv=4300;ma=100;break;case 3:mv=4280;ma=0;break;case 4:mv=4279;ma=-100;break;case 5:mv=3702;ma=-5;break;case 6:mv=4400;ma=6;fault=i==14;break;case 7:mv=4400;ma=0;if(i==15)now+=5000000;break;default:mv=3300;ma=-100;break;}
   if(i==60||i==120){vendor.ResetFilter();metalio_soc_filter_reset(&f,now);}
   int a=-1,b=-1;bool ac=false,ad=false,bc=false;
   bool av=vendor.GetBatteryLevel(a,ac,ad),bv=metalio_soc_filter_update(&f,!fault,mv,ma,now,&b,&bc);
   assert(av==bv);if(av){assert(a==b&&ac==bc);++compared;}
   // Frequent UI reads must not weight the window or accelerate steps.
   for(int repeat=0;repeat<4;++repeat){av=vendor.GetBatteryLevel(a,ac,ad);bv=metalio_soc_filter_update(&f,!fault,mv,ma,now,&b,&bc);assert(av==bv);if(av)assert(a==b&&ac==bc);}
  }
 }
 std::printf("PASS: official690a023 C++ oracle:12002 curve points and%u sequenced samples match C policy, repeated reads/wakes/full/faults/charge switches\n",compared);
}
'''
with tempfile.TemporaryDirectory(prefix='s31-soc-') as folder:
 work=Path(folder);(work/'unit.c').write_text(UNIT);src=ROOT/'components/read_pico_pmu/metalio_soc_filter.c';inc=ROOT/'components/read_pico_pmu/include'
 flags=['-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-g','-I'+str(inc)]
 subprocess.run(['cc','-std=c11',*flags,str(src),str(work/'unit.c'),'-lm','-o',str(work/'unit')],check=True);subprocess.run([str(work/'unit')],check=True)
 if args.vendor:
  vendor=args.vendor
  for name,blob in [('bq27220_gauge.h','b069caebd1e134182ec07cdc008d22ef3b631425'),('bq27220_gauge.cc','2809c990a9084cf1963fd39e2c5693cc8bd2c33f')]:
   data=(vendor/name).read_bytes();assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==blob
  (work/'oracle.cc').write_text(ORACLE)
  (work/'power_i2c.h').write_text('#pragma once\n#include <cstdint>\n#include <cstddef>\n#define ESP_OK 0\nusing esp_err_t=int;struct power_i2c_t{int dummy;};\nint power_i2c_probe(const power_i2c_t*,uint8_t,int);\nint power_i2c_write_read(const power_i2c_t*,uint8_t,uint8_t,uint8_t*,size_t,int);\n')
  (work/'esp_log.h').write_text('#pragma once\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n')
  (work/'esp_timer.h').write_text('#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n')
  (work/'freertos').mkdir();(work/'freertos/FreeRTOS.h').write_text('#pragma once\n#define pdMS_TO_TICKS(ms) (ms)\n');(work/'freertos/task.h').write_text('#pragma once\nvoid vTaskDelay(int);\n')
  # Relative include resolves its adjacent vendor hardware header only if one is present; the pinned snapshot contains gauge files only.
  subprocess.run(['cc','-std=c11',*flags,'-c',str(src),'-o',str(work/'filter.o')],check=True)
  subprocess.run(['c++','-std=c++17',*flags,'-I'+str(work),'-I'+str(vendor),str(vendor/'bq27220_gauge.cc'),str(work/'oracle.cc'),str(work/'filter.o'),'-o',str(work/'oracle')],check=True);subprocess.run([str(work/'oracle')],check=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Exercise actual early-key wire code, one-use evidence and cold-boot timing."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
hooks=root/'boards/metalio47/src/bootloader_components/s31_power_key/hooks.c'
hardware=root/'components/metalio_hw/metalio_hw.c'
unit=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "metalio_early_key_record.h"
#define SDA 0
#define SCL 1
#define PIN_FUNC_GPIO 1
#define SIG_GPIO_OUT_IDX 256
#define CONFIG_METALIO_EARLY_POWER_KEY 1
#define ESP_OK 0
#define ESP_IMAGE_MAX_SEGMENTS 16
#define SOC_RTC_DRAM_LOW 0x2e000000u
#define SOC_RTC_DRAM_HIGH 0x2e008000u
#define APP_KEY_ADDRESS 0x2e000060u
#define ESP_LOGI(...) ((void)0)
static int GPIO;
static unsigned now_us,raw_reason,reads,outputs,disabled,touched,handoff_writes,load_calls,flash_reads,panel_low,panel_output,panel_released;
static bool level[2],clock_stuck,data_stuck,early_boot_power_key;
static int bits[256];static unsigned bits_count,bits_at;
static uint8_t frames[24][4];static unsigned frame_count,byte_count,bit_count,current_byte;
static metalio_early_key_record_t captured={.magic=METALIO_EARLY_KEY_MAGIC};
typedef int esp_err_t;
typedef struct {uint8_t bytes[256];} esp_app_desc_t;
typedef struct {uint32_t offset,size;} esp_partition_pos_t;
typedef struct {
 uint32_t start_addr;
 struct {unsigned chip_id,segment_count;} image;
 struct {uint32_t load_addr,data_len;} segments[16];
 uint32_t segment_data[16];
} esp_image_metadata_t;
static metalio_early_key_record_t s_boot_key_handoff;
static metalio_early_key_app_desc_t image_desc;
static esp_image_metadata_t metadata;
static esp_partition_pos_t partition;
static int load_error,flash_error;
static void *tracked_memcpy(void *dest,const void *src,size_t size){
 uintptr_t address=(uintptr_t)dest;
 if(address>=SOC_RTC_DRAM_LOW&&address<SOC_RTC_DRAM_HIGH){
  assert(address==APP_KEY_ADDRESS&&size==32);++handoff_writes;
  return memcpy(&s_boot_key_handoff,src,size);
 }
 return memcpy(dest,src,size);
}
#define memcpy tracked_memcpy
static esp_err_t __real_bootloader_load_image(const esp_partition_pos_t *part,esp_image_metadata_t *data){
 assert(part==&partition&&data==&metadata);++load_calls;return load_error;
}
static int bootloader_flash_read(unsigned address,void *dest,unsigned size,bool decrypt){
 assert(address==partition.offset+32+256&&size==24&&decrypt);++flash_reads;
 if(flash_error)return flash_error;
 memcpy(dest,&image_desc,size);return ESP_OK;
}
static void esp_rom_delay_us(unsigned us){now_us+=us;assert(now_us<100000);}
static unsigned esp_rom_get_reset_reason(int cpu){assert(!cpu);return raw_reason;}
static unsigned esp_reset_reason(void){return raw_reason;}
static void gpio_ll_set_level(void *hw,unsigned pin,unsigned value){
 assert(hw==&GPIO&&(pin<=1||pin==18||pin==19));if(pin>=18){assert(!value);panel_low|=1u<<pin;return;}touched|=1u<<pin;
 if(pin==SDA && level[SCL] && !clock_stuck && level[SDA]!=(bool)value){
  if(!value){assert(frame_count<24);++frame_count;byte_count=bit_count=current_byte=0;}
  else bit_count=current_byte=0;
 }
 if(pin==SCL && value && !level[SCL] && frame_count && !clock_stuck){
  current_byte=(current_byte<<1)|level[SDA];
  if(++bit_count==9){
   assert(byte_count<4);frames[frame_count-1][byte_count++]=(uint8_t)(current_byte>>1);
   bit_count=current_byte=0;
  }
 }
 level[pin]=value!=0;
}
static int gpio_ll_get_level(void *hw,unsigned pin){
 assert(hw==&GPIO&&pin<=1);
 if(pin==SCL)return !clock_stuck && level[SCL];
 if(data_stuck)return 0;
 assert(bits_at<bits_count);return bits[bits_at++];
}
static void gpio_ll_func_sel(void *hw,unsigned p,unsigned f){assert(hw==&GPIO&&(p<=1||p==18||p==19)&&f==1);}
static void esp_rom_gpio_connect_out_signal(unsigned p,int s,bool i,bool o){assert((p<=1||p==18||p==19)&&s==256&&!i&&!o);}
static void gpio_ll_od_enable(void *hw,unsigned p){assert(hw==&GPIO&&p<=1);}
static void gpio_ll_input_enable(void *hw,unsigned p){assert(hw==&GPIO&&p<=1);}
static void gpio_ll_pullup_en(void *hw,unsigned p){assert(hw==&GPIO&&p<=1);}
static void gpio_ll_pulldown_dis(void *hw,unsigned p){assert(hw==&GPIO&&p<=1);}
static void gpio_ll_output_enable(void *hw,unsigned p){assert(hw==&GPIO&&(p<=1||p==18||p==19));if(p>=18){assert(panel_low&(1u<<p));panel_output|=1u<<p;return;}outputs|=1u<<p;}
static void gpio_ll_hold_dis(void *hw,unsigned p){assert(hw==&GPIO&&(p<=1||p==18||p==19));if(p>=18){assert(panel_output&(1u<<p));panel_released|=1u<<p;}}
static void gpio_ll_output_disable(void *hw,unsigned p){assert(hw==&GPIO&&p<=1);disabled|=1u<<p;}
'''
for name in ('delay_us','set','high','clock_high','start','stop','write_byte','read_register','panel_idle_early','bootloader_before_init','__wrap_bootloader_load_image'):
    unit+=function(name,hooks)+'\n'
unit+=function('metalio_capture_boot_power_key',hardware)+'\n'
unit+=r'''
static void publish(void){assert(__wrap_bootloader_load_image(&partition,&metadata)==load_error);}
static void reset(void){
 now_us=reads=outputs=disabled=touched=handoff_writes=load_calls=flash_reads=bits_count=bits_at=0;
 panel_low=panel_output=panel_released=0;
 raw_reason=1;clock_stuck=data_stuck=early_boot_power_key=false;
 level[0]=level[1]=true;frame_count=byte_count=bit_count=current_byte=0;
 memset(frames,0,sizeof(frames));memset(&s_boot_key_handoff,0xaa,sizeof(s_boot_key_handoff));
 captured=(metalio_early_key_record_t){.magic=METALIO_EARLY_KEY_MAGIC};
 partition=(esp_partition_pos_t){.offset=0x510000,.size=0x500000};
 metadata=(esp_image_metadata_t){.start_addr=0x510000,.image={.chip_id=32,.segment_count=3},
  .segments={{.load_addr=0x40270020,.data_len=0x1000},{.load_addr=0x2e000000,.data_len=4},{.load_addr=0x2e000400,.data_len=36}},
  .segment_data={0x510020}};
 image_desc=(metalio_early_key_app_desc_t){.magic=METALIO_EARLY_KEY_DESC_MAGIC,.version=1,
  .board=METALIO_EARLY_KEY_BOARD,.address=APP_KEY_ADDRESS,.size=32};
 load_error=flash_error=0;

}
static void push_read(uint8_t value,int nack){
 const int prefix[]={1,0,0,1,0};
 for(unsigned i=0;i<5;i++)bits[bits_count++]=prefix[i];
 if(nack>=0)bits[bits_count-5+(unsigned)nack]=1;
 for(unsigned bit=0;bit<8;bit++)bits[bits_count++]=(value>>(7-bit))&1u;
}
static void full_reads(uint8_t first,uint8_t second){
 push_read(0xe3,-1);push_read(0,-1);push_read(first,-1);push_read(second,-1);
}
static bool fake_read(uint8_t reg,uint8_t *value){
 ++reads;
 if(reg==7)*value=0xe3;
 else if(reg==5)*value=0;
 else {assert(reg==1);*value=now_us<100000?0x7f:0xff;}
 return true;
}
int main(void){
 // The old app-time sample misses a 100ms press; the same actual sampler before
 // application loading retains it, with 30ms stable physical evidence.
 reset();metalio_early_key_record_t early=metalio_early_key_sample(1,fake_read,delay_us);
 assert(early.flags==3&&now_us==30000);
 now_us=1300000;metalio_early_key_record_t late=metalio_early_key_sample(1,fake_read,delay_us);
 assert(late.flags==1);
 assert(metalio_early_key_take(&early,1,NULL));assert(!metalio_early_key_take(&early,1,NULL));
 // Real byte-level GPIO wire code uses only pointer writes + repeated-start reads.
 reset();full_reads(0x7f,0x7f);bootloader_before_init();publish();
 assert(captured.flags==3&&now_us>=30000&&bits_at==bits_count);
 assert(panel_low==((1u<<18)|(1u<<19))&&panel_output==panel_low&&panel_released==panel_low);
 assert(touched==3&&outputs==3&&disabled==3&&level[0]&&level[1]);
 assert(handoff_writes==1&&load_calls==1&&flash_reads==1&&frame_count==8);
 const unsigned regs[]={7,5,1,1};
 for(unsigned i=0;i<4;i++){
  assert(frames[2*i][0]==0x40&&frames[2*i][1]==regs[i]);
  assert(frames[2*i+1][0]==0x41);
 }
 metalio_early_key_record_t valid=captured;
 metalio_capture_boot_power_key();assert(early_boot_power_key);
 metalio_capture_boot_power_key();assert(!early_boot_power_key);
 // Single-sample noise and release during debounce never authorize unlock.
 for(unsigned i=0;i<3;i++){
  reset();full_reads(i==1?0xff:0x7f,i==0?0xff:i==1?0x7f:0xff);
  bootloader_before_init();publish();metalio_capture_boot_power_key();
  assert(!early_boot_power_key);
 }
 // Corruption, stale/replayed evidence and every non-POR reset fail closed.
 for(unsigned bit=0;bit<256;bit++){
  metalio_early_key_record_t bad=valid;((uint8_t*)&bad)[bit/8]^=1u<<(bit%8);
  assert(!metalio_early_key_take(&bad,1,NULL));
 }
 for(unsigned reason=0;reason<=32;reason++){
  if(reason==1)continue;
  metalio_early_key_record_t stale=valid;assert(!metalio_early_key_take(&stale,reason,NULL));
  reset();raw_reason=reason;bootloader_before_init();publish();
  assert(!touched&&!outputs&&!disabled&&!bits_at&&!now_us);assert(panel_released==((1u<<18)|(1u<<19)));
  metalio_capture_boot_power_key();assert(!early_boot_power_key);
 }
 // Invalid direction/inversion and each byte NACK cannot fabricate zero-valued input.
 for(unsigned kind=0;kind<5;kind++){
  reset();
  if(kind==0)push_read(0x63,-1);
  else if(kind==1){push_read(0xe3,-1);push_read(0x80,-1);}
  else push_read(0xe3,kind==2?1:kind==3?2:4);
  bootloader_before_init();publish();metalio_capture_boot_power_key();
  assert(!early_boot_power_key&&disabled==3&&level[0]&&level[1]);
 }
 for(unsigned fault=0;fault<2;fault++){
  reset();clock_stuck=fault==0;data_stuck=fault==1;
  bootloader_before_init();publish();metalio_capture_boot_power_key();
  assert(!early_boot_power_key&&disabled==3&&now_us<10000);
 }

 // Failed/legacy/foreign images and loaded-code or SDK-reserved addresses receive no handoff.
 for(unsigned defect=0;defect<12;defect++){
  reset();full_reads(0x7f,0x7f);bootloader_before_init();
  if(defect==0)load_error=-1;
  else if(defect==1)image_desc.magic=0;
  else if(defect==2)image_desc.board^=1;
  else if(defect==3)image_desc.address=0x2e000400;
  else if(defect==4)image_desc.address=SOC_RTC_DRAM_HIGH-32;
  else if(defect==5)image_desc.address=0;
  else if(defect==6)image_desc.address++;
  else if(defect==7)metadata.image.chip_id=9;
  else if(defect==8)metadata.image.segment_count=17;
  else if(defect==9)metadata.segments[0].data_len=256;
  else if(defect==10)flash_error=-1;
  else image_desc.size=64;
  publish();assert(load_calls==1&&!handoff_writes);
  metalio_capture_boot_power_key();assert(!early_boot_power_key);
 }
 puts("PASS: actual early wire reads + 30ms P17 evidence, one-use app consumption, late-short-press reproduction, 256 corruptions, all non-POR resets, invalid/legacy/failed images, loaded-code/SDK-reserved addresses, input/invert/NACK/stuck-bus failures; no shared-IRQ or other-pin admission");
}
'''
# fake_read's old-time sample intentionally starts after normal startup.
unit=unit.replace('now_us+=us;assert(now_us<100000);','now_us+=us;')
with tempfile.TemporaryDirectory(prefix='s31-early-key-') as d:
    p=Path(d);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I',str(root/'components/metalio_hw/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)


#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Execute the S31 row-layout/start and full physical wake-target boundaries."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1];lcd=R/'boards/metalio47/src/components/epdiy/src/output_lcd/lcd_driver.c';sleep=R/'boards/metalio47/src/main/sleep.c'
if not lcd.exists():lcd=R/'components/epdiy/src/output_lcd/lcd_driver.c';sleep=R/'main/sleep.c'
text=lcd.read_text();start=text.index('    gpio_set_level(lcd.config.bus.stv, 0);',text.index('void IRAM_ATTR epd_lcd_start_frame'))
end=text.index('    // start LCD engine',start)
startup=text[start:end]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "output_common/white_border.h"
#define IRAM_ATTR
#define ESP_LOGI(...) ((void)0)
#define EPD_LCD_PROBE_D0_TOGGLE 0
#define BOUNCE_BUF_LINES 8
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 1
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 2
typedef struct {int bus_width,white_border_pixels,ckv_high_time;struct{int stv;}bus;}LcdEpdConfig_t;
static struct {LcdEpdConfig_t config;int native_height,display_lines,line_bytes,dummy_bytes,lcd_res_h,bb_size,line_length_us,ckv_period_us;size_t lines_fed;bool bb_behind_cache;
 bool(*line_source_cb)(void*,uint8_t*);void*line_cb_payload;}lcd;
static bool border_whitening;static unsigned source_row,checks;
static void check_cache_configuration(void){++checks;}
static __attribute__((unused)) void esp_cache_msync(uint8_t*p,size_t n,int flags){(void)p;(void)n;assert(flags==3);}
static bool line_source(void*unused,uint8_t*out){(void)unused;for(unsigned x=0;x<304;++x)out[x]=source_row<684?(uint8_t)(x^(source_row*19)):0;++source_row;return false;}
static unsigned actions[8],action_count;
static void gpio_set_level(int pin,int level){assert(pin==42);actions[action_count++]=1000+level;}
static void esp_rom_delay_us(int us){actions[action_count++]=(unsigned)us;}
static void start_ckv_cycles(int cycles){assert(cycles==13);actions[action_count++]=2000;}
typedef struct{uint8_t *front_fb,*back_fb;}EpdiyHighlevelState;
static unsigned physical_w=1216,physical_h=684;
static int epd_width(void){return (int)physical_w;}static int epd_height(void){return (int)physical_h;}
static uint8_t*epd_hl_get_framebuffer(EpdiyHighlevelState*h){return h->front_fb;}
'''
unit+=function('assign_lcd_parameters_from_config',lcd)+'\n'+function('fill_bounce_buffer',lcd)+'\n'
unit+='static void start_phase(void){int initial_lines=8;\n'+startup+'}\n'
unit+=function('lock_clear_wake_target',sleep)+'\n'
unit+=r'''
int main(void){LcdEpdConfig_t config={.bus_width=8,.white_border_pixels=0,.ckv_high_time=159,.bus.stv=42};
 assign_lcd_parameters_from_config(&config,1216,684);assert(checks==1&&lcd.line_bytes==304&&lcd.display_lines==688);
#if CONFIG_IDF_TARGET_ESP32S31
 assert(lcd.dummy_bytes==0&&lcd.bb_size==2432);
#else
 assert(lcd.dummy_bytes==1&&lcd.bb_size==2440);
#endif
 uint8_t *buffer=malloc((size_t)lcd.bb_size+2);assert(buffer);lcd.line_source_cb=line_source;
 for(unsigned batch=0;batch<87;++batch){memset(buffer,0xa5,(size_t)lcd.bb_size+2);fill_bounce_buffer(buffer+1);
  assert(buffer[0]==0xa5&&buffer[lcd.bb_size+1]==0xa5);
  for(unsigned y=0;y<8;++y){unsigned row=batch*8+y;size_t offset=y*(304+lcd.dummy_bytes)+(lcd.dummy_bytes%2)+1;
   for(unsigned x=0;x<304;++x)assert(buffer[offset+x]==(row<684?(uint8_t)(x^(row*19)):0));}
 }
 assert(source_row==688);free(buffer);lcd.ckv_period_us=17;start_phase();
#if CONFIG_IDF_TARGET_ESP32S31
 unsigned expected[]={1000,2000,17,1001,15};
#else
 unsigned expected[]={1000,2000,17,1001,17,15};
#endif
 assert(action_count==sizeof(expected)/sizeof(expected[0])&&!memcmp(actions,expected,sizeof(expected)));
 for(unsigned rotation=0;rotation<2;++rotation){if(rotation){physical_w=684;physical_h=1216;}size_t n=(size_t)physical_w/2*physical_h;
  uint8_t *front=malloc(n+2),*back=malloc(n);assert(front&&back);memset(front,0x12,n+2);memset(back,0x57,n);
  EpdiyHighlevelState state={front+1,back};lock_clear_wake_target(&state);
  assert(front[0]==0x12&&front[n+1]==0x12);for(size_t i=0;i<n;++i)assert(front[i+1]==255&&back[i]==0x57);
  free(front);free(back);
 }
 puts("PASS: real row configuration/feed keeps first/last pixels and all688 queued rows, bounded DMA buffers, S31 phase start alignment, physical wake edges cleared with back history preserved");}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 for target in [0,1]:
  flags=['-DCONFIG_IDF_TARGET_ESP32S31=1'] if target else []
  # The vendor branch tests use defined(), so leave the macro absent on the legacy target.
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'components/epdiy/src'),*flags,str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

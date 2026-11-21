#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check actual panel pin ordering before S31 rail setup and LCD restoration."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1];source=R/'boards/metalio47/src/components/read_pico/read_pico_board.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define GPIO_MODE_OUTPUT 1
#define READ_PICO_EPD_SCAN_FULL 2
typedef int esp_err_t;
typedef struct {int data[8],clock,ckv,leh,start_pulse,stv;}lcd_bus_config_t;
typedef struct {int marker;}read_pico_epd_scan_t;
static bool latch[64],direction[64],released[64];static unsigned rails,lcd;
static bool panel_pin(int p){return p==18||p==19||p==35||p==37;}
static void gpio_set_level(int p,int value){assert(panel_pin(p)&&!value);latch[p]=false;}
static void gpio_set_direction(int p,int value){assert(panel_pin(p)&&value==1&&!latch[p]);direction[p]=true;}
static void gpio_hold_dis(int p){assert(panel_pin(p)&&!latch[p]&&direction[p]);released[p]=true;}
static void check_idle(void){int pins[]={18,19,35,37};for(unsigned i=0;i<4;++i)assert(!latch[pins[i]]&&direction[pins[i]]&&released[pins[i]]);}
static int metalio_hw_init(void){check_idle();++rails;return ESP_OK;}
static void read_pico_epd_scan(int mhz,int mode,read_pico_epd_scan_t *s){assert(mhz==20&&mode==2);s->marker=9;}
static void read_pico_epd_init_lcd(const lcd_bus_config_t *b,int bits,int w,int h,const read_pico_epd_scan_t *s){
 check_idle();assert(b->stv==35&&b->ckv==40&&b->clock==16&&b->leh==17&&b->start_pulse==4&&bits==8&&w==1216&&h==684&&s->marker==9);++lcd;}
'''
for n in ['panel_control_idle','read_pico_epd_restore_lcd','board_init']:unit+=function(n,source)+'\n'
unit+=r'''
int main(void){for(int p=0;p<64;++p)latch[p]=true;board_init(1216);assert(rails==1&&lcd==1);
 for(int p=0;p<64;++p){latch[p]=true;direction[p]=released[p]=false;}read_pico_epd_restore_lcd();assert(rails==1&&lcd==2);
 puts("PASS: inactive XOE/MODE/STV/control latches and directions precede hold release, rail setup and LCD restore; original bus/timing retained");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(unit);subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)

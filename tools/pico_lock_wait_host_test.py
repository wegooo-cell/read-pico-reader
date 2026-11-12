#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""Actual lock wait: busy decoder, stuck IRQ and rejected sleep must never synthesize unlock."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef void *sc7a20h_handle_t;typedef int esp_err_t;typedef int gpio_num_t;typedef int app_wake_source_t;
typedef struct {int ths_mg,duration;}sc7a20h_motion_cfg_t;typedef int sc7a20h_events_t;
#define SC7A20H_MOTION_DEFAULT() ((sc7a20h_motion_cfg_t){0})
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define APP_LOCK_STUCK_IRQ_US 3000000
#define APP_LOCK_KEY_POLL_US 100000
#define APP_LOCK_SLEEP_ERROR_LIMIT 5
#define APP_LOCK_EPD_RETRY_US 1000000LL
#define READ_PICO_IOE_INT_GPIO 7
#define ESP_SLEEP_WAKEUP_TIMER 1
#define pdMS_TO_TICKS(x) (x)
enum {APP_WAKE_NONE,APP_WAKE_KEY,APP_WAKE_PICKUP,APP_WAKE_TIMEOUT};
static int64_t now,key_at;static int fault,suspended,resumed,last_wake,sleeps,epd_retries,epd_parks;
static int64_t last_epd_retry;
static bool book_cover_jobs_quiesce(unsigned ms){assert(ms==2000);return fault!=1;}
static bool ui_avatar_quiesce(unsigned ms){assert(ms==2000);return fault!=2;}
static bool app_settings_pickup_wake(void){return false;}
static int64_t esp_timer_get_time(void){return now;}
static void periph_suspend(bool off){assert(!off);++suspended;}
static void periph_resume(void){++resumed;}
static void lock_arm_ioe_wakeup(void){}
static void sc7a20h_arm_pickup_wake(void *h,const sc7a20h_motion_cfg_t *m){(void)h;(void)m;}
static void pickup_ack(void *h){(void)h;}
static bool pickup_wait_quiet(void *h,int a,int b){(void)h;(void)a;(void)b;return true;}
static void sc7a20h_config_light_sleep_wakeup(void *h){(void)h;}
static int sc7a20h_int1_gpio(void *h){(void)h;return 8;}
static int sc7a20h_int1_level(void *h){(void)h;return 0;}
static bool pickup_ia(void *h){(void)h;return false;}
static void gpio_wakeup_disable(int gpio){(void)gpio;}
static bool read_pico_pmu_take_key_wakeup(void){return key_at&&now>=key_at;}
static bool lock_wait_key(void){return read_pico_pmu_take_key_wakeup();}
static struct {int64_t pending_at,touch_until;} s_lock_hint;
static void lock_hint_finish(bool restore){(void)restore;}
static void read_pico_clear_ioe_int(void){}
static int gpio_get_level(int gpio){(void)gpio;return fault==3?0:1;}
static void vTaskDelay(int ms){now+=(int64_t)ms*1000;assert(now<12000000);}
static int esp_sleep_enable_timer_wakeup(uint64_t us){(void)us;return fault==4?-1:0;}
static esp_err_t read_pico_epd_verify_poweroff(void){
 return fault==7||(fault==6&&epd_retries<2)?-1:ESP_OK;
}
static void epd_poweroff(void){
 assert(!epd_retries||now-last_epd_retry>=APP_LOCK_EPD_RETRY_US);
 last_epd_retry=now;
 ++epd_retries;
}
static esp_err_t read_pico_ioe_sleep(bool off){assert(!off);++epd_parks;return ESP_OK;}
static int esp_light_sleep_start(void){assert(fault!=7&&(fault!=6||epd_retries==2));++sleeps;now+=100000;return fault==5?-1:0;}
static void esp_sleep_disable_wakeup_source(int source){(void)source;}
static void sc7a20h_read_events(void *h,sc7a20h_events_t *e){(void)h;(void)e;}
static void sc7a20h_power_down(void *h){(void)h;}
static void sc7a20h_int1_begin(void *h){(void)h;}
static void app_settings_set_last_wake(uint8_t wake){last_wake=wake;}
'''
unit+=function('app_light_sleep_wait_timed',ROOT/'boards/pico/src/main/sleep.c')+'\n'
unit+=r'''
int main(void){
 for(fault=0;fault<=7;++fault)for(unsigned real=0;real<2;++real){
  now=0;key_at=real?5000000:0;suspended=resumed=sleeps=epd_retries=epd_parks=0;last_wake=-1;
  assert(app_light_sleep_wait_timed(NULL,8000)==(real?APP_WAKE_KEY:APP_WAKE_TIMEOUT));
  assert(now>=5000000&&suspended==1&&resumed==1&&last_wake==(real?APP_WAKE_KEY:APP_WAKE_NONE));
  if(fault==1||fault==2||fault==7)assert(!sleeps);
  if(fault==6)assert(epd_retries==2&&epd_parks==2&&sleeps>0);
  if(fault==7)assert(epd_retries>=5&&epd_parks==epd_retries);
 }
 puts("PASS: real lock wait retains lock through busy image/font jobs, stuck IRQ, timer, panel-off and light-sleep failures; only validated key wakes or actual timeout exits");
}
'''
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-but-set-variable','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

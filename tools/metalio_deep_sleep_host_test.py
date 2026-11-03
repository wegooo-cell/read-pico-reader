"""Run production S31 sleep admission/masks and lock timeout continuation with fault injection."""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#define ESP_OK 0
static bool book_cover_jobs_quiesce(unsigned ms){assert(ms==2000);return true;}
static bool ui_avatar_quiesce(unsigned ms){assert(ms==2000);return true;}
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_NOT_FINISHED -4
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_TIMEOUT -2
#define ESP_SLEEP_WAKEUP_ALL 0
#define ESP_EXT1_WAKEUP_ANY_LOW 1
#define READ_PICO_IOE_INT_GPIO 2
#define APP_LOCK_IDLE_TIMEOUT_MS 0
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;typedef int app_sleep_mode_t;typedef int app_wake_source_t;typedef void *sc7a20h_handle_t;
enum {APP_SLEEP_DEEP,APP_SLEEP_OFF};
enum {APP_WAKE_NONE,APP_WAKE_KEY,APP_WAKE_PICKUP,APP_WAKE_TIMEOUT};
typedef struct {char path[288];bool reading;} app_lock_font_t;
static int ioe=1,mutex,io_calls,io_fail_at,io_taken,delay_ms,irq_low,key_low;
static uint8_t outputs[2]={0x20,0x14},latches[2],config[2];
static bool paused,usb,transfer_busy,ble_failed,deep_success;
static void *s_lock_sleep_touch;
static bool s_lock_io_pending,s_lock_io_no_media,deep_pins,deep_rails,deep_touch;
static int hardware_error;static bool late_usb;
#define CST836U_MODE_DEEPSLEEP 3
static unsigned network_resumes;
static int font_closed,sd_closed,sd_error,restored,hold_calls,lcd_deinit,wake_source,sleep_calls;
static unsigned waits,wait_timeouts;
static jmp_buf deep;
static int xSemaphoreTake(int m,int timeout){(void)m;assert(timeout==100);assert(!io_taken);io_taken=1;return pdTRUE;}
static void xSemaphoreGive(int m){(void)m;assert(io_taken);io_taken=0;}
static int fail_io(void){assert(io_taken);return ++io_calls==io_fail_at?ESP_ERR_TIMEOUT:ESP_OK;}
static int fca9555_set_output(int dev,unsigned port,uint8_t value){assert(dev==ioe&&port<2);int err=fail_io();if(!err)latches[port]=value;return err;}
static int fca9555_set_config(int dev,unsigned port,uint8_t value){assert(dev==ioe&&port<2);int err=fail_io();if(!err)config[port]=value;return err;}
static int fca9555_read_input(int dev,unsigned port,uint8_t *out){assert(dev==ioe&&port<2);int err=fail_io();if(!err)*out=port==1&&key_low?0x7f:0xff;return err;}
static int read_pico_pmu_keys_pause(void){if(paused)return -1;paused=true;return ESP_OK;}
static void read_pico_pmu_keys_resume(void){assert(paused);paused=false;}
static void vTaskDelay(int ms){delay_ms+=ms;assert(delay_ms<5000);}
static int read_pico_ioe_int_level(void){return !irq_low;}
static int esp_sleep_disable_wakeup_source(int source){assert(source==ESP_SLEEP_WAKEUP_ALL);wake_source=0;return ESP_OK;}
static int esp_sleep_enable_ext1_wakeup_io(uint64_t mask,int mode){assert(mask==4&&mode==ESP_EXT1_WAKEUP_ANY_LOW);wake_source=1;return ESP_OK;}
static bool metalio_usb_blocks_sleep(void){return usb||(late_usb&&sd_closed);}
static bool read_pico_transfer_pause_for_sleep(void){return !transfer_busy;}
static void read_pico_transfer_resume_after_sleep(void){++network_resumes;}
static int ble_pt_suspend_for_sleep(void){return ble_failed?-1:ESP_OK;}
static int app_sync_time_to_pmu(void){return ESP_OK;}
static void app_font_begin_lock(app_lock_font_t *font){assert(!font_closed);font->reading=true;}
static void app_font_end_lock(const app_lock_font_t *font){assert(font->reading);font_closed=0;restored++;}
static int ttf_font_suspend_io(void){return 0;}
typedef struct{bool mounted;}read_pico_sd_info_t;
static int read_pico_sd_get_info(read_pico_sd_info_t *i){i->mounted=!sd_closed;return 0;}
static int book_suspend_io(void){return 0;}
static void metalio_haptic_stop(void){}
static int lock_resume_media_io(unsigned wait){assert(wait==15000||wait==500);s_lock_io_pending=false;return 0;}
static int cst836u_set_mode(void *t,int mode){assert(t&&mode==3);if(hardware_error==1)return -3;deep_touch=true;return 0;}
static int cst836u_wake(void *t){assert(t);deep_touch=false;return 0;}
static int metalio_prepare_light_sleep_pins(void){deep_pins=true;return hardware_error==2?-3:0;}
static void metalio_release_light_sleep_pins(void){deep_pins=false;}
static int metalio_sleep_cut_rails(uint16_t *saved){*saved=123;deep_rails=true;return hardware_error==3?-3:0;}
static int metalio_sleep_restore_rails(uint16_t saved){assert(saved==123);deep_rails=false;return 0;}
static void read_pico_epd_restore_lcd(void){lcd_deinit=0;}
static int ttf_font_open_builtin(void){font_closed++;return ESP_OK;}
static int read_pico_sd_sync(void){if(!sd_error)sd_closed++;return sd_error;}
static void metalio_frontlight(unsigned b,unsigned w){assert(!b&&w==50);}
static void epd_poweroff(void){}
static unsigned off_calls, resumes, ble_resumes;
static bool online;
static bool pico_online_busy(void){return online;}
static void ble_pt_resume_after_sleep(void){++ble_resumes;}
static int read_pico_sd_remount(void){sd_closed=0;return ESP_OK;}
static void metalio_haptic_resume(void){++resumes;}
static void metalio_poweroff(void){++off_calls;}
static void epd_lcd_deinit(void){++lcd_deinit;}
static void metalio_hold_for_deep_sleep(void){++hold_calls;}
// 充电监控的挂起/恢复由充电驱动自己验证；这里只要求回滚路径成对调用。
// The charge driver owns suspend/resume; the rollback paths only require a paired call.
static void metalio_charger_suspend(void){}
static void metalio_charger_resume(void){}
static void esp_deep_sleep_start(void){++sleep_calls;assert(wake_source&&paused&&font_closed&&sd_closed&&hold_calls&&lcd_deinit&&deep_pins&&deep_rails);deep_success=true;longjmp(deep,1);}
static int app_light_sleep_wait_timed(void *acc,unsigned ms){assert(!acc&&ms==0);return ++waits<=wait_timeouts?APP_WAKE_TIMEOUT:APP_WAKE_KEY;}
'''
for name in ('metalio_io_read','metalio_prepare_key_wake','metalio_restore_key_io'):
    unit+=function(name,root/'components/metalio_hw/metalio_hw.c')+'\n'
for name in ('lock_restore_key_wake','lock_prepare_key_wake','lock_power_off','app_enter_host_sleep','lock_wait_for_unlock'):
    unit+=function(name,root/'main/sleep.c')+'\n'
unit+=r'''
static void reset(void){io_calls=io_fail_at=io_taken=delay_ms=irq_low=key_low=font_closed=sd_closed=sd_error=restored=hold_calls=lcd_deinit=wake_source=sleep_calls=0;paused=usb=transfer_busy=ble_failed=deep_success=false;config[0]=0xc0;config[1]=0xe3;latches[0]=outputs[0];latches[1]=outputs[1];waits=wait_timeouts=0;s_lock_sleep_touch=NULL;s_lock_io_pending=deep_pins=deep_rails=deep_touch=late_usb=false;hardware_error=0;}
static void intact(void){assert(!font_closed&&!sd_closed&&!hold_calls&&!lcd_deinit&&!sleep_calls&&!paused&&!deep_pins&&!deep_rails&&!deep_touch&&!s_lock_io_pending);assert(config[0]==0xc0&&config[1]==0xe3&&latches[0]==0x20&&latches[1]==0x14);}
int main(void){
 reset();assert(app_enter_host_sleep(99)==ESP_ERR_INVALID_ARG);intact();
 reset();online=true;assert(app_enter_host_sleep(APP_SLEEP_DEEP)==ESP_ERR_NOT_FINISHED);intact();online=false;
 reset();assert(app_enter_host_sleep(APP_SLEEP_OFF)==ESP_ERR_TIMEOUT);assert(off_calls==1&&resumes==1&&delay_ms==1000);intact();
 reset();assert(lock_prepare_key_wake());assert(paused&&wake_source&&config[0]==0&&config[1]==0x80&&latches[0]==0x20&&latches[1]==0x97);
 lock_restore_key_wake();intact();
 // Every failed transfer in the reference mask restores normal directions before TF/font closure.
 for(int i=1;i<=9;i++){reset();io_fail_at=i;app_enter_host_sleep(APP_SLEEP_DEEP);intact();}
 reset();irq_low=1;app_enter_host_sleep(APP_SLEEP_DEEP);assert(delay_ms==600);intact();
 reset();key_low=1;app_enter_host_sleep(APP_SLEEP_DEEP);assert(delay_ms==600);intact();
 reset();usb=true;app_enter_host_sleep(APP_SLEEP_DEEP);assert(!io_calls);intact();
 reset();transfer_busy=true;app_enter_host_sleep(APP_SLEEP_DEEP);assert(!io_calls);intact();
 reset();ble_failed=true;app_enter_host_sleep(APP_SLEEP_DEEP);assert(!io_calls);intact();
 reset();sd_error=-1;app_enter_host_sleep(APP_SLEEP_DEEP);assert(restored==1);intact();
 reset();s_lock_sleep_touch=(void*)1;sd_error=-1;
 unsigned keep_ble=ble_resumes,keep_net=network_resumes;
 assert(app_enter_host_sleep(APP_SLEEP_DEEP)==-1);assert(ble_resumes==keep_ble&&network_resumes==keep_net);intact();s_lock_sleep_touch=NULL;
 reset();if(!setjmp(deep)){app_enter_host_sleep(APP_SLEEP_DEEP);assert(0);}assert(deep_success&&sleep_calls==1);
 // Touch, bus and partial-rail failures roll back every changed resource without unlocking.
 for(int e=1;e<=3;e++){
  reset();s_lock_sleep_touch=(void*)1;hardware_error=e;
  assert(app_enter_host_sleep(APP_SLEEP_DEEP)==-3);intact();
 }
 reset();late_usb=true;assert(app_enter_host_sleep(APP_SLEEP_DEEP)==ESP_ERR_INVALID_STATE);intact();
 // A timeout handoff that returns never unlocks; only the third, real key wake exits this loop.
 reset();usb=true;wait_timeouts=2;lock_wait_for_unlock((void*)1);assert(waits==3&&!sleep_calls);intact();
 puts("PASS: production reference TCA mask/P17-only wake, all 9 I2C faults, stuck IRQ/held key/USB/radio deferral before TF closure, SD-failure font restore, deep rail/touch/bus transaction and rollback, late USB deferral, and timeout cannot unlock");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

"""Exercise production S31 light sleep: long timer idle, USB, keys and rollback."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
source = root / 'boards/metalio47/src/main/sleep.c'
if not source.exists():
    source = root / 'main/sleep.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
typedef int esp_err_t;
typedef void *sc7a20h_handle_t;
typedef void *cst836u_handle_t;
typedef struct {bool mounted;} read_pico_sd_info_t;
enum {ESP_OK=0,ESP_ERR_NOT_FINISHED=1,ESP_ERR_INVALID_STATE=-1,ESP_ERR_TIMEOUT=-3,ESP_SLEEP_WAKEUP_EXT1=2,ESP_SLEEP_WAKEUP_TIMER=4};
#define CST836U_MODE_DEEPSLEEP 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(tag,...) do {if(0)printf(__VA_ARGS__);} while(0)
#define ESP_LOGW ESP_LOGI
static const char *esp_err_to_name(int e){(void)e;return "error";}
static bool s_lock_io_pending,s_lock_io_no_media,usb,busy,online,queued,allow_key,keys,rails,pins,lcd,touch,media,usb_at_timer,slow_media,confirmed;
static bool off_success;static unsigned parked_off;
static jmp_buf off_jump;
static unsigned sleeps,stop_after,usb_after,timers,cut,restore,closes,mounts,resume,keys_restored,latches;
static unsigned fail_at,operation;
static int64_t clock_us;
static uint64_t timer_us;
static int64_t esp_timer_get_time(void){return clock_us;}
static unsigned fail(void){return ++operation==fail_at;}
static bool metalio_usb_blocks_sleep(void){return usb||(usb_after&&sleeps>=usb_after);}
static bool usb_storage_active(void){return busy;}
static bool pico_online_busy(void){return online;}
static int read_pico_sd_get_info(read_pico_sd_info_t *i){i->mounted=media;return fail()?-2:ESP_OK;}
static int book_suspend_io(void){return fail()?-2:ESP_OK;}
static int ttf_font_suspend_io(void){return fail()?-2:ESP_OK;}
static int cst836u_set_mode(void *t,int m){assert(t&&m==1);if(fail())return -2;touch=true;return ESP_OK;}
static int read_pico_sd_sync(void){if(fail())return -2;media=false;++closes;return ESP_OK;}
static void metalio_haptic_stop(void){}
static void metalio_haptic_resume(void){}
// 充电监控的睡眠挂起/恢复由充电驱动自己验证；本测试只要求事务期间不被插入。
// The charge driver owns suspend/resume; this test only needs the transaction to stay isolated.
static void metalio_charger_suspend(void){}
static void metalio_charger_resume(void){}
static void metalio_frontlight(unsigned b,unsigned w){assert(!b&&w==50);}
static void epd_poweroff(void){}
static void epd_lcd_deinit(void){lcd=true;}
static int metalio_prepare_light_sleep_pins(void){pins=true;return fail()?-2:ESP_OK;}
static int sc7a20h_shared_irq_idle(void *a){assert(a);return fail()?-2:ESP_OK;}
static bool lock_take_key_wakeup(void){bool v=allow_key&&queued;queued=false;return v;}
static bool lock_prepare_key_wake(void){if(fail())return false;keys=true;return true;}
static int esp_sleep_enable_timer_wakeup(uint64_t us){assert(us>0&&us<=60000000);timer_us=us;if(usb_at_timer)usb=true;return fail()?-2:ESP_OK;}
static int metalio_sleep_cut_rails(uint16_t *s){*s=123;rails=true;++cut;return fail()?-2:ESP_OK;}
static int metalio_hold_for_light_sleep(void){return fail()?-2:ESP_OK;}
static int metalio_light_sleep_start(void){
 assert(pins&&rails&&keys&&touch&&lcd&&!media);assert(++sleeps<=181);
 clock_us+=(int64_t)timer_us;return fail()?-2:ESP_OK;
}
static uint32_t esp_sleep_get_wakeup_causes(void){return 1u<<(sleeps==stop_after?ESP_SLEEP_WAKEUP_EXT1:ESP_SLEEP_WAKEUP_TIMER);}
static int read_pico_ioe_int_level(void){return 1;}
static bool metalio_confirm_power_key(void){assert(keys&&rails&&pins);return confirmed;}
static void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;}
static void metalio_release_light_sleep_pins(void){pins=false;}
static int esp_sleep_disable_wakeup_source(int s){assert(s==ESP_SLEEP_WAKEUP_TIMER);++timers;return ESP_OK;}
static int metalio_sleep_restore_rails(uint16_t s){assert(s==123);rails=false;++restore;return fail()?-2:ESP_OK;}
static void lock_restore_key_wake(void){keys=false;++keys_restored;}
static void read_pico_pmu_latch_power_wakeup(void){assert(!keys);queued=true;++latches;}
static int read_pico_sd_remount(void){assert(!rails&&!pins);++mounts;media=true;return ESP_OK;}
static int lock_resume_media_io(unsigned wait){assert(wait==500&&(!closes||media)&&!lcd&&!touch);++resume;s_lock_io_pending=slow_media;return slow_media?ESP_ERR_NOT_FINISHED:ESP_OK;}
static void read_pico_epd_restore_lcd(void){assert(!rails&&!pins&&(!closes||media));lcd=false;}
static int cst836u_wake(void *t){assert(t);touch=false;return fail()?-2:ESP_OK;}
'''
unit += r'''
static int lock_power_off_parked(void){assert(rails&&pins&&keys&&touch&&lcd&&!media&&closes==1&&!mounts&&!resume);++parked_off;if(off_success)longjmp(off_jump,1);return ESP_ERR_TIMEOUT;}
'''
unit += function('lock_light_sleep_slice_us', source)
unit += function('lock_light_sleep_once', source)
unit += r'''
static void reset(void){
 s_lock_io_no_media=s_lock_io_pending=usb=busy=online=queued=keys=rails=pins=lcd=touch=usb_at_timer=slow_media=false;
 off_success=false;parked_off=0;media=allow_key=confirmed=true;sleeps=usb_after=timers=cut=restore=closes=mounts=resume=keys_restored=latches=0;
 stop_after=1;fail_at=operation=0;clock_us=0;
}
static void restored(void){assert(!keys&&!rails&&!pins&&!lcd&&!touch);assert(cut==restore);assert(closes==mounts);}
int main(void){
 bool key;
 reset();assert(lock_light_sleep_once((void*)1,(void*)2,&key,0,false)==ESP_OK&&key&&latches==1);restored();
 reset();slow_media=true;assert(lock_light_sleep_once((void*)1,(void*)2,&key,0,false)==ESP_OK&&key&&s_lock_io_pending);restored();
 // Three hours of timer slices only prepare/close/remount once; none of the timer wakes grants unlock.
 reset();stop_after=180;assert(lock_light_sleep_once((void*)1,(void*)2,&key,0,false)==ESP_OK&&key);
 assert(sleeps==180&&closes==1&&mounts==1&&cut==1&&restore==1&&latches==1&&clock_us>=10800000000LL);restored();
 // Inserting USB exits maintenance and restores the normal serial route's peripherals without unlocking.
 reset();stop_after=180;usb_after=3;assert(lock_light_sleep_once((void*)1,(void*)2,&key,0,false)==ESP_OK&&!key&&!latches);
 assert(sleeps==3&&mounts==1&&closes==1);restored();
 // Double-click remains enforced after a single EXT1: the caller stays locked and retries later.
 reset();allow_key=false;assert(lock_light_sleep_once((void*)1,(void*)2,&key,0,false)==ESP_ERR_INVALID_STATE&&!key&&latches==1);restored();
 // A shared EXT1 without verified physical P17 is not an unlock in single or double mode.
 for(unsigned mode=0;mode<2;mode++){
  reset();allow_key=mode;confirmed=false;
  assert(lock_light_sleep_once(NULL,(void*)2,&key,0,false)==ESP_ERR_INVALID_STATE&&!key&&!latches);restored();
 }
 // Every failing operation restores all resources already changed; no timer or error unlocks.
 reset();assert(lock_light_sleep_once((void*)1,(void*)2,&key,0,false)==ESP_OK);unsigned count=operation;
 for(unsigned i=1;i<=count;i++){
  reset();fail_at=i;int err=lock_light_sleep_once((void*)1,(void*)2,&key,0,false);
  assert(err!=ESP_OK);restored();assert(!key||latches==1);assert(timers<=1);
 }
 for(unsigned i=0;i<3;i++){
  reset();if(i==0)usb=true;else if(i==1)busy=true;else online=true;
  assert(lock_light_sleep_once(NULL,(void*)2,&key,0,false)==ESP_ERR_INVALID_STATE&&!key&&!sleeps&&!closes);
 }
 reset();media=false;assert(lock_light_sleep_once(NULL,(void*)2,&key,0,false)==ESP_OK&&key&&sleeps==1&&closes==1);restored();
 reset();queued=true;assert(lock_light_sleep_once((void*)1,(void*)2,&key,0,false)!=ESP_OK&&key&&!sleeps);restored();
 reset();usb_at_timer=true;assert(lock_light_sleep_once((void*)1,(void*)2,&key,0,false)!=ESP_OK&&!key&&!sleeps);restored();
 // A timer session stops at the deep deadline rather than endlessly repeating60s slices.
 reset();stop_after=180;assert(lock_light_sleep_once((void*)1,(void*)2,&key,600000000,false)==ESP_OK&&!key);
 assert(clock_us>=600000000&&clock_us<600100000&&sleeps==10&&mounts==1&&!latches);restored();
 // Short final slices and already-expired deadlines cannot oversleep by another minute.
 reset();stop_after=180;assert(lock_light_sleep_once((void*)1,(void*)2,&key,1500000,false)==ESP_OK&&!key);
 assert(clock_us==1500000&&sleeps==1);restored();
 reset();stop_after=180;assert(lock_light_sleep_once((void*)1,(void*)2,&key,1,false)==ESP_OK&&!key&&!sleeps);restored();
 // The ten-minute off deadline cuts from parked rails, with no TF/LCD/frontlight restoration.
 reset();stop_after=180;confirmed=false;off_success=true;
 if(!setjmp(off_jump)){lock_light_sleep_once(NULL,(void*)2,&key,600000000,true);assert(0);}
 assert(parked_off==1&&rails&&pins&&lcd&&touch&&!mounts&&!resume&&!latches);
 // A refused physical cut rolls back all changed peripherals and remains locked.
 reset();stop_after=180;confirmed=false;
 assert(lock_light_sleep_once(NULL,(void*)2,&key,600000000,true)==ESP_ERR_TIMEOUT&&!key&&parked_off==1);restored();
 // A physical press at the deadline takes the normal verified wake path instead of powering off.
 reset();stop_after=180;
 assert(lock_light_sleep_once(NULL,(void*)2,&key,600000000,true)==ESP_OK&&key&&!parked_off);restored();
 puts("PASS: actual S31 light transaction, 3h timer retention and exact10min/short/expired deadlines with one TF cycle, USB insertion/no unlock, single IRQ/double gate and all operation faults/rollback");
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)

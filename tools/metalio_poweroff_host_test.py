#!/usr/bin/env python3
"""中文：执行真实RTC/文件/按键/硬件关机顺序和失败恢复。
English: Execute actual RTC/file/key/hardware shutdown ordering and fault recovery.
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
typedef int esp_err_t;
typedef struct{bool mounted;}read_pico_sd_info_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_TIMEOUT -2
#define ESP_ERR_NOT_FINISHED -3
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) ((void)0)
static bool s_lock_io_pending,s_lock_io_no_media,card,closed,paused,key_pending,cut_success,usb;
static bool metalio_usb_blocks_sleep(void){return usb;}
static bool metalio_confirm_power_key(void){return key_pending;}
static int rtc_error,failure;
static unsigned rtc,book,font,closes,key_pauses,pulses,remounts,resumes,haptics,clock_ms;
static jmp_buf poweroff;
static int app_sync_time_to_pmu(void){assert(!closes&&!pulses);++rtc;return rtc_error;}
static void vTaskDelay(unsigned ms){clock_ms+=ms;}
static int read_pico_sd_get_info(read_pico_sd_info_t *i){i->mounted=card;return card?ESP_OK:ESP_ERR_INVALID_STATE;}
static int book_suspend_io(void){assert(rtc&&!pulses&&!closes);++book;return failure==1?-4:ESP_OK;}
static int ttf_font_suspend_io(void){assert(book&&!pulses&&!closes);++font;return failure==2?-4:ESP_OK;}
static int read_pico_sd_sync(void){assert(book&&font&&!pulses);++closes;if(failure==3)return ESP_ERR_NOT_FINISHED;closed=true;return ESP_OK;}
static int read_pico_pmu_keys_pause(void){assert(closed&&!pulses);++key_pauses;if(key_pending)return ESP_ERR_INVALID_STATE;paused=true;return ESP_OK;}
static void read_pico_pmu_keys_resume(void){assert(paused);paused=false;}
static void metalio_haptic_stop(void){assert(paused&&closed);}
static void metalio_frontlight(unsigned b,unsigned w){assert(!b&&w==50&&closed&&paused);}
static void epd_poweroff(void){assert(closed&&paused);}
static void metalio_poweroff(void){assert(closed&&paused&&rtc&&font);++pulses;if(cut_success)longjmp(poweroff,1);}
static int read_pico_sd_remount(void){assert(closed);++remounts;closed=false;return ESP_OK;}
static int lock_resume_media_io(unsigned wait){assert(wait==500&&!closed);++resumes;s_lock_io_pending=false;return ESP_OK;}
static void metalio_haptic_resume(void){++haptics;}
// 充电监控的挂起/恢复由充电驱动自己验证；这里只要求关机事务前后成对调用。
// The charge driver owns suspend/resume; this test only requires the paired call around power-off.
static void metalio_charger_suspend(void){}
static void metalio_charger_resume(void){}
'''
source=root/'main/sleep.c'
unit+=function('lock_power_off',source)+'\n'+function('lock_power_off_parked',source)+'\n'
unit+=r'''
static void reset(void){
 usb=s_lock_io_pending=s_lock_io_no_media=closed=paused=key_pending=cut_success=false;card=true;
 rtc_error=failure=0;rtc=book=font=closes=key_pauses=pulses=remounts=resumes=haptics=clock_ms=0;
}
int main(void){
 reset();assert(lock_power_off()==ESP_ERR_TIMEOUT);
 assert(rtc==1&&book==1&&font==1&&closes==1&&pulses==1&&remounts==1&&resumes==1&&!paused&&clock_ms==1000);
 for(unsigned i=1;i<=3;i++){
  reset();failure=i;assert(lock_power_off()!=ESP_OK);
  assert(!pulses&&!remounts&&!paused&&!closed&&resumes==1);
 }
 reset();key_pending=true;assert(lock_power_off()==ESP_ERR_INVALID_STATE);
 assert(key_pending&&!pulses&&remounts==1&&!paused);
 reset();rtc_error=-4;assert(lock_power_off()==ESP_ERR_TIMEOUT&&rtc==2&&pulses==1&&clock_ms==1020);
 reset();rtc_error=ESP_ERR_INVALID_STATE;assert(lock_power_off()==ESP_ERR_TIMEOUT&&rtc==1&&pulses==1);
 reset();card=false;assert(lock_power_off()==ESP_ERR_TIMEOUT&&s_lock_io_no_media&&pulses==1);
 reset();cut_success=true;
 if(!setjmp(poweroff)){(void)lock_power_off();assert(!"hardware power cut unexpectedly returned");}
 assert(pulses==1&&paused&&closed&&!remounts&&!resumes);
 // Execute the real parked handoff: RTC and pulse only, no media/rail/lamp restore.
 reset();closed=paused=true;book=font=1;cut_success=true;
 if(!setjmp(poweroff)){lock_power_off_parked();assert(0);}
 assert(rtc==1&&pulses==1&&!closes&&!remounts&&!resumes&&closed&&paused);
 reset();closed=paused=true;book=font=1;usb=true;
 assert(lock_power_off_parked()==ESP_ERR_INVALID_STATE&&!pulses&&!remounts);
 reset();closed=paused=true;book=font=1;key_pending=true;
 assert(lock_power_off_parked()==ESP_ERR_INVALID_STATE&&!pulses&&!remounts);
 reset();closed=paused=true;book=font=1;rtc_error=-4;
 assert(lock_power_off_parked()==ESP_ERR_TIMEOUT&&rtc==2&&pulses==1&&!remounts&&!resumes);
 puts("PASS: actual S31 RTC-first hardware-off transaction, bounded RTC retry, absent card, pending-key cancellation, file/flush faults and returning-power rollback");
}
'''
with tempfile.TemporaryDirectory(prefix='s31-poweroff-') as d:
 p=Path(d);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

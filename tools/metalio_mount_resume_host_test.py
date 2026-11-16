#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""异步挂载屏障、唤醒重试与USB所有权。/ Async mount barrier, wake retries and USB ownership."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
R=Path(__file__).resolve().parents[1]
s=R/'boards/metalio47/src/main/sleep.c'
if not s.exists():s=R/'main/sleep.c'
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FINISHED 1
#define ESP_ERR_INVALID_STATE 2
#define pdMS_TO_TICKS(x) (x)
typedef struct{bool mounted;}read_pico_sd_info_t;
static bool s_lock_io_pending,s_lock_io_no_media,s_lock_io_failed,usb,mounted,handles_active;
static bool s_lock_sd_parked,s_lock_book_reopen_failed,sd_suspended;
static void *s_lock_sleep_touch;
static unsigned s_lock_io_failures;
static int64_t s_lock_io_retry_us,clock_us;
static int remaining,mount_error,font_error,book_error,font_calls,book_calls,remounts;
static int64_t esp_timer_get_time(void){return clock_us;}
static void metalio_sleep_report_reset(void){}
static bool usb_storage_active(void){return usb;}
static int read_pico_sd_get_info(read_pico_sd_info_t *p){
 p->mounted=mounted;
 if(remaining>0){--remaining;return ESP_ERR_NOT_FINISHED;}
 if(mount_error==ESP_ERR_NOT_FINISHED){mounted=true;p->mounted=true;}
 return mounted?ESP_OK:mount_error;
}
static int read_pico_sd_resume(void){
 if(sd_suspended){++remounts;sd_suspended=false;return mount_error;}
 return mounted?ESP_OK:mount_error;
}
static void vTaskDelay(int ms){assert(ms==20);clock_us+=20000;if(!remaining&&mount_error==ESP_ERR_NOT_FINISHED)mounted=true;}
static int ttf_font_resume_io(void){if(!handles_active)return 0;assert(mounted);++font_calls;return font_error;}
static int book_resume_io(void){if(!handles_active)return 0;assert(mounted);++book_calls;return book_error;}
'''+function('lock_media_result',s)+function('lock_resume_media_io',s)+function('lock_resume_io_service',s)+function('app_sleep_media_ready',s)+r'''
static void reset(void){s_lock_io_failed=false;s_lock_io_failures=0;s_lock_io_pending=handles_active=true;s_lock_io_no_media=usb=mounted=s_lock_book_reopen_failed=false;s_lock_sleep_touch=NULL;s_lock_sd_parked=sd_suspended=true;s_lock_io_retry_us=clock_us=0;remaining=remounts=font_calls=book_calls=font_error=book_error=0;mount_error=ESP_ERR_NOT_FINISHED;}
int main(void){
 reset();remaining=3;s_lock_sleep_touch=(void *)1;
 assert(lock_resume_media_io(0)==ESP_ERR_NOT_FINISHED&&remounts==0);
 s_lock_sleep_touch=NULL;
 assert(lock_resume_media_io(15000)==ESP_OK&&!s_lock_io_pending&&font_calls==1&&book_calls==1&&remounts==1);
 reset();mounted=true;s_lock_sd_parked=sd_suspended=false;assert(lock_resume_media_io(15000)==ESP_OK&&!remounts&&font_calls==1);
 reset();remaining=10000;assert(lock_resume_media_io(100)==ESP_ERR_NOT_FINISHED&&clock_us==100000&&s_lock_io_pending&&!font_calls&&!book_calls);
 reset();mount_error=9;assert(lock_resume_media_io(15000)==9&&s_lock_io_pending&&!font_calls&&!book_calls);
 mount_error=ESP_ERR_NOT_FINISHED;lock_resume_io_service();assert(!s_lock_io_pending&&font_calls==1&&book_calls==1);
 reset();usb=true;assert(lock_resume_media_io(15000)==ESP_ERR_INVALID_STATE&&!remounts&&!font_calls&&s_lock_io_pending);
 reset();mounted=true;font_error=7;assert(lock_resume_media_io(15000)==7&&s_lock_io_pending&&book_calls==1);
 font_error=0;lock_resume_io_service();assert(!s_lock_io_pending&&font_calls==2&&book_calls==2);
 reset();mounted=true;book_error=8;lock_resume_io_service();assert(s_lock_io_pending&&font_calls==1);lock_resume_io_service();assert(font_calls==1);
 clock_us=1000000;book_error=0;lock_resume_io_service();assert(!s_lock_io_pending&&font_calls==2);
 // 解锁后由主循环继续调用，挂载/字体失败各自恢复，USB不抢占。/ Public main-loop service survives unlock, separate mount/font failures and USB ownership.
 reset();remaining=2;assert(!app_sleep_media_ready()&&s_lock_io_pending&&!font_calls);
 clock_us=1000000;assert(!app_sleep_media_ready()&&remounts==1&&!font_calls);
 clock_us=2000000;assert(app_sleep_media_ready()&&!s_lock_io_pending&&font_calls==1&&remounts==1);
 reset();mounted=true;font_error=7;assert(!app_sleep_media_ready());clock_us=1000000;font_error=0;
 assert(app_sleep_media_ready()&&font_calls==2);reset();usb=true;assert(!app_sleep_media_ready()&&!remounts);
 usb=false;clock_us=1000000;assert(app_sleep_media_ready());
 reset();s_lock_io_no_media=true;handles_active=false;mount_error=9;
 assert(lock_resume_media_io(0)==ESP_OK&&!s_lock_io_pending&&!font_calls&&!book_calls);
 reset();mounted=true;font_error=7;for(unsigned i=0;i<3;i++){clock_us=(int64_t)i*1000000;assert(!app_sleep_media_ready());}
 assert(s_lock_io_failed&&font_calls==3);clock_us+=1000000;assert(!app_sleep_media_ready()&&font_calls==3);
 reset();s_lock_io_no_media=true;mounted=false;remaining=100;assert(lock_resume_media_io(0)==ESP_ERR_NOT_FINISHED&&!font_calls&&!book_calls);
 puts("PASS: async remount waits before font/book IO; bounded timeout, failed-mount retry, USB exclusion, font/book partial failure and one-second retry backoff");
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(u)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

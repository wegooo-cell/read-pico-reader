#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run real wake/loop recovery together: card lifecycle precedes font reopening."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
from board_source_view import select_source

root = Path(__file__).resolve().parents[1]
sleep = root / 'boards/metalio47/src/main/sleep.c'
if not sleep.exists():
    sleep = root / 'main/sleep.c'
loop = select_source((root / 'main/app/app_loop.c').read_text(), 'metalio47')
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
enum {ESP_OK,ESP_ERR_NOT_FINISHED,ESP_ERR_INVALID_STATE,ESP_FAIL,ESP_ERR_NOT_FOUND};
enum {READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_READY,READ_PICO_TRANSFER_UPLOADING};
#define pdMS_TO_TICKS(x) (x)
typedef struct {bool mounted,present;} read_pico_sd_info_t;
typedef struct {int state;} read_pico_transfer_status_t;
typedef struct {bool busy;} online_font_status_t;
typedef struct {int64_t now_ms;} app_ctx_t;
typedef struct {void(*on_media_lost)(app_ctx_t*);void(*on_media_ready)(app_ctx_t*);} app_desc_t;
static bool s_lock_io_pending,s_lock_io_no_media,s_lock_io_failed,s_lock_sd_parked,s_lock_book_reopen_failed;
static void *s_lock_sleep_touch;
static unsigned s_lock_io_failures;
static int64_t s_lock_io_retry_us,clock_us;
static bool mounted,mounting,verified,usb,ota,download,manual,online,workers=true,sd_suspended;
static bool known_no_media;
static bool font_live,book_live,builtin,abnormal;
static int font_error,book_error,transfer;
static unsigned fonts,books,health,mounts,closes,lost,ready,recovered,generation,font_generation,file_lost;
static int64_t esp_timer_get_time(void){return clock_us;}
static void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;}
static void metalio_sleep_report_reset(void){}
static bool usb_storage_active(void){return usb;}
static bool app_sd_recovery_busy(void){return manual;}
static bool pico_online_busy(void){return ota;}
static bool app_download_active(void){return download;}
static void online_font_status(online_font_status_t *s){s->busy=online;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *s){s->state=transfer;}
static int read_pico_sd_get_info(read_pico_sd_info_t *s){
 s->mounted=mounted;s->present=!known_no_media;
 return mounting?ESP_ERR_NOT_FINISHED:mounted?ESP_OK:known_no_media?ESP_ERR_NOT_FOUND:ESP_FAIL;
}
static int read_pico_sd_resume(void){
 if(sd_suspended){
  sd_suspended=false;
  if(known_no_media)return ESP_ERR_NOT_FOUND;
  assert(!font_live&&!book_live);
  mounting=true;++mounts;
 }
 read_pico_sd_info_t info;return read_pico_sd_get_info(&info);
}
static void read_pico_sd_poll_health(void){++health;}
static int ttf_font_resume_io(void){assert(!mounting);if(known_no_media)return ESP_OK;assert(mounted&&verified);++fonts;if(!font_error){font_live=true;font_generation=generation;}return font_error;}
static int book_resume_io(void){assert(!mounting);if(known_no_media)return ESP_OK;assert(mounted&&verified);++books;if(!book_error)book_live=true;return book_error;}
static bool book_cover_jobs_quiesce(unsigned ms){assert(!ms);return workers;}
static bool ui_avatar_quiesce(unsigned ms){assert(!ms);return workers;}
static void book_close(void){book_live=false;++closes;}
static void ttf_font_close_embedded(void){assert(!book_live);++closes;}
static void ttf_font_unload(void){font_live=false;++closes;}
static int ttf_font_open_builtin(void){builtin=true;return ESP_OK;}
static bool ttf_font_ready(void){return true;}
static bool ttf_font_is_builtin(void){return builtin;}
static void ui_text_set_system_font(bool on){assert(!on);}
static void app_book_shelf_prefetch_reset(void){}
static void ui_avatar_invalidate(void){}
static void book_lock_collage_invalidate(void){}
static bool read_pico_sd_needs_recovery(void){return !sd_suspended&&!mounted&&!mounting&&!known_no_media;}
static int read_pico_sd_start_probe(void){assert(0);return ESP_FAIL;}
static int read_pico_sd_start_recovery(void){assert(!font_live&&!book_live&&!mounted&&!usb&&!ota);mounting=true;++mounts;return ESP_OK;}
static bool app_font_recover_media(void){assert(mounted&&verified&&!mounting);++recovered;if(!abnormal){font_live=true;builtin=false;font_generation=generation;}return !abnormal;}
static void page_lost(app_ctx_t *c){(void)c;++lost;}
static void page_ready(app_ctx_t *c){(void)c;assert(mounted&&verified&&!mounting);++ready;}
static const app_desc_t app_book={page_lost,page_ready};
static void app_book_wake_file_failed(app_ctx_t *ctx){(void)ctx;assert(book_error!=ESP_OK);++file_lost;book_live=false;}
'''
for name in ('lock_media_result', 'lock_resume_media_io', 'lock_resume_io_service',
             'app_sleep_media_ready', 'app_sleep_media_failed', 'app_sleep_media_pending',
             'app_sleep_book_reopen_failed', 'app_sleep_discard_media_io'):
    unit += function(name, sleep) + '\n'
unit += 'static int64_t s_media_recovery_after_ms; static unsigned s_media_recovery_attempts;\n'
with tempfile.TemporaryDirectory() as folder:
    p = Path(folder)
    (p / 'loop.c').write_text(loop)
    for name in ('recover_media', 'poll_media'):
        unit += function(name, p / 'loop.c') + '\n'
    unit += r'''
static bool ui_mounted,invalidated;
static bool poll(unsigned ms){clock_us=(int64_t)ms*1000;app_ctx_t ctx={ms};return poll_media(&ctx,&app_book,&ui_mounted,&invalidated);}
static void finish_mount(void){assert(mounting&&!font_live&&!book_live);mounting=false;mounted=verified=true;++generation;}
static void reset(void){
 s_lock_io_pending=true;s_lock_io_no_media=s_lock_io_failed=s_lock_sd_parked=s_lock_book_reopen_failed=false;
 s_lock_sleep_touch=NULL;s_lock_io_failures=0;s_lock_io_retry_us=clock_us=0;
 mounted=verified=ui_mounted=true;mounting=usb=ota=download=manual=online=sd_suspended=known_no_media=false;workers=true;
 font_live=book_live=builtin=abnormal=invalidated=false;font_error=book_error=transfer=0;
 fonts=books=health=mounts=closes=lost=ready=recovered=file_lost=0;generation=1;font_generation=0;
 s_media_recovery_after_ms=0;s_media_recovery_attempts=0;
}
int main(void){
 // Timer/shared-line and PIN waits leave a parked card untouched.
 reset();mounted=verified=false;s_lock_sd_parked=sd_suspended=true;s_lock_sleep_touch=(void*)1;
 assert(lock_resume_media_io(0)==ESP_ERR_NOT_FINISHED&&!mounts);
 clock_us=1000000;lock_resume_io_service();assert(!mounts&&!fonts&&!books);
 s_lock_sleep_touch=NULL;assert(lock_resume_media_io(0)==ESP_ERR_NOT_FINISHED&&mounts==1);
 // A long in-progress mount is observed, never closed or requeued.
 for(unsigned ms=2000;ms<20000;ms+=1000){assert(!poll(ms));assert(mounts==1&&!fonts&&!books&&!closes&&!s_lock_io_failed);}
 finish_mount();assert(poll(20000)&&fonts==1&&books==1&&!s_lock_io_pending&&health);
 assert(!poll(21000)&&mounts==1);

 // A confirmed empty slot needs no mount and still completes wake.
 reset();mounted=verified=ui_mounted=false;known_no_media=true;s_lock_sd_parked=sd_suspended=s_lock_io_no_media=true;
 assert(lock_resume_media_io(0)==ESP_OK&&!s_lock_io_pending&&!mounts);
 assert(!poll(0)&&!mounts);

 // A book or font failure on an intact card stays file-local.
 reset();font_error=ESP_FAIL;
 assert(!poll(0)&&fonts==1&&book_live);
 assert(!poll(1000)&&fonts==2);
 assert(poll(2000)&&fonts==3&&book_live&&!s_lock_io_pending&&!lost&&!closes&&!mounts&&recovered==1);
 reset();book_error=ESP_FAIL;
 assert(!poll(0)&&!poll(1000));
 assert(poll(2000)&&file_lost==1&&!s_lock_io_pending&&!lost&&!mounts&&recovered==1);

 // Health-confirmed loss closes all old files before recovery. Busy owners defer initiation.
 reset();font_error=ESP_FAIL;
 assert(!poll(0));assert(!poll(1000));
 mounted=verified=false;
 assert(poll(2000)&&closes==3&&lost==1&&!font_live&&!book_live&&!s_lock_io_pending&&!mounts);
 ota=true;assert(!poll(4000)&&!mounts);ota=false;
 workers=false;assert(!poll(5000)&&!mounts);workers=true;
 assert(!poll(6000)&&mounting&&mounts==1);
 assert(!poll(7000)&&mounts==1);
 finish_mount();font_error=0;assert(poll(8000)&&ready==1&&recovered==1&&font_live&&font_generation==2);
 // A failed wake mount goes through the same teardown even if UI did not know the old mount.
 reset();mounted=verified=ui_mounted=false;s_lock_sd_parked=true;
 assert(!poll(0)&&!poll(1000));
 assert(poll(2000)&&lost==1&&closes==3&&!mounts);
 assert(!poll(3000)&&mounts==1&&mounting);

 // The recovery callback remains subject to abnormal-boot builtin protection.
 reset();s_lock_io_pending=false;mounted=verified=ui_mounted=false;abnormal=builtin=true;
 assert(!poll(12000)&&mounting);finish_mount();assert(!poll(13000)&&recovered==1&&builtin&&!font_live);
 puts("PASS: S31 lock/PIN defers mount, unlock queues one verified mount, independent book/font failure preserves mount, confirmed loss drains handles before recovery, abnormal-boot guard preserved");
}
'''
    (p / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)

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
enum {ESP_OK,ESP_ERR_NOT_FINISHED,ESP_ERR_INVALID_STATE,ESP_FAIL};
enum {READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_READY,READ_PICO_TRANSFER_UPLOADING};
#define pdMS_TO_TICKS(x) (x)
typedef struct {bool mounted,present;} read_pico_sd_info_t;
typedef struct {int state;} read_pico_transfer_status_t;
typedef struct {bool busy;} online_font_status_t;
typedef struct {int64_t now_ms;} app_ctx_t;
typedef struct {void(*on_media_lost)(app_ctx_t*);void(*on_media_ready)(app_ctx_t*);} app_desc_t;
static bool s_lock_io_pending,s_lock_io_no_media,s_lock_io_failed;
static unsigned s_lock_io_failures;
static int64_t s_lock_io_retry_us,clock_us;
static bool mounted,mounting,verified,usb,ota,download,manual,online,workers=true,invalidate_busy;
static bool font_live,book_live,builtin,abnormal;
static int font_error,book_error,transfer;
static unsigned fonts,books,health,mounts,closes,invalidates,lost,ready,recovered,generation,font_generation;
static int64_t esp_timer_get_time(void){return clock_us;}
static void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;}
static void metalio_sleep_report_reset(void){}
static bool usb_storage_active(void){return usb;}
static bool app_sd_recovery_busy(void){return manual;}
static bool pico_online_busy(void){return ota;}
static bool app_download_active(void){return download;}
static void online_font_status(online_font_status_t *s){s->busy=online;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *s){s->state=transfer;}
static int read_pico_sd_get_info(read_pico_sd_info_t *s){s->mounted=mounted;s->present=true;return mounting?ESP_ERR_NOT_FINISHED:mounted?ESP_OK:ESP_FAIL;}
static int read_pico_sd_remount(void){assert(!font_live&&!book_live);mounting=true;++mounts;return ESP_ERR_NOT_FINISHED;}
static void read_pico_sd_poll_health(void){++health;}
static int ttf_font_resume_io(void){assert(mounted&&verified&&!mounting);++fonts;if(!font_error){font_live=true;font_generation=generation;}return font_error;}
static int book_resume_io(void){assert(mounted&&verified&&!mounting);++books;if(!book_error)book_live=true;return book_error;}
static bool ttf_font_io_pending(void){return true;}
static bool book_io_pending(void){return true;}
static bool book_cover_jobs_quiesce(unsigned ms){assert(!ms);return workers;}
static bool ui_avatar_quiesce(unsigned ms){assert(!ms);return workers;}
static void book_close(void){book_live=false;++closes;}
static void ttf_font_close_embedded(void){assert(!book_live);++closes;}
static void ttf_font_unload(void){font_live=false;++closes;}
static int ttf_font_open_builtin(void){builtin=true;return ESP_OK;}
static bool ttf_font_ready(void){return true;}
static bool ttf_font_is_builtin(void){return builtin;}
static void ui_text_set_system_font(bool on){assert(!on);}
static void ui_avatar_invalidate(void){}
static void book_lock_collage_invalidate(void){}
static bool read_pico_sd_needs_recovery(void){return !mounted&&!mounting;}
static int read_pico_sd_invalidate_io(void){assert(!font_live&&!book_live);++invalidates;if(invalidate_busy)return ESP_ERR_NOT_FINISHED;mounted=false;verified=false;return ESP_OK;}
static int read_pico_sd_start_probe(void){assert(0);return ESP_FAIL;}
static int read_pico_sd_start_recovery(void){assert(!font_live&&!book_live&&!mounted&&!usb&&!ota);mounting=true;++mounts;return ESP_OK;}
static bool app_font_recover_media(void){assert(mounted&&verified&&!mounting);++recovered;if(!abnormal){font_live=true;builtin=false;font_generation=generation;}return !abnormal;}
static void page_lost(app_ctx_t *c){(void)c;++lost;}
static void page_ready(app_ctx_t *c){(void)c;assert(mounted&&verified&&!mounting);++ready;}
'''
for name in ('lock_media_result', 'lock_resume_media_io', 'lock_resume_io_service',
             'app_sleep_media_ready', 'app_sleep_media_failed', 'app_sleep_discard_media_io'):
    unit += function(name, sleep) + '\n'
unit += 'static int64_t s_media_recovery_after_ms; static unsigned s_media_recovery_attempts;\n'
with tempfile.TemporaryDirectory() as folder:
    p = Path(folder)
    (p / 'loop.c').write_text(loop)
    for name in ('recover_media', 'poll_media'):
        unit += function(name, p / 'loop.c') + '\n'
    unit += r'''
static app_desc_t page={page_lost,page_ready};
static bool ui_mounted,invalidated;
static bool poll(unsigned ms){clock_us=(int64_t)ms*1000;app_ctx_t ctx={ms};return poll_media(&ctx,&page,&ui_mounted,&invalidated);}
static void finish_mount(void){assert(mounting&&!font_live&&!book_live);mounting=false;mounted=verified=true;++generation;}
static void reset(void){
 s_lock_io_pending=true;s_lock_io_no_media=s_lock_io_failed=false;s_lock_io_failures=0;s_lock_io_retry_us=clock_us=0;
 mounted=verified=ui_mounted=true;mounting=usb=ota=download=manual=online=invalidate_busy=false;workers=true;
 font_live=book_live=builtin=abnormal=invalidated=false;font_error=book_error=transfer=0;
 fonts=books=health=mounts=closes=invalidates=lost=ready=recovered=0;generation=1;font_generation=0;
 s_media_recovery_after_ms=0;s_media_recovery_attempts=0;
}
int main(void){
 // Even a long in-progress mount performs no font/book opens. It is not cancelled as a fault.
 reset();mounted=verified=false;mounting=true;
 for(unsigned ms=0;ms<20000;ms+=1000){assert(!poll(ms));assert(!fonts&&!books&&!closes&&!s_lock_io_failed);}
 finish_mount();assert(poll(20000)&&fonts==1&&books==1&&!s_lock_io_pending&&health);
 // Cached mounted=true plus a persistent font failure used to block all health/recovery forever.
 reset();font_error=ESP_FAIL;invalidate_busy=true;
 assert(!poll(0)&&fonts==1&&book_live&&health==1);
 assert(!poll(1000)&&fonts==2);
 workers=false;assert(!poll(2000)&&s_lock_io_failed&&!closes);
 workers=true;assert(poll(3000)&&closes==3&&lost==1&&!font_live&&!book_live&&s_lock_io_pending);
 // USB/OTA/download/HTTP owners prevent a new lifecycle transaction, including after partial cleanup.
 usb=true;assert(!poll(4000)&&invalidates==1);usb=false;
 ota=true;assert(!poll(5000)&&invalidates==1);ota=false;
 online=true;assert(!poll(6000)&&invalidates==1);online=false;
 transfer=READ_PICO_TRANSFER_READY;assert(!poll(7000)&&invalidates==1);transfer=0;
 invalidate_busy=false;assert(!poll(8000)&&closes==3&&!s_lock_io_pending&&!mounted);
 assert(!poll(9000)&&mounting&&mounts==1&&fonts==3);
 assert(!poll(10000)&&mounting&&fonts==3);
 finish_mount();font_error=0;assert(poll(11000)&&ready==1&&recovered==1&&font_live&&font_generation==2);
 // The recovery callback remains subject to abnormal-boot builtin protection.
 reset();s_lock_io_pending=false;mounted=verified=ui_mounted=false;abnormal=builtin=true;
 assert(!poll(12000)&&mounting);finish_mount();assert(!poll(13000)&&recovered==1&&builtin&&!font_live);
 puts("PASS: real wake+loop sequence: pending card precedes font/book, stale-mounted persistent failure no longer blocks health, all old/partially reopened handles close before remount, lifecycle owners/workers respected, validated new session restores font, abnormal-boot protection retained");
}
'''
    (p / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)

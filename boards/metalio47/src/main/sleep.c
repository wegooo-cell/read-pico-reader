/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏进睡、浅睡等待按键或拿起、软睡/关机拉掉 EN。
 *
 * Enter lock and sleep, light-sleep wait for key or pickup, and drop EN
 * for soft sleep / power-off.
 * 用户修订：票根从任意页面进入均用系统字体；浅睡唤醒恢复此前阅读字体，深睡由启动检查点恢复。
 * User revision: tickets use the system face from every page; light wake restores the previous reader face, while deep wake uses the startup checkpoint.
 * 用户修订：单击锁屏提示复用Pico布局；双击/PIN和S31外设休眠顺序保持。
 * User revision: use Pico's single-click hint layout, retaining double-click/PIN and S31 peripheral sleep sequencing.
 */

#include "sleep.h"
#include "ui_avatar.h"
#include "book_cover_jobs.h"
#include "metalio_hw.h"
#include "metalio_charger.h"
#include "metalio_sleep.h"
#include "ble_page_turner.h"
#include "driver/usb_serial_jtag.h"
#include "ttf_font.h"
#include "output_lcd/lcd_driver.h"
#include "boot_state.h"
#include "lock_pin.h"
#include "lock_screen.h"
#include "read_pico_sd.h"
#include "usb_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "app_font_context.h"
#include "book_cover.h"
#include "book_source.h"
#include "book_lock_collage.h"
#include "book_lock_bookmark.h"
#include "book_ticket.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pmu_selftest.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "sc7a20h_lab.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_image_dither.h"
#include "ui_wallpaper.h"
#include "ui_lock_feedback.h"
#include "e0470_epaper_waveform.h"
#include "ota_online.h"

static const char* TAG = "read_pico";
// 只消费新动作；单击选项不把持续按住的锁屏长按当作下一次解锁。
// Consume new actions only: a held lock-initiating press cannot unlock again.
static bool lock_take_key_wakeup(void) {
    return app_settings_wake_presses() == 1 ? read_pico_pmu_take_key_wakeup_single() :
        read_pico_pmu_take_key_wakeup();
}
static bool s_lock_io_pending;
static bool s_lock_io_no_media;
static int64_t s_lock_io_retry_us;
static unsigned s_lock_io_failures;
static bool s_lock_io_failed;
// 恢复失败不能无限挡住主循环的卡恢复；真正的重挂由消费者释放后执行。
// Bounded reopen failures yield to loop-owned card recovery after consumers close.
static esp_err_t lock_media_result(esp_err_t err) {
    if (err == ESP_OK) { s_lock_io_failures = 0; s_lock_io_failed = false; }
    else if (err != ESP_ERR_NOT_FINISHED && ++s_lock_io_failures >= 3) s_lock_io_failed = true;
    return err;
}
// TF异步挂载完成才恢复书籍/字体；失败保持原路径和挂起句柄，允许后续重试。
// Reopen book/font IO only after asynchronous TF mount; retain paths and suspended handles on failure.
static esp_err_t lock_resume_media_io(uint32_t wait_ms) {
    if (!s_lock_io_pending) return ESP_OK;
    if (usb_storage_active()) return ESP_ERR_INVALID_STATE;
    if (s_lock_io_failed) return ESP_FAIL;
    // 无卡且无待重开的句柄时可以完成恢复；不为内建字体强制挂卡。
    // With no card or suspended handles, complete recovery without mounting for the built-in face.
    if (s_lock_io_no_media) {
        s_lock_io_no_media=false;
        if (!ttf_font_io_pending() && !book_io_pending()) {
            s_lock_io_pending=false; return lock_media_result(ESP_OK);
        }
    }
    read_pico_sd_info_t info = {0};
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err != ESP_ERR_NOT_FINISHED && !info.mounted) err = read_pico_sd_remount();
    int64_t deadline = esp_timer_get_time() + (int64_t)wait_ms * 1000;
    while (err == ESP_OK || err == ESP_ERR_NOT_FINISHED) {
        err = read_pico_sd_get_info(&info);
        if (err != ESP_ERR_NOT_FINISHED) break;
        if (esp_timer_get_time() >= deadline) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (err != ESP_OK || !info.mounted)
        return lock_media_result(err == ESP_OK ? ESP_ERR_INVALID_STATE : err);
    esp_err_t font = ttf_font_resume_io(), book = book_resume_io();
    if (font == ESP_OK && book == ESP_OK) s_lock_io_pending = false;
    return lock_media_result(font != ESP_OK ? font : book);
}
static void lock_resume_io_service(void) {
    metalio_sleep_report_reset();
    if (s_lock_io_pending && esp_timer_get_time() >= s_lock_io_retry_us) {
        s_lock_io_retry_us = esp_timer_get_time() + INT64_C(1000000);
        (void)lock_resume_media_io(0);
    }
}
bool app_sleep_media_ready(void) {
    // 解锁后仍继续恢复；不能把有意卸载当拔卡，也不能销毁尚待恢复的字体槽。
    // Continue after unlock; intentional unmount is not card removal and pending font slots must survive.
    lock_resume_io_service();
    return !s_lock_io_pending;
}
bool app_sleep_media_failed(void) { return s_lock_io_pending && s_lock_io_failed; }
bool app_sleep_media_pending(void) { return s_lock_io_pending; }
void app_sleep_discard_media_io(void) {
    s_lock_io_pending = false;
    s_lock_io_no_media = false;
    s_lock_io_retry_us = 0;
    s_lock_io_failures = 0;
    s_lock_io_failed = false;
}
static bool s_lock_feedback_started;
static bool s_lock_deep_ready;
static bool s_lock_frame_cached;
static cst836u_handle_t s_lock_sleep_touch;
static bool s_boot_from_deep, s_boot_power_wake, s_boot_power_on;
void app_lock_capture_boot_wakeup(void) {
    s_boot_power_on=esp_reset_reason()==ESP_RST_POWERON;
    s_boot_from_deep=esp_reset_reason()==ESP_RST_DEEPSLEEP;
    s_boot_power_wake=s_boot_from_deep && esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT1 &&
        (esp_sleep_get_ext1_wakeup_status() & (UINT64_C(1)<<READ_PICO_IOE_INT_GPIO))!=0;
}
static esp_err_t lock_light_sleep_once(sc7a20h_handle_t acc, cst836u_handle_t touch, bool *key_wake, int64_t wake_by, bool off_at_deadline);
void app_lock_set_resume_ready(bool ready) { s_lock_deep_ready = ready; }



extern const uint8_t lock_4bpp_bin_start[] asm("_binary_lock_4bpp_bin_start");

// 中文：保留S31灭灯/按键边界，反馈期间的新按键留给锁屏唤醒。
// English: Keep S31 lamp/key boundaries; preserve new presses during feedback for waking.
void app_lock_feedback(EpdiyHighlevelState *hl) {
    s_lock_feedback_started = false;
    if (!hl || app_settings_lock_style() != 2 || pico_online_busy()) return;
    read_pico_transfer_status_t transfer = {0};
    read_pico_transfer_get_status(&transfer);
    if (transfer.state == READ_PICO_TRANSFER_UPLOADING) return;
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    if (!fb || epd_rotated_display_width() < 97 || epd_rotated_display_height() < 97) return;
    metalio_frontlight(0, 50);
    read_pico_pmu_drain_events();
    s_lock_feedback_started = true;
    display_main_transition_cancel();
    epd_hl_set_all_white(hl);
    if (update_display_full(hl) != EPD_DRAW_SUCCESS) return;
    EpdRect area = ui_lock_feedback_draw(fb);
    (void)update_display_area_diff_with(hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, area);
    epd_poweroff();
}

static bool draw_wallpaper(uint8_t *framebuffer) {
    return app_settings_lock_style() == 1 &&
        ui_wallpaper_prepare_lock(framebuffer, app_settings_wallpaper_path());
}

void app_lock_wait_key_idle(int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t released_from = -1;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        read_pico_pmu_drain_events();
        read_pico_clear_ioe_int();
        bool held = true;
        if (read_pico_pmu_poll() == ESP_OK) {
            held = (read_pico_pmu_get()->key_state & 0x01) != 0;
        }
        // GPIO2还承载触摸/传感器中断，不能用它判断电源键是否松开。
        // GPIO2 also carries touch/sensor IRQs; use the debounced TCA power-key state.
        if (!held) {
            if (released_from < 0) released_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - released_from >= 30) return;
        } else {
            released_from = -1;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

app_wake_source_t app_last_wake_source(void) {
    return (app_wake_source_t)app_settings_last_wake();
}

// 单击等待650ms再局部提示，三秒后还原原图；不唤灯、不把触摸当作解锁。
// Wait650ms for a double click, show locally for3s, then restore art without waking the lamp or granting touch unlock.
static EpdiyHighlevelState *s_lock_hint_hl;
static EpdiyHighlevelState *s_boot_lock_hl;
static bool s_boot_lock_active, s_boot_lock_known, s_boot_lock_draw;

static enum EpdDrawError lock_present_art(EpdiyHighlevelState *hl,uint8_t style) {
    if (style == 1) {
        // 壁纸直接从真实旧帧走一轮完整GC48，避免先白屏再输出；不继承文件/设置页的黑白策略。
        // Present wallpaper from the real prior frame in one native GC48 pass, bypassing inherited system BW policy.
        const bool binary = display_system_binary_set(false);
        enum EpdDrawError result = update_display_full(hl);
        display_system_binary_set(binary);
        return result;
    }
    // PNG已合成在目标帧中；只清除物理旧态再输出，透明区的底页和不透明主体都保留。
    // The PNG is already composed into the target; clear only the prior optical state, retaining both the backdrop and opaque artwork.
    return style==3 ? update_display_image_gray(hl) : update_display_full(hl);
}

// 未恢复像素前不做差分提示；缺卡/坏透明底图仍保留物理锁图，不假设白底。
// Never hint against unknown pixels; absent media or a damaged transparent backdrop keeps physical art rather than assuming white.
static void lock_resume_service(void) {
    EpdiyHighlevelState *hl=s_boot_lock_hl;
    if(!hl)return;
    read_pico_sd_info_t info={0};
    if(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FINISHED)return;
    if(!info.mounted){s_boot_lock_hl=NULL;return;}
    uint8_t *fb=epd_hl_get_framebuffer(hl);
    bool restored=lock_screen_restore(fb);
    if(!restored && !s_boot_lock_draw)return;
    s_boot_lock_hl=NULL;
    if(!restored) {
        uint8_t style=app_settings_lock_style();
        if(style==3)return;
        epd_hl_set_all_white(hl);
        app_lock_font_t saved;app_font_begin_lock(&saved);
        bool drawn=style==0?book_ticket_draw(fb,false):style==2?book_lock_collage_prepare(fb):draw_wallpaper(fb);
        app_font_end_lock(&saved);
        if(!drawn)return;
    }
    if(restored && pico_boot_resume_frame_cached())
        memcpy(hl->back_fb,fb,(size_t)epd_width()*epd_height()/2);
    else {
        enum EpdDrawError result=lock_present_art(hl,app_settings_lock_style());
        guard_draw_result(hl,result);epd_poweroff();
        if(result!=EPD_DRAW_SUCCESS)return;
        if(app_settings_lock_style()==2)book_lock_collage_cache_commit(fb);
        if(app_settings_lock_style()==1)ui_wallpaper_cache_commit(fb);
        if(lock_screen_save(fb))(void)pico_boot_mark_frame_cached();
    }
    s_boot_lock_known=true;s_lock_hint_hl=hl;
}

bool app_lock_resume_frame(EpdiyHighlevelState *hl) {
    s_boot_lock_hl=hl;s_boot_lock_active=hl!=NULL;
    s_boot_lock_known=s_boot_lock_draw=false;s_lock_hint_hl=NULL;
    lock_resume_service();return s_boot_lock_known;
}

static struct {
    uint8_t *pixels;
    EpdRect area;
    uint32_t serial;
    int64_t pending_at, until;
} s_lock_hint;

static uint8_t lock_hint_pixel(uint8_t *fb,int x,int y) {
    int px=x,py=y;
    switch(epd_get_rotation()) {
        case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;
        case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;
        case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;
        default:break;
    }
    return epd_get_pixel(px,py,epd_width(),epd_height(),fb);
}

static void lock_hint_present(void) {
    epd_poweron();
    enum EpdDrawError result=update_display_area_diff_with(s_lock_hint_hl,&E0470_WAVEFORM,MODE_GL16,s_lock_hint.area);
    guard_draw_result(s_lock_hint_hl,result);
    epd_poweroff();
}

static void lock_hint_finish(bool restore) {
    if(s_lock_hint.pixels) {
        uint8_t *fb=epd_hl_get_framebuffer(s_lock_hint_hl);
        EpdRect r=s_lock_hint.area;
        for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x) {
            size_t i=(size_t)y*r.width+x;
            uint8_t v=(s_lock_hint.pixels[i/2]>>((i&1)*4))&15;
            epd_draw_pixel(r.x+x,r.y+y,(uint8_t)(v*17),fb);
        }
        if(restore)lock_hint_present();
        heap_caps_free(s_lock_hint.pixels);s_lock_hint.pixels=NULL;
    }
    s_lock_hint.pending_at=s_lock_hint.until=0;
}

static void lock_hint_service(void) {
    if(!s_lock_hint_hl || app_settings_wake_presses() != 2)return;
    int64_t now=esp_timer_get_time()/1000;
    uint32_t serial=read_pico_pmu_lock_key_serial();
    if(serial!=s_lock_hint.serial) {
        s_lock_hint.serial=serial;s_lock_hint.pending_at=now+650;
    }
    if(s_lock_hint.pending_at && now>=s_lock_hint.pending_at) {
        s_lock_hint.pending_at=0;s_lock_hint.until=now+3000;
        if(!s_lock_hint.pixels) {
            uint8_t *fb=epd_hl_get_framebuffer(s_lock_hint_hl);
            const int w=epd_rotated_display_width(),h=epd_rotated_display_height();
            s_lock_hint.area=(EpdRect){(w-448)/2,h-196,448,72};
            if(fb && w>=448 && h>=268)
                s_lock_hint.pixels=heap_caps_calloc(1,448u*72u/2u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(s_lock_hint.pixels) {
                EpdRect r=s_lock_hint.area;
                for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x) {
                    size_t i=(size_t)y*r.width+x;
                    s_lock_hint.pixels[i/2]|=(lock_hint_pixel(fb,r.x+x,r.y+y)>>4)<<((i&1)*4);
                }
                ui_fill_round_rect(fb,r,28,UI_GRAY_WHITE);
                ui_draw_round_rect(fb,r,28,UI_GRAY_BLACK);
                ui_text_fixed_vc(fb,w/2,r.y+r.height/2,28,"双击电源键解锁",EPD_DRAW_ALIGN_CENTER,false);
                lock_hint_present();
            }
        }
    }
    if(s_lock_hint.until && now>=s_lock_hint.until)lock_hint_finish(true);
}


static void lock_hint_begin(EpdiyHighlevelState *hl) {
    s_lock_hint_hl=s_boot_lock_active&&!s_boot_lock_known?NULL:hl;
    s_lock_hint.serial=read_pico_pmu_lock_key_serial();
}
static void lock_hint_end(void) {
    lock_hint_finish(false);
    s_lock_hint_hl=NULL;
}

// 锁屏策略可保持浅睡或到时关机；USB/输入和交接失败保持锁定。
// Lock policy may remain in light sleep or power off on deadline; failed handoffs retain the gate.
app_wake_source_t app_light_sleep_wait_timed(sc7a20h_handle_t acc,uint32_t timeout_ms) {
    if (!(book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000))) return APP_WAKE_TIMEOUT;
    const int64_t start=esp_timer_get_time();
    const int64_t deadline=timeout_ms?start+(int64_t)timeout_ms*1000:0;
    bool pickup=acc && app_settings_pickup_wake();
    sc7a20h_sample_t last={0}; bool last_valid=false;
    int64_t motion_at=start;
    if(pickup) sc7a20h_power_up(acc);
    else if(acc) (void)sc7a20h_shared_irq_idle(acc);
    metalio_frontlight(0,50);
    app_wake_source_t wake=APP_WAKE_NONE;
    bool usb_was_blocked=metalio_usb_blocks_sleep();
    const int64_t off_delay=app_settings_keep_light_sleep()?0:
        app_settings_staged_shutdown()?APP_LOCK_OFF_AFTER_US:1;
    int64_t off_at=s_lock_sleep_touch&&!timeout_ms&&off_delay ? start+off_delay : 0;
    int64_t off_retry_at=off_at;
    bool frame_marked=false,frame_mark_attempted=false;
    unsigned light_failures=0;
    int64_t light_retry_at=0;
    ESP_LOGI(TAG,"S31 lock wait: off_delay_ms=%lld usb=%u pickup=%u checkpoint=%u",
        (long long)(off_delay/1000),(unsigned)usb_was_blocked,(unsigned)pickup,(unsigned)s_lock_deep_ready);
    for(;;) {
        if(lock_take_key_wakeup()) { wake=APP_WAKE_KEY; break; }
        bool usb=metalio_usb_blocks_sleep();
        if (usb && !usb_was_blocked) metalio_usb_serial_reconnect();
        // 充电暂缓关机；拔线后重新计满十分钟，不能执行充电期间已过期的截止时间。
        // Charging defers shutdown; unplugging starts a fresh ten-minute interval.
        if (usb && off_at) off_retry_at=off_at=esp_timer_get_time()+off_delay;
        if (!usb && usb_was_blocked && off_at)
            off_retry_at=off_at=esp_timer_get_time()+off_delay;
        usb_was_blocked=usb;
        lock_resume_io_service();
        lock_resume_service();
        lock_hint_service();
        if (!s_lock_hint.pending_at && !s_lock_hint.until && s_lock_sleep_touch && !pickup && !usb) {
            if(off_at && esp_timer_get_time()>=off_retry_at && s_lock_deep_ready) {
                if(s_lock_frame_cached && !lock_pin_enabled() && !frame_marked && !frame_mark_attempted) {
                    frame_mark_attempted=true;
                    frame_marked=pico_boot_mark_frame_cached()==ESP_OK;
                }
                esp_err_t err=app_enter_host_sleep(APP_SLEEP_OFF);
                (void)lock_resume_media_io(0);
                // 关机未完成仍保持锁定；30秒后重试，不因卡未挂载永远禁用关机。
                // A failed power-off stays locked and retries after30s, without requiring a mounted card.
                off_retry_at=esp_timer_get_time()+INT64_C(30000000);
                ESP_LOGW(TAG,"S31 lock power-off deferred: %s; retry_ms=30000",esp_err_to_name(err));
            }
            if(!s_lock_io_pending && esp_timer_get_time()>=light_retry_at) {
                bool key=false;
                int64_t wake_by=deadline;
                if(off_at && s_lock_deep_ready && (!wake_by || off_retry_at<wake_by)) wake_by=off_retry_at;
                bool off_deadline=off_at && s_lock_deep_ready && wake_by==off_retry_at;
                if(off_deadline && s_lock_frame_cached && !lock_pin_enabled() && !frame_marked && !frame_mark_attempted) {
                    frame_mark_attempted=true;
                    frame_marked=pico_boot_mark_frame_cached()==ESP_OK;
                }
                esp_err_t err=lock_light_sleep_once(acc,s_lock_sleep_touch,&key,wake_by,off_deadline);
                if(key) { wake=APP_WAKE_KEY; break; }
                if(off_deadline && err==ESP_ERR_TIMEOUT && esp_timer_get_time()>=off_retry_at)
                    off_retry_at=esp_timer_get_time()+INT64_C(30000000);
                // 为按键去抖/双击和提示保留窗口；一次中断或I/O失败不能整晚禁止浅睡。
                // Leave time for debounce/double-click/hints; one IRQ or IO failure must not disable sleep all night.
                static const unsigned retry_ms[]={1000,3000,10000,30000};
                unsigned delay=1000;
                if(err==ESP_OK) light_failures=0;
                else {
                    unsigned step=light_failures<4?light_failures++:3;
                    delay=retry_ms[step];
                    ESP_LOGW(TAG,"S31 light deferred: %s; retry_ms=%u",esp_err_to_name(err),delay);
                }
                light_retry_at=esp_timer_get_time()+(int64_t)delay*1000;
            }
        }
        int64_t now_us=esp_timer_get_time();
        if(pickup && now_us>=motion_at) {
            motion_at=now_us+250000;
            sc7a20h_sample_t now;
            // 仅比较新的完整样本，降低采样频率；短按由原厂TCA后台任务去抖。
            // Compare fresh complete samples at 4Hz; vendor TCA background sampling debounces short keys.
            if(sc7a20h_read(acc,&now)==ESP_OK && (now.status&0x08)) {
                if(last_valid && (abs(now.x_mg-last.x_mg)+abs(now.y_mg-last.y_mg)+abs(now.z_mg-last.z_mg)>550)) { wake=APP_WAKE_PICKUP; break; }
                last=now;last_valid=true;
            }
        }
        if(deadline && esp_timer_get_time()>=deadline && !usb) { wake=APP_WAKE_TIMEOUT; break; }
        read_pico_clear_ioe_int();
        // 不重复操作低功耗/唤醒寄存器；每轮阻塞以保证按键、空闲任务和看门狗得到调度。
        // Avoid repeated sleep/wake register transitions; block each loop for key/idle/watchdog scheduling.
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    lock_hint_finish(wake!=APP_WAKE_KEY);
    if(pickup) (void)sc7a20h_shared_irq_idle(acc);
    if (wake != APP_WAKE_TIMEOUT) metalio_frontlight_restore();
    app_settings_set_last_wake((uint8_t)(wake==APP_WAKE_TIMEOUT?APP_WAKE_NONE:wake));
    ESP_LOGI(TAG,"S31 retained lock exit cause=%u elapsed_ms=%lld irq=%d",(unsigned)wake,
        (long long)((esp_timer_get_time()-start)/1000),read_pico_ioe_int_level());
    return wake;
}

app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc) {
    return app_light_sleep_wait_timed(acc, 0);
}

static void lock_restore_key_wake(void) {
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t err=metalio_restore_key_io();
    read_pico_pmu_keys_resume();
    if(err!=ESP_OK) ESP_LOGE(TAG,"S31 sleep input restore: %s",esp_err_to_name(err));
}

static bool lock_prepare_key_wake(void) {
    if(read_pico_pmu_keys_pause()!=ESP_OK) return false;
    esp_err_t err=metalio_prepare_key_wake();
    bool idle=false;
    // 对齐原厂 WaitIntLineHigh 的有界等待；确认电源键和共享低有效线已释放。
    // Follow the vendor bounded WaitIntLineHigh check; confirm key and shared active-low IRQ release.
    for(unsigned tries=0;err==ESP_OK && tries<30;tries++) {
        uint16_t inputs;
        err=metalio_io_read(&inputs);
        if(err==ESP_OK && (inputs&(1u<<15)) && read_pico_ioe_int_level()!=0) { idle=true; break; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if(idle) {
        err=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        if(err==ESP_OK) err=esp_sleep_enable_ext1_wakeup_io(1ULL<<READ_PICO_IOE_INT_GPIO,ESP_EXT1_WAKEUP_ANY_LOW);
    }
    if(err==ESP_OK && idle) return true;
    ESP_LOGW(TAG,"S31 deep sleep deferred: shared IRQ=%d result=%s",read_pico_ioe_int_level(),esp_err_to_name(err));
    lock_restore_key_wake();
    return false;
}

// 中文：按官方 power_hw 的顺序执行；保留PSRAM/排版/字体缓存，定时唤醒不解锁或点灯。
// English: Follow vendor power_hw ordering; timer wakes retain RAM/layout/font caches and never unlock/light.
// 维护切片不跨越十分钟交接点；定时到达只恢复外设，不作为解锁动作。
// Maintenance slices cannot overrun the ten-minute handoff; a deadline restores peripherals, never unlocks.
static uint64_t lock_light_sleep_slice_us(int64_t wake_by) {
    int64_t remaining=wake_by?wake_by-esp_timer_get_time():INT64_C(60000000);
    if(remaining<1) return 1;
    return remaining<INT64_C(60000000)?(uint64_t)remaining:UINT64_C(60000000);
}
// TF/字体已同步关闭且电源轨仍停放时直接断电，不先恢复LCD/TF/前光。
// With media already closed/synced and rails parked, power off without reviving LCD/SD/light.
static esp_err_t lock_power_off_parked(void) {
    esp_err_t rtc=app_sync_time_to_pmu();
    if(rtc!=ESP_OK && rtc!=ESP_ERR_INVALID_STATE) {
        vTaskDelay(pdMS_TO_TICKS(20));rtc=app_sync_time_to_pmu();
    }
    if(rtc!=ESP_OK) ESP_LOGW(TAG,"S31 parked off RTC: %s; keep existing time",esp_err_to_name(rtc));
    if(metalio_usb_blocks_sleep() || metalio_confirm_power_key())return ESP_ERR_INVALID_STATE;
    metalio_frontlight(0,50);
    metalio_poweroff();
    vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_ERR_TIMEOUT; // 实际断电不返回。/ An actual power cut never returns.
}
static esp_err_t lock_light_sleep_once(sc7a20h_handle_t acc,cst836u_handle_t touch,bool *key_wake,int64_t wake_by,bool off_at_deadline) {
    *key_wake=false;
    if (!touch || metalio_usb_blocks_sleep() || usb_storage_active() || pico_online_busy())
        return ESP_ERR_INVALID_STATE;
    read_pico_sd_info_t info={0};
    // 挂载/写卡事务仍需等待；无卡或已挂载失败不等于有活动I/O。
    // Defer active mount/write transactions; absent or failed media is not active IO.
    esp_err_t media_state=read_pico_sd_get_info(&info);
    if(media_state==ESP_ERR_NOT_FINISHED || (media_state!=ESP_OK && info.mounted)) return media_state;
    // 浅睡准备与恢复全程会使用 I2C/电源轨；先挂起充电监控，避免其插入这些事务。
    // Light-sleep preparation and restoration use I2C and rails; suspend the charger monitor first
    // so it cannot interleave with those transactions.
    metalio_charger_suspend();
    bool touch_asleep=false,sd_closed=false,lcd_stopped=false,keys_ready=false,rails_cut=false,pins_prepared=false;
    uint16_t saved_rails=0;
    bool key_irq=false,power_confirmed=false;
    s_lock_io_no_media=!info.mounted && !s_lock_io_pending;
    s_lock_io_pending=true;
    esp_err_t err=book_suspend_io();
    if(err==ESP_OK) err=ttf_font_suspend_io();
    if(err==ESP_OK) {
        err=cst836u_set_mode(touch,CST836U_MODE_DEEPSLEEP);
        touch_asleep=err==ESP_OK;
    }
    if(err==ESP_OK) { err=read_pico_sd_sync(); sd_closed=err==ESP_OK; }
    if(err==ESP_OK) {
        metalio_haptic_stop();
        metalio_frontlight(0,50);
        epd_poweroff();
        vTaskDelay(pdMS_TO_TICKS(120));
        epd_lcd_deinit(); lcd_stopped=true;
        pins_prepared=true;
        err=metalio_prepare_light_sleep_pins();
        if(err==ESP_OK && acc) err=sc7a20h_shared_irq_idle(acc);
    }
    // SD/屏总线准备期间仍由正常按键任务采样，不吞掉这段时间的新短按。
    // Keep clicks sampled during SD/panel preparation instead of masking them away.
    if(err==ESP_OK && lock_take_key_wakeup()) {
        *key_wake=true; err=ESP_ERR_INVALID_STATE;
    }
    if(err==ESP_OK) { keys_ready=lock_prepare_key_wake(); if(!keys_ready) err=ESP_ERR_INVALID_STATE; }
    if(err==ESP_OK) err=esp_sleep_enable_timer_wakeup(lock_light_sleep_slice_us(wake_by));
    if(err==ESP_OK && metalio_usb_blocks_sleep()) err=ESP_ERR_INVALID_STATE;
    if(err==ESP_OK) {
        rails_cut=true; err=metalio_sleep_cut_rails(&saved_rails);
    }
    if(err==ESP_OK) {
        err=metalio_hold_for_light_sleep();
    }
    if(err==ESP_OK) {
        ESP_LOGI(TAG,"S31 light sleep: slice<=60s; handles paused, RAM/font/index retained");
        unsigned maintenance=0;
        for(;;) {
            if(wake_by && esp_timer_get_time()>=wake_by) break;
            err=metalio_light_sleep_start();
            if(err!=ESP_OK) break;
            uint32_t causes=esp_sleep_get_wakeup_causes();
            key_irq=(causes & (1u<<ESP_SLEEP_WAKEUP_EXT1))!=0;
            // 恢复方向和慢挂卡之前验证P17；定时/USB/共享线毛刺不能补造解锁事件。
            // Verify P17 before direction/media restoration; timers, USB and shared-line glitches cannot synthesize unlocks.
            if(key_irq)power_confirmed=metalio_confirm_power_key();
            if(key_irq || !(causes & (1u<<ESP_SLEEP_WAKEUP_TIMER)) ||
                (wake_by && esp_timer_get_time()>=wake_by)) break;
            // 定时切片仅检查USB和共享唤醒线；保持TF/字体/LCD和电源轨关闭。
            // Timer slices check USB and the shared wake line with media/LCD/rails still suspended.
            ++maintenance;
            if(metalio_usb_blocks_sleep() || read_pico_ioe_int_level()==0) break;
            vTaskDelay(pdMS_TO_TICKS(30));
            if(read_pico_ioe_int_level()==0) break;
            err=esp_sleep_enable_timer_wakeup(lock_light_sleep_slice_us(wake_by));
            if(err!=ESP_OK) break;
        }
        ESP_LOGI(TAG,"S31 light wake: maintenance=%u ext1=%u power_confirmed=%u result=%s",
            maintenance,(unsigned)key_irq,(unsigned)power_confirmed,esp_err_to_name(err));
    }
    if(err==ESP_OK && off_at_deadline && wake_by && esp_timer_get_time()>=wake_by &&
       sd_closed && rails_cut && pins_prepared && keys_ready && !key_irq && !metalio_usb_blocks_sleep()) {
        power_confirmed=metalio_confirm_power_key();
        if(power_confirmed)key_irq=true;
        else {
            err=lock_power_off_parked();
            // 断电未完成才恢复外设，仍保持锁屏；交接期间的新按键继续保留。
            // Only a failed cut restores peripherals, retaining the lock and new physical input.
            power_confirmed=metalio_confirm_power_key();
            if(power_confirmed)key_irq=true;
        }
    }
    if(pins_prepared) metalio_release_light_sleep_pins();
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if(rails_cut) {
        esp_err_t restore=metalio_sleep_restore_rails(saved_rails);
        if(err==ESP_OK) err=restore;
        metalio_frontlight(0,50);
    }
    if(keys_ready) {
        lock_restore_key_wake();
        // 只补记已确认且在恢复期间松开的真实按键；共享IRQ本身不提供解锁授权。
        // Retain only a confirmed press released during restoration; a shared IRQ alone cannot authorize unlock.
        if(power_confirmed) read_pico_pmu_latch_power_wakeup();
    }
    const int64_t restore_at=esp_timer_get_time();
    if(sd_closed) (void)read_pico_sd_remount();
    if(lcd_stopped) read_pico_epd_restore_lcd();
    if(touch_asleep) {
        esp_err_t restore=cst836u_wake(touch);
        if(err==ESP_OK) err=restore;
    }
    metalio_haptic_resume();
    // 恢复输入先于TF；正常卡短暂等待，慢卡由已有主循环重试，不阻塞解锁十五秒。
    // Restore input before TF; briefly allow normal media, then let the main loop retry slow media.
    esp_err_t restored_io=lock_resume_media_io(500);
    if(restored_io!=ESP_OK)
        ESP_LOGW(TAG,"S31 wake media pending: %s",esp_err_to_name(restored_io));
    ESP_LOGI(TAG,"S31 wake peripherals_ms=%lld media=%s",
        (long long)((esp_timer_get_time()-restore_at)/1000),esp_err_to_name(restored_io));
    // GPIO仅唤醒CPU，双击才解锁；未完成时回到保留待机收齐按键。/ GPIO wakes only the CPU; retain standby until the sampled double click completes.
    if(key_irq){
        if(lock_take_key_wakeup())*key_wake=true;
        else if(err==ESP_OK)err=ESP_ERR_INVALID_STATE;
    }
    // 外设与按键恢复完成后再恢复充电监控，避免其插入恢复期的 I2C 事务。
    // Resume the charger monitor only after peripheral/key restoration so it cannot interleave.
    metalio_charger_resume();
    // 恢复失败仍保持锁屏；仅真正的电源键可交给PIN/解锁逻辑。
    // Restore failure retains the lock; only a real power wake reaches PIN/unlock handling.
    ESP_LOGI(TAG,"S31 light restore: result=%s power_wake=%u",esp_err_to_name(err),*key_wake);
    return err;
}

// 关机只关闭文件，不切换/重载字体；硬件交接失败时仍恢复同一字体与锁屏。
// Power-off closes handles without selecting/loading fonts; a failed handoff restores the same face and lock.
static esp_err_t lock_power_off(void) {
    esp_err_t rtc=app_sync_time_to_pmu();
    if(rtc!=ESP_OK && rtc!=ESP_ERR_INVALID_STATE) {
        vTaskDelay(pdMS_TO_TICKS(20));
        rtc=app_sync_time_to_pmu();
    }
    if(rtc!=ESP_OK) ESP_LOGW(TAG,"S31 power-off RTC sync: %s; retain existing RTC time",esp_err_to_name(rtc));
    read_pico_sd_info_t info={0};
    (void)read_pico_sd_get_info(&info);
    if(!s_lock_io_pending)s_lock_io_no_media=!info.mounted;
    s_lock_io_pending=true;
    esp_err_t err=book_suspend_io();
    if(err==ESP_OK)err=ttf_font_suspend_io();
    bool sd_closed=false;
    if(err==ESP_OK) { err=read_pico_sd_sync();sd_closed=err==ESP_OK; }
    // 关机事务（按键暂停、EPD 掉电、扩展口断电）前挂起充电监控；失败回滚后恢复。
    // Suspend the charger monitor before the key pause/EPD rail/expander power-off transaction;
    // resume it on failure rollback.
    metalio_charger_suspend();
    bool keys_paused=false;
    if(err==ESP_OK) {
        err=read_pico_pmu_keys_pause();
        keys_paused=err==ESP_OK;
    }
    if(err==ESP_OK) {
        metalio_haptic_stop();
        metalio_frontlight(0,50);epd_poweroff();
        metalio_poweroff();
        vTaskDelay(pdMS_TO_TICKS(1000));
        err=ESP_ERR_TIMEOUT;
    }
    if(keys_paused)read_pico_pmu_keys_resume();
    if(sd_closed)(void)read_pico_sd_remount();
    (void)lock_resume_media_io(500);
    metalio_haptic_resume();
    metalio_charger_resume();
    return err;
}

esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {
    if (!(book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000))) return ESP_ERR_NOT_FINISHED;
    if (mode != APP_SLEEP_OFF && mode != APP_SLEEP_DEEP) return ESP_ERR_INVALID_ARG;
    if (mode == APP_SLEEP_DEEP && metalio_usb_blocks_sleep()) return ESP_ERR_INVALID_STATE;
    if (pico_online_busy() || !read_pico_transfer_pause_for_sleep()) return ESP_ERR_NOT_FINISHED;
    // 锁屏调用方已经持有无线暂停；失败时不能擅自恢复其会话。
    // An active lock owns the radio pause; failed deep admission must not release that ownership.
    bool restore_radios=s_lock_sleep_touch==NULL;
    esp_err_t err = ble_pt_suspend_for_sleep();
    if (err != ESP_OK) { if(restore_radios) read_pico_transfer_resume_after_sleep(); return err; }
    if(mode==APP_SLEEP_OFF) {
        err=lock_power_off();
        if(restore_radios) { ble_pt_resume_after_sleep(); read_pico_transfer_resume_after_sleep(); }
        return err;
    }
    if (mode == APP_SLEEP_DEEP && !lock_prepare_key_wake()) {
        if(restore_radios) { ble_pt_resume_after_sleep(); read_pico_transfer_resume_after_sleep(); }
        return ESP_ERR_INVALID_STATE;
    }
    // 深睡准入完成、电源/总线事务开始前挂起充电监控；成功入睡由下次启动重新初始化，
    // 失败回滚必须在本函数返回前恢复，避免监控永久停摆。
    // Suspend the charger monitor once deep-sleep admission passed and the power/bus transaction
    // starts; a real sleep re-initializes on next boot, while a failed rollback must resume it here.
    if (mode == APP_SLEEP_DEEP) metalio_charger_suspend();
    (void)app_sync_time_to_pmu();
    app_lock_font_t saved_font = {0};
    app_font_begin_lock(&saved_font);
    (void)ttf_font_open_builtin();
    if(mode==APP_SLEEP_DEEP) {
        s_lock_io_pending=true;
        err=ttf_font_suspend_io();
    }
    if(err==ESP_OK) err = read_pico_sd_sync();
    if (err != ESP_OK) {
        if (mode == APP_SLEEP_DEEP) {
            lock_restore_key_wake();
            (void)lock_resume_media_io(15000);
            metalio_charger_resume();
        }
        app_font_end_lock(&saved_font);
        if(restore_radios) { ble_pt_resume_after_sleep(); read_pico_transfer_resume_after_sleep(); }
        return err;
    }
    metalio_frontlight(0, 50); epd_poweroff();
    if (mode == APP_SLEEP_DEEP) {
        // 复用已验证的原厂浅睡断轨/总线准备，避免仅停CPU而外设仍带电。
        // Reuse validated vendor light-sleep rails/bus preparation so peripherals are parked as well as the CPU.
        bool pins_prepared=false,rails_cut=false,touch_asleep=false;
        uint16_t saved_rails=0;
        if(s_lock_sleep_touch) {
            err=cst836u_set_mode(s_lock_sleep_touch,CST836U_MODE_DEEPSLEEP);
            touch_asleep=err==ESP_OK;
        }
        epd_lcd_deinit();
        if(err==ESP_OK) { pins_prepared=true;err=metalio_prepare_light_sleep_pins(); }
        if(err==ESP_OK && (metalio_usb_blocks_sleep() || read_pico_ioe_int_level()==0)) err=ESP_ERR_INVALID_STATE;
        if(err==ESP_OK) { rails_cut=true;err=metalio_sleep_cut_rails(&saved_rails); }
        if(err==ESP_OK) {
            metalio_hold_for_deep_sleep();
            esp_deep_sleep_start();
        }
        if(pins_prepared) metalio_release_light_sleep_pins();
        if(rails_cut) (void)metalio_sleep_restore_rails(saved_rails);
        metalio_frontlight(0,50);
        lock_restore_key_wake();
        (void)read_pico_sd_remount();
        (void)lock_resume_media_io(15000);
        read_pico_epd_restore_lcd();
        if(touch_asleep) (void)cst836u_wake(s_lock_sleep_touch);
        app_font_end_lock(&saved_font);
        metalio_haptic_resume();
        metalio_charger_resume();
        if(restore_radios) { ble_pt_resume_after_sleep(); read_pico_transfer_resume_after_sleep(); }
        return err;
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t app_sync_time_to_pmu(void) {
    time_t now = time(NULL);
    if (now < 946684800 || now > 4102444799U) {
        ESP_LOGW(TAG, "skip PMU time sync: system clock invalid");
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t seconds = (uint32_t)now;
    uint8_t payload[4] = {
        (uint8_t)(seconds & 0xff),
        (uint8_t)((seconds >> 8) & 0xff),
        (uint8_t)((seconds >> 16) & 0xff),
        (uint8_t)((seconds >> 24) & 0xff),
    };
    esp_err_t err = read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
    ESP_LOGI(TAG, "PMU time sync before power action: %s", esp_err_to_name(err));
    return err;
}

void app_restart_host(void) {
    (void)app_sync_time_to_pmu();
    ttf_font_open_builtin(); (void)read_pico_sd_sync();
    metalio_haptic_stop(); metalio_frontlight(0,50); epd_poweroff(); esp_restart();
}

static bool s_download_active;
static void download_trace(const char *stage,bool first) {
    char state[256];metalio_usb_describe(state,sizeof(state));
    ESP_LOGI(TAG,"USB download %s: %s",stage,state);
    // 只记录本次准备过程，覆盖旧记录，不在滑动/常规轮询中写卡。
    // Keep one bounded preparation trace; no writes during normal polling or dragging.
    (void)mkdir("/sdcard/.metalio_s31",0777);
    FILE *log=fopen("/sdcard/.metalio_s31/usb-download.log",first?"w":"a");
    if (log) {
        fprintf(log,"%s %s %s\n",esp_app_get_description()->version,stage,state);
        (void)fclose(log);
    }
}
bool app_download_active(void) { return s_download_active; }
void app_cancel_download(void) {
    if (!s_download_active) return;
    metalio_download_cancel();
    s_download_active=false;
    ble_pt_resume_after_sleep();
    read_pico_transfer_resume_after_sleep();
}
esp_err_t app_enter_download(void) {
    if (usb_storage_active()) return ESP_ERR_INVALID_STATE;
    if (s_download_active) {
        download_trace("before_recover",true);
        esp_err_t retry=metalio_request_download();
        download_trace(retry==ESP_OK?"waiting_host":"recovery_failed",false);
        if (retry!=ESP_OK) app_cancel_download();
        return retry;
    }
    if (!(book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000))) return ESP_ERR_NOT_FINISHED;
    if (!read_pico_transfer_pause_for_sleep()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err=ble_pt_suspend_for_sleep();
    if (err!=ESP_OK) { read_pico_transfer_resume_after_sleep(); return err; }
    download_trace("before_prepare",true);
    err=metalio_download_prepare();
    if (err!=ESP_OK) {
        ble_pt_resume_after_sleep(); read_pico_transfer_resume_after_sleep(); return err;
    }
    (void)app_sync_time_to_pmu();
    // 保持TF/字体/LCD和调度器，由电脑使用官方USJ控制线进入ROM；等待时仍可返回退出。
    // Retain TF/fonts/LCD and scheduling; the host enters ROM via official USJ control lines. Cancel stays usable.
    err=metalio_request_download();
    if (err!=ESP_OK) {
        download_trace("recovery_failed",false);
        metalio_download_cancel();
        ble_pt_resume_after_sleep(); read_pico_transfer_resume_after_sleep();
        return err;
    }
    s_download_active=true;
    download_trace("waiting_host",false);
    return ESP_OK;
}

static void lock_wait_for_unlock(sc7a20h_handle_t acc) {
    (void)acc;
    for(;;) {
        app_wake_source_t wake=app_light_sleep_wait_timed(NULL,APP_LOCK_IDLE_TIMEOUT_MS);
        if(wake==APP_WAKE_KEY || wake==APP_WAKE_PICKUP) return;
        // 超时不能触发重启或解锁；稳定性版继续保留当前锁屏内存。
        // A timeout cannot reboot or unlock; retain the current lock context in this stability build.
    }
}

bool lock_pin_grace_active(void) {
    if(!lock_pin_enabled() || !lock_pin_available() || !app_settings_pin_grace_minutes())return false;
    const time_t now=time(NULL);
    const uint32_t locked_at=app_settings_pin_locked_at();
    if(now<1704067200 || now>4102444799LL || locked_at<1704067200 || (uint64_t)now<locked_at)return false;
    return (uint64_t)now-locked_at<(uint64_t)app_settings_pin_grace_minutes()*60u;
}

// 唤醒只清完整物理目标，不裁成UI区域，也不修改面板参考帧或增加全刷。
// Clear the full physical wake target, without UI cropping, reference changes or an extra refresh.
static void lock_clear_wake_target(EpdiyHighlevelState *hl) {
    memset(epd_hl_get_framebuffer(hl), 0xFF, (size_t)(epd_width() / 2) * epd_height());
}

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc, cst836u_handle_t touch,
    bool reader_background
) {
    const int64_t lock_at = esp_timer_get_time();
    // 先灭灯并消费本次锁屏动作；后续生成/推屏期间的新短按必须保留给唤醒。
    // Turn the lamp off and consume the initiating action before composition; retain later wake presses.
    metalio_frontlight(0, 50);
    if (!s_lock_feedback_started) read_pico_pmu_drain_events();
    s_lock_feedback_started = false;
    // Refuse to interrupt an active upload. The caller repaints its page when
    // this function returns, and the user can lock again after the transfer.
    bool radio_paused = read_pico_transfer_pause_for_sleep();
    const int64_t transfer_at = esp_timer_get_time();
    if (!radio_paused) {
        metalio_frontlight_restore();
        pico_boot_clear_resume();
        ESP_LOGW(TAG, "lock postponed while transfer is busy");
        return;
    }
    if (ble_pt_suspend_for_sleep()!=ESP_OK) {
        read_pico_transfer_resume_after_sleep();
        metalio_frontlight_restore();
        pico_boot_clear_resume();
        ESP_LOGW(TAG,"S31 lock postponed: BLE shutdown failed"); return;
    }
    const int64_t radio_at = esp_timer_get_time();
    uint8_t* framebuffer = epd_hl_get_framebuffer(hl);
    // 仅清目标内存，旧页面在拼贴准备期间仍留在屏幕；不提前清白物理面板。
    // Clear only target memory; retain the old physical page until the complete collage is ready.
    epd_poweroff();
    uint8_t lock_style = app_settings_lock_style();
    // 壁纸先在内存准备完整画面，再从真实back一次全像素GC48；不提前推白，也不伪造白底历史。
    // 票根/拼贴保留既有擦白；透明PNG仍先从最后成功帧合成，再由图片出口清理物理旧态。
    // Prepare wallpaper in memory and present once with full-pixel GC48 from actual back history.
    // Retain ticket/collage whitening and transparent-PNG composition/optical cleanup.
    if (lock_style == 0 || lock_style == 2) guard_draw_result(hl, update_display_white(hl));
    s_lock_frame_cached=false;
    // 透明书封以最后成功推屏的真实参考作为底图，不取未显示的目标。
    // Overlay the last successfully presented reference, never an uncommitted target.
    if(lock_style==3)memcpy(framebuffer,hl->back_fb,(size_t)epd_width()*epd_height()/2);
    else epd_hl_set_all_white(hl);
    bool ticket = lock_style == 0;
    bool system_lock = ticket || lock_style == 2 || lock_pin_enabled();
    app_lock_font_t lock_font = {0};
    if (system_lock) app_font_begin_lock(&lock_font);
    // 阅读票根独占锁屏画布：始终先画最近书籍封面，再叠票根；自定义壁纸只属于壁纸模式。
    // Ticket mode owns the lock canvas: book cover first, ticket on top. Custom wallpaper is wallpaper-only.
    bool lock_drawn = ticket
        ? book_ticket_draw(framebuffer, reader_background)
        : lock_style == 2 ? book_lock_collage_prepare(framebuffer) :
          lock_style == 3 ? book_lock_bookmark_draw(framebuffer,app_settings_bookmark_path()) : draw_wallpaper(framebuffer);
    if (!lock_drawn) {
        ui_lock_fallback_draw(framebuffer);
    }
    const int64_t draw_at = esp_timer_get_time();
    enum EpdDrawError result = lock_present_art(hl,lock_style);
    if (result != EPD_DRAW_SUCCESS) {
        guard_draw_result(hl, result);
        result = lock_present_art(hl,lock_style);
    }
    const int64_t presented_at = esp_timer_get_time();
    ESP_LOGI(TAG, "S31 lock style=%u GC48 result=%d transfer_ms=%lld ble_ms=%lld prepare_ms=%lld push_ms=%lld visible_ms=%lld", lock_style,
        (int)result, (long long)((transfer_at - lock_at) / 1000),
        (long long)((radio_at - transfer_at) / 1000), (long long)((draw_at - radio_at) / 1000),
        (long long)((presented_at - draw_at) / 1000), (long long)((presented_at - lock_at) / 1000));
    if (result != EPD_DRAW_SUCCESS && !lock_pin_enabled()) {
        // 推屏失败不宣布锁屏完成；恢复本页与灯光，允许再次尝试。
        // A failed presentation does not enter sleep; restore the page and lamp for another attempt.
        epd_poweroff();
        if (system_lock) app_font_end_lock(&lock_font);
        read_pico_transfer_resume_after_sleep();
        ble_pt_resume_after_sleep();
        metalio_frontlight_restore();
        pico_boot_clear_resume();
        return;
    }
    // 中文：只缓存成功显示的完整锁屏，必须在密码层覆盖前提交。
    // English: Commit only successfully presented lock art, before the PIN overlay.
    if (lock_drawn && result == EPD_DRAW_SUCCESS) {
        if (lock_style == 2) book_lock_collage_cache_commit(framebuffer);
        if (lock_style == 1) ui_wallpaper_cache_commit(framebuffer);
        s_lock_frame_cached=lock_screen_save(framebuffer);
    }
    // 密码启用时即使显示失败也保持验证边界，绝不回到未验证页面。
    // A display failure must never return an unauthenticated page when the PIN is enabled.
    // 锁屏按所选策略定时或直接硬件关机；始终保留锁图、密码和阅读检查点。
    // Apply the selected timed/immediate hardware-off policy, retaining artwork, PIN and reader checkpoints.
    ESP_LOGI(TAG, "lock LIGHT");
    epd_poweroff();
    s_lock_sleep_touch=touch;
    lock_hint_begin(hl);
    if(lock_pin_enabled()) {
        time_t now=time(NULL);
        app_settings_record_pin_lock(now>=1704067200 && now<=4102444799LL?(uint32_t)now:0);
        lock_screen_authenticate(hl,touch,acc,false);
    }
    else lock_wait_for_unlock(acc);
    lock_hint_end();
    s_lock_sleep_touch=NULL;
    s_lock_deep_ready=false;s_lock_frame_cached=false;
    pico_boot_clear_resume();
    (void)lock_resume_media_io(0);
    if (system_lock) app_font_end_lock(&lock_font);
    read_pico_transfer_resume_after_sleep();
    ble_pt_resume_after_sleep();

    // 仅清目标画布，保留面板和真实参考帧；当前页接着单次重绘，无额外白屏。
    // Clear only the target canvas, retaining panel/reference history for the next page presentation.
    read_pico_pmu_drain_events();
    lock_clear_wake_target(hl);
    read_pico_pmu_drain_events();
    if (ignore_until_ms) {
        *ignore_until_ms = esp_timer_get_time() / 1000 + APP_LOCK_IGNORE_BOOT_MS;
    }
    ESP_LOGI(TAG, "unlocked");
}

void app_lock_boot_gate(EpdiyHighlevelState *hl, cst836u_handle_t touch, sc7a20h_handle_t acc) {
    lock_hint_begin(hl);
    s_boot_lock_draw=true;
    lock_resume_service();
    if (!lock_pin_enabled()) {
        if(pico_boot_has_resume() || pico_boot_lock_pending()) {
            pico_boot_ready();pico_boot_hold_resume();
            // 硬件上电且开机时确实采样到电源键按下（长按开机）就直接恢复，不再走单击/双击短按门禁；
            // 其他复位（软件复位、未确认的深睡唤醒）仍保留解锁门禁，密码验证不受影响。
            // A hardware power-on with a confirmed power-key press at boot (long-press boot) restores
            // directly and skips the single/double-click gate; other resets keep the gate and PIN
            // verification is unaffected.
            if(!s_boot_power_on || !metalio_boot_power_key()) {
                lock_wait_for_unlock(NULL);
            }
        }
        lock_hint_end();
        (void)app_lock_resume_frame(NULL);
        // 异常启动不自动续读，但真实解锁后可以消费已保留的锁定标记。
        // Abnormal startup cannot resume books, but verified unlock can consume the retained lock marker.
        if(pico_boot_lock_pending() && !pico_boot_has_resume())pico_boot_clear_resume();
        return;
    }
    // 卡挂载有界等待仅为了字体/锁屏图；没有卡仍必须验证。
    // Bound mount waiting for the face/art only; absent media still requires authentication.
    read_pico_sd_info_t info;
    for (unsigned i = 0; i < 150 && read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED; ++i)
        vTaskDelay(pdMS_TO_TICKS(20));
    app_lock_font_t saved; app_font_begin_lock(&saved);
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    if (!lock_screen_restore(fb)) {
        if(app_settings_lock_style()==3)memset(fb,255,(size_t)epd_width()*epd_height()/2);
        bool drawn = app_settings_lock_style() == 0 ? book_ticket_draw(fb, false) :
            app_settings_lock_style() == 2 ? book_lock_collage_prepare(fb) :
            app_settings_lock_style() == 3 ? book_lock_bookmark_draw(fb,app_settings_bookmark_path()) : draw_wallpaper(fb);
        if (!drawn) ui_lock_fallback_draw(fb);
        enum EpdDrawError result=lock_present_art(hl,app_settings_lock_style());
        guard_draw_result(hl,result);epd_poweroff();
        if(drawn && result==EPD_DRAW_SUCCESS)(void)lock_screen_save(fb);
    }
    lock_screen_authenticate(hl, touch, acc, true);
    lock_hint_end();
    (void)app_lock_resume_frame(NULL);
    app_font_end_lock(&saved);
    if(pico_boot_lock_pending() && !pico_boot_has_resume())pico_boot_clear_resume();
}

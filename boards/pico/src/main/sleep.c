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
 * 用户修订：其他样式/自动锁屏先准备完整前缓冲，再清屏并做原 GC16 输出，缩短白屏等待。
 * User revision: other styles and idle locks prepare the complete front buffer before the original clear/GC16 output, shortening white-screen waits.
 * 用户修订：拼贴的新整帧缓存移到完整显示之后写入，密码层覆盖前提交。
 * User revision: persist new collage frames after complete presentation and before PIN overlays.
 * 用户修订：主动拼贴锁屏立即清白并显示圆锁，随后只用一次原GC16输出完整拼贴；取消占位和四步显影。
 * User revision: manual collage locks promptly present white and a round lock, then present the complete collage once with original GC16; remove placeholders and four-pass reveal.
 * 用户修订：合入官方 a2a63a6 的外设停用、IOE 收口与 CPU 浅睡保持；保留十分钟浅睡、密码和阅读检查点。
 * User revision: port official a2a63a6 peripheral/IOE parking and CPU light-sleep retention while preserving ten-minute light sleep, PIN and reader checkpoints.
 * 冻结：沿用实际后缓冲，不把圆锁当白色；不增加帧副本，自动休眠/其他样式/失败回退保留原流程。
 * Frozen: Retain true history including the badge, add no frame copy and preserve idle/other-style/failure recovery.
 * 用户2026-11-04：透明书封正常锁屏直接从真实旧帧做一次GC16，省去三轮预清屏；绘图/输出失败与其他样式保留原清屏。
 * User2026-11-04: transparent covers use one GC16 from true history; preserve original clearing for art/output failures and other styles.
 * 用户2026-11-04追加：透明书封正常解锁保留实际显示的后缓冲，由主循环一次GC16恢复原页面；失败与其他样式保留原清屏。
 * User2026-11-04 follow-up: retain true history on transparent-cover unlock for one caller GC16 restore; failures and other styles retain clearing.
 * 用户2026-11-04追加：上述锁屏/浅睡解锁与网络后置扩展到Pico全部四种样式；拼贴反馈仅局部GC16圆锁，失败保留清屏恢复。
 * User2026-11-04 additionally extends single-pass lock/unlock and deferred networking to all four Pico styles; collage feedback uses a local GC16 badge, retaining failure clearing.
 * 用户2026-11-04透字反馈：透明书封入锁恢复一次GC16白目标再显示最终图；临时白画布用后释放，不改素材、缓存、解锁或其他样式。
 * User2026-11-04 ghosting report: transparent locks add one white GC16 target before final art; free the temporary white canvas and preserve assets, caching, unlocks and other styles.
 * 用户确认透明书封透字消失，要求其余样式同样处理：四种入锁均先白目标再最终图，素材/缓存/解锁与异常兜底保留。
 * After confirming the cover fix, the user requests the same protection for other styles: all four locks use white then final art, preserving assets, caches, unlocks and failure recovery.
 */
#include "ui_avatar.h"
#include "book_cover_jobs.h"

#include "sleep.h"
#include "boot_state.h"
#include "lock_pin.h"
#include "lock_screen.h"
#include "read_pico_sd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "app_font_context.h"
#include "book_cover.h"
#include "book_lock_collage.h"
#include "book_lock_bookmark.h"
#include "book_ticket.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "ota_online.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_private/sleep_cpu.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pmu_selftest.h"
#include "read_pico_board.h"
#include "read_pico_buzzer.h"
#include "ble_page_turner.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "sc7a20h_lab.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_image_dither.h"
#include "ui_wallpaper.h"
#include "ui_lock_feedback.h"

static const char* TAG = "read_pico";
static bool s_lock_feedback_visible, s_lock_feedback_failed;

#define APP_LOCK_LIGHT_SLEEP_MS (10U * 60U * 1000U)
#define APP_LOCK_KEY_POLL_US (2LL * 1000 * 1000)
#define APP_LOCK_STUCK_IRQ_US (3LL * 1000 * 1000)
#define APP_LOCK_SLEEP_ERROR_LIMIT 5
// PMU 确认后仍未掉 EN 时恢复调用方，禁止无限等待。/ Restore the caller if EN stays high after acknowledgement instead of waiting forever.
#define HOST_EN_DROP_WAIT_MS 5000


void app_lock_feedback(EpdiyHighlevelState *hl) {
    s_lock_feedback_visible = s_lock_feedback_failed = false;
    if (!hl || app_settings_lock_style() != 2 || pico_online_busy()) return;
    read_pico_transfer_status_t transfer = {0};
    read_pico_transfer_get_status(&transfer);
    if (transfer.state == READ_PICO_TRANSFER_UPLOADING) return;
    uint8_t *framebuffer = epd_hl_get_framebuffer(hl);
    if (!framebuffer || epd_rotated_display_width() < 97 || epd_rotated_display_height() < 97) return;
    display_main_transition_cancel();
    // 只在实际当前画面上覆盖圆锁；局部GC16保留灰阶，不先清白整屏。
    // Overlay the badge on true history with local GC16; never whiten the whole page first.
    memcpy(framebuffer,hl->back_fb,(size_t)epd_width()*epd_height()/2);
    EpdRect area = ui_lock_feedback_draw(framebuffer);
    s_lock_feedback_visible = update_display_area_full_with(hl, &E0470_WAVEFORM,
        MODE_GC16, area) == EPD_DRAW_SUCCESS;
    s_lock_feedback_failed = !s_lock_feedback_visible;
}

static bool draw_wallpaper(uint8_t *framebuffer) {
    const char *path = app_settings_wallpaper_path();
    return app_settings_lock_style() == 1 &&
        ui_wallpaper_prepare_lock(framebuffer,path);
}

static cst836u_handle_t s_tp;
static sc7a20h_handle_t s_acc;
static bool s_acc_was_on;

void app_sleep_bind(cst836u_handle_t tp, sc7a20h_handle_t acc) {
    s_tp = tp;
    s_acc = acc;
    // 没开 PM 时 IDF 不分配 CPU 保存区，浅睡里 CPU 不会掉电；这里手动申请一次。
    // Without PM, IDF never allocates CPU retention, so light sleep keeps the CPU powered; request it once here.
    esp_err_t err = sleep_cpu_configure(true);
    if (err != ESP_OK) ESP_LOGW(TAG, "CPU light-sleep power-down off: %s", esp_err_to_name(err));
}

// 触摸、加速度计都在常开 3V3 上，ESP 浅睡或 EN 拉低都不会断它们的电，必须主动送进最低功耗态。
// Touch and accelerometer sit on the always-on 3V3; neither light sleep nor EN low cuts them, so park them explicitly.
static void periph_suspend(bool host_off) {
    if (read_pico_buzzer_score_busy()) (void)read_pico_buzzer_score_stop();
    esp_err_t ioe = read_pico_ioe_sleep(host_off);
    if (ioe != ESP_OK) ESP_LOGW(TAG, "ioe sleep %s", esp_err_to_name(ioe));
    s_acc_was_on = sc7a20h_powered(s_acc);
    if (s_acc_was_on) (void)sc7a20h_power_down(s_acc);
    if (s_tp != NULL && cst836u_get_mode(s_tp) != CST836U_MODE_DEEPSLEEP) {
        esp_err_t err = cst836u_set_mode(s_tp, CST836U_MODE_DEEPSLEEP);
        if (err != ESP_OK) ESP_LOGW(TAG, "touch deepsleep %s", esp_err_to_name(err));
    }
}

// 深睡的触摸只认 TP_RST 复位；加速度计按睡前的配置重新上电。
// Deep-sleeping touch only wakes via TP_RST; the accelerometer comes back with its pre-sleep config.
static void periph_resume(void) {
    (void)read_pico_ioe_wake();
    if (s_tp != NULL && cst836u_get_mode(s_tp) == CST836U_MODE_DEEPSLEEP) {
        esp_err_t err = cst836u_wake(s_tp);
        if (err != ESP_OK) ESP_LOGW(TAG, "touch wake %s", esp_err_to_name(err));
    }
    if (s_acc_was_on) (void)sc7a20h_power_up(s_acc);
    s_acc_was_on = false;
}

// 单击提示只保留一个小区域；等待双击窗口结束才绘制，触摸仅临时唤醒三秒。
// Retain one small hint region; draw after the double-click window and wake touch for only three seconds.
static EpdiyHighlevelState *s_lock_hint_hl;
static EpdiyHighlevelState *s_boot_lock_hl;
static bool s_boot_lock_active, s_boot_lock_known, s_boot_lock_draw;

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
        bool drawn=style==0?book_ticket_draw(fb,false):style==2?book_lock_collage_draw(fb):draw_wallpaper(fb);
        app_font_end_lock(&saved);
        if(!drawn)return;
    }
    if(restored && pico_boot_resume_frame_cached())
        memcpy(hl->back_fb,fb,(size_t)epd_width()*epd_height()/2);
    else {
        enum EpdDrawError result=update_display_full(hl);
        guard_draw_result(hl,result);epd_poweroff();
        if(result!=EPD_DRAW_SUCCESS)return;
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
    int64_t pending_at, touch_until;
    bool touch_awake;
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
    if(s_lock_hint.touch_awake && s_tp) (void)cst836u_set_mode(s_tp,CST836U_MODE_DEEPSLEEP);
    s_lock_hint.touch_awake=false;s_lock_hint.pending_at=s_lock_hint.touch_until=0;
}

static void lock_hint_service(void) {
    if(!s_lock_hint_hl)return;
    int64_t now=esp_timer_get_time()/1000;
    uint32_t serial=read_pico_pmu_lock_key_serial();
    if(serial!=s_lock_hint.serial) {
        s_lock_hint.serial=serial;s_lock_hint.pending_at=now+650;
    }
    if(s_lock_hint.pending_at && now>=s_lock_hint.pending_at) {
        s_lock_hint.pending_at=0;s_lock_hint.touch_until=now+3000;
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
        if(!s_lock_hint.touch_awake && s_tp && cst836u_wake(s_tp)==ESP_OK)s_lock_hint.touch_awake=true;
    }
    if(s_lock_hint.touch_until && now>=s_lock_hint.touch_until)lock_hint_finish(true);
    if(s_lock_hint.touch_awake && s_tp) {
        cst836u_touch_t touch={0};
        // 窗口内点屏保留同一提示，不重绘、不累计延时，也绝不触发解锁。
        // Screen taps retain this hint without redraws or extending the window, and can never unlock.
        (void)cst836u_read(s_tp,&touch);
    }
}

// 深睡唤醒后的第一次等待只认一次按键：开机那次短按已经在电源交接时消耗掉了，
// 不能再要求双击，否则用户会感觉“多按一遍”。
// The first wait after a deep-sleep wake accepts a single press: the power-on press was already
// consumed by the power handoff, so requiring a double click would ask for one extra press.
static bool s_lock_wake_single_once;
void app_lock_wake_single_once(void) { s_lock_wake_single_once = true; }

static bool lock_wait_key(void) {
    const bool single = app_settings_wake_presses() == 1 || s_lock_wake_single_once;
    bool wake = single ? read_pico_pmu_take_key_wakeup_single()
                       : read_pico_pmu_take_key_wakeup();
    if(wake) {
        if(s_lock_wake_single_once && app_settings_wake_presses() != 1)
            s_lock_wake_single_once = false;
        return true;
    }
    lock_resume_service();lock_hint_service();return false;
}

static void lock_arm_ioe_wakeup(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << READ_PICO_IOE_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    read_pico_clear_ioe_int();
    gpio_wakeup_enable((gpio_num_t)READ_PICO_IOE_INT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

void app_lock_wait_key_idle(int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t high_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        read_pico_pmu_drain_events();
        read_pico_clear_ioe_int();
        bool held = false;
        if (read_pico_pmu_poll() == ESP_OK) {
            held = (read_pico_pmu_get()->key_state & 0x01) != 0;
        }
        bool int_high = gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) != 0;
        if (!held && int_high) {
            if (high_from == 0) high_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - high_from >= 150) return;
        } else {
            high_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

app_wake_source_t app_last_wake_source(void) {
    return (app_wake_source_t)app_settings_last_wake();
}

static void pickup_ack(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_read_events(acc, &ev);
    sc7a20h_ack_int(acc);
}

static bool pickup_ia(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_aoi_src_t aoi;
    if (sc7a20h_read_events(acc, &ev) != ESP_OK) return false;
    sc7a20h_aoi_decode(ev.aoi1_src, &aoi);
    return aoi.ia;
}

// 第一次低电平不够：高通余波还会再拉高。要连续安静一段时间才进浅睡。
// A first low is not enough: high-pass ringing can rise again. Wait for a quiet stretch before light sleep.
static bool pickup_wait_quiet(sc7a20h_handle_t acc, int quiet_ms, int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t low_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        if (sc7a20h_int1_level(acc) == 0) {
            if (low_from == 0) low_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - low_from >= quiet_ms) return true;
        } else {
            pickup_ack(acc);
            low_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

app_wake_source_t app_light_sleep_wait_timed(sc7a20h_handle_t acc, uint32_t timeout_ms) {
    bool covers_idle=book_cover_jobs_quiesce(2000);
    bool avatar_idle=ui_avatar_quiesce(2000);
    bool poll_only=!(covers_idle && avatar_idle);
    bool pickup = acc != NULL && app_settings_pickup_wake();
    bool acc_armed = false;
    const int64_t deadline_us = timeout_ms
        ? esp_timer_get_time() + (int64_t)timeout_ms * 1000 : 0;
    periph_suspend(false);
    lock_arm_ioe_wakeup();
    if (pickup) {
        sc7a20h_motion_cfg_t motion = SC7A20H_MOTION_DEFAULT();
        motion.ths_mg = 350;
        motion.duration = 3;
        sc7a20h_arm_pickup_wake(acc, &motion);
        acc_armed = true;
        pickup_ack(acc);
        if (!pickup_wait_quiet(acc, 400, 2000)) {
            pickup = false;
            ESP_LOGW(TAG, "pickup INT1 stuck high, key only");
        } else {
            sc7a20h_config_light_sleep_wakeup(acc);
            if (!pickup_wait_quiet(acc, 400, 1500)) {
                gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                pickup = false;
                ESP_LOGW(TAG, "pickup INT1 re-asserted, key only");
            } else {
                // 安静窗口里若 INT1 闪过，ESP 会留下高电平唤醒挂起，关掉再打开清掉。
                // If INT1 glitched in the quiet window, ESP keeps a high-level wake pending; disable then re-enable to clear it.
                gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                sc7a20h_config_light_sleep_wakeup(acc);
                if (sc7a20h_int1_level(acc) > 0) {
                    gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                    pickup = false;
                    ESP_LOGW(TAG, "pickup INT1 high at sleep, key only");
                } else {
                    ESP_LOGI(
                        TAG, "wait key GPIO%d or pickup GPIO%d",
                        READ_PICO_IOE_INT_GPIO, (int)sc7a20h_int1_gpio(acc)
                    );
                }
            }
        }
    } else {
        ESP_LOGI(TAG, "wait key on IOE_INT GPIO%d", READ_PICO_IOE_INT_GPIO);
    }

    app_wake_source_t wake = APP_WAKE_NONE;
    int64_t ioe_low_since_us = 0;
    unsigned sleep_errors = 0;
    esp_err_t last_sleep_error = ESP_OK;
    int64_t last_sleep_error_log_ms = 0;
    for (;;) {
        int64_t now_us = esp_timer_get_time();
        // Check the deadline before every interrupt recovery path. A stuck-low
        // IO expander must never bypass the ten-minute PMU handoff forever.
        // 每次处理中断前先检查期限，避免 IO 扩展器低电平让票根永远停住。
        if (deadline_us && now_us >= deadline_us) {
            wake = APP_WAKE_TIMEOUT;
            ESP_LOGI(TAG, "lock light sleep timeout after %u ms", (unsigned)timeout_ms);
            break;
        }
        // The PMU event is authoritative even if the IOE edge was missed.
        // Timer wakeups below provide a bounded fallback poll for that case.
        // 即使漏掉 IOE 中断，也由定时唤醒轮询 PMU 按键事件。
        if (lock_wait_key()) {
            wake = APP_WAKE_KEY;
            break;
        }
        if(poll_only) {vTaskDelay(pdMS_TO_TICKS(40));continue;}
        read_pico_clear_ioe_int();
        if (gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) == 0) {
            if (!ioe_low_since_us) ioe_low_since_us = now_us;
            if (now_us - ioe_low_since_us >= APP_LOCK_STUCK_IRQ_US) {
                // 中断或浅睡异常只能回退到轮询，不能冒充双击解锁。/ Fall back to polling on IRQ/sleep faults, never synthesize an unlock.
                ESP_LOGW(TAG, "lock IOE_INT stuck low; retain lock and poll key");
                poll_only=true;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        ioe_low_since_us = 0;
        if (pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
            continue;
        }
        int64_t sleep_us = APP_LOCK_KEY_POLL_US;
        if(s_lock_hint.pending_at || s_lock_hint.touch_until)sleep_us=100000;
        if (deadline_us) {
            int64_t remaining_us = deadline_us - esp_timer_get_time();
            if (remaining_us < sleep_us) sleep_us = remaining_us;
        }
        if (sleep_us <= 0) continue;
        esp_err_t timer_error = esp_sleep_enable_timer_wakeup((uint64_t)sleep_us);
        if (timer_error != ESP_OK) {
            ESP_LOGW(TAG, "lock sleep timer rejected: %s", esp_err_to_name(timer_error));
            if (++sleep_errors >= APP_LOCK_SLEEP_ERROR_LIMIT) {
                poll_only=true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        esp_err_t sleep_error = esp_light_sleep_start();
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        read_pico_clear_ioe_int();
        if (lock_wait_key()) {
            wake = APP_WAKE_KEY;
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            if (dt_ms < 80) {
                ESP_LOGW(TAG, "pickup ignored, sleep %lld ms", (long long)dt_ms);
                pickup_ack(acc);
                pickup_wait_quiet(acc, 400, 1500);
                continue;
            }
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
        }
        if (sleep_error != ESP_OK) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (sleep_error != last_sleep_error || now_ms - last_sleep_error_log_ms >= 30000) {
                ESP_LOGW(TAG, "lock light sleep rejected: %s, IOE_INT=%d",
                         esp_err_to_name(sleep_error),
                         gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO));
                last_sleep_error_log_ms = now_ms;
            }
            last_sleep_error = sleep_error;
            if (++sleep_errors >= APP_LOCK_SLEEP_ERROR_LIMIT) {
                ESP_LOGE(TAG, "lock light sleep repeatedly rejected; retain lock and poll key");
                poll_only=true;
            }
            // A rejected sleep must not spin at full CPU speed behind the static e-paper image.
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            last_sleep_error = ESP_OK;
            sleep_errors = 0;
        }
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
    if (acc_armed) {
        gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
        sc7a20h_events_t ev;
        sc7a20h_read_events(acc, &ev);
        sc7a20h_power_down(acc);
        sc7a20h_int1_begin(acc);
    }
    lock_hint_finish(wake!=APP_WAKE_KEY);
    periph_resume();
    app_settings_set_last_wake((uint8_t)(wake == APP_WAKE_TIMEOUT ? APP_WAKE_NONE : wake));
    return wake;
}

app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc) {
    return app_light_sleep_wait_timed(acc, 0);
}

esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {
    if (mode != APP_SLEEP_OFF && mode != APP_SLEEP_DEEP) return ESP_ERR_INVALID_ARG;
    // 产品页用 on_before_lock 保存并收齐后台；保留阅读内存以便交接失败仍可回到原页。
    // Product pages checkpoint/join in on_before_lock; retain reader state so a failed handoff can resume the same page.
    if (pico_online_busy() || !(book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000)) || !read_pico_transfer_pause_for_sleep()) return ESP_ERR_INVALID_STATE;
    (void)ble_pt_stop(2000);
    (void)app_sync_time_to_pmu();
    // 必须先断面板轨，再拉低 MODE，避免常开逻辑仍耗电。/ Drop panel rails before parking MODE on the always-on supply.
    pmu_selftest_prepare_powerdown();
    periph_suspend(true);
    esp_err_t err = ESP_FAIL;
    const unsigned attempts = mode == APP_SLEEP_OFF ? 1 : 3;
    for (unsigned attempt = 0; attempt < attempts; ++attempt) {
        err = mode == APP_SLEEP_OFF ? read_pico_pmu_power_off() : read_pico_pmu_report_sleep();
        ESP_LOGI(TAG, "%s attempt %u: %s", mode == APP_SLEEP_OFF ? "power off" : "SOFT_SLEEP",
                 attempt + 1, esp_err_to_name(err));
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(HOST_EN_DROP_WAIT_MS));
            err = ESP_ERR_TIMEOUT;
            ESP_LOGE(TAG, "EN still high after PMU ack");
            break;
        }
        if (attempt + 1 < attempts) vTaskDelay(pdMS_TO_TICKS(200));
    }
    // 失败不复位重试；恢复外设，交还锁屏/密码层，阅读恢复检查点仍由原入口处理。
    // Do not reset/retry on failure; restore peripherals to the lock/PIN caller, which retains checkpoint ownership.
    periph_resume();
    read_pico_transfer_resume_after_sleep();
    return err;
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
    pmu_selftest_prepare_powerdown();
    esp_err_t err = read_pico_pmu_action(PMU_ACTION_HOST_NORMAL_RESTART, 250, 0x5052);
    ESP_LOGI(TAG, "normal restart: %s", esp_err_to_name(err));
    if (err != ESP_OK) esp_restart();
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

// 密码宽限期内免验证：时钟无效或未记录时一律视为过期（安全回落要密码）。
// Inside the PIN grace window no prompt is needed; an invalid clock or a missing
// record always reads as expired so the safe fallback is the prompt itself.
bool lock_pin_grace_active(void) {
    if (!lock_pin_enabled() || !app_settings_pin_grace_minutes()) return false;
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    time_t now = time(NULL);
    if (now < 1704067200 && pmu && pmu->time_synced) now = pmu->unix_sec;
    if (now < 1704067200) return false;
    uint32_t locked_at = app_settings_pin_locked_at();
    if (!locked_at) return false;
    return (uint32_t)now >= locked_at &&
           (uint32_t)now - locked_at < (uint32_t)app_settings_pin_grace_minutes() * 60u;
}

// 所有锁屏样式先用一次原GC16白目标擦旧字，再输出已合成锁图；不恢复三轮盲清屏。
// All lock styles clear old text with one calibrated white GC16 target before final art, without three blind-clear cycles.
static enum EpdDrawError present_clean_lock(EpdiyHighlevelState *hl) {
    const size_t bytes = (size_t)epd_width() * epd_height() / 2;
    uint8_t *white = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!white) return EPD_DRAW_FAILED_ALLOC;
    memset(white, 255, bytes);
    // 仅替换临时状态的目标指针，共用真实后缓冲；最终锁图及ctx->fb始终不被擦白。
    // A temporary state shares real history but targets white; never overwrite the final art or ctx->fb.
    EpdiyHighlevelState clean = *hl;
    clean.front_fb = white;
    enum EpdDrawError result = update_display_full(&clean);
    heap_caps_free(white);
    if (result != EPD_DRAW_SUCCESS) return result;
    return update_display_full(hl);
}

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc, cst836u_handle_t touch,
    bool reader_background
) {
    bool feedback_failed = s_lock_feedback_failed;
    s_lock_feedback_visible = s_lock_feedback_failed = false;
    // Refuse to interrupt an active upload. The caller repaints its page when
    // this function returns, and the user can lock again after the transfer.
    bool radio_paused = read_pico_transfer_pause_for_sleep();
    if (!radio_paused) {
        pico_boot_clear_resume();
        ESP_LOGW(TAG, "lock postponed while transfer is busy");
        return;
    }
    uint8_t* framebuffer = epd_hl_get_framebuffer(hl);
    // 主动拼贴只在当前页上局部显示圆锁；各样式在RAM中准备完整最终画面。
    // Manual collages show a local badge over the current page; compose each complete final frame in RAM.
    uint8_t lock_style = app_settings_lock_style();
    // 透明书封保留当前页面作为底图，不清白、不重排阅读页，也不读取旧阅读快照。
    // Keep the current page beneath the PNG; no white reset, reader reflow or old reader snapshot.
    if (lock_style != 3) epd_hl_set_all_white(hl);
    else memcpy(framebuffer,hl->back_fb,(size_t)epd_width()*epd_height()/2);
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
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
    const bool direct_frame = !feedback_failed && lock_drawn;
    if (direct_frame) result = present_clean_lock(hl);
    if (!direct_frame || result != EPD_DRAW_SUCCESS) {
        // 所有样式从真实后缓冲输出；绘图或输出失败仍清白，再按白基准重试最终图。
        // All styles use true history; art/output failures retain clearing and the from-white final frame.
        epd_poweron();
        epd_clear();
        result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    }
    if (lock_style == 2 && lock_drawn && result == EPD_DRAW_SUCCESS) book_lock_collage_cache_commit(framebuffer);
    if (lock_style == 1 && lock_drawn && result == EPD_DRAW_SUCCESS) ui_wallpaper_cache_commit(framebuffer);

    // PIN也只保存成功显示的原样式，密码输入层不能把兜底或键盘写成锁图。
    // PIN locks persist only successfully displayed artwork; the PIN layer must not store fallback/keypad pixels.
    if(lock_drawn && result==EPD_DRAW_SUCCESS && lock_pin_enabled() && lock_screen_save(framebuffer))
        (void)pico_boot_mark_frame_cached();
    app_lock_wait_key_idle(800);
    // 锁屏先浅睡，十分钟后进入深睡；按键唤醒使用已保存的页面检查点。
    // Locks light-sleep first, enter deep sleep after ten minutes and wake through the saved page checkpoint.
    ESP_LOGI(TAG, "lock LIGHT");
    epd_poweroff();
    s_lock_hint_hl=hl;s_lock_hint.serial=read_pico_pmu_lock_key_serial();
    app_wake_source_t wake = APP_WAKE_KEY;
    // 密码宽限期：锁屏时记录时刻，宽限期内跳过密码直接浅睡；超时或时钟无效则照常验证。
    // PIN grace: record the lock moment and skip the PIN inside the window; expired or
    // invalid clocks verify as usual.
    if (lock_pin_enabled()) {
        const pmu_snapshot_t *pmu = read_pico_pmu_get();
        time_t now = time(NULL);
        if (now < 1704067200 && pmu && pmu->time_synced) now = pmu->unix_sec;
        app_settings_record_pin_lock((uint32_t)now);
        // 宽限判定由密码循环在实际按键唤醒后执行，不能在开始等待时提前放行。
        // The PIN loop checks grace after the actual wake key, never before waiting.
        lock_screen_authenticate(hl, touch, acc, false);
    }
    else wake = app_light_sleep_wait_timed(NULL, APP_LOCK_LIGHT_SLEEP_MS);
    while (wake == APP_WAKE_TIMEOUT) {
        // PMU SOFT_SLEEP drops the ESP rail while keeping its RTC alive. A short
        // power-key press boots the host; the e-paper keeps this lock image.
        if(lock_drawn && result==EPD_DRAW_SUCCESS && !lock_pin_enabled() && lock_screen_save(framebuffer)) (void)pico_boot_mark_frame_cached();
        app_enter_host_sleep(APP_SLEEP_DEEP);
        wake=app_light_sleep_wait_timed(NULL,APP_LOCK_LIGHT_SLEEP_MS);
    }
    pico_boot_clear_resume();
    s_lock_hint_hl=NULL;
    if (system_lock) app_font_end_lock(&lock_font);
    // 所有样式的Wi-Fi/HTTP均由调用者恢复页面后重启，避免网络初始化挡住首帧。
    // Callers restart Wi-Fi/HTTP after restoring the page for every style.

    // 成功显示的所有样式保留实际锁图/密码界面后缓冲，主循环各唤醒入口均请求一次GC16恢复原页面。
    // Retain actual cover/PIN history after a successful lock; every caller requests one GC16 page restore.
    // 不复制旧正文或伪造白基准；绘图/显示失败仍走原清屏。
    // Never replay a saved reader frame or fabricate white history; art/output failures keep the old clear.
    read_pico_pmu_drain_events();
    if (!lock_drawn || result != EPD_DRAW_SUCCESS) {
        epd_poweron();
        epd_clear();
        epd_hl_set_all_white(hl);
    }
    read_pico_pmu_drain_events();
    if (ignore_until_ms) {
        *ignore_until_ms = esp_timer_get_time() / 1000 + APP_LOCK_IGNORE_BOOT_MS;
    }
    ESP_LOGI(TAG, "unlocked");
}

void app_lock_boot_gate(EpdiyHighlevelState *hl, cst836u_handle_t touch, sc7a20h_handle_t acc) {
    s_lock_hint_hl=s_boot_lock_active&&!s_boot_lock_known?NULL:hl;s_lock_hint.serial=0;
    s_boot_lock_draw=true;lock_resume_service();
    if (!lock_pin_enabled()) {
        if(pico_boot_has_resume()) {
            pico_boot_ready();pico_boot_hold_resume();
            // 深睡唤醒的开机短按已经算一次，这里再等一次即可，不再要求双击。
            // The power-on short press already counts as one; this wait accepts a single press
            // instead of demanding a double click.
            app_lock_wake_single_once();
            (void)app_light_sleep_wait_timed(NULL,0);
        }
        s_boot_lock_hl=NULL;s_boot_lock_active=false;s_lock_hint_hl=NULL;return;
    }
    // 卡挂载有界等待仅为了字体/锁屏图；没有卡仍必须验证。
    // Bound mount waiting for the face/art only; absent media still requires authentication.
    read_pico_sd_info_t info;
    for (unsigned i = 0; i < 150 && read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED; ++i)
        vTaskDelay(pdMS_TO_TICKS(20));
    lock_resume_service();
    app_lock_font_t saved; app_font_begin_lock(&saved);
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    if (!lock_screen_restore(fb)) {
        // 冷启动丢失锁屏缓存时没有当前页面，用干净底图并继续密码校验。
        // A cold start with no retained art has no current page; use a clean base and keep authentication.
        if (app_settings_lock_style() == 3) memset(fb,255,(size_t)epd_width()*epd_height()/2);
        bool drawn = app_settings_lock_style() == 0 ? book_ticket_draw(fb, false) :
            app_settings_lock_style() == 2 ? book_lock_collage_draw(fb) :
            app_settings_lock_style() == 3 ? book_lock_bookmark_draw(fb,app_settings_bookmark_path()) : draw_wallpaper(fb);
        if (!drawn) ui_lock_fallback_draw(fb);
    }
    s_boot_lock_hl=NULL;s_boot_lock_active=false;
    lock_screen_authenticate(hl, touch, acc, true);
    s_lock_hint_hl=NULL;
    app_font_end_lock(&saved);
}

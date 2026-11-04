/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：硬件前建立持久化启动保护；深睡恢复入口先消费再开书，中断图书进入可操作恢复页。
 * User revision: persist startup protection before hardware; consume deep-sleep checkpoints before opening books, and show an operable recovery page after interrupted opening.
 * 只做开机装配：拉起板级硬件、读设置、定 VCOM、放开机图、开字体；
 * 未标定则先拦住进设定页。然后把控制权交给 app_loop。
 *
 * Boot wiring only: board init, settings, PMU RTC restore, VCOM, splash, fonts. If VCOM
 * is unset, the factory page blocks first. Then control goes to app_loop.
 */

#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "app_loop.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "setup_state.h"
extern const app_desc_t app_onboarding;
#include "lock_screen.h"
#include "ui_lock_feedback.h"
#include "lock_pin.h"
#include "ttf_font.h"
#include "book_store.h"
#include "esp_system.h"
#include "read_pico_sd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include "app_registry.h"
#include "display.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "esp_log.h"
#include "ota_update.h"
#include "pmu_selftest.h"
#include "metalio_charger.h"
#include "metalio_hw.h"
#include "output_lcd/lcd_driver.h"
#include "read_pico_board.h"
#include "read_pico_crashlog.h"
#include "read_pico_init.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "settings.h"
#include "sleep.h"
#include "ttf_font.h"
#include "usb_storage.h"
#include "app_font_context.h"
#include "ui_kit.h"
#include "ui_font.h"
#include "vcom_setup.h"
#include "debug_panel.h"
#include "metalio_sleep.h"

static const char* TAG = "read_pico";
extern const app_desc_t app_boot_recovery;

// 主机上电会丢失系统时间；PMU 在主机断电后继续走时。先取实时值再恢复，
// 不使用启动时缓存的快照，以免将已经经过的开机耗时丢掉。
static void restore_time_from_pmu(void) {
    struct timeval current;
    if (gettimeofday(&current,NULL)==0 && current.tv_sec>=1704067200 && current.tv_sec<=4102444799U) return;
    if (read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0) != ESP_OK) return;
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (!pmu || !pmu->time_ok || !pmu->time_synced || pmu->unix_sec < 1704067200 || pmu->unix_sec > 4102444799U) return;
    struct timeval tv = {.tv_sec = (time_t)pmu->unix_sec, .tv_usec = 0};
    if (settimeofday(&tv, NULL) == 0) ESP_LOGI(TAG, "system clock restored from Metalio PCF8563");
}


// VCOM 从 NVS 读取（默认 3000，实测最优）。V1.5 没有 Pico PMU，标定值只存本机 NVS。
// Load VCOM from NVS (3000 by default, the measured optimum). V1.5 has no Pico
// PMU, so the calibrated value lives in local NVS only.
static bool resolve_vcom_at_boot(void) {
    const int mv = vcom_setup_load_mv();
    epd_set_vcom((uint16_t)mv);
    ESP_LOGI(TAG,"Metalio V1.5 runtime VCOM=%dmV (NVS, default 3000)", mv);
    return true;
}

// 开机按住音量键进 VCOM 校准页；V1.5 音量键在 FCA9555 P1.0/P1.1，低有效。
// Held volume keys (active low on FCA9555 P1.0/P1.1) open the VCOM calibration page.
static bool volume_key_held(void) {
    uint16_t first = 0xffff, again = 0xffff;
    if (metalio_io_read(&first) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(30));
    if (metalio_io_read(&again) != ESP_OK) return false;
    const bool pressed_first = ((first >> 8) & 0x03) != 0x03;
    const bool pressed_again = ((again >> 8) & 0x03) != 0x03;
    return pressed_first && pressed_again;
}

// 启动与锁屏均由当前字形/壁纸绘制；检查真实画布，不为旧资源尺寸保留整屏图片。
// Splash/lock use current glyphs/wallpapers; check the actual canvas instead of linking unused legacy art.
static bool canvas_matches_panel(void) {
    const int width=epd_width(),height=epd_height();
    if ((width==UI_LOCK_WIDTH && height==UI_LOCK_HEIGHT) ||
        (width==UI_LOCK_HEIGHT && height==UI_LOCK_WIDTH)) return true;
    ESP_LOGE(TAG,"Canvas/panel mismatch: %dx%d expected %dx%d",width,height,UI_LOCK_WIDTH,UI_LOCK_HEIGHT);
    return false;
}

static void draw_startup_frame(uint8_t *framebuffer) {
    ui_clear_page(framebuffer);
    const int cx = epd_rotated_display_width() / 2;
    const int cy = epd_rotated_display_height() / 2;
    static const char *const wordmark[] = {"K", "i", "i", "k", "o", "r", "e", "a", "d"};
    static const char *const caption[] = {"准", "备", "资", "源"};
    static const char *const caption_en[]={"P","r","e","p","a","r","i","n","g"," ","r","e","s","o","u","r","c","e","s"};
    const struct {
        const char *text;
        const char *const *letters;
        int count, px, tracking, gray, center_y;
    } lines[] = {
        {"Kiikoread", wordmark, 9, 72, 2, 0, cy - 50},
        {app_setup_english()?"Preparing resources":"准备资源", app_setup_english()?caption_en:caption, app_setup_english()?19:4, 24, app_setup_english()?1:8, 7, cy + 20},
    };
    // 以实际字宽居中，疏朗品牌字距与浅灰提示形成层次，不引入新的字体或位图。
    // Center measured advances; tracked branding and a muted caption add hierarchy without new fonts or bitmaps.
    for (size_t row = 0; row < sizeof(lines) / sizeof(lines[0]); ++row) {
        int above = 0, below = 0;
        ui_font_measure_line_px(lines[row].px, lines[row].text, &above, &below);
        int width = (lines[row].count - 1) * lines[row].tracking;
        for (int i = 0; i < lines[row].count; ++i)
            width += ui_font_text_width_px(lines[row].px, lines[row].letters[i]);
        int x = cx - width / 2;
        const int baseline = lines[row].center_y + (above - below) / 2;
        for (int i = 0; i < lines[row].count; ++i) {
            ui_font_draw_text_px(framebuffer, x, baseline, lines[row].px,
                                lines[row].letters[i], EPD_DRAW_ALIGN_LEFT,
                                lines[row].gray, 15, false);
            x += ui_font_text_width_px(lines[row].px, lines[row].letters[i]) + lines[row].tracking;
        }
    }
}

// 深睡保持原物理锁图；卡与参考帧稍后就绪，不能用临时圆锁覆盖。
// Preserve physical art on deep boot; pending media/reference recovery must not replace it with a badge.
static bool prepare_startup_frame(EpdiyHighlevelState *hl,uint8_t *framebuffer,bool resuming,bool vcom_ok) {
    if(resuming && vcom_ok && !lock_pin_enabled())return app_lock_resume_frame(hl);
    epd_poweron();
    if(!resuming)read_pico_i2c_census_take();
    epd_clear();
    epd_hl_set_all_white(hl);
    if(resuming)ui_lock_fallback_draw(framebuffer);
    else draw_startup_frame(framebuffer);
    update_display_from_white(hl);
    return false;
}

void app_main(void) {
    metalio_capture_boot_power_key();
    app_lock_capture_boot_wakeup();
    ESP_ERROR_CHECK(metalio_sleep_sdk_init());
    // 共享 TF 卡期间复位后，先把内部 PHY 交回串口，再清理已用完的 BOOT 请求。
    // Reclaim Serial/JTAG after a reset during card sharing, then clear the served BOOT request.
    usb_storage_phy_init();
    metalio_clear_download_request();

    const esp_reset_reason_t reset = esp_reset_reason();
    ESP_LOGI(TAG, "startup reset=%d stack=%u", (int)reset, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    app_settings_init();
    // 大缓冲/字体之前读取凭据；失败不缓存，后续入口仍可重试。/ Read credentials before large buffers/fonts; retry failed reads later.
    (void)lock_pin_enabled();
    pico_boot_init(reset == ESP_RST_PANIC || reset == ESP_RST_INT_WDT ||
                   reset == ESP_RST_TASK_WDT || reset == ESP_RST_WDT);

    read_pico_handle_t hw;
    if (read_pico_init(&hw) != ESP_OK) return;
    rp_boot_mark("hw");
    if (hw.pmu_ready) {
        restore_time_from_pmu();
        // 快充开关按保存的偏好立即生效；驱动未就绪时保持初始化时的标准电流。
        // Apply the saved fast-charge preference immediately; a missing driver keeps the standard current.
        (void)metalio_charger_set_fast_charge(app_settings_fast_charge());
    }
    const bool vcom_ok = resolve_vcom_at_boot();
    rp_boot_mark("vcom");
    ESP_LOGI(TAG,"TPS65185 last VCOM register readback=%dmV",metalio_tps_vcom_mv());
    pmu_selftest_bind(hw.sensor);

    if (!canvas_matches_panel()) return;

    EpdiyHighlevelState hl = hw.hl;
    uint8_t* framebuffer = hw.framebuffer;

    // 深睡保留屏幕已有画面：验证缓存后同时恢复前后帧，省去清白和开机图两次推屏。
    // Deep sleep retains the panel image: validate its cache and restore both references, skipping white erase and splash output.
    const bool resuming = pico_boot_has_resume() && reset == ESP_RST_DEEPSLEEP;
    // 非已确认硬件按键开机的双击门禁显示锁图，不能在准备资源页暗中等按键。
    // Double-press gates outside confirmed hardware-key boots retain lock art instead of waiting behind the resource splash.
    const bool retain_lock = (pico_boot_has_resume() || pico_boot_lock_pending()) &&
        (reset != ESP_RST_POWERON || !metalio_boot_power_key());
    (void)prepare_startup_frame(&hl,framebuffer,retain_lock,vcom_ok);
    rp_boot_mark("splash");
    // 直接续读不先加载整份系统字体；密码验证仍先使用所选系统字体。
    // Direct reader resume skips loading the whole system face first; PIN authentication still uses the selected UI face.
    if(!resuming || lock_pin_enabled())app_font_activate_system();
    else (void)ttf_font_open_builtin();
    rp_boot_mark("font");

    if (!hw.touch_ready) {
        ESP_LOGE(TAG, "No touch controller, UI cannot run");
        epd_poweroff();
        return;
    }

    // V1.5 没有 Pico PMU：改为开机按住音量键进入 VCOM 校准页；写入后立即生效。
    // No Pico PMU on V1.5: hold a volume key while powering on to open VCOM
    // calibration; an accepted value is applied on the next rail power-up.
    if (!vcom_ok) {
        vcom_setup_run(&hl, framebuffer, hw.touch);
        display_request_full_refresh();
    } else if(volume_key_held()) {
        debug_panel_run(&hl, framebuffer, hw.touch);
        display_request_full_refresh();
    }

    // 硬件、显示、触摸与必要的出厂设置均成功后确认新槽。
    // Confirm a new slot only after hardware, display, touch, and required setup succeed.
    pico_ota_confirm_running();

    app_lock_boot_gate(&hl, hw.touch, hw.sensor);
    metalio_frontlight_restore();
    rp_boot_mark("lock");
    const app_desc_t *first = app_home_page();
    pico_resume_t resume = {0};
    if (pico_boot_take_resume(&resume)) {
        if (!resume.reader) first = app_at(resume.tab);
        else {
            // 挂载等待有界；恢复入口已消费，无卡或资源失败不会反复自动开书。
            // Bound mount waiting after consuming the checkpoint; absent media or failed resources cannot auto-open in a loop.
            if (!strncmp(resume.path, "/sdcard/", 8)) {
                read_pico_sd_info_t info;
                for (unsigned wait = 0; wait < 150 && read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED; ++wait)
                    vTaskDelay(pdMS_TO_TICKS(20));
            }
            if (!strncmp(resume.path, "/flash/books/", 13) && !book_store_flash_ready()) {
                book_store_root_t roots[BOOK_STORE_ROOT_MAX]; int count;
                (void)book_store_roots(roots, &count);
            }
            struct stat st;
            if (!stat(resume.path, &st) && S_ISREG(st.st_mode) &&
                app_book_request_resume(resume.path, resume.fullscreen)) first = app_at(1);
        }
    }
    char interrupted[PICO_BOOT_PATH_MAX];
    if (pico_boot_interrupted_book(interrupted, sizeof(interrupted))) first = &app_boot_recovery;
    if(resuming && !lock_pin_enabled()) {
        if(first==app_at(1) && resume.reader)app_font_activate_reading();
        else app_font_activate_system();
    }
    rp_boot_mark("resume");
    // 首次引导服从现有密码/异常启动门禁，老配置不会重新触发。/ Setup retains PIN/crash gates and never reopens for legacy profiles.
    if(first==app_home_page()&&!lock_pin_enabled()&&app_setup_pending())first=&app_onboarding;
#ifdef KIIKO_LOCAL_TRANSFER_START
    // Explicit unattended local tests only; preserve PIN and recovery gates.
    if (!lock_pin_enabled() && first == app_home_page()) {
        extern const app_desc_t app_transfer;
        extern void app_transfer_request_wifi_upload(void);
        app_transfer_request_wifi_upload();
        first = &app_transfer;
    }
#endif
    app_loop_run(&(app_loop_config_t){
        .hl = &hl,
        .fb = framebuffer,
        .acc = hw.sensor,
        .tp = hw.touch,
        .sensor_ready = hw.sensor_ready,
        // 用户修订：深睡一次性恢复上次阅读或主页面；异常启动安全回首页。
        // User revision: resume the last reader/main tab once after deep sleep; abnormal starts safely enter Home.
        .first_app = first,
    });
}

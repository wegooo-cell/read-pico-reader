/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：硬件前建立持久化启动保护；深睡恢复入口先消费再开书，中断图书进入可操作恢复页。
 * User revision: persist startup protection before hardware; consume deep-sleep checkpoints before opening books, and show an operable recovery page after interrupted opening.
 * 用户2026-11-03修订：无密码的单击深睡恢复承接本次 PMU 按键上电证据，不再要求额外短按；非上电复位、双击及密码门禁保留。
 * User2026-11-03: single-press deep resume without PIN accepts fresh PMU key power-on evidence; retain non-power-on, double-click and PIN gates.
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
#include "soc/rtc_cntl_reg.h"
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

static const char* TAG = "read_pico";
extern const app_desc_t app_boot_recovery;

// 软复位保留有效的 S3 RTC；断电才用 PMU 的实时秒和毫秒恢复。
// Retain a valid S3 RTC across soft reset; restore fresh PMU seconds and milliseconds after power loss.
static void restore_time_from_pmu(void) {
    struct timeval current;
    if (gettimeofday(&current, NULL) == 0 && current.tv_sec >= 1704067200 &&
        current.tv_sec <= 4102444799U) return;
    if (read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0) != ESP_OK) return;
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (!pmu || !pmu->time_ok || !pmu->time_synced || pmu->unix_millis >= 1000 ||
        pmu->unix_sec < 1704067200 || pmu->unix_sec > 4102444799U) return;
    struct timeval tv = {.tv_sec = (time_t)pmu->unix_sec, .tv_usec = pmu->unix_millis * 1000};
    if (settimeofday(&tv, NULL) == 0) ESP_LOGI(TAG, "system clock restored from PMU");
}


// 已标定则开机读一次喂给 epdiy。未标定先用板级默认出开机图，随后拦住进标定页。
// Load VCOM once when set. Otherwise keep the board default for the splash,
// then gate into the setup page.
static bool resolve_vcom_at_boot(void) {
    int mv = 0;
    for (int i = 0; i < 3; i++) {
        if (read_pico_pmu_vcom_get(&mv) == ESP_OK) {
            epd_set_vcom((uint16_t)mv);
            ESP_LOGI(TAG, "panel VCOM loaded from PMU");
            return true;
        }
    }
    ESP_LOGW(TAG, "panel VCOM unset, keeping board default until setup");
    return false;
}

// 极简启动画面直接借内建字形绘制，不读TF、不借阅读字体、不保存整屏位图。
// Draw the minimal cold splash with built-in glyphs, without TF access, reader faces or a full-screen asset.
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

// 深睡保持面板上原锁屏；挂卡与参考帧恢复由锁屏输入循环继续，不能用圆锁覆盖。
// Keep the retained lock on deep boot; the locked-input loop resumes pending media/reference restoration without a badge replacement.
static bool prepare_startup_frame(EpdiyHighlevelState *hl,uint8_t *framebuffer,bool resuming,bool vcom_ok) {
    if(resuming && vcom_ok)return app_lock_resume_frame(hl);
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
    // 共享 TF 卡期间复位后，先把内部 PHY 交回串口，再清理已用完的 BOOT 请求。
    // Reclaim Serial/JTAG after a reset during card sharing, then clear the served BOOT request.
    usb_storage_phy_init();
    REG_CLR_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);

    const esp_reset_reason_t reset = esp_reset_reason();
    ESP_LOGI(TAG, "startup reset=%d stack=%u", (int)reset, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    app_settings_init();
    // 在大缓冲和字体之前读取小型锁屏设置；读取失败不阻塞，之后的检查仍可重试。
    // Read the tiny lock setting before large buffers/fonts; keep failures nonblocking and let subsequent checks retry.
    (void)lock_pin_enabled();
    pico_boot_init(reset == ESP_RST_PANIC || reset == ESP_RST_INT_WDT ||
                   reset == ESP_RST_TASK_WDT || reset == ESP_RST_WDT);

    read_pico_handle_t hw;
    if (read_pico_init(&hw) != ESP_OK) return;
    rp_boot_mark("hw");
    if (hw.pmu_ready) restore_time_from_pmu();
    const bool vcom_ok = resolve_vcom_at_boot();
    rp_boot_mark("vcom");
    pmu_selftest_bind(hw.sensor);
    app_sleep_bind(hw.touch, hw.sensor);


    EpdiyHighlevelState hl = hw.hl;
    uint8_t* framebuffer = hw.framebuffer;

    // 深睡保留屏幕已有画面：验证缓存后同时恢复前后帧，省去清白和开机图两次推屏。
    // Deep sleep retains the panel image: validate its cache and restore both references, skipping white erase and splash output.
    const bool resuming = pico_boot_has_resume();
    bool restored_lock=prepare_startup_frame(&hl,framebuffer,resuming,vcom_ok);
    rp_boot_mark("splash");
    // 直接续读不先加载整份系统字体；密码验证仍先使用所选系统字体。
    // Direct reader resume skips loading the whole system face first; PIN authentication still uses the selected UI face.
    if(!resuming || lock_pin_enabled())app_font_activate_system();
    else (void)ttf_font_open_builtin();
    rp_boot_mark("font");

    if (!hw.touch_ready) {
        ESP_LOGE(TAG, "No touch controller, UI cannot run");
        return;
    }

    if (hw.pmu_ready && !vcom_ok) {
        vcom_setup_run(&hl, framebuffer, hw.touch);
    }

    // 硬件、显示、触摸与必要的出厂设置均成功后确认新槽。
    // Confirm a new slot only after hardware, display, touch, and required setup succeed.
    pico_ota_confirm_running();

    // 单击开机：开机那一次短按就算开机，直接续读；双击开机仍停在锁屏等第二次。
    // One-press boot treats the powering short press as the boot; double-click boot still waits on the lock face.
    const bool one_press_boot = resuming && !lock_pin_enabled() && hw.pmu_ready &&
        app_settings_wake_presses() == 1;
    bool boot_press = read_pico_pmu_take_boot_short_press();
    // PMU 软睡后的第一按可能只上电，不产生 SHORT；软件/USB/异常复位不能复用该证据。
    // The first press after PMU soft sleep may only power on, without SHORT; software/USB/fault resets cannot reuse it.
    const bool boot_power_key = read_pico_pmu_take_boot_power_key();
    if (reset == ESP_RST_POWERON && boot_power_key) boot_press = true;
    // 按住稍久会被 PMU 记成长按，释放也可能晚于握手；有界补等一次按键事件，避免已经按过还要再按。
    // A slightly long hold is logged as a long press and its release can land after the handshake,
    // so wait a bounded window for that event instead of asking for a second press.
    for (unsigned wait = 0; one_press_boot && !boot_press && wait < 50; ++wait) {
        boot_press = read_pico_pmu_take_key_wakeup_single();
        if (!boot_press) vTaskDelay(pdMS_TO_TICKS(20));
    }
    const bool boot_with_one_press = one_press_boot && boot_press;
    if (boot_with_one_press) {
        // 开机那次已完成解锁，其延后释放不得在慢首帧之后再次触发锁屏。
        // This press already unlocked; its delayed tail must not relock after a slow first frame.
        read_pico_pmu_rearm_after_boot_key();
        ESP_LOGI(TAG, "resume lock cache=%d; one-press boot", restored_lock);
        pico_boot_ready();
        pico_boot_hold_resume();
    } else {
        if(resuming && !lock_pin_enabled())
            ESP_LOGI(TAG,"resume lock cache=%d; power presses=%u",restored_lock,
                (unsigned)app_settings_wake_presses());
        app_lock_boot_gate(&hl, hw.touch, hw.sensor);
    }
    (void)app_lock_resume_frame(NULL);
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

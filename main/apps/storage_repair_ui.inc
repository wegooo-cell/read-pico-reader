#include "ui_locale.h"
// PICO专用短语不占用S31紧张的应用空间。/ Keep Pico-only maintenance wording out of the tight S31 image.
#define STORAGE_TEXT(zh,en) (ui_locale_english() ? (en) : (zh))
// PICO维护页只绘制快照；危险操作必须经过不同页面状态的两次确认。
// Pico maintenance paints snapshots only; destructive work needs two confirmations in distinct states.
static bool s_storage_rebuild_question;
static bool s_storage_wait_release;
static EpdRect storage_check_button(void) { return (EpdRect){54, 850, 576, 74}; }
static EpdRect storage_rebuild_button(void) { return (EpdRect){54, 948, 576, 74}; }
static const char *storage_phase_label(storage_repair_phase_t phase) {
    switch (phase) {
        case STORAGE_REPAIR_PREPARE: return STORAGE_TEXT("正在释放存储资源…","Releasing storage resources...");
        case STORAGE_REPAIR_REMOUNT: return STORAGE_TEXT("正在重新挂载 TF 卡…","Remounting the TF card...");
        case STORAGE_REPAIR_CHECK: return STORAGE_TEXT("正在检查机内存储和阅读记录…","Checking storage and reading records...");
        case STORAGE_REPAIR_COPY: return STORAGE_TEXT("正在备份机内存储，请勿拔卡…","Backing up storage. Keep TF inserted...");
        case STORAGE_REPAIR_VERIFY: return STORAGE_TEXT("正在核验备份与原始数据…","Verifying backup against original data...");
        case STORAGE_REPAIR_RECHECK: return STORAGE_TEXT("正在再次核验，尚未清空机内存储…","Rechecking source and backup; not cleared...");
        case STORAGE_REPAIR_REBUILD: return STORAGE_TEXT("正在重建机内存储，请勿断电…","Rebuilding storage. Keep power connected...");
        case STORAGE_REPAIR_BACKUP_READY: return STORAGE_TEXT("原始备份已通过校验","Original backup verified");
        case STORAGE_REPAIR_FAILED: return STORAGE_TEXT("操作未完成","Operation incomplete");
        case STORAGE_REPAIR_DONE: return STORAGE_TEXT("存储检查完成","Storage checks finished");
        default: return STORAGE_TEXT("先检查存储，再决定如何修复","Check storage before choosing repair");
    }
}
static void storage_repair_draw(uint8_t *fb) {
    const storage_repair_status_t status = app_storage_repair_status();
    ui_clear_page(fb);
    back_header(fb, STORAGE_TEXT("存储检查与修复","Storage checks and repair"));
    ui_text_fixed(fb, 54, 264, 24, STORAGE_TEXT("默认保留书籍、设置和阅读记录","Checks preserve books, settings and progress"), EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed(fb, 54, 310, 22, STORAGE_TEXT("检查 TF、机内存储与进度备用存储","Check TF, internal storage and progress"), EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed(fb, 54, 386, 24, storage_phase_label(status.phase), EPD_DRAW_ALIGN_LEFT, false);
    if (s_storage_rebuild_question) {
        ui_text_fixed(fb, 54, 475, 24, STORAGE_TEXT("重建将清空机内书籍与机内记录","Rebuild clears internal books and records"), EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 54, 527, 23, STORAGE_TEXT("设置保留，TF 卡不会格式化","Settings and TF card are retained"), EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 54, 579, 22, STORAGE_TEXT("先备份并校验，之后还需再次确认","Backup is checked before a second confirmation"), EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 54, 631, 21, STORAGE_TEXT("原始备份不能自动还原旧文件","Raw backups do not restore files automatically"), EPD_DRAW_ALIGN_LEFT, false);
    } else if (status.phase >= STORAGE_REPAIR_COPY) {
        char line[96];
        snprintf(line, sizeof(line), STORAGE_TEXT("TF 卡：%s","TF card: %s"), status.card_error == ESP_ERR_NOT_FINISHED ? STORAGE_TEXT("未检查","Not checked") :
                 status.card_error == ESP_OK ? STORAGE_TEXT("可用","Available") : STORAGE_TEXT("暂不可用","Unavailable"));
        ui_text_fixed(fb, 54, 470, 24, line, EPD_DRAW_ALIGN_LEFT, false);
        snprintf(line, sizeof(line), STORAGE_TEXT("机内存储：%s","Internal storage: %s"), status.flash_error == ESP_ERR_NOT_FINISHED ? STORAGE_TEXT("未检查","Not checked") :
                 status.flash_error == ESP_OK ? STORAGE_TEXT("可用","Available") : STORAGE_TEXT("暂不可用","Unavailable"));
        ui_text_fixed(fb, 54, 518, 24, line, EPD_DRAW_ALIGN_LEFT, false);
        snprintf(line, sizeof(line), STORAGE_TEXT("阅读记录：%s","Reading progress: %s"), status.progress_error == ESP_ERR_NOT_FINISHED ? STORAGE_TEXT("未检查","Not checked") :
                 status.progress_error != ESP_OK ? STORAGE_TEXT("暂不可用","Unavailable") :
                 status.card_progress ? STORAGE_TEXT("使用 TF 备用存储","TF fallback storage") : STORAGE_TEXT("使用机内存储","Internal storage"));
        ui_text_fixed(fb, 54, 566, 23, line, EPD_DRAW_ALIGN_LEFT, false);
        if (status.total && app_storage_repair_busy()) {
            snprintf(line, sizeof(line), STORAGE_TEXT("进度：%u%%","Progress: %u%%"), (unsigned)(status.done * 100 / status.total));
            ui_text_fixed(fb, 54, 620, 23, line, EPD_DRAW_ALIGN_LEFT, false);
        }
    }
    if (status.phase == STORAGE_REPAIR_BACKUP_READY) {
        ui_text_fixed(fb, 54, 630, 22, STORAGE_TEXT("下一步会清空机内存储，请再次确认","The next step clears internal storage. Confirm."), EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 54, 684, 21, "TF:/kiikoread-recovery", EPD_DRAW_ALIGN_LEFT, false);
        const char *folder = strstr(status.backup_path, "/storage-");
        if (folder) {
            char name[48]; const char *end = strchr(folder + 1, '/');
            snprintf(name, sizeof(name), "%.*s", end ? (int)(end - folder - 1) : 31, folder + 1);
            ui_text_fixed(fb, 54, 730, 21, name, EPD_DRAW_ALIGN_LEFT, false);
        }
    } else if (status.phase == STORAGE_REPAIR_FAILED) {
        const char *message = status.erase_started ? STORAGE_TEXT("重建未完成，原始备份已保留","Rebuild incomplete. Original backup retained.") :
            status.error == ESP_ERR_NO_MEM ? STORAGE_TEXT("内存不足，机内原数据未清空","Low memory. Original storage was not cleared.") :
            status.error == ESP_ERR_INVALID_SIZE ? STORAGE_TEXT("TF 空间不足，请至少预留 6 MB","Leave at least 6 MB free on TF.") :
            status.error == ESP_ERR_INVALID_STATE ? STORAGE_TEXT("存储被占用或确认已失效，请重试","Storage busy or consent expired. Retry.") :
            STORAGE_TEXT("请检查 TF 卡，重新检查或重做备份","Check TF, then repeat checks or backup.");
        ui_text_box(fb, (EpdRect){54, 670, 576, 112}, 22, message, EPD_DRAW_ALIGN_LEFT, false);
    } else if (status.phase == STORAGE_REPAIR_DONE && status.flash_error != ESP_OK) {
        ui_text_box(fb, (EpdRect){54, 650, 576, 140}, 22,
                    STORAGE_TEXT("机内存储仍不可用；若格式无法识别，可先备份后重建。TF 书籍可继续使用。","Internal storage is unavailable. An unrecognized format can be backed up and rebuilt. TF books remain usable."),
                    EPD_DRAW_ALIGN_LEFT, false);
    } else if (status.phase == STORAGE_REPAIR_DONE && status.erase_started) {
        ui_text_box(fb, (EpdRect){54, 650, 576, 140}, 22,
                    STORAGE_TEXT("机内存储已重建；原始备份保留在 TF:/kiikoread-recovery。","Internal storage rebuilt. Original backup is in TF:/kiikoread-recovery."),
                    EPD_DRAW_ALIGN_LEFT, false);
    }
    const bool ready = status.phase == STORAGE_REPAIR_BACKUP_READY;
    ui_draw_button(fb, storage_check_button(), app_storage_repair_busy() ? STORAGE_TEXT("操作中，请稍候…","Working...") :
                   (ready || s_storage_rebuild_question) ? STORAGE_TEXT("取消重建","Cancel rebuild") : STORAGE_TEXT("检查并尝试恢复","Check & recover"), false);
    ui_draw_button(fb, storage_rebuild_button(), ready ? STORAGE_TEXT("确认清空并重建","Clear & rebuild") :
                   s_storage_rebuild_question ? STORAGE_TEXT("确认并开始备份","Confirm backup") : STORAGE_TEXT("备份后重建","Back up & rebuild"), true);
    settings_navigation(fb);
}
static app_redraw_t storage_repair_tick(app_ctx_t *ctx) {
    storage_repair_phase_t before = app_storage_repair_status().phase;
    bool changed = app_storage_repair_tick(ctx->now_ms);
    if (before != STORAGE_REPAIR_BACKUP_READY && app_storage_repair_status().phase == STORAGE_REPAIR_BACKUP_READY)
        s_storage_wait_release = true;
    // 备份结束时仍按着的手指不能变成最终确认，必须先抬手再重新点按。
    // A finger held across backup completion cannot become final consent; require release and a fresh tap.
    if (s_storage_wait_release && ctx->touch && !ctx->touch->touched) s_storage_wait_release = false;
    return changed ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
}
static app_redraw_t storage_repair_gesture(app_ctx_t *ctx, const ui_gesture_event_t *event) {
    if (app_storage_repair_busy() || s_storage_wait_release || event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (event->x0 < 170 && event->y0 < 190) {
        app_storage_repair_cancel(); s_storage_rebuild_question = false;
        s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE;
    }
    storage_repair_status_t status = app_storage_repair_status();
    if (ui_rect_hit(storage_check_button(), event->x0, event->y0)) {
        if (s_storage_rebuild_question || status.phase == STORAGE_REPAIR_BACKUP_READY) {
            s_storage_rebuild_question = false; app_storage_repair_cancel();
        } else (void)app_storage_repair_begin(false, ctx->now_ms);
        return APP_REDRAW_PAGE;
    }
    if (!ui_rect_hit(storage_rebuild_button(), event->x0, event->y0)) return APP_REDRAW_NONE;
    if (status.phase == STORAGE_REPAIR_BACKUP_READY) {
        (void)app_storage_repair_confirm(ctx->now_ms);
    } else if (s_storage_rebuild_question) {
        (void)app_storage_repair_begin(true, ctx->now_ms); s_storage_rebuild_question = false;
    } else s_storage_rebuild_question = true;
    return APP_REDRAW_PAGE;
}

#undef STORAGE_TEXT

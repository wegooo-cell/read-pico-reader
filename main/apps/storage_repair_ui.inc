// PICO维护页只绘制快照；危险操作必须经过不同页面状态的两次确认。
// Pico maintenance paints snapshots only; destructive work needs two confirmations in distinct states.
static bool s_storage_rebuild_question;
static bool s_storage_wait_release;
static EpdRect storage_check_button(void) { return (EpdRect){54, 850, 576, 74}; }
static EpdRect storage_rebuild_button(void) { return (EpdRect){54, 948, 576, 74}; }
static const char *storage_phase_label(storage_repair_phase_t phase) {
    switch (phase) {
        case STORAGE_REPAIR_PREPARE: return "正在释放存储资源…";
        case STORAGE_REPAIR_REMOUNT: return "正在重新挂载 TF 卡…";
        case STORAGE_REPAIR_CHECK: return "正在检查机内存储和阅读记录…";
        case STORAGE_REPAIR_COPY: return "正在备份机内存储，请勿拔卡…";
        case STORAGE_REPAIR_VERIFY: return "正在核验备份与原始数据…";
        case STORAGE_REPAIR_RECHECK: return "正在再次核验，尚未清空机内存储…";
        case STORAGE_REPAIR_REBUILD: return "正在重建机内存储，请勿断电…";
        case STORAGE_REPAIR_BACKUP_READY: return "原始备份已通过校验";
        case STORAGE_REPAIR_FAILED: return "操作未完成";
        case STORAGE_REPAIR_DONE: return "存储检查完成";
        default: return "先检查存储，再决定如何修复";
    }
}
static void storage_repair_draw(uint8_t *fb) {
    const storage_repair_status_t status = app_storage_repair_status();
    back_header(fb, "存储检查与修复");
    ui_text_fixed(fb, 54, 264, 24, "默认保留书籍、设置和阅读记录", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed(fb, 54, 310, 22, "检查 TF、机内存储与进度备用存储", EPD_DRAW_ALIGN_LEFT, false);
    ui_text_fixed(fb, 54, 386, 24, storage_phase_label(status.phase), EPD_DRAW_ALIGN_LEFT, false);
    if (s_storage_rebuild_question) {
        ui_text_fixed(fb, 54, 475, 24, "重建将清空机内书籍与机内记录", EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 54, 527, 23, "设置保留，TF 卡不会格式化", EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 54, 579, 22, "先备份并校验，之后还需再次确认", EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 54, 631, 21, "原始备份不能自动还原旧文件", EPD_DRAW_ALIGN_LEFT, false);
    } else if (status.phase >= STORAGE_REPAIR_COPY) {
        char line[96];
        snprintf(line, sizeof(line), "TF 卡：%s", status.card_error == ESP_ERR_NOT_FINISHED ? "未检查" :
                 status.card_error == ESP_OK ? "可用" : "暂不可用");
        ui_text_fixed(fb, 54, 470, 24, line, EPD_DRAW_ALIGN_LEFT, false);
        snprintf(line, sizeof(line), "机内存储：%s", status.flash_error == ESP_ERR_NOT_FINISHED ? "未检查" :
                 status.flash_error == ESP_OK ? "可用" : "暂不可用");
        ui_text_fixed(fb, 54, 518, 24, line, EPD_DRAW_ALIGN_LEFT, false);
        snprintf(line, sizeof(line), "阅读记录：%s", status.progress_error == ESP_ERR_NOT_FINISHED ? "未检查" :
                 status.progress_error != ESP_OK ? "暂不可用" :
                 status.card_progress ? "使用 TF 备用存储" : "使用机内存储");
        ui_text_fixed(fb, 54, 566, 23, line, EPD_DRAW_ALIGN_LEFT, false);
        if (status.total && app_storage_repair_busy()) {
            snprintf(line, sizeof(line), "进度：%u%%", (unsigned)(status.done * 100 / status.total));
            ui_text_fixed(fb, 54, 620, 23, line, EPD_DRAW_ALIGN_LEFT, false);
        }
    }
    if (status.phase == STORAGE_REPAIR_BACKUP_READY) {
        ui_text_fixed(fb, 54, 630, 22, "下一步会清空机内存储，请再次确认", EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, 54, 684, 21, "TF:/kiikoread-recovery", EPD_DRAW_ALIGN_LEFT, false);
        const char *folder = strstr(status.backup_path, "/storage-");
        if (folder) {
            char name[48]; const char *end = strchr(folder + 1, '/');
            snprintf(name, sizeof(name), "%.*s", end ? (int)(end - folder - 1) : 31, folder + 1);
            ui_text_fixed(fb, 54, 730, 21, name, EPD_DRAW_ALIGN_LEFT, false);
        }
    } else if (status.phase == STORAGE_REPAIR_FAILED) {
        const char *message = status.erase_started ? "重建未完成，原始备份已保留" :
            status.error == ESP_ERR_NO_MEM ? "内存不足，机内原数据未清空" :
            status.error == ESP_ERR_INVALID_SIZE ? "TF 空间不足，请至少预留 6 MB" :
            status.error == ESP_ERR_INVALID_STATE ? "存储被占用或确认已失效，请重试" :
            "请检查 TF 卡，重新检查或重做备份";
        ui_text_box(fb, (EpdRect){54, 670, 576, 112}, 22, message, EPD_DRAW_ALIGN_LEFT, false);
    } else if (status.phase == STORAGE_REPAIR_DONE && status.flash_error != ESP_OK) {
        ui_text_box(fb, (EpdRect){54, 650, 576, 140}, 22,
                    "机内存储仍不可用；若格式无法识别，可先备份后重建。TF 书籍可继续使用。",
                    EPD_DRAW_ALIGN_LEFT, false);
    } else if (status.phase == STORAGE_REPAIR_DONE && status.erase_started) {
        ui_text_box(fb, (EpdRect){54, 650, 576, 140}, 22,
                    "机内存储已重建；原始备份保留在 TF:/kiikoread-recovery。",
                    EPD_DRAW_ALIGN_LEFT, false);
    }
    const bool ready = status.phase == STORAGE_REPAIR_BACKUP_READY;
    ui_draw_button(fb, storage_check_button(), app_storage_repair_busy() ? "操作中，请稍候…" :
                   (ready || s_storage_rebuild_question) ? "取消重建" : "检查并尝试恢复", false);
    ui_draw_button(fb, storage_rebuild_button(), ready ? "确认清空并重建" :
                   s_storage_rebuild_question ? "确认并开始备份" : "备份后重建", true);
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

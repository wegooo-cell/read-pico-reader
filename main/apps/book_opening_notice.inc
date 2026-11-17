// 在读盘和排版前局刷提示，首屏或错误页面自然盖掉它。
// Present the notice before file I/O and layout; the first page or error screen replaces it.
static void show_book_opening(app_ctx_t *ctx) {
    if (!ctx->fb || !ctx->hl) return;
    const int width = 248, height = 48;
    const EpdRect notice = {(UI_LOCK_WIDTH - width) / 2,
        UI_LOCK_HEIGHT * 31 / 100 - height / 2, width, height};
    ui_fill_round_rect(ctx->fb, notice, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(ctx->fb, notice, 24, UI_GRAY_BLACK);
    ui_text_vc(ctx->fb, UI_LOCK_WIDTH / 2, notice.y + height / 2,
               20, "正在打开书籍", EPD_DRAW_ALIGN_CENTER, false);
    guard_draw_result(ctx->hl, update_display_click_feedback(ctx->hl, notice));
}

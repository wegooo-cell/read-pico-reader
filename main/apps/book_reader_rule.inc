// 两板共用：触摸回调只排队，浅栈重排后立即应用阅读线且保留阅读位置。
// Shared by both boards: defer reflow out of touch callbacks and retain the text anchor.
static app_redraw_t apply_reader_rule(app_ctx_t *ctx) {
    (void)ctx;
#if KIIKO_BOARD_METALIO47
    pause_reader_prep();
#else
    invalidate_prep();
#endif
    s_reader_rule_pending = true;
    return APP_REDRAW_NONE;
}
static app_redraw_t reader_rule_tick(app_ctx_t *ctx) {
    (void)ctx;
    s_reader_rule_pending = false;
    if (s_view != READING || !s_text) return APP_REDRAW_NONE;
    lock_draw();
    size_t off = book_layout_page_start_offset(s_page);
    invalidate_prep();
    book_layout_set_reading_line(app_settings_book_reading_line());
    book_layout_set_reading_line_full(app_settings_book_reading_line_full());
    book_layout_set_reading_line_offset(app_settings_book_reading_line_offset());
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    unlock_draw();
    if (!ok) {
        free_book();
        s_view = SHELF;
        copy_text(s_message, sizeof(s_message), "排版失败，请重新扫描并打开图书");
    } else {
        save_progress();
        prepare_inline_image();
    }
    return APP_REDRAW_PAGE;
}

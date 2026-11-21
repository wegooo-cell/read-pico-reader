/* SPDX-License-Identifier: Apache-2.0
 * Shared library transactions. Discovery/cancellation never enroll files.
 * 关闭首次提示只影响本次显示，返回书架仍由未导入状态重新提示。
 * Dismissing the initial prompt is temporary; unimported libraries prompt again on entry.
 */
static void library_filter_loaded(app_ctx_t *ctx) {
    if(s_import_candidates||s_manage_books)return;
    (void)book_library_mode();if(book_library_error()!=ESP_OK)return;
    int kept=0;
    for(int i=0;i<s_count;++i)if(book_library_contains(s_shelf[i].path)&&!shelf_hidden_load(s_shelf[i].path)){
        s_shelf[i].selected=false;s_shelf[kept++]=s_shelf[i];
    }
    if(kept!=s_count){s_count=kept;sort_shelf(ctx);invalidate_covers();}
}
static void library_scan_again(app_ctx_t *ctx) {
    book_library_invalidate();s_shelf_cache_valid=false;s_scan_pending=true;s_poll_ms=0;
    s_shelf_warning[0]=s_batch_message[0]=s_message[0]=0;
    ctx->leaf=0;invalidate_covers();
}
static app_redraw_t library_restart_import(app_ctx_t *ctx) {
    // 先保存清空结果，再重置界面；失败时原列表仍可继续使用。
    // Persist the empty shelf before changing the UI; failures retain the current library.
    book_import_reset();
    if(book_library_reset()!=ESP_OK){
        copy_text(s_batch_message,sizeof(s_batch_message),"清空书架失败，请重试");return APP_REDRAW_PAGE;
    }
    clear_selection();s_count=s_visible_count=0;s_manage_dir[0]=s_query[0]=0;
    s_manage_books=s_import_candidates=s_import_return_bulk=s_bulk_selecting=false;
    s_sort_menu=s_rescan_prompt=s_import_all_pending=false;
    s_filter=s_category_selected=0;s_view=SHELF;
    library_scan_again(ctx);s_scan_pending=false;s_library_setup_pending=true;
    return APP_REDRAW_PAGE;
}
static app_redraw_t library_open_candidates(app_ctx_t *ctx) {
    book_import_reset();s_manage_dir[0]=0;s_count=s_visible_count=0;
    s_import_return_bulk=s_view==BULK;s_import_all_pending=false;s_rescan_prompt=false;
    s_library_setup_pending=false;s_import_candidates=true;s_manage_books=false;
    s_view=BULK;s_bulk_selecting=true;s_bulk_grid=false;s_sort_menu=false;s_filter=0;s_category_selected=0;
    s_query[0]=0;clear_selection();library_scan_again(ctx);
    copy_text(s_message,sizeof(s_message),"正在查找可导入书籍…");return APP_REDRAW_PAGE;
}
static app_redraw_t library_cancel_candidates(app_ctx_t *ctx) {
    book_import_reset();
    s_import_all_pending=false;s_import_candidates=false;s_bulk_selecting=false;s_manage_books=false;
    s_view=s_import_return_bulk?BULK:SHELF;s_query[0]=0;
    clear_selection();s_count=s_visible_count=0;library_scan_again(ctx);
    s_library_setup_pending=book_library_mode()==BOOK_LIBRARY_UNSET&&book_library_error()==ESP_OK;
    if(s_library_setup_pending){s_view=SHELF;s_scan_pending=false;}
    return APP_REDRAW_PAGE;
}
static bool library_unhide_book(const char *path){return shelf_hidden_save(path,false);}
static void library_restore_import_selection(void){
    for(int i=0;i<s_count;++i)s_shelf[i].selected=book_import_selection(s_shelf[i].path)!=BOOK_IMPORT_NONE;
}
static void library_page_selection(app_ctx_t *ctx,bool selected){
    int start=ctx->leaf*shelf_rows(),end=start+shelf_rows();if(end>s_visible_count)end=s_visible_count;
    for(int i=start;i<end;++i){
        shelf_entry_t *item=&s_shelf[i];
        if(s_import_candidates){
            if(item->shelved||book_import_selection(item->path)==BOOK_IMPORT_COVERED)continue;
            if(book_import_select(item->path,item->is_dir,selected)!=ESP_OK){copy_text(s_batch_message,sizeof(s_batch_message),"选择过多，请分批导入");break;}
        }else if(!item->is_dir)item->selected=selected;
    }
    if(s_import_candidates)library_restore_import_selection();
}
static app_redraw_t library_import_selected(app_ctx_t *ctx) {
    if(s_import_candidates){
        if(!book_import_choice_count()){copy_text(s_batch_message,sizeof(s_batch_message),"请先选择书籍或文件夹");return APP_REDRAW_PAGE;}
        esp_err_t error=book_import_begin(shelf_hidden_load,library_unhide_book);
        if(error!=ESP_OK){copy_text(s_batch_message,sizeof(s_batch_message),"无法开始导入，请检查存储后重试");return APP_REDRAW_PAGE;}
        s_scan_pending=false;invalidate_covers();copy_text(s_batch_message,sizeof(s_batch_message),"正在导入所选书籍…");return APP_REDRAW_PAGE;
    }
    unsigned count=selected_count();
    if(!count){copy_text(s_batch_message,sizeof(s_batch_message),"没有需要导入的书籍");return APP_REDRAW_PAGE;}
    esp_err_t error=book_library_begin_snapshot();
    for(int i=0;error==ESP_OK&&i<s_count;++i)if(s_shelf[i].selected)error=book_library_add(s_shelf[i].path);
    if(error==ESP_OK)error=book_library_commit();
    if(error!=ESP_OK){book_library_abort();copy_text(s_batch_message,sizeof(s_batch_message),"保存书架失败，请检查存储卡后重试");return APP_REDRAW_PAGE;}
    unsigned failed=0;
    for(int i=0;i<s_count;++i)if(s_shelf[i].selected){
        if(shelf_hidden_load(s_shelf[i].path)&&!shelf_hidden_save(s_shelf[i].path,false)){++failed;continue;}
        s_shelf[i].shelved=true;s_shelf[i].selected=false;
    }
    if(failed){snprintf(s_batch_message,sizeof(s_batch_message),"%u 本重新上架失败，请重试",failed);return APP_REDRAW_PAGE;}
    if(!s_import_candidates){library_scan_again(ctx);return APP_REDRAW_PAGE;}
    return library_cancel_candidates(ctx);
}
static app_redraw_t library_import_all(app_ctx_t *ctx) {
    (void)library_open_candidates(ctx);
    if(book_import_select(app_settings_books_dir(),true,true)!=ESP_OK){copy_text(s_batch_message,sizeof(s_batch_message),"内存不足，请手动分批导入");return APP_REDRAW_PAGE;}
    s_import_all_pending=true;return library_import_selected(ctx);
}
static app_redraw_t library_import_tick(app_ctx_t *ctx){
    esp_err_t error=book_import_step(32);if(error==ESP_ERR_NOT_FINISHED)return APP_REDRAW_NONE;
    bool all=s_import_all_pending;s_import_all_pending=false;
    unsigned failed=book_import_unhide_failures();
    if(error==ESP_OK&&!failed)return library_cancel_candidates(ctx);
    if(all&&error!=ESP_OK)book_import_reset();
    library_scan_again(ctx);
    if(failed)snprintf(s_batch_message,sizeof(s_batch_message),"%u 本重新上架失败，可重试",failed);
    else copy_text(s_batch_message,sizeof(s_batch_message),error==ESP_ERR_NOT_FOUND?"所选目录中没有可导入书籍":"目录未能完整导入，已保留原书架，请重试");
    return APP_REDRAW_PAGE;
}
static bool library_remove_selected(void){
    bool legacy=book_library_mode()==BOOK_LIBRARY_ALL;
    esp_err_t error=book_library_begin_snapshot();
    for(int i=0;error==ESP_OK&&i<s_count;++i){
        if(legacy){if(!s_shelf[i].selected)error=book_library_add(s_shelf[i].path);}
        else if(s_shelf[i].selected)error=book_library_remove(s_shelf[i].path);
    }
    if(error==ESP_OK)error=book_library_commit();
    if(error!=ESP_OK){book_library_abort();copy_text(s_batch_message,sizeof(s_batch_message),"移出书架失败，请检查存储后重试");return false;}
    s_shelf_cache_valid=false;return true;
}
static EpdRect library_setup_choice(unsigned i){return (EpdRect){74,530+(int)i*108,536,88};}
static void library_draw_setup(uint8_t *fb) {
    if(s_rescan_prompt)draw_bulk(fb,s_rescan_leaf);
    else{
        ui_clear_page(fb);ui_nav_status(fb);
        ui_text_box(fb,(EpdRect){36,80,500,76},52,"书架",EPD_DRAW_ALIGN_LEFT,false);
        ui_draw_icon(fb,342,286,70,UI_ICON_BOOK_OPEN,0x90);
        ui_text_vc(fb,342,352,32,"空书架",EPD_DRAW_ALIGN_CENTER,false);ui_nav_draw(fb,1);
    }
    EpdRect panel={42,418,600,412};ui_fill_round_rect(fb,panel,24,UI_GRAY_WHITE);ui_draw_control_frame(fb,panel,24,UI_GRAY_BLACK);
    ui_text_box(fb,(EpdRect){96,440,492,68},34,s_rescan_prompt?"重新扫描书籍":"导入书籍",EPD_DRAW_ALIGN_CENTER,false);
    ui_draw_icon(fb,601,460,26,UI_ICON_X,0x40);
    bulk_button(fb,library_setup_choice(0),"一键导入所有书籍",true,29);
    bulk_button(fb,library_setup_choice(1),"手动选择导入书籍",false,29);
    ui_text_vc(fb,342,780,20,"书籍需放在TF卡根目录下的books文件夹",EPD_DRAW_ALIGN_CENTER,false);
    if(s_batch_message[0])ui_text_box(fb,(EpdRect){60,842,564,72},21,s_batch_message,EPD_DRAW_ALIGN_CENTER,false);
}
static app_redraw_t library_setup_action(app_ctx_t *ctx,int x,int y){
    if(ui_rect_hit(library_setup_choice(0),x,y))return library_import_all(ctx);
    if(ui_rect_hit(library_setup_choice(1),x,y))return library_open_candidates(ctx);
    if(ui_rect_hit((EpdRect){570,430,62,62},x,y)){
        s_library_setup_pending=s_rescan_prompt=false;return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

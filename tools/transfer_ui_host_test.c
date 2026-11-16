/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：直接验证触屏配网状态机及输入边界。
 * English: Exercise the device provisioning state machine and input bounds.
 * 冻结：仅供宿主测试，不访问网络。/ Frozen: Host tests only; no network access.
 */
#include "transfer_ui_test_env.h"
#include "../main/apps/app_transfer.c"
#include <assert.h>
#include "../components/read_pico_transfer/transfer_signature.h"
int metalio_haptic_click(void){return ESP_OK;}
static unsigned cover_wifi_returns;
static bool cover_wifi_ready;
void app_book_return_from_cover_wifi(bool connected) {
    ++cover_wifi_returns;
    cover_wifi_ready = connected;
}
static bool test_usb_active;
static esp_err_t test_usb_stop_error;
esp_err_t usb_storage_start(void) { test_usb_active = true; return ESP_OK; }
esp_err_t usb_storage_stop(void) {
    if (test_usb_stop_error != ESP_OK) return test_usb_stop_error;
    test_usb_active = false;
    return ESP_OK;
}
bool usb_storage_active(void) { return test_usb_active; }
bool usb_storage_connected(void) { return test_usb_active; }
static void tap(app_ctx_t* ctx, EpdRect rect) {
    ui_gesture_event_t ev = {.type = UI_GESTURE_PRESS, .x0 = rect.x + 2, .y0 = rect.y + 2, .x = rect.x + 2, .y = rect.y + 2};
    app_redraw_t pressed = on_gesture(ctx, &ev);
    // 整页刷新在 app_loop 中会取消活动手势；入口反馈必须保留松手事件。
    // app_loop cancels active gestures on a page refresh; feedback must preserve release.
    assert(pressed != APP_REDRAW_PAGE && pressed != APP_REDRAW_FULL);
    ev.type = UI_GESTURE_TAP;
    on_gesture(ctx, &ev);
}
static void qr_regression(app_ctx_t* ctx) {
    test_status = (read_pico_transfer_status_t){.mode=READ_PICO_TRANSFER_MODE_AP,.state=READ_PICO_TRANSFER_READY,.network_ready=true};
    strcpy(test_status.ssid,"ReadPico-test");
    strcpy(test_status.url,"http://192.168.4.1");
    s_mode = READ_PICO_TRANSFER_MODE_AP;
    on_enter(ctx);
    s_view = TRANSFER_HOME;
    s_qr_url = true;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(s_qr_ready && s_qr_url && !strcmp(test_qr_payload,test_status.url));
    tap(ctx, control_rect(0));
    assert(!s_qr_url && !strcmp(test_qr_payload,test_status.ssid));
    tap(ctx, control_rect(1));
    assert(s_qr_url && !strcmp(test_qr_payload,test_status.url));
    int encodes = test_qr_encodes;
    render(ctx,ctx->fb);
    assert(test_qr_encodes == encodes);
    test_status.cur_bytes++;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(test_qr_encodes == encodes);
    strcpy(test_status.url,"http://192.168.4.2");
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!strcmp(test_qr_payload,test_status.url));
    test_status.network_ready=false;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!s_qr_ready && !test_qr_payload[0]);
    encodes=test_qr_encodes;
    render(ctx,ctx->fb);
    assert(test_qr_encodes==encodes);
    test_status.network_ready=true;
    test_qr_failure=true;
    ctx->now_ms += 3000;
    on_tick(ctx);
    assert(!s_qr_ready);
    test_qr_failure=false;
    queue_network_start(READ_PICO_TRANSFER_MODE_STA);
    assert(!s_qr_ready && !test_qr_payload[0]);
    test_status.mode=READ_PICO_TRANSFER_MODE_STA;
    strcpy(test_status.url,"http://10.20.30.40");
    on_tick(ctx);
    assert(s_qr_url && s_qr_ready && !strcmp(test_qr_payload,test_status.url));
    encodes=test_qr_encodes;
    render(ctx,ctx->fb);
    assert(test_qr_encodes==encodes);
    stop_session();
    assert(!s_qr_ready && !test_qr_payload[0]);
    test_status=(read_pico_transfer_status_t){0};
    s_mode=READ_PICO_TRANSFER_MODE_AP;
    test_qr_encodes=0;
    ctx->now_ms=0;
}
// 页面标记可能因旧会话重连失败而丢失，确认遗忘仍须释放实际射频并取消排队连接。
// A failed restart can lose the page's session flag; forgetting must still stop the radio and queued reconnect.
static void wifi_recovery_regression(app_ctx_t* ctx) {
    test_configured = s_saved_configured = true;
    s_view = TRANSFER_NETWORKS;
    s_wifi_settings_only = true;
    s_session_started = false;
    s_start_pending = s_network_connect_pending = s_scan_pending = false;
    test_status = s_status = (read_pico_transfer_status_t){.mode=READ_PICO_TRANSFER_MODE_STA,
        .state=READ_PICO_TRANSFER_ERROR,.wifi_configured=true};
    test_wifi_running = true;
    int stops = test_stop_count;
    tap(ctx, network_control_rect(6));
    assert(!test_wifi_running && test_stop_count > stops && s_network_connect_pending);
    network_ui_tick(ctx);
    assert(s_session_started && !s_network_connect_pending);
    // 上传中不能遗忘；停止后可以重试。/ Forgetting waits for an active upload, then remains retryable.
    test_wifi_running = true;
    s_session_started = false;
    s_network_connect_pending = true;
    tap(ctx, network_control_rect(11));
    test_busy = true;
    stops = test_stop_count;
    int forgets = test_forget_count;
    tap(ctx, network_control_rect(11));
    assert(s_forget_confirm && test_wifi_running && test_stop_count == stops && test_forget_count == forgets);
    test_busy = false;
    tap(ctx, network_control_rect(11));
    assert(!s_forget_confirm && !test_wifi_running && !test_configured && !s_saved_configured);
    assert(!s_start_pending && !s_network_connect_pending && !s_scan_pending);
    network_ui_tick(ctx);
    assert(!s_session_started && !s_network_connect_pending);
    // 遗忘后重新选择同名网络会进入密码输入。/ Selecting the forgotten network asks for a password again.
    s_network_page = 0;
    tap(ctx, network_control_rect(0));
    assert(s_view == TRANSFER_PASSWORD && !s_password[0]);
    clear_password();
    s_wifi_settings_only = false;
    s_forget_confirm = false;
    test_status = (read_pico_transfer_status_t){0};
}
static void password_caret_regression(app_ctx_t *ctx) {
    clear_password();ui_keyboard_begin(&s_password_input,true);
    assert(s_password_input.cursor==0);
    tap(ctx,(EpdRect){24,1010,30,30}); // 数字页。/ Number panel.
    for(int i=0;i<8;i++)tap(ctx,(EpdRect){24+i*64,740,30,30});
    assert(!strcmp(s_password,"12345678")&&s_password_input.cursor==8);
    EpdRect field=password_control_rect(47);
    tap(ctx,(EpdRect){field.x+17+3*20-2,329,4,4});assert(s_password_input.cursor==3);
    tap(ctx,(EpdRect){24+64,740,30,30});assert(!strcmp(s_password,"123245678")&&s_password_input.cursor==4);
    tap(ctx,password_control_rect(45));assert(s_password_visible&&s_password_input.cursor==4);
    tap(ctx,password_control_rect(45));assert(!s_password_visible&&s_password_input.cursor==4);
    ui_text_edit_place(&s_password_input,3);
    tap(ctx,(EpdRect){600,896,30,30});assert(!strcmp(s_password,"12245678")&&s_password_input.cursor==2);
    ui_text_edit_place(&s_password_input,0);tap(ctx,(EpdRect){600,896,30,30});assert(!strcmp(s_password,"12245678"));
    clear_password();assert(!s_password[0]&&!s_password_input.cursor&&!s_password_visible);
    ui_keyboard_begin(&s_password_input,true);
    strcpy(s_password,"abcdefgh");ui_text_edit_init(&s_password_input,s_password,sizeof(s_password));
    cst836u_touch_t touch={.touched=true,.count=1,.x=610,.y=930};ctx->touch=&touch;ctx->now_ms=1000;
    ui_gesture_event_t held={.type=UI_GESTURE_PRESS,.x0=610,.y0=930,.x=610,.y=930};
    assert(on_gesture(ctx,&held)==APP_REDRAW_AREA && !s_input_layout);
    ctx->consumed=true;assert(on_tick(ctx)==APP_REDRAW_NONE);ctx->consumed=false;
    ctx->now_ms=1499;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdefgh"));
    ctx->now_ms=1500;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcdefg"));assert(!s_input_layout);
    ctx->now_ms=1620;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcdef"));
    touch.count=2;ctx->now_ms=1740;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdef"));
    touch.count=1;ctx->now_ms=2000;assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcdef"));
    held.type=UI_GESTURE_PRESS;on_gesture(ctx,&held);ctx->now_ms=2500;assert(on_tick(ctx)==APP_REDRAW_AREA&&!strcmp(s_password,"abcde"));
    held.type=UI_GESTURE_CANCEL;touch.touched=false;on_gesture(ctx,&held);ctx->now_ms=2800;
    assert(on_tick(ctx)==APP_REDRAW_NONE&&!strcmp(s_password,"abcde"));ctx->touch=NULL;ctx->now_ms=0;
    clear_password();ui_keyboard_begin(&s_password_input,true);
}
int main(void) {
    assert(transfer_signature_valid("",0)&&transfer_signature_valid("中文🙂",10));
    assert(!transfer_signature_valid("a\0b",3)&&!transfer_signature_valid("\xed\xa0\x80",3));
    assert(!transfer_signature_valid("\xc0\xaf",2)&&!transfer_signature_valid("\xf4\x90\x80\x80",4));
    assert(!transfer_signature_valid("\xe4\xb8",2)&&!transfer_signature_valid("a\nb",3));
    strcpy(test_signature,"reader");char signature[96];assert(transfer_signature_get(signature,sizeof(signature))&&!strcmp(signature,"reader"));
    test_signature_fail=true;assert(transfer_signature_set("新签名")==ESP_FAIL&&!strcmp(test_signature,"reader"));
    test_signature_fail=false;assert(transfer_signature_set("新签名")==ESP_OK&&!strcmp(test_signature,"新签名"));
    assert(s_signature_revision==1);s_signature_seen=s_signature_revision;
    uint8_t fb = 0;
    app_ctx_t ctx = {.fb = &fb};
    app_transfer_request_method_picker();
    on_enter(&ctx);
    assert(s_view == TRANSFER_METHODS && !s_wifi_settings_only && !s_start_pending);
    on_tick(&ctx);
    assert(test_qr_encodes == 0);
    qr_regression(&ctx);
    test_configured = true;
    on_enter(&ctx);
    tap(&ctx, method_control_rect(0));
    assert(s_view == TRANSFER_HOME && s_start_pending && !s_scan_pending);
    assert(s_mode == READ_PICO_TRANSFER_MODE_STA);
    on_tick(&ctx);
    tap(&ctx, control_rect(2));
    assert(s_view == TRANSFER_METHODS && !s_start_pending);
    test_configured = false;
    on_enter(&ctx);
    ui_gesture_event_t cancelled = {.type=UI_GESTURE_PRESS, .x0=50, .y0=350, .x=50, .y=350};
    assert(on_gesture(&ctx, &cancelled) == APP_REDRAW_AREA);
    cancelled.type = UI_GESTURE_CANCEL;
    assert(on_gesture(&ctx, &cancelled) == APP_REDRAW_AREA);
    assert(s_view == TRANSFER_METHODS && !s_start_pending && s_pressed == -1);
    for (int i = 44; i < 48; ++i) {
        EpdRect r = password_control_rect(i);
        assert(r.x >= 0 && r.x + r.width <= UI_LOCK_WIDTH);
        assert(r.y >= 0 && r.y + r.height <= UI_LOCK_HEIGHT);
    }
    on_enter(&ctx);
    test_busy = true;
    tap(&ctx, method_control_rect(0));
    assert(s_view == TRANSFER_METHODS);
    test_busy = false;
    tap(&ctx, method_control_rect(0));
    assert(s_view == TRANSFER_NETWORKS && s_scan_pending);
    network_ui_tick(&ctx);
    assert(s_network_count == 8 && !s_scan_pending);
    test_configured = s_saved_configured = true;
    tap(&ctx, (EpdRect){540, UI_NAV_TOP + 16, 20, 20});
    // 底栏由真实app_loop统一处理，子页手势本身不派发导航。/ app_loop owns navigation; page gestures do not dispatch it.
    assert(test_nav_index == -1);
    test_nav_index = -1;
    assert(s_view == TRANSFER_NETWORKS);
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 0);
    tap(&ctx, network_control_rect(12));
    assert(test_forget_count == 0 && s_saved_configured && !s_forget_confirm);
    tap(&ctx, network_control_rect(11));
    test_forget_error = ESP_FAIL;
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 1 && s_saved_configured && s_forget_confirm);
    test_forget_error = ESP_OK;
    tap(&ctx, network_control_rect(11));
    assert(test_forget_count == 2 && !s_saved_configured);
    test_configured = true;
    enter_networks();
    network_ui_tick(&ctx);
    tap(&ctx, network_control_rect(9));
    assert(s_network_page == 1);
    tap(&ctx, network_control_rect(7));
    assert(s_network_page == 0);
    // 断开保留凭据，上传期间拒绝断开，之后仍可重连。
    // Disconnect keeps credentials, refuses an active upload and allows reconnecting.
    test_status.network_ready = s_status.network_ready = true;
    s_session_started = true;
    test_busy = true;
    int stops = test_stop_count;
    tap(&ctx, network_control_rect(6));
    assert(test_stop_count == stops && s_status.network_ready);
    test_busy = false;
    tap(&ctx, network_control_rect(6));
    assert(test_stop_count > stops && !s_session_started && s_saved_configured && test_configured);
    assert(!s_start_pending && !s_network_connect_pending && test_forget_count == 2);
    test_status.network_ready = s_status.network_ready = false;
    tap(&ctx, network_control_rect(6));
    assert(s_start_pending && s_mode == READ_PICO_TRANSFER_MODE_STA);
    s_start_pending = false; s_view = TRANSFER_NETWORKS;
    tap(&ctx, network_control_rect(9));
    assert(s_network_page == 1);
    tap(&ctx, network_control_rect(3));
    assert(s_view == TRANSFER_PASSWORD && !strcmp(s_selected.ssid, "network7"));
    s_view = TRANSFER_NETWORKS; s_network_page = 0;
    tap(&ctx, network_control_rect(0));
    assert(s_view == TRANSFER_PASSWORD && !s_password[0]);
    tap(&ctx, password_control_rect(46));
    assert(test_save_count == 0 && s_view == TRANSFER_PASSWORD);
    password_caret_regression(&ctx);
    ui_gesture_event_t cancel = {.type = UI_GESTURE_PRESS,.x0 = 42,.y0 = 422,.x = 42,.y = 422};
    on_gesture(&ctx, &cancel);
    cancel.type = UI_GESTURE_TAP;
    cancel.x = 650;
    on_gesture(&ctx, &cancel);
    assert(!s_password[0]);
    // 容量及保存失败时保留密码。/ Capacity and preservation after failed saving.
    tap(&ctx,(EpdRect){24,1010,30,30});
    for(int i=0;i<70;i++)tap(&ctx,(EpdRect){24,740,30,30});assert(strlen(s_password)==64);
    tap(&ctx,(EpdRect){600,896,30,30});assert(strlen(s_password)==63);
    tap(&ctx, password_control_rect(45));
    assert(s_password_visible);
    test_save_error = ESP_FAIL;
    tap(&ctx, password_control_rect(46));
    assert(s_view == TRANSFER_PASSWORD && strlen(s_password) == 63);
    test_save_error = ESP_OK;
    tap(&ctx, password_control_rect(46));
    assert(s_view == TRANSFER_HOME && s_mode == READ_PICO_TRANSFER_MODE_STA && s_start_pending);
    assert(!s_password[0]);
    assert(strcmp(test_saved_ssid, "中文家庭网络") == 0);
    enter_networks();
    network_ui_tick(&ctx);
    tap(&ctx, network_control_rect(0));
    tap(&ctx,(EpdRect){24,740,30,30});
    tap(&ctx, password_control_rect(44));
    assert(s_view == TRANSFER_NETWORKS && !s_password[0]);
    test_scan_count = 0;
    tap(&ctx, network_control_rect(8));
    network_ui_tick(&ctx);
    assert(!s_network_count && s_network_message[0]);
    test_scan_error = ESP_FAIL;
    tap(&ctx, network_control_rect(8));
    network_ui_tick(&ctx);
    assert(!s_network_count && s_network_message[0]);
    tap(&ctx, network_control_rect(6));
    assert(s_view == TRANSFER_HOME && s_start_pending && s_mode == READ_PICO_TRANSFER_MODE_STA);
    int stops_before = test_stop_count;
    tap(&ctx, control_rect(2));
    assert(test_stop_count == stops_before + 1 && !s_start_pending);
    assert(s_view == TRANSFER_METHODS && !ctx.request_return && ctx.request_app == NULL);
    on_enter(&ctx);
    tap(&ctx, method_control_rect(1));
    on_tick(&ctx);
    test_busy = true;
    stops_before = test_stop_count;
    tap(&ctx, control_rect(2));
    assert(test_stop_count == stops_before && s_view == TRANSFER_HOME && !ctx.request_return);
    tap(&ctx, (EpdRect){540, UI_NAV_TOP + 16, 20, 20});
    assert(test_stop_count == stops_before && test_nav_index == -1);
    test_busy = false;
#if KIIKO_BOARD_METALIO47
    // Small live progress is bounded to the two-second poll, with failed scans retried.
    s_status.state=READ_PICO_TRANSFER_READY;test_status.state=READ_PICO_TRANSFER_UPLOADING;
    strcpy(test_status.cur_name,"font-one.ttf");
    test_status.cur_total=100000;test_status.cur_bytes=32000;s_poll_ms=0;ctx.now_ms=3000;
    assert(on_tick(&ctx)==APP_REDRAW_AREA&&s_status.state==READ_PICO_TRANSFER_UPLOADING&&s_status.cur_bytes==32000);
    int scans=test_display_calls;
    assert(present(&ctx,APP_REDRAW_AREA)&&test_display_calls==scans+1);
    assert(test_display_wave==display_main_bw_waveform()&&test_display_mode==MODE_DU);
    EpdRect card=status_rect();
    assert(test_display_area.x>card.x&&test_display_area.y>card.y);
    assert(test_display_area.width<card.width&&test_display_area.height<card.height);
    assert(test_display_area.y+test_display_area.height<card.y+190);
    test_status.cur_bytes=64000;ctx.now_ms+=1000;
    assert(on_tick(&ctx)==APP_REDRAW_NONE&&test_display_calls==scans+1);
    ctx.now_ms+=1000;
    assert(on_tick(&ctx)==APP_REDRAW_AREA&&s_status.cur_bytes==64000);
    test_display_error=1;
    assert(present(&ctx,APP_REDRAW_AREA)&&s_progress_pending);
    ctx.now_ms+=2000;
    assert(on_tick(&ctx)==APP_REDRAW_AREA); // Unchanged bytes must still retry.
    test_display_error=0;
    assert(present(&ctx,APP_REDRAW_AREA)&&!s_progress_pending);
    ctx.now_ms+=2000;
    assert(on_tick(&ctx)==APP_REDRAW_NONE);
    strcpy(test_status.cur_name,"font-two.ttf");ctx.now_ms+=2000;
    assert(on_tick(&ctx)==APP_REDRAW_AREA);
    assert(present(&ctx,APP_REDRAW_AREA));
    test_status.state=READ_PICO_TRANSFER_ERROR;test_status.last_error=ESP_FAIL;ctx.now_ms+=2000;
    assert(on_tick(&ctx)==APP_REDRAW_AREA&&!s_progress_pending);
    test_status.last_error=ESP_OK;
    test_status.state=READ_PICO_TRANSFER_READY;ctx.now_ms+=3000;
    assert(on_tick(&ctx)!=APP_REDRAW_NONE);
#endif
    test_status.changed_count = 1;
    s_poll_ms=0;ctx.now_ms = 3000;
    assert(on_tick(&ctx) == APP_REDRAW_AREA);
    assert(s_status.changed_count == 1);
    s_session_started = true;
    transfer_on_exit(&ctx);
    assert(test_store_changes == 1);
    on_enter(&ctx);
    tap(&ctx, method_control_rect(1));
    on_tick(&ctx);
    s_root.is_flash = false;
    s_free = 123456;
    stops_before = test_stop_count;
    app_transfer.on_media_lost(&ctx);
    assert(test_stop_count == stops_before + 1);
    assert(s_media_lost && !s_free && !s_qr_ready && !s_root.path[0]);
    assert(!s_start_pending && !s_session_started && s_view == TRANSFER_METHODS);
    s_root.is_flash = true;
    app_transfer.on_media_lost(&ctx);
    assert(test_stop_count == stops_before + 1);
    ctx.request_return = false;
    test_configured = true;
    app_transfer_request_wifi_upload();
    on_enter(&ctx);
    assert(s_direct_entry && s_view == TRANSFER_HOME && s_start_pending);
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && s_view == TRANSFER_HOME);
    ctx.request_return = false;
    app_transfer_request_hotspot_start();
    on_enter(&ctx);
    assert(s_direct_entry && s_view == TRANSFER_HOME && s_start_pending);
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && s_view == TRANSFER_HOME);
    ctx.request_return = false;
    app_transfer_request_usb_start();
    on_enter(&ctx);
    assert(s_usb_start_pending && !test_usb_active);
    assert(on_tick(&ctx) == APP_REDRAW_PAGE);
    assert(s_direct_entry && s_usb_entry && s_view == TRANSFER_HOME && test_usb_active);
    test_usb_stop_error = ESP_FAIL;
    tap(&ctx, (EpdRect){36, 79, 50, 50});
    assert(test_usb_active && !ctx.request_return && s_view == TRANSFER_HOME);
    assert(on_key(&ctx, UI_KEY_2) == APP_REDRAW_PAGE && !ctx.request_app);
    assert(test_usb_active && s_usb_message[0]);
    test_usb_stop_error = ESP_OK;
    tap(&ctx, control_rect(2));
    assert(ctx.request_return && !test_usb_active);
    enter_networks();
    test_scan_count = 8; test_scan_error = ESP_OK;
    network_ui_tick(&ctx);
    wifi_recovery_regression(&ctx);
    // 配网子页返回保留在线STA，后台对时可以继续。/ Keep ready STA alive on setup return so NTP can complete.
    app_transfer_request_wifi_setup();on_enter(&ctx);
    test_status=(read_pico_transfer_status_t){.mode=READ_PICO_TRANSFER_MODE_STA,.state=READ_PICO_TRANSFER_READY,.network_ready=true};
    s_session_started=true;int before_exit=test_stop_count;transfer_on_exit(&ctx);
    assert(test_stop_count==before_exit&&!s_wifi_settings_only&&!s_session_started);
    const read_pico_transfer_status_t ready = {.mode=READ_PICO_TRANSFER_MODE_STA,
        .state=READ_PICO_TRANSFER_READY, .network_ready=true, .wifi_configured=true, .cover_lookup=true};
    ctx.request_return = false;
    test_status = ready;
    app_transfer_request_cover_wifi();
    on_enter(&ctx);
    assert(s_view == TRANSFER_NETWORKS && s_cover_wifi_only && !transfer_navigation_enabled(&ctx));
    assert(network_ui_tick(&ctx) == APP_REDRAW_NONE && !ctx.request_return);
    tap(&ctx, network_control_rect(6));
    assert(ctx.request_return && cover_wifi_returns == 1 && cover_wifi_ready);
    transfer_on_exit(&ctx);
    assert(!s_cover_wifi_only && test_status.network_ready);

    ctx.request_return = false;
    app_transfer_request_cover_wifi();
    on_enter(&ctx);
    tap(&ctx, (EpdRect){36, 79, 50, 50});
    assert(ctx.request_return && cover_wifi_returns == 2 && !cover_wifi_ready);
    transfer_on_exit(&ctx);

    ctx.request_return = false;
    test_status = (read_pico_transfer_status_t){0};
    app_transfer_request_cover_wifi();
    on_enter(&ctx);
    s_session_started = true;
    test_status = ready;
    assert(network_ui_tick(&ctx) == APP_REDRAW_NONE);
    assert(ctx.request_return && cover_wifi_returns == 3 && cover_wifi_ready);
    transfer_on_exit(&ctx);

    // The connected general-purpose STA is replaced on Continue, with saved credentials.
    ctx.request_return = false;
    test_status = ready;
    test_status.cover_lookup = false;
    app_transfer_request_cover_wifi();
    on_enter(&ctx);
    tap(&ctx, network_control_rect(6));
    assert(!ctx.request_return && s_network_connect_pending && cover_wifi_returns == 3);
    test_status = (read_pico_transfer_status_t){.mode=READ_PICO_TRANSFER_MODE_STA,
        .state=READ_PICO_TRANSFER_STARTING, .cover_lookup=true};
    assert(network_ui_tick(&ctx) == APP_REDRAW_PAGE && test_cover_profile_requested);
    assert(!ctx.request_return);
    test_status = ready;
    assert(network_ui_tick(&ctx) == APP_REDRAW_NONE && ctx.request_return &&
           cover_wifi_returns == 4 && cover_wifi_ready);
    transfer_on_exit(&ctx);
    puts("transfer_ui_host_test: PASS");
}

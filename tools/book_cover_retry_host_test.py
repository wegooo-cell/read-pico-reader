#!/usr/bin/env python3
"""Exercise the actual cover HTTP retry and direct-entry Wi-Fi state machine."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

root = Path(__file__).resolve().parents[1]
network = root / "main/book/book_weread_cover.c"
ui = root / "main/apps/book_manage_ui.h"
source = r'''
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE,
       ESP_ERR_INVALID_SIZE, ESP_ERR_TIMEOUT, ESP_ERR_NOT_FINISHED, ESP_ERR_INVALID_RESPONSE,
       ESP_ERR_HTTP_CONNECT, ESP_ERR_HTTP_EAGAIN, ESP_ERR_HTTP_CONNECTING,
       ESP_ERR_HTTP_CONNECTION_CLOSED, ESP_ERR_HTTP_FETCH_HEADER, ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT };
enum {HTTP_ADDR_TYPE_INET = 2, HTTP_METHOD_GET, HTTP_METHOD_POST};
typedef enum { COVER_FETCH_JSON, COVER_FETCH_IMAGE, COVER_FETCH_ZLIB_SEARCH,
               COVER_FETCH_ZLIB_PREVIEW, COVER_FETCH_ZLIB_IMAGE } cover_fetch_t;
#define ESP_LOGW(tag, ...) ((void)0)
#define ESP_LOGI(tag, format, label, ...) ((void)(label))
#define pdMS_TO_TICKS(ms) (ms)
typedef struct {size_t size; bool overflow, prefer_zlib_proxy, prefer_weserv; char location[640];} http_response_t;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(size, caps) malloc(size)
typedef struct {
    const char *url;
    int crt_bundle_attach, timeout_ms, addr_type, event_handler, method;
    bool disable_auto_redirect, is_async;
    http_response_t *user_data;
} esp_http_client_config_t;
typedef http_response_t *esp_http_client_handle_t;
static int esp_crt_bundle_attach, collect;
static bool stop, queued, stalled, truncated;
static int64_t fake_us;
static char last_request[704];
static unsigned calls, delays, header_count;
typedef struct {int code, result, tls, flags;} outcome_t;
static outcome_t outcomes[3];
static bool cancelled(void) {return stop;}
static bool save_pending(void) {return queued;}
static int64_t esp_timer_get_time(void) {return fake_us;}
static void vTaskDelay(int ms) {
    assert(ms == 300 || ms == 20);
    if (ms == 300) ++delays;
    fake_us += (int64_t)ms * 1000;
}
static bool cover_url(char *out, const char *in) {(void)out; (void)in; return false;}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    assert(c->disable_auto_redirect && c->addr_type == HTTP_ADDR_TYPE_INET);
    snprintf(last_request, sizeof(last_request), "%s", c->url);
    const int timeout = c->method == HTTP_METHOD_POST ? 6000 : !c->is_async ? 12000 :
        strstr(c->url, "i0.wp.com") ? 8000 : strstr(c->url, "images.weserv.nl") ? 12000 : 5000;
    assert(c->timeout_ms == timeout);
    return c->user_data;
}
static esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *name, const char *value) {
    assert(c && name[0] && value[0]); ++header_count;
    return ESP_OK;
}
static esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t c, const char *data, size_t size) {
    assert(c && size == strlen(data) && !strcmp(data, "message=test&page=1&limit=12"));
    return ESP_OK;
}
static int esp_http_client_perform(esp_http_client_handle_t c) {
    if (stalled) { ++calls; return ESP_ERR_HTTP_EAGAIN; }
    assert(calls < 3);
    const outcome_t o = outcomes[calls++];
    if (o.code == 200) c->size = 25;
    return o.result;
}
static int esp_http_client_get_status_code(esp_http_client_handle_t c) {(void)c; return stalled || !calls ? 0 : outcomes[calls-1].code;}
static int esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t c, int *code, int *flags) {
    (void)c; *code = 0; *flags = stalled || !calls ? 0 : outcomes[calls-1].flags;
    return stalled || !calls ? 0 : outcomes[calls-1].tls;
}
static int64_t esp_http_client_get_content_length(esp_http_client_handle_t c) {
    return (int64_t)c->size + truncated;
}
static bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) {
    (void)c;
    return !truncated;
}
static void esp_http_client_cleanup(esp_http_client_handle_t c) {(void)c;}
'''
source += function("perform_zlib_image", network) + "\n" + function("fetch_attempt", network) + "\n" + function("fetch", network)
source += "\n" + function("append_keyword", network) + "\n" + function("zlib_proxy_url", network) + "\n" + function("fetch_zlib_image", network)
source += r'''
enum {READ_PICO_TRANSFER_MODE_AP, READ_PICO_TRANSFER_MODE_STA};
enum {READ_PICO_TRANSFER_STOPPED, READ_PICO_TRANSFER_STARTING, READ_PICO_TRANSFER_READY, READ_PICO_TRANSFER_ERROR};
typedef struct {bool network_ready,cover_lookup; int mode,state;} read_pico_transfer_status_t;
typedef struct {int mode; bool cover_lookup,network_only;} read_pico_transfer_cfg_t;
static read_pico_transfer_status_t wifi;
static void read_pico_transfer_get_status(read_pico_transfer_status_t *out) {*out=wifi;}
static bool read_pico_transfer_try_stop_if_idle(void) {return true;}
static void ttf_font_cache_clear(void) {}
static int starts;
static esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t *config) {
    assert(config->mode == READ_PICO_TRANSFER_MODE_STA && config->network_only && config->cover_lookup);
    ++starts;
    wifi.network_ready = false;
    wifi.state = READ_PICO_TRANSFER_STARTING;
    return ESP_OK;
}
typedef int app_redraw_t;
typedef int app_desc_t;
typedef struct {int64_t now_ms; const app_desc_t *request_app;} app_ctx_t;
const app_desc_t app_transfer = 1;
enum {APP_REDRAW_NONE, APP_REDRAW_PAGE};
enum {BULK, COVER_LOOKUP};
enum {COVER_CONNECT_IDLE, COVER_CONNECT_START, COVER_CONNECT_WAIT};
static int s_view, s_cover_connection, pending_setup;
static int64_t s_cover_connect_deadline;
static bool s_cover_filtering,s_cover_follow;
static char s_cover_error[96],s_batch_message[96];
static struct {bool running,saving,active; unsigned saved;} s_cover_status;
static unsigned s_cover_count;
#define book_weread_cover_status() s_cover_status
static bool pico_online_busy(void) {return false;}
static bool read_pico_sd_io_ready(void) {return true;}
static void app_transfer_request_cover_wifi(void) {++pending_setup;}
static void invalidate_covers(void) {}
static void cover_lookup_cleanup(void) {
    s_cover_connection = COVER_CONNECT_IDLE;
    s_cover_filtering = false;
    s_cover_error[0] = 0;
}
static void copy_text(char *out, size_t size, const char *text) {snprintf(out, size, "%s", text);}
'''
for name in ("cover_station_ready","cover_station_compatible","cover_lookup_open","cover_connection_tick",
             "bulk_cover_start","cover_retry_enabled"):
    source += "\n" + function(name, ui)
source += r'''
int main(void) {
    http_response_t response = {0};
    outcomes[0] = (outcome_t){0,ESP_ERR_HTTP_CONNECT,ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT,0};
    outcomes[1] = (outcome_t){200,ESP_OK,0,0};
    assert(fetch("https://weread.qq.com/", &response, false) == ESP_OK);
    assert(calls == 2 && delays == 1 && header_count == 4 && response.size == 25);
    calls = 0;
    outcomes[0] = (outcome_t){0,ESP_ERR_HTTP_CONNECT,0,8};
    assert(fetch("https://weread.qq.com/", &response, false) == ESP_ERR_HTTP_CONNECT);
    assert(calls == 1 && delays == 1);
    calls = 0;
    outcomes[0] = outcomes[1] = (outcome_t){0,ESP_ERR_HTTP_CONNECT,ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT,0};
    assert(fetch("https://weread.qq.com/", &response, false) == ESP_ERR_TIMEOUT && calls == 2);
    calls = 0;
    outcomes[0] = (outcome_t){-1,ESP_ERR_HTTP_FETCH_HEADER,0,0};
    outcomes[1] = (outcome_t){200,ESP_OK,0,0};
    assert(fetch("https://weread.qq.com/", &response, false) == ESP_OK && calls == 2);

    int status;
    bool retryable;
    const char *form = "message=test&page=1&limit=12";
    calls = 0;
    outcomes[0] = (outcome_t){0,ESP_ERR_HTTP_CONNECT,ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT,0};
    assert(fetch_attempt("https://librella.fi/eapi/book/search", &response, COVER_FETCH_ZLIB_SEARCH,
                         form, &status, &retryable) == ESP_ERR_TIMEOUT && calls == 1);
    calls = 0;
    outcomes[0] = (outcome_t){200,ESP_OK,0,0};
    assert(fetch_attempt("https://librella.fi/eapi/book/search", &response, COVER_FETCH_ZLIB_SEARCH,
                         form, &status, &retryable) == ESP_OK && calls == 1 && status == 200);

    calls = 0;
    queued = true;
    assert(fetch("https://s3proxy-alp2-covers.cdn-zlib.sk/covers/test.jpg", &response,
                 COVER_FETCH_ZLIB_PREVIEW) == ESP_ERR_NOT_FINISHED && calls == 0);
    queued = false;
    stalled = true;
    fake_us = 0;
    assert(fetch("https://s3proxy-alp2-covers.cdn-zlib.sk/covers/test.jpg", &response,
                 COVER_FETCH_ZLIB_IMAGE) == ESP_ERR_TIMEOUT && fake_us == 5000000 && calls == 250);
    stalled = false;
    calls = 0;
    outcomes[0] = (outcome_t){200,ESP_OK,0,0};
    assert(fetch("https://images.weserv.nl/?url=cover", &response, COVER_FETCH_ZLIB_IMAGE) == ESP_OK && calls == 1);
    calls = 0;
    truncated = true;
    assert(fetch("https://i0.wp.com/cover", &response, COVER_FETCH_ZLIB_IMAGE) == ESP_ERR_INVALID_RESPONSE);
    truncated = false;

    const char *cover = "https://s3proxy-alp2-covers.cdn-zlib.sk/covers/test.jpg";
    calls = 0;
    outcomes[0] = (outcome_t){0,ESP_ERR_HTTP_CONNECT,ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT,0};
    outcomes[1] = (outcome_t){200,ESP_OK,0,0};
    assert(fetch_zlib_image(cover, &response, false) == ESP_OK && calls == 2);
    assert(response.prefer_zlib_proxy && !response.prefer_weserv && !strcmp(last_request,
        "https://i0.wp.com/s3proxy-alp2-covers.cdn-zlib.sk/covers/test.jpg?ssl=1&w=200&quality=55"));
    calls = 0;
    outcomes[0] = (outcome_t){503,ESP_OK,0,0};
    outcomes[1] = (outcome_t){200,ESP_OK,0,0};
    assert(fetch_zlib_image(cover, &response, true) == ESP_OK && calls == 2);
    assert(response.prefer_weserv && !strcmp(last_request,
        "https://images.weserv.nl/?url=https%3A%2F%2Fs3proxy%2Dalp2%2Dcovers%2Ecdn%2Dzlib%2Esk%2Fcovers%2Ftest%2Ejpg&w=200&h=288&fit=inside&output=jpg&q=55"));
    calls = 0;
    outcomes[0] = (outcome_t){200,ESP_OK,0,0};
    assert(fetch_zlib_image(cover, &response, true) == ESP_OK && calls == 1);
    assert(strstr(last_request, "images.weserv.nl"));
    calls = 0;
    const char *signed_url = "https://s3proxy-alp2-covers.cdn-zlib.sk/covers/test.jpg?secret=not-public";
    assert(fetch_zlib_image(signed_url, &response, false) == ESP_OK && calls == 1);
    assert(!strcmp(last_request, signed_url));

    app_ctx_t ctx = {0};
    wifi = (read_pico_transfer_status_t){true,false,READ_PICO_TRANSFER_MODE_STA,READ_PICO_TRANSFER_READY};
    assert(bulk_cover_start(&ctx) == APP_REDRAW_PAGE && !ctx.request_app && s_view == COVER_LOOKUP && !pending_setup);
    assert(s_cover_connection == (KIIKO_BOARD_METALIO47 ? COVER_CONNECT_START : COVER_CONNECT_IDLE));
    assert(!cover_retry_enabled());
    if (KIIKO_BOARD_METALIO47) {
        assert(cover_connection_tick(&ctx) == APP_REDRAW_NONE && starts == 1 && s_cover_connection == COVER_CONNECT_WAIT);
        wifi.cover_lookup = wifi.network_ready = true;
        assert(cover_connection_tick(&ctx) == APP_REDRAW_PAGE && s_cover_connection == COVER_CONNECT_IDLE);
    }
    s_cover_filtering = false;
    s_cover_count = 2;
    s_cover_status.saved = 1;
    assert(cover_retry_enabled());
    s_cover_status.saved = 2;
    assert(!cover_retry_enabled());

    s_view = BULK;
    wifi.network_ready = false;
    assert(bulk_cover_start(&ctx) == APP_REDRAW_NONE && ctx.request_app == &app_transfer && pending_setup == 1);
    s_cover_connection = COVER_CONNECT_WAIT;
    wifi.state = READ_PICO_TRANSFER_ERROR;
    assert(cover_connection_tick(&ctx) == APP_REDRAW_PAGE && cover_retry_enabled());
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kiiko-cover-retry-") as directory:
    tmp = Path(directory)
    (tmp / "test.c").write_text(source)
    for board in (0, 1):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        f"-DKIIKO_BOARD_METALIO47={board}", str(tmp / "test.c"), "-o", str(tmp / "test")],
                       check=True)
        subprocess.run([str(tmp / "test")], check=True)
print("PASS: HTTPS retry and certificate rejection, online direct entry, offline provisioning and retry eligibility")

#!/usr/bin/env python3
"""回归实际 Wi-Fi 空闲与 HTTP 准入。 / Regress the production Wi-Fi idle and HTTP admission paths."""
from pathlib import Path
import subprocess
import tempfile

from home_cover_cache_host_test import function

source = Path(__file__).resolve().parents[1] / "components/read_pico_transfer/read_pico_transfer.c"
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_FAIL (-1)
typedef int esp_err_t;
typedef struct {void *user_ctx;} httpd_req_t;
typedef struct {esp_err_t (*handler)(httpd_req_t *);void *user_ctx;} httpd_uri_t;
typedef int httpd_handle_t;
typedef struct {bool wifi_enabled,network_ready;int mode;} status_t;
static status_t s_status;
static int64_t now,s_wifi_last_use_us;
static bool s_stopping,s_upload_active,s_time_active,s_config_busy,s_http_active;
static int s_lock,stops;
#define portENTER_CRITICAL(p) assert(++*(p)==1)
#define portEXIT_CRITICAL(p) assert(--*(p)==0)
static int64_t esp_timer_get_time(void){return now;}
static void read_pico_transfer_stop(void){assert(s_stopping&&!s_lock);++stops;s_status.wifi_enabled=false;}
static esp_err_t info_handler(httpd_req_t *req){(void)req;assert(s_http_active&&!s_lock);return ESP_OK;}
void read_pico_transfer_idle_poll(bool in_use);
static esp_err_t upload_handler(httpd_req_t *req){
    (void)req;assert(s_http_active&&!s_lock);
    now+=150000000;
    read_pico_transfer_idle_poll(false);
    assert(s_status.wifi_enabled);
    return ESP_OK;
}
static httpd_uri_t registered;
static esp_err_t httpd_register_uri_handler(httpd_handle_t http,const httpd_uri_t *route){
    assert(http==1);registered=*route;return ESP_OK;
}
'''
unit += next(line for line in source.read_text().splitlines() if line.startswith("#define WIFI_IDLE_US")) + "\n"
for name in ("transfer_http_dispatch", "transfer_register_routes", "read_pico_transfer_idle_poll"):
    unit += function(name, source) + "\n"
unit += r'''
static void enable(int mode){
    s_status=(status_t){.wifi_enabled=true,.mode=mode};
    s_wifi_last_use_us=now;
    s_stopping=s_upload_active=s_time_active=s_config_busy=s_http_active=false;
}
int main(void){
    for(int mode=0;mode<2;++mode)for(int linked=0;linked<2;++linked){
        enable(mode);s_status.network_ready=linked;
        now+=WIFI_IDLE_US-1;read_pico_transfer_idle_poll(false);assert(s_status.wifi_enabled);
        ++now;read_pico_transfer_idle_poll(false);assert(!s_status.wifi_enabled);
    }
    assert(stops==4);
    for(int job=0;job<3;++job){
        enable(1);now+=2*WIFI_IDLE_US;
        s_upload_active=job==0;s_time_active=job==1;read_pico_transfer_idle_poll(job==2);
        assert(s_status.wifi_enabled&&s_wifi_last_use_us==now);
        s_upload_active=s_time_active=false;
        now+=WIFI_IDLE_US-1;read_pico_transfer_idle_poll(false);assert(s_status.wifi_enabled);
        ++now;read_pico_transfer_idle_poll(false);assert(!s_status.wifi_enabled);
    }
    enable(0);
    httpd_uri_t route={.handler=upload_handler};
    assert(transfer_register_routes(1,&route,1)==ESP_OK);
    httpd_req_t request={.user_ctx=registered.user_ctx};
    assert(registered.handler(&request)==ESP_OK&&!s_http_active&&s_wifi_last_use_us==now);
    route.handler=info_handler;
    now+=WIFI_IDLE_US-1;assert(registered.handler(&request)==ESP_OK);
    ++now;read_pico_transfer_idle_poll(false);assert(!s_status.wifi_enabled);
    assert(registered.handler(&request)==ESP_FAIL);
    enable(1);s_config_busy=true;now+=WIFI_IDLE_US;
    read_pico_transfer_idle_poll(false);assert(s_status.wifi_enabled);
    s_config_busy=false;read_pico_transfer_idle_poll(false);assert(!s_status.wifi_enabled);
    puts("PASS: Wi-Fi idle threshold, AP/STA, disconnected/connected, active jobs and HTTP, background status polling, configuration and stop admission");
}
'''
with tempfile.TemporaryDirectory(prefix="wifi-idle-") as temp:
    work = Path(temp)
    test = work / "test.c"
    test.write_text(unit)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(test), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)

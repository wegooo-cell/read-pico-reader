"""Exercise real USB teardown with delayed writes and injected failures.

SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
中文：验证串口回收、独占存储卡、失败重试与启动失败收尾。
English: Verify console recovery, exclusive card ownership, retries and failed-start cleanup.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
typedef int esp_err_t;
static bool book_cover_jobs_quiesce(unsigned ms){assert(ms==2000);return true;}
static bool ui_avatar_quiesce(unsigned ms){assert(ms==2000);return true;}
static void book_lock_collage_invalidate(void){}
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_TIMEOUT 5
typedef unsigned TickType_t;
TickType_t xTaskGetTickCount(void);
void vTaskSuspend(void *task);
bool tud_task_event_ready(void);
void usbd_defer_func(void (*callback)(void *),void *arg,bool isr);
void tud_connect(void);
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(int ms);
const char *esp_err_to_name(int err);
typedef struct {int slot,max_freq_khz;} sdmmc_host_t;
typedef struct {int width,clk,cmd,d0,d1,d2,d3,cd,wp,flags;} sdmmc_slot_config_t;
typedef struct {int marker;} sdmmc_card_t;
int read_pico_sd_usb_acquire(sdmmc_card_t **out);
int read_pico_sd_usb_release(void);
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){.slot=1})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define SDMMC_HOST_SLOT_0 0
#define GPIO_NUM_20 20
#define GPIO_NUM_21 21
#define GPIO_NUM_22 22
#define GPIO_NUM_23 23
#define GPIO_NUM_24 24
#define GPIO_NUM_25 25
void metalio_ana_sw(bool high);
esp_err_t metalio_usb_route_serial(bool serial);
void metalio_usb_serial_startup(void);
void metalio_sd_release_hold(void);
#define GPIO_NUM_NC -1
int sdmmc_host_init(void);
int sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config);
int sdmmc_host_deinit(void);
int sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card);
typedef void *usb_phy_handle_t;
typedef struct {int controller,target;} usb_phy_config_t;
#define USB_PHY_CTRL_SERIAL_JTAG 1
#define USB_PHY_TARGET_INT 0
int usb_new_phy(const usb_phy_config_t *config,usb_phy_handle_t *handle);
int usb_del_phy(usb_phy_handle_t handle);
typedef struct {bool mounted;} read_pico_sd_info_t;
int read_pico_sd_get_info(read_pico_sd_info_t *info);
int read_pico_sd_sync(void);
int read_pico_sd_remount(void);
bool ttf_font_is_builtin(void);
int ttf_font_open_builtin(void);
typedef void *tinyusb_msc_storage_handle_t;
typedef struct {struct {int auto_mount_off;} user_flags;} tinyusb_msc_driver_config_t;
typedef struct {struct {sdmmc_card_t *card;} medium;int mount_point;
struct {bool do_not_format;struct {int max_files;} config;} fat_fs;} tinyusb_msc_storage_config_t;
#define TINYUSB_MSC_STORAGE_MOUNT_USB 1
typedef struct {int port;} tinyusb_config_t;
#define TINYUSB_DEFAULT_CONFIG() ((tinyusb_config_t){0})
int tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config);
int tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config,tinyusb_msc_storage_handle_t *handle);
int tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle);
int tinyusb_msc_uninstall_driver(void);
int tinyusb_driver_install(const tinyusb_config_t *config);
int tinyusb_driver_uninstall(void);
bool tud_mounted(void);
void tud_disconnect(void);
'''
UNIT = r'''
#include "usb_stub.h"
#include "usb_storage.c"
#include <stdio.h>
static bool mounted=true,host,driver,msc,connected,serial_route;
static sdmmc_card_t *borrowed;
static int stage,delete_error,usb_error,msc_error,busy,deletes,remounts,phy_restores,route_error;
static int raw_release_error;
static int failed(int where){return stage==where?ESP_FAIL:ESP_OK;}
static unsigned ticks,parked,events;
static bool drain_stalled;
static void (*deferred)(void *);
TickType_t xTaskGetTickCount(void){return ticks;}
void vTaskSuspend(void *task){assert(!task);++parked;}
bool tud_task_event_ready(void){if(events){--events;return true;}return false;}
void usbd_defer_func(void (*fn)(void *),void *arg,bool isr){assert(!arg&&!isr&&!deferred);deferred=fn;}
void vTaskDelay(int ms){assert(ms>0);ticks+=ms;if(deferred&&!drain_stalled){void (*fn)(void *)=deferred;deferred=NULL;fn(NULL);}}
void tud_connect(void){assert(driver);connected=true;}

const char *esp_err_to_name(int e){(void)e;return "test";}
int read_pico_sd_get_info(read_pico_sd_info_t *i){i->mounted=mounted;return ESP_OK;}
int read_pico_sd_sync(void){assert(mounted);mounted=false;return ESP_OK;}
int read_pico_sd_remount(void){assert(!driver&&!borrowed&&!msc&&!host);mounted=true;++remounts;return ESP_OK;}
bool ttf_font_is_builtin(void){return true;}
int ttf_font_open_builtin(void){return ESP_OK;}
int sdmmc_host_init(void){assert(!mounted&&!host);if(failed(1))return ESP_FAIL;host=true;return ESP_OK;}
int sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *c){assert(slot==0&&c->width==4&&c->clk==24&&c->cmd==25&&c->d0==20&&c->d1==21&&c->d2==22&&c->d3==23);return failed(2);}
void metalio_sd_release_hold(void){}
void metalio_ana_sw(bool high){if(high&&!serial_route)++phy_restores;serial_route=high;}
esp_err_t metalio_usb_route_serial(bool high){if(route_error)return route_error;metalio_ana_sw(high);return ESP_OK;}
void metalio_usb_serial_startup(void){assert(metalio_usb_route_serial(true)==ESP_OK);}
int sdmmc_card_init(const sdmmc_host_t *h,sdmmc_card_t *c){assert(h->max_freq_khz==20000);c->marker=47;return failed(3);}
int sdmmc_host_deinit(void){assert(!borrowed&&!driver&&!msc);host=false;return ESP_OK;}
int read_pico_sd_usb_acquire(sdmmc_card_t **out){
    *out=NULL;int err=read_pico_sd_sync();if(err)return err;
    sdmmc_host_t h=SDMMC_HOST_DEFAULT();h.max_freq_khz=20000;
    sdmmc_slot_config_t c=SDMMC_SLOT_CONFIG_DEFAULT();
    c.width=4;c.clk=24;c.cmd=25;c.d0=20;c.d1=21;c.d2=22;c.d3=23;
    err=sdmmc_host_init();if(err)return err;
    err=sdmmc_host_init_slot(0,&c);if(err)return err;
    s_card=calloc(1,sizeof(*s_card));assert(s_card);
    err=sdmmc_card_init(&h,s_card);if(err)return err;*out=s_card;return ESP_OK;
}
int read_pico_sd_usb_release(void){
    if(raw_release_error)return raw_release_error;
    if(host)sdmmc_host_deinit();free(s_card);s_card=NULL;return ESP_OK;
}
int usb_new_phy(const usb_phy_config_t *c,usb_phy_handle_t *h){assert(c->controller==USB_PHY_CTRL_SERIAL_JTAG);*h=malloc(1);assert(*h);++phy_restores;return ESP_OK;}
int usb_del_phy(usb_phy_handle_t h){free(h);return ESP_OK;}
int tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *c){assert(c->user_flags.auto_mount_off&&!mounted);if(failed(4))return ESP_FAIL;msc=true;return ESP_OK;}
int tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *c,tinyusb_msc_storage_handle_t *h){
    assert(msc&&c->medium.card&&c->fat_fs.do_not_format);
    if(failed(5))return ESP_FAIL;borrowed=c->medium.card;*h=malloc(1);assert(*h);return ESP_OK;
}
int tinyusb_driver_install(const tinyusb_config_t *c){(void)c;assert(borrowed&&!mounted&&!serial_route);if(failed(6))return ESP_FAIL;driver=connected=true;return ESP_OK;}
int tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t h){
    assert(borrowed&&borrowed->marker==47&&!mounted&&!driver);++deletes;
    if(busy>0){--busy;return ESP_ERR_INVALID_STATE;}if(delete_error)return delete_error;
    free(h);borrowed=NULL;return ESP_OK;
}
int tinyusb_driver_uninstall(void){assert(borrowed&&!connected&&atomic_load(&s_usb_stop_state)==USB_PARKED);if(usb_error)return usb_error;driver=false;return ESP_OK;}
int tinyusb_msc_uninstall_driver(void){assert(!borrowed&&!driver);if(msc_error)return msc_error;msc=false;return ESP_OK;}
bool tud_mounted(void){return connected;}
void tud_disconnect(void){assert(driver);connected=false;}
static void stopped(void){assert(mounted&&!usb_storage_active()&&!usb_storage_connected());assert(!s_cleanup_pending&&!s_card&&!s_storage&&!host&&!driver&&!msc&&serial_route);}
static void retained(void){assert(!mounted&&usb_storage_active()&&!usb_storage_connected()&&s_cleanup_pending&&s_card&&host&&serial_route);assert(usb_storage_start()==ESP_ERR_INVALID_STATE);}
int main(void){
    usb_storage_phy_init();assert(serial_route);usb_storage_phy_init();assert(serial_route);
    assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);drain_stalled=true;int before_timeout=remounts;
    assert(usb_storage_stop()==ESP_ERR_TIMEOUT&&driver&&borrowed&&connected&&!mounted&&remounts==before_timeout);
    assert(!s_cleanup_pending&&atomic_load(&s_usb_stop_state)==USB_RUNNING);
    assert(deferred);void(*stale)(void*)=deferred;deferred=NULL;stale(NULL);assert(!parked);
    drain_stalled=false;events=7;assert(usb_storage_stop()==ESP_OK&&parked==1);stopped();
    assert(usb_storage_start()==ESP_OK);drain_stalled=true;
    assert(usb_storage_stop()==ESP_ERR_TIMEOUT&&deferred&&s_usb_park_queued);
    drain_stalled=false;events=9;assert(usb_storage_stop()==ESP_OK&&parked==2);stopped();
    for(int i=0;i<3;++i){assert(usb_storage_start()==ESP_OK&&usb_storage_connected());busy=3;assert(usb_storage_stop()==ESP_OK);stopped();}
    assert(usb_storage_start()==ESP_OK);delete_error=ESP_FAIL;int before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(borrowed&&remounts==before);
    delete_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);busy=25;int attempts=deletes;before=remounts;
    assert(usb_storage_stop()==ESP_ERR_INVALID_STATE&&deletes-attempts==20);retained();assert(remounts==before);
    assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);usb_error=ESP_FAIL;before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(borrowed&&driver&&remounts==before);
    usb_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);msc_error=ESP_FAIL;before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(!borrowed&&!driver&&msc&&remounts==before);
    msc_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    route_error=ESP_FAIL;assert(usb_storage_start()==ESP_FAIL);assert(s_active&&s_cleanup_pending&&!driver&&!mounted);
    route_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);route_error=ESP_FAIL;before=remounts;
    assert(usb_storage_stop()==ESP_FAIL&&s_active&&s_cleanup_pending&&remounts==before&&!mounted);
    route_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);raw_release_error=ESP_FAIL;before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(remounts==before&&!borrowed);
    raw_release_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    for(stage=1;stage<=6;++stage){assert(usb_storage_start()==ESP_FAIL);stopped();}stage=0;
    stage=6;delete_error=ESP_FAIL;before=remounts;assert(usb_storage_start()==ESP_FAIL);retained();assert(remounts==before);
    stage=delete_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    release_serial_phy();assert(!serial_route);
    puts("USB: console recovery, delayed writes, queued-event drain, timeout/reconnect/stale callback, 3 stop failures, 6 start failures, exclusive remount and retry passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p/'usb_stub.h').write_text(STUB)
    for name in ('ui_avatar.h','book_cover_jobs.h','book_lock_collage.h','metalio_hw.h','esp_err.h','driver/sdmmc_host.h','esp_log.h','esp_private/usb_phy.h',
                 'freertos/FreeRTOS.h','freertos/task.h','read_pico_sd.h','read_pico_sd_msc.h','sdmmc_cmd.h',
                 'device/usbd_pvt.h','tinyusb.h','tinyusb_default_config.h','tinyusb_msc.h','ttf_font.h'):
        header=p/name;header.parent.mkdir(parents=True,exist_ok=True);header.write_text('#include "usb_stub.h"\n')
    (p/'test.c').write_text(UNIT)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer',f'-I{p}',f'-I{ROOT / "main/apps"}',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

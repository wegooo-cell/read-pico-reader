#pragma once

/* 显式启用的本地 USB 测时入口；普通构建不含命令读取。/ Explicit local USB bench; no command reader in product builds. */
#ifdef KIIKO_LOCAL_LATENCY
#include <fcntl.h>
#include "hal/usb_serial_jtag_ll.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <sys/stat.h>
#include "book/book_progress.h"
#if KIIKO_BOARD_METALIO47
void epd_pie_pixels_enable(bool enabled);
bool epd_pie_pixels_enabled(void);
void epd_pie_pixels_bench(void);
void epd_pipeline_enable(bool enabled);
void epd_pipeline_test(void);
void epd_lcd_cache_probe_arm(int phases);
int epd_lcd_cache_probe_completed(void);
#endif
extern const app_desc_t app_book;
static int64_t local_probe_contact_start_us;
static void click_present(app_ctx_t *ctx,const app_desc_t *current,EpdRect area);
static void local_probe_quick(app_ctx_t *ctx,const app_desc_t *current,bool open);

static void local_latency_init(void) {
    int flags=fcntl(STDIN_FILENO,F_GETFL,0);
    if(flags>=0)fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK);
    ESP_LOGI("latency","LOCAL_USB_PROBE ready version=1");
}

static app_redraw_t local_latency_poll(app_ctx_t *ctx,const app_desc_t *current) {
    static char line[80];static unsigned used;char c;
    while(usb_serial_jtag_ll_read_rxfifo((uint8_t*)&c,1)==1) {
        if(c=='\r')continue;
        if(c!='\n') { if(used<sizeof(line)-1)line[used++]=c;else used=0;continue; }
        line[used]=0;used=0;
        int a=-1,b=-1;
        ctx->user_activity=true;ctx->consumed=false;ctx->now_ms=esp_timer_get_time()/1000;
        if(!strcmp(line,"perf info")) {
            ESP_LOGI("latency","INFO page=%d mode=%d auto_lock=%d widget=%d heap=%u internal=%u largest=%u stack=%u",
                app_index_of(current),app_settings_main_refresh_mode(),app_settings_auto_lock_minutes(),app_settings_home_component(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(NULL));
            ESP_LOGI("latency","READER fx=%d lg=%d px=%d",app_settings_reader_turn_effect(),
                app_settings_low_ghost(),app_settings_book_px());
#if KIIKO_BOARD_METALIO47
            ESP_LOGI("latency","CONTEXT recovery=%d radio=%d font=%s",pico_boot_recovery(),read_pico_transfer_radio_busy(),ttf_font_path());
#endif
        }
#if KIIKO_BOARD_METALIO47
        else if(!strcmp(line,"perf pie test"))epd_pie_pixels_bench();
        else if(!strcmp(line,"perf pipeline test"))epd_pipeline_test();
        else if(!strcmp(line,"perf restart")){ESP_LOGI("latency","INTENTIONAL_RESTART");esp_restart();}
        else if(sscanf(line,"perf cache %d",&a)==1&&a>=0&&a<=48) {
            epd_lcd_cache_probe_arm(a);ESP_LOGI("latency","CACHE_ARM phases=%d",a);
        }
        else if(!strcmp(line,"perf cacheinfo"))ESP_LOGI("latency","CACHE_COMPLETED phases=%d",epd_lcd_cache_probe_completed());
        else if(sscanf(line,"perf pipeline %d",&a)==1&&(a==0||a==1)) {
            epd_pipeline_enable(a!=0);epd_pie_pixels_enable(a!=0);
            ESP_LOGI("latency","PIPELINE_ENABLED value=%d",a);
        }
        else if(sscanf(line,"perf pie %d",&a)==1&&(a==0||a==1)) {
            epd_pie_pixels_enable(a!=0);ESP_LOGI("latency","PIE_ENABLED value=%d",epd_pie_pixels_enabled());
        }
#endif
        else if(sscanf(line,"perf nav %d",&a)==1&&a>=0&&a<4&&app_index_of(current)>=0&&app_index_of(current)<4) {
            // 重放同一底栏请求/按压反馈路径，计时包含反馈扫描，不冒充触摸硬件或光学测量。
            // Replay root request/press feedback, including its scan, without claiming physical touch or optical measurement.
            local_probe_contact_start_us=esp_timer_get_time();
            ui_nav_request(ctx,a);ui_click_feedback_nav_request(a);
            EpdRect area;if(ui_click_feedback_nav_tick(ctx->fb,esp_timer_get_time()/1000,&area))click_present(ctx,current,area);
            ESP_LOGI("latency","NAV_REQUEST page=%d",a);
        } else if(sscanf(line,"perf page %d",&a)==1&&a>=0&&a<4) {
            ui_nav_request(ctx,a);ESP_LOGI("latency","REQUEST page=%d",a);
        } else if(sscanf(line,"perf mode %d",&a)==1&&a>=0&&a<=2) {
            app_settings_set_main_refresh_mode(a);ESP_LOGI("latency","MODE %d",a);
        } else if(sscanf(line,"perf tap %d %d",&a,&b)==2&&a>=0&&a<684&&b>=0&&b<1216&&current->on_gesture) {
            ui_gesture_event_t ev={.type=UI_GESTURE_TAP,.x0=a,.y0=b,.x=a,.y=b,.hold_ms=50};
            int64_t t=esp_timer_get_time();
            app_redraw_t result=current->on_gesture(ctx,&ev);
            ESP_LOGI("latency","HANDLER tap=%d,%d redraw=%d us=%lld",a,b,result,(long long)(esp_timer_get_time()-t));
            return result;
        } else if(sscanf(line,"perf quick %d",&a)==1&&(a==0||a==1)) {
            local_probe_quick(ctx,current,a!=0);
        } else if(sscanf(line,"perf widget %d",&a)==1&&a>=0&&a<APP_HOME_COMPONENT_COUNT&&a!=3) {
            esp_err_t error=app_settings_set_home_component((app_home_component_t)a);
            ESP_LOGI("latency","WIDGET value=%d error=%d",a,error);
            return error==ESP_OK?APP_REDRAW_PAGE:APP_REDRAW_NONE;
        } else if(!strcmp(line,"perf back")&&current->on_key) {
            return current->on_key(ctx,UI_KEY_1);
        } else if(sscanf(line,"perf key %d",&a)==1&&a>=1&&a<=3&&current->on_key) {
            // 重放三键短按，阅读页按用户的按键设置翻页；用于无人值守采集翻页计时。
            // Replay a short key press; the reader maps it through the user's key settings. For unattended turn timing.
            ESP_LOGI("latency","KEY %d",a);
            return current->on_key(ctx,a-1);
        } else if(sscanf(line,"perf turnfx %d",&a)==1&&a>=0&&a<=3) {
            app_settings_set_reader_turn_effect((uint8_t)a);
            ESP_LOGI("latency","TURNFX %d",app_settings_reader_turn_effect());
        } else if(sscanf(line,"perf lowghost %d",&a)==1&&(a==0||a==1)) {
            app_settings_set_low_ghost(a!=0);
            ESP_LOGI("latency","LOWGHOST %d",app_settings_low_ghost());
        } else if(sscanf(line,"perf open %d %n",&a,&b)==1&&b>0&&a>=0&&a<1000) {
            // 只允许基准书：写入目标章首的进度后按路径打开，测试从固定位置开始，不碰用户书籍。
            // Bench book only: seed progress at the chapter start, then open by path from a fixed spot.
            const char *path=line+b;struct stat st;
            book_progress_t p={0};
            esp_err_t err=!strstr(path,"kiiko-bench")?ESP_ERR_INVALID_ARG:stat(path,&st)?ESP_ERR_NOT_FOUND:ESP_OK;
            if(err==ESP_OK) {
                p.file_size=(uint32_t)st.st_size;p.chapter=(uint16_t)a;p.px=app_settings_book_px();
                err=book_progress_save(path,&p);
            }
            if(err==ESP_OK&&!app_book_request_open(path))err=ESP_FAIL;
            if(err==ESP_OK)ctx->request_app=&app_book;
            ESP_LOGI("latency","OPEN chapter=%d error=%s size=%u path=%s",a,esp_err_to_name(err),
                (unsigned)p.file_size,path);
        } else if(!strcmp(line,"perf repaint"))return APP_REDRAW_PAGE;
        else ESP_LOGI("latency","UNKNOWN");
    }
    return APP_REDRAW_NONE;
}
#endif

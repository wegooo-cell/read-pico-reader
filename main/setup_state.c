/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：只写本板设置命名空间；未配置和显式待完成可进入引导，存储错误不冒充新机。
 * English: Write only this board's settings namespace; fresh/pending profiles need setup, errors are never treated as a fresh device.
 */
#include "setup_state.h"
#include "kiiko_board_config.h"
#include "nvs.h"
#ifdef KIIKO_UI_LOCALIZATION
#include "ui_locale.h"
#endif
static bool s_pending, s_english, s_library_pending;
#define SETUP_STATE_KEY "setup_v1"
#define SETUP_LANGUAGE_KEY "ui_lang"
#define SETUP_LIBRARY_KEY "shelf_init"
static esp_err_t save(uint8_t state,bool english,bool fresh) {
    nvs_handle_t h;esp_err_t err=nvs_open(KIIKO_BOARD_SETTINGS_NAMESPACE,NVS_READWRITE,&h);
    if(err!=ESP_OK)return err;
    err=nvs_set_u8(h,SETUP_STATE_KEY,state);
    if(err==ESP_OK)err=nvs_set_u8(h,SETUP_LANGUAGE_KEY,english?1:0);
    if(err==ESP_OK&&fresh)err=nvs_set_u8(h,SETUP_LIBRARY_KEY,1);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);return err;
}
void app_setup_init(void) {
    s_pending=s_english=s_library_pending=false;
#ifdef KIIKO_UI_LOCALIZATION
    ui_locale_set_english(false);
#endif
    nvs_handle_t h;esp_err_t err=nvs_open(KIIKO_BOARD_SETTINGS_NAMESPACE,NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND){
        // 先持久化“待完成”，以免引导中的其他设置让下次启动被误判为老用户。
        // Persist Pending before wizard changes could make a restart resemble a legacy profile.
        if(save(0,false,true)==ESP_OK)s_pending=s_library_pending=true;
        return;
    }
    if(err!=ESP_OK)return;
    uint8_t state=255,language=0,library=0;
    if(nvs_get_u8(h,SETUP_LIBRARY_KEY,&library)==ESP_OK&&library==1)s_library_pending=true;
    err=nvs_get_u8(h,SETUP_STATE_KEY,&state);
    if(nvs_get_u8(h,SETUP_LANGUAGE_KEY,&language)==ESP_OK&&language<=1)s_english=language==1;
    size_t entries=1;
    esp_err_t count_err=err==ESP_ERR_NVS_NOT_FOUND?nvs_get_used_entry_count(h,&entries):ESP_OK;
    nvs_close(h);
#ifdef KIIKO_UI_LOCALIZATION
    ui_locale_set_english(s_english);
#endif
    // 已存在的旧命名空间没有标记就直接沿用旧配置；不自动擦除、不清密码。
    // Existing legacy namespaces without a marker keep their profile; never erase or reset credentials.
    if(err==ESP_ERR_NVS_NOT_FOUND&&count_err==ESP_OK&&entries==0){
        if(save(0,false,true)==ESP_OK)s_pending=s_library_pending=true;
    }else if(err==ESP_OK&&state<=1)s_pending=state==0;
}
bool app_setup_pending(void){return s_pending;}
bool app_setup_english(void){return s_english;}
esp_err_t app_setup_set_english(bool english){
    esp_err_t err=save(s_pending?0:1,english,false);
    if(err==ESP_OK){
        s_english=english;
#ifdef KIIKO_UI_LOCALIZATION
        ui_locale_set_english(english);
#endif
    }
    return err;
}
esp_err_t app_setup_finish(void){
    esp_err_t err=save(1,s_english,false);if(err==ESP_OK)s_pending=false;return err;
}

bool app_setup_library_pending(void){return s_library_pending;}
esp_err_t app_setup_library_complete(void){
    if(!s_library_pending)return ESP_OK;
    nvs_handle_t h;esp_err_t err=nvs_open(KIIKO_BOARD_SETTINGS_NAMESPACE,NVS_READWRITE,&h);
    if(err!=ESP_OK)return err;
    err=nvs_set_u8(h,SETUP_LIBRARY_KEY,0);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);if(err==ESP_OK)s_library_pending=false;return err;
}

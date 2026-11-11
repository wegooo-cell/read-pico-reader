// 中文：书架进度角标默认开启；新开关与排序只在NVS提交成功后更新内存。
// English: Cover progress defaults on; update live preferences only after a successful NVS commit.
static bool s_shelf_badges=true;
static bool shelf_preference_put(const char*key,uint8_t value){
    nvs_handle_t h;if(nvs_open(NVS_NS,NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t err=nvs_set_u8(h,key,value);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);return err==ESP_OK;
}
bool app_settings_shelf_progress_badges(void){return s_shelf_badges;}
bool app_settings_set_shelf_progress_badges(bool on){
    if(on==s_shelf_badges)return true;
    if(!shelf_preference_put("shelf_badges",on))return false;
    s_shelf_badges=on;return true;
}
// 中文：备份v25的小型校验块保留完整八种排序和角标开关，旧版本采用原排序及开启角标。
// English: A checked v25 block preserves all eight sort orders and badges; legacy files retain their old order and enable badges.
typedef struct{uint8_t order,badges,checksum[4];}settings_backup_shelf_t;
static uint32_t backup_shelf_checksum(const settings_backup_shelf_t*c){
    return ((2166136261u^c->order)*16777619u^c->badges)*16777619u;
}
static settings_backup_shelf_t backup_shelf_make(unsigned order,bool badges){
    settings_backup_shelf_t c={.order=order,.badges=badges};uint32_t h=backup_shelf_checksum(&c);
    for(unsigned i=0;i<4;++i)c.checksum[i]=(uint8_t)(h>>(8*i));
    return c;
}
static bool backup_shelf_valid(const settings_backup_shelf_t*c){
    uint32_t h=0;for(unsigned i=0;i<4;++i)h|=(uint32_t)c->checksum[i]<<(8*i);
    return c->order<8&&c->badges<=1&&h==backup_shelf_checksum(c);
}

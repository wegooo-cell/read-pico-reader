// 阅读线格式独立保存成一个字节，手势开关不覆盖已保存样式、范围和位置。
// Save one packed guide preset; gesture toggles retain its style,span and offset.
#define NVS_KEY_BOOK_RULE_SAVED "bk_rule_saved"
static uint8_t s_book_rule_saved;
static bool book_rule_saved_valid(unsigned value) {
    return !value || ((value & 3) >= 1 && (value & 3) <= 2 && (value >> 3) <= 8);
}
static uint8_t book_rule_format(void) {
    return (s_book_reading_line ? s_book_reading_line : 1) | (s_book_rule_full ? 4 : 0) | (s_book_rule_offset << 3);
}
static void book_rule_saved_load(nvs_handle_t h) {
    uint8_t value=0;
    s_book_rule_saved=nvs_get_u8(h,NVS_KEY_BOOK_RULE_SAVED,&value)==ESP_OK && value && book_rule_saved_valid(value)
        ? value : book_rule_format();
}
bool app_settings_save_book_reading_line(void) {
    if (!s_book_reading_line) return false;
    uint8_t value=book_rule_format();
    if (!shelf_preference_put(NVS_KEY_BOOK_RULE_SAVED,value)) return false;
    s_book_rule_saved=value;return true;
}
bool app_settings_toggle_book_reading_line(void) {
    const uint8_t saved=s_book_rule_saved ? s_book_rule_saved : book_rule_format();
    const uint8_t style=s_book_reading_line ? 0 : saved & 3;
    const bool full=s_book_reading_line ? s_book_rule_full : (saved & 4)!=0;
    const uint8_t offset=s_book_reading_line ? s_book_rule_offset : saved >> 3;
    nvs_handle_t h;if(nvs_open(NVS_NS,NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t err=nvs_set_u8(h,NVS_KEY_BOOK_RULE,style);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_BOOK_RULE_FULL,full);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_BOOK_RULE_OFFSET,offset);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_BOOK_RULE_SAVED,saved);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    if(err!=ESP_OK)return false;
    s_book_reading_line=style;s_book_rule_full=full;s_book_rule_offset=offset;s_book_rule_saved=saved;
    return true;
}

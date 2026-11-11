// 中文：与各板现有命名空间共用NVS，旧配置缺少此键时使用默认值，不覆盖原设置。
// English: Use each board's existing NVS namespace; missing keys use defaults without overwriting old settings.
static reader_controls_config_t s_reader_controls;
static bool s_reader_controls_loaded;
static void reader_controls_normalize(reader_controls_config_t *c) {
    reader_controls_config_t d=reader_controls_defaults(KIIKO_BOARD_METALIO47);
    for(unsigned i=0;i<RC_SLOT_COUNT;++i)
        if(!reader_controls_action_allowed(c->actions[i],KIIKO_BOARD_METALIO47))c->actions[i]=d.actions[i];
#if !KIIKO_BOARD_METALIO47
    for(unsigned i=0;i<2;++i)if(c->sides[i]==RC_SIDE_BRIGHTNESS||c->sides[i]==RC_SIDE_WARMTH)c->sides[i]=RC_SIDE_OFF;
#endif
    // 中文：兼容旧版0秒及较长间隔，保留通过校验的手势绑定。
    // English: Migrate legacy zero and long intervals while retaining validated gesture bindings.
    const unsigned seconds = reader_controls_seconds(c);
    if (seconds < READER_AUTO_MIN_SECONDS || seconds > READER_AUTO_MAX_SECONDS) {
        c->seconds_le[0] = seconds < READER_AUTO_MIN_SECONDS
            ? READER_AUTO_MIN_SECONDS : READER_AUTO_MAX_SECONDS;
        c->seconds_le[1] = 0;
    }
    reader_controls_seal(c);
}
const reader_controls_config_t *app_settings_reader_controls(void) {
    if(!s_reader_controls_loaded){
        s_reader_controls=reader_controls_defaults(KIIKO_BOARD_METALIO47);
        nvs_handle_t h;
        if(nvs_open(NVS_NS,NVS_READONLY,&h)==ESP_OK){
            reader_controls_config_t saved;size_t bytes=sizeof(saved);
            if(nvs_get_blob(h,"rd_controls",&saved,&bytes)==ESP_OK&&bytes==sizeof(saved)&&reader_controls_valid(&saved))s_reader_controls=saved;
            nvs_close(h);
        }
        reader_controls_normalize(&s_reader_controls);s_reader_controls_loaded=true;
    }
    return &s_reader_controls;
}
static bool reader_controls_save(reader_controls_config_t *c) {
    reader_controls_seal(c);if(!reader_controls_valid(c))return false;
    if(!memcmp(app_settings_reader_controls(),c,sizeof(*c)))return true;
    nvs_handle_t h;if(nvs_open(NVS_NS,NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t e=nvs_set_blob(h,"rd_controls",c,sizeof(*c));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);
    if(e==ESP_OK)s_reader_controls=*c;
    return e==ESP_OK;
}
bool app_settings_set_reader_control(unsigned slot,unsigned action) {
    if(slot>=RC_SLOT_COUNT||!reader_controls_action_allowed(action,KIIKO_BOARD_METALIO47))return false;
    reader_controls_config_t c=*app_settings_reader_controls();c.actions[slot]=(uint8_t)action;return reader_controls_save(&c);
}
bool app_settings_set_reader_side(unsigned side,unsigned action) {
    if(side>=2||action>=RC_SIDE_COUNT)return false;
#if !KIIKO_BOARD_METALIO47
    if(action==RC_SIDE_BRIGHTNESS||action==RC_SIDE_WARMTH)return false;
#endif
    reader_controls_config_t c=*app_settings_reader_controls();c.sides[side]=(uint8_t)action;return reader_controls_save(&c);
}
bool app_settings_set_reader_auto_seconds(unsigned seconds) {
    if (seconds < READER_AUTO_MIN_SECONDS || seconds > READER_AUTO_MAX_SECONDS) return false;
    reader_controls_config_t c=*app_settings_reader_controls();c.seconds_le[0]=seconds;c.seconds_le[1]=seconds>>8;return reader_controls_save(&c);
}

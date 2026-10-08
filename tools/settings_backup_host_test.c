/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the firmware backup format and validation against a temporary card. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef __APPLE__
/* Some host libc versions predate strlcpy, which ESP-IDF provides. */
size_t strlcpy(char *dst, const char *src, size_t cap) {
    size_t length = strlen(src);
    if (cap) {
        size_t copied = length < cap - 1 ? length : cap - 1;
        memcpy(dst, src, copied);
        dst[copied] = 0;
    }
    return length;
}
#endif

#define APP_SETTINGS_BACKUP_ROOT "build/book-tests/settings-card"
#include "../main/settings.c"

static read_pico_transfer_wifi_backup_t saved_wifi = {
    .configured = 1, .ssid = "Home_2.4G", .password = "password123"
};
static unsigned wifi_imports;
esp_err_t read_pico_transfer_export_wifi_backup(read_pico_transfer_wifi_backup_t *out) {
    *out = saved_wifi;
    return ESP_OK;
}
bool read_pico_transfer_wifi_backup_valid(const read_pico_transfer_wifi_backup_t *backup) {
    if (!backup || backup->configured > 1) return false;
    if (!backup->configured) return !backup->ssid[0] && !backup->password[0];
    return backup->ssid[0] &&
           strnlen(backup->ssid, sizeof(backup->ssid)) < sizeof(backup->ssid) &&
           strnlen(backup->password, sizeof(backup->password)) < sizeof(backup->password);
}
esp_err_t read_pico_transfer_import_wifi_backup(const read_pico_transfer_wifi_backup_t *backup) {
    if (!read_pico_transfer_wifi_backup_valid(backup)) return ESP_ERR_INVALID_ARG;
    saved_wifi = *backup;
    ++wifi_imports;
    return ESP_OK;
}

static unsigned history_saves, history_restores;
esp_err_t book_history_backup_write(FILE *file) {
    ++history_saves;
    return fwrite("RPHIST1", 1, 8, file) == 8 ? ESP_OK : ESP_FAIL;
}
bool book_history_backup_validate(FILE *file) {
    char marker[8];
    return fread(marker, 1, 8, file) == 8 && !memcmp(marker, "RPHIST1", 8) && fgetc(file) == EOF;
}
esp_err_t book_history_backup_restore(FILE *file) {
    ++history_restores;
    return book_history_backup_validate(file) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static bool card_mounted = true;
static bool commit_fails;
static int commit_count;
static uint8_t test_loaded_system_size;
static uint8_t test_loaded_fast;
static uint8_t test_loaded_shelf;
static bool test_turn_present;
static uint8_t test_loaded_turn;
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info) { info->mounted = card_mounted; return ESP_OK; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) { (void)ns; (void)mode; *h = 1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value) { (void)h; if (!strcmp(key, NVS_KEY_READER_TURN) && test_turn_present) { *value = test_loaded_turn; return ESP_OK; } if (!strcmp(key, NVS_KEY_SHELF_V22)) { *value = 1; return ESP_OK; } if (!strcmp(key, NVS_KEY_SHELF_STYLE) && test_loaded_shelf) { *value = test_loaded_shelf; return ESP_OK; } if (!strcmp(key, "ui_fast")) { *value = test_loaded_fast; return ESP_OK; } if (!strcmp(key, NVS_KEY_SYS_SIZE) && test_loaded_system_size) { *value = test_loaded_system_size; return ESP_OK; } return ESP_FAIL; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) { (void)h; if (!strcmp(key, NVS_KEY_READER_TURN)) { test_loaded_turn=value; test_turn_present=true; } if (!strcmp(key, NVS_KEY_SHELF_STYLE)) test_loaded_shelf=value; if (!strcmp(key, "ui_fast")) test_loaded_fast=value; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *value, size_t *size) { (void)h; (void)key; (void)value; (void)size; return ESP_FAIL; }
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value) { (void)h; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) { (void)h; (void)key; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; ++commit_count; return commit_fails ? ESP_FAIL : ESP_OK; }

int main(void) {
    assert(app_settings_system_contrast() == 100);
    assert(app_settings_book_line_spacing() == 130); /* New installations start at the middle slider stop. */

    test_loaded_fast = 1;
    app_settings_init();
    (void)mkdir(APP_SETTINGS_BACKUP_ROOT, 0700);
    (void)remove(BACKUP_FILE);
    (void)remove(BACKUP_PREVIOUS);
    (void)remove(BACKUP_TEMP);
    card_mounted = false;
    assert(app_settings_backup_save() == ESP_ERR_INVALID_STATE);
    card_mounted = true;
    s_system_size = 200;
    s_book_px = 62;
    s_book_tracking = 4;
    s_book_indent = 3;
    s_book_rule_offset = 7; /* +6 px */
    s_reader_full_pages = 5;
    s_reader_turn_effect = 1;
    s_reader_power_turn = true;
    s_reader_immersive = true;
    s_shelf_style = 5;
    s_staged_shutdown = true;
    s_auto_lock_minutes = 5;
    s_home_full_refresh = true;
    s_ble_turner = true;
    s_reader_hold_refresh = true;
    app_settings_set_reader_vertical_turn(true);
    assert(app_settings_reader_vertical_turn());

    strlcpy(s_device_name, "Kiiko Pico", sizeof(s_device_name));
    strlcpy(s_status_signature, "今天也要读书", sizeof(s_status_signature));
    strlcpy(s_avatar, "/sdcard/pictures/missing-avatar.jpg", sizeof(s_avatar));
    strlcpy(s_font, "/sdcard/fonts/missing.ttf", sizeof(s_font));
    s_lock_style = 1;
    strlcpy(s_wallpaper, "/sdcard/pictures/missing.jpg", sizeof(s_wallpaper));
    assert(app_settings_backup_save() == ESP_OK);
    assert(history_saves == 1);
    FILE *saved = fopen(BACKUP_FILE, "rb");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7, SEEK_SET) == 0);
    settings_backup_profile_t saved_profile;
    assert(fread(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    settings_backup_wifi_t saved_network;
    assert(fread(&saved_network, 1, sizeof(saved_network), saved) == sizeof(saved_network));
    assert(fclose(saved) == 0);
    assert(!strcmp(saved_profile.device_name, "Kiiko Pico") &&
           !strcmp(saved_profile.status_signature, "今天也要读书") &&
           !strcmp(saved_profile.avatar, "/sdcard/pictures/missing-avatar.jpg"));
    assert(!(saved_profile.home_full_refresh & 64));
    assert(test_loaded_fast == 1);
    assert(backup_wifi_valid(&saved_network) && saved_network.credentials.configured &&
           !strcmp(saved_network.credentials.ssid, "Home_2.4G") &&
           !strcmp(saved_network.credentials.password, "password123"));
    // 旧备份仍可恢复，但退出的极速测试位被忽略；其他配置与网络继续恢复。
    // Accept old backups while ignoring the retired fast-test bit; restore other settings and WiFi.
    saved = fopen(BACKUP_FILE, "r+b");
    settings_backup_v1_t legacy_header; uint8_t legacy_ext[7];
    assert(saved && fread(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fread(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    saved_profile.home_full_refresh |= 64;
    uint32_t legacy_hash = backup_profile_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2], &saved_profile);
    for (int i=0;i<4;++i) saved_profile.checksum[i]=(uint8_t)(legacy_hash>>(8*i));
    assert(fseek(saved, sizeof(legacy_header) + sizeof(legacy_ext), SEEK_SET) == 0);
    assert(fwrite(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    assert(fclose(saved) == 0);
    memset(&saved_wifi, 0, sizeof(saved_wifi));
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7 +
                            sizeof(settings_backup_profile_t) +
                            offsetof(settings_backup_wifi_t, credentials.password), SEEK_SET) == 0);
    assert(fputc('X', saved) != EOF && fclose(saved) == 0);
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(!saved_wifi.configured && wifi_imports == 0);
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7 +
                            sizeof(settings_backup_profile_t), SEEK_SET) == 0);
    assert(fwrite(&saved_network, 1, sizeof(saved_network), saved) == sizeof(saved_network));
    assert(fclose(saved) == 0);
    s_system_size = 120;
    s_book_px = 48;
    s_book_tracking = 2;
    s_book_indent = 0;
    s_book_rule_offset = 4;
    s_reader_full_pages = 15;
    s_reader_turn_effect = 0;
    s_reader_power_turn = false;
    s_reader_immersive = false;
    s_shelf_style = 2;
    s_staged_shutdown = false;
    s_auto_lock_minutes = 0;
    s_home_full_refresh = false;
    s_ble_turner = false;
    s_reader_hold_refresh = false;
    app_settings_set_reader_vertical_turn(false);

    strlcpy(s_device_name, "Pico", sizeof(s_device_name));
    s_status_signature[0] = 0;
    s_avatar[0] = 0;
    s_lock_style = 0;
    s_wallpaper[0] = 0;
    const int prior_restore_commits = commit_count;
    assert(app_settings_backup_restore() == ESP_OK);
    assert(history_restores == 1);
    assert(app_settings_system_font_size() == 200);
    assert(wifi_imports == 1 && saved_wifi.configured &&
           !strcmp(saved_wifi.ssid, "Home_2.4G") &&
           !strcmp(saved_wifi.password, "password123"));
    assert(s_book_px == 62 && s_book_tracking == 4 && s_book_indent == 3 &&
           s_book_rule_offset == 7 && s_reader_full_pages == 5);
    assert(s_reader_turn_effect == 1 && s_reader_power_turn && s_reader_immersive && s_shelf_style == 5);
    assert(s_staged_shutdown&&app_settings_auto_lock_minutes()==5);
    assert(s_ble_turner);
    assert(s_reader_hold_refresh);
    assert(app_settings_reader_vertical_turn());

    assert(s_home_full_refresh && !strcmp(s_device_name, "Kiiko Pico") &&
           !strcmp(s_status_signature, "今天也要读书"));
    assert(!s_avatar[0]); /* Missing avatar falls back to the default mark. */
    assert(!s_font[0]); /* Missing external font falls back to built-in. */
    assert(s_lock_style == 0 && !s_wallpaper[0]); /* Missing wallpaper uses ticket. */
    assert(commit_count == prior_restore_commits + 1); /* Restore commits exactly once. */
    app_settings_set_reader_full_pages(30);
    assert(s_reader_full_pages == 30);
    app_settings_set_reader_full_pages(0);
    assert(s_reader_full_pages == 0);
    app_settings_set_book_reading_line_offset(-8);
    assert(app_settings_book_reading_line_offset() == -8);
    app_settings_set_book_reading_line_offset(7); /* Reject odd-pixel shifts. */
    assert(app_settings_book_reading_line_offset() == -8);

    s_book_px = 70;
    assert(app_settings_backup_save() == ESP_OK); /* Overwrite an existing backup. */
    s_book_px = 48;
    commit_fails = true;
    strlcpy(saved_wifi.ssid, "Other_2.4G", sizeof(saved_wifi.ssid));
    assert(app_settings_backup_restore() == ESP_FAIL);
    assert(s_book_px == 48); /* RAM state is unchanged on commit failure. */
    assert(!strcmp(saved_wifi.ssid, "Other_2.4G"));
    commit_fails = false;
    assert(app_settings_backup_restore() == ESP_OK && s_book_px == 70 &&
           s_reader_full_pages == 0 && app_settings_book_reading_line_offset() == -8 &&
           s_staged_shutdown);
    assert(!strcmp(saved_wifi.ssid, "Home_2.4G"));

    /* PICOSET3 backups predate the shutdown choice and restore the safe off default. */
    FILE *file = fopen(BACKUP_FILE, "rb");
    assert(file);
    settings_backup_v1_t legacy;
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET3", sizeof(legacy.magic));
    backup_seal(&legacy);
    uint8_t v3_extension[6] = {3, 4};
    uint32_t v3_hash = backup_rule_offset_checksum(&legacy, v3_extension[0], v3_extension[1]);
    for (int i = 0; i < 4; ++i) v3_extension[i + 2] = (uint8_t)(v3_hash >> (i * 8));
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fwrite(v3_extension, 1, sizeof(v3_extension), file) == sizeof(v3_extension));
    assert(fclose(file) == 0);
    assert(app_settings_backup_restore() == ESP_OK && !s_staged_shutdown);

    assert(!s_home_full_refresh && !strcmp(s_device_name, "Pico") && !s_status_signature[0]);
    assert(!strcmp(saved_wifi.ssid, "Home_2.4G")); /* Old backups leave network alone. */

    /* Existing PICOSET2 backups remain readable, with centered guide lines. */
    file = fopen(BACKUP_FILE, "rb");
    assert(file);
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET2", sizeof(legacy.magic));
    backup_seal(&legacy);
    uint8_t old_extension[5] = {3};
    uint32_t old_hash = backup_indent_checksum(&legacy, old_extension[0]);
    for (int i = 0; i < 4; ++i) old_extension[i + 1] = (uint8_t)(old_hash >> (i * 8));
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fwrite(old_extension, 1, sizeof(old_extension), file) == sizeof(old_extension));
    assert(fclose(file) == 0);
    s_book_rule_offset = 0;
    assert(app_settings_backup_restore() == ESP_OK && s_book_rule_offset == 4 && !s_reader_hold_refresh);
    assert(!app_settings_reader_vertical_turn());

    /* Existing PICOSET1 backups remain readable and use the two-em default. */
    file = fopen(BACKUP_FILE, "rb");
    assert(file);
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET1", sizeof(legacy.magic));
    backup_seal(&legacy);
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    s_book_indent = 0;
    assert(app_settings_backup_restore() == ESP_OK && s_book_indent == 2);
    /* A backup without a configured network clears an existing saved network. */
    memset(&saved_wifi, 0, sizeof(saved_wifi));
    assert(app_settings_backup_save() == ESP_OK);
    saved_wifi.configured = 1;
    strlcpy(saved_wifi.ssid, "Temporary", sizeof(saved_wifi.ssid));
    strlcpy(saved_wifi.password, "temporary123", sizeof(saved_wifi.password));
    assert(app_settings_backup_restore() == ESP_OK && !saved_wifi.configured);

    file = fopen(BACKUP_FILE, "r+b");
    assert(file);
    assert(fputc('X', file) != EOF);
    assert(fclose(file) == 0);
    s_book_px = 48;
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(s_book_px == 48);
    assert(remove(BACKUP_FILE) == 0);
    assert(app_settings_backup_restore() == ESP_ERR_NOT_FOUND);
    for (int percent = 100; percent <= 200; percent += 10) {
        app_settings_set_system_font_size((uint8_t)percent);
        assert(app_settings_system_font_size() == percent);
    }
    app_settings_set_system_font_size(210);
    app_settings_set_system_font_size(195);
    assert(app_settings_system_font_size() == 200);
    test_loaded_system_size = 180; s_system_size = 120;
    app_settings_init(); assert(app_settings_system_font_size() == 180);
    test_loaded_system_size = 200; s_system_size = 120;
    app_settings_init(); assert(app_settings_system_font_size() == 200);


    test_loaded_shelf = 5; s_shelf_style = 2;
    app_settings_init(); assert(app_settings_shelf_style() == 5);
    app_settings_set_shelf_style(6); assert(app_settings_shelf_style() == 5);
    for (int style = 1; style <= 5; ++style) {
        app_settings_set_shelf_style((uint8_t)style);
        assert(app_settings_shelf_style() == style);
        s_shelf_style = 0;
        app_settings_init();
        assert(app_settings_shelf_style() == style);
        assert(app_settings_backup_save() == ESP_OK);
        s_shelf_style = 2;
        assert(app_settings_backup_restore() == ESP_OK);
        assert(app_settings_shelf_style() == style);
    }
    int before_shelf_invalid = commit_count;
    app_settings_set_shelf_style(0);
    app_settings_set_shelf_style(6);
    app_settings_set_shelf_style(255);
    assert(app_settings_shelf_style() == 5 && commit_count == before_shelf_invalid);
    test_loaded_shelf = 6; s_shelf_style = 2;
    app_settings_init(); assert(app_settings_shelf_style() == 2);
    // 覆盖原设置及快档的保存、重启加载与整包备份恢复，非法值不能生效。
    // Exercise old/fast settings across save, reload and backup restore; reject invalid values.
    for (uint8_t effect = 0; effect <= 2; ++effect) {
        app_settings_set_reader_turn_effect(effect);
        assert(app_settings_reader_turn_effect() == effect);
        s_reader_turn_effect = 255;
        app_settings_init();
        assert(app_settings_reader_turn_effect() == effect);
        assert(app_settings_backup_save() == ESP_OK);
        s_reader_turn_effect = 0;
        assert(app_settings_backup_restore() == ESP_OK);
        assert(app_settings_reader_turn_effect() == effect);
    }
    int before_invalid = commit_count;
    app_settings_set_reader_turn_effect(3);
    app_settings_set_reader_turn_effect(255);
    assert(app_settings_reader_turn_effect() == 2 && commit_count == before_invalid);
    test_loaded_turn = 3; s_reader_turn_effect = 0;
    app_settings_init(); assert(app_settings_reader_turn_effect() == 0);
    puts("settings backup host test passed (original/fast ripple persistence/reload/backup, clear-list restore, 200% system size)");
    return 0;
}

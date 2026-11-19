/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOOK_PROGRESS_DIR
#define BOOK_PROGRESS_DIR "/flash/progress"
#endif
#define PROGRESS_FILE_MAX (sizeof(BOOK_PROGRESS_DIR) + 15)

static bool progress_key_valid(const char *key) {
    return !strncmp(key, "b_", 2) && strlen(key) == 10 && strspn(key + 2, "0123456789abcdef") == 8;
}

static bool progress_key_matches(const char *key, const uint8_t *data, size_t len) {
    if (!record_valid(data, len)) return false;
    char expected[11];
    make_key((const char *)data + HEADER_SIZE, expected);
    return !strcmp(key, expected);
}

static void slot_path(char path[PROGRESS_FILE_MAX], const char *key, unsigned slot) {
    snprintf(path, PROGRESS_FILE_MAX, BOOK_PROGRESS_DIR "/%.8s.%u", key + 2, slot);
}

static uint32_t record_seal(const uint8_t *data, size_t len) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < len; ++i) hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}

static esp_err_t slot_read(const char *key, unsigned slot, uint8_t *data, size_t *len) {
    char path[PROGRESS_FILE_MAX];
    slot_path(path, key, slot);
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? ESP_ERR_NVS_NOT_FOUND : ESP_FAIL;
    uint8_t bytes[RECORD_CAP + 4];
    size_t size = fread(bytes, 1, sizeof(bytes), file);
    bool complete = fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!complete) return ESP_ERR_INVALID_STATE;
    if (size < HEADER_SIZE + 6 || !progress_key_matches(key, bytes, size - 4) ||
        get32(bytes + size - 4) != record_seal(bytes, size - 4)) return ESP_ERR_INVALID_STATE;
    if (*len < size - 4) return ESP_ERR_INVALID_SIZE;
    *len = size - 4;
    memcpy(data, bytes, *len);
    return ESP_OK;
}

static uint32_t record_order(const uint8_t *data) { return data[3] == 2 ? get32(data + 16) : 0; }

static esp_err_t disk_read(const char *key, uint8_t *data, size_t *len, unsigned *slot) {
    if (!progress_key_valid(key)) return ESP_ERR_INVALID_ARG;
    size_t first_len = *len;
    esp_err_t first = slot_read(key, 0, data, &first_len);
    uint8_t second_data[RECORD_CAP];
    size_t second_len = sizeof(second_data);
    esp_err_t second = slot_read(key, 1, second_data, &second_len);
    if (second == ESP_OK && (first != ESP_OK || record_order(second_data) > record_order(data))) {
        if (*len < second_len) return ESP_ERR_INVALID_SIZE;
        memcpy(data, second_data, second_len);
        *len = second_len;
        if (slot) *slot = 1;
        return ESP_OK;
    }
    if (first == ESP_OK) {
        *len = first_len;
        if (slot) *slot = 0;
        return ESP_OK;
    }
    if (first == ESP_FAIL || second == ESP_FAIL) return ESP_FAIL;
    return first == ESP_ERR_NVS_NOT_FOUND ? second : first;
}

static esp_err_t disk_prepare(void) {
    esp_err_t err = book_store_prepare_flash();
    if (err != ESP_OK) return err;
    return mkdir(BOOK_PROGRESS_DIR, 0777) == 0 || errno == EEXIST ? ESP_OK : ESP_FAIL;
}

static bool disk_available(void) {
    return book_store_flash_ready() || book_store_prepare_flash() == ESP_OK;
}

static esp_err_t disk_write(const char *key, const uint8_t *data, size_t len) {
    uint8_t previous[RECORD_CAP];
    size_t previous_len = sizeof(previous);
    unsigned active = 1;
    esp_err_t err = disk_read(key, previous, &previous_len, &active);
    if (err == ESP_OK) {
        if (strcmp((const char *)previous + HEADER_SIZE, (const char *)data + HEADER_SIZE))
            return ESP_ERR_INVALID_STATE;
        if (previous_len == len && !memcmp(previous, data, len)) return ESP_OK;
    } else if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_INVALID_STATE) return err;

    // Commit to the inactive slot while keeping the last checked copy readable.
    unsigned slot = 1 - active;
    char path[PROGRESS_FILE_MAX];
    slot_path(path, key, slot);
    FILE *file = fopen(path, "wb");
    if (!file) return ESP_FAIL;
    uint8_t seal[4];
    put32(seal, record_seal(data, len));
    bool ok = fwrite(data, 1, len, file) == len && fwrite(seal, 1, sizeof(seal), file) == sizeof(seal) &&
              fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    previous_len = sizeof(previous);
    if (!ok || slot_read(key, slot, previous, &previous_len) != ESP_OK ||
        previous_len != len || memcmp(data, previous, len)) {
        (void)unlink(path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t disk_erase(const char *key) {
    if (!book_store_flash_ready()) return ESP_ERR_INVALID_STATE;
    for (unsigned slot = 0; slot < 2; ++slot) {
        char path[PROGRESS_FILE_MAX];
        slot_path(path, key, slot);
        if (unlink(path) != 0 && errno != ENOENT) return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t disk_walk(book_progress_record_fn visit, void *ctx, bool clear) {
    if (!disk_available()) return clear ? ESP_ERR_INVALID_STATE : ESP_OK;
    DIR *dir = opendir(BOOK_PROGRESS_DIR);
    if (!dir) return !book_store_flash_ready() || errno == ENOENT ? ESP_OK : ESP_FAIL;
    esp_err_t err = ESP_OK;
    for (struct dirent *ent; err == ESP_OK && (ent = readdir(dir));) {
        const char *name = ent->d_name;
        if (strlen(name) != 10 || strspn(name, "0123456789abcdefABCDEF") != 8 ||
            name[8] != '.' || (name[9] != '0' && name[9] != '1')) continue;
        char key[11] = "b_";
        for (unsigned i = 0; i < 8; ++i) key[2+i] = (char)tolower((unsigned char)name[i]);
        uint8_t data[RECORD_CAP];
        size_t len = sizeof(data);
        if (clear) {
            char path[PROGRESS_FILE_MAX];
            slot_path(path, key, (unsigned)(name[9] - '0'));
            if (unlink(path) != 0 && errno != ENOENT) err = ESP_FAIL;
            continue;
        }
        bool has_first = slot_read(key, 0, data, &len) == ESP_OK;
        if (has_first != (name[9] == '0')) continue;
        len = sizeof(data);
        if (disk_read(key, data, &len, NULL) == ESP_OK) err = visit(key, data, len, ctx);
    }
    closedir(dir);
    return err;
}

void book_progress_prepare(void) {
    static bool migrated;
    if (migrated || disk_prepare() != ESP_OK) return;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return;
    unsigned count = 0;
    nvs_iterator_t it = NULL;
    for (err = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_BLOB, &it);
         err == ESP_OK; err = nvs_entry_next(&it)) {
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err != ESP_OK) break;
        uint8_t old[RECORD_CAP], current[RECORD_CAP];
        size_t len = sizeof(old), current_len = sizeof(current);
        if (nvs_get_blob(h, info.key, old, &len) != ESP_OK || !progress_key_matches(info.key, old, len)) continue;
        err = disk_read(info.key, current, &current_len, NULL);
        if (err == ESP_OK && strcmp((const char *)old + HEADER_SIZE, (const char *)current + HEADER_SIZE))
            err = ESP_ERR_INVALID_STATE;
        if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_STATE ||
            (err == ESP_OK && record_order(current) < record_order(old)))
            err = disk_write(info.key, old, len);
        if (err != ESP_OK) break;
        err = nvs_erase_key(h, info.key);
        if (err != ESP_OK) break;
        ++count;
    }
    nvs_release_iterator(it);
    if (count) {
        esp_err_t commit = nvs_commit(h);
        if (commit != ESP_OK) err = commit;
        ESP_LOGI(TAG, "progress moved from NVS: %u", count);
    }
    nvs_close(h);
    migrated = err == ESP_ERR_NVS_NOT_FOUND;
}

void book_progress_prepare_at_boot(void) {
    // 进度已移出NVS，空闲条目多也必须在显示/字体大分配前保留文件系统资源。
    // Progress lives in files even when NVS has space; mount before large display/font allocations.
    esp_err_t err = book_progress_prepare_storage();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "progress storage startup: %s", esp_err_to_name(err));
        return;
    }
    nvs_stats_t stats;
    if (nvs_get_stats(NULL, &stats) == ESP_OK && stats.available_entries < 64)
        book_progress_prepare();
}

esp_err_t book_progress_prepare_storage(void) { return disk_prepare(); }

esp_err_t book_progress_restore_record(const char *key, const uint8_t *data, size_t len) {
    return progress_key_matches(key, data, len) ? disk_write(key, data, len) : ESP_ERR_INVALID_ARG;
}

esp_err_t book_progress_clear_records(void) {
    esp_err_t err = disk_prepare();
    return err == ESP_OK ? disk_walk(NULL, NULL, true) : err;
}

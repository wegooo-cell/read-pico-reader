/* SPDX-License-Identifier: Apache-2.0
 * 完整备份v29的有标记扩展；兼容文件仍是v25。/ Tagged v29 full-backup extension; interchange stays v25.
 */
typedef struct {
    uint8_t lowghost, covers, margin_y;
    bool has_lowghost, has_covers, has_margin_y;
} settings_backup_extra_t;

static uint32_t backup_extra_hash(const uint8_t *bytes, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static bool backup_extra_write(FILE *file) {
    uint8_t block[12] = {'K', 'R', 'E', 1, s_low_ghost, s_shelf_covers, s_book_margin_y, 0};
    uint32_t hash = backup_extra_hash(block, 8);
    for (unsigned i = 0; i < 4; ++i) block[8+i] = hash >> (8*i);
    return fwrite(block, 1, sizeof(block), file) == sizeof(block);
}

static bool backup_extra_read(FILE *file, unsigned version, settings_backup_extra_t *extra) {
    *extra = (settings_backup_extra_t){.lowghost=s_low_ghost, .covers=s_shelf_covers};
    if (version < 28) return true;
    if (version == 28) {
        // 已交付的本地v28是五字节低残影块；页边距草案用互补的两个字节。
        // Delivered local v28 has a five-byte low-ghost block; the margin draft uses two complements.
        uint8_t first[2];
        if (fread(first, 1, 2, file) != 2) return false;
        if (first[0] <= 48 && (first[0] ^ first[1]) == 255) {
            extra->margin_y = first[0]; extra->has_margin_y = true;
            return true;
        }
        settings_backup_hold_t legacy = {.action=first[0], .checksum={first[1]}};
        if (fread(legacy.checksum+1, 1, 3, file) != 3 || legacy.action > 1 || !backup_hold_valid(&legacy)) return false;
        // 未发布的封面草案也用了同一布局，不能由这些字节推断封面开关。
        // The unpublished cover draft used identical bytes: never infer its cover switch here.
        extra->lowghost = legacy.action; extra->has_lowghost = true;
        return true;
    }
    uint8_t block[12];
    if (version != 29 || fread(block, 1, sizeof(block), file) != sizeof(block) ||
        memcmp(block, "KRE\1", 4) || block[4] > 1 || block[5] > 1 || block[6] > 48 || block[7]) return false;
    uint32_t hash = 0;
    for (unsigned i = 0; i < 4; ++i) hash |= (uint32_t)block[8+i] << (8*i);
    if (hash != backup_extra_hash(block, 8)) return false;
    *extra = (settings_backup_extra_t){block[4], block[5], block[6], true, true, true};
    return true;
}

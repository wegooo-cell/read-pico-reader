/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：有界、全路径核验的追加式收录索引；坏尾部只读回退，不改原文件。
 * English: Bounded full-path append journal; invalid tails fall back read-only without touching books.
 * 冻结：只写隐藏索引，不格式化、不改书籍或NVS；索引失败不能隐藏书籍。
 * Frozen: write only the hidden index, never format/edit books/NVS or hide books after index failure.
 */
#include "book_added.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef BOOK_ADDED_SD_ROOT
#define BOOK_ADDED_SD_ROOT "/sdcard"
#endif
#ifndef BOOK_ADDED_FLASH_ROOT
#define BOOK_ADDED_FLASH_ROOT "/flash"
#endif
#define ADDED_PATH_MAX 288
#define ADDED_RECORD_MAX 65536u
#define ADDED_FILE_MAX (24u * 1024u * 1024u)
static uint64_t s_last_order;

static const char *medium(const char *path) {
    static const char *roots[] = {BOOK_ADDED_SD_ROOT, BOOK_ADDED_FLASH_ROOT};
    if (!path) return NULL;
    for (unsigned i = 0; i < 2; ++i) {
        size_t n = strlen(roots[i]);
        if (!strncmp(path, roots[i], n) && path[n] == '/' && path[n + 1]) return roots[i];
    }
    return NULL;
}
static bool journal_path(const char *root, char out[ADDED_PATH_MAX]) {
    return root && snprintf(out, ADDED_PATH_MAX, "%s/.kiiko-added-v1", root) < ADDED_PATH_MAX;
}
static uint32_t checksum(const uint8_t *data, size_t size, uint32_t hash) {
    for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}
static void put64(uint8_t *out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out[i] = (uint8_t)(value >> (i * 8));
}
static uint64_t get64(const uint8_t *in) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)in[i] << (i * 8);
    return value;
}
static int compare_items(const void *left, const void *right) {
    return strcmp(((const book_added_item_t *)left)->path, ((const book_added_item_t *)right)->path);
}
// 长度、根目录和校验全部通过才使用记录；重复路径以最后一次记录为准。
// Accept records only after length/root/checksum checks; the latest record wins for repeated paths.
static bool load_root(const char *root, book_added_item_t *items, size_t count, unsigned *remaining) {
    if(remaining)*remaining=0;
    char path[ADDED_PATH_MAX];
    if (!journal_path(root, path)) return false;
    struct stat st;
    #ifdef ESP_PLATFORM
    int status=stat(path,&st);
#else
    int status=lstat(path,&st);
#endif
    if (status == 0 && (!S_ISREG(st.st_mode) || (st.st_size < 0 || st.st_size > ADDED_FILE_MAX))) return false;
    FILE *file = fopen(path, "rb");
    if (!file) { bool missing=errno==ENOENT; if(missing&&remaining)*remaining=ADDED_RECORD_MAX; return missing; }
    // 顺序索引使用有界16KiB缓冲，减少TF的小块往返；分配失败沿用默认缓冲。
    // Bound sequential journal buffering to16KiB; failed allocation retains stdio's default.
    (void)setvbuf(file,NULL,_IOFBF,16384);
    bool valid = true;
    unsigned records = 0;
    for (;;) {
        uint8_t header[20] = {0};
        size_t n = fread(header, 1, sizeof(header), file);
        if (!n && feof(file) && !ferror(file)) break;
        unsigned length = header[4] | ((unsigned)header[5] << 8);
        if (n != sizeof(header) || memcmp(header, "KAD1", 4) || !length ||
            length >= ADDED_PATH_MAX || ++records > ADDED_RECORD_MAX) { valid = false; break; }
        char identity[ADDED_PATH_MAX];
        if (fread(identity, 1, length, file) != length) { valid = false; break; }
        identity[length] = 0;
        uint32_t expected = 0;
        for (unsigned i = 0; i < 4; ++i) expected |= (uint32_t)header[16 + i] << (i * 8);
        uint32_t actual = checksum((uint8_t *)identity, length, checksum(header, 16, 2166136261u));
        if (memchr(identity, 0, length) || (!medium(identity) || strcmp(medium(identity), root)) || expected != actual) { valid = false; break; }
        uint64_t order = get64(header + 8);
        if (order > s_last_order) s_last_order = order;
        book_added_item_t key = {.path = identity};
        book_added_item_t *found = bsearch(&key, items, count, sizeof(*items), compare_items);
        if (found && found->order) *found->order = order;
    }
    if (fclose(file)) valid = false;
    if(valid&&remaining)*remaining=ADDED_RECORD_MAX-records;
    return valid && records < ADDED_RECORD_MAX;
}
static bool append(FILE *file, const char *path, uint64_t order) {
    size_t length = strlen(path);
    if (!length || length >= ADDED_PATH_MAX) return false;
    uint8_t header[20] = {'K','A','D','1',(uint8_t)length,(uint8_t)(length >> 8),0,0};
    put64(header + 8, order);
    uint32_t hash = checksum((const uint8_t *)path, length, checksum(header, 16, 2166136261u));
    for (unsigned i = 0; i < 4; ++i) header[16 + i] = (uint8_t)(hash >> (i * 8));
    return fwrite(header, 1, sizeof(header), file) == sizeof(header) && fwrite(path, 1, length, file) == length;
}
static bool close_written(FILE *file) {
    bool ok = fflush(file) == 0;
    if (ok && fsync(fileno(file))) ok = false;
    if (fclose(file)) ok = false;
    return ok;
}
void book_added_load(book_added_item_t *items, size_t count) {
    if (!items || !count || count > ADDED_RECORD_MAX) return;
    for (size_t i = 0; i < count; ++i) if (!items[i].path || !items[i].order) return;
    qsort(items, count, sizeof(*items), compare_items);
    static const char *roots[] = {BOOK_ADDED_SD_ROOT, BOOK_ADDED_FLASH_ROOT};
    bool writable[2];unsigned remaining[2]={0};
    for (unsigned r = 0; r < 2; ++r) writable[r] = load_root(roots[r], items, count,&remaining[r]);
    time_t now = time(NULL);
    uint64_t clock_order = now >= 1577836800 ? (uint64_t)now << 16 : 0;
    if (clock_order > s_last_order) s_last_order = clock_order;
    for (unsigned r = 0; r < 2; ++r) {
        if (!writable[r]) continue;
        FILE *file = NULL;
        for (size_t i = 0; i < count; ++i) {
            if (!remaining[r])break;
            if (!items[i].order || *items[i].order || (!medium(items[i].path) || strcmp(medium(items[i].path), roots[r]))) continue;
            if (!file) {
                char path[ADDED_PATH_MAX];
                if (!journal_path(roots[r], path) || !(file = fopen(path, "ab"))) break;
                (void)setvbuf(file,NULL,_IOFBF,16384);
            }
            if (s_last_order == UINT64_MAX || !append(file, items[i].path, s_last_order + 1)) break;
            *items[i].order = ++s_last_order;--remaining[r];
        }
        if (file) (void)close_written(file);
    }
}
static void write_identity(const char *path, uint64_t order) {
    const char *root = medium(path);
    char journal[ADDED_PATH_MAX];
    if (!journal_path(root, journal)) return;
    uint64_t existing = 0;
    book_added_item_t item = {.path=path,.order=&existing};
    unsigned remaining=0;
    if (!load_root(root, &item, 1,&remaining)||!remaining) return;
    FILE *file = fopen(journal, "ab");
    if (file) { (void)append(file, path, order); (void)close_written(file); }
}
void book_added_migrate(const char *old_path, const char *new_path) {
    const char *root = medium(old_path);
    if (!root || !medium(new_path)) return;
    uint64_t order = 0;
    book_added_item_t item = {.path=old_path,.order=&order};
    if (load_root(root, &item, 1,NULL) && order) write_identity(new_path, order);
}
void book_added_forget(const char *path) { write_identity(path, 0); }
